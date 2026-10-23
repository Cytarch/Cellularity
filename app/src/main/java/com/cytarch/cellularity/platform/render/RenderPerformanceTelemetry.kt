package com.cytarch.cellularity.platform.render

import com.cytarch.cellularity.BuildConfig
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import kotlin.math.ceil

/**
 * Opt-in, process-local render profiling shared by GLES and Vulkan.
 *
 * Release builds compile the managed recorder off. Diagnostic builds keep
 * recording disabled by default and enable it through the ADB profiling
 * receiver; samples are never persisted or used to tune runtime policy.
 */
internal object RenderPerformanceTelemetry {
    private const val SAMPLE_CAPACITY = 2_048

    const val JNI_VULKAN_DIRECT_RENDER = 0
    const val JNI_DRAW_MICROSCOPE = 1
    const val JNI_CLEAR_MICROSCOPE = 2
    const val JNI_DRAW_GENOME_PREVIEW = 3
    const val JNI_DRAW_ENVIRONMENT_PREVIEW = 4
    const val JNI_RUNTIME_PUBLICATION_POLL = 5
    const val JNI_VIEWPORT_SNAPSHOT = 6
    const val JNI_ENVIRONMENT_SNAPSHOT = 7
    const val JNI_APPLY_ENVIRONMENT_PATCH = 8
    const val JNI_UPDATE_GENOME_PREVIEW = 9
    const val JNI_GENOME_PREVIEW_CONTROL = 10
    const val JNI_CAPTURE_GENOME_PREVIEW = 11
    const val JNI_CAPTURE_SUBSTRATE_PREVIEW = 12
    const val JNI_MICROSCOPE_INPUT_COMMAND = 13

    private val available = BuildConfig.ENABLE_RENDER_PROFILING
    private val recording = AtomicBoolean(false)
    private val resetGeneration = AtomicLong(0L)
    private val configurationLock = Any()
    private val gles by lazy { BackendAccumulator("gles", SAMPLE_CAPACITY) }
    private val vulkan by lazy { BackendAccumulator("vulkan", SAMPLE_CAPACITY) }
    private val nativeSnapshotBuffer by lazy {
        NativeRenderProfilingLayout.allocateBuffer()
    }

    val enabled: Boolean
        get() = available && recording.get()

    @JvmStatic
    fun isBoundaryRecordingEnabled(): Boolean = enabled

    val generation: Long
        get() = resetGeneration.get()

    fun setEnabled(enabled: Boolean, reset: Boolean = false) {
        if (!available) return
        transition(enabled, reset)
    }

    fun reset() {
        if (!available) return
        transition(recording.get(), reset = true)
    }

    fun recordFrameRequest(backend: RenderBackendKind, coalesced: Boolean) {
        if (!available || !recording.get()) return
        accumulator(backend).recordRequest(coalesced)
    }

    /** Captures input arrival without calling the clock while profiling is disabled. */
    fun captureInputTimestampNanos(): Long =
        if (available && recording.get()) System.nanoTime() else 0L

    /** Records the oldest accepted camera change waiting to become visible. */
    fun recordCameraInput(backend: RenderBackendKind, inputAtNanos: Long) {
        if (!available || !recording.get() || inputAtNanos <= 0L) return
        if (backend == RenderBackendKind.VULKAN) return
        accumulator(backend).recordCameraInput(
            inputAtNanos = inputAtNanos,
            acceptedAtNanos = System.nanoTime()
        )
    }

    @JvmStatic
    fun recordBoundaryJniCall(kind: Int) {
        if (!available || !recording.get()) return
        RenderBoundaryTelemetry.recordJniCall(kind)
    }

    @JvmStatic
    fun recordRuntimePublicationPoll(successful: Boolean) {
        if (!available || !recording.get()) return
        RenderBoundaryTelemetry.recordRuntimePublicationPoll(successful)
    }

    @JvmStatic
    fun recordViewportSnapshot(floatCount: Int) {
        if (!available || !recording.get()) return
        RenderBoundaryTelemetry.recordViewportSnapshot(floatCount)
    }

    fun recordRenderTaskQueued(backend: RenderBackendKind) {
        if (!available || !recording.get()) return
        RenderBoundaryTelemetry.recordRenderTaskQueued(backend)
    }

    fun recordRenderTaskExecuted(
        backend: RenderBackendKind,
        drainedBeforeFrame: Boolean
    ) {
        if (!available || !recording.get()) return
        RenderBoundaryTelemetry.recordRenderTaskExecuted(backend, drainedBeforeFrame)
    }

