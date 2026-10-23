package com.cytarch.cellularity.platform.render.preview

import java.util.concurrent.atomic.AtomicLong

interface NativePreviewCaptureRequest : AutoCloseable {
    fun await(timeoutMillis: Long): NativePreviewCaptureResult

    fun cancel(): NativePreviewCaptureCancelResult

    override fun close()
}

class JniNativePreviewCaptureRequest(
    requestHandle: Long,
    private val requestId: Long
) : NativePreviewCaptureRequest {
    private val handle = AtomicLong(requestHandle)
    private val resultBuffer = NativePreviewCaptureResultLayout.allocateBuffer()

    init {
        require(requestHandle != 0L) { "Preview capture request handle is null" }
        require(requestId > 0L) { "Preview capture request id must be positive" }
    }

    @Synchronized
    override fun await(timeoutMillis: Long): NativePreviewCaptureResult {
        require(timeoutMillis > 0L) { "Preview capture timeout must be positive" }
        val current = handle.get()
        check(current != 0L) { "Preview capture request is closed" }
        val payload = NativePreviewCaptureBridge.await(current, timeoutMillis, resultBuffer)
        return NativePreviewCaptureResultLayout.decode(resultBuffer, payload).also {
            check(it.requestId == requestId) {
                "Preview capture result belongs to another request"
            }
        }
    }

    @Synchronized
    override fun cancel(): NativePreviewCaptureCancelResult {
        val current = handle.get()
        return if (current == 0L) {
            NativePreviewCaptureCancelResult.ALREADY_TERMINAL
        } else {
            NativePreviewCaptureCancelResult.fromWireValue(
                NativePreviewCaptureBridge.cancel(current)
            )
        }
    }

    @Synchronized
    override fun close() {
        val current = handle.getAndSet(0L)
        if (current == 0L) return
        try {
            NativePreviewCaptureBridge.cancel(current)
        } finally {
            NativePreviewCaptureBridge.destroy(current)
        }
    }
}
