package com.cytarch.cellularity.platform.render.microscope

import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class MicroscopeInputBatchLayoutTest {
    @Test
    fun builderWritesExactLittleEndianCib1Records() {
        val builder = AndroidMicroscopeInputBatchBuilder()
        builder.begin(
            sequence = -1L,
            eventTimeMillis = 123L,
            expectedInputEpoch = Long.MIN_VALUE,
            profilingTimestampNanos = 987_654_321L
        )
        builder.touchDown(3, 1.25f, -2.5f)
        builder.touchMove(3, 4f, 5f)
        builder.touchUp(3)
        builder.pan(6f, 7f)
        builder.zoom(1.1f, 8f, 9f)
        builder.fling(10f, 11f)
        builder.tap(12f, 13f)
        builder.zoomEnded()

        assertTrue(builder.finish())
        val bytes = builder.inputBuffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        assertEquals(MicroscopeInputBatchLayout.INPUT_CAPACITY, bytes.capacity())
        assertEquals(MicroscopeInputBatchLayout.INPUT_MAGIC, bytes.getInt(0))
        assertEquals(MicroscopeInputBatchLayout.VERSION, bytes.getShort(4).unsigned())
        assertEquals(48, bytes.getShort(6).unsigned())
        assertEquals(32, bytes.getShort(8).unsigned())
        assertEquals(8, bytes.getShort(10).unsigned())
        assertEquals(0, bytes.getInt(12))
        assertEquals(-1L, bytes.getLong(16))
        assertEquals(123_000_000L, bytes.getLong(24))
        assertEquals(Long.MIN_VALUE, bytes.getLong(32))
        assertEquals(987_654_321L, bytes.getLong(40))

        assertRecord(bytes, 0, MicroscopeInputBatchLayout.KIND_DOWN, 3, 1.25f, -2.5f)
        assertRecord(bytes, 1, MicroscopeInputBatchLayout.KIND_MOVE, 3, 4f, 5f)
        assertRecord(bytes, 2, MicroscopeInputBatchLayout.KIND_UP, 3)
        assertRecord(bytes, 3, MicroscopeInputBatchLayout.KIND_PAN, 0, 6f, 7f)
        assertRecord(bytes, 4, MicroscopeInputBatchLayout.KIND_ZOOM, 0, 1.1f, 8f, 9f)
        assertRecord(bytes, 5, MicroscopeInputBatchLayout.KIND_FLING, 0, 10f, 11f)
        assertRecord(bytes, 6, MicroscopeInputBatchLayout.KIND_TAP, 0, 12f, 13f)
        assertRecord(bytes, 7, MicroscopeInputBatchLayout.KIND_ZOOM_ENDED, 0)
    }

    @Test
    fun managedRejectionClearsEveryCir1StatusSlot() {
        val reader = MicroscopeInputBatchResultReader()
        val bytes = reader.buffer.order(ByteOrder.LITTLE_ENDIAN)
        repeat(MicroscopeInputBatchLayout.RESULT_CAPACITY / Long.SIZE_BYTES) { index ->
            bytes.putLong(index * Long.SIZE_BYTES, -1L)
        }

        MicroscopeInputBatchLayout.writeIngressResult(
            bytes,
            batchSequence = 91L,
            inputEpoch = 17L,
            status = MicroscopeInputIngressStatus.ATTACHMENT_UNAVAILABLE
        )
        reader.decode(expectedBatchSequence = 91L, expectedCommandCount = 5)

        assertEquals(MicroscopeInputIngressStatus.ATTACHMENT_UNAVAILABLE, reader.ingressStatus)
        assertEquals(0, reader.commandCount)
        assertEquals(17L, reader.inputEpoch)
        for (index in 0 until MicroscopeInputBatchLayout.COMMAND_CAPACITY) {
            assertEquals(
                0,
                bytes.getInt(
                    MicroscopeInputBatchLayout.RESULT_HEADER_SIZE +
                        index * MicroscopeInputBatchLayout.RESULT_STATUS_SIZE
                )
            )
        }
    }

    @Test
    fun builderAcceptsExactlySixtyFourCommandsAndRejectsTheNextRecord() {
        val builder = AndroidMicroscopeInputBatchBuilder()
        builder.begin(
            sequence = 1L,
            eventTimeMillis = 1L,
            expectedInputEpoch = 1L,
            profilingTimestampNanos = 0L
        )
        repeat(MicroscopeInputBatchLayout.COMMAND_CAPACITY) {
            builder.pan(it.toFloat(), -it.toFloat())
        }

        assertTrue(builder.finish())
        assertEquals(MicroscopeInputBatchLayout.COMMAND_CAPACITY, builder.commandCount)
        assertThrows(IllegalStateException::class.java) {
            builder.pan(65f, -65f)
        }
    }

    @Test
    fun eventTimeConversionRejectsNegativeAndOverflowingAndroidClocks() {
        assertEquals(
            123_000_000L,
            MicroscopeInputBatchLayout.checkedEventTimeNanos(123L)
        )
        assertThrows(IllegalArgumentException::class.java) {
            MicroscopeInputBatchLayout.checkedEventTimeNanos(-1L)
        }
        assertThrows(IllegalArgumentException::class.java) {
            MicroscopeInputBatchLayout.checkedEventTimeNanos(
                Long.MAX_VALUE / 1_000_000L + 1L
            )
        }
    }

    @Test
    fun resultReaderAcceptsOnlyCanonicalInternalFailureStatusOrdering() {
        val reader = MicroscopeInputBatchResultReader()
        val bytes = reader.buffer.order(ByteOrder.LITTLE_ENDIAN)
        MicroscopeInputBatchLayout.writeIngressResult(
            bytes,
            batchSequence = 55L,
            inputEpoch = 12L,
            status = MicroscopeInputIngressStatus.INTERNAL_FAILURE
        )
        bytes.putShort(10, 3.toShort())
        bytes.putInt(
            MicroscopeInputBatchLayout.RESULT_HEADER_SIZE,
            MicroscopeInputCommandStatus.WORLD_ACCEPTED.wireValue
        )
        bytes.putInt(
            MicroscopeInputBatchLayout.RESULT_HEADER_SIZE +
                MicroscopeInputBatchLayout.RESULT_STATUS_SIZE,
            MicroscopeInputCommandStatus.INDETERMINATE_INTERNAL_FAILURE.wireValue
        )
        bytes.putInt(
            MicroscopeInputBatchLayout.RESULT_HEADER_SIZE +
                2 * MicroscopeInputBatchLayout.RESULT_STATUS_SIZE,
            MicroscopeInputCommandStatus.NOT_EXECUTED_INTERNAL_FAILURE.wireValue
        )

        reader.decode(expectedBatchSequence = 55L, expectedCommandCount = 3)
        assertEquals(MicroscopeInputIngressStatus.INTERNAL_FAILURE, reader.ingressStatus)
        assertEquals(
            MicroscopeInputCommandStatus.WORLD_ACCEPTED,
            reader.commandStatus(0)
        )
        assertEquals(
            MicroscopeInputCommandStatus.INDETERMINATE_INTERNAL_FAILURE,
            reader.commandStatus(1)
        )
        assertEquals(
            MicroscopeInputCommandStatus.NOT_EXECUTED_INTERNAL_FAILURE,
            reader.commandStatus(2)
        )

        bytes.putInt(
            MicroscopeInputBatchLayout.RESULT_HEADER_SIZE,
            MicroscopeInputCommandStatus.NOT_EXECUTED_INTERNAL_FAILURE.wireValue
        )
        assertThrows(IllegalStateException::class.java) {
            reader.decode(expectedBatchSequence = 55L, expectedCommandCount = 3)
        }
    }

    @Test
    fun motionReaderUsesRevisionAndFlagsWithoutAllocatingViewportArray() {
        val reader = PresentationMotionSnapshotReader()
        val bytes = reader.buffer.order(ByteOrder.LITTLE_ENDIAN)
        bytes.putInt(0, PresentationMotionSnapshotLayout.MAGIC)
        bytes.putShort(4, PresentationMotionSnapshotLayout.VERSION.toShort())
        bytes.putShort(6, PresentationMotionSnapshotLayout.SIZE_BYTES.toShort())
        bytes.putLong(8, 41L)
        bytes.putLong(16, 37L)
        bytes.putInt(
            24,
            PresentationMotionSnapshotLayout.FLAG_INERTIA_ACTIVE or
                PresentationMotionSnapshotLayout.FLAG_CAMERA_MOTION_ACTIVE
        )
        bytes.putInt(28, 0)

        assertTrue(reader.decode())
        assertEquals(41L, reader.revision)
        assertEquals(37L, reader.cameraRevision)
        assertTrue(reader.inertiaActive)
        assertTrue(reader.cameraMotionActive)
        assertFalse(reader.decode())
    }

    private fun assertRecord(
        bytes: java.nio.ByteBuffer,
        index: Int,
        kind: Int,
        pointerId: Int,
        value0: Float = 0f,
        value1: Float = 0f,
        value2: Float = 0f,
        value3: Float = 0f
    ) {
        val offset = MicroscopeInputBatchLayout.INPUT_HEADER_SIZE +
            index * MicroscopeInputBatchLayout.COMMAND_RECORD_SIZE
        assertEquals(kind, bytes.getShort(offset).unsigned())
        assertEquals(0, bytes.getShort(offset + 2).unsigned())
        assertEquals(pointerId, bytes.getInt(offset + 4))
        assertEquals(value0.toRawBits(), bytes.getInt(offset + 8))
        assertEquals(value1.toRawBits(), bytes.getInt(offset + 12))
        assertEquals(value2.toRawBits(), bytes.getInt(offset + 16))
        assertEquals(value3.toRawBits(), bytes.getInt(offset + 20))
        assertEquals(0L, bytes.getLong(offset + 24))
    }

    private fun Short.unsigned(): Int = toInt() and 0xFFFF
}
