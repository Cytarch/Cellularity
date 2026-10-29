/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.awt.SwingPanel
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.material3.Text
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.Alignment
import androidx.compose.ui.unit.dp
import java.awt.Canvas
import java.awt.Color as AwtColor
import java.awt.Component
import java.awt.Dimension
import java.awt.event.ComponentAdapter
import java.awt.event.ComponentEvent
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import java.awt.event.MouseWheelEvent
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/**
 * The simulation's input, in the surface's own client pixels.
 *
 * A sealed hierarchy rather than raw callbacks because every one of these has to be
 * applied on the thread that owns the renderer, and the queued value is the natural
 * place to make that requirement visible.
 */
sealed interface SurfaceInputEvent {
    data class Pan(val dx: Float, val dy: Float) : SurfaceInputEvent
    data class Zoom(val factor: Float, val x: Float, val y: Float) : SurfaceInputEvent
    data class Tap(val x: Float, val y: Float) : SurfaceInputEvent
    data class Temperature(val mode: Int) : SurfaceInputEvent
    data class Tool(val ordinal: Int) : SurfaceInputEvent
    data object Reset : SurfaceInputEvent
}

/**
 * The most recently mounted surface, so a probe can ask what it established.
 *
 * A window that shows no simulation can be in several states that look identical from
 * outside -- no canvas laid out yet, no native window, no session -- and a cell count of
 * zero does not say which. This is held as a weak reference because it exists for
 * diagnosis only: a probe that never runs must not keep a surface, and with it a Vulkan
 * device, alive.
 *
 * It is deliberately not a parameter of the composable. Those say what the surface is for;
 * this says what happened to it, and mixing the two would put a testing concern into every
 * caller.
 */
object SurfaceDiagnostics {
    private var latest: java.lang.ref.WeakReference<Any>? = null

    internal fun register(holder: Any) {
        latest = java.lang.ref.WeakReference(holder)
    }

    /** What the newest surface established, or a note that there is no surface. */
    fun describe(): String {
        val holder = latest?.get() ?: return "no surface has been mounted"
        return try {
            val describe = holder.javaClass.getDeclaredMethod("describe")
            describe.isAccessible = true
            describe.invoke(holder) as? String ?: "the surface did not describe itself"
        } catch (thrown: ReflectiveOperationException) {
            "the surface could not be described: $thrown"
        } catch (thrown: RuntimeException) {
            "the surface could not be described: $thrown"
        }
    }
}

/**
 * The live simulation, rendered by the native renderer inside the Compose window.
 *
 * The renderer presents into a Win32 child window, and the child window is parented to
 * the heavyweight AWT canvas that Compose puts here. That is the whole trick: Compose
 * draws its own scene with Skia into the same top-level window, and the child window
 * sits above it with the simulation in it, so the interface and the simulation are one
 * window rather than two.
 *
 * The canvas is deliberately heavyweight. A lightweight Swing component would be painted
 * by Skia and have no native window of its own, and then there would be nothing to parent
 * the renderer to.
 *
 * Everything the renderer is asked to do happens on a thread of this surface's own, never
 * on the thread that draws the interface. See [SurfaceHolder] for why that is not an
 * optimisation.
 */