    fun recordInputEvent() {
        if (!available || !recording.get()) return
        RenderBoundaryTelemetry.recordInputEvent()
    }

    fun recordSemanticInputCommands(count: Int) {
        if (!available || !recording.get() || count <= 0) return
        RenderBoundaryTelemetry.recordSemanticInputCommands(count)
    }

    @JvmStatic
    fun recordFullDiagnostics(durationNanos: Long, payloadBytes: Int) {
        if (!available || !recording.get()) return
        RenderBoundaryTelemetry.recordFullDiagnostics(durationNanos, payloadBytes)
    }

    fun beginFrame(backend: RenderBackendKind): RenderFrameProfileToken? {
        if (!available || !recording.get()) return null
        val now = System.nanoTime()
        accumulator(backend).recordFrameStarted(now)
        return RenderFrameProfileToken(backend, now)
    }

    fun finishFrame(
        token: RenderFrameProfileToken?,
        outcome: RenderFrameProfileOutcome
    ) {
        if (token == null) return
        val finishedAt = System.nanoTime()
        accumulator(token.backend).recordFrameFinished(
            durationNanos = (finishedAt - token.startedAtNanos).coerceAtLeast(0L),
            finishedAtNanos = finishedAt,
            outcome = outcome
        )
    }

    fun snapshot(): String = synchronized(configurationLock) {
        val boundarySnapshot = RenderBoundaryTelemetry.snapshot()
        val glesSnapshot = gles.snapshot()
        val managedVulkanSnapshot = vulkan.snapshot()
        val vulkanSnapshot = readNativeVulkanSnapshot()
            ?.toBackendSnapshot(managedVulkanSnapshot)
            ?: managedVulkanSnapshot
        buildString {
            appendLine("Render performance profile")
            appendLine("schema=2")
            appendLine("available=$available")
            appendLine("enabled=${available && recording.get()}")
            append(glesSnapshot.encode())
            append(vulkanSnapshot.encode())
            append(
                boundarySnapshot.encode(
                    frameCallbackCount = glesSnapshot.callbackCount +
                        vulkanSnapshot.callbackCount
                )
            )
        }
    }

    private fun transition(enabled: Boolean, reset: Boolean) =
        synchronized(configurationLock) {
            recording.set(false)
            val currentGeneration = resetGeneration.get()
            val targetGeneration = if (reset) {
                if (currentGeneration == Long.MAX_VALUE) {
                    publishNativeConfiguration(false, currentGeneration)
                    return@synchronized
                }
                currentGeneration + 1L
            } else {
                currentGeneration
            }
            if (reset) {
                gles.reset()
                vulkan.reset()
                RenderBoundaryTelemetry.reset()
            }
            if (!publishNativeConfiguration(enabled, targetGeneration)) {
                return@synchronized
            }
            resetGeneration.set(targetGeneration)
            recording.set(enabled)
        }

    private fun publishNativeConfiguration(
        enabled: Boolean,
        generation: Long
    ): Boolean {
        val result = runCatching {
            NativeRenderProfilingBridge.publishRenderProfilingConfiguration(
                enabled,
                generation
            )
        }.getOrNull() ?: return true // Local JVM tests have no Android DSO.
        return result == NativeRenderProfilingLayout.PUBLISH_APPLIED ||
            result == NativeRenderProfilingLayout.PUBLISH_DUPLICATE
    }

    private fun readNativeVulkanSnapshot(): NativeRenderProfilingSnapshot? {
        repeat(2) {
            nativeSnapshotBuffer.clear()
            val result = runCatching {
                NativeRenderProfilingBridge.readRenderProfilingSnapshot(
                    nativeSnapshotBuffer
                )
            }.getOrNull() ?: return null
            if (result != NativeRenderProfilingLayout.RESULT_READY) return null
            val snapshot = runCatching {
                NativeRenderProfilingLayout.decode(nativeSnapshotBuffer)
            }.getOrNull() ?: return null
            if (snapshot.resetGeneration == resetGeneration.get()) return snapshot
        }
        return null
    }

    private fun accumulator(backend: RenderBackendKind): BackendAccumulator = when (backend) {
        RenderBackendKind.OPEN_GL_ES -> gles
        RenderBackendKind.VULKAN -> vulkan
    }

