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

    /**
     * Takes a window the caller already owns as the surface to present into, and returns its
     * handle, or 0 if it is not a live window.
     *
     * Nothing is created. What this buys is the message queue: a window receives its messages on
     * the thread that created it, and the only window in this process whose messages are
     * dispatched is one the AWT toolkit made on the thread that pumps them. The heavyweight
     * canvas the interface puts on the screen is exactly that window, so the renderer is pointed
     * at it instead of at a window of its own -- and input, resizing and hiding then arrive by
     * the ordinary AWT routes rather than through Win32 calls from a thread that owns nothing.
     *
     * The window's geometry and lifetime stay the toolkit's: the bridge never moves, shows,
     * hides or destroys an adopted window.
     */
    external fun nativeAdoptWindow(windowHandle: Long): Long

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
     * The process's top-level windows in their real stacking order, topmost first.
     *
     * Neither half of this port can report a z-order about itself: Compose knows which of its
     * own surfaces is above which, Win32 knows which window is above which, and "is the
     * simulation underneath the interface" is the second question. A window that appears
     * above the renderer here takes clicks before the renderer sees them.
     */
    external fun nativeDescribeWindows(): String?

    /**
     * Places the child window at a screen point with an extent, in one call.
     *
     * The point is in screen coordinates and is converted to the parent's client space by the
     * bridge, because that conversion is Win32's to make: the managed side knows where the
     * interface put the viewport but not where the parent's client area begins or how the two
     * spaces are scaled, and guessing either is how a window ends up over the interface.
     */
    /**
     * Shows or hides the renderer's window.
     *
     * A child window is not clipped by what the interface draws, so it is in front of the
     * interface wherever it lies. That is what makes the simulation visible and also what made
     * it cover every other tab: the surface stays mounted while another tab is showing,
     * because it owns the device and the simulation thread, and the picture has to be taken
     * out of the way without the session being torn down.
     */
    external fun nativeShowWindow(windowHandle: Long, visible: Boolean): Int

    /**
     * How this process relates to the display's scaling, and how large the child window's
     * client area really is.
     *
     * This decides whether every other number means what it appears to. A process that declares
     * itself aware of the display's scaling is given real pixels by Win32; one that does not is
     * given virtualised ones, so the same value means two different things depending on who is
     * asked. A window sized from one space and hit-tested in the other lands where neither side
     * intended, and no amount of reasoning from one side alone can tell which space is which.
     */
    external fun nativeDescribeScaling(windowHandle: Long): String?

    external fun nativeSetChildWindowBounds(
        windowHandle: Long,
        screenX: Int,
        screenY: Int,
        width: Int,
        height: Int
    ): Int

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

    /**
     * Why a stored plate cannot be opened, or null when it can.
     *
     * Opening answers with a handle or with nothing, and the phone's decoder reports the rest as a
     * failure the interface shows. Without this the desktop could only say that the simulation did
     * not open, which is the same sentence for a plate from a newer build, a truncated file and a
     * file that is not a plate at all -- and the person had just chosen one of them and would like
     * to know which.
     *
     * The allocation is passed because the decoder needs a fallback for a file that predates
     * recording its own; it must be the same one the open would use, or this answers a different
     * question than the open does.
     */
    external fun nativeDescribeSubstrateProblem(
        substratePath: String,
        maxCellCount: Int,
        maxFoodCount: Int,
        dishRadius: Double
    ): String?

    /**
     * Writes the plate the session is showing into [substratePath], and prepares its preview.
     *
     * This is the phone's own save call, and it is the same one on purpose: the write goes through
     * the microscope owner, which is where the world is, so the file holds the world being edited
     * rather than a copy the interface happened to be holding.
     *
     * Returns null when the plate was written, and a sentence when it was not -- the writer's or the
     * decoder's own complaint rather than a code, because the person is owed the reason. The answer
     * to "is there a picture to go with it" is [previewToken]: its first element receives the
     * prepared preview frame's token, or zero for a plate written without one, which is a complete
     * plate and not a failed save. The array is written, never read, and must hold one element.
     *
     * The token is one-shot and belongs to the session's thread: capture it with
     * [nativeCaptureSubstratePreview] and give it back with [nativeDiscardSubstratePreview], both on
     * that thread. A token that is neither captured nor discarded holds the world it was taken from.
     */
    external fun nativeSaveSubstrate(
        sessionHandle: Long,
        substratePath: String,
        previewToken: LongArray
    ): String?

    /**
     * The side of a plate's preview picture, in pixels.
     *
     * The picture is square and its side is the container's own policy, so the caller sizes the
     * memory it hands over instead of guessing a number the format would have to agree with. It is a
     * property of the format rather than of a session, which is why it takes no handle.
     */
    external fun nativeSubstratePreviewPixels(): Int

    /**
     * Draws the prepared substrate preview [token] into the direct [pixels] buffer, once.
     *
     * The picture is [nativeSubstratePreviewPixels] square RGBA8 with [rowStride] bytes to a row; the
     * buffer's capacity is checked against that layout. It must be asked for from the thread that
     * owns the session -- the thread that draws frames.
     *
     * Returns 0 when the picture was drawn, 1 when the renderer refused it, 2 when the handle or the
     * arguments are not usable, and 3 when there was nothing left to capture -- the token had already
     * been used, or was never a preview token.
     */
    external fun nativeCaptureSubstratePreview(
        sessionHandle: Long,
        token: Long,
        pixels: java.nio.ByteBuffer,
        rowStride: Int
    ): Int

    /**
     * Attaches an encoded preview to a plate that was just written.
     *
     * The container's policy for a plate's picture is WebP lossy, and the attachment is what the
     * phone does with the picture it encodes, so the payload must be that same WebP: a payload that
     * is not one is refused by the format rather than stored as something a reader would then fail
     * on. It names the file rather than a session because the file is a temporary path the caller
     * owns -- what is being written is not yet a plate in the collection.
     *
     * A failure to attach does not fail the save, deliberately: the plate itself is already written,
     * and a picture is an ornament. A plate without one is read and run exactly the same.
     */
    external fun nativeAttachSubstratePreview(substratePath: String, webpBytes: ByteArray)

    /**
     * Gives back a prepared preview frame that will not be used.
     *
     * A prepared frame holds the world it was taken from until it is drawn or discarded, so a save
     * that decides against a picture must hand it back rather than leave it in the renderer's one
     * slot. Best effort by design: the frame is dead either way, and a plate that was written must
     * not be reported as unsaved because cleaning up after it failed.
     */
    external fun nativeDiscardSubstratePreview(sessionHandle: Long, token: Long)

    /**
     * The plate's own description, as the container records it, or null when it cannot be read.
     *
     * Everything the collection shows about a plate -- how many cells, how long it has been running,
     * how much food is left -- is in the metadata block and the body, which only the release codec
     * can read. The desktop adapter used to report nothing rather than guess, which was honest and
     * useless: the freezer showed zero cells for a plate holding thousands. This is the phone's own
     * inspection in the phone's own versioned form, decoded on this side by the desktop's codec.
     *
     * The allocation is passed for the same reason it is passed when opening: a plate that predates
     * recording its own needs a fallback, and describing a file with a different one would describe
     * a different plate than opening it would.
     */
    external fun nativeInspectSubstrate(
        substratePath: String,
        maxCellCount: Int,
        maxFoodCount: Int,
        dishRadius: Double
    ): ByteArray?

    /**
     * The picture a plate carries, as the file stores it, or null when it has none.
     *
     * The bytes are the container's own encoded preview rather than a re-encoding, so what is shown
     * is what a reader of the file sees. A plate without a picture is a normal plate, not a failure.
     */
    external fun nativeReadSubstratePreview(substratePath: String): ByteArray?

    /**
     * Draws the newest frame into [pixels] and returns what happened.
     *
     * This is how the desktop draws the world now. A window of its own cannot be composited
     * with the interface -- it is a sibling of it, so it is either in front of everything or
     * behind everything -- and a microscope whose controls, panels and dialogs are behind the
     * picture is not the microscope the phone shows. So the frame is asked for as pixels and
     * drawn inside the same scene as the rest of the interface, which is what a compositor
     * does for the phone's surface: the world ends up below the interface because the
     * interface is painted after it, in one window and one scene.
     *
     * [pixels] must be a direct buffer of at least `rowStride * height` bytes and stays the
     * caller's: nothing is allocated per frame, and the pixels land in it once. The buffer is
     * reused for the life of the session, so the caller copies out of it before asking for
     * the next frame.
     *
     * Returns 0 when a frame was drawn, 1 when the renderer refused it, 2 when the handle or
     * the arguments are not usable, and 3 when there is nothing to draw yet -- an idle
     * state, not a fault, which leaves the caller's previous picture on screen.
     */
    external fun nativeRenderFrameInto(
        sessionHandle: Long,
        pixels: java.nio.ByteBuffer,
        width: Int,
        height: Int,
        rowStride: Int
    ): Int

    /**
     * Draws the newest frame straight into memory at [address], and returns what happened.
     *
     * This is the same frame as [nativeRenderFrameInto] with one copy removed: the address is
     * the address of a picture the caller already owns and will draw, so the readback lands in
     * it and nothing has to be copied afterwards. The copy it removes is a whole window's
     * worth of pixels per frame, which at sixty frames a second is the difference between a
     * renderer that keeps up and one that spends its time moving bytes it is about to draw.
     *
     * The memory is borrowed, never owned: it must stay alive and untouched while the call
     * runs, and the caller is responsible for telling its graphics layer that the pixels it
     * handed over have changed.
     *
     * Returns the same statuses as [nativeRenderFrameInto].
     */
    external fun nativeRenderFrameIntoAddress(
        sessionHandle: Long,
        address: Long,
        width: Int,
        height: Int,
        rowStride: Int
    ): Int

    /**
     * Creates the window a Vulkan surface needs and nothing else.
     *
     * The device is created against a surface, so a session exists only while some window
     * does. This one is never shown, is placed off every monitor, is owned by nothing, and
     * never has a frame drawn into it: it is the handle the surface is created from. It must
     * be created and destroyed on the thread that owns the session, like every other call
     * here.
     */
    external fun nativeCreateSurfaceAnchorWindow(width: Int, height: Int): Long

    external fun nativeQueuePan(sessionHandle: Long, dx: Float, dy: Float): Int

    external fun nativeQueueZoom(
        sessionHandle: Long,
        factor: Float,
        screenX: Float,
        screenY: Float
    ): Int

    external fun nativeQueueTap(sessionHandle: Long, screenX: Float, screenY: Float): Int

    /**
     * The same tap, placing a cell grown from [genomeBytes] rather than from the plate's default.
     *
     * The bytes are the genome draft the editor saves and the phone publishes, and the renderer
     * decodes them with the same codec, so what lands in the plate is the creature the editor is
     * showing. Refused -- rather than quietly placing the default cell -- when the draft cannot be
     * read, because a fallback would leave the person looking at a cell and wondering which genome
     * it came from.
     *
     * The genome is attached to the tap rather than published once and remembered, because a tap
     * here is answered by this process on its own thread: what the person clicked is exactly what
     * they were looking at.
     */
    external fun nativeQueueTapWithGenome(
        sessionHandle: Long,
        screenX: Float,
        screenY: Float,
        genomeBytes: ByteArray
    ): Int

    /** Queues the velocity a pointer was released with, so the camera keeps moving. */
    external fun nativeQueueFling(
        sessionHandle: Long,
        velocityX: Float,
        velocityY: Float
    ): Int

    /**
     * Queues one touch phase: 0 down, 1 move, 2 up.
     *
     * Held pointers are what the MOVE tool drags a cell with, so the phases are paired by the
     * caller and a pointer that is never released leaves the simulation holding it.
     */
    external fun nativeQueueTouch(
        sessionHandle: Long,
        pointerId: Int,
        phase: Int,
        screenX: Float,
        screenY: Float
    ): Int

    external fun nativeSetTemperatureMode(sessionHandle: Long, mode: Int): Int

    external fun nativeSetToolOrdinal(sessionHandle: Long, ordinal: Int): Int

    external fun nativeSetSlowMotionSpeed(sessionHandle: Long, speed: Float): Int

    external fun nativeSetIncubateTargetUps(sessionHandle: Long, targetUps: Double): Int

    external fun nativeSetPerformanceHudEnabled(sessionHandle: Long, enabled: Boolean): Int

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
     * The runtime's status as eleven longs: revision, worldEpoch, completedUpdateCount,
     * cellCount, the bits of the food mass, the bits of the simulation time in seconds,
     * temperatureMode, toolOrdinal, selectedCellId, a failed flag and a diagnosis-requested
     * flag.
     *
     * It is one array rather than eleven calls because the runtime publishes these as a single
     * snapshot; read separately they would describe different moments and a status line could
     * show a cell count from before an update beside a revision from after it.
     *
     * Two of the entries are not numbers: a long cannot hold a fraction, so the food mass and
     * the simulation time travel as the bit pattern of a double and are read with
     * [Double.fromBits]. Android's bridge transports the same two fields the same way. Reading
     * them as longs produces a number that grows without meaning -- the rounding that used to be
     * here reported a plate that had food on it as empty.
     */
    external fun nativeReadStats(sessionHandle: Long): LongArray?

    /**
     * One cell as a wire document, or null when the runtime has no such cell.
     *
     * A cell that died after the call that named it is the ordinary reason for the null, so
     * callers must treat it as a stale reference rather than as a failure to report.
     */
    external fun nativeInspectCell(sessionHandle: Long, cellId: Int): ByteArray?

    /**
     * The selected cell's genome, in the same format the phone reads it in.
     *
     * Separate from the cell snapshot because the runtime publishes them separately: a cell
     * changes every frame and a genome does not, so they are two documents rather than one.
     * Null when nothing is selected, which is the ordinary state of a plate nobody has tapped.
     */
    external fun nativeInspectSelectedGenome(sessionHandle: Long): ByteArray?

    /**
     * Opens a renderer for the genome preview: a session with no window and no simulation.
     *
     * The preview draws into memory, so it needs a device rather than a surface, and the organism
     * it draws is grown on the spot from the genome it is handed. Such a session never presents a
     * frame and never steps a world, which is why it is opened on its own rather than being a
     * mode of a plate.
     *
     * Returns an opaque handle, or 0 when the renderer could not be created. The caller closes it
     * with [nativeStopSession] like any other session.
     */
    external fun nativeCreateGenomePreviewSession(
        windowHandle: Long,
        shaderRoot: String,
        textureRoot: String
    ): Long

    /**
     * The edge of the square a genome preview is drawn at, in pixels.
     *
     * Asked for rather than assumed, because the size belongs to the renderer: a caller that
     * hard-coded it would keep working after the renderer changed and quietly show a corner of a
     * larger picture.
     */
    external fun nativeGenomePreviewPixels(): Int

    /**
     * The same preview into a direct buffer, for callers that hold one.
     *
     * Written as a second entry point rather than as a conversion in the caller because the
     * conversion would be a copy of a whole picture: a test that wanted pixels and got them by
     * way of an address would be measuring a different path from the one the editor uses.
     */
    external fun nativeCaptureGenomePreview(
        sessionHandle: Long,
        genomeBytes: ByteArray,
        maximumSimulationTime: Double,
        pixels: java.nio.ByteBuffer,
        rowStride: Int
    ): Int
    /** The same into a direct buffer, for callers that hold one. */
    external fun nativeCaptureGenomePreviewWithCamera(
        sessionHandle: Long,
        genomeBytes: ByteArray,
        maximumSimulationTime: Double,
        cameraCenterX: Double,
        cameraCenterY: Double,
        cameraZoom: Float,
        pixels: java.nio.ByteBuffer,
        rowStride: Int
    ): Int
    /**
     * Hands the preview session the genome it should show. The phone publishes content and view
     * state separately and then asks the preview to draw -- that is what makes an interactive
     * preview interactive, because the camera survives between frames.
     */
    external fun nativePublishGenomePreviewContent(
        sessionHandle: Long,
        genomeBytes: ByteArray,
        simulationTimeHours: Double
    ): Int

    /** Where the preview's view stands: selected gene, grid, tree, viewport size. */
    external fun nativePublishGenomePreviewViewState(
        sessionHandle: Long,
        selectedGeneIndex: Int,
        gridEnabled: Boolean,
        treeEnabled: Boolean,
        viewportWidth: Int,
        viewportHeight: Int
    ): Int

    /**
     * One thing the person did to the preview, numbered as the phone numbers it:
     * 0 restore viewport, 1 move, 2 zoom, 3 select gene, 4 capture ghost, 5 clear ghost.
     */
    external fun nativeEnqueueGenomePreviewInteraction(
        sessionHandle: Long,
        kind: Int,
        first: Float,
        second: Float,
        third: Float
    ): Int
    /**
     * The preview's own frame, drawn into the caller's memory.
     *
     * This is the world the published content built, seen through the viewport the published view
     * moved -- so a camera somebody dragged is the camera the picture comes from. [capacityBytes] is
     * the size of the memory at [address]; a frame that would not fit is refused. Returns 0 when a
     * frame was written, 3 when there is nothing to draw yet, 1 when the session is gone.
     */
    external fun nativeDrawGenomePreviewIntoAddress(
        sessionHandle: Long,
        address: Long,
        rowStride: Int,
        capacityBytes: Long
    ): Int

    /**
     * Where the preview stands: how many cells it grew, which gene a tap chose, and its camera.
     *
     * Nine numbers, read as one snapshot rather than as nine calls, because a cell count from
     * before a selection read beside the selection itself would describe two different previews.
     * In order: the cell count, the content revision it was counted at, the preview's cell
     * allocation, the selection token, the selected gene index, the viewport revision, and then
     * the camera's centre X, centre Y and zoom.
     *
     * The last three are the bit patterns of floats, for the reason the plate's food mass travels
     * that way: a long cannot hold a fraction, and a rounded camera is a camera nobody set. Read
     * them with [Float.fromBits].
     *
     * Null when the handle is not a live session.
     */
    external fun nativeReadGenomePreviewStatus(sessionHandle: Long): LongArray?

    external fun nativeStopSession(sessionHandle: Long): Int

    external fun nativeDestroyWindow(windowHandle: Long): Int
}
