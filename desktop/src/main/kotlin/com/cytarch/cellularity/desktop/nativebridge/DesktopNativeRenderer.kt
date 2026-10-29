/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import java.io.File
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

    /** Where Compose Desktop leaves a packaged application's resources. */
    private const val RESOURCES_DIRECTORY_PROPERTY = "compose.application.resources.dir"

    /** The directory `stageNativeAssets` in desktop/build.gradle stages into. */
    private const val PACKAGED_NATIVE_DIRECTORY = "native"

    /** Where a CMake build tree keeps the library, relative to the tree's root. */
    private val DEVELOPMENT_LIBRARY_LOCATIONS = listOf(
        "build/win-cmake",
        "build/win-cmake/Release",
        "build/win-cmake/Debug"
    )

    /**
     * The libraries the renderer imports that a JRE image does not already provide.
     *
     * They are named rather than discovered because a JNI load gives no chance to discover
     * them: the import is resolved while `System.load` runs, and by then it is too late to
     * load anything.
     */
    private val SUPPORT_LIBRARIES = listOf("vulkan-1.dll", "z.dll")

    /**
     * Loads the library, or records why it could not be loaded.
     *
     * The failure is captured rather than thrown so a missing library degrades to a
     * message in the interface instead of a crash on startup. A user whose build is
     * incomplete should see what is missing, not a stack trace.
     *
     * The layouts are tried in turn, and the first that holds the library wins.
     *
     * A packaged application keeps the library beside its shaders and textures, under the
     * resource directory Compose Desktop passes as `compose.application.resources.dir`.
     * That one is tried first because it is the only layout a user's install has: the
     * discovery below walks up from the working directory, and a packaged launch has
     * whatever working directory the user happened to start it from.
     *
     * A development run keeps it in the CMake build tree, where the JVM does not search at
     * all. That one is found by walking up from the working directory, but only inside a
     * directory that still holds a CMake cache: that marker is what separates a build tree
     * from an installed layout, and without it this would happily load a stale library left
     * behind by an old build.
     *
     * CELLULARITY_NATIVE_DIR still names the directory directly, and the launcher's own
     * library path is the last resort for a hand-assembled install.
     */
    private val loadFailure: Throwable? = try {
        val packaged = packagedNativeDirectory()?.let(::libraryIn)
        val fromEnvironment = System.getenv("CELLULARITY_NATIVE_DIR")
            ?.takeIf { it.isNotBlank() }
            ?.let { Path.of(it) }
            ?.let(::libraryIn)
        val fromBuildTree = findDevelopmentLibrary()
        // The application directory is the fallback for a copy made beside the launcher
        // rather than into the resources tree. It is read from java.library.path instead of
        // being assumed, so an install that points somewhere else still resolves.
        val fromApplicationDirectory = applicationLibraryDirectories()
            .firstNotNullOfOrNull(::libraryIn)
        val discovered = packaged
            ?: fromEnvironment
            ?: fromBuildTree
            ?: fromApplicationDirectory
        if (discovered != null) {
            // The support libraries the renderer imports have to be resolvable before it is
            // loaded, and Windows resolves an import against the directory of the module
            // being loaded last of all. Loading them from the same directory first makes
            // the packaged copy win over whatever happens to be installed, which is the
            // difference between using the Vulkan loader this build was tested against and
            // using a stranger's.
            loadSupportLibrariesBeside(discovered.parent)
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
     * The resource directory Compose Desktop gives a packaged application.
     *
     * The property is set from the launcher's configuration, so its presence is also what
     * tells this process that it is running from an install rather than from the
     * repository.
     */
    private fun packagedNativeDirectory(): Path? {
        val resources = System.getProperty(RESOURCES_DIRECTORY_PROPERTY)
            ?.takeIf { it.isNotBlank() }
            ?.let { Path.of(it) }
            ?: return null
        val native = resources.resolve(PACKAGED_NATIVE_DIRECTORY)
        return native.takeIf { Files.isDirectory(it) }
    }

    /**
     * The directories a packaged launcher puts on the library path.
     *
     * Every entry is returned rather than the first one that exists, because the launcher
     * lists the bundled runtime's `bin` as well as the application directory and the
     * library could legitimately be in either.
     */
    private fun applicationLibraryDirectories(): List<Path> =
        System.getProperty("java.library.path")
            ?.split(File.pathSeparator)
            ?.asSequence()
            ?.filter { it.isNotBlank() }
            ?.map { Path.of(it) }
            ?.filter { Files.isDirectory(it) }
            ?.toList()
            ?: emptyList()

    /** Loads any support library [directory] holds, skipping the ones already loaded. */
    private fun loadSupportLibrariesBeside(directory: Path?) {
        if (directory == null) return
        for (library in SUPPORT_LIBRARIES) {
            val candidate = directory.resolve(library)
            if (!Files.isRegularFile(candidate)) continue
            try {
                System.load(candidate.toAbsolutePath().toString())
            } catch (ignored: UnsatisfiedLinkError) {
                // A support library that will not load is not necessarily fatal: Windows
                // may already have resolved the import from the system, and the load of the
                // renderer itself is what reports whether it did.
            }
        }
    }

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
     * Where the child window is, and how much room its parent has.
     *
     * Eight numbers: the child's screen rectangle, the parent's client size, and the child's
     * offset inside that client area. Two systems decide these -- Win32, which this bridge
     * calls, and the layout, which Compose decides -- and a disagreement between them is
     * invisible from inside either one. Reporting them together is what makes it visible.
     *
     * Null when the handle is not a live window this bridge created.
     */
    external fun nativeWindowGeometry(windowHandle: Long): LongArray?

    /**
     * The extent the surface is actually presenting at, as {width, height}.
     *
     * Not the window's size, and not the requested size: the swapchain is resized from the
     * thread that presents, so between a window resize and the next frame these disagree.
     * Reading it is how a caller tells "the surface caught up" from "it never will".
     */
    external fun nativeSurfaceExtent(sessionHandle: Long): LongArray?

    /**
     * What Win32 would hand a click at this screen point to, among the windows the bridge
     * made: 1 for one of them, 0 for something else inside the same top-level window, -1 for
     * somewhere else entirely.
     *
     * The simulation is a native child window and the interface is drawn by Compose into the
     * same top-level window, so which one a click reaches depends on what Windows considers
     * to be on top at that pixel. Neither side knows about the other's windows, so this is the
     * only account of it that is not a guess.
     */
    external fun nativeWindowAtPoint(screenX: Int, screenY: Int): Int

    /**
     * Writes one of the bridge's windows into a BMP file and returns the byte count, or -1.
     *
     * This is how the renderer's output is inspected without inspecting the machine: a screen
     * capture would photograph whatever is in front and would take the screen from whoever is
     * using it, while capturing one window by its handle reads only that window and leaves
     * everyone else's screen untouched.
     */
    external fun nativeCaptureWindow(windowHandle: Long, path: String): Int

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

    /**
     * The same, opening a plate that was saved earlier rather than starting a new one.
     *
     * A separate entry point rather than an extra parameter on [nativeCreateSession],
     * because the JVM resolves these by name: widening the signature of the existing one
     * would not be a compile error anywhere, it would be an UnsatisfiedLinkError on the
     * first click. Two names make the difference visible where it can be checked.
     *
     * The counts are still passed, and they are not redundant: they are the allocation the
     * plate was saved with, and the loader needs a fallback for a file that predates
     * recording it. What the file does supply is the world itself, including when it was
     * created -- which is the difference between continuing an experiment and starting one.
     */
    external fun nativeCreateSessionFromSubstrate(
        windowHandle: Long,
        width: Int,
        height: Int,
        maxCellCount: Int,
        maxFoodCount: Int,
        dishRadius: Double,
        shaderRoot: String,
        textureRoot: String,
        substratePath: String
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
     * What one world of this shape would cost in memory, or null when it would not fit.
     *
     * The two answers are different and the difference is the point: a number is shown to
     * the person as the price of starting a plate, and a refusal is what disables the start
     * button. A refusal must not be reported as zero, because zero reads as "this is free".
     *
     * This does not need a session, and that is deliberate: it is asked before there is
     * anything to ask about, which is exactly why the freezer can decide whether a plate is
     * startable at all.
     */
    fun estimateWorldAllocationBytes(
        maxCellCount: Int,
        maxFoodCount: Int,
        radius: Double
    ): Long? {
        if (!isAvailable) return null
        val bytes = nativeEstimateWorldAllocationBytes(maxCellCount, maxFoodCount, radius)
        return bytes.takeIf { it >= 0L }
    }

    external fun nativeEstimateWorldAllocationBytes(
        maxCellCount: Int,
        maxFoodCount: Int,
        radius: Double
    ): Long

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
