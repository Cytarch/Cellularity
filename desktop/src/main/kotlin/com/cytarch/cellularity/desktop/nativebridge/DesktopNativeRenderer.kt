/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

/**
 * The JNI surface of the desktop renderer.
 *
 * The symbols the native library exports are derived from this class name, so renaming
 * either the package or the class silently breaks the binding: the JVM looks up
 * `Java_com_cytarch_cellularity_desktop_nativebridge_DesktopNativeRenderer_<method>` and
 * a mismatch shows up only as an `UnsatisfiedLinkError` on the first call. Neither the
 * package nor the class name contains an underscore, which matters because an underscore
 * would have to be escaped as `_1` in the symbol and that is easy to get wrong.
 *
 * Every method here is `external` and the declarations are deliberately `object` members
 * rather than companion members, so the generated symbol is the method on this class and
 * the native side receives the singleton as the second argument.
 *
 * ## The one rule that is not enforced by the compiler
 *
 * The underlying Vulkan session records the thread that created it and refuses work from
 * any other. [createSession], [renderFrame] and [stopSession] must therefore all be called
 * from the same thread for the lifetime of a session. The simulation itself runs on the
 * thread the native side starts; only drawing and input come back through here, which is
 * what makes a single calling thread sufficient rather than merely convenient.
 */
internal object DesktopNativeRenderer {

    /**
     * Loads the library, or records why it could not be loaded.
     *
     * The failure is captured rather than thrown so a missing library degrades to a
     * message in the interface instead of a crash on startup. A user whose build is
     * incomplete should see what is missing, not a stack trace.
     */
    private val loadFailure: Throwable? = try {
        System.loadLibrary("cellularity_desktop")
        null
    } catch (failure: UnsatisfiedLinkError) {
        failure
    } catch (failure: SecurityException) {
        failure
    }

    val isAvailable: Boolean
        get() = loadFailure == null

    val failureDescription: String?
        get() = loadFailure?.message

    fun requireLoaded() {
        loadFailure?.let { failure ->
            throw IllegalStateException(
                "The desktop renderer library is not available: ${failure.message}",
                failure
            )
        }
    }

    /** Creates the child window the renderer presents into. Returns 0 on failure. */
    external fun createWindow(
        ownerWindowHandle: Long,
        x: Int,
        y: Int,
        width: Int,
        height: Int
    ): Long

    external fun resizeWindow(windowHandle: Long, width: Int, height: Int): Int

    /**
     * Creates a simulation and attaches it to [windowHandle].
     *
     * Returns an opaque session handle, or 0 if the renderer could not start. The
     * shader and texture roots are passed in because the build tree and an installed
     * layout put them in different places.
     */
    external fun createSession(
        windowHandle: Long,
        width: Int,
        height: Int,
        maxCellCount: Int,
        maxFoodCount: Int,
        dishRadius: Double,
        shaderRoot: String,
        textureRoot: String
    ): Long

    external fun renderFrame(sessionHandle: Long): Int

    external fun queuePan(sessionHandle: Long, dx: Float, dy: Float): Int

    external fun queueZoom(
        sessionHandle: Long,
        factor: Float,
        screenX: Float,
        screenY: Float
    ): Int

    external fun queueTap(sessionHandle: Long, screenX: Float, screenY: Float): Int

    external fun setTemperatureMode(sessionHandle: Long, mode: Int): Int

    external fun setToolOrdinal(sessionHandle: Long, ordinal: Int): Int

    external fun resetWorld(sessionHandle: Long): Int

    external fun cellCount(sessionHandle: Long): Long

    external fun stopSession(sessionHandle: Long): Int

    external fun destroyWindow(windowHandle: Long): Int
}
