package com.cytarch.cellularity.platform.render.microscope

import android.content.Context
import android.view.View
import com.cytarch.cellularity.platform.render.RenderSurfaceLifecycle

/** Backend-specific Android surface operations required by the common host. */
internal interface MicroscopeRenderSurfaceHost {
    val view: View
    val cadence: MicroscopeRenderCadence

    fun setCadence(cadence: MicroscopeRenderCadence)

    fun requestFrame()

    fun resumeRendering()

    fun pauseRendering()

    fun requestMaximumDisplayRefreshRate()

    fun release()
}

/** UI-thread surface events not represented by the render-thread lifecycle. */
internal interface MicroscopePlatformSurfaceCallbacks {
    fun onSurfaceAvailable()

    fun onSurfaceDestroying()

    fun onSurfaceContentCurrent(current: Boolean) = Unit
}

internal fun interface MicroscopeRenderSurfaceHostFactory {
    fun create(
        context: Context,
        renderEndpoint: MicroscopeRenderEndpoint,
        lifecycle: RenderSurfaceLifecycle,
        platformCallbacks: MicroscopePlatformSurfaceCallbacks,
        onRuntimeFailure: (String) -> Unit
    ): MicroscopeRenderSurfaceHost
}
