package com.cytarch.cellularity.shared.ui.play.microscope

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class MicroscopeGestureInterpreterTest {
    @Test
    fun onePointerEventsBecomeOrderedTouchAndPanCommands() {
        val interpreter = MicroscopeGestureInterpreter()

        assertEquals(
            listOf(MicroscopeInputCommand.TouchDown(7, 10f, 20f)),
            interpreter.onPointerEvent(
                event(MicroscopePointerAction.DOWN, 7, 1_000, pointer(7, 10f, 20f))
            )
        )
        assertEquals(
            listOf(
                MicroscopeInputCommand.TouchMove(7, 14f, 17f),
                MicroscopeInputCommand.Pan(4f, -3f)
            ),
            interpreter.onPointerEvent(
                event(MicroscopePointerAction.MOVE, null, 1_010, pointer(7, 14f, 17f))
            )
        )
        assertEquals(
            listOf(
                MicroscopeInputCommand.TouchUp(7),
                MicroscopeInputCommand.ZoomEnded
            ),
            interpreter.onPointerEvent(
                event(MicroscopePointerAction.UP, 7, 1_020, pointer(7, 14f, 17f))
            )
        )
    }

    @Test
    fun twoPointersPreserveTouchThenPanThenPinchOrdering() {
        val interpreter = MicroscopeGestureInterpreter()
        interpreter.onPointerEvent(
            event(MicroscopePointerAction.DOWN, 0, 1_000, pointer(0, 0f, 0f))
        )
        interpreter.onPointerEvent(
            event(
                MicroscopePointerAction.POINTER_DOWN,
                1,
                1_010,
                pointer(0, 0f, 0f),
                pointer(1, 10f, 0f)
            )
        )

        assertEquals(
            listOf(
                MicroscopeInputCommand.TouchMove(0, 2f, 4f),
                MicroscopeInputCommand.TouchMove(1, 13f, 4f),
                MicroscopeInputCommand.Pan(2.5f, 4f),
                MicroscopeInputCommand.Zoom(1.1f, 7.5f, 4f)
            ),
            interpreter.onPointerEvent(
                event(
                    MicroscopePointerAction.MOVE,
                    null,
                    1_020,
                    pointer(0, 2f, 4f),
                    pointer(1, 13f, 4f)
                )
            )
        )
    }

    @Test
    fun abruptPinchIsRejectedAndFlingWaitsForZoomGestureToFinish() {
        val interpreter = MicroscopeGestureInterpreter(initialTimestampMillis = 100)
        interpreter.onPointerEvent(
            event(
                MicroscopePointerAction.POINTER_DOWN,
                1,
                300,
                pointer(0, 0f, 0f),
                pointer(1, 10f, 0f)
            )
        )
        val commands = interpreter.onPointerEvent(
            event(
                MicroscopePointerAction.MOVE,
                null,
                400,
                pointer(0, 0f, 0f),
                pointer(1, 20f, 0f)
            )
        )

        assertEquals(
            listOf(
                MicroscopeInputCommand.TouchMove(0, 0f, 0f),
                MicroscopeInputCommand.TouchMove(1, 20f, 0f),
                MicroscopeInputCommand.Pan(5f, 0f)
            ),
            commands
        )
        assertNull(interpreter.onFling(30f, 40f, 600))
        assertEquals(
            MicroscopeInputCommand.Fling(30f, 40f),
            interpreter.onFling(30f, 40f, 601)
        )
    }

    @Test
    fun cancelReleasesEveryPointerEndsZoomAndClearsGestureHistory() {
        val interpreter = MicroscopeGestureInterpreter()
        interpreter.onPointerEvent(
            event(MicroscopePointerAction.DOWN, 2, 0, pointer(2, 1f, 1f))
        )

        assertEquals(
            listOf(
                MicroscopeInputCommand.TouchUp(2),
                MicroscopeInputCommand.TouchUp(3),
                MicroscopeInputCommand.ZoomEnded
            ),
            interpreter.onPointerEvent(
                event(
                    MicroscopePointerAction.CANCEL,
                    null,
                    10,
                    pointer(2, 1f, 1f),
                    pointer(3, 4f, 5f)
                )
            )
        )

        interpreter.onPointerEvent(
            event(MicroscopePointerAction.DOWN, 9, 20, pointer(9, 20f, 20f))
        )
        assertEquals(
            listOf(
                MicroscopeInputCommand.TouchMove(9, 21f, 22f),
                MicroscopeInputCommand.Pan(1f, 2f)
            ),
            interpreter.onPointerEvent(
                event(MicroscopePointerAction.MOVE, null, 30, pointer(9, 21f, 22f))
            )
        )
    }

    @Test
    fun tapAndThreePointerMoveKeepTheirOriginalSemantics() {
        val interpreter = MicroscopeGestureInterpreter()
        assertEquals(MicroscopeInputCommand.Tap(3f, 5f), interpreter.onTap(3f, 5f))

        interpreter.onPointerEvent(
            event(
                MicroscopePointerAction.POINTER_DOWN,
                2,
                0,
                pointer(0, 0f, 0f),
                pointer(1, 10f, 0f),
                pointer(2, 5f, 5f)
            )
        )
        assertEquals(
            listOf(
                MicroscopeInputCommand.TouchMove(0, 1f, 1f),
                MicroscopeInputCommand.TouchMove(1, 11f, 1f),
                MicroscopeInputCommand.TouchMove(2, 6f, 6f)
            ),
            interpreter.onPointerEvent(
                event(
                    MicroscopePointerAction.MOVE,
                    null,
                    10,
                    pointer(0, 1f, 1f),
                    pointer(1, 11f, 1f),
                    pointer(2, 6f, 6f)
                )
            )
        )
    }

    @Test
    fun nonOwningViewAndSinkPreserveExactTwoPointerCommandOrder() {
        val interpreter = MicroscopeGestureInterpreter()
        val sink = RecordingSink()
        interpreter.onPointerEvent(
            view(MicroscopePointerAction.DOWN, 4, 1_000, pointer(4, 0f, 0f)),
            sink
        )
        sink.commands.clear()
        interpreter.onPointerEvent(
            view(
                MicroscopePointerAction.POINTER_DOWN,
                9,
                1_010,
                pointer(4, 0f, 0f),
                pointer(9, 10f, 0f)
            ),
            sink
        )
        sink.commands.clear()

        interpreter.onPointerEvent(
            view(
                MicroscopePointerAction.MOVE,
                null,
                1_020,
                pointer(4, 2f, 4f),
                pointer(9, 13f, 4f)
            ),
            sink
        )

        assertEquals(
            listOf(
                MicroscopeInputCommand.TouchMove(4, 2f, 4f),
                MicroscopeInputCommand.TouchMove(9, 13f, 4f),
                MicroscopeInputCommand.Pan(2.5f, 4f),
                MicroscopeInputCommand.Zoom(1.1f, 7.5f, 4f)
            ),
            sink.commands
        )
    }

    @Test
    fun resetDropsOldGestureGeometryAndSuppressesItsLateFling() {
        val interpreter = MicroscopeGestureInterpreter()
        interpreter.onPointerEvent(
            event(MicroscopePointerAction.DOWN, 1, 100, pointer(1, 2f, 3f))
        )
        interpreter.reset(500)

        assertEquals(
            listOf(MicroscopeInputCommand.TouchMove(1, 20f, 30f)),
            interpreter.onPointerEvent(
                event(MicroscopePointerAction.MOVE, null, 510, pointer(1, 20f, 30f))
            )
        )
        assertNull(interpreter.onFling(30f, 40f, 700))
        assertEquals(
            MicroscopeInputCommand.Fling(30f, 40f),
            interpreter.onFling(30f, 40f, 701)
        )
    }

    private fun event(
        action: MicroscopePointerAction,
        actionPointerId: Int?,
        timestampMillis: Long,
        vararg pointers: MicroscopePointer
    ) = MicroscopePointerEvent(
        action = action,
        actionPointerId = actionPointerId,
        pointers = pointers.toList(),
        timestampMillis = timestampMillis
    )

    private fun pointer(id: Int, x: Float, y: Float) = MicroscopePointer(id, x, y)

    private fun view(
        action: MicroscopePointerAction,
        actionPointerId: Int?,
        timestampMillis: Long,
        vararg pointers: MicroscopePointer
    ): MicroscopePointerEventView = object : MicroscopePointerEventView {
        override val action = action
        override val actionPointerId = actionPointerId
        override val pointerCount = pointers.size
        override val timestampMillis = timestampMillis

        override fun pointerId(index: Int) = pointers[index].id

        override fun pointerX(index: Int) = pointers[index].x

        override fun pointerY(index: Int) = pointers[index].y
    }

    private class RecordingSink : MicroscopeInputCommandSink {
        val commands = mutableListOf<MicroscopeInputCommand>()

        override fun touchDown(pointerId: Int, x: Float, y: Float) {
            commands += MicroscopeInputCommand.TouchDown(pointerId, x, y)
        }

        override fun touchMove(pointerId: Int, x: Float, y: Float) {
            commands += MicroscopeInputCommand.TouchMove(pointerId, x, y)
        }

        override fun touchUp(pointerId: Int) {
            commands += MicroscopeInputCommand.TouchUp(pointerId)
        }

        override fun pan(deltaX: Float, deltaY: Float) {
            commands += MicroscopeInputCommand.Pan(deltaX, deltaY)
        }

        override fun zoom(factor: Float, centerX: Float, centerY: Float) {
            commands += MicroscopeInputCommand.Zoom(factor, centerX, centerY)
        }

        override fun zoomEnded() {
            commands += MicroscopeInputCommand.ZoomEnded
        }

        override fun tap(x: Float, y: Float) {
            commands += MicroscopeInputCommand.Tap(x, y)
        }

        override fun fling(velocityX: Float, velocityY: Float) {
            commands += MicroscopeInputCommand.Fling(velocityX, velocityY)
        }
    }
}