@Composable
fun MicroscopeRenderSurface(
    assetRoots: DesktopAssetRoots.Roots?,
    maxCellCount: Int,
    maxFoodCount: Int,
    dishRadius: Double,
    inputEvents: List<SurfaceInputEvent>,
    onCellCount: (Long) -> Unit,
    onFailure: (String) -> Unit,
    onSessionReady: (Long) -> Unit = { },
    /**
     * A saved plate to open, or null to start a new one.
     *
     * The choice is made once, when the surface is created, and not revisited: a session
     * is a running world and cannot become a different one, so the alternative to opening
     * the file would be tearing the world down and building another.
     */
    substratePath: String? = null,
    modifier: Modifier = Modifier
) {
    if (!DesktopNativeRenderer.isAvailable || assetRoots == null) {
        RendererUnavailable(
            reason = if (!DesktopNativeRenderer.isAvailable) {
                DesktopNativeRenderer.failureDescription
                    ?: "The desktop renderer library could not be loaded."
            } else {
                "The compiled shaders and textures were not found. Build the native " +
                    "targets first, or set CELLULARITY_HOST_SHADER_ROOT and " +
                    "CELLULARITY_HOST_TEXTURE_ROOT."
            },
            modifier = modifier
        )
        return
    }

    // The native window and the session outlive recomposition, so they are held in
    // plain fields rather than in state: changing them must not itself trigger a
    // recomposition, and a recomposition must not recreate them.
    val holder = remember {
        SurfaceHolder(
            maxCellCount = maxCellCount,
            maxFoodCount = maxFoodCount,
            dishRadius = dishRadius,
            shaderRoot = assetRoots.shaderRoot.toString(),
            textureRoot = assetRoots.textureRoot.toString(),
            onCellCount = onCellCount,
            onFailure = onFailure,
            substratePath = substratePath,
            onSessionReady = onSessionReady
        ).also(SurfaceDiagnostics::register)
    }

    DisposableEffect(holder) {
        onDispose { holder.release() }
    }

    Box(modifier = modifier) {
        SwingPanel(
            modifier = Modifier.fillMaxSize(),
            factory = { holder.attachCanvas() },
            update = { }
        )
    }

    // The queue is handed to the frame loop rather than drained by its own effect.
    // A LaunchedEffect keyed on the input list would restart every time an event arrived
    // and replay the whole list, so every pan would be applied once per event that
    // followed it. The loop already runs once per frame; this makes it the single place
    // that touches the renderer, which is also what the renderer requires.
    DisposableEffect(holder, inputEvents) {
        holder.hold(inputEvents)
        onDispose { holder.clearHold(inputEvents) }
    }

    // The interface's view of the world is refreshed here rather than from the render
    // thread. The count is written by that thread and read here, so the two never touch
    // the same state.
    LaunchedEffect(holder) {
        while (true) {
            kotlinx.coroutines.delay(CELL_COUNT_REFRESH_MILLIS)
            holder.publishCellCount()
        }
    }
}

private const val CELL_COUNT_REFRESH_MILLIS = 250L

@Composable
private fun RendererUnavailable(reason: String, modifier: Modifier = Modifier) {
    Box(modifier = modifier, contentAlignment = Alignment.Center) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp),
            modifier = Modifier.padding(24.dp)
        ) {
            Text(text = "The simulation renderer is unavailable", textAlign = TextAlign.Center)
            Text(text = reason, textAlign = TextAlign.Center)
        }
    }
}

/**
 * Owns the native window, the session, and the thread that drives them.
 *
 * ## Why there is a thread here
 *
 * The native bridge requires one calling thread for the whole life of a session: the
 * backend records the thread that created it and refuses every later call from another,
 * and the swapchain and command buffers are single-owner for the same reason. What that
 * thread must not be is the one drawing the interface.
 *
 * It was, and the result is what a person saw: opening an experiment left the simulation
 * drawing and the interface gone. Presenting blocks until the display is ready, so a loop
 * that draws and then waits runs the event thread out of time to handle a single click;
 * and while the session did not exist yet the loop slept, which is worse, because sleeping
 * in the event thread stops the interface completely rather than merely slowing it. The
 * window could not even be closed by its own button, because the click had nowhere to be
 * delivered.
 *
 * So the thread is created here, once, and everything the renderer is asked to do goes
 * through it: creating the window and the session, presenting frames, stopping. The AWT
 * callbacks publish what they see and return immediately, and the composition only reads.
 *
 * ## What may be touched from where
 *
 * [pendingInput] and the two size fields are the crossing points and are atomic; the
 * native handles below are read and written only by the render thread. The one exception
 * is the AWT peer handle, which is looked up where the event that reveals it arrives and
 * published, because that lookup reflects over an AWT component and belongs on the thread
 * that owns it.
 */
