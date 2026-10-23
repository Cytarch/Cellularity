package com.cytarch.cellularity.platform.render.vulkan

import android.content.Context
import android.os.Looper
import android.view.Surface
import com.cytarch.cellularity.platform.render.AndroidRenderAssetsOwner
import java.io.ByteArrayOutputStream
import java.nio.charset.StandardCharsets
import java.util.concurrent.atomic.AtomicLong

internal object NativeVulkanWorkerActions {
    const val SURFACE_REATTACH_REQUIRED = 1
    const val TERMINAL_FAILURE = 1 shl 1
    const val RECOVERABLE_FAILURE = 1 shl 2
}

/** Thin one-way owner of a native Vulkan worker control block. */
internal class NativeVulkanRenderWorkerBridge private constructor(
    initialHandle: Long,
    private val assetsOwner: AndroidRenderAssetsOwner,
    private val statusListener: NativeVulkanWorkerStatusListener
) : AutoCloseable {
    private val ingressLock = Any()
    private val diagnosticsLock = Any()
    private val handle = AtomicLong(initialHandle)
    private val statusBuffer = NativeVulkanWorkerStatusLayout.allocateBuffer()
    private var diagnosticsRequestId = 1L
    private var diagnosticsProviderToken = 0L
    private var beforeDestroy: (() -> Unit)? = null
    private val failureDiagnosticsReaderDelegate = lazy {
        NativeVulkanFailureDiagnosticsReader(this)
    }

    fun authorizeMicroscope(
        renderEndpointHandle: Long,
        surfaceToken: Long
    ) {
        val authorization = withLiveHandle { worker ->
            NativeVulkanRenderWorkerJni.nativeAuthorizeMicroscopeWorker(
                worker,
                renderEndpointHandle,
                surfaceToken
            )
        }
        check(authorization == AUTHORIZED) {
            "Native microscope worker authorization failed: $authorization"
        }
    }

    fun installBeforeDestroy(operation: () -> Unit) {
        synchronized(ingressLock) {
            check(handle.get() != 0L) { "Native Vulkan worker is closed" }
            check(beforeDestroy == null) {
                "Native Vulkan worker finalizer is already installed"
            }
            beforeDestroy = operation
        }
    }

    fun start() {
        check(Looper.myLooper() === Looper.getMainLooper()) {
            "Native Vulkan worker must be started from the main looper"
        }
        val result = withLiveHandle { worker ->
            NativeVulkanRenderWorkerJni.nativeStartWorker(worker)
        }
        if (result != STARTED) {
            val status = runCatching(::readStatus).getOrNull()
            closeAsync()
            error(
                "Native Vulkan worker did not start: result=$result" +
                    (status?.let { ", failure=${it.failure}" } ?: "")
            )
        }
        diagnosticsProviderToken = VulkanRuntimeDiagnostics.installFullReportProvider(
            ::requestDiagnosticsReport
        )
    }

    fun attachSurface(
        surface: Surface,
        width: Int,
        height: Int,
        generation: Long
    ): Boolean = withLiveHandle { worker ->
        when (NativeVulkanRenderWorkerJni.nativeAttachSurface(
            worker,
            surface,
            width,
            height,
            generation
        )) {
            COMMAND_ACCEPTED -> true
            COMMAND_STALE_GENERATION,
            COMMAND_CLOSING -> false
            else -> error("Native Vulkan worker rejected the Surface")
        }
    }

    fun resize(width: Int, height: Int, generation: Long) {
        withLiveHandle { worker ->
            when (NativeVulkanRenderWorkerJni.nativeResizeSurface(
                worker,
                width,
                height,
                generation
            )) {
                COMMAND_ACCEPTED,
                COMMAND_STALE_GENERATION,
                COMMAND_CLOSING -> Unit
                else -> error("Native Vulkan worker rejected a resize")
            }
        }
    }

    fun detachSurface(generation: Long) {
        withLiveHandleOrNull(Unit) { worker ->
            when (NativeVulkanRenderWorkerJni.nativeDetachSurface(
                worker,
                generation
            )) {
                COMMAND_ACCEPTED,
                COMMAND_STALE_GENERATION,
                COMMAND_CLOSING -> Unit
                else -> error("Native Vulkan worker rejected a detach")
            }
        }
    }

    fun resume() {
        command("resume") { NativeVulkanRenderWorkerJni.nativeResume(it) }
    }

    fun pause() {
        withLiveHandleOrNull(Unit) { worker ->
            val result = NativeVulkanRenderWorkerJni.nativePause(worker)
            if (result != COMMAND_ACCEPTED && result != COMMAND_CLOSING) {
                error("Native Vulkan worker rejected pause: $result")
            }
        }
    }

    fun requestFrame(): Boolean = withLiveHandleOrNull(false) { worker ->
        when (NativeVulkanRenderWorkerJni.nativeRequestFrame(worker)) {
            FRAME_ACCEPTED -> true
            FRAME_COALESCED -> false
            FRAME_CLOSING,
            FRAME_FAILURE_LATCHED -> false
            else -> error("Native Vulkan worker rejected a frame request")
        }
    }

    fun setContinuousRendering(enabled: Boolean) {
        command("frame pacing") { worker ->
            NativeVulkanRenderWorkerJni.nativeSetFramePacingMode(
                worker,
                if (enabled) PACING_CONTINUOUS else PACING_ON_DEMAND
            )
        }
    }

    fun setDemoPerformanceHudEnabled(enabled: Boolean) {
        command("demo HUD") { worker ->
            NativeVulkanRenderWorkerJni.nativeSetDemoPerformanceHudEnabled(
                worker,
                enabled
            )
        }
    }

    fun readStatus(): NativeVulkanWorkerStatus = withLiveHandle { worker ->
        statusBuffer.clear()
        check(
            NativeVulkanRenderWorkerJni.nativeReadCompactStatus(
                worker,
                statusBuffer
            ) == STATUS_READY
        ) { "Could not read native Vulkan worker status" }
        NativeVulkanWorkerStatusLayout.decode(statusBuffer)
    }

    fun requestDiagnosticsReport(): String? = synchronized(diagnosticsLock) {
        requestDiagnosticsReportLocked()
    }

    private fun requestDiagnosticsReportLocked(): String? {
        if (Looper.myLooper() === Looper.getMainLooper()) return null
        val requestId = synchronized(ingressLock) {
            if (handle.get() == 0L || diagnosticsRequestId <= 0L) {
                null
            } else {
                diagnosticsRequestId
            }
        } ?: return null
        val admission = withLiveHandleOrNull(DIAGNOSTICS_CLOSING) { worker ->
            NativeVulkanRenderWorkerJni.nativeRequestDiagnostics(worker, requestId)
        }
        if (admission != DIAGNOSTICS_ACCEPTED) return null
        diagnosticsRequestId = if (requestId == Long.MAX_VALUE) 0L else requestId + 1L

        var revision = 0L
        var offset = 0L
        var totalBytes = -1L
        val output = ByteArrayOutputStream()
        val buffer = NativeVulkanDiagnosticsChunkLayout.allocateBuffer()
        val deadline = System.nanoTime() + DIAGNOSTICS_TIMEOUT_NANOS
        try {
            while (true) {
                buffer.clear()
                val readStatus = withLiveHandleOrNull(DIAGNOSTICS_FAILED) { worker ->
                    NativeVulkanRenderWorkerJni.nativeReadDiagnosticsChunk(
                        worker,
                        requestId,
                        revision,
                        offset,
                        buffer
                    )
                }
                if (readStatus == DIAGNOSTICS_PENDING) {
                    if (System.nanoTime() >= deadline) return null
                    try {
                        Thread.sleep(DIAGNOSTICS_RETRY_MILLIS)
                    } catch (_: InterruptedException) {
                        Thread.currentThread().interrupt()
                        return null
                    }
                    continue
                }
                if (readStatus != DIAGNOSTICS_READY) return null
                val chunk = NativeVulkanDiagnosticsChunkLayout.decode(buffer)
                if (revision == 0L) revision = chunk.revision
                if (chunk.revision != revision || revision == 0L) {
                    return null
                }
                if (totalBytes < 0L) totalBytes = chunk.totalBytes
                if (chunk.totalBytes != totalBytes) return null
                if (chunk.payloadBytes == 0) {
                    return if (offset == totalBytes) {
                        output.toString(StandardCharsets.UTF_8.name())
                    } else {
                        null
                    }
                }
                val bytes = ByteArray(chunk.payloadBytes)
                val payload = buffer.duplicate()
                payload.position(NativeVulkanDiagnosticsChunkLayout.HEADER_SIZE)
                payload.get(bytes)
                output.write(bytes)
                offset += chunk.payloadBytes
                if (offset == totalBytes) {
                    return output.toString(StandardCharsets.UTF_8.name())
                }
                if (offset > totalBytes) return null
            }
        } finally {
            withLiveHandleOrNull(Unit) { worker ->
                NativeVulkanRenderWorkerJni.nativeReleaseDiagnostics(
                    worker,
                    requestId,
                    revision
                )
                Unit
            }
        }
    }

    internal fun readFailureDiagnostics(expectedRevision: Long): String? {
        if (expectedRevision == 0L) return null
        var offset = 0L
        var totalBytes = -1L
        val output = ByteArrayOutputStream()
        val buffer = NativeVulkanDiagnosticsChunkLayout.allocateBuffer()
        while (true) {
            buffer.clear()
            val readStatus = withLiveHandleOrNull(DIAGNOSTICS_FAILED) { worker ->
                NativeVulkanRenderWorkerJni.nativeReadDiagnosticsChunk(
                    worker,
                    FAILURE_DIAGNOSTICS_REQUEST_ID,
                    expectedRevision,
                    offset,
                    buffer
                )
            }
            if (readStatus != DIAGNOSTICS_READY) return null
            val chunk = runCatching {
                NativeVulkanDiagnosticsChunkLayout.decode(buffer)
            }.getOrNull() ?: return null
            if (chunk.revision != expectedRevision) return null
            if (totalBytes < 0L) totalBytes = chunk.totalBytes
            if (chunk.totalBytes != totalBytes) return null
            if (chunk.payloadBytes == 0) {
                return if (offset == totalBytes) {
                    output.toString(StandardCharsets.UTF_8.name())
                } else {
                    null
                }
            }
            val bytes = ByteArray(chunk.payloadBytes)
            val payload = buffer.duplicate()
            payload.position(NativeVulkanDiagnosticsChunkLayout.HEADER_SIZE)
            payload.get(bytes)
            output.write(bytes)
            offset += chunk.payloadBytes
            if (offset == totalBytes) {
                return output.toString(StandardCharsets.UTF_8.name())
            }
            if (offset > totalBytes) return null
        }
    }

    override fun close() = closeAsync()

    fun closeAsync() {
        if (failureDiagnosticsReaderDelegate.isInitialized()) {
            failureDiagnosticsReaderDelegate.value.close()
        }
        val taken = synchronized(ingressLock) {
            val current = handle.getAndSet(0L)
            if (current != 0L) {
                NativeVulkanRenderWorkerJni.nativeRequestStop(current)
            }
            current
        }
        if (taken == 0L) return
        if (diagnosticsProviderToken != 0L) {
            VulkanRuntimeDiagnostics.removeFullReportProvider(diagnosticsProviderToken)
            diagnosticsProviderToken = 0L
        }
        NativeRenderWorkerCloseCoordinator.closeAsync(
            taken,
            assetsOwner,
            statusListener,
            beforeDestroy
        )
    }

    private fun command(name: String, operation: (Long) -> Int) {
        withLiveHandle { worker ->
            val result = operation(worker)
            if (result != COMMAND_ACCEPTED && result != COMMAND_CLOSING) {
                error("Native Vulkan worker rejected $name: $result")
            }
        }
    }

    private inline fun <T> withLiveHandle(operation: (Long) -> T): T =
        synchronized(ingressLock) {
            val current = handle.get()
            check(current != 0L) { "Native Vulkan worker is closed" }
            operation(current)
        }

    private inline fun <T> withLiveHandleOrNull(
        fallback: T,
        operation: (Long) -> T
    ): T = synchronized(ingressLock) {
        val current = handle.get()
        if (current == 0L) fallback else operation(current)
    }

    companion object {
        private val nextListenerGeneration = AtomicLong(1L)

        fun createDemo(
            context: Context,
            initialContinuousRendering: Boolean,
            statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
        ): NativeVulkanRenderWorkerBridge = create(
            context,
            CONTENT_DEMO,
            0L,
            0L,
            initialContinuousRendering,
            statusReceiver,
            startImmediately = true
        )

        fun createPreview(
            context: Context,
            contentKind: Int,
            endpointHandle: Long,
            endpointGeneration: Long,
            statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
        ): NativeVulkanRenderWorkerBridge = create(
            context,
            contentKind,
            endpointHandle,
            endpointGeneration,
            false,
            statusReceiver,
            startImmediately = true
        )

        fun createDormantMicroscope(
            context: Context,
            initialContinuousRendering: Boolean,
            statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
        ): NativeVulkanRenderWorkerBridge = create(
            context,
            CONTENT_MICROSCOPE,
            0L,
            0L,
            initialContinuousRendering,
            statusReceiver,
            startImmediately = false
        )

        private fun create(
            context: Context,
            contentKind: Int,
            endpointHandle: Long,
            endpointGeneration: Long,
            initialContinuousRendering: Boolean,
            statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit,
            startImmediately: Boolean
        ): NativeVulkanRenderWorkerBridge {
            check(NativeVulkanBackendLoader.isAvailable()) {
                NativeVulkanBackendLoader.failureDescription()
            }
            val apiAddress = NativeVulkanBackendLoader.backendApiV1Address()
            check(apiAddress != 0L) { "Vulkan backend API table is unavailable" }
            val assetsOwner = AndroidRenderAssetsOwner(
                context.applicationContext.assets
            )
            val generation = acquireListenerGeneration()
            var createdBridge: NativeVulkanRenderWorkerBridge? = null
            val listener = NativeVulkanWorkerStatusListener(
                listenerGeneration = generation,
                receiver = { status, actions ->
                    val legacy = status.toLegacyStatus()
                    VulkanRuntimeDiagnostics.updateCompact(legacy)
                    if (
                        status.phase ==
                        NativeVulkanWorkerStatus.Phase.RECOVERABLE_FAILURE
                    ) {
                        createdBridge?.failureDiagnosticsReaderDelegate
                            ?.value
                            ?.capture(status)
                    }
                    statusReceiver(status, actions)
                }
            )
            try {
                val workerHandle = NativeVulkanRenderWorkerJni.nativeCreateWorkerControlBlock(
                    assetsOwner.requireNativeLeaseHandle(),
                    apiAddress,
                    endpointHandle,
                    endpointGeneration,
                    contentKind,
                    if (initialContinuousRendering) PACING_CONTINUOUS else PACING_ON_DEMAND,
                    listener,
                    generation
                )
                check(workerHandle != 0L) {
                    "Native Vulkan worker creation returned a null handle"
                }
                val bridge = NativeVulkanRenderWorkerBridge(
                    workerHandle,
                    assetsOwner,
                    listener
                )
                createdBridge = bridge
                listener.bind(bridge)
                if (startImmediately) bridge.start()
                return bridge
            } catch (failure: Throwable) {
                if (createdBridge != null) {
                    createdBridge?.closeAsync()
                } else {
                    listener.close()
                    assetsOwner.close()
                }
                throw failure
            }
        }

        private fun acquireListenerGeneration(): Long {
            while (true) {
                val current = nextListenerGeneration.get()
                check(current > 0L) { "Vulkan listener generation is exhausted" }
                val next = if (current == Long.MAX_VALUE) 0L else current + 1L
                if (nextListenerGeneration.compareAndSet(current, next)) return current
            }
        }

        const val CONTENT_MICROSCOPE = 0
        const val CONTENT_GENOME_PREVIEW = 1
        const val CONTENT_ENVIRONMENT_FIELD_PREVIEW = 2
        const val CONTENT_DEMO = 3

        private const val PACING_CONTINUOUS = 0
        private const val PACING_ON_DEMAND = 1
        private const val AUTHORIZED = 0
        private const val STARTED = 0
        private const val COMMAND_ACCEPTED = 0
        private const val COMMAND_STALE_GENERATION = 1
        private const val COMMAND_CLOSING = 4
        private const val FRAME_ACCEPTED = 0
        private const val FRAME_COALESCED = 1
        private const val FRAME_CLOSING = 3
        private const val FRAME_FAILURE_LATCHED = 4
        private const val STATUS_READY = 0
        private const val DIAGNOSTICS_ACCEPTED = 0
        private const val DIAGNOSTICS_CLOSING = 3
        private const val DIAGNOSTICS_READY = 0
        private const val DIAGNOSTICS_PENDING = 1
        private const val DIAGNOSTICS_FAILED = 5
        private const val DIAGNOSTICS_RETRY_MILLIS = 25L
        private const val DIAGNOSTICS_TIMEOUT_NANOS = 5_000_000_000L
        private const val FAILURE_DIAGNOSTICS_REQUEST_ID = 0L
    }
}
