package com.cytarch.cellularity.platform.render

import android.os.Debug
import com.cytarch.cellularity.BuildConfig
import java.util.Locale
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicLongArray
import kotlin.math.ceil

/**
 * Diagnostic-only counters for the managed/native render boundary.
 *
 * Event recording is deliberately limited to primitive atomics and a bounded
 * preallocated duration ring. Formatting and array copies happen only when a
 * report is requested. Release builds compile the callers off through
 * [BuildConfig.ENABLE_RENDER_PROFILING], allowing R8 to remove this object.
 */
internal object RenderBoundaryTelemetry {
    private const val SAMPLE_CAPACITY = 2_048
    private const val JNI_CALL_KIND_COUNT = 14
    private const val BACKEND_COUNT = 2
    private const val GLES_BACKEND_INDEX = 0
    private const val VULKAN_BACKEND_INDEX = 1
    private const val BYTES_PER_FLOAT = 4L
    private const val UNAVAILABLE_ALLOCATION_COUNT = -1L
    private const val MANAGED_ALLOCATION_RUNTIME_STAT = "art.gc.bytes-allocated"

    private val jniMetricNames = arrayOf(
        "vulkan_direct_render",
        "draw_microscope",
        "clear_microscope",
        "draw_genome_preview",
        "draw_environment_preview",
        "runtime_publication_poll",
        "viewport_snapshot",
        "environment_snapshot",
        "apply_environment_patch",
        "update_genome_preview",
        "genome_preview_control",
        "capture_genome_preview",
        "capture_substrate_preview",
        "microscope_input_command"
    )

    private val frameJniKinds = intArrayOf(
        RenderPerformanceTelemetry.JNI_VULKAN_DIRECT_RENDER,
        RenderPerformanceTelemetry.JNI_DRAW_MICROSCOPE,
        RenderPerformanceTelemetry.JNI_CLEAR_MICROSCOPE,
        RenderPerformanceTelemetry.JNI_DRAW_GENOME_PREVIEW,
        RenderPerformanceTelemetry.JNI_DRAW_ENVIRONMENT_PREVIEW,
        RenderPerformanceTelemetry.JNI_RUNTIME_PUBLICATION_POLL,
        RenderPerformanceTelemetry.JNI_ENVIRONMENT_SNAPSHOT,
        RenderPerformanceTelemetry.JNI_APPLY_ENVIRONMENT_PATCH
    )

    private val counters = BoundaryAccumulator(SAMPLE_CAPACITY)
    private val managedAllocatedBytesAtReset = AtomicLong(UNAVAILABLE_ALLOCATION_COUNT)

    fun reset() {
        if (!BuildConfig.ENABLE_RENDER_PROFILING) return
        counters.reset()
        managedAllocatedBytesAtReset.set(readManagedAllocatedBytes())
    }

    fun recordJniCall(kind: Int) {
        if (kind !in 0 until JNI_CALL_KIND_COUNT) return
        counters.jniCalls.incrementAndGet(kind)
    }

    fun recordRuntimePublicationPoll(successful: Boolean) {
        counters.runtimePublicationPolls.incrementAndGet()
        if (successful) counters.runtimePublicationSuccesses.incrementAndGet()
    }

    fun recordViewportSnapshot(floatCount: Int) {
        if (floatCount < 0) return
        counters.viewportSnapshotAllocations.incrementAndGet()
        counters.viewportSnapshotCopyBytes.addAndGet(floatCount.toLong() * BYTES_PER_FLOAT)
    }

    fun recordRenderTaskQueued(backend: RenderBackendKind) {
        counters.renderTasksQueued.incrementAndGet(backendIndex(backend))
    }

    fun recordRenderTaskExecuted(
        backend: RenderBackendKind,
        drainedBeforeFrame: Boolean
    ) {
        val index = backendIndex(backend)
        counters.renderTasksExecuted.incrementAndGet(index)
        if (drainedBeforeFrame) counters.renderTasksPreFrameDrained.incrementAndGet(index)
    }

