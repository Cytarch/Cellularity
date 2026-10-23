package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeInputCommandSink
import java.nio.ByteBuffer
import java.nio.ByteOrder

internal enum class MicroscopeInputIngressStatus(val wireValue: Int) {
    APPLIED(0),
    MALFORMED(1),
    STALE_INPUT_EPOCH(2),
    ATTACHMENT_UNAVAILABLE(3),
    RESYNCING(4),
    INACTIVE_SURFACE(5),
    CLOSING(6),
    INTERNAL_FAILURE(7);

    companion object {
        fun fromWireValue(value: Int): MicroscopeInputIngressStatus =
            entries.firstOrNull { it.wireValue == value }
                ?: throw IllegalStateException("Unknown microscope input ingress status: $value")
    }
}

internal enum class MicroscopeInputCommandStatus(val wireValue: Int) {
    UNUSED(0),
    WORLD_ACCEPTED(1),
    WORLD_ACCEPTED_COALESCED_PREVIOUS(2),
    WORLD_REJECTED_ORDERED_CAPACITY(3),
    WORLD_REJECTED_PATH_CAPACITY(4),
    WORLD_REJECTED_POINTER_STATE(5),
    WORLD_REJECTED_POINTER_CAPACITY(6),
    WORLD_REJECTED_INVALID_ARGUMENT(7),
    WORLD_REJECTED_SEQUENCE_EXHAUSTED(8),
    PRESENTATION_APPLIED(9),
    PRESENTATION_REJECTED_INTERACTION_GATE(10),
    UI_APPLIED(11),
    WORLD_REJECTED_STALE_PLACEMENT_GENOME(12),
    NOT_EXECUTED_INTERNAL_FAILURE(13),
    INDETERMINATE_INTERNAL_FAILURE(14);

    companion object {
        fun fromWireValue(value: Int): MicroscopeInputCommandStatus =
            entries.firstOrNull { it.wireValue == value }
                ?: throw IllegalStateException("Unknown microscope input command status: $value")
    }
}

internal object MicroscopeInputBatchLayout {
    const val INPUT_MAGIC = 0x31424943
    const val RESULT_MAGIC = 0x31524943
    const val VERSION = 1
    const val INPUT_HEADER_SIZE = 48
    const val COMMAND_RECORD_SIZE = 32
    const val COMMAND_CAPACITY = 64
    const val INPUT_CAPACITY = INPUT_HEADER_SIZE + COMMAND_CAPACITY * COMMAND_RECORD_SIZE
    const val RESULT_HEADER_SIZE = 64
    const val RESULT_STATUS_SIZE = 4
    const val RESULT_CAPACITY = RESULT_HEADER_SIZE + COMMAND_CAPACITY * RESULT_STATUS_SIZE

    const val AGGREGATE_CAMERA_CHANGED = 1
    const val AGGREGATE_ZOOM_DISPLAY_SIZE_CHANGED = 1 shl 1
    const val AGGREGATE_ZOOM_ENDED = 1 shl 2
    const val AGGREGATE_KNOWN_MASK =
        AGGREGATE_CAMERA_CHANGED or
            AGGREGATE_ZOOM_DISPLAY_SIZE_CHANGED or
            AGGREGATE_ZOOM_ENDED

    const val KIND_DOWN = 1
    const val KIND_MOVE = 2
    const val KIND_UP = 3
    const val KIND_PAN = 4
    const val KIND_ZOOM = 5
    const val KIND_FLING = 6
    const val KIND_TAP = 7
    const val KIND_ZOOM_ENDED = 8

    fun allocateInputBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(INPUT_CAPACITY).order(ByteOrder.LITTLE_ENDIAN)

    fun allocateResultBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(RESULT_CAPACITY).order(ByteOrder.LITTLE_ENDIAN)

    fun writeIngressResult(
        buffer: ByteBuffer,
        batchSequence: Long,
        inputEpoch: Long,
        status: MicroscopeInputIngressStatus
    ) {
        requireWritableDirect(buffer, RESULT_CAPACITY, "Microscope input result")
        val bytes = buffer.order(ByteOrder.LITTLE_ENDIAN)
        for (offset in 0 until RESULT_CAPACITY step Long.SIZE_BYTES) {
            bytes.putLong(offset, 0L)
        }
        bytes.putInt(0, RESULT_MAGIC)
        bytes.putShort(4, VERSION.toShort())
        bytes.putShort(6, RESULT_HEADER_SIZE.toShort())
        bytes.putShort(8, RESULT_STATUS_SIZE.toShort())
        bytes.putShort(10, 0.toShort())
        bytes.putLong(16, batchSequence)
        bytes.putLong(24, inputEpoch)
        bytes.putInt(52, status.wireValue)
    }