    private class BackendAccumulator(
        private val name: String,
        sampleCapacity: Int
    ) {
        private val durations = LongSampleRing(sampleCapacity)
        private val intervals = LongSampleRing(sampleCapacity)
        private val cameraLatencies = LongSampleRing(sampleCapacity)
        private val inputToNativeAccept = LongSampleRing(sampleCapacity)
        private val nativeAcceptToPresent = LongSampleRing(sampleCapacity)
        private var requestCount = 0L
        private var coalescedRequestCount = 0L
        private var callbackCount = 0L
        private var presentedCount = 0L
        private var deferredCount = 0L
        private var failedCount = 0L
        private var lastFrameStartedAtNanos = 0L
        private var firstFrameStartedAtNanos = 0L
        private var latestFrameFinishedAtNanos = 0L
        private var pendingCameraInputAtNanos = 0L
        private var pendingCameraNativeAcceptedAtNanos = 0L

        @Synchronized
        fun reset() {
            durations.reset()
            intervals.reset()
            cameraLatencies.reset()
            inputToNativeAccept.reset()
            nativeAcceptToPresent.reset()
            requestCount = 0L
            coalescedRequestCount = 0L
            callbackCount = 0L
            presentedCount = 0L
            deferredCount = 0L
            failedCount = 0L
            lastFrameStartedAtNanos = 0L
            firstFrameStartedAtNanos = 0L
            latestFrameFinishedAtNanos = 0L
            pendingCameraInputAtNanos = 0L
            pendingCameraNativeAcceptedAtNanos = 0L
        }

        @Synchronized
        fun recordRequest(coalesced: Boolean) {
            requestCount += 1L
            if (coalesced) coalescedRequestCount += 1L
        }

        @Synchronized
        fun recordCameraInput(inputAtNanos: Long, acceptedAtNanos: Long) {
            if (pendingCameraInputAtNanos == 0L) {
                pendingCameraInputAtNanos = inputAtNanos
                pendingCameraNativeAcceptedAtNanos = acceptedAtNanos
                inputToNativeAccept.add(
                    (acceptedAtNanos - inputAtNanos).coerceAtLeast(0L)
                )
            }
        }

        @Synchronized
        fun recordFrameStarted(startedAtNanos: Long) {
            if (firstFrameStartedAtNanos == 0L) {
                firstFrameStartedAtNanos = startedAtNanos
            }
            if (lastFrameStartedAtNanos > 0L && startedAtNanos > lastFrameStartedAtNanos) {
                intervals.add(startedAtNanos - lastFrameStartedAtNanos)
            }
            lastFrameStartedAtNanos = startedAtNanos
            callbackCount += 1L
        }

        @Synchronized
        fun recordFrameFinished(
            durationNanos: Long,
            finishedAtNanos: Long,
            outcome: RenderFrameProfileOutcome
        ) {
            durations.add(durationNanos)
            latestFrameFinishedAtNanos = finishedAtNanos
            if (
                outcome == RenderFrameProfileOutcome.PRESENTED &&
                pendingCameraInputAtNanos > 0L &&
                finishedAtNanos >= pendingCameraInputAtNanos
            ) {
                cameraLatencies.add(finishedAtNanos - pendingCameraInputAtNanos)
                if (
                    pendingCameraNativeAcceptedAtNanos > 0L &&
                    finishedAtNanos >= pendingCameraNativeAcceptedAtNanos
                ) {
                    nativeAcceptToPresent.add(
                        finishedAtNanos - pendingCameraNativeAcceptedAtNanos
                    )
                }
                pendingCameraInputAtNanos = 0L
                pendingCameraNativeAcceptedAtNanos = 0L
            }
            when (outcome) {
                RenderFrameProfileOutcome.PRESENTED -> presentedCount += 1L
                RenderFrameProfileOutcome.DEFERRED -> deferredCount += 1L
                RenderFrameProfileOutcome.FAILED -> failedCount += 1L
            }
        }

        @Synchronized
        fun snapshot(): BackendSnapshot {
            val elapsedNanos = if (
                firstFrameStartedAtNanos > 0L &&
                latestFrameFinishedAtNanos >= firstFrameStartedAtNanos
            ) {
                latestFrameFinishedAtNanos - firstFrameStartedAtNanos
            } else {
                0L
            }
            return BackendSnapshot(
                name = name,
                requestCount = requestCount,
                coalescedRequestCount = coalescedRequestCount,
                callbackCount = callbackCount,
                presentedCount = presentedCount,
                deferredCount = deferredCount,
                failedCount = failedCount,
                elapsedNanos = elapsedNanos,
                durations = durations.snapshot(),
                intervals = intervals.snapshot(),
                cameraLatencies = cameraLatencies.snapshot(),
                inputToNativeAccept = inputToNativeAccept.snapshot(),
                nativeAcceptToPresent = nativeAcceptToPresent.snapshot()
            )
        }
    }

