/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asComposeImageBitmap
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeGestureInterpreter
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeInputCommandSink
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopePointer
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopePointerAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopePointerEvent
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.max
import kotlin.math.roundToInt
import kotlin.math.sqrt
import kotlinx.coroutines.CompletableDeferred
import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorSpace
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.ImageInfo
import org.jetbrains.skia.Pixmap

/**
 * The world, drawn inside the interface's own scene.
 *
 * This is the desktop counterpart of the phone's surface, and it exists because of the one
 * thing a second window cannot do. A window is a sibling of the interface's window, not a layer
 * inside it: whatever is drawn into it is either in front of everything the interface paints or
 * behind everything, and Windows will not mix the two. The microscope's controls, its zoom
 * label, the diagnosis panel, the plate menu and every dialog are interface, so with a window
 * of its own the world covers them -- which is what was seen: controls that could not be
 * clicked, tabs that appeared as blank rectangles, and an exit dialog that darkened the screen
 * without ever becoming visible.
 *
 * So the frame is not presented by a window at all. It is asked for as pixels, once per frame,
 * and drawn as one image inside the same scene as the rest of the interface. The world ends up
 * below the interface for the same reason it does on the phone: the interface is painted after
 * it, in one window and one scene graph. Everything that follows from that -- dialogs, panels,
 * the plate menu, the lot -- behaves the way it does on the phone, because there is nothing
 * left in the way.
 *
 * What is kept from the window design is only what the graphics device genuinely needs: a
 * window to create its surface against. It is never shown, never focused and never drawn into;
 * see [DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow]. Input has nothing to do with it
 * either -- gestures arrive through Compose, like every other gesture in the application, and
 * are interpreted by the same shared interpreter the phone feeds.
 *
 * ## Reading the sizes
 *
 * The extent handed to the renderer is the composable's size in *pixels*, because that is the
 * space the simulation projects into and converts gestures through: a tap at a point on screen
 * has to resolve to the same cell here as it does on the phone. Compose reports layout sizes in
 * pixels, so the value goes to the renderer unchanged -- the same rule the phone follows, where
 * the surface reports its own pixel extent.
 */