    fun checkedEventTimeNanos(eventTimeMillis: Long): Long {
        require(eventTimeMillis >= 0L) { "MotionEvent time must not be negative" }
        require(eventTimeMillis <= Long.MAX_VALUE / NANOS_PER_MILLISECOND) {
            "MotionEvent time overflows nanoseconds"
        }
        return eventTimeMillis * NANOS_PER_MILLISECOND
    }

    fun readBatchSequence(input: ByteBuffer): Long =
        input.duplicate().order(ByteOrder.LITTLE_ENDIAN).getLong(16)

    fun readExpectedInputEpoch(input: ByteBuffer): Long =
        input.duplicate().order(ByteOrder.LITTLE_ENDIAN).getLong(32)

    private fun requireWritableDirect(buffer: ByteBuffer, capacity: Int, name: String) {
        require(buffer.isDirect && !buffer.isReadOnly && buffer.capacity() >= capacity) {
            "$name buffer must be writable, direct, and at least $capacity bytes"
        }
    }

    private const val NANOS_PER_MILLISECOND = 1_000_000L
}

/** Reusable main-thread encoder; each command record is written in full. */
internal class AndroidMicroscopeInputBatchBuilder : MicroscopeInputCommandSink {
    val inputBuffer: ByteBuffer = MicroscopeInputBatchLayout.allocateInputBuffer()
    val resultReader = MicroscopeInputBatchResultReader()

    var commandCount: Int = 0
        private set
    var tapCount: Int = 0
        private set
    var batchSequence: Long = 0L
        private set
    var inputEpoch: Long = 0L
        private set
    var eventTimeMillis: Long = 0L
        private set
    var profilingIngressTimestampNanos: Long = 0L
        private set

    fun begin(
        sequence: Long,
        eventTimeMillis: Long,
        expectedInputEpoch: Long,
        profilingTimestampNanos: Long
    ) {
        require(sequence != 0L) { "Microscope input sequence is exhausted" }
        require(expectedInputEpoch != 0L) { "Microscope input epoch is unavailable" }
        commandCount = 0
        tapCount = 0
        batchSequence = sequence
        inputEpoch = expectedInputEpoch
        this.eventTimeMillis = eventTimeMillis
        profilingIngressTimestampNanos = profilingTimestampNanos
        inputBuffer.putInt(0, MicroscopeInputBatchLayout.INPUT_MAGIC)
        inputBuffer.putShort(4, MicroscopeInputBatchLayout.VERSION.toShort())
        inputBuffer.putShort(6, MicroscopeInputBatchLayout.INPUT_HEADER_SIZE.toShort())
        inputBuffer.putShort(8, MicroscopeInputBatchLayout.COMMAND_RECORD_SIZE.toShort())
        inputBuffer.putShort(10, 0.toShort())
        inputBuffer.putInt(12, 0)
        inputBuffer.putLong(16, sequence)
        inputBuffer.putLong(
            24,
            MicroscopeInputBatchLayout.checkedEventTimeNanos(eventTimeMillis)
        )
        inputBuffer.putLong(32, expectedInputEpoch)
        inputBuffer.putLong(40, profilingTimestampNanos)
    }

    fun finish(): Boolean {
        inputBuffer.putShort(10, commandCount.toShort())
        return commandCount != 0
    }

    override fun touchDown(pointerId: Int, x: Float, y: Float) =
        append(MicroscopeInputBatchLayout.KIND_DOWN, pointerId, x, y)

    override fun touchMove(pointerId: Int, x: Float, y: Float) =
        append(MicroscopeInputBatchLayout.KIND_MOVE, pointerId, x, y)

    override fun touchUp(pointerId: Int) =
        append(MicroscopeInputBatchLayout.KIND_UP, pointerId)

    override fun pan(deltaX: Float, deltaY: Float) =
        append(MicroscopeInputBatchLayout.KIND_PAN, 0, deltaX, deltaY)

    override fun zoom(factor: Float, centerX: Float, centerY: Float) =
        append(MicroscopeInputBatchLayout.KIND_ZOOM, 0, factor, centerX, centerY)

    override fun zoomEnded() = append(MicroscopeInputBatchLayout.KIND_ZOOM_ENDED, 0)

    override fun tap(x: Float, y: Float) {
        tapCount += 1
        check(tapCount == 1) { "One microscope input batch cannot contain multiple taps" }
        append(MicroscopeInputBatchLayout.KIND_TAP, 0, x, y)
    }

