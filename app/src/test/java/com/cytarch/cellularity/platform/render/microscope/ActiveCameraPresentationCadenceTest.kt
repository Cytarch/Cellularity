package com.cytarch.cellularity.platform.render.microscope

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ActiveCameraPresentationCadenceTest {
    @Test
    fun pointerGestureAndFlingSettleOnDisplayFramesInsteadOfInputRate() {
        val cadence = ActiveCameraPresentationCadence(stableFrameLimit = 3)
        val interval = 16_666_667L
        var frameTime = 1_000_000_000L

        repeat(240) {
            cadence.onCameraCommand(pointerGestureActive = true)
        }
        assertTrue(cadence.active)
        assertEquals(0, cadence.telemetrySnapshot().callbackCount)

        assertTrue(cadence.onVsync(frameTime, 0L, false, false))
        frameTime += interval
        assertTrue(cadence.onVsync(frameTime, 1L, false, false))
        cadence.onPointerGestureFinished()

        frameTime += interval
        assertTrue(cadence.onVsync(frameTime, 2L, false, false))
        frameTime += interval
        assertTrue(cadence.onVsync(frameTime, 2L, false, false))
        frameTime += interval
        assertTrue(cadence.onVsync(frameTime, 2L, false, false))
        frameTime += interval
        assertFalse(cadence.onVsync(frameTime, 2L, false, false))

        val telemetry = cadence.telemetrySnapshot()
        assertFalse(telemetry.active)
        assertEquals(6, telemetry.callbackCount)
        assertArrayEquals(LongArray(5) { interval }, telemetry.frameIntervalsNanos)
    }

    @Test
    fun nativeInertiaKeepsPresentationActiveEvenBeforeItsFirstCameraStep() {
        val cadence = ActiveCameraPresentationCadence(stableFrameLimit = 2)
        cadence.onCameraCommand(pointerGestureActive = false)

        assertTrue(cadence.onVsync(10, 0L, true, true))
        assertTrue(cadence.onVsync(20, 0L, true, true))
        assertTrue(cadence.onVsync(30, 1L, true, true))
        assertTrue(cadence.onVsync(40, 1L, false, false))
        assertFalse(cadence.onVsync(50, 1L, false, false))
    }

    @Test
    fun nativeCameraMotionFlagKeepsPresentationActive() {
        val cadence = ActiveCameraPresentationCadence(stableFrameLimit = 2)
        cadence.onCameraCommand(pointerGestureActive = false)

        assertTrue(cadence.onVsync(10, 3L, false, true))
        assertTrue(cadence.onVsync(20, 3L, false, false))
        assertFalse(cadence.onVsync(30, 3L, false, false))
        cadence.reset()
        assertFalse(cadence.active)
        assertEquals(0, cadence.telemetrySnapshot().callbackCount)
    }

    @Test
    fun unavailableMotionSnapshotSettlesInsteadOfLeavingAHotLoop() {
        val cadence = ActiveCameraPresentationCadence(stableFrameLimit = 2)
        cadence.onCameraCommand(pointerGestureActive = false)

        assertTrue(cadence.onVsync(10, 0L, false, false))
        assertTrue(cadence.onVsync(20, 0L, false, false))
        assertFalse(cadence.onVsync(30, 0L, false, false))
    }
}