@Composable
fun MicroscopeRenderSurface(
    assetRoots: DesktopAssetRoots.Roots?,
    maxCellCount: Int,
    maxFoodCount: Int,
    dishRadius: Double,
    temperatureMode: Int,
    toolOrdinal: Int,
    substratePath: String?,
    performanceHudEnabled: Boolean,
    /**
     * The genome a synthesizer tap places, as the editor encodes it, or null when there is no
     * editor to take one from.
     *
     * This is the desktop's half of the phone's placement genome, and it arrives the same way: the
     * editor's current draft is published with the plate, so a cell placed by clicking is the
     * creature the editor is showing rather than the plate's default one.
     */
    placementGenomeBytes: ByteArray?,
    isVisible: Boolean,
    onCellCount: (Long) -> Unit,
    onFailure: (String) -> Unit,
    onSessionReady: (Long) -> Unit,
    /**
     * Where a save reaches the plate's picture, when this surface is the one drawing it.
     *
     * Null in the probes and in any composition that has nothing to save; the capture then answers
     * "no picture" and a save writes a plate without one, which is exactly what it should do when
     * there is no renderer to take a picture from.
     */
    previewCapture: SubstratePreviewCapture? = null,
    modifier: Modifier = Modifier
) {
    if (!DesktopNativeRenderer.isAvailable || assetRoots == null) {
        RendererUnavailable(
            reason = if (!DesktopNativeRenderer.isAvailable) {
                "The renderer library could not be loaded, so the plate cannot be drawn."
            } else {
                "The renderer's shaders and textures were not found, so the plate cannot be drawn."
            },
            modifier = modifier
        )
        return
    }

    val currentOnCellCount by rememberUpdatedState(onCellCount)
    val currentOnFailure by rememberUpdatedState(onFailure)
    val currentOnSessionReady by rememberUpdatedState(onSessionReady)

    // The session outlives recomposition and lives as long as the screen does: it owns the
    // Vulkan device and the simulation's world, so rebuilding it would throw the run away.
    val surface = remember(assetRoots, substratePath) {
        WorldSurface(
            shaderRoot = assetRoots.shaderRoot.toString(),
            textureRoot = assetRoots.textureRoot.toString(),
            maxCellCount = maxCellCount,
            maxFoodCount = maxFoodCount,
            dishRadius = dishRadius,
            substratePath = substratePath,
            onCellCount = { count -> currentOnCellCount(count) },
            onFailure = { message -> currentOnFailure(message) },
            onSessionReady = { handle -> currentOnSessionReady(handle) }
        )
    }

    // The setting is told to the running session rather than remembered for the next one, so
    // switching it on in the settings changes the picture that is already being drawn -- which
    // is what a switch in a settings window is expected to do.
    LaunchedEffect(surface, performanceHudEnabled) {
        surface.setPerformanceHudEnabled(performanceHudEnabled)
    }
    DisposableEffect(surface) {
        surface.start()
        onDispose { surface.close() }
    }

    // The save finds the picture through this surface for as long as the surface is the one
    // drawing: attached here rather than passed as a callback, because a capture is a question
    // with an answer and the answer belongs to whoever asked.
    DisposableEffect(surface, previewCapture) {
        previewCapture?.attach(surface)
        onDispose { previewCapture?.detach(surface) }
    }

    // Everything the render thread has to be told about is left with the surface rather than
    // applied here. The thread that owns the session is the only one allowed to speak to it,
    // and the interface thread must never wait for it -- the calls are cheap, but the rule is
    // what keeps a slow frame from becoming a frozen window.
    LaunchedEffect(surface, isVisible) {
        surface.setPresenting(isVisible)
    }
    LaunchedEffect(surface, temperatureMode) {
        surface.setTemperatureMode(temperatureMode)
    }
    LaunchedEffect(surface, toolOrdinal) {
        surface.setToolOrdinal(toolOrdinal)
    }
    // The genome goes the same way the tool and the mode do: it is a value the surface reads when
    // it needs one, not a message it has to keep in step with.
    LaunchedEffect(surface, placementGenomeBytes) {
        surface.setPlacementGenome(placementGenomeBytes)
    }

    Box(
        modifier = modifier
            .onSizeChanged { size -> surface.setExtent(size) }
            .pointerInput(surface) {
                trackMicroscopeGestures(
                    interpreter = MicroscopeGestureInterpreter(),
                    sink = object : MicroscopeInputCommandSink {
                        override fun touchDown(pointerId: Int, x: Float, y: Float) {
                            surface.queueTouch(pointerId, TOUCH_PHASE_DOWN, x, y)
                        }

                        override fun touchMove(pointerId: Int, x: Float, y: Float) {
                            surface.queueTouch(pointerId, TOUCH_PHASE_MOVE, x, y)
                        }

                        override fun touchUp(pointerId: Int) {
                            surface.queueTouch(pointerId, TOUCH_PHASE_UP, 0f, 0f)
                        }

                        override fun pan(deltaX: Float, deltaY: Float) {
                            surface.queuePan(deltaX, deltaY)
                        }

                        override fun zoom(factor: Float, centerX: Float, centerY: Float) {
                            surface.queueZoom(factor, centerX, centerY)
                        }

                        // The phone sends this to end the zoom cadence a pinch began. A wheel
                        // has no such beginning and no such end -- every notch is a complete
                        // gesture -- so there is nothing here to end.
                        override fun zoomEnded() = Unit

                        override fun tap(x: Float, y: Float) {
                            surface.queueTap(x, y)
                        }

                        override fun fling(velocityX: Float, velocityY: Float) {
                            surface.queueFling(velocityX, velocityY)
                        }
                    },
                    onWheel = { notches, anchor ->
                        surface.queueZoom(
                            factor = if (notches < 0f) WHEEL_ZOOM_STEP
                            else 1f / WHEEL_ZOOM_STEP,
                            centerX = anchor.x,
                            centerY = anchor.y
                        )
                    }
                )
            }
    ) {
        // Drawn rather than composed: the picture is one bitmap that is filled again and again,
        // so a new frame changes a number and nothing else. Reading that number in the drawing
        // phase means a frame invalidates the drawing and not the composition -- sixty times a
        // second, that is the difference between a busy interface and an idle one.
        Canvas(modifier = Modifier.fillMaxSize()) {
            val paintedFrames = surface.frameVersion.value
            val picture = surface.frameBitmap
            if (picture != null && paintedFrames > 0L) {
                drawImage(
                    image = picture,
                    srcOffset = IntOffset.Zero,
                    srcSize = IntSize(picture.width, picture.height),
                    dstOffset = IntOffset.Zero,
                    dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()),
                    // A microscope picture: smooth, and replaced every frame. Nearest neighbour
                    // would show the pixels of a scaled frame, and a high-quality filter would
                    // spend time on a picture that is about to be replaced.
                    filterQuality = FilterQuality.Low
                )
            }
        }
    }
}