private class SurfaceHolder(
    private val maxCellCount: Int,
    private val maxFoodCount: Int,
    private val dishRadius: Double,
    private val shaderRoot: String,
    private val textureRoot: String,
    private val substratePath: String?,
    private val onCellCount: (Long) -> Unit,
    private val onFailure: (String) -> Unit,
    private val onSessionReady: (Long) -> Unit
) {
    @Volatile
    private var canvas: Canvas? = null

    /** Everything the interface has queued for the renderer, consumed once per frame. */
    private val pendingInput = AtomicReference<List<SurfaceInputEvent>>(emptyList())

    /** The renderer's viewport, as the interface last observed it, or zero for none yet. */
    private val requestedExtent = AtomicLong(0L)

    /** The parent window handle, published by the AWT callback that learned it. */
    private val ownerHandle = AtomicLong(0L)

    /** The newest cell count, written by the render thread and read by the composition. */
    private val latestCellCount = AtomicLong(0L)

    /** Pointer input from the AWT callbacks, drained by the frame loop. */
    private val queuedPointerInput = AtomicReference<List<SurfaceInputEvent>>(emptyList())

    /** The live session, published so a caller can see whether there is one. */
    private val publishedSession = AtomicLong(0L)

    /**
     * What the surface has managed to establish so far.
     *
     * Answered from the render thread's own view, because that is the only thread whose
     * progress decides whether a session exists. A caller that sees a surface with no window
     * and no session can tell "nothing has been laid out yet" from "the renderer refused",
     * which a cell count of zero cannot express.
     */
    fun describe(): String = buildString {
        append("thread=")
        append(if (threadStarted.get() != 0L) "started" else "not started")
        append(" extent=")
        append(unpackWidth(requestedExtent.get()))
        append('x')
        append(unpackHeight(requestedExtent.get()))
        append(" owner=")
        append(ownerHandle.get() != 0L)
        append(" window=")
        append(publishedWindow.get() != 0L)
        // The handle itself, not whether there is one: a probe outside the composition can
        // only ask the renderer about a session it can name, and publishing a boolean would
        // leave it unable to name anything.
        append(" session=")
        append(publishedSession.get())
        append(" reported=")
        append(reported)
        append(" cells=")
        append(latestCellCount.get())
        // The canvas is the component the native child window is parented to, so its size is
        // the size the simulation will occupy. Reported here rather than found by walking the
        // AWT hierarchy, which cannot tell this canvas from any other of the same size.
        append(" canvas=")
        append(canvas?.let { "${it.width}x${it.height}" } ?: "none")
    }

    private val threadStarted = AtomicLong(0L)

    private val publishedWindow = AtomicLong(0L)

    @Volatile
    private var released = false

    @Volatile
    private var reported = false

    /**
     * The render thread.
     *
     * A plain thread rather than a coroutine dispatcher: this is one blocking loop whose
     * whole purpose is to keep a particular thread alive and identical for the renderer,
     * and nothing about it is asynchronous.
     *
     * Started on first use rather than in the constructor, so that every field it reads is
     * initialised before it can read one. A thread started from a constructor can run
     * before the constructor has finished, and the loop would then read fields that are
     * still null.
     */
    private val thread: Thread by lazy {
        Thread(::runFrameLoop, "cellularity-render").apply {
            isDaemon = true
            start()
        }
    }

    fun start() {
        // Touching the lazy value is what starts the thread; the loop itself waits for the
        // canvas to be laid out.
        thread
        threadStarted.set(thread.id)
    }

    /**
     * Creates the heavyweight canvas and tells the render thread where to put its window.
     *
     * The native window cannot be created here. A component that is not yet displayable has
     * no native peer, and therefore no handle to parent to, so the listener waits for that
     * to become true rather than assuming it. What it does here is only what has to happen
     * on this thread: reading the handle and the extent, and publishing them.
     */
    fun attachCanvas(): Component {
        val surface = Canvas().apply {
            background = AwtColor(0x1E, 0x1E, 0x1E)
            // A preferred size keeps the first layout from collapsing the canvas to
            // nothing, which would open the native window at zero extent.
            preferredSize = Dimension(640, 480)
            minimumSize = Dimension(1, 1)
        }
        surface.addComponentListener(object : ComponentAdapter() {
            override fun componentResized(event: ComponentEvent) {
                publishGeometry(event.component)
            }

            override fun componentShown(event: ComponentEvent) {
                publishGeometry(event.component)
            }
        })
        surface.addMouseListener(surfaceMouseHandler(surface))
        surface.addMouseMotionListener(surfaceMouseHandler(surface))
        surface.addMouseWheelListener(surfaceMouseHandler(surface))
        canvas = surface
        // The listeners above only fire on a change. A canvas that Swing has already laid
        // out was resized before they were attached, so waiting for the event would wait
        // forever and the renderer would never get a window. Asking once here covers that
        // case; the listener covers every later one.
        publishGeometry(surface)
        // Nothing can be presented until the canvas is laid out, so the loop is started
        // here rather than before there is anything for it to wait for.
        start()
        return surface
    }

    /**
     * Translates pointer events into queued simulation input.
     *
     * One handler serves all three listener roles so the drag state is shared: a press has
     * to be remembered across a motion event and then judged on release, and two separate
     * handlers could not see each other's state.
     *
     * The handlers queue rather than call, for the same reason the composition does: this
     * runs on the event thread, and the renderer may only be spoken to from the other one.
     */
    private fun surfaceMouseHandler(surface: Canvas): MouseAdapter =
        object : MouseAdapter() {
            private var pressedX = 0
            private var pressedY = 0
            private var lastX = 0
            private var lastY = 0
            private var dragging = false
            private var moved = false

            override fun mousePressed(event: MouseEvent) {
                dragging = true
                moved = false
                pressedX = event.x
                pressedY = event.y
                lastX = event.x
                lastY = event.y
                // The canvas has to ask for focus explicitly, otherwise the keyboard
                // shortcuts (temperature, tool, reset) go to whichever component had it.
                surface.requestFocusInWindow()
            }

            override fun mouseReleased(event: MouseEvent) {
                if (!dragging) return
                dragging = false
                // A press that never moved is a click, which is the gesture that selects a
                // cell or places one. A press that moved was a pan, which has already been
                // applied frame by frame, and must not also count as a click or every pan
                // would end by selecting whatever it stopped over.
                if (!moved) {
                    enqueue(SurfaceInputEvent.Tap(event.x.toFloat(), event.y.toFloat()))
                }
            }

            override fun mouseDragged(event: MouseEvent) {
                if (!dragging) return
                val dx = event.x - lastX
                val dy = event.y - lastY
                lastX = event.x
                lastY = event.y
                if (dx == 0 && dy == 0) return
                moved = true
                enqueue(SurfaceInputEvent.Pan(dx.toFloat(), dy.toFloat()))
            }

            override fun mouseWheelMoved(event: MouseWheelEvent) {
                // Multiplicative, matching the wheel on the phone and the native host:
                // zooming in then out returns exactly where it started.
                val factor = if (event.wheelRotation < 0) 1.1f else 1f / 1.1f
                enqueue(
                    SurfaceInputEvent.Zoom(factor, event.x.toFloat(), event.y.toFloat())
                )
            }
        }

    /** Records the canvas's extent and parent, for the render thread to act on. */
    private fun publishGeometry(component: Component) {
        if (released) return
        val width = component.width
        val height = component.height
        if (width <= 0 || height <= 0) return
        requestedExtent.set(pack(width, height))

        // Not an error yet when this is zero: AWT realises the peer asynchronously and the
        // listener calls back once it has. It stays an error if it never does, which is why
        // the screen reports that the renderer is unavailable rather than nothing.
        val handle = nativeWindowHandleOf(component)
        if (handle != 0L) ownerHandle.set(handle)
    }

    /** Publishes the queue this composition is using. */
    fun hold(events: List<SurfaceInputEvent>) {
        pendingInput.set(events)
    }

    /** Drops the queue, but only if it is still the one being cleared. */
    fun clearHold(events: List<SurfaceInputEvent>) {
        pendingInput.compareAndSet(events, emptyList())
    }

    /** Hands an event from an AWT callback to the render thread. */
    private fun enqueue(event: SurfaceInputEvent) {
        // Appending to the queue the composition published would be lost the next time it
        // republishes, so pointer input goes to a queue of its own that the frame loop also
        // drains. A single-slot swap is enough because a person cannot generate pointer
        // events faster than frames.
        queuedPointerInput.updateAndGet { current -> current + event }
    }

    /** Reads the newest count. Called from the composition. */
    fun publishCellCount() {
        onCellCount(latestCellCount.get())
    }

    private fun runFrameLoop() {
        var windowHandle = 0L
        var sessionHandle = 0L
        var appliedExtent = 0L
        try {
            while (!released) {
                val pointer = queuedPointerInput.getAndSet(emptyList())
                val fromComposition = pendingInput.getAndSet(emptyList())
                if (sessionHandle != 0L) {
                    for (event in pointer) apply(sessionHandle, event)
                    for (event in fromComposition) apply(sessionHandle, event)
                }

                val requested = requestedExtent.get()
                if (requested == 0L) {
                    // The canvas has not been laid out yet, so there is nothing to present
                    // into. This is the wait that used to happen in the event thread.
                    sleepFrame()
                    continue
                }

                if (windowHandle == 0L) {
                    val owner = ownerHandle.get()
                    if (owner == 0L) {
                        sleepFrame()
                        continue
                    }
                    windowHandle = DesktopNativeRenderer.nativeCreateWindow(
                        owner,
                        0,
                        0,
                        unpackWidth(requested),
                        unpackHeight(requested)
                    )
                    if (windowHandle == 0L) {
                        failOnce("The renderer could not create its window.")
                        return
                    }
                    publishedWindow.set(windowHandle)
                    appliedExtent = requested
                } else if (requested != appliedExtent) {
                    DesktopNativeRenderer.nativeResizeWindow(
                        windowHandle,
                        unpackWidth(requested),
                        unpackHeight(requested)
                    )
                    appliedExtent = requested
                }

                if (sessionHandle == 0L) {
                    // A saved plate is decoded from its file; a new one is built from the
                    // allocation the caller named. The decision is made here, once, because
                    // the renderer takes a path or does not and there is no later moment at
                    // which a running world could become a different one.
                    val stored = substratePath
                    sessionHandle = if (stored != null) {
                        DesktopNativeRenderer.nativeCreateSessionFromSubstrate(
                            windowHandle,
                            unpackWidth(appliedExtent),
                            unpackHeight(appliedExtent),
                            maxCellCount,
                            maxFoodCount,
                            dishRadius,
                            shaderRoot,
                            textureRoot,
                            stored
                        )
                    } else {
                        DesktopNativeRenderer.nativeCreateSession(
                            windowHandle,
                            unpackWidth(appliedExtent),
                            unpackHeight(appliedExtent),
                            maxCellCount,
                            maxFoodCount,
                            dishRadius,
                            shaderRoot,
                            textureRoot
                        )
                    }
                    if (sessionHandle == 0L) {
                        failOnce(
                            if (substratePath != null) {
                                "The saved plate could not be opened."
                            } else {
                                "The simulation could not be started."
                            }
                        )
                        return
                    }
                    publishedSession.set(sessionHandle)
                    // Reported from the thread that owns the session, which is the only
                    // thread that may speak to it. Callers treat it as a publication.
                    onSessionReady(sessionHandle)
                }

                val result = DesktopNativeRenderer.nativeRenderFrame(sessionHandle)
                if (result == 2) return
                if (result == 1) {
                    failOnce("The renderer reported a failed frame.")
                    return
                }
                latestCellCount.set(DesktopNativeRenderer.nativeCellCount(sessionHandle))
            }
        } catch (thrown: Throwable) {
            failOnce("The renderer stopped: $thrown")
        } finally {
            // Teardown happens here rather than in release(), because stopping a session is
            // a call like any other and the bridge requires it from the owning thread.
            if (sessionHandle != 0L) {
                DesktopNativeRenderer.nativeStopSession(sessionHandle)
            }
            if (windowHandle != 0L) {
                DesktopNativeRenderer.nativeDestroyWindow(windowHandle)
            }
        }
    }

    private fun apply(sessionHandle: Long, event: SurfaceInputEvent) {
        when (event) {
            is SurfaceInputEvent.Pan ->
                DesktopNativeRenderer.nativeQueuePan(sessionHandle, event.dx, event.dy)

            is SurfaceInputEvent.Zoom ->
                DesktopNativeRenderer.nativeQueueZoom(sessionHandle, event.factor, event.x, event.y)

            is SurfaceInputEvent.Tap ->
                DesktopNativeRenderer.nativeQueueTap(sessionHandle, event.x, event.y)

            is SurfaceInputEvent.Temperature ->
                DesktopNativeRenderer.nativeSetTemperatureMode(sessionHandle, event.mode)

            is SurfaceInputEvent.Tool ->
                DesktopNativeRenderer.nativeSetToolOrdinal(sessionHandle, event.ordinal)

            SurfaceInputEvent.Reset ->
                DesktopNativeRenderer.nativeResetWorld(sessionHandle)
        }
    }

    /**
     * Ends the frame loop and waits for it to finish tearing the session down.
     *
     * Waiting matters: the loop's last act is to stop the session and destroy the window on
     * the thread that owns them, and returning before that finished would let a new surface
     * be built against a renderer still shutting the old one down. The wait is bounded so a
     * renderer that has stopped answering cannot hang the interface that is closing.
     */
    fun release() {
        if (released) return
        released = true
        thread.interrupt()
        try {
            thread.join(RELEASE_WAIT_MILLIS)
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
        }
        canvas = null
    }

    private fun failOnce(message: String) {
        if (reported) return
        reported = true
        onFailure(message)
    }

    private fun sleepFrame() {
        try {
            Thread.sleep(FRAME_WAIT_MILLIS)
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    private companion object {
        const val FRAME_WAIT_MILLIS = 8L
        const val RELEASE_WAIT_MILLIS = 4000L

        /** Both extents in one long, so a reader never sees half of a size change. */
        fun pack(width: Int, height: Int): Long =
            (width.toLong() shl 32) or (height.toLong() and 0xFFFFFFFFL)

        fun unpackWidth(packed: Long): Int = (packed ushr 32).toInt()

        fun unpackHeight(packed: Long): Int = (packed and 0xFFFFFFFFL).toInt()

        /**
         * The AWT peer's native window handle, which is the parent the renderer needs.
         *
         * There is no supported way to get this. `Component.getPeer` was removed in JDK 21,
         * so the package-private `peer` field is the only route left, and reading it needs
         * `--add-opens java.desktop/java.awt=ALL-UNNAMED` on the command line -- see the
         * note in the launcher. `getHWnd` is the accessor AWT itself uses on Windows, and
         * the peer's type lives in a package the module system does not export, so the
         * value can be held as `Any` but its type cannot be named.
         *
         * When the open is missing this returns zero, and the interface reports the renderer
         * as unavailable rather than crashing, which is the right failure for a packaging
         * mistake.
         */
        fun nativeWindowHandleOf(component: Component): Long {
            if (!component.isShowing) return 0L
            return try {
                val peerField = Component::class.java.getDeclaredField("peer")
                peerField.isAccessible = true
                val peer: Any = peerField.get(component) ?: return 0L
                val getHWnd = peer.javaClass.methods.firstOrNull { it.name == "getHWnd" }
                    ?: return 0L
                (getHWnd.invoke(peer) as? Long) ?: 0L
            } catch (_: ReflectiveOperationException) {
                0L
            } catch (_: RuntimeException) {
                0L
            } catch (_: LinkageError) {
                // A missing --add-opens surfaces as an error rather than an exception on
                // some JDKs. Either way the caller gets zero and says so.
                0L
            }
        }
    }
}
