package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature

/**
 * Backend-independent presentation state for the microscope host.
 *
 * Android callbacks apply the resulting cadence and one-shot actions to a
 * concrete surface. This class never owns a View, render thread or scheduler.
 */
internal class MicroscopePresentationStateMachine(
    initialTemperature: MicroscopeTemperature = MicroscopeTemperature.FREEZE,
    private val publicationIntervalMillis: Long =
        MicroscopePresentationPolicy.TEST_PUBLICATION_INTERVAL_MILLIS,
    private val activeCameraCadence: ActiveCameraPresentationCadence =
        ActiveCameraPresentationCadence()
) {
    var attached: Boolean = false
        private set
    var lifecycleResumed: Boolean = false
        private set
    var presentationVisible: Boolean = true
        private set
    var temperature: MicroscopeTemperature = initialTemperature
        private set
    var pointerGestureInProgress: Boolean = false
        private set

    var basePlan: MicroscopePresentationPlan = createBasePlan()
        private set

    val effectiveCadence: MicroscopeRenderCadence
        get() = if (shouldRunActiveCameraCallback()) {
            MicroscopeRenderCadence.CONTINUOUS
        } else {
            basePlan.cadence
        }

    val activeCameraRendering: Boolean
        get() = activeCameraCadence.active

    fun onAttached() {
        attached = true
        resetActiveCameraCadence()
    }

    fun onDetached() {
        attached = false
        pointerGestureInProgress = false
        resetActiveCameraCadence()
    }

    fun onLifecycleResumed() {
        lifecycleResumed = true
        resetActiveCameraCadence()
    }

    fun onLifecyclePaused() {
        lifecycleResumed = false
        pointerGestureInProgress = false
        resetActiveCameraCadence()
    }

    /** Returns true when the base presentation plan changed. */
    fun setPresentationVisible(visible: Boolean): Boolean {
        if (presentationVisible == visible) return false
        presentationVisible = visible
        if (!visible) pointerGestureInProgress = false
        basePlan = createBasePlan()
        resetActiveCameraCadence()
        return true
    }

    /** Returns true when the base presentation plan changed. */
    fun setTemperature(value: MicroscopeTemperature): Boolean {
        if (temperature == value) return false
        temperature = value
        basePlan = createBasePlan()
        resetActiveCameraCadence()
        return true
    }

    fun onPointerGestureStarted() {
        pointerGestureInProgress = true
    }

    fun onPointerGestureFinished() {
        pointerGestureInProgress = false
        activeCameraCadence.onPointerGestureFinished()
    }

    fun onInputCommandsExecuted(cameraChanged: Boolean): MicroscopeInputPresentationAction {
        if (
            !attached ||
            !lifecycleResumed ||
            !presentationVisible ||
            temperature != MicroscopeTemperature.INCUBATE
        ) {
            return MicroscopeInputPresentationAction.NONE
        }
        if (cameraChanged) {
            activeCameraCadence.onCameraCommand(pointerGestureInProgress)
            return MicroscopeInputPresentationAction.START_ACTIVE_CAMERA
        }
        return if (
            effectiveCadence == MicroscopeRenderCadence.ON_DEMAND &&
            !activeCameraCadence.active
        ) {
            MicroscopeInputPresentationAction.REQUEST_FRAME
        } else {
            MicroscopeInputPresentationAction.NONE
        }
    }

    /** Returns whether display-paced active-camera rendering should continue. */
    fun onVsync(
        frameTimeNanos: Long,
        cameraRevision: Long,
        inertiaActive: Boolean,
        cameraMotionActive: Boolean
    ): Boolean = activeCameraCadence.onVsync(
        frameTimeNanos,
        cameraRevision,
        inertiaActive,
        cameraMotionActive
    )

    fun shouldRunFramePacedCallback(): Boolean =
        lifecycleResumed && attached &&
            basePlan.frameIntervalMillis != null &&
            !(
                presentationVisible &&
                    temperature == MicroscopeTemperature.INCUBATE &&
                    activeCameraCadence.active
                )

    fun shouldRequestInitialFrame(): Boolean =
        lifecycleResumed && attached && basePlan.requestInitialFrame

    fun shouldWakeForQueuedInput(): Boolean =
        lifecycleResumed && attached && presentationVisible &&
            temperature == MicroscopeTemperature.INCUBATE &&
            effectiveCadence == MicroscopeRenderCadence.ON_DEMAND

    fun shouldRunActiveCameraCallback(): Boolean =
        lifecycleResumed && presentationVisible && attached &&
            temperature == MicroscopeTemperature.INCUBATE &&
            activeCameraCadence.active

    fun resetActiveCameraCadence() {
        activeCameraCadence.reset()
    }

    fun activeCameraTelemetry(): ActiveCameraCadenceTelemetry =
        activeCameraCadence.telemetrySnapshot()

    private fun createBasePlan(): MicroscopePresentationPlan =
        MicroscopePresentationPolicy.plan(
            temperature = temperature,
            presentationVisible = presentationVisible,
            publicationIntervalMillis = publicationIntervalMillis
        )
}

internal enum class MicroscopeInputPresentationAction {
    NONE,
    REQUEST_FRAME,
    START_ACTIVE_CAMERA
}