/**
 * Translates Compose pointer input into the shared microscope gestures.
 *
 * The interpretation itself is not reimplemented: [MicroscopeGestureInterpreter] is the same
 * object the phone feeds from `MotionEvent`, so one finger pans, two fingers pan and pinch, and
 * a press without movement is a tap here for the same reasons and with the same thresholds as
 * there. What is desktop-specific is only the translation, and the wheel, which has no phone
 * equivalent: a notch zooms about the cursor at the step the standalone host used, so a mouse
 * zooms the way it always has in this application.
 */
private suspend fun PointerInputScope.trackMicroscopeGestures(
    interpreter: MicroscopeGestureInterpreter,
    sink: MicroscopeInputCommandSink,
    onWheel: (notches: Float, anchor: Offset) -> Unit
) {
    awaitPointerEventScope {
        // The pointers the simulation currently holds, in the order they went down.
        val held = LinkedHashMap<PointerId, Offset>()
        var pressOrigin = Offset.Zero
        var travelled = 0f
        var lastX = 0f
        var lastY = 0f
        var lastTimestamp = 0L
        var velocityX = 0f
        var velocityY = 0f

        while (true) {
            val event = awaitPointerEvent()
            val changes = event.changes
            val timestamp = changes.firstOrNull()?.uptimeMillis ?: lastTimestamp

            if (event.type == PointerEventType.Scroll) {
                val scroll = changes.firstOrNull() ?: continue
                val notches = scroll.scrollDelta.y
                if (notches != 0f) {
                    onWheel(notches, scroll.position)
                }
                // Consumed so the wheel cannot also scroll something behind the viewport: over
                // the plate it is a zoom and nothing else.
                changes.forEach { it.consume() }
                continue
            }

            val down = changes.filter { !it.previousPressed && it.pressed }
            val up = changes.filter { it.previousPressed && !it.pressed }
            val stillPressed = changes.filter { it.pressed }
            val action = when {
                down.isNotEmpty() && held.isEmpty() -> MicroscopePointerAction.DOWN
                down.isNotEmpty() -> MicroscopePointerAction.POINTER_DOWN
                up.isNotEmpty() && stillPressed.isEmpty() -> MicroscopePointerAction.UP
                up.isNotEmpty() -> MicroscopePointerAction.POINTER_UP
                else -> MicroscopePointerAction.MOVE
            }
            val actionPointer = when (action) {
                MicroscopePointerAction.DOWN, MicroscopePointerAction.POINTER_DOWN ->
                    down.first().id
                MicroscopePointerAction.UP, MicroscopePointerAction.POINTER_UP ->
                    up.first().id
                else -> stillPressed.firstOrNull()?.id
            }

            // The pointers the simulation is told about are the ones still held plus the one
            // this event releases, which is the set a MotionEvent would have carried.
            val releasedIds = up.map { it.id }.toSet()
            val live = LinkedHashMap<PointerId, Offset>()
            for (change in changes) {
                if (change.pressed || change.id in releasedIds) {
                    live[change.id] = change.position
                }
            }
            held.clear()
            for (change in changes) {
                if (change.pressed) {
                    held[change.id] = change.position
                }
            }

            if (live.isNotEmpty()) {
                interpreter.onPointerEvent(
                    MicroscopePointerEvent(
                        action = action,
                        actionPointerId = actionPointer?.value?.toInt(),
                        pointers = live.map { (id, position) ->
                            MicroscopePointer(id.value.toInt(), position.x, position.y)
                        },
                        timestampMillis = timestamp
                    ),
                    sink
                )
            }

            when (action) {
                MicroscopePointerAction.DOWN -> {
                    val origin = down.first().position
                    pressOrigin = origin
                    travelled = 0f
                    lastX = origin.x
                    lastY = origin.y
                    lastTimestamp = timestamp
                    velocityX = 0f
                    velocityY = 0f
                }

                MicroscopePointerAction.MOVE -> {
                    val change = stillPressed.firstOrNull() ?: continue
                    val position = change.position
                    travelled = max(
                        travelled,
                        sqrt(
                            (position.x - pressOrigin.x) * (position.x - pressOrigin.x) +
                                (position.y - pressOrigin.y) * (position.y - pressOrigin.y)
                        )
                    )
                    val elapsedMillis = (timestamp - lastTimestamp).coerceAtLeast(1L)
                    velocityX = (position.x - lastX) * 1000f / elapsedMillis
                    velocityY = (position.y - lastY) * 1000f / elapsedMillis
                    lastX = position.x
                    lastY = position.y
                    lastTimestamp = timestamp
                }

                MicroscopePointerAction.UP -> {
                    val position = up.first().position
                    // A press that never moved is a click, which is how the standalone host
                    // told them apart as well: a drag does not select a cell on the way.
                    if (travelled <= viewConfiguration.touchSlop) {
                        interpreter.onTap(position.x, position.y, sink)
                    } else if (sqrt(velocityX * velocityX + velocityY * velocityY)
                        >= FLING_MINIMUM_SPEED
                    ) {
                        interpreter.onFling(velocityX, velocityY, timestamp, sink)
                    }
                }

                else -> Unit
            }

            changes.forEach { it.consume() }
        }
    }
}