    fun recordInputEvent() {
        counters.inputEvents.incrementAndGet()
    }

    fun recordSemanticInputCommands(count: Int) {
        if (count <= 0) return
        counters.semanticInputCommands.addAndGet(count.toLong())
    }

    fun recordFullDiagnostics(durationNanos: Long, payloadBytes: Int) {
        counters.fullDiagnosticsBuilds.incrementAndGet()
        counters.fullDiagnosticsTransfers.incrementAndGet()
        if (payloadBytes > 0) {
            counters.fullDiagnosticsTransferBytes.addAndGet(payloadBytes.toLong())
        }
        counters.fullDiagnosticsDurations.add(durationNanos.coerceAtLeast(0L))
    }

    fun snapshot(): BoundarySnapshot {
        // Read the process counter before allocating report arrays/objects so
        // the profiler does not charge its own formatting work to the sample.
        val currentManagedAllocatedBytes = readManagedAllocatedBytes()
        val allocationBaseline = managedAllocatedBytesAtReset.get()
        val managedAllocatedBytesDelta = if (
            currentManagedAllocatedBytes >= 0L &&
            allocationBaseline >= 0L &&
            currentManagedAllocatedBytes >= allocationBaseline
        ) {
            currentManagedAllocatedBytes - allocationBaseline
        } else {
            UNAVAILABLE_ALLOCATION_COUNT
        }
        return counters.snapshot(managedAllocatedBytesDelta)
    }

    private fun backendIndex(backend: RenderBackendKind): Int = when (backend) {
        RenderBackendKind.OPEN_GL_ES -> GLES_BACKEND_INDEX
        RenderBackendKind.VULKAN -> VULKAN_BACKEND_INDEX
    }

    private fun readManagedAllocatedBytes(): Long {
        if (!BuildConfig.ENABLE_RENDER_PROFILING) return UNAVAILABLE_ALLOCATION_COUNT
        return runCatching {
            Debug.getRuntimeStat(MANAGED_ALLOCATION_RUNTIME_STAT)?.toLongOrNull()
        }.getOrNull() ?: UNAVAILABLE_ALLOCATION_COUNT
    }

    private class BoundaryAccumulator(sampleCapacity: Int) {
        val jniCalls = AtomicLongArray(JNI_CALL_KIND_COUNT)
        val runtimePublicationPolls = AtomicLong()
        val runtimePublicationSuccesses = AtomicLong()
        val viewportSnapshotAllocations = AtomicLong()
        val viewportSnapshotCopyBytes = AtomicLong()
        val renderTasksQueued = AtomicLongArray(BACKEND_COUNT)
        val renderTasksExecuted = AtomicLongArray(BACKEND_COUNT)
        val renderTasksPreFrameDrained = AtomicLongArray(BACKEND_COUNT)
        val inputEvents = AtomicLong()
        val semanticInputCommands = AtomicLong()
        val fullDiagnosticsBuilds = AtomicLong()
        val fullDiagnosticsTransfers = AtomicLong()
        val fullDiagnosticsTransferBytes = AtomicLong()
        val fullDiagnosticsDurations = BoundaryLongSampleRing(sampleCapacity)

        fun reset() {
            reset(jniCalls)
            runtimePublicationPolls.set(0L)
            runtimePublicationSuccesses.set(0L)
            viewportSnapshotAllocations.set(0L)
            viewportSnapshotCopyBytes.set(0L)
            reset(renderTasksQueued)
            reset(renderTasksExecuted)
            reset(renderTasksPreFrameDrained)
            inputEvents.set(0L)
            semanticInputCommands.set(0L)
            fullDiagnosticsBuilds.set(0L)
            fullDiagnosticsTransfers.set(0L)
            fullDiagnosticsTransferBytes.set(0L)
            fullDiagnosticsDurations.reset()
        }