    override fun fling(velocityX: Float, velocityY: Float) =
        append(MicroscopeInputBatchLayout.KIND_FLING, 0, velocityX, velocityY)

    private fun append(
        kind: Int,
        pointerId: Int,
        value0: Float = 0f,
        value1: Float = 0f,
        value2: Float = 0f,
        value3: Float = 0f
    ) {
        check(commandCount < MicroscopeInputBatchLayout.COMMAND_CAPACITY) {
            "Microscope input batch exceeds its fixed command capacity"
        }
        val offset = MicroscopeInputBatchLayout.INPUT_HEADER_SIZE +
            commandCount * MicroscopeInputBatchLayout.COMMAND_RECORD_SIZE
        inputBuffer.putShort(offset, kind.toShort())
        inputBuffer.putShort(offset + 2, 0.toShort())
        inputBuffer.putInt(offset + 4, pointerId)
        inputBuffer.putFloat(offset + 8, value0)
        inputBuffer.putFloat(offset + 12, value1)
        inputBuffer.putFloat(offset + 16, value2)
        inputBuffer.putFloat(offset + 20, value3)
        inputBuffer.putLong(offset + 24, 0L)
        commandCount += 1
    }
}

/** Reusable validated view over the caller-owned CIR1 result buffer. */
internal class MicroscopeInputBatchResultReader {
    val buffer: ByteBuffer = MicroscopeInputBatchLayout.allocateResultBuffer()

    var ingressStatus: MicroscopeInputIngressStatus = MicroscopeInputIngressStatus.MALFORMED
        private set
    var commandCount: Int = 0
        private set
    var aggregateFlags: Int = 0
        private set
    var inputEpoch: Long = 0L
        private set
    var viewportRevision: Long = 0L
        private set
    var presentationMotionRevision: Long = 0L
        private set
    var zoomDisplaySize: Float = 0f
        private set

    val cameraChanged: Boolean
        get() = aggregateFlags and MicroscopeInputBatchLayout.AGGREGATE_CAMERA_CHANGED != 0
    val zoomDisplaySizeChanged: Boolean
        get() = aggregateFlags and
            MicroscopeInputBatchLayout.AGGREGATE_ZOOM_DISPLAY_SIZE_CHANGED != 0
    val zoomEnded: Boolean
        get() = aggregateFlags and MicroscopeInputBatchLayout.AGGREGATE_ZOOM_ENDED != 0

    fun decode(expectedBatchSequence: Long, expectedCommandCount: Int? = null) {
        val bytes = buffer.order(ByteOrder.LITTLE_ENDIAN)
        check(bytes.getInt(0) == MicroscopeInputBatchLayout.RESULT_MAGIC) {
            "Microscope input result magic is invalid"
        }
        check((bytes.getShort(4).toInt() and 0xFFFF) == MicroscopeInputBatchLayout.VERSION)
        check(
            (bytes.getShort(6).toInt() and 0xFFFF) ==
                MicroscopeInputBatchLayout.RESULT_HEADER_SIZE
        )
        check(
            (bytes.getShort(8).toInt() and 0xFFFF) ==
                MicroscopeInputBatchLayout.RESULT_STATUS_SIZE
        )
        val decodedCount = bytes.getShort(10).toInt() and 0xFFFF
        check(decodedCount in 0..MicroscopeInputBatchLayout.COMMAND_CAPACITY)
        val decodedFlags = bytes.getInt(12)
        check(decodedFlags and MicroscopeInputBatchLayout.AGGREGATE_KNOWN_MASK.inv() == 0)
        check(bytes.getLong(16) == expectedBatchSequence) {
            "Microscope input result sequence does not match its request"
        }
        check(bytes.getLong(56) == 0L) {
            "Microscope input result reserved field is non-zero"
        }
        for (index in decodedCount until MicroscopeInputBatchLayout.COMMAND_CAPACITY) {
            check(statusWireValue(bytes, index) == 0) {
                "Microscope input result contains a stale unused status"
            }
        }
        ingressStatus = MicroscopeInputIngressStatus.fromWireValue(bytes.getInt(52))
        when (ingressStatus) {
            MicroscopeInputIngressStatus.APPLIED -> {
                if (expectedCommandCount != null) {
                    check(decodedCount == expectedCommandCount) {
                        "Applied microscope input result has a mismatched command count"
                    }
                }
            }
            MicroscopeInputIngressStatus.INTERNAL_FAILURE -> {
                check(
                    expectedCommandCount == null ||
                        decodedCount == 0 ||
                        decodedCount == expectedCommandCount
                )
                check(decodedFlags == 0)
            }
            else -> {
                check(decodedCount == 0)
                check(decodedFlags == 0) {
                    "Rejected microscope input result contains aggregate flags"
                }
            }
        }
        if (
            decodedFlags and
            MicroscopeInputBatchLayout.AGGREGATE_ZOOM_DISPLAY_SIZE_CHANGED != 0
        ) {
            val displaySize = Float.fromBits(bytes.getInt(48))
            check(displaySize.isFinite() && displaySize > 0f)
        } else {
            check(bytes.getInt(48) == 0) {
                "Microscope input result has a stale zoom display size"
            }
        }
        var internalFailureSeen = false
        for (index in 0 until decodedCount) {
            val status = MicroscopeInputCommandStatus.fromWireValue(
                statusWireValue(bytes, index)
            )
            when (ingressStatus) {
                MicroscopeInputIngressStatus.APPLIED -> check(
                    status != MicroscopeInputCommandStatus.UNUSED &&
                        status != MicroscopeInputCommandStatus.NOT_EXECUTED_INTERNAL_FAILURE &&
                        status != MicroscopeInputCommandStatus.INDETERMINATE_INTERNAL_FAILURE
                )
                MicroscopeInputIngressStatus.INTERNAL_FAILURE -> when (status) {
                    MicroscopeInputCommandStatus.INDETERMINATE_INTERNAL_FAILURE -> {
                        check(!internalFailureSeen)
                        internalFailureSeen = true
                    }
                    MicroscopeInputCommandStatus.NOT_EXECUTED_INTERNAL_FAILURE ->
                        check(internalFailureSeen)
                    MicroscopeInputCommandStatus.UNUSED -> error(
                        "Executed microscope input status slot is unused"
                    )
                    else -> check(!internalFailureSeen)
                }
                else -> error(
                    "Rejected microscope input result contains command statuses"
                )
            }
        }
        commandCount = decodedCount
        aggregateFlags = decodedFlags
        inputEpoch = bytes.getLong(24)
        viewportRevision = bytes.getLong(32)
        presentationMotionRevision = bytes.getLong(40)
        zoomDisplaySize = Float.fromBits(bytes.getInt(48))
    }

