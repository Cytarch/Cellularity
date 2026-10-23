package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class MicroscopePresentationStateMachineTest {
    @Test
    fun lifecycleStartsPausedUntilTheHostExplicitlyResumesIt() {
        val state = MicroscopePresentationStateMachine(
            initialTemperature = MicroscopeTemperature.INCUBATE
        )
        state.onAttached()

        assertFalse(state.lifecycleResumed)
        assertFalse(state.shouldRequestInitialFrame())

        state.onLifecycleResumed()

        assertTrue(state.lifecycleResumed)
        assertTrue(state.shouldRequestInitialFrame())
    }

    @Test
    fun observeIdleKeepsContinuousPresentationWithoutPacedCallbacks() {
        val state = attachedState(MicroscopeTemperature.OBSERVE)

        assertEquals(MicroscopeRenderCadence.CONTINUOUS, state.basePlan.cadence)
        assertEquals(MicroscopeRenderCadence.CONTINUOUS, state.effectiveCadence)
        assertFalse(state.shouldRunFramePacedCallback())
        assertEquals(TEST_PUBLICATION_INTERVAL_MILLIS, state.basePlan.statusPollIntervalMillis)
        assertFalse(state.shouldRunActiveCameraCallback())
    }

    @Test
    fun incubateUsesOneIndependentPresentationTimerInsteadOfSimulationUpdates() {
        val state = attachedState(MicroscopeTemperature.INCUBATE)

        assertEquals(MicroscopeRenderCadence.ON_DEMAND, state.basePlan.cadence)
        assertEquals(
            MicroscopePresentationPolicy.INCUBATE_FRAME_INTERVAL_MILLIS,
            state.basePlan.frameIntervalMillis
        )
        assertEquals(TEST_PUBLICATION_INTERVAL_MILLIS, state.basePlan.statusPollIntervalMillis)
        assertTrue(state.shouldRunFramePacedCallback())
        assertTrue(state.shouldWakeForQueuedInput())

        // The presentation state has no simulation-update input. Repeated
        // status reads cannot turn Incubate into one render request per UPS.
        repeat(1_000) {
            assertEquals(MicroscopeRenderCadence.ON_DEMAND, state.effectiveCadence)
        }
        assertTrue(state.shouldRunFramePacedCallback())
    }

    @Test
    fun cameraMotionChangesOnlyTemporaryPresentationCadence() {
        val state = attachedState(MicroscopeTemperature.INCUBATE)
        val basePlan = state.basePlan

        state.onPointerGestureStarted()
        assertEquals(
            MicroscopeInputPresentationAction.START_ACTIVE_CAMERA,
            state.onInputCommandsExecuted(cameraChanged = true)
        )

        assertEquals(MicroscopeTemperature.INCUBATE, state.temperature)
        assertEquals(basePlan, state.basePlan)
        assertEquals(MicroscopeRenderCadence.CONTINUOUS, state.effectiveCadence)
        assertTrue(state.shouldRunActiveCameraCallback())
        assertFalse(state.shouldRunFramePacedCallback())
        assertFalse(state.shouldWakeForQueuedInput())

        state.onPointerGestureFinished()
        assertTrue(state.onVsync(10, 1L, false, false))
        assertTrue(state.onVsync(20, 1L, false, false))
        assertTrue(state.onVsync(30, 1L, false, false))
        assertFalse(state.onVsync(40, 1L, false, false))

        assertEquals(basePlan, state.basePlan)
        assertEquals(MicroscopeRenderCadence.ON_DEMAND, state.effectiveCadence)
        assertTrue(state.shouldRunFramePacedCallback())
    }

    @Test
    fun rejectedCameraCommandNeverStartsAnActiveCadence() {
        val state = attachedState(MicroscopeTemperature.INCUBATE)
        state.onPointerGestureStarted()

        assertEquals(
            MicroscopeInputPresentationAction.REQUEST_FRAME,
            state.onInputCommandsExecuted(cameraChanged = false)
        )
        assertFalse(state.activeCameraRendering)
        assertFalse(state.shouldRunActiveCameraCallback())
        assertEquals(MicroscopeRenderCadence.ON_DEMAND, state.effectiveCadence)
        assertFalse(state.onVsync(10L, 1L, false, false))
    }

    @Test
    fun hidingAndRestoringTabPreservesIncubatePolicy() {
        val state = attachedState(MicroscopeTemperature.INCUBATE)

        assertTrue(state.setPresentationVisible(false))
        assertEquals(MicroscopeTemperature.INCUBATE, state.temperature)
        assertEquals(MicroscopeRenderCadence.ON_DEMAND, state.basePlan.cadence)
        assertNull(state.basePlan.frameIntervalMillis)
        assertEquals(
            MicroscopePresentationPolicy.NO_RENDER_STATUS_INTERVAL_MILLIS,
            state.basePlan.statusPollIntervalMillis
        )
        assertEquals(
            MicroscopeInputPresentationAction.NONE,
            state.onInputCommandsExecuted(cameraChanged = true)
        )
        assertFalse(state.shouldRunFramePacedCallback())
        assertFalse(state.shouldWakeForQueuedInput())

        assertTrue(state.setPresentationVisible(true))
        assertEquals(MicroscopeTemperature.INCUBATE, state.temperature)
        assertEquals(
            MicroscopePresentationPolicy.INCUBATE_FRAME_INTERVAL_MILLIS,
            state.basePlan.frameIntervalMillis
        )
        assertTrue(state.shouldRunFramePacedCallback())
    }

    @Test
    fun pauseAndResumeCannotRetainAnActiveCameraFlag() {
        val state = attachedState(MicroscopeTemperature.INCUBATE)
        state.onPointerGestureStarted()
        state.onInputCommandsExecuted(cameraChanged = true)
        assertTrue(state.activeCameraRendering)

        state.onLifecyclePaused()
        assertFalse(state.activeCameraRendering)
        assertFalse(state.pointerGestureInProgress)
        assertFalse(state.shouldRunActiveCameraCallback())
        assertFalse(state.shouldRunFramePacedCallback())
        assertFalse(state.shouldRequestInitialFrame())
        assertFalse(state.shouldWakeForQueuedInput())
        assertEquals(
            MicroscopeInputPresentationAction.NONE,
            state.onInputCommandsExecuted(cameraChanged = true)
        )
        assertFalse(state.activeCameraRendering)

        state.onLifecycleResumed()
        assertFalse(state.activeCameraRendering)
        assertEquals(MicroscopeRenderCadence.ON_DEMAND, state.effectiveCadence)
        assertFalse(state.shouldRunActiveCameraCallback())
        assertTrue(state.shouldRunFramePacedCallback())
        assertTrue(state.shouldRequestInitialFrame())
        assertFalse(state.onVsync(50, 2L, false, false))
    }

    @Test
    fun noRenderModePollsStatusWithoutRecurringFramePresentation() {
        val state = attachedState(MicroscopeTemperature.INCUBATE_NO_RENDER)

        assertEquals(MicroscopeRenderCadence.ON_DEMAND, state.basePlan.cadence)
        assertTrue(state.basePlan.requestInitialFrame)
        assertNull(state.basePlan.frameIntervalMillis)
        assertEquals(
            MicroscopePresentationPolicy.NO_RENDER_STATUS_INTERVAL_MILLIS,
            state.basePlan.statusPollIntervalMillis
        )
        assertEquals(
            MicroscopeInputPresentationAction.NONE,
            state.onInputCommandsExecuted(cameraChanged = true)
        )
        assertFalse(state.shouldRunFramePacedCallback())
        assertFalse(state.shouldRunActiveCameraCallback())
    }

    private fun attachedState(
        temperature: MicroscopeTemperature
    ): MicroscopePresentationStateMachine = MicroscopePresentationStateMachine(
        initialTemperature = temperature,
        publicationIntervalMillis = TEST_PUBLICATION_INTERVAL_MILLIS,
        activeCameraCadence = ActiveCameraPresentationCadence(stableFrameLimit = 3)
    ).also {
        it.onAttached()
        it.onLifecycleResumed()
    }

    private companion object {
        const val TEST_PUBLICATION_INTERVAL_MILLIS = 47L
    }
}