        fun snapshot(managedAllocatedBytesDelta: Long): BoundarySnapshot = BoundarySnapshot(
            jniCalls = snapshot(jniCalls),
            runtimePublicationPolls = runtimePublicationPolls.get(),
            runtimePublicationSuccesses = runtimePublicationSuccesses.get(),
            viewportSnapshotAllocations = viewportSnapshotAllocations.get(),
            viewportSnapshotCopyBytes = viewportSnapshotCopyBytes.get(),
            renderTasksQueued = snapshot(renderTasksQueued),
            renderTasksExecuted = snapshot(renderTasksExecuted),
            renderTasksPreFrameDrained = snapshot(renderTasksPreFrameDrained),
            inputEvents = inputEvents.get(),
            semanticInputCommands = semanticInputCommands.get(),
            fullDiagnosticsBuilds = fullDiagnosticsBuilds.get(),
            fullDiagnosticsTransfers = fullDiagnosticsTransfers.get(),
            fullDiagnosticsTransferBytes = fullDiagnosticsTransferBytes.get(),
            fullDiagnosticsDurations = fullDiagnosticsDurations.snapshot(),
            managedAllocatedBytesDelta = managedAllocatedBytesDelta
        )

        private fun reset(values: AtomicLongArray) {
            for (index in 0 until values.length()) values.set(index, 0L)
        }

        private fun snapshot(values: AtomicLongArray): LongArray =
            LongArray(values.length()) { index -> values.get(index) }
    }

