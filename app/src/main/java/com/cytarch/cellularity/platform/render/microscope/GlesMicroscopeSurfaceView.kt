package com.cytarch.cellularity.platform.render.microscope

import android.annotation.SuppressLint
import android.content.Context
import android.opengl.GLSurfaceView
import android.os.Build
import android.view.Surface
import android.view.SurfaceHolder
import com.cytarch.cellularity.platform.render.AndroidGlRenderer
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.RenderPerformanceTelemetry
import com.cytarch.cellularity.platform.render.RenderSurfaceLifecycle
import java.util.concurrent.atomic.AtomicBoolean

/**
 * GLES-only surface implementation. Input, cadence policy and lifecycle
 * decisions live outside it.
 */
@SuppressLint("ViewConstructor")
internal class GlesMicroscopeSurfaceView(
    context: Context,
    lifecycle: RenderSurfaceLifecycle,
    private val platformCallbacks: MicroscopePlatformSurfaceCallbacks
) : GLSurfaceView(context), MicroscopeRenderSurfaceHost {
    private val frameRequestOutstanding = AtomicBoolean(false)
    private val androidRenderer = AndroidGlRenderer(
        lifecycle = lifecycle,
        swapInterval = 1,
        performanceBackend = RenderBackendKind.OPEN_GL_ES,
        onFrameStarted = { frameRequestOutstanding.set(false) }
    )

    override val view = this

    override var cadence: MicroscopeRenderCadence = MicroscopeRenderCadence.CONTINUOUS
        private set

    private var hostResumed = false
    private var rendererResumed = false

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 0, 16, 0)
        setRenderer(androidRenderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    override fun setCadence(cadence: MicroscopeRenderCadence) {
        this.cadence = cadence
        renderMode = when (cadence) {
            MicroscopeRenderCadence.CONTINUOUS -> RENDERMODE_CONTINUOUSLY
            MicroscopeRenderCadence.ON_DEMAND -> RENDERMODE_WHEN_DIRTY
        }
    }

    override fun requestFrame() {
        val coalesced = !frameRequestOutstanding.compareAndSet(false, true)
        RenderPerformanceTelemetry.recordFrameRequest(
            RenderBackendKind.OPEN_GL_ES,
            coalesced = coalesced
        )
        requestRender()
    }

    override fun resumeRendering() {
        hostResumed = true
        resumeRendererIfAttached()
    }

    override fun pauseRendering() {
        hostResumed = false
        pauseRendererIfNeeded()
    }

    override fun requestMaximumDisplayRefreshRate() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || !holder.surface.isValid) return
        val maxRefreshRate = display?.supportedModes
            ?.maxOfOrNull { it.refreshRate }
            ?: display?.refreshRate
            ?: return
        holder.surface.setFrameRate(maxRefreshRate, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
    }

    override fun release() = pauseRendering()

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        resumeRendererIfAttached()
    }

    override fun onDetachedFromWindow() {
        pauseRendererIfNeeded()
        super.onDetachedFromWindow()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        super.surfaceCreated(holder)
        requestMaximumDisplayRefreshRate()
        platformCallbacks.onSurfaceAvailable()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        platformCallbacks.onSurfaceDestroying()
        androidRenderer.onSurfaceDestroyed()
        super.surfaceDestroyed(holder)
    }

    private fun resumeRendererIfAttached() {
        if (!hostResumed || !isAttachedToWindow || rendererResumed) return
        super.onResume()
        rendererResumed = true
    }

    private fun pauseRendererIfNeeded() {
        if (!rendererResumed) return
        super.onPause()
        rendererResumed = false
    }
}

internal object GlesMicroscopeSurfaceHostFactory : MicroscopeRenderSurfaceHostFactory {
    override fun create(
        context: Context,
        renderEndpoint: MicroscopeRenderEndpoint,
        lifecycle: RenderSurfaceLifecycle,
        platformCallbacks: MicroscopePlatformSurfaceCallbacks,
        onRuntimeFailure: (String) -> Unit
    ): MicroscopeRenderSurfaceHost = GlesMicroscopeSurfaceView(
        context = context,
        lifecycle = lifecycle,
        platformCallbacks = platformCallbacks
    )
}
