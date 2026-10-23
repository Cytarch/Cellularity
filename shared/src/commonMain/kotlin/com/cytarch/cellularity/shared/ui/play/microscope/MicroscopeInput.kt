package com.cytarch.cellularity.shared.ui.play.microscope

import com.cytarch.cellularity.shared.model.immutableListCopy
import kotlin.math.abs
import kotlin.math.ln
import kotlin.math.sqrt

data class MicroscopePointer(
    val id: Int,
    val x: Float,
    val y: Float
)

enum class MicroscopePointerAction {
    DOWN,
    POINTER_DOWN,
    MOVE,
    UP,
    POINTER_UP,
    CANCEL
}

interface MicroscopePointerEventView {
    val action: MicroscopePointerAction
    val actionPointerId: Int?
    val pointerCount: Int
    val timestampMillis: Long

    fun pointerId(index: Int): Int

    fun pointerX(index: Int): Float

    fun pointerY(index: Int): Float
}

class MicroscopePointerEvent(
    override val action: MicroscopePointerAction,
    override val actionPointerId: Int?,
    pointers: Iterable<MicroscopePointer>,
    override val timestampMillis: Long
) : MicroscopePointerEventView {
    val pointers: List<MicroscopePointer> = immutableListCopy(pointers)

    override val pointerCount: Int
        get() = pointers.size

    override fun pointerId(index: Int): Int = pointers[index].id

    override fun pointerX(index: Int): Float = pointers[index].x

    override fun pointerY(index: Int): Float = pointers[index].y
}

sealed interface MicroscopeInputCommand {
    data class TouchDown(val pointerId: Int, val x: Float, val y: Float) :
        MicroscopeInputCommand

    data class TouchMove(val pointerId: Int, val x: Float, val y: Float) :
        MicroscopeInputCommand

    data class TouchUp(val pointerId: Int) : MicroscopeInputCommand

    data class Pan(val deltaX: Float, val deltaY: Float) : MicroscopeInputCommand

    data class Zoom(
        val factor: Float,
        val centerX: Float,
        val centerY: Float
    ) : MicroscopeInputCommand

    data object ZoomEnded : MicroscopeInputCommand

    data class Tap(val x: Float, val y: Float) : MicroscopeInputCommand

    data class Fling(val velocityX: Float, val velocityY: Float) : MicroscopeInputCommand
}

interface MicroscopeInputCommandSink {
    fun touchDown(pointerId: Int, x: Float, y: Float)

    fun touchMove(pointerId: Int, x: Float, y: Float)

    fun touchUp(pointerId: Int)

    fun pan(deltaX: Float, deltaY: Float)

    fun zoom(factor: Float, centerX: Float, centerY: Float)

    fun zoomEnded()

    fun tap(x: Float, y: Float)

    fun fling(velocityX: Float, velocityY: Float)
}

/**
 * Platform-neutral interpretation of the microscope's existing one-finger pan and two-finger
 * pan/pinch behavior. Android only translates MotionEvent snapshots into this contract.
 */