    internal data class BoundarySnapshot(
        val jniCalls: LongArray,
        val runtimePublicationPolls: Long,
        val runtimePublicationSuccesses: Long,
        val viewportSnapshotAllocations: Long,
        val viewportSnapshotCopyBytes: Long,
        val renderTasksQueued: LongArray,
        val renderTasksExecuted: LongArray,
        val renderTasksPreFrameDrained: LongArray,
        val inputEvents: Long,
        val semanticInputCommands: Long,
        val fullDiagnosticsBuilds: Long,
        val fullDiagnosticsTransfers: Long,
        val fullDiagnosticsTransferBytes: Long,
        val fullDiagnosticsDurations: BoundaryDistributionSnapshot,
        val managedAllocatedBytesDelta: Long
    ) {
        fun encode(frameCallbackCount: Long): String = buildString {
            appendLine("boundary_schema=1")
            var totalJniCalls = 0L
            for (index in jniCalls.indices) {
                val count = jniCalls[index]
                totalJniCalls += count
                appendLine("boundary_jni_${jniMetricNames[index]}_count=$count")
            }
            var frameJniCalls = 0L
            for (kind in frameJniKinds) frameJniCalls += jniCalls[kind]
            val inputJniCalls = jniCalls[
                RenderPerformanceTelemetry.JNI_MICROSCOPE_INPUT_COMMAND
            ]
            appendLine("boundary_jni_total_count=$totalJniCalls")
            appendLine("boundary_frame_jni_call_count=$frameJniCalls")
            appendLine("boundary_native_input_jni_call_count=$inputJniCalls")
            appendLine(
                "boundary_frame_jni_calls_per_callback=" +
                    ratio(frameJniCalls, frameCallbackCount)
            )
            appendLine("boundary_runtime_publication_poll_count=$runtimePublicationPolls")
            appendLine(
                "boundary_runtime_publication_success_count=$runtimePublicationSuccesses"
            )
            appendLine(
                "boundary_viewport_snapshot_allocation_count=$viewportSnapshotAllocations"
            )
            appendLine("boundary_viewport_snapshot_copy_bytes=$viewportSnapshotCopyBytes")
            appendBackendTasks("gles", GLES_BACKEND_INDEX)
            appendBackendTasks("vulkan", VULKAN_BACKEND_INDEX)
            appendLine("boundary_input_event_count=$inputEvents")
            appendLine("boundary_semantic_input_command_count=$semanticInputCommands")
            appendLine("boundary_full_diagnostics_build_count=$fullDiagnosticsBuilds")
            appendLine("boundary_full_diagnostics_transfer_count=$fullDiagnosticsTransfers")
            appendLine(
                "boundary_full_diagnostics_transfer_bytes=$fullDiagnosticsTransferBytes"
            )
            appendDistribution("boundary_full_diagnostics", fullDiagnosticsDurations)
            appendLine(
                "boundary_managed_allocated_bytes_delta=$managedAllocatedBytesDelta"
            )
            appendLine(
                "boundary_managed_allocated_bytes_per_frame=" +
                    ratioOrUnavailable(managedAllocatedBytesDelta, frameCallbackCount)
            )
            appendLine(
                "boundary_managed_allocated_bytes_per_input_event=" +
                    ratioOrUnavailable(managedAllocatedBytesDelta, inputEvents)
            )
        }

        private fun StringBuilder.appendBackendTasks(name: String, index: Int) {
            appendLine(
                "boundary_${name}_render_task_queued_count=${renderTasksQueued[index]}"
            )
            appendLine(
                "boundary_${name}_render_task_executed_count=${renderTasksExecuted[index]}"
            )
            appendLine(
                "boundary_${name}_render_task_pre_frame_drain_count=" +
                    renderTasksPreFrameDrained[index]
            )
            // The characterized worker queues every Task independently. This
            // explicit zero will become a measured value only when coalescing
            // is introduced by a later migration stage.
            appendLine("boundary_${name}_render_task_coalesced_count=0")
        }

        private fun StringBuilder.appendDistribution(
            prefix: String,
            distribution: BoundaryDistributionSnapshot
        ) {
            appendLine("${prefix}_sample_count=${distribution.sampleCount}")
            appendLine("${prefix}_p50_ns=${distribution.p50}")
            appendLine("${prefix}_p95_ns=${distribution.p95}")
            appendLine("${prefix}_p99_ns=${distribution.p99}")
            appendLine("${prefix}_max_ns=${distribution.maximum}")
        }

        private fun ratio(numerator: Long, denominator: Long): String =
            if (denominator > 0L) {
                String.format(
                    Locale.ROOT,
                    "%.6f",
                    numerator.toDouble() / denominator.toDouble()
                )
            } else {
                "0.000000"
            }

        private fun ratioOrUnavailable(numerator: Long, denominator: Long): String =
            if (numerator < 0L || denominator <= 0L) "-1" else ratio(numerator, denominator)
    }

    private class BoundaryLongSampleRing(capacity: Int) {
        private val values = LongArray(capacity)
        private var next = 0
        private var count = 0

        @Synchronized
        fun reset() {
            next = 0
            count = 0
        }

        @Synchronized
        fun add(value: Long) {
            values[next] = value
            next = (next + 1) % values.size
            if (count < values.size) count += 1
        }

        @Synchronized
        fun snapshot(): BoundaryDistributionSnapshot {
            if (count == 0) return BoundaryDistributionSnapshot()
            val sorted = LongArray(count)
            val first = if (count == values.size) next else 0
            for (index in 0 until count) {
                sorted[index] = values[(first + index) % values.size]
            }
            sorted.sort()
            return BoundaryDistributionSnapshot(
                sampleCount = count,
                p50 = percentile(sorted, 0.50),
                p95 = percentile(sorted, 0.95),
                p99 = percentile(sorted, 0.99),
                maximum = sorted.last()
            )
        }

        private fun percentile(sorted: LongArray, percentile: Double): Long {
            val rank = ceil(percentile * sorted.size).toInt().coerceIn(1, sorted.size)
            return sorted[rank - 1]
        }
    }

    internal data class BoundaryDistributionSnapshot(
        val sampleCount: Int = 0,
        val p50: Long = 0L,
        val p95: Long = 0L,
        val p99: Long = 0L,
        val maximum: Long = 0L
    )
}
