package com.cytarch.cellularity.platform.render

import android.opengl.EGL14
import android.opengl.GLSurfaceView
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/** Adapts Android's GL callback types to the application render contract. */
class AndroidGlRenderer(
    private val lifecycle: RenderSurfaceLifecycle,
    private val swapInterval: Int? = null,
    private val performanceBackend: RenderBackendKind? = null,
    private val onFrameStarted: () -> Unit = {}
) : GLSurfaceView.Renderer {
    override fun onSurfaceCreated(unused: GL10?, config: EGLConfig?) {
        swapInterval?.let {
            EGL14.eglSwapInterval(EGL14.eglGetCurrentDisplay(), it)
        }
        lifecycle.onSurfaceCreated()
    }

    override fun onSurfaceChanged(unused: GL10?, width: Int, height: Int) {
        lifecycle.onSurfaceChanged(width, height)
    }

    override fun onDrawFrame(unused: GL10?) {
        onFrameStarted()
        val profile = performanceBackend?.let(RenderPerformanceTelemetry::beginFrame)
        try {
            val result = lifecycle.onRenderFrame()
            check(RenderFrameResultCodec.isValid(result)) {
                "Native renderer returned an invalid frame result"
            }
            val profileOutcome = when (RenderFrameResultCodec.outcomeCode(result)) {
                RenderFrameResultCodec.OUTCOME_PRESENTED ->
                    RenderFrameProfileOutcome.PRESENTED
                RenderFrameResultCodec.OUTCOME_DEFERRED,
                RenderFrameResultCodec.OUTCOME_SWAPCHAIN_RECREATED,
                RenderFrameResultCodec.OUTCOME_SKIPPED_NOT_READY ->
                    RenderFrameProfileOutcome.DEFERRED
                else -> RenderFrameProfileOutcome.FAILED
            }
            RenderPerformanceTelemetry.finishFrame(
                profile,
                profileOutcome
            )
        } catch (failure: Throwable) {
            RenderPerformanceTelemetry.finishFrame(
                profile,
                RenderFrameProfileOutcome.FAILED
            )
            throw failure
        }
    }

    /** Called by the owning [GLSurfaceView] because Renderer has no destroy callback. */
    fun onSurfaceDestroyed() {
        lifecycle.onSurfaceDestroyed()
    }
}
