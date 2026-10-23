package com.cytarch.cellularity.platform.render.vulkan

import com.cytarch.cellularity.platform.render.NativeRenderProfilingLayout
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeVulkanWorkerWireLayoutTest {
    @Test
    fun vws1GoldenLayoutDecodesEveryFixedOffset() {
        val buffer = NativeVulkanWorkerStatusLayout.allocateBuffer()
        buffer.putInt(0, 0x31535756)
        buffer.putShort(4, 1.toShort())
        buffer.putShort(6, NativeVulkanWorkerStatusLayout.SIZE.toShort())
        buffer.putLong(8, 0x0102030405060708L)
        buffer.putInt(16, NativeVulkanWorkerStatus.Phase.RECOVERABLE_FAILURE.wireValue)
        buffer.putInt(20, 5)
        buffer.putInt(24, (1 shl 0) or (1 shl 6))
        buffer.putInt(28, NativeVulkanWorkerStatus.Failure.FRAME_FAILED.wireValue)
        buffer.putLong(32, 11L)
        buffer.putLong(40, 12L)
        buffer.putLong(48, 13L)
        buffer.putLong(56, 14L)
        buffer.putLong(64, 15L)
        buffer.putLong(72, 16L)
        buffer.putInt(80, 1920)
        buffer.putInt(84, 1080)
        buffer.putLong(88, 0L)

        val decoded = NativeVulkanWorkerStatusLayout.decode(buffer)

        assertEquals(0x0102030405060708L, decoded.revision)
        assertEquals(NativeVulkanWorkerStatus.Phase.RECOVERABLE_FAILURE, decoded.phase)
        assertEquals(5, decoded.lastFrameOutcome)
        assertEquals((1 shl 0) or (1 shl 6), decoded.flags)
        assertEquals(NativeVulkanWorkerStatus.Failure.FRAME_FAILED, decoded.failure)
        assertEquals(11L, decoded.presentedFrames)
        assertEquals(12L, decoded.swapchainRecreations)
        assertEquals(13L, decoded.failedFrameAttempts)
        assertEquals(14L, decoded.desiredSurfaceGeneration)
        assertEquals(15L, decoded.appliedSurfaceGeneration)
        assertEquals(16L, decoded.diagnosticsRevision)
        assertEquals(1920, decoded.desiredWidth)
        assertEquals(1080, decoded.desiredHeight)

        assertFalse(decoded.toLegacyStatus().terminalFailure)
        assertTrue(
            decoded.copy(
                phase = NativeVulkanWorkerStatus.Phase.TERMINAL_FAILURE
            ).toLegacyStatus().terminalFailure
        )
    }

    @Test
    fun vws1RejectsUnknownMetadataAndInvalidOutcome() {
        val buffer = validWorkerStatusBuffer()
        buffer.putInt(24, 1 shl 12)
        assertThrows(IllegalArgumentException::class.java) {
            NativeVulkanWorkerStatusLayout.decode(buffer)
        }

        fillValidWorkerStatus(buffer)
        buffer.putLong(88, 1L)
        assertThrows(IllegalArgumentException::class.java) {
            NativeVulkanWorkerStatusLayout.decode(buffer)
        }

        fillValidWorkerStatus(buffer)
        buffer.putInt(20, 99)
        buffer.putInt(24, 1 shl 6)
        assertThrows(IllegalArgumentException::class.java) {
            NativeVulkanWorkerStatusLayout.decode(buffer)
        }

        assertThrows(IllegalArgumentException::class.java) {
            NativeVulkanWorkerStatusLayout.decode(ByteBuffer.allocateDirect(95))
        }
    }

    @Test
    fun vdc1GoldenHeaderDecodesUnsignedRevisionAndPayloadBounds() {
        val buffer = NativeVulkanDiagnosticsChunkLayout.allocateBuffer()
        buffer.putInt(0, 0x31434456)
        buffer.putShort(4, 1.toShort())
        buffer.putShort(6, NativeVulkanDiagnosticsChunkLayout.HEADER_SIZE.toShort())
        buffer.putLong(8, Long.MIN_VALUE)
        buffer.putLong(16, 123_456L)
        buffer.putInt(24, 4096)
        buffer.putInt(28, 0)

        val decoded = NativeVulkanDiagnosticsChunkLayout.decode(buffer)

        assertEquals(Long.MIN_VALUE, decoded.revision)
        assertEquals(123_456L, decoded.totalBytes)
        assertEquals(4096, decoded.payloadBytes)
    }

    @Test
    fun vdc1RejectsMalformedHeaderAndImpossiblePayload() {
        val buffer = NativeVulkanDiagnosticsChunkLayout.allocateBuffer()
        fillDiagnosticsHeader(buffer)
        buffer.putInt(24, NativeVulkanDiagnosticsChunkLayout.MAXIMUM_PAYLOAD_SIZE + 1)
        assertThrows(IllegalArgumentException::class.java) {
            NativeVulkanDiagnosticsChunkLayout.decode(buffer)
        }

        fillDiagnosticsHeader(buffer)
        buffer.putLong(16, -1L)
        assertThrows(IllegalArgumentException::class.java) {
            NativeVulkanDiagnosticsChunkLayout.decode(buffer)
        }

        fillDiagnosticsHeader(buffer)
        buffer.putInt(28, 1)
        assertThrows(IllegalArgumentException::class.java) {
            NativeVulkanDiagnosticsChunkLayout.decode(buffer)
        }
    }

    @Test
    fun vrp1GoldenLayoutDecodesAllThreeDistributions() {
        val buffer = NativeRenderProfilingLayout.allocateBuffer()
        buffer.putInt(0, 0x31505256)
        buffer.putShort(4, 1.toShort())
        buffer.putShort(6, NativeRenderProfilingLayout.SIZE.toShort())
        buffer.putLong(8, 17L)
        buffer.putInt(16, 1)
        buffer.putInt(20, 0)
        buffer.putLong(24, 18L)
        buffer.putLong(32, 19L)
        buffer.putLong(40, 20L)
        buffer.putLong(48, 21L)
        buffer.putLong(56, 22L)
        fillDistribution(buffer, 64, 30L)
        fillDistribution(buffer, 104, 40L)
        fillDistribution(buffer, 144, 50L)

        val decoded = NativeRenderProfilingLayout.decode(buffer)

        assertEquals(17L, decoded.resetGeneration)
        assertTrue(decoded.enabled)
        assertEquals(18L, decoded.callbackCount)
        assertEquals(19L, decoded.presentedCount)
        assertEquals(20L, decoded.deferredCount)
        assertEquals(21L, decoded.failedCount)
        assertEquals(22L, decoded.elapsedNanos)
        assertEquals(30L, decoded.cpuFrame.sampleCount)
        assertEquals(34L, decoded.cpuFrame.maximumNanos)
        assertEquals(40L, decoded.frameInterval.sampleCount)
        assertEquals(44L, decoded.frameInterval.maximumNanos)
        assertEquals(50L, decoded.cameraLatency.sampleCount)
        assertEquals(54L, decoded.cameraLatency.maximumNanos)
    }

    @Test
    fun vrp1RejectsInvalidFlagsReservedBytesAndGeneration() {
        val buffer = validProfilingBuffer()
        buffer.putInt(16, 2)
        assertThrows(IllegalArgumentException::class.java) {
            NativeRenderProfilingLayout.decode(buffer)
        }

        fillValidProfiling(buffer)
        buffer.putInt(20, 1)
        assertThrows(IllegalArgumentException::class.java) {
            NativeRenderProfilingLayout.decode(buffer)
        }

        fillValidProfiling(buffer)
        buffer.putLong(8, -1L)
        assertThrows(IllegalArgumentException::class.java) {
            NativeRenderProfilingLayout.decode(buffer)
        }

        fillValidProfiling(buffer)
        val decoded = NativeRenderProfilingLayout.decode(buffer)
        assertFalse(decoded.enabled)
    }

    private fun validWorkerStatusBuffer(): ByteBuffer =
        NativeVulkanWorkerStatusLayout.allocateBuffer().also(::fillValidWorkerStatus)

    private fun fillValidWorkerStatus(buffer: ByteBuffer) {
        buffer.clear()
        repeat(buffer.capacity()) { buffer.put(it, 0) }
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0, 0x31535756)
        buffer.putShort(4, 1.toShort())
        buffer.putShort(6, NativeVulkanWorkerStatusLayout.SIZE.toShort())
        buffer.putLong(8, 1L)
        buffer.putInt(16, NativeVulkanWorkerStatus.Phase.READY.wireValue)
        buffer.putInt(28, NativeVulkanWorkerStatus.Failure.NONE.wireValue)
    }

    private fun fillDiagnosticsHeader(buffer: ByteBuffer) {
        buffer.clear()
        repeat(buffer.capacity()) { buffer.put(it, 0) }
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0, 0x31434456)
        buffer.putShort(4, 1.toShort())
        buffer.putShort(6, NativeVulkanDiagnosticsChunkLayout.HEADER_SIZE.toShort())
        buffer.putLong(8, 1L)
    }

    private fun validProfilingBuffer(): ByteBuffer =
        NativeRenderProfilingLayout.allocateBuffer().also(::fillValidProfiling)

    private fun fillValidProfiling(buffer: ByteBuffer) {
        buffer.clear()
        repeat(buffer.capacity()) { buffer.put(it, 0) }
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0, 0x31505256)
        buffer.putShort(4, 1.toShort())
        buffer.putShort(6, NativeRenderProfilingLayout.SIZE.toShort())
    }

    private fun fillDistribution(buffer: ByteBuffer, offset: Int, first: Long) {
        repeat(5) { index -> buffer.putLong(offset + index * Long.SIZE_BYTES, first + index) }
    }
}