    private class LongSampleRing(capacity: Int) {
        private val values = LongArray(capacity)
        private var next = 0
        private var count = 0

        fun reset() {
            next = 0
            count = 0
        }

        fun add(value: Long) {
            values[next] = value
            next = (next + 1) % values.size
            if (count < values.size) count += 1
        }

        fun snapshot(): DistributionSnapshot {
            if (count == 0) return DistributionSnapshot()
            val sorted = LongArray(count)
            val first = if (count == values.size) next else 0
            for (index in 0 until count) {
                sorted[index] = values[(first + index) % values.size]
            }
            sorted.sort()
            return DistributionSnapshot(
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

    private data class DistributionSnapshot(
        val sampleCount: Int = 0,
        val p50: Long = 0L,
        val p95: Long = 0L,
        val p99: Long = 0L,
        val maximum: Long = 0L
    )

    private data class BackendSnapshot(
        val name: String,
        val requestCount: Long,
        val coalescedRequestCount: Long,
        val callbackCount: Long,
        val presentedCount: Long,
        val deferredCount: Long,
        val failedCount: Long,
        val elapsedNanos: Long,
        val durations: DistributionSnapshot,
        val intervals: DistributionSnapshot,
        val cameraLatencies: DistributionSnapshot,
        val inputToNativeAccept: DistributionSnapshot,
        val nativeAcceptToPresent: DistributionSnapshot
    ) {
        fun encode(): String = buildString {
            val framesPerSecond = if (elapsedNanos > 0L) {
                presentedCount.toDouble() * 1_000_000_000.0 / elapsedNanos.toDouble()
            } else {
                0.0
            }
            appendLine("${name}_request_count=$requestCount")
            appendLine("${name}_coalesced_request_count=$coalescedRequestCount")
            appendLine("${name}_callback_count=$callbackCount")
            appendLine("${name}_presented_count=$presentedCount")
            appendLine("${name}_deferred_count=$deferredCount")
            appendLine("${name}_failed_count=$failedCount")
            appendLine("${name}_elapsed_ns=$elapsedNanos")
            appendLine(
                "${name}_presented_fps=" +
                    String.format(Locale.ROOT, "%.3f", framesPerSecond)
            )
            appendDistribution("${name}_cpu_frame", durations)
            appendDistribution("${name}_frame_interval", intervals)
            appendDistribution("${name}_camera_input_to_present", cameraLatencies)
            appendDistribution("${name}_input_to_native_accept", inputToNativeAccept)
            appendDistribution("${name}_native_accept_to_present", nativeAcceptToPresent)
        }

        private fun StringBuilder.appendDistribution(
            prefix: String,
            distribution: DistributionSnapshot
        ) {
            appendLine("${prefix}_sample_count=${distribution.sampleCount}")
            appendLine("${prefix}_p50_ns=${distribution.p50}")
            appendLine("${prefix}_p95_ns=${distribution.p95}")
            appendLine("${prefix}_p99_ns=${distribution.p99}")
            appendLine("${prefix}_max_ns=${distribution.maximum}")
        }
    }

    private fun NativeRenderProfilingSnapshot.toBackendSnapshot(
        managed: BackendSnapshot
    ): BackendSnapshot = BackendSnapshot(
        name = "vulkan",
        requestCount = managed.requestCount,
        coalescedRequestCount = managed.coalescedRequestCount,
        callbackCount = callbackCount,
        presentedCount = presentedCount,
        deferredCount = deferredCount,
        failedCount = failedCount,
        elapsedNanos = elapsedNanos,
        durations = cpuFrame.toManaged(),
        intervals = frameInterval.toManaged(),
        cameraLatencies = cameraLatency.toManaged(),
        inputToNativeAccept = DistributionSnapshot(),
        nativeAcceptToPresent = DistributionSnapshot()
    )

    private fun NativeRenderProfilingDistribution.toManaged() =
        DistributionSnapshot(
            sampleCount = sampleCount.coerceAtMost(Int.MAX_VALUE.toLong()).toInt(),
            p50 = p50Nanos,
            p95 = p95Nanos,
            p99 = p99Nanos,
            maximum = maximumNanos
        )
}

internal data class RenderFrameProfileToken(
    val backend: RenderBackendKind,
    val startedAtNanos: Long
)

internal enum class RenderFrameProfileOutcome {
    PRESENTED,
    DEFERRED,
    FAILED
}
