package com.cytarch.cellularity.platform.render.preview

import java.nio.ByteBuffer
import java.nio.ByteOrder

internal enum class NativePreviewCaptureAdmissionStatus(val wireValue: Int) {
    ACCEPTED(0),
    BUSY(1),
    INVALID_ARGUMENT(2),
    CLOSING(3),
    BUDGET_EXCEEDED(4),
    DUPLICATE_REQUEST_ID(5),
    INTERNAL_FAILURE(6);

    companion object {
        fun fromWireValue(value: Int): NativePreviewCaptureAdmissionStatus =
            entries.firstOrNull { it.wireValue == value }
                ?: throw IllegalStateException("Unknown preview capture admission status: $value")
    }
}

internal data class NativePreviewCaptureAdmission(
    val status: NativePreviewCaptureAdmissionStatus,
    val requestHandle: Long
) {
    val accepted: Boolean
        get() = status == NativePreviewCaptureAdmissionStatus.ACCEPTED
}

internal object NativePreviewCaptureAdmissionLayout {
    const val MAGIC = 0x31414350
    const val VERSION = 1
    const val SIZE_BYTES = 24

    fun allocateBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)

    fun decode(buffer: ByteBuffer): NativePreviewCaptureAdmission {
        require(buffer.isDirect && !buffer.isReadOnly && buffer.capacity() >= SIZE_BYTES) {
            "Preview capture admission buffer must be writable, direct, and at least 24 bytes"
        }
        val bytes = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        check(bytes.getInt(0) == MAGIC) { "Preview capture admission magic is invalid" }
        check((bytes.getShort(4).toInt() and 0xFFFF) == VERSION) {
            "Preview capture admission version is unsupported"
        }
        check((bytes.getShort(6).toInt() and 0xFFFF) == SIZE_BYTES) {
            "Preview capture admission size is invalid"
        }
        val status = NativePreviewCaptureAdmissionStatus.fromWireValue(bytes.getInt(8))
        check(bytes.getInt(12) == 0) { "Preview capture admission reserved field is non-zero" }
        val handle = bytes.getLong(16)
        check((status == NativePreviewCaptureAdmissionStatus.ACCEPTED) == (handle != 0L)) {
            "Preview capture admission handle does not match its status"
        }
        return NativePreviewCaptureAdmission(status, handle)
    }
}

internal object NativePreviewCaptureResultLayout {
    const val MAGIC = 0x31524350
    const val VERSION = 1
    const val SIZE_BYTES = 56

    private const val PIXEL_FORMAT_NONE = 0
    private const val PIXEL_FORMAT_RGBA8 = 1
    private const val ORIGIN_NONE = 0
    private const val ORIGIN_TOP_LEFT = 1
    private const val ALPHA_MODE_NONE = 0
    private const val ALPHA_MODE_STRAIGHT = 1
    private const val ALPHA_MODE_OPAQUE = 2

    fun allocateBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)

    fun decode(buffer: ByteBuffer, payload: ByteArray?): NativePreviewCaptureResult {
        require(buffer.isDirect && !buffer.isReadOnly && buffer.capacity() >= SIZE_BYTES) {
            "Preview capture result buffer must be writable, direct, and at least 56 bytes"
        }
        val bytes = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        check(bytes.getInt(0) == MAGIC) { "Preview capture result magic is invalid" }
        check((bytes.getShort(4).toInt() and 0xFFFF) == VERSION) {
            "Preview capture result version is unsupported"
        }
        check((bytes.getShort(6).toInt() and 0xFFFF) == SIZE_BYTES) {
            "Preview capture result size is invalid"
        }
        val requestId = bytes.getLong(8)
        val status = NativePreviewCaptureStatus.fromWireValue(bytes.getInt(16))
        val width = bytes.getInt(20)
        val height = bytes.getInt(24)
        val rowStrideBytes = bytes.getInt(28)
        val pixelFormat = bytes.getInt(32)
        val origin = bytes.getInt(36)
        val alphaMode = bytes.getInt(40)
        check(bytes.getInt(44) == 0) { "Preview capture result reserved field is non-zero" }
        val payloadBytes = bytes.getLong(48)
        check(requestId > 0L) { "Preview capture result request id is invalid" }
        if (status == NativePreviewCaptureStatus.COMPLETED) {
            check(width > 0 && height > 0 && width <= Int.MAX_VALUE / 4) {
                "Completed preview capture dimensions are invalid"
            }
            check(rowStrideBytes == width * 4) {
                "Completed preview capture row stride is invalid"
            }
            val expectedBytes = Math.multiplyExact(rowStrideBytes.toLong(), height.toLong())
            check(
                pixelFormat == PIXEL_FORMAT_RGBA8 &&
                    origin == ORIGIN_TOP_LEFT &&
                    (alphaMode == ALPHA_MODE_STRAIGHT || alphaMode == ALPHA_MODE_OPAQUE) &&
                    payloadBytes == expectedBytes &&
                    payload != null &&
                    payload.size.toLong() == expectedBytes
            ) { "Completed preview capture payload metadata is invalid" }
            return NativePreviewCaptureResult(
                requestId = requestId,
                status = status,
                width = width,
                height = height,
                rowStrideBytes = rowStrideBytes,
                pixelFormat = NativePreviewPixelFormat.RGBA8,
                origin = NativePreviewOrigin.TOP_LEFT,
                alphaMode = if (alphaMode == ALPHA_MODE_STRAIGHT) {
                    NativePreviewAlphaMode.STRAIGHT
                } else {
                    NativePreviewAlphaMode.OPAQUE
                },
                payload = payload
            )
        }
        check(
            width == 0 && height == 0 && rowStrideBytes == 0 &&
                pixelFormat == PIXEL_FORMAT_NONE && origin == ORIGIN_NONE &&
                alphaMode == ALPHA_MODE_NONE && payloadBytes == 0L && payload == null
        ) { "Non-completed preview capture contains payload metadata" }
        return NativePreviewCaptureResult(requestId = requestId, status = status)
    }
}