/** Shown when the renderer cannot be started at all, instead of an empty viewport. */
@Composable
private fun RendererUnavailable(reason: String, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Text(text = reason, modifier = Modifier.padding(24.dp))
    }
}

/**
 * The session, its thread and the frame it last produced.
 *
 * One thread owns everything the renderer holds: the window the device was created against, the
 * backend session and the world's presentation. That is not a preference -- the backend records
 * the thread that created it and refuses work from any other -- so the interface talks to this
 * object and this object talks to the renderer, always from that one thread.
 *
 * The loop is deliberately narrow: apply what the interface asked for, ask for one frame, hand
 * the pixels to the composition, wait out the rest of the frame interval. The world steps on its
 * own thread inside the renderer, so this loop never steps it and a slow frame here can never
 * stall the simulation.
 */
private class WorldSurface(
    private val shaderRoot: String,
    private val textureRoot: String,
    private val maxCellCount: Int,
    private val maxFoodCount: Int,
    private val dishRadius: Double,
    private val substratePath: String?,
    private val onCellCount: (Long) -> Unit,
    private val onFailure: (String) -> Unit,
    private val onSessionReady: (Long) -> Unit
) : SubstratePreviewSource {
    /**
     * How many frames have been drawn into [frameBitmap].
     *
     * The drawing phase reads this and nothing else, which is what keeps a frame from having to
     * allocate anything: the picture is one bitmap that is filled again and again, and a new
     * frame is the same bitmap with a higher number beside it. Watching the bitmap itself would
     * not work -- it is the same object every time, so the composition would have no reason to
     * redraw.
     */
    val frameVersion = mutableStateOf(0L)

    /**
     * The picture the composition draws, or null until the renderer has made one.
     *
     * A plain field rather than state: it changes only when the size does, and the version above
     * is what tells the drawing phase to look at it again. Making it state would add a second
     * reason to redraw for every size change, which is harmless and misleading at once.
     */
    var frameBitmap: ImageBitmap? = null
        private set

    /** The bitmap, its memory and the address the renderer writes to. One per size. */
    private var frameTargetRef: FrameTarget? = null

    private val requestedExtent = AtomicReference(IntSize.Zero)
    private val requestedTemperature = AtomicInteger(NO_REQUEST)
    private val requestedTool = AtomicInteger(NO_REQUEST)
    /** The renderer's performance readout, as the settings' switch last set it. */
    private val requestedPerformanceHud = AtomicReference(false)

    /**
     * The genome a synthesizer tap places, as the editor's draft last encoded it.
     *
     * Held rather than remembered by the session, because it is read when a tap is queued and a
     * tap happens on the interface's clock, not the renderer's: whatever the person was looking at
     * when they clicked is what gets placed. Null means nobody has offered a genome, which is the
     * case a plate with no editor open is in.
     */
    private val placementGenome = AtomicReference<ByteArray?>(null)
    private val presenting = AtomicReference(true)
    private val running = AtomicReference(false)

    /**
     * Pointer work waiting for the thread that owns the session.
     *
     * A gesture arrives on the interface thread, but the session may only be spoken to from its
     * own thread, so the work is handed over rather than performed. It is applied at the top of
     * the next frame, which is also the earliest moment it could have been visible.
     */
    private val pendingInput = ConcurrentLinkedQueue<(Long) -> Unit>()

    /**
     * A plate's preview waiting for the thread that may draw it.
     *
     * Saving does not need this thread: writing the plate goes through the owner queue, which is
     * where the world is. The picture does -- drawing it uses the renderer's one backend session,
     * and the backend records the thread that created it and refuses work from any other -- so the
     * request is left here and taken at the top of a frame, like a gesture, and the answer travels
     * back through the completion the save is already waiting on.
     */
    private class PendingPreviewCapture(
        val token: Long,
        val completion: (ByteArray?) -> Unit
    )

    private val pendingPreviewCaptures = ConcurrentLinkedQueue<PendingPreviewCapture>()

    /** What the renderer was last told about the performance readout. */
    private var appliedPerformanceHud = false

    private var thread: Thread? = null

    fun start() {
        if (!running.compareAndSet(false, true)) return
        val worker = Thread({ run() }, "CellularityWorldRender")
        worker.isDaemon = true
        thread = worker
        worker.start()
    }

    fun close() {
        running.set(false)
        val worker = thread
        thread = null
        worker?.join(SHUTDOWN_TIMEOUT_MILLIS)
    }

    fun setExtent(size: IntSize) {
        requestedExtent.set(size)
    }

    fun setPresenting(visible: Boolean) {
        presenting.set(visible)
    }

    fun setTemperatureMode(mode: Int) {
        requestedTemperature.set(mode)
    }

    fun setToolOrdinal(ordinal: Int) {
        requestedTool.set(ordinal)
    }

    /**
     * Switches the renderer's own performance readout on or off.
     *
     * It is handed to the render thread like every other request, because the session belongs to
     * that thread; it is applied at the top of the next frame, which is soon enough for a switch.
     */
    fun setPerformanceHudEnabled(enabled: Boolean) {
        requestedPerformanceHud.set(enabled)
    }

    fun queuePan(deltaX: Float, deltaY: Float) {
        pendingInput.add { session ->
            DesktopNativeRenderer.nativeQueuePan(session, deltaX, deltaY)
        }
    }

    fun queueZoom(factor: Float, centerX: Float, centerY: Float) {
        pendingInput.add { session ->
            DesktopNativeRenderer.nativeQueueZoom(session, factor, centerX, centerY)
        }
    }

    fun queueTap(x: Float, y: Float) {
        pendingInput.add { session ->
            // The genome is read where the tap is applied rather than where it is queued, so a
            // click always places the draft that was current when it was made -- there is no
            // window in which a stale genome could be used, because the value and the tap travel
            // together.
            val genome = placementGenome.get()
            if (genome == null) {
                DesktopNativeRenderer.nativeQueueTap(session, x, y)
            } else {
                DesktopNativeRenderer.nativeQueueTapWithGenome(session, x, y, genome)
            }
        }
    }

    /**
     * Offers the genome a synthesizer tap should place.
     *
     * Called whenever the editor's draft changes; a null means the editor is gone and taps go back
     * to the plate's own cell.
     */
    fun setPlacementGenome(genomeBytes: ByteArray?) {
        placementGenome.set(genomeBytes)
    }

    fun queueFling(velocityX: Float, velocityY: Float) {
        pendingInput.add { session ->
            DesktopNativeRenderer.nativeQueueFling(session, velocityX, velocityY)
        }
    }

    fun queueTouch(pointerId: Int, phase: Int, x: Float, y: Float) {
        pendingInput.add { session ->
            DesktopNativeRenderer.nativeQueueTouch(session, pointerId, phase, x, y)
        }
    }

    /**
     * Takes the picture of a prepared preview frame, from the thread that owns the renderer.
     *
     * Answers through the completion rather than returning: the interface thread may ask, but only
     * this thread may draw, so the request waits in a queue until the top of the next frame --
     * including while the world is not being presented, because a plate is saved from the panel
     * that describes it as often as from the picture itself, and a save that only worked while the
     * microscope was on screen would be a save that failed for no visible reason.
     */
    override suspend fun captureSubstratePreview(token: Long): ByteArray? {
        if (token == 0L) return null
        val answer = CompletableDeferred<ByteArray?>()
        pendingPreviewCaptures.add(
            PendingPreviewCapture(token) { pixels -> answer.complete(pixels) }
        )
        return answer.await()
    }

    private fun run() {
        var anchor = 0L
        var session = 0L
        var width = 0
        var height = 0
        var consecutiveFailures = 0
        var reportedFailure = false
        var lastCellCountMillis = 0L
        try {
            while (running.get() && session == 0L) {
                val extent = requestedExtent.get()
                if (extent.width <= 0 || extent.height <= 0) {
                    Thread.sleep(IDLE_MILLIS)
                    continue
                }
                anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(
                    extent.width,
                    extent.height
                )
                if (anchor == 0L) {
                    onFailure("The renderer could not create the surface it draws through.")
                    return
                }
                session = createSession(anchor, extent.width, extent.height)
                if (session == 0L) {
                    // A stored plate that will not open is a file the person chose, and the
                    // decoder knows what is wrong with it. Asking is the difference between "the
                    // simulation could not be opened" and a sentence they can act on -- and a
                    // plate from a newer build and a file that is not a plate at all are not the
                    // same problem.
                    val problem = substratePath?.let { path ->
                        DesktopNativeRenderer.nativeDescribeSubstrateProblem(
                            substratePath = path,
                            maxCellCount = maxCellCount,
                            maxFoodCount = maxFoodCount,
                            dishRadius = dishRadius
                        )
                    }
                    onFailure(
                        if (problem == null) {
                            "The renderer could not open the simulation."
                        } else {
                            "This plate could not be opened: $problem"
                        }
                    )
                    DesktopNativeRenderer.nativeDestroyWindow(anchor)
                    return
                }
                width = extent.width
                height = extent.height
                onSessionReady(session)
            }
            if (session == 0L) return

            while (running.get()) {
                val frameStartedNanos = System.nanoTime()
                val extent = requestedExtent.get()
                if (extent.width > 0 && (extent.width != width || extent.height != height)) {
                    // The picture is redrawn at the new size. The simulation is told by the
                    // frame call itself, which applies the extent the projection uses -- so a
                    // tap and the pixels it lands on are always measured in the same space.
                    width = extent.width
                    height = extent.height
                }
                applyInterfaceRequests(session)

                if (!presenting.get()) {
                    // Nothing asks for a frame while another tab is showing, exactly as on the
                    // phone: the presentation stops, the simulation does not. The session, the
                    // device and the world all stay as they are, so coming back costs a frame
                    // rather than a restart.
                    Thread.sleep(PARKED_MILLIS)
                    continue
                }

                when (publish(session, width, height)) {
                    FRAME_DRAWN -> {
                        consecutiveFailures = 0
                        reportedFailure = false
                    }

                    FRAME_IDLE -> Thread.sleep(IDLE_MILLIS)

                    else -> {
                        consecutiveFailures++
                        if (!reportedFailure) {
                            onFailure("The renderer stopped drawing the plate.")
                            reportedFailure = true
                        }
                        if (consecutiveFailures >= FAILURE_PARK_THRESHOLD) {
                            Thread.sleep(FAILURE_PARK_MILLIS)
                        }
                    }
                }

                val now = System.currentTimeMillis()
                if (now - lastCellCountMillis >= CELL_COUNT_INTERVAL_MILLIS) {
                    lastCellCountMillis = now
                    onCellCount(DesktopNativeRenderer.nativeCellCount(session))
                }

                val elapsed = System.nanoTime() - frameStartedNanos
                val remaining = FRAME_INTERVAL_NANOS - elapsed
                if (remaining > 0L) {
                    Thread.sleep(remaining / 1_000_000L, (remaining % 1_000_000L).toInt())
                }
            }
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
        } catch (failure: Throwable) {
            onFailure("The plate could not be drawn: ${failure.message ?: failure::class.simpleName}.")
        } finally {
            while (pendingInput.poll() != null) {
                // Deliberately dropped: the session these would have been applied to is going
                // away, and applying input to a stopped session is worse than losing it.
            }
            var abandonedCapture = pendingPreviewCaptures.poll()
            while (abandonedCapture != null) {
                // Answered rather than dropped: a save is waiting on this, and a save that waits
                // forever because the picture went away with the session would leave the plate
                // unwritten and the person with no answer at all. No picture, plate written.
                abandonedCapture.completion(null)
                abandonedCapture = pendingPreviewCaptures.poll()
            }
            frameTargetRef?.close()
            // The picture is freed with the session that drew into it: the address the renderer
            // holds is the memory of this bitmap, and leaving it alive after the session stops would
            // be a leak of exactly the size of the window.
            if (session != 0L) {
                runCatching { DesktopNativeRenderer.nativeStopSession(session) }
            }
            if (anchor != 0L) {
                runCatching { DesktopNativeRenderer.nativeDestroyWindow(anchor) }
            }
        }
    }

    private fun createSession(anchor: Long, width: Int, height: Int): Long =
        if (substratePath == null) {
            DesktopNativeRenderer.nativeCreateSession(
                windowHandle = anchor,
                width = width,
                height = height,
                maxCellCount = maxCellCount,
                maxFoodCount = maxFoodCount,
                dishRadius = dishRadius,
                shaderRoot = shaderRoot,
                textureRoot = textureRoot
            )
        } else {
            DesktopNativeRenderer.nativeCreateSessionFromSubstrate(
                windowHandle = anchor,
                width = width,
                height = height,
                maxCellCount = maxCellCount,
                maxFoodCount = maxFoodCount,
                dishRadius = dishRadius,
                shaderRoot = shaderRoot,
                textureRoot = textureRoot,
                substratePath = substratePath
            )
        }

    /** Applies whatever the interface has changed since the last frame. */
    private fun applyInterfaceRequests(session: Long) {
        val mode = requestedTemperature.getAndSet(NO_REQUEST)
        if (mode != NO_REQUEST) {
            DesktopNativeRenderer.nativeSetTemperatureMode(session, mode)
        }
        val ordinal = requestedTool.getAndSet(NO_REQUEST)
        if (ordinal != NO_REQUEST) {
            DesktopNativeRenderer.nativeSetToolOrdinal(session, ordinal)
        }
        val hud = requestedPerformanceHud.get()
        if (hud != appliedPerformanceHud) {
            appliedPerformanceHud = hud
            DesktopNativeRenderer.nativeSetPerformanceHudEnabled(session, hud)
        }
        var action = pendingInput.poll()
        while (action != null) {
            action(session)
            action = pendingInput.poll()
        }
        var capture = pendingPreviewCaptures.poll()
        while (capture != null) {
            capture.completion(drawSubstratePreview(session, capture.token))
            capture = pendingPreviewCaptures.poll()
        }
    }

    /**
     * Draws the prepared preview frame [token] names and answers with its pixels, or null.
     *
     * A fresh direct buffer per capture rather than a reused one: this happens once per save, not
     * once per frame, and a direct buffer is what the capture call takes without an extra copy. The
     * picture is the format's own square, so the caller does not have to know its size.
     *
     * Null means there was no picture to take -- the renderer refused the frame, or the token had
     * already been used. A plate without a picture is a complete plate, so that is an answer rather
     * than a failure, and the save goes on to write one.
     */
    private fun drawSubstratePreview(session: Long, token: Long): ByteArray? {
        val side = DesktopNativeRenderer.nativeSubstratePreviewPixels()
        if (side <= 0) return null
        val rowStride = side * PREVIEW_BYTES_PER_PIXEL
        val pixels = ByteBuffer.allocateDirect(rowStride * side)
        val status = DesktopNativeRenderer.nativeCaptureSubstratePreview(
            sessionHandle = session,
            token = token,
            pixels = pixels,
            rowStride = rowStride
        )
        if (status != STATUS_OK) return null
        val bytes = ByteArray(rowStride * side)
        pixels.rewind()
        pixels.get(bytes)
        return bytes
    }

    /**
     * Asks for one frame and, when there is one, makes the composition show it.
     *
     * The readback lands directly in the pixels of the very bitmap the composition draws: the
     * renderer is given the address of that bitmap's memory rather than a buffer of ours, so a
     * frame costs one copy fewer than it used to -- no, it costs the readback and nothing else.
     * The earlier shape of this method made a fresh image per frame out of a byte array, which
     * meant a twelve-megabyte allocation and two full copies sixty times a second, and a native
     * bitmap per frame that only a finaliser would ever release. That is what a task manager
     * shows as memory that only grows while the plate is on screen, and what makes the interface
     * feel slow while the simulation itself is perfectly healthy: the simulation never touched
     * those bytes.
     *
     * Nothing is allocated per frame now. The bitmap is made once per size, the address of its
     * pixels is looked up once, and a frame changes only the version the drawing phase watches.
     */
    private fun publish(session: Long, width: Int, height: Int): Int {
        if (width <= 0 || height <= 0) return FRAME_IDLE
        val target = frameTarget(width, height) ?: return FRAME_IDLE
        val status = DesktopNativeRenderer.nativeRenderFrameIntoAddress(
            sessionHandle = session,
            address = target.address,
            width = width,
            height = height,
            rowStride = target.rowStride
        )
        if (status != FRAME_DRAWN) return status
        // The pixels were written outside Skia, so Skia has to be told: without this it keeps
        // whatever the bitmap's generation counter last described, which is a picture the
        // interface would go on showing while the world moved on.
        target.bitmap.notifyPixelsChanged()
        frameVersion.value = frameVersion.value + 1L
        return FRAME_DRAWN
    }

    /**
     * The bitmap the world is drawn into, made once per size.
     *
     * Rendered at the size the renderer is asked to draw at, which is the composable's own pixel
     * size: the picture is then never scaled on its way to the screen, and a gesture resolved
     * against it lands on the cell it points at.
     */
    private fun frameTarget(width: Int, height: Int): FrameTarget? {
        val existing = frameTargetRef
        if (existing != null && existing.bitmap.width == width && existing.bitmap.height == height) {
            return existing
        }
        val previous = frameTargetRef
        val rowStride = width * BYTES_PER_PIXEL
        val created = try {
            val bitmap = Bitmap()
            val allocated = bitmap.allocPixels(
                ImageInfo(
                    width,
                    height,
                    ColorType.RGBA_8888,
                    ColorAlphaType.PREMUL,
                    ColorSpace.sRGB
                ),
                rowStride
            )
            if (!allocated) {
                bitmap.close()
                onFailure("The renderer's picture could not be allocated at ${width}x$height.")
                return null
            }
            val pixmap = bitmap.peekPixels()
            if (pixmap == null) {
                bitmap.close()
                onFailure("The renderer's picture has no memory to draw into.")
                return null
            }
            val address = pixmap.addr
            if (address == 0L) {
                pixmap.close()
                bitmap.close()
                onFailure("The renderer's picture has no memory to draw into.")
                return null
            }
            val built = FrameTarget(bitmap, pixmap, address, rowStride)
            frameBitmap = bitmap.asComposeImageBitmap()
            built
        } catch (failure: Throwable) {
            onFailure(
                "The renderer's picture could not be made: " +
                    (failure.message ?: failure::class.simpleName)
            )
            return null
        }
        frameTargetRef = created
        previous?.close()
        return created
    }

    /**
     * One bitmap, the pixmap that exposes its memory, and the address the renderer writes to.
     *
     * Held together because they have one lifetime: the address is only meaningful while the
     * bitmap owns the pixels it points at, and freeing one without the other is the kind of
     * mistake that shows up as tearing in a picture rather than as an exception.
     */
    private class FrameTarget(
        val bitmap: Bitmap,
        private val pixmap: Pixmap,
        val address: Long,
        val rowStride: Int
    ) {
        fun close() {
            pixmap.close()
            bitmap.close()
        }
    }

    private companion object {
        const val BYTES_PER_PIXEL = 4

        /** A plate's preview is the same four bytes a pixel, and one picture per save. */
        const val PREVIEW_BYTES_PER_PIXEL = 4

        /** The capture call's own status: the picture was drawn. */
        const val STATUS_OK = 0

        const val NO_REQUEST = Int.MIN_VALUE

        /** The frame interval the loop aims at: sixty frames a second, the phone's ceiling. */
        const val FRAME_INTERVAL_NANOS = 16_666_667L

        /** How long to wait when there is nothing to draw, or the plate is not on screen. */
        const val IDLE_MILLIS = 4L
        const val PARKED_MILLIS = 50L

        /** How many refused frames in a row before the loop stops asking every interval. */
        const val FAILURE_PARK_THRESHOLD = 3
        const val FAILURE_PARK_MILLIS = 500L

        const val CELL_COUNT_INTERVAL_MILLIS = 500L
        const val SHUTDOWN_TIMEOUT_MILLIS = 5_000L
    }
}

/** A frame was drawn. The renderer's own status values. */
private const val FRAME_DRAWN = 0

/** There was nothing to draw yet, which is an idle state and not a fault. */
private const val FRAME_IDLE = 3

/** One touch phase, as [DesktopNativeRenderer.nativeQueueTouch] takes it. */
private const val TOUCH_PHASE_DOWN = 0
private const val TOUCH_PHASE_MOVE = 1
private const val TOUCH_PHASE_UP = 2

/** A wheel notch zooms by this factor, the step the standalone host used. */
private const val WHEEL_ZOOM_STEP = 1.1f

/** Below this speed a release is a click rather than a throw. The phone's own threshold. */
private const val FLING_MINIMUM_SPEED = 50f
