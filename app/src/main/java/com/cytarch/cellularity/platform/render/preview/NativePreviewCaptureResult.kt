package com.cytarch.cellularity.platform.render.preview

enum class NativePreviewCaptureStatus(val wireValue: Int) {
    COMPLETED(0),
    FAILED(1),
    CANCELLED(2),
    TIMED_OUT(3),
    ALREADY_CONSUMED(4),
    INTERNAL_FAILURE(5);

    companion object {
        fun fromWireValue(value: Int): NativePreviewCaptureStatus =
            entries.firstOrNull { it.wireValue == value }
                ?: throw IllegalStateException("Unknown preview capture result status: $value")
    }
}

enum class NativePreviewPixelFormat { NONE, RGBA8 }

enum class NativePreviewOrigin { NONE, TOP_LEFT }

enum class NativePreviewAlphaMode { NONE, STRAIGHT, OPAQUE }

data class NativePreviewCaptureResult(
    val requestId: Long,
    val status: NativePreviewCaptureStatus,
    val width: Int = 0,
    val height: Int = 0,
    val rowStrideBytes: Int = 0,
    val pixelFormat: NativePreviewPixelFormat = NativePreviewPixelFormat.NONE,
    val origin: NativePreviewOrigin = NativePreviewOrigin.NONE,
    val alphaMode: NativePreviewAlphaMode = NativePreviewAlphaMode.NONE,
    val payload: ByteArray? = null
)

enum class NativePreviewCaptureCancelResult(val wireValue: Int) {
    CANCELLATION_REQUESTED(0),
    ALREADY_TERMINAL(1),
    ALREADY_CANCELLED(2);

    companion object {
        fun fromWireValue(value: Int): NativePreviewCaptureCancelResult =
            entries.firstOrNull { it.wireValue == value }
                ?: throw IllegalStateException("Unknown preview capture cancel result: $value")
    }
}
