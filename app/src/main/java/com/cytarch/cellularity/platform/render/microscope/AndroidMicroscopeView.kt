package com.cytarch.cellularity.platform.render.microscope

import android.annotation.SuppressLint
import android.content.Context
import android.os.SystemClock
import android.view.Choreographer
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ViewGroup
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.findViewTreeLifecycleOwner
import com.cytarch.cellularity.BuildConfig
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.RenderPerformanceTelemetry
import com.cytarch.cellularity.platform.render.RenderSurfaceLifecycle
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeGestureInterpreter
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopePointerAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopePointerEventView
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTool

/**
 * Backend-neutral Android host for microscope input, cadence and lifecycle.
 * The child [MicroscopeRenderSurfaceHost] owns all concrete surface mechanics.
 */
@SuppressLint("ViewConstructor")
internal class AndroidMicroscopeView(
    context: Context,
    private val endpoint: MicroscopeRenderEndpoint,
    private val infoReceiver: MicroscopeInfoReceiver,
    private val substratePreviewCapture: SubstratePreviewCaptureCoordinator,
    override val backendKind: RenderBackendKind,
    surfaceHostFactory: MicroscopeRenderSurfaceHostFactory,
    onRuntimeFailure: (String) -> Unit
) : AndroidMicroscopeRenderHost(context) {
    private val presentationState = MicroscopePresentationStateMachine(
        publicationIntervalMillis =
            endpoint.runtimePublicationSource.publicationIntervalMillis
    )
    @Volatile
    private var released = false
    private val statusPoller = MicroscopeStatusPoller(
        source = endpoint.runtimePublicationSource,
        receiver = infoReceiver,
        host = object : MicroscopeStatusPoller.Host {
            override fun post(action: Runnable): Boolean =
                this@AndroidMicroscopeView.post(action)

            override fun postDelayed(action: Runnable, delayMillis: Long): Boolean =
                this@AndroidMicroscopeView.postDelayed(action, delayMillis)

            override fun removeCallbacks(action: Runnable) {
                this@AndroidMicroscopeView.removeCallbacks(action)
            }
        },
        onInputIngressSnapshot = ::onInputIngressSnapshot
    )
    private val renderSurface: MicroscopeRenderSurfaceHost
    private val gestureInterpreter = MicroscopeGestureInterpreter(SystemClock.uptimeMillis())
    private val pointerEventView = MotionEventMicroscopePointerEventView()
    private val inputBatchBuilder = AndroidMicroscopeInputBatchBuilder()
    private val presentationMotionReader = PresentationMotionSnapshotReader()
    private val gestureDetector: GestureDetector
    private var activeInputCollector: AndroidMicroscopeInputBatchBuilder? = null
    private val inputStream = MicroscopeInputStreamState()
    private var submittedControls: MicroscopeControls? = null
    private var publishedPlacementGenome: GenomeDraft? = null
    private var placementGenomeRevision = 0L
    private var hostLifecycleOwner: LifecycleOwner? = null
    private val deferredResume = Runnable {
        val owner = hostLifecycleOwner
        if (
            !released &&
            isAttachedToWindow &&
            owner != null &&
            owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)
        ) {
            resumeRenderThread()
        }
    }

    @Volatile
    private var previewSurfaceReady = false

    private val substratePreviewEndpoint = object : SubstratePreviewCaptureEndpoint {
        override fun enqueueSubstratePreviewCapture(
            requestId: Long,
            substratePreviewToken: Long
        ): NativePreviewCaptureRequest? {
            if (
                released ||
                !previewSurfaceReady ||
                !presentationState.lifecycleResumed
            ) {
                return null
            }
            val request = endpoint.enqueueSubstratePreviewCapture(
                requestId,
                substratePreviewToken
            ) ?: return null
            renderSurface.requestFrame()
            return request
        }
    }

    private val choreographer = Choreographer.getInstance()
    private var activeCameraFramePosted = false
    private val activeCameraFrameCallback = Choreographer.FrameCallback { frameTimeNanos ->
        activeCameraFramePosted = false
        if (!presentationState.shouldRunActiveCameraCallback()) return@FrameCallback
        val snapshotAvailable = endpoint.readPresentationMotionSnapshot(
            presentationMotionReader.buffer
        )
        if (snapshotAvailable) {
            presentationMotionReader.decode()
        }
        if (
            presentationState.onVsync(
                frameTimeNanos,
                presentationMotionReader.cameraRevision,
                snapshotAvailable && presentationMotionReader.inertiaActive,
                snapshotAvailable && presentationMotionReader.cameraMotionActive
            )
        ) {
            scheduleActiveCameraFrame()
        } else {
            finishActiveCameraRendering()
        }
    }
    private val framePacedCallback = object : Runnable {
        override fun run() {
            if (!presentationState.shouldRunFramePacedCallback()) return
            renderSurface.requestFrame()
            scheduleFramePacedCallback()
        }
    }
    private val hostLifecycleObserver = object : DefaultLifecycleObserver {
        override fun onResume(owner: LifecycleOwner) = scheduleRenderThreadResume()

        override fun onPause(owner: LifecycleOwner) = pauseRenderThread()

        override fun onDestroy(owner: LifecycleOwner) = pauseRenderThread()
    }

    override var inputEnabled: Boolean = true

    init {
        renderSurface = surfaceHostFactory.create(
            context = context,
            renderEndpoint = endpoint,
            lifecycle = object : RenderSurfaceLifecycle {
                override fun onSurfaceCreated() {
                    endpoint.onSurfaceCreated()
                    previewSurfaceReady = true
                }

                override fun onSurfaceChanged(width: Int, height: Int) {
                    endpoint.onSurfaceChanged(width, height)
                }

                override fun onRenderFrame(): Long = renderFrame()

                override fun onSurfaceDestroyed() {
                    previewSurfaceReady = false
                    endpoint.onSurfaceDestroyed()
                }
            },
            platformCallbacks = object : MicroscopePlatformSurfaceCallbacks {
                override fun onSurfaceAvailable() {
                    previewSurfaceReady = true
                    renderSurface.requestMaximumDisplayRefreshRate()
                    statusPoller.requestImmediatePoll()
                    if (presentationState.shouldRequestInitialFrame()) {
                        renderSurface.requestFrame()
                    }
                }

                override fun onSurfaceDestroying() {
                    previewSurfaceReady = false
                    enterInputResync(SystemClock.uptimeMillis(), requestNative = true)
                }
            },
            onRuntimeFailure = onRuntimeFailure
        )
        addView(
            renderSurface.view,
            ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
        isClickable = true

        gestureDetector = GestureDetector(
            context,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onDown(e: MotionEvent): Boolean = true

                override fun onDoubleTap(e: MotionEvent): Boolean = true

                override fun onSingleTapUp(e: MotionEvent): Boolean {
                    collectDetectorCommand(e.eventTime) { collector ->
                        gestureInterpreter.onTap(e.x, e.y, collector)
                    }
                    return true
                }

                override fun onFling(
                    e1: MotionEvent?,
                    e2: MotionEvent,
                    velocityX: Float,
                    velocityY: Float
                ): Boolean {
                    collectDetectorCommand(e2.eventTime) { collector ->
                        gestureInterpreter.onFling(
                            velocityX,
                            velocityY,
                            e2.eventTime,
                            collector
                        )
                    }
                    return true
                }
            }
        )
    }

    override fun updateControls(
        tool: MicroscopeTool,
        temperature: MicroscopeTemperature,
        incubateTargetUps: Double,
        performanceHudEnabled: Boolean
    ) {
        val controls = MicroscopeControls(
            tool,
            temperature,
            incubateTargetUps,
            performanceHudEnabled
        )
        if (controls == submittedControls) return
        submittedControls = controls
        val temperatureChanged = presentationState.setTemperature(controls.temperature)
        endpoint.setToolOrdinal(controls.tool.nativeOrdinal)
        endpoint.setTemperatureMode(controls.temperature.nativeMode)
        endpoint.setIncubateTargetUps(controls.incubateTargetUps)
        endpoint.setPerformanceHudEnabled(controls.performanceHudEnabled)
        if (temperatureChanged) applyRenderCadence()
    }

    override fun updatePlacementGenome(genome: GenomeDraft) {
        if (genome == publishedPlacementGenome) return
        val revision = endpoint.publishPlacementGenome(genome)
        if (revision == 0L) return
        publishedPlacementGenome = genome
        placementGenomeRevision = revision
    }

    private fun renderFrame(): Long =
        if (presentationState.temperature == MicroscopeTemperature.INCUBATE_NO_RENDER) {
            endpoint.clearWithoutRendering()
        } else {
            endpoint.draw()
        }

    /**
     * Stops only presentation while another Play tab covers this host. The
     * simulation remains resumed so Incubate can continue without drawing an
     * invisible dish underneath UI.
     */
    override fun setPresentationVisible(visible: Boolean) {
        if (!presentationState.setPresentationVisible(visible)) return
        applyRenderCadence()
        if (visible) renderSurface.requestMaximumDisplayRefreshRate()
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        presentationState.onAttached()
        substratePreviewCapture.attach(substratePreviewEndpoint)
        bindHostLifecycle()
        renderSurface.requestMaximumDisplayRefreshRate()
        applyRenderCadence()
    }

    override fun onDetachedFromWindow() {
        detachFromHost()
        super.onDetachedFromWindow()
    }

    override fun release() {
        if (released) return
        released = true
        detachFromHost()
        statusPoller.release()
        renderSurface.release()
    }

    private fun detachFromHost() {
        substratePreviewCapture.detach(substratePreviewEndpoint)
        removeCallbacks(framePacedCallback)
        cancelActiveCameraFrame()
        removeCallbacks(deferredResume)
        presentationState.onDetached()
        updateStatusPolling()
        pauseRenderThread()
        hostLifecycleOwner?.lifecycle?.removeObserver(hostLifecycleObserver)
        hostLifecycleOwner = null
    }

    private fun bindHostLifecycle() {
        val owner = findViewTreeLifecycleOwner() ?: return
        if (hostLifecycleOwner === owner) return
        hostLifecycleOwner?.lifecycle?.removeObserver(hostLifecycleObserver)
        hostLifecycleOwner = owner
        owner.lifecycle.addObserver(hostLifecycleObserver)
        if (owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
            scheduleRenderThreadResume()
        } else {
            pauseRenderThread()
        }
    }

    /**
     * A replacement AndroidView can be attached while its Activity is already
     * resumed. Defer the render-surface resume until the child attachment pass
     * is complete; calling it from the parent's onAttachedToWindow can leave a
     * newly inserted backend view without a surface until the next Activity
     * resume.
     */
    private fun scheduleRenderThreadResume() {
        removeCallbacks(deferredResume)
        post(deferredResume)
    }

    private fun resumeRenderThread() {
        if (presentationState.lifecycleResumed) return
        presentationState.onLifecycleResumed()
        endpoint.setLifecyclePaused(false)
        renderSurface.resumeRendering()
        applyRenderCadence()
    }

    private fun pauseRenderThread() {
        removeCallbacks(deferredResume)
        if (!presentationState.lifecycleResumed) return
        presentationState.onLifecyclePaused()
        removeCallbacks(framePacedCallback)
        cancelActiveCameraFrame()
        updateStatusPolling()
        endpoint.setLifecyclePaused(true)
        renderSurface.pauseRendering()
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (!inputEnabled) return false
        return super.dispatchTouchEvent(event)
    }

    override fun onInterceptTouchEvent(event: MotionEvent): Boolean = inputEnabled

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!inputEnabled) return false
        if (!pointerEventView.bind(event)) {
            gestureDetector.onTouchEvent(event)
            return true
        }
        val isDown = event.actionMasked == MotionEvent.ACTION_DOWN
        when (inputStream.admitPointerEvent(isDown)) {
            MicroscopePointerEventAdmission.TERMINAL -> return true
            MicroscopePointerEventAdmission.WAITING_FOR_FRESH_DOWN -> {
                statusPoller.requestImmediatePoll()
                return true
            }
            MicroscopePointerEventAdmission.FRESH_DOWN ->
                gestureInterpreter.reset(event.eventTime)
            MicroscopePointerEventAdmission.ACTIVE -> Unit
        }
        if (isDown) presentationState.onPointerGestureStarted()

        if (BuildConfig.ENABLE_RENDER_PROFILING) {
            RenderPerformanceTelemetry.recordInputEvent()
        }
        val sequence = takeInputBatchSequence() ?: return true
        inputBatchBuilder.begin(
            sequence = sequence,
            eventTimeMillis = event.eventTime,
            expectedInputEpoch = inputStream.inputEpoch,
            profilingTimestampNanos =
                RenderPerformanceTelemetry.captureInputTimestampNanos()
        )
        activeInputCollector = inputBatchBuilder
        try {
            gestureInterpreter.onPointerEvent(pointerEventView, inputBatchBuilder)
            if (event.actionMasked == MotionEvent.ACTION_UP) performClick()
            gestureDetector.onTouchEvent(event)
        } finally {
            activeInputCollector = null
        }

        val gestureFinished =
            event.actionMasked == MotionEvent.ACTION_UP ||
                event.actionMasked == MotionEvent.ACTION_CANCEL
        if (gestureFinished) {
            presentationState.onPointerGestureFinished()
        }
        submitCollectedInput(inputBatchBuilder)
        if (gestureFinished && presentationState.shouldRunActiveCameraCallback()) {
            scheduleActiveCameraFrame()
        }
        return true
    }

    private inline fun collectDetectorCommand(
        eventTimeMillis: Long,
        collect: (AndroidMicroscopeInputBatchBuilder) -> Unit
    ) {
        val active = activeInputCollector
        if (active != null) {
            collect(active)
            return
        }
        if (
            inputStream.terminal ||
            inputStream.requiresFreshDown ||
            inputStream.inputEpoch == 0L
        ) return
        val sequence = takeInputBatchSequence() ?: return
        inputBatchBuilder.begin(
            sequence = sequence,
            eventTimeMillis = eventTimeMillis,
            expectedInputEpoch = inputStream.inputEpoch,
            profilingTimestampNanos =
                RenderPerformanceTelemetry.captureInputTimestampNanos()
        )
        collect(inputBatchBuilder)
        submitCollectedInput(inputBatchBuilder)
    }

    private fun submitCollectedInput(builder: AndroidMicroscopeInputBatchBuilder) {
        if (!builder.finish()) return
        if (BuildConfig.ENABLE_RENDER_PROFILING) {
            RenderPerformanceTelemetry.recordSemanticInputCommands(builder.commandCount)
        }
        val expectedPlacementRevision = if (builder.tapCount == 1) {
            placementGenomeRevision
        } else {
            0L
        }
        endpoint.submitMicroscopeInputBatch(
            expectedInputEpoch = builder.inputEpoch,
            batchSequence = builder.batchSequence,
            input = builder.inputBuffer,
            result = builder.resultReader.buffer,
            expectedPlacementGenomeRevision = expectedPlacementRevision
        )
        val result = builder.resultReader
        result.decode(builder.batchSequence, builder.commandCount)
        when (result.ingressStatus.recoveryAction()) {
            MicroscopeInputRecoveryAction.APPLY -> applyInputBatchResult(builder, result)
            MicroscopeInputRecoveryAction.REQUEST_RESYNC ->
                enterInputResync(builder.eventTimeMillis, requestNative = true)
            MicroscopeInputRecoveryAction.WAIT_FOR_RESYNC ->
                enterInputResync(builder.eventTimeMillis, requestNative = false)
            MicroscopeInputRecoveryAction.CLOSE -> closeInputLocally()
        }
    }

    private fun applyInputBatchResult(
        builder: AndroidMicroscopeInputBatchBuilder,
        result: MicroscopeInputBatchResultReader
    ) {
        if (result.inputEpoch != inputStream.inputEpoch) {
            enterInputResync(builder.eventTimeMillis, requestNative = true)
            return
        }
        if (result.cameraChanged && backendKind == RenderBackendKind.OPEN_GL_ES) {
            RenderPerformanceTelemetry.recordCameraInput(
                backendKind,
                builder.profilingIngressTimestampNanos
            )
        }
        if (result.zoomDisplaySizeChanged) {
            infoReceiver.onZoomViewHeightChanged(result.zoomDisplaySize)
        }
        if (result.zoomEnded) {
            infoReceiver.onZoomViewHeightChanged(null)
        }
        onInputCommandsExecuted(result.cameraChanged)
    }

    private fun takeInputBatchSequence(): Long? {
        val sequence = inputStream.takeBatchSequence()
        if (sequence == null) {
            closeInputLocally()
            return null
        }
        return sequence
    }

    private fun onInputIngressSnapshot(snapshot: MicroscopeInputIngressSnapshot) {
        when (snapshot.state) {
            MicroscopeInputIngressState.ACCEPTING -> {
                if (snapshot.inputEpoch == 0L) {
                    enterInputResync(SystemClock.uptimeMillis(), requestNative = true)
                    return
                }
                if (inputStream.acceptEpoch(snapshot.inputEpoch)) {
                    gestureInterpreter.reset(SystemClock.uptimeMillis())
                    presentationState.onPointerGestureFinished()
                }
            }
            MicroscopeInputIngressState.RESYNCING,
            MicroscopeInputIngressState.INACTIVE_SURFACE -> enterInputResync(
                SystemClock.uptimeMillis(),
                requestNative = false,
                requestImmediatePoll = false
            )
            MicroscopeInputIngressState.CLOSING -> closeInputLocally()
        }
    }

    private fun enterInputResync(
        timestampMillis: Long,
        requestNative: Boolean,
        requestImmediatePoll: Boolean = true
    ) {
        if (!inputStream.beginResync()) return
        activeInputCollector = null
        gestureInterpreter.reset(timestampMillis)
        presentationState.onPointerGestureFinished()
        infoReceiver.onZoomViewHeightChanged(null)
        if (requestNative) endpoint.requestInputResync()
        if (requestImmediatePoll) statusPoller.requestImmediatePoll()
    }

    private fun closeInputLocally() {
        inputStream.close()
        activeInputCollector = null
        gestureInterpreter.reset(SystemClock.uptimeMillis())
        presentationState.onPointerGestureFinished()
        infoReceiver.onZoomViewHeightChanged(null)
    }

    private fun onInputCommandsExecuted(cameraChanged: Boolean) {
        when (presentationState.onInputCommandsExecuted(cameraChanged)) {
            MicroscopeInputPresentationAction.START_ACTIVE_CAMERA ->
                scheduleActiveCameraFrame()
            MicroscopeInputPresentationAction.REQUEST_FRAME ->
                renderSurface.requestFrame()
            MicroscopeInputPresentationAction.NONE -> Unit
        }
    }

    private fun applyRenderCadence() {
        removeCallbacks(framePacedCallback)
        cancelActiveCameraFrame()
        presentationState.resetActiveCameraCadence()

        val plan = presentationState.basePlan
        renderSurface.setCadence(plan.cadence)
        if (presentationState.shouldRequestInitialFrame()) renderSurface.requestFrame()
        scheduleFramePacedCallback()
        updateStatusPolling()
        if (inputStream.requiresFreshDown) statusPoller.requestImmediatePoll()
    }

    private fun scheduleFramePacedCallback() {
        if (!presentationState.shouldRunFramePacedCallback()) return
        val delay = presentationState.basePlan.frameIntervalMillis ?: return
        postDelayed(framePacedCallback, delay)
    }

    private fun updateStatusPolling() {
        statusPoller.update(
            attached = presentationState.attached,
            lifecycleResumed = presentationState.lifecycleResumed,
            statusPollIntervalMillis =
                presentationState.basePlan.statusPollIntervalMillis
        )
    }

    private fun scheduleActiveCameraFrame() {
        if (!presentationState.shouldRunActiveCameraCallback() || activeCameraFramePosted) return
        removeCallbacks(framePacedCallback)
        if (renderSurface.cadence != MicroscopeRenderCadence.CONTINUOUS) {
            // During actual camera motion the producer remains active and the
            // concrete surface implementation performs display pacing.
            renderSurface.setCadence(MicroscopeRenderCadence.CONTINUOUS)
        }
        activeCameraFramePosted = true
        choreographer.postFrameCallback(activeCameraFrameCallback)
    }

    private fun finishActiveCameraRendering() {
        if (
            !presentationState.lifecycleResumed ||
            !presentationState.attached ||
            !presentationState.presentationVisible ||
            presentationState.temperature != MicroscopeTemperature.INCUBATE
        ) {
            return
        }
        renderSurface.setCadence(MicroscopeRenderCadence.ON_DEMAND)
        // Preserve the last camera state across the cadence hand-off.
        renderSurface.requestFrame()
        scheduleFramePacedCallback()
    }

    private fun cancelActiveCameraFrame() {
        if (!activeCameraFramePosted) return
        choreographer.removeFrameCallback(activeCameraFrameCallback)
        activeCameraFramePosted = false
    }

    internal fun activeCameraCadenceTelemetry(): ActiveCameraCadenceTelemetry =
        presentationState.activeCameraTelemetry()

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private data class MicroscopeControls(
        val tool: MicroscopeTool,
        val temperature: MicroscopeTemperature,
        val incubateTargetUps: Double,
        val performanceHudEnabled: Boolean
    )

    private class MotionEventMicroscopePointerEventView : MicroscopePointerEventView {
        private lateinit var event: MotionEvent

        override lateinit var action: MicroscopePointerAction
            private set

        override val actionPointerId: Int?
            get() = when (action) {
                MicroscopePointerAction.DOWN,
                MicroscopePointerAction.POINTER_DOWN,
                MicroscopePointerAction.UP,
                MicroscopePointerAction.POINTER_UP ->
                    event.getPointerId(event.actionIndex)
                MicroscopePointerAction.MOVE,
                MicroscopePointerAction.CANCEL -> null
            }

        override val pointerCount: Int
            get() = event.pointerCount

        override val timestampMillis: Long
            get() = event.eventTime

        fun bind(value: MotionEvent): Boolean {
            action = when (value.actionMasked) {
                MotionEvent.ACTION_DOWN -> MicroscopePointerAction.DOWN
                MotionEvent.ACTION_POINTER_DOWN -> MicroscopePointerAction.POINTER_DOWN
                MotionEvent.ACTION_MOVE -> MicroscopePointerAction.MOVE
                MotionEvent.ACTION_UP -> MicroscopePointerAction.UP
                MotionEvent.ACTION_POINTER_UP -> MicroscopePointerAction.POINTER_UP
                MotionEvent.ACTION_CANCEL -> MicroscopePointerAction.CANCEL
                else -> return false
            }
            event = value
            return true
        }

        override fun pointerId(index: Int): Int = event.getPointerId(index)

        override fun pointerX(index: Int): Float = event.getX(index)

        override fun pointerY(index: Int): Float = event.getY(index)
    }

}
