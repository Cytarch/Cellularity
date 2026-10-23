package com.cytarch.cellularity.platform.render.preview

import java.nio.ByteBuffer
import java.nio.ByteOrder

internal enum class EnvironmentFieldPreviewUpdateStatus(val wireValue: Int) {
    APPLIED(0),
    DUPLICATE(1),
    STALE_ENDPOINT(2),
    CLOSING(3),
    MALFORMED(4),
    REVISION_EXHAUSTED(5),
    WRONG_ENDPOINT_KIND(6);

    companion object {
        fun fromWireValue(value: Int): EnvironmentFieldPreviewUpdateStatus =
            entries.firstOrNull { it.wireValue == value }
                ?: throw IllegalStateException(
                    "Unknown Environment Field Preview update status: $value"
                )
    }
}

internal data class EnvironmentFieldPreviewUpdateResult(
    val status: EnvironmentFieldPreviewUpdateStatus,
    val ingressRevision: Long
) {
    val accepted: Boolean
        get() = status == EnvironmentFieldPreviewUpdateStatus.APPLIED ||
            status == EnvironmentFieldPreviewUpdateStatus.DUPLICATE
}

internal object EnvironmentFieldPreviewUpdateResultLayout {
    const val MAGIC = 0x31414645
    const val VERSION = 1
    const val SIZE_BYTES = 24

    fun allocateBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)

    fun decode(buffer: ByteBuffer): EnvironmentFieldPreviewUpdateResult {
        require(buffer.isDirect) {
            "Environment Field Preview result buffer must be direct"
        }
        require(buffer.capacity() >= SIZE_BYTES) {
            "Environment Field Preview result buffer is too short"
        }
        val bytes = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        check(bytes.getInt(0) == MAGIC) {
            "Environment Field Preview result magic is invalid"
        }
        check((bytes.getShort(4).toInt() and 0xFFFF) == VERSION) {
            "Environment Field Preview result version is unsupported"
        }
        check((bytes.getShort(6).toInt() and 0xFFFF) == SIZE_BYTES) {
            "Environment Field Preview result size is invalid"
        }
        val status = EnvironmentFieldPreviewUpdateStatus.fromWireValue(bytes.getInt(8))
        check(bytes.getInt(12) == 0) {
            "Environment Field Preview result reserved field is non-zero"
        }
        val revision = bytes.getLong(16)
        if (status == EnvironmentFieldPreviewUpdateStatus.APPLIED ||
            status == EnvironmentFieldPreviewUpdateStatus.DUPLICATE
        ) {
            check(revision != 0L) {
                "Accepted Environment Field Preview update has a zero revision"
            }
        } else {
            check(revision == 0L) {
                "Rejected Environment Field Preview update has a revision"
            }
        }
        return EnvironmentFieldPreviewUpdateResult(status, revision)
    }
}
