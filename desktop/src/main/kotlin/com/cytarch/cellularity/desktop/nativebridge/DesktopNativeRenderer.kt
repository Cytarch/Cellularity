/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import java.nio.file.Files
import java.nio.file.Path

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
object DesktopNativeRenderer {

    /**
     * Loads the library, or records why it could not be loaded.
     *
     * The failure is captured rather than thrown so a missing library degrades to a
     * message in the interface instead of a crash on startup. A user whose build is
     * incomplete should see what is missing, not a stack trace.
     *
     * The library is built by CMake, so a development copy lives in the build tree where
     * the JVM does not search. It is found by walking up from the working directory, but
     * only inside a directory that still holds a CMake cache: that marker is what
     * separates a build tree from an installed layout, and without it this would happily
     * load a stale library left behind by an old build.
     */
    private val loadFailure: Throwable? = try {
        val fromEnvironment = System.getenv("CELLULARITY_NATIVE_DIR")
            ?.takeIf { it.isNotBlank() }
            ?.let { Path.of(it) }
            ?.let(::libraryIn)
        val discovered = fromEnvironment ?: findDevelopmentLibrary()
        if (discovered != null) {
            System.load(discovered.toAbsolutePath().toString())
        } else {
            System.loadLibrary("cellularity_desktop")
        }
        null
    } catch (failure: UnsatisfiedLinkError) {
        failure
    } catch (failure: SecurityException) {
        failure
    }

    /** The library in [directory], or null when it is not there. */
    private fun libraryIn(directory: Path): Path? =
        directory.resolve(System.mapLibraryName("cellularity_desktop"))
            .takeIf { Files.isRegularFile(it) }

    /**
     * The library in the build tree, if this is running from one.
     *
     * A CMake cache in the directory is what makes it a build tree; the same walk
     * without that check would find any stray copy left in a parent directory.
     */
    private fun findDevelopmentLibrary(): Path? {
        var directory: Path? = Path.of("").toAbsolutePath()
        while (directory != null) {
            for (relative in DEVELOPMENT_LIBRARY_LOCATIONS) {
                val candidate = directory.resolve(relative)
                if (!Files.isRegularFile(candidate.resolve("CMakeCache.txt"))) continue
                libraryIn(candidate)?.let { return it }
            }
            directory = directory.parent
        }
        return null
    }

    private val DEVELOPMENT_LIBRARY_LOCATIONS = listOf(
        "build/win-cmake",
        "build/win-cmake/Release",
        "build/win-cmake/Debug"
    )

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
    external fun nativeCreateWindow(
        ownerWindowHandle: Long,
        x: Int,
        y: Int,
        width: Int,
        height: Int
    ): Long

    external fun nativeResizeWindow(windowHandle: Long, width: Int, height: Int): Int

    /**
     * Creates a simulation and attaches it to [windowHandle].
     *
     * Returns an opaque session handle, or 0 if the renderer could not start. The
     * shader and texture roots are passed in because the build tree and an installed
     * layout put them in different places.
     */
    external fun nativeCreateSession(
        windowHandle: Long,
        width: Int,
        height: Int,
        maxCellCount: Int,
        maxFoodCount: Int,
        dishRadius: Double,
        shaderRoot: String,
        textureRoot: String
    ): Long

    external fun nativeRenderFrame(sessionHandle: Long): Int

    external fun nativeQueuePan(sessionHandle: Long, dx: Float, dy: Float): Int

    external fun nativeQueueZoom(
        sessionHandle: Long,
        factor: Float,
        screenX: Float,
        screenY: Float
    ): Int

    external fun nativeQueueTap(sessionHandle: Long, screenX: Float, screenY: Float): Int

    external fun nativeSetTemperatureMode(sessionHandle: Long, mode: Int): Int

    external fun nativeSetToolOrdinal(sessionHandle: Long, ordinal: Int): Int

    external fun nativeResetWorld(sessionHandle: Long): Int

    external fun nativeCellCount(sessionHandle: Long): Long

    /**
     * The environment as one wire document, or null when there is nothing to read.
     *
     * The document is handed over whole and undecoded because the schema belongs to one
     * place. Parsing it here as well as in the editor would make a second reader that the
     * next schema change has to be told about, and a reader that silently disagrees with
     * the writer is the failure this shape avoids.
     *
     * This is a request round-trip to the simulation thread rather than a field read, so it
     * waits for the runtime to service it and is not something to call once per frame.
     */
    external fun nativeReadEnvironment(sessionHandle: Long): ByteArray?

    /**
     * Applies a patch document and returns the result document the runtime wrote for it.
     *
     * A null means the exchange did not happen at all. A patch that was refused still
     * returns a document, because the refusal carries which change was rejected and why,
     * and that account is the only thing a caller can act on.
     */
    external fun nativeApplyEnvironmentPatch(
        sessionHandle: Long,
        patchBytes: ByteArray
    ): ByteArray?

    /**
     * The runtime's status as ten longs: revision, worldEpoch, completedUpdateCount,
     * cellCount, foodWholeUnits, simulationTimeMillis, temperatureMode, toolOrdinal,
     * selectedCellId and a failed flag.
     *
     * It is one array rather than ten calls because the runtime publishes these as a single
     * snapshot; read separately they would describe different moments and a status line
     * could show a cell count from before an update beside a revision from after it.
     */
    external fun nativeReadStats(sessionHandle: Long): LongArray?

    /**
     * One cell as a wire document, or null when the runtime has no such cell.
     *
     * A cell that died after the call that named it is the ordinary reason for the null, so
     * callers must treat it as a stale reference rather than as a failure to report.
     */
    external fun nativeInspectCell(sessionHandle: Long, cellId: Int): ByteArray?

    external fun nativeStopSession(sessionHandle: Long): Int

    external fun nativeDestroyWindow(windowHandle: Long): Int
}
