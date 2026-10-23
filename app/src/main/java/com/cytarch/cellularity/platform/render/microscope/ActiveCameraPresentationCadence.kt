package com.cytarch.cellularity.platform.render.microscope

/**
 * Tracks the lifetime of temporary continuous camera rendering. It neither
 * changes camera input nor issues render requests: the surface producer is
 * display-paced while this state is active. The native snapshot keeps it
 * alive for the exact lifetime of fling inertia; several stable samples after
 * it clears absorb the final publication without a device-specific duration
 * estimate.
 */
internal class ActiveCameraPresentationCadence(
    private val stableFrameLimit: Int = DEFAULT_STABLE_FRAME_LIMIT
) {
    private var gestureActive = false
    private var settling = false
    private var stableFrameCount = 0
    private var lastCameraRevision: Long? = null
    private var lastFrameTimeNanos = 0L
    private val frameIntervalsNanos = LongArray(TELEMETRY_CAPACITY)
    private var intervalStart = 0
    private var intervalCount = 0
    private var callbackCount = 0L

    init {
        require(stableFrameLimit > 0) { "Stable frame limit must be positive" }
    }

    val active: Boolean
        get() = gestureActive || settling

    fun onCameraCommand(pointerGestureActive: Boolean) {
        if (!active) resetTelemetry()
        gestureActive = gestureActive || pointerGestureActive
        settling = true
        stableFrameCount = 0
    }

    fun onPointerGestureFinished() {
        gestureActive = false
    }

    /** Returns whether continuous camera rendering should remain enabled. */
    fun onVsync(
        frameTimeNanos: Long,
        cameraRevision: Long,
        inertiaActive: Boolean,
        cameraMotionActive: Boolean
    ): Boolean {
        if (!active) return false
        recordFrameTime(frameTimeNanos)

        val changed = lastCameraRevision == null || cameraRevision != lastCameraRevision
        lastCameraRevision = cameraRevision

        if (gestureActive || inertiaActive || cameraMotionActive) {
            stableFrameCount = 0
            return true
        }

        stableFrameCount = if (changed) 0 else stableFrameCount + 1
        if (stableFrameCount >= stableFrameLimit) {
            settling = false
        }
        return active
    }

    fun reset() {
        gestureActive = false
        settling = false
        stableFrameCount = 0
        lastCameraRevision = null
        resetTelemetry()
    }

    fun telemetrySnapshot(): ActiveCameraCadenceTelemetry {
        val intervals = LongArray(intervalCount)
        for (index in 0 until intervalCount) {
            intervals[index] = frameIntervalsNanos[
                (intervalStart + index) % TELEMETRY_CAPACITY
            ]
        }
        return ActiveCameraCadenceTelemetry(
            active = active,
            callbackCount = callbackCount,
            frameIntervalsNanos = intervals
        )
    }

    private fun recordFrameTime(frameTimeNanos: Long) {
        callbackCount += 1
        if (lastFrameTimeNanos > 0L && frameTimeNanos > lastFrameTimeNanos) {
            val interval = frameTimeNanos - lastFrameTimeNanos
            if (intervalCount < TELEMETRY_CAPACITY) {
                frameIntervalsNanos[(intervalStart + intervalCount) % TELEMETRY_CAPACITY] =
                    interval
                intervalCount += 1
            } else {
                frameIntervalsNanos[intervalStart] = interval
                intervalStart = (intervalStart + 1) % TELEMETRY_CAPACITY
            }
        }
        lastFrameTimeNanos = frameTimeNanos
    }

    private fun resetTelemetry() {
        lastFrameTimeNanos = 0L
        intervalStart = 0
        intervalCount = 0
        callbackCount = 0L
    }

    private companion object {
        const val DEFAULT_STABLE_FRAME_LIMIT = 3
        const val TELEMETRY_CAPACITY = 240
    }
}

internal data class ActiveCameraCadenceTelemetry(
    val active: Boolean,
    val callbackCount: Long,
    val frameIntervalsNanos: LongArray
)
