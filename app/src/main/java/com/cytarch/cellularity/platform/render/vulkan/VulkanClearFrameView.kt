package com.cytarch.cellularity.platform.render.vulkan

import android.content.Context
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.RenderPerformanceTelemetry
import java.util.concurrent.atomic.AtomicBoolean

internal typealias NativeVulkanWorkerFactory = (
    statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
) -> NativeVulkanRenderWorkerBridge

/** Surface/lifecycle host; the render loop and Vulkan state live in native code. */
internal class VulkanClearFrameView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    workerFactory: NativeVulkanWorkerFactory? = null,
    opaqueSurface: Boolean = false,
    initialContinuousRendering: Boolean = true
) : SurfaceView(context, attrs), SurfaceHolder.Callback2, AutoCloseable {
    var performanceHudEnabled: Boolean = false
        set(value) {
            if (field == value) return
            field = value
            renderWorker.setDemoPerformanceHudEnabled(value)
            if (resumed && surfaceAvailable) requestNativeFrame()
        }

    var statusReceiver: (VulkanClearFrameStatus) -> Unit = {}
        set(value) {
            field = value
            value(latestStatus)
        }

    var surfaceAvailableReceiver: () -> Unit = {}

    var surfaceDestroyingReceiver: () -> Unit = {}

    var nativeSurfaceReadyReceiver: () -> Boolean = { true }

    var nativeSurfaceUnavailableReceiver: () -> Unit = {}

    var surfaceSizeReceiver: (Int, Int) -> Unit = { _, _ -> }

    var surfaceContentCurrentReceiver: (Boolean) -> Unit = {}
        set(value) {
            field = value
            value(redrawCoordinator.contentCurrent)
        }

    private val closed = AtomicBoolean(false)
    private val choreographer = Choreographer.getInstance()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val redrawCoordinator = VulkanSurfaceRedrawCoordinator()
    private val renderWorker = (workerFactory ?: { receiver ->
        NativeVulkanRenderWorkerBridge.createDemo(
            context,
            initialContinuousRendering,
            receiver
        )
    }).invoke(::onWorkerStatus)
    private val frameCallback = Choreographer.FrameCallback {
        frameCallbackPosted = false
        if (resumed && surfaceAvailable && !closed.get()) {
            requestNativeFrame()
            if (continuousRendering) postFrameCallback()
        }
    }
    private val redrawStatusCheck = Choreographer.FrameCallback {
        redrawStatusCheckPosted = false
        if (
            closed.get() || !resumed || !surfaceAvailable ||
            !redrawCoordinator.hasPendingTarget
        ) {
            return@FrameCallback
        }
        val status = runCatching(renderWorker::readStatus).getOrNull()
        val transition = if (status == null) {
            redrawCoordinator.terminate(
                VulkanSurfaceRedrawCoordinator.Completion.FAILED
            )
        } else {
            redrawCoordinator.observe(status, System.nanoTime())
        }
        applyRedrawTransition(transition)
    }
    private val readinessCheck = object : Runnable {
        override fun run() {
            readinessCheckPosted = false
            if (
                closed.get() || !resumed || !surfaceAvailable ||
                !windowSubmitted || readySurfaceGeneration == surfaceGeneration
            ) {
                return
            }
            val status = runCatching(renderWorker::readStatus).getOrNull()
            if (
                status?.phase == NativeVulkanWorkerStatus.Phase.READY &&
                status.appliedSurfaceGeneration == surfaceGeneration
            ) {
                if (nativeSurfaceReadyReceiver()) {
                    readySurfaceGeneration = surfaceGeneration
                } else {
                    postReadinessCheck()
                }
            } else if (
                status?.phase != NativeVulkanWorkerStatus.Phase.TERMINAL_FAILURE &&
                status?.phase != NativeVulkanWorkerStatus.Phase.STOP_REQUESTED &&
                status?.phase != NativeVulkanWorkerStatus.Phase.STOPPED
            ) {
                postReadinessCheck()
            }
        }
    }

    private var resumed = false
    private var surfaceAvailable = false
    private var windowSubmitted = false
    private var frameCallbackPosted = false
    private var readinessCheckPosted = false
    private var redrawStatusCheckPosted = false
    private var surfaceGeneration = 0L
    private var readySurfaceGeneration = 0L
    private var surfaceWidth = 0
    private var surfaceHeight = 0
    private var publishedContentCurrent = false
    private var continuousRendering = initialContinuousRendering
    private var latestStatus = VulkanClearFrameStatus()

    init {
        holder.setFormat(
            if (opaqueSurface) PixelFormat.OPAQUE else PixelFormat.RGBA_8888
        )
        holder.addCallback(this)
    }

    fun onHostResume() {
        if (closed.get() || resumed) return
        resumed = true
        renderWorker.resume()
        if (holder.surface.isValid) {
            surfaceAvailable = true
            if (surfaceGeneration == 0L) surfaceGeneration = 1L
            surfaceWidth = width.coerceAtLeast(0)
            surfaceHeight = height.coerceAtLeast(0)
            val requestRedraw = beginRedrawTarget(surfaceWidth, surfaceHeight)
            attachCurrentSurface(surfaceWidth, surfaceHeight)
            if (requestRedraw) requestNativeFrame()
            surfaceAvailableReceiver()
        }
        if (continuousRendering) postFrameCallback()
    }

    fun onHostPause() {
        if (closed.get() || !resumed) return
        resumed = false
        removeFrameCallback()
        removeReadinessCheck()
        removeRedrawStatusCheck()
        applyRedrawTransition(
            redrawCoordinator.terminate(
                VulkanSurfaceRedrawCoordinator.Completion.PAUSED
            )
        )
        readySurfaceGeneration = 0L
        windowSubmitted = false
        renderWorker.pause()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        removeRedrawStatusCheck()
        applyRedrawTransition(
            redrawCoordinator.terminate(
                VulkanSurfaceRedrawCoordinator.Completion.SURFACE_GONE
            )
        )
        surfaceGeneration = incrementGeneration(surfaceGeneration)
        surfaceAvailable = true
        windowSubmitted = false
        surfaceWidth = width.coerceAtLeast(0)
        surfaceHeight = height.coerceAtLeast(0)
        requestMaximumDisplayRefreshRate()
        if (resumed) {
            val requestRedraw = beginRedrawTarget(surfaceWidth, surfaceHeight)
            attachCurrentSurface(surfaceWidth, surfaceHeight)
            if (requestRedraw) requestNativeFrame()
            if (continuousRendering) postFrameCallback()
        }
        surfaceAvailableReceiver()
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int
    ) {
        surfaceAvailable = true
        surfaceWidth = width.coerceAtLeast(0)
        surfaceHeight = height.coerceAtLeast(0)
        surfaceSizeReceiver(width, height)
        if (resumed) {
            val requestRedraw = beginRedrawTarget(surfaceWidth, surfaceHeight)
            if (!windowSubmitted) {
                attachCurrentSurface(surfaceWidth, surfaceHeight)
            } else {
                renderWorker.resize(surfaceWidth, surfaceHeight, surfaceGeneration)
            }
            if (requestRedraw) requestNativeFrame()
        }
        if (resumed && continuousRendering) postFrameCallback()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceAvailable = false
        windowSubmitted = false
        readySurfaceGeneration = 0L
        removeFrameCallback()
        removeReadinessCheck()
        removeRedrawStatusCheck()
        applyRedrawTransition(
            redrawCoordinator.terminate(
                VulkanSurfaceRedrawCoordinator.Completion.SURFACE_GONE
            )
        )
        surfaceDestroyingReceiver()
        renderWorker.detachSurface(surfaceGeneration)
    }

    override fun surfaceRedrawNeeded(holder: SurfaceHolder) {
        ensureRedrawTransaction()
    }

    override fun surfaceRedrawNeededAsync(
        holder: SurfaceHolder,
        drawingFinished: Runnable
    ) {
        ensureRedrawTransaction(drawingFinished)
    }

    fun setContinuousRendering(enabled: Boolean) {
        if (continuousRendering == enabled) return
        if (enabled) {
            renderWorker.setContinuousRendering(true)
            continuousRendering = true
            postFrameCallback()
        } else {
            continuousRendering = false
            removeFrameCallback()
            renderWorker.setContinuousRendering(false)
        }
    }

    fun requestFrame() {
        if (!closed.get()) requestNativeFrame()
    }

    /** Requests a frame and reports when a newer frame reaches this Surface. */
    fun requestFrameWithPresentationTracking() {
        ensureRedrawTransaction(forceFrameRequest = true)
    }

    fun requestDiagnosticsReport(): String? =
        renderWorker.requestDiagnosticsReport()

    fun requestMaximumDisplayRefreshRate() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || !holder.surface.isValid) return
        val maxRefreshRate = display?.supportedModes
            ?.maxOfOrNull { it.refreshRate }
            ?: display?.refreshRate
            ?: return
        holder.surface.setFrameRate(
            maxRefreshRate,
            Surface.FRAME_RATE_COMPATIBILITY_DEFAULT
        )
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        resumed = false
        surfaceAvailable = false
        windowSubmitted = false
        removeFrameCallback()
        removeReadinessCheck()
        removeRedrawStatusCheck()
        applyRedrawTransition(
            redrawCoordinator.terminate(
                VulkanSurfaceRedrawCoordinator.Completion.CLOSED
            )
        )
        holder.removeCallback(this)
        renderWorker.closeAsync()
    }

    private fun attachCurrentSurface(
        width: Int = this.width,
        height: Int = this.height
    ) {
        if (!resumed || !surfaceAvailable || windowSubmitted || closed.get()) return
        val surface = holder.surface
        if (!surface.isValid || surfaceGeneration == 0L) return
        windowSubmitted = renderWorker.attachSurface(
            surface,
            width.coerceAtLeast(0),
            height.coerceAtLeast(0),
            surfaceGeneration
        )
        if (windowSubmitted) postReadinessCheck()
    }

    private fun onWorkerStatus(status: NativeVulkanWorkerStatus, actions: Int) {
        if (closed.get()) return
        val legacy = status.toLegacyStatus()
        latestStatus = legacy
        statusReceiver(legacy)
        val needsReplacement = actions and (
            NativeVulkanWorkerActions.SURFACE_REATTACH_REQUIRED or
                NativeVulkanWorkerActions.RECOVERABLE_FAILURE
            ) != 0
        if (needsReplacement && resumed && surfaceAvailable) {
            if (readySurfaceGeneration != 0L) {
                readySurfaceGeneration = 0L
                nativeSurfaceUnavailableReceiver()
            }
            windowSubmitted = false
            beginRedrawTarget(
                surfaceWidth,
                surfaceHeight,
                status
            )
            attachCurrentSurface(surfaceWidth, surfaceHeight)
            // The failed frame was already consumed. Recovery always needs a
            // new request even when the pending redraw target itself did not
            // change.
            requestNativeFrame()
        }
        if (actions and NativeVulkanWorkerActions.TERMINAL_FAILURE != 0) {
            removeFrameCallback()
            removeRedrawStatusCheck()
            applyRedrawTransition(
                redrawCoordinator.terminate(
                    VulkanSurfaceRedrawCoordinator.Completion.FAILED
                )
            )
        }
    }

    private fun postFrameCallback() {
        if (
            frameCallbackPosted || !continuousRendering || !resumed ||
            !surfaceAvailable || closed.get()
        ) {
            return
        }
        frameCallbackPosted = true
        choreographer.postFrameCallback(frameCallback)
    }

    private fun removeFrameCallback() {
        if (!frameCallbackPosted) return
        choreographer.removeFrameCallback(frameCallback)
        frameCallbackPosted = false
    }

    private fun postReadinessCheck() {
        if (readinessCheckPosted || closed.get()) return
        readinessCheckPosted = true
        mainHandler.postDelayed(readinessCheck, READINESS_RETRY_MILLIS)
    }

    private fun removeReadinessCheck() {
        if (!readinessCheckPosted) return
        mainHandler.removeCallbacks(readinessCheck)
        readinessCheckPosted = false
    }

    private fun ensureRedrawTransaction(
        callback: Runnable? = null,
        forceFrameRequest: Boolean = false
    ) {
        if (closed.get() || !resumed || !surfaceAvailable) {
            callback?.run()
            return
        }
        val width = surfaceWidth.takeIf { it > 0 } ?: this.width
        val height = surfaceHeight.takeIf { it > 0 } ?: this.height
        if (surfaceGeneration <= 0L || width <= 0 || height <= 0) {
            callback?.run()
            return
        }
        val baseline = runCatching(renderWorker::readStatus).getOrNull()
        if (baseline == null) {
            callback?.run()
            applyRedrawTransition(
                redrawCoordinator.terminate(
                    VulkanSurfaceRedrawCoordinator.Completion.FAILED
                )
            )
            return
        }
        val transition = if (callback == null) {
            redrawCoordinator.beginTarget(
                surfaceGeneration,
                width,
                height,
                baseline,
                System.nanoTime()
            )
        } else {
            redrawCoordinator.registerCallback(
                surfaceGeneration,
                width,
                height,
                baseline,
                System.nanoTime(),
                callback
            )
        }
        applyRedrawTransition(transition)
        if (transition.requestFrame || forceFrameRequest) requestNativeFrame()
    }

    private fun beginRedrawTarget(
        width: Int,
        height: Int,
        knownBaseline: NativeVulkanWorkerStatus? = null
    ): Boolean {
        if (
            closed.get() || !resumed || !surfaceAvailable ||
            surfaceGeneration <= 0L || width <= 0 || height <= 0
        ) {
            applyRedrawTransition(
                redrawCoordinator.terminate(
                    VulkanSurfaceRedrawCoordinator.Completion.FAILED
                )
            )
            return false
        }
        val baseline = knownBaseline
            ?: runCatching(renderWorker::readStatus).getOrNull()
            ?: run {
                applyRedrawTransition(
                    redrawCoordinator.terminate(
                        VulkanSurfaceRedrawCoordinator.Completion.FAILED
                    )
                )
                return false
            }
        val transition = redrawCoordinator.beginTarget(
            surfaceGeneration,
            width,
            height,
            baseline,
            System.nanoTime()
        )
        applyRedrawTransition(transition)
        return transition.requestFrame
    }

    private fun applyRedrawTransition(
        transition: VulkanSurfaceRedrawCoordinator.Transition
    ) {
        publishContentCurrent()
        when (transition.completion) {
            VulkanSurfaceRedrawCoordinator.Completion.TIMED_OUT ->
                VulkanRuntimeDiagnostics.recordSurfaceRedrawTimeout()
            VulkanSurfaceRedrawCoordinator.Completion.COUNTER_EXHAUSTED ->
                VulkanRuntimeDiagnostics.recordSurfaceRedrawCounterExhausted()
            else -> Unit
        }
        transition.callbacks.forEach { callback ->
            runCatching(callback::run)
        }
        if (transition.keepPolling) postRedrawStatusCheck()
    }

    private fun publishContentCurrent() {
        val current = redrawCoordinator.contentCurrent
        if (publishedContentCurrent == current) return
        publishedContentCurrent = current
        surfaceContentCurrentReceiver(current)
    }

    private fun postRedrawStatusCheck() {
        if (
            redrawStatusCheckPosted || closed.get() || !resumed ||
            !surfaceAvailable || !redrawCoordinator.hasPendingTarget
        ) {
            return
        }
        redrawStatusCheckPosted = true
        choreographer.postFrameCallback(redrawStatusCheck)
    }

    private fun removeRedrawStatusCheck() {
        if (!redrawStatusCheckPosted) return
        choreographer.removeFrameCallback(redrawStatusCheck)
        redrawStatusCheckPosted = false
    }

    private fun requestNativeFrame() {
        val accepted = renderWorker.requestFrame()
        RenderPerformanceTelemetry.recordFrameRequest(
            RenderBackendKind.VULKAN,
            coalesced = !accepted
        )
    }

    private companion object {
        const val READINESS_RETRY_MILLIS = 8L

        fun incrementGeneration(current: Long): Long {
            check(current != Long.MAX_VALUE) {
                "Vulkan Surface generation is exhausted"
            }
            return current + 1L
        }
    }
}
