package com.cytarch.cellularity.platform.render.microscope

import android.content.Context
import com.cytarch.cellularity.platform.render.RenderSurfaceLifecycle
import com.cytarch.cellularity.platform.render.vulkan.VulkanClearFrameView

/** Production microscope host backed by the native-owned Vulkan worker. */
internal class VulkanMicroscopeSurfaceHost(
    context: Context,
    private val renderEndpoint: MicroscopeRenderEndpoint,
    platformCallbacks: MicroscopePlatformSurfaceCallbacks,
    onRuntimeFailure: (String) -> Unit
) : MicroscopeRenderSurfaceHost {
    private val surface = VulkanClearFrameView(
        context = context,
        workerFactory = { receiver ->
            renderEndpoint.createVulkanRenderWorker(
                context,
                initialContinuousRendering = true,
                statusReceiver = receiver
            )
        }
    ).apply {
        surfaceAvailableReceiver = platformCallbacks::onSurfaceAvailable
        surfaceDestroyingReceiver = {
            renderEndpoint.onVulkanSurfaceUnavailable()
            platformCallbacks.onSurfaceDestroying()
        }
        nativeSurfaceReadyReceiver = renderEndpoint::onVulkanSurfaceReady
        nativeSurfaceUnavailableReceiver =
            renderEndpoint::onVulkanSurfaceUnavailable
        surfaceContentCurrentReceiver =
            platformCallbacks::onSurfaceContentCurrent
        statusReceiver = { status ->
            if (status.terminalFailure) {
                onRuntimeFailure(
                    status.error.ifBlank {
                        "Vulkan microscope renderer entered a failed state"
                    }
                )
            }
        }
    }

    override val view = surface

    override var cadence: MicroscopeRenderCadence = MicroscopeRenderCadence.CONTINUOUS
        private set

    override fun setCadence(cadence: MicroscopeRenderCadence) {
        this.cadence = cadence
        surface.setContinuousRendering(cadence == MicroscopeRenderCadence.CONTINUOUS)
    }

    override fun requestFrame() = surface.requestFrame()

    override fun resumeRendering() = surface.onHostResume()

    override fun pauseRendering() {
        renderEndpoint.onVulkanSurfaceUnavailable()
        surface.onHostPause()
    }

    override fun requestMaximumDisplayRefreshRate() =
        surface.requestMaximumDisplayRefreshRate()

    override fun release() {
        renderEndpoint.onVulkanSurfaceUnavailable()
        surface.close()
    }
}

internal object VulkanMicroscopeSurfaceHostFactory : MicroscopeRenderSurfaceHostFactory {
    override fun create(
        context: Context,
        renderEndpoint: MicroscopeRenderEndpoint,
        lifecycle: RenderSurfaceLifecycle,
        platformCallbacks: MicroscopePlatformSurfaceCallbacks,
        onRuntimeFailure: (String) -> Unit
    ): MicroscopeRenderSurfaceHost = VulkanMicroscopeSurfaceHost(
        context = context,
        renderEndpoint = renderEndpoint,
        platformCallbacks = platformCallbacks,
        onRuntimeFailure = onRuntimeFailure
    )
}