class MicroscopeGestureInterpreter(
    initialTimestampMillis: Long? = null
) {
    private var previousX = 0f
    private var previousY = 0f
    private var previousMiddleX = 0f
    private var previousMiddleY = 0f
    private var previousDistance = 0f
    private var previousPointerCount = -1
    private var lastZoomTimestampMillis: Long? = initialTimestampMillis

    fun onPointerEvent(event: MicroscopePointerEvent): List<MicroscopeInputCommand> {
        val commands = mutableListOf<MicroscopeInputCommand>()
        onPointerEvent(
            event,
            object : MicroscopeInputCommandSink {
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
        )
        return commands
    }

    fun onPointerEvent(event: MicroscopePointerEventView, sink: MicroscopeInputCommandSink) {
        val actionPointerId = event.actionPointerId
        var actionPointerIndex = -1
        if (actionPointerId != null) {
            for (index in 0 until event.pointerCount) {
                if (event.pointerId(index) == actionPointerId) {
                    actionPointerIndex = index
                    break
                }
            }
        }

        when (event.action) {
            MicroscopePointerAction.MOVE -> for (index in 0 until event.pointerCount) {
                sink.touchMove(
                    event.pointerId(index),
                    event.pointerX(index),
                    event.pointerY(index)
                )
            }
            MicroscopePointerAction.DOWN,
            MicroscopePointerAction.POINTER_DOWN -> if (actionPointerIndex >= 0) {
                sink.touchDown(
                    event.pointerId(actionPointerIndex),
                    event.pointerX(actionPointerIndex),
                    event.pointerY(actionPointerIndex)
                )
            }
            MicroscopePointerAction.UP,
            MicroscopePointerAction.POINTER_UP -> event.actionPointerId?.let { pointerId ->
                sink.touchUp(pointerId)
            }
            MicroscopePointerAction.CANCEL -> for (index in 0 until event.pointerCount) {
                sink.touchUp(event.pointerId(index))
            }
        }

        val hasFirst = event.pointerCount > 0
        val firstX = if (hasFirst) event.pointerX(0) else 0f
        val firstY = if (hasFirst) event.pointerY(0) else 0f
        val pointerCount = event.pointerCount
        var distance = -1f
        var middleX = -1f
        var middleY = -1f
        if ((pointerCount == 2 || pointerCount == 3) && hasFirst) {
            val secondX = event.pointerX(1)
            val secondY = event.pointerY(1)
            val deltaX = secondX - firstX
            val deltaY = secondY - firstY
            distance = sqrt((deltaX * deltaX + deltaY * deltaY).toDouble()).toFloat()
            middleX = (firstX + secondX) / 2f
            middleY = (firstY + secondY) / 2f
        }

        if (event.action == MicroscopePointerAction.MOVE && hasFirst) {
            if (pointerCount == 1 && previousPointerCount == 1) {
                sink.pan(firstX - previousX, firstY - previousY)
            }
            if (pointerCount == 2 && previousPointerCount == 2) {
                sink.pan(
                    middleX - previousMiddleX,
                    middleY - previousMiddleY
                )
                lastZoomTimestampMillis = event.timestampMillis
                val ratio = distance / previousDistance
                if (
                    ratio.isFinite() && ratio > 0f &&
                    abs(ln(ratio.toDouble())) < MAX_LOG_ZOOM_DELTA
                ) {
                    sink.zoom(ratio, middleX, middleY)
                }
            }
        }

        if (
            event.action == MicroscopePointerAction.UP ||
            event.action == MicroscopePointerAction.CANCEL
        ) {
            sink.zoomEnded()
        }

        if (pointerCount == 2) {
            previousMiddleX = middleX
            previousMiddleY = middleY
            previousDistance = distance
        }
        if (hasFirst) {
            previousX = firstX
            previousY = firstY
        }
        previousPointerCount = if (event.action == MicroscopePointerAction.CANCEL) {
            -1
        } else {
            pointerCount
        }
    }

    fun onTap(x: Float, y: Float): MicroscopeInputCommand =
        MicroscopeInputCommand.Tap(x, y)

    fun onTap(x: Float, y: Float, sink: MicroscopeInputCommandSink) {
        sink.tap(x, y)
    }

    fun onFling(
        velocityX: Float,
        velocityY: Float,
        timestampMillis: Long
    ): MicroscopeInputCommand? {
        val lastZoom = lastZoomTimestampMillis
        return if (lastZoom == null || timestampMillis - lastZoom > FLING_AFTER_ZOOM_DELAY_MS) {
            MicroscopeInputCommand.Fling(velocityX, velocityY)
        } else {
            null
        }
    }

    fun onFling(
        velocityX: Float,
        velocityY: Float,
        timestampMillis: Long,
        sink: MicroscopeInputCommandSink
    ): Boolean {
        val lastZoom = lastZoomTimestampMillis
        if (lastZoom != null && timestampMillis - lastZoom <= FLING_AFTER_ZOOM_DELAY_MS) {
            return false
        }
        sink.fling(velocityX, velocityY)
        return true
    }

    fun reset(timestampMillis: Long) {
        previousX = 0f
        previousY = 0f
        previousMiddleX = 0f
        previousMiddleY = 0f
        previousDistance = 0f
        previousPointerCount = -1
        lastZoomTimestampMillis = timestampMillis
    }

    private companion object {
        const val MAX_LOG_ZOOM_DELTA = 0.3
        const val FLING_AFTER_ZOOM_DELAY_MS = 200L
    }
}
