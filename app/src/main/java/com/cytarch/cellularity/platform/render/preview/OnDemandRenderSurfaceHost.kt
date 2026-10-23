package com.cytarch.cellularity.platform.render.preview

import android.content.Context
import android.graphics.PixelFormat
import android.opengl.GLSurfaceView
import android.view.SurfaceHolder
import android.view.View
import com.cytarch.cellularity.platform.render.AndroidGlRenderer
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.RenderSurfaceLifecycle
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanRenderWorkerBridge
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanWorkerStatus
import com.cytarch.cellularity.platform.render.vulkan.VulkanClearFrameView
import java.util.concurrent.atomic.AtomicBoolean

internal interface OnDemandRenderLifecycle : RenderSurfaceLifecycle {
    fun createVulkanRenderWorker(
        context: Context,
        statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
    ): NativeVulkanRenderWorkerBridge

    fun onVulkanPlatformSurfaceAvailable() = Unit

    fun onVulkanPlatformSurfaceChanged(width: Int, height: Int) = Unit

    fun onVulkanPlatformSurfaceContentCurrent(current: Boolean) = Unit
}

internal interface OnDemandRenderSurfaceHost {
    val view: View
    val supportsOffscreenCapture: Boolean

    fun requestFrame()
    fun requestFrameWithPresentationTracking()
    fun resumeRendering()
    fun pauseRendering()
    fun release()
}

internal object OnDemandRenderSurfaceHostFactory {
    fun create(
        context: Context,
        backendKind: RenderBackendKind,
        lifecycle: OnDemandRenderLifecycle,
        opaqueSurface: Boolean = false,
        onSurfaceInvalidated: () -> Unit,
        onRuntimeFailure: (String) -> Unit = {}
    ): OnDemandRenderSurfaceHost = when (backendKind) {
        RenderBackendKind.OPEN_GL_ES -> GlesOnDemandRenderSurfaceHost(
            context,
            lifecycle,
            opaqueSurface,
            onSurfaceInvalidated
        )
        RenderBackendKind.VULKAN -> VulkanOnDemandRenderSurfaceHost(
            context,
            lifecycle,
            opaqueSurface,
            onSurfaceInvalidated,
            onRuntimeFailure
        )
    }
}

private class GlesOnDemandRenderSurfaceHost(
    context: Context,
    private val lifecycle: OnDemandRenderLifecycle,
    opaqueSurface: Boolean,
    private val onSurfaceInvalidated: () -> Unit
) : OnDemandRenderSurfaceHost {
    private val surfaceInitialized = AtomicBoolean(false)
    private val androidRenderer = AndroidGlRenderer(
        object : RenderSurfaceLifecycle {
            override fun onSurfaceCreated() {
                lifecycle.onSurfaceCreated()
                surfaceInitialized.set(true)
            }

            override fun onSurfaceChanged(width: Int, height: Int) {
                lifecycle.onSurfaceChanged(width, height)
            }

            override fun onSurfaceDestroyed() {
                notifySurfaceDestroyed()
            }

            override fun onRenderFrame(): Long = lifecycle.onRenderFrame()
        }
    )
    private val surface = object : GLSurfaceView(context) {
        override fun surfaceDestroyed(holder: SurfaceHolder) {
            onSurfaceInvalidated()
            super.surfaceDestroyed(holder)
            notifySurfaceDestroyed()
        }
    }.apply {
        setEGLContextClientVersion(3)
        if (opaqueSurface) {
            setEGLConfigChooser(8, 8, 8, 8, 16, 0)
            holder.setFormat(PixelFormat.OPAQUE)
        } else {
            setEGLConfigChooser(8, 8, 8, 0, 16, 0)
        }
        preserveEGLContextOnPause = false
        setRenderer(androidRenderer)
        renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
    }
    private var resumed = true
    private var released = false

    override val view: View = surface
    override val supportsOffscreenCapture: Boolean = true

    override fun requestFrame() {
        if (!released) surface.requestRender()
    }

    override fun requestFrameWithPresentationTracking() = requestFrame()

    override fun resumeRendering() {
        if (released) return
        if (!resumed) {
            resumed = true
            surface.onResume()
        }
        surface.requestRender()
    }

    override fun pauseRendering() {
        if (released || !resumed) return
        onSurfaceInvalidated()
        resumed = false
        surface.onPause()
        notifySurfaceDestroyed()
    }

    override fun release() {
        if (released) return
        pauseRendering()
        released = true
    }

    private fun notifySurfaceDestroyed() {
        if (surfaceInitialized.compareAndSet(true, false)) {
            lifecycle.onSurfaceDestroyed()
        }
    }
}

private class VulkanOnDemandRenderSurfaceHost(
    context: Context,
    private val lifecycle: OnDemandRenderLifecycle,
    opaqueSurface: Boolean,
    onSurfaceInvalidated: () -> Unit,
    onRuntimeFailure: (String) -> Unit
) : OnDemandRenderSurfaceHost {
    private val surface = VulkanClearFrameView(
        context = context,
        workerFactory = { receiver ->
            lifecycle.createVulkanRenderWorker(context, receiver)
        },
        opaqueSurface = opaqueSurface,
        initialContinuousRendering = false
    ).apply {
        surfaceAvailableReceiver = lifecycle::onVulkanPlatformSurfaceAvailable
        surfaceSizeReceiver = lifecycle::onVulkanPlatformSurfaceChanged
        surfaceContentCurrentReceiver =
            lifecycle::onVulkanPlatformSurfaceContentCurrent
        surfaceDestroyingReceiver = onSurfaceInvalidated
        statusReceiver = { status ->
            if (status.terminalFailure) {
                onRuntimeFailure(
                    status.error.ifBlank {
                        "Vulkan preview renderer entered a failed state"
                    }
                )
            }
        }
    }

    override val view: View = surface
    override val supportsOffscreenCapture: Boolean = true

    override fun requestFrame() = surface.requestFrame()

    override fun requestFrameWithPresentationTracking() =
        surface.requestFrameWithPresentationTracking()

    override fun resumeRendering() {
        surface.onHostResume()
        surface.requestFrame()
    }

    override fun pauseRendering() {
        surface.onHostPause()
    }

    override fun release() = surface.close()
}
