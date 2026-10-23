package com.cytarch.cellularity.platform.render.preview

import com.cytarch.cellularity.NativeCellularityControllerBridge
import java.lang.reflect.Modifier
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class NativePreviewCaptureContractTest {
    @Test
    fun pca1GoldenLayoutDecodesEveryFixedOffset() {
        val buffer = NativePreviewCaptureAdmissionLayout.allocateBuffer()
        buffer.putInt(0, NativePreviewCaptureAdmissionLayout.MAGIC)
        buffer.putShort(4, NativePreviewCaptureAdmissionLayout.VERSION.toShort())
        buffer.putShort(6, NativePreviewCaptureAdmissionLayout.SIZE_BYTES.toShort())
        buffer.putInt(8, NativePreviewCaptureAdmissionStatus.ACCEPTED.wireValue)
        buffer.putInt(12, 0)
        buffer.putLong(16, 0x0102030405060708L)

        val decoded = NativePreviewCaptureAdmissionLayout.decode(buffer)

        assertEquals(NativePreviewCaptureAdmissionStatus.ACCEPTED, decoded.status)
        assertEquals(0x0102030405060708L, decoded.requestHandle)
        assertTrue(decoded.accepted)
    }

    @Test
    fun pcr1GoldenLayoutDecodesCompletedAndTerminalResults() {
        val payload = ByteArray(24) { it.toByte() }
        val completed = resultBuffer(
            requestId = 9L,
            status = NativePreviewCaptureStatus.COMPLETED,
            width = 2,
            height = 3,
            rowStrideBytes = 8,
            pixelFormat = 1,
            origin = 1,
            alphaMode = 1,
            payloadBytes = payload.size.toLong()
        )

        val decoded = NativePreviewCaptureResultLayout.decode(completed, payload)

        assertEquals(9L, decoded.requestId)
        assertEquals(NativePreviewCaptureStatus.COMPLETED, decoded.status)
        assertEquals(2, decoded.width)
        assertEquals(3, decoded.height)
        assertEquals(8, decoded.rowStrideBytes)
        assertEquals(NativePreviewPixelFormat.RGBA8, decoded.pixelFormat)
        assertEquals(NativePreviewOrigin.TOP_LEFT, decoded.origin)
        assertEquals(NativePreviewAlphaMode.STRAIGHT, decoded.alphaMode)
        assertArrayEquals(payload, decoded.payload)

        for (status in NativePreviewCaptureStatus.entries.filterNot {
            it == NativePreviewCaptureStatus.COMPLETED
        }) {
            val terminal = NativePreviewCaptureResultLayout.decode(
                resultBuffer(requestId = 10L, status = status),
                null
            )
            assertEquals(10L, terminal.requestId)
            assertEquals(status, terminal.status)
            assertEquals(null, terminal.payload)
        }
    }

    @Test
    fun layoutsFailClosedForMalformedMetadataAndStorage() {
        val admission = NativePreviewCaptureAdmissionLayout.allocateBuffer()
        fillAdmission(admission, NativePreviewCaptureAdmissionStatus.ACCEPTED, 7L)
        admission.putInt(12, 1)
        assertThrows(IllegalStateException::class.java) {
            NativePreviewCaptureAdmissionLayout.decode(admission)
        }

        fillAdmission(admission, NativePreviewCaptureAdmissionStatus.ACCEPTED, 0L)
        assertThrows(IllegalStateException::class.java) {
            NativePreviewCaptureAdmissionLayout.decode(admission)
        }

        val result = resultBuffer(
            requestId = 11L,
            status = NativePreviewCaptureStatus.COMPLETED,
            width = 2,
            height = 2,
            rowStrideBytes = 8,
            pixelFormat = 1,
            origin = 1,
            alphaMode = 2,
            payloadBytes = 16L
        )
        result.putInt(44, 1)
        assertThrows(IllegalStateException::class.java) {
            NativePreviewCaptureResultLayout.decode(result, ByteArray(16))
        }

        assertThrows(IllegalArgumentException::class.java) {
            NativePreviewCaptureAdmissionLayout.decode(ByteBuffer.allocate(24))
        }
        assertThrows(IllegalArgumentException::class.java) {
            NativePreviewCaptureResultLayout.decode(
                ByteBuffer.allocateDirect(56).asReadOnlyBuffer(),
                null
            )
        }
    }

    @Test
    fun captureBridgeOwnsExactlyFiveNativeMethodsWithFixedDescriptors() {
        val bridge = NativePreviewCaptureBridge::class.java
        val nativeMethods = bridge.declaredMethods.filter {
            Modifier.isNative(it.modifiers)
        }
        assertEquals(
            setOf(
                "nativeEnqueueGenome",
                "nativeEnqueueSubstrate",
                "nativeAwait",
                "nativeCancel",
                "nativeDestroy"
            ),
            nativeMethods.mapTo(mutableSetOf()) { it.name }
        )
        assertEquals(5, nativeMethods.size)
        nativeMethods.forEach {
            assertTrue(Modifier.isPrivate(it.modifiers))
            assertTrue(Modifier.isStatic(it.modifiers))
        }

        assertNativeMethod(
            "nativeEnqueueGenome",
            Void.TYPE,
            Long::class.javaPrimitiveType!!,
            Long::class.javaPrimitiveType!!,
            ByteArray::class.java,
            Double::class.javaPrimitiveType!!,
            ByteBuffer::class.java
        )
        assertNativeMethod(
            "nativeEnqueueSubstrate",
            Void.TYPE,
            Long::class.javaPrimitiveType!!,
            Long::class.javaPrimitiveType!!,
            Long::class.javaPrimitiveType!!,
            Long::class.javaPrimitiveType!!,
            ByteBuffer::class.java
        )
        assertNativeMethod(
            "nativeAwait",
            ByteArray::class.java,
            Long::class.javaPrimitiveType!!,
            Long::class.javaPrimitiveType!!,
            ByteBuffer::class.java
        )
        assertNativeMethod(
            "nativeCancel",
            Int::class.javaPrimitiveType!!,
            Long::class.javaPrimitiveType!!
        )
        assertNativeMethod(
            "nativeDestroy",
            Void.TYPE,
            Long::class.javaPrimitiveType!!
        )
        assertFalse(
            NativeCellularityControllerBridge::class.java.declaredMethods.any {
                it.name == "nativeCaptureGenomeFilePreviewRgba" ||
                    it.name == "nativeCaptureSubstratePreviewRgba"
            }
        )
    }

    @Test
    fun endpointAcknowledgmentTreatsUint64FieldsAsRawBits() {
        val buffer = GenomePreviewAcknowledgmentLayout.allocateBuffer()
        buffer.putInt(0, GenomePreviewAcknowledgmentLayout.MAGIC)
        buffer.putShort(4, GenomePreviewAcknowledgmentLayout.VERSION.toShort())
        buffer.putShort(6, GenomePreviewAcknowledgmentLayout.SIZE_BYTES.toShort())
        buffer.putLong(8, Long.MIN_VALUE)
        buffer.putLong(16, -1L)
        buffer.putLong(24, Long.MIN_VALUE + 1L)
        buffer.putLong(32, -2L)
        buffer.putLong(40, -3L)
        buffer.putLong(48, -4L)
        buffer.putLong(56, -5L)
        buffer.putLong(64, -6L)
        buffer.putLong(72, -7L)
        buffer.putLong(80, -8L)
        buffer.putInt(88, 3)
        buffer.putInt(92, -1)
        buffer.putFloat(96, 1.0f)
        buffer.putFloat(100, 2.0f)
        buffer.putFloat(104, 8.0f)
        buffer.putInt(108, 0)

        val decoded = GenomePreviewAcknowledgmentLayout.decode(buffer)

        assertEquals(Long.MIN_VALUE, decoded.endpointGeneration)
        assertEquals(-1L, decoded.attachmentGeneration)
        assertEquals(-3L, decoded.presentedContentRevision)
        assertEquals(-4L, decoded.presentedViewRevision)
        assertEquals(-5L, decoded.presentedInteractionToken)
        assertEquals(-8L, decoded.selectionToken)
    }

    private fun assertNativeMethod(
        name: String,
        returnType: Class<*>,
        vararg parameterTypes: Class<*>
    ) {
        val method = NativePreviewCaptureBridge::class.java.getDeclaredMethod(
            name,
            *parameterTypes
        )
        assertTrue(Modifier.isNative(method.modifiers))
        assertEquals(returnType, method.returnType)
    }

    private fun fillAdmission(
        buffer: ByteBuffer,
        status: NativePreviewCaptureAdmissionStatus,
        requestHandle: Long
    ) {
        buffer.order(ByteOrder.LITTLE_ENDIAN)
        buffer.putInt(0, NativePreviewCaptureAdmissionLayout.MAGIC)
        buffer.putShort(4, NativePreviewCaptureAdmissionLayout.VERSION.toShort())
        buffer.putShort(6, NativePreviewCaptureAdmissionLayout.SIZE_BYTES.toShort())
        buffer.putInt(8, status.wireValue)
        buffer.putInt(12, 0)
        buffer.putLong(16, requestHandle)
    }

    private fun resultBuffer(
        requestId: Long,
        status: NativePreviewCaptureStatus,
        width: Int = 0,
        height: Int = 0,
        rowStrideBytes: Int = 0,
        pixelFormat: Int = 0,
        origin: Int = 0,
        alphaMode: Int = 0,
        payloadBytes: Long = 0L
    ): ByteBuffer = NativePreviewCaptureResultLayout.allocateBuffer().apply {
        putInt(0, NativePreviewCaptureResultLayout.MAGIC)
        putShort(4, NativePreviewCaptureResultLayout.VERSION.toShort())
        putShort(6, NativePreviewCaptureResultLayout.SIZE_BYTES.toShort())
        putLong(8, requestId)
        putInt(16, status.wireValue)
        putInt(20, width)
        putInt(24, height)
        putInt(28, rowStrideBytes)
        putInt(32, pixelFormat)
        putInt(36, origin)
        putInt(40, alphaMode)
        putInt(44, 0)
        putLong(48, payloadBytes)
    }
}