    fun commandStatus(index: Int): MicroscopeInputCommandStatus {
        require(index in 0 until commandCount)
        return MicroscopeInputCommandStatus.fromWireValue(statusWireValue(buffer, index))
    }

    private fun statusWireValue(bytes: ByteBuffer, index: Int): Int =
        bytes.getInt(
            MicroscopeInputBatchLayout.RESULT_HEADER_SIZE +
                index * MicroscopeInputBatchLayout.RESULT_STATUS_SIZE
        )
}

internal object PresentationMotionSnapshotLayout {
    const val MAGIC = 0x31534D43
    const val VERSION = 1
    const val SIZE_BYTES = 32
    const val FLAG_INERTIA_ACTIVE = 1
    const val FLAG_CAMERA_MOTION_ACTIVE = 1 shl 1
    const val KNOWN_FLAGS = FLAG_INERTIA_ACTIVE or FLAG_CAMERA_MOTION_ACTIVE

    fun allocateBuffer(): ByteBuffer =
        ByteBuffer.allocateDirect(SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)
}

internal class PresentationMotionSnapshotReader {
    val buffer: ByteBuffer = PresentationMotionSnapshotLayout.allocateBuffer()

    var revision: Long = 0L
        private set
    var cameraRevision: Long = 0L
        private set
    var inertiaActive: Boolean = false
        private set
    var cameraMotionActive: Boolean = false
        private set

    fun decode(): Boolean {
        val bytes = buffer.order(ByteOrder.LITTLE_ENDIAN)
        check(bytes.getInt(0) == PresentationMotionSnapshotLayout.MAGIC)
        check((bytes.getShort(4).toInt() and 0xFFFF) == PresentationMotionSnapshotLayout.VERSION)
        check((bytes.getShort(6).toInt() and 0xFFFF) == PresentationMotionSnapshotLayout.SIZE_BYTES)
        val nextRevision = bytes.getLong(8)
        val nextCameraRevision = bytes.getLong(16)
        val flags = bytes.getInt(24)
        check(flags and PresentationMotionSnapshotLayout.KNOWN_FLAGS.inv() == 0)
        check(bytes.getInt(28) == 0)
        val changed = nextRevision != revision
        revision = nextRevision
        cameraRevision = nextCameraRevision
        inertiaActive = flags and PresentationMotionSnapshotLayout.FLAG_INERTIA_ACTIVE != 0
        cameraMotionActive =
            flags and PresentationMotionSnapshotLayout.FLAG_CAMERA_MOTION_ACTIVE != 0
        return changed
    }
}
