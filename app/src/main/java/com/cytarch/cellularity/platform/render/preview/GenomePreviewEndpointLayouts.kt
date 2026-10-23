package com.cytarch.cellularity.platform.render.preview

import java.nio.ByteBuffer
import java.nio.ByteOrder

internal enum class GenomePreviewPublishStatus(val wireValue: Int) {
    APPLIED(0),
    DUPLICATE(1),
    STALE_PRODUCER_SEQUENCE(2),
    CONFLICTING_PRODUCER_SEQUENCE(3),
    INVALID_ARGUMENT(4),
    BUDGET_EXCEEDED(5),
    REVISION_EXHAUSTED(6),
    EXCHANGE_UNAVAILABLE(7),
    CLOSING(8),
    INTERNAL_FAILURE(9);

    companion object {
        fun fromWireValue(value: Int): GenomePreviewPublishStatus =
            entries.firstOrNull { it.wireValue == value }
                ?: throw IllegalStateException("Unknown Genome Preview publish status: $value")
    }
}

internal data class GenomePreviewPublishResult(
    val status: GenomePreviewPublishStatus,
    val revision: Long
) {
    val accepted: Boolean
        get() = status == GenomePreviewPublishStatus.APPLIED ||
            status == GenomePreviewPublishStatus.DUPLICATE
}

internal object GenomePreviewPublishResultLayout {
    const val MAGIC = 0x31535047
    const val VERSION = 1
    const val SIZE_BYTES = 24

    fun allocateBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)

    fun decode(buffer: ByteBuffer): GenomePreviewPublishResult {
        require(buffer.isDirect && !buffer.isReadOnly && buffer.capacity() >= SIZE_BYTES)
        val bytes = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        check(bytes.getInt(0) == MAGIC) { "Genome Preview publish result magic is invalid" }
        check((bytes.getShort(4).toInt() and 0xFFFF) == VERSION)
        check((bytes.getShort(6).toInt() and 0xFFFF) == SIZE_BYTES)
        val status = GenomePreviewPublishStatus.fromWireValue(bytes.getInt(8))
        check(bytes.getInt(12) == 0)
        val revision = bytes.getLong(16)
        check((status == GenomePreviewPublishStatus.APPLIED || status == GenomePreviewPublishStatus.DUPLICATE) == (revision != 0L)) {
            "Genome Preview publish revision does not match its status"
        }
        return GenomePreviewPublishResult(status, revision)
    }
}

internal data class GenomePreviewAcknowledgment(
    val endpointGeneration: Long,
    val attachmentGeneration: Long,
    val appliedContentRevision: Long,
    val appliedViewRevision: Long,
    val presentedContentRevision: Long,
    val presentedViewRevision: Long,
    val presentedInteractionToken: Long,
    val cellCountContentRevision: Long,
    val viewportRevision: Long,
    val selectionToken: Long,
    val previewCellCount: Int,
    val selectedGeneIndex: Int,
    val cameraX: Float,
    val cameraY: Float,
    val zoom: Float
)

internal object GenomePreviewAcknowledgmentLayout {
    const val MAGIC = 0x32414647
    const val VERSION = 2
    const val SIZE_BYTES = 112

    fun allocateBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)

    fun decode(buffer: ByteBuffer): GenomePreviewAcknowledgment {
        require(buffer.isDirect && !buffer.isReadOnly && buffer.capacity() >= SIZE_BYTES)
        val bytes = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        check(bytes.getInt(0) == MAGIC) { "Genome Preview acknowledgment magic is invalid" }
        check((bytes.getShort(4).toInt() and 0xFFFF) == VERSION)
        check((bytes.getShort(6).toInt() and 0xFFFF) == SIZE_BYTES)
        check(bytes.getInt(108) == 0)
        return GenomePreviewAcknowledgment(
            endpointGeneration = bytes.getLong(8),
            attachmentGeneration = bytes.getLong(16),
            appliedContentRevision = bytes.getLong(24),
            appliedViewRevision = bytes.getLong(32),
            presentedContentRevision = bytes.getLong(40),
            presentedViewRevision = bytes.getLong(48),
            presentedInteractionToken = bytes.getLong(56),
            cellCountContentRevision = bytes.getLong(64),
            viewportRevision = bytes.getLong(72),
            selectionToken = bytes.getLong(80),
            previewCellCount = bytes.getInt(88),
            selectedGeneIndex = bytes.getInt(92),
            cameraX = bytes.getFloat(96),
            cameraY = bytes.getFloat(100),
            zoom = bytes.getFloat(104)
        ).also {
            check(it.endpointGeneration != 0L)
            check(it.previewCellCount >= 0)
            check(it.selectedGeneIndex >= -1)
            check(it.cameraX.isFinite() && it.cameraY.isFinite())
            check(it.zoom == 0.0f || it.zoom.isFinite() && it.zoom > 0.0f)
        }
    }
}
