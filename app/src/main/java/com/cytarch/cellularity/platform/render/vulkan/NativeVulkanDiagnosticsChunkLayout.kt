package com.cytarch.cellularity.platform.render.vulkan

import java.nio.ByteBuffer
import java.nio.ByteOrder

internal data class NativeVulkanDiagnosticsChunk(
    val revision: Long,
    val totalBytes: Long,
    val payloadBytes: Int
)

internal object NativeVulkanDiagnosticsChunkLayout {
    const val HEADER_SIZE = 32
    const val MAXIMUM_PAYLOAD_SIZE = 64 * 1024
    private const val MAGIC = 0x31434456
    private const val VERSION = 1

    fun allocateBuffer(): ByteBuffer = ByteBuffer.allocateDirect(
        HEADER_SIZE + MAXIMUM_PAYLOAD_SIZE
    ).order(ByteOrder.LITTLE_ENDIAN)

    fun decode(source: ByteBuffer): NativeVulkanDiagnosticsChunk {
        val buffer = source.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        require(buffer.capacity() >= HEADER_SIZE) { "VDC1 buffer is too short" }
        require(buffer.getInt(0) == MAGIC) { "VDC1 magic is invalid" }
        require(buffer.getShort(4).toInt() and 0xffff == VERSION) {
            "VDC1 version is unsupported"
        }
        require(buffer.getShort(6).toInt() and 0xffff == HEADER_SIZE) {
            "VDC1 header size is invalid"
        }
        val totalBytes = buffer.getLong(16)
        val payloadBytes = buffer.getInt(24)
        require(totalBytes >= 0L) { "VDC1 report size is invalid" }
        require(payloadBytes in 0..minOf(MAXIMUM_PAYLOAD_SIZE, buffer.capacity() - HEADER_SIZE)) {
            "VDC1 payload size is invalid"
        }
        require(buffer.getInt(28) == 0) { "VDC1 reserved bytes are nonzero" }
        return NativeVulkanDiagnosticsChunk(
            revision = buffer.getLong(8),
            totalBytes = totalBytes,
            payloadBytes = payloadBytes
        )
    }
}
