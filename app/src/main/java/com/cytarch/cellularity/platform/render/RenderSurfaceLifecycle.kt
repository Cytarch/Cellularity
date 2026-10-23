package com.cytarch.cellularity.platform.render

/**
 * Application-facing lifecycle of a render surface.
 *
 * Domain commands and persistence deliberately live outside this contract so
 * recreating an Android surface cannot be confused with replacing a world.
 */
interface RenderSurfaceLifecycle {
    fun onSurfaceCreated()
    fun onSurfaceChanged(width: Int, height: Int)
    fun onSurfaceDestroyed()
    fun onRenderFrame(): Long
}
