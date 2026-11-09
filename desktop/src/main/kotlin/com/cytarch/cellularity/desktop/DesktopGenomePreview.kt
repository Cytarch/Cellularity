/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asComposeImageBitmap
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorLayoutMemory
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewHandle
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewInteraction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewMetrics
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewState
import com.cytarch.cellularity.shared.ui.play.genome_editor.sections.GenomePreviewSurfaceGeometry
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.roundToInt
import org.jetbrains.skia.Bitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorSpace
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.ImageInfo
import org.jetbrains.skia.Pixmap

/**
 * The organism a genome grows into, drawn beside the controls that describe it.
 *
 * The editor is otherwise a list of numbers, and a list of numbers cannot tell a person whether
 * the creature they are building swims, splits or starves. The picture comes from the renderer
 * rather than from this file, and it is the phone's interactive preview rather than a photograph
 * of one: the genome is published as content, the view is published as a view, the person's
 * gestures are handed over, and the frame that comes back is drawn through the camera those
 * gestures moved. What is desktop about it is only where the pixels go -- into a bitmap inside
 * this scene, because a window of its own cannot be a layer of it.
 *
 * The session lives on a thread of its own, for the reason the plate's does: growing an organism
 * through a day of simulated time is work, and a keystroke in a slider must not wait for it. What
 * crosses between the two is small and one-way in each direction -- requests go over, a frame
 * version and the preview's own account of itself come back.
 */
@Composable
fun DesktopGenomePreview(
    preview: GenomePreviewState,
    layout: GenomeEditorLayoutMemory,
    onGeneSelected: (Int) -> Unit,
    handle: GenomePreviewHandle,
    metrics: GenomePreviewMetrics,
    geometry: GenomePreviewSurfaceGeometry,
    modifier: Modifier = Modifier
) {
    val roots = remember { DesktopAssetRoots.resolve() }
    // The camera the person left behind, read once: it is where the next session opens, not a
    // value to keep watching.
    val surface = remember(roots) {
        roots?.let { PreviewSurface(it, layout.restoreViewport()) }
    }

    if (!DesktopNativeRenderer.isAvailable || surface == null) {
        Box(modifier = modifier, contentAlignment = Alignment.Center) {
            Text(
                text = if (!DesktopNativeRenderer.isAvailable) {
                    "The renderer library could not be loaded, so the organism cannot be drawn."
                } else {
                    "The renderer's shaders and textures were not found, so the organism cannot be drawn."
                },
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(24.dp)
            )
        }
        return
    }

    val currentOnGeneSelected = rememberUpdatedState(onGeneSelected)

    DisposableEffect(surface) {
        surface.start()
        handle.interaction = surface.interaction
        onDispose {
            if (handle.interaction === surface.interaction) handle.interaction = null
            surface.close()
        }
    }

    // The extent handed over is the panel's size in pixels, because that is the space the preview
    // projects into and converts a tap through. Compose reports layout sizes in pixels, so the
    // value goes to the renderer unchanged -- the same rule the plate's viewport follows.
    val initialExtent = remember(geometry.viewportWidth, geometry.viewportHeight) {
        IntSize(geometry.viewportWidth, geometry.viewportHeight)
    }
    LaunchedEffect(surface, initialExtent) { surface.setExtent(initialExtent) }
    LaunchedEffect(surface, preview) { surface.update(preview) }

    // What the render thread found out is applied here, on the thread that owns the interface:
    // the Cell count is shown by the editor, a tap that chose a gene becomes a selection in it,
    // and where the camera ended up is remembered for the next visit. This is the same division
    // the phone has, where the view posts its acknowledgments to the main thread.
    LaunchedEffect(surface) {
        snapshotFlow { surface.publication.value }.collect { published ->
            metrics.cellCount = published.cellCount
            metrics.maximumCellCount = published.maximumCellCount
            published.viewport?.let(layout::rememberViewport)
            published.selectedGeneIndex?.let { currentOnGeneSelected.value(it) }
        }
    }

    // A picture the panel has outgrown is freed here rather than on the thread that outgrew it:
    // the drawing happens on this thread, so this is the only thread that can know a frame is not
    // being drawn from it at this moment.
    LaunchedEffect(surface) {
        snapshotFlow { surface.retiredPictureCount.value }.collect {
            surface.freeRetiredPictures()
        }
    }

    Box(modifier = modifier.onSizeChanged { surface.setExtent(it) }, contentAlignment = Alignment.Center) {
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(surface) {
                    // A wheel notch zooms about the cursor, which is the desktop's answer to the
                    // pinch the phone uses: the editor's gestures have no such thing because a
                    // mouse has no second finger.
                    awaitPointerEventScope {
                        while (true) {
                            val event = awaitPointerEvent()
                            if (event.type != PointerEventType.Scroll) continue
                            val change = event.changes.firstOrNull() ?: continue
                            val notches = change.scrollDelta.y
                            if (notches == 0f) continue
                            surface.interaction.handleZoom(
                                factor = if (notches < 0f) WHEEL_ZOOM_STEP
                                else 1f / WHEEL_ZOOM_STEP,
                                x = change.position.x,
                                y = change.position.y
                            )
                            change.consume()
                        }
                    }
                }
        ) {
            // Drawn rather than composed: the picture is one bitmap that is filled again and
            // again, so a frame changes a number and nothing else. Reading that number in the
            // drawing phase means a frame invalidates the drawing and not the composition.
            val paintedFrames = surface.frameVersion.value
            val picture = surface.frameBitmap
            if (picture != null && paintedFrames > 0L) {
                drawImage(
                    image = picture,
                    srcOffset = IntOffset.Zero,
                    srcSize = IntSize(picture.width, picture.height),
                    dstOffset = IntOffset.Zero,
                    dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()),
                    // One pixel of the picture for one pixel of the panel: the renderer drew at
                    // this size, so the filter has nothing to interpolate and the frame costs
                    // nothing to show.
                    filterQuality = FilterQuality.Low
                )
            }
        }
        val message = surface.problem.value
            ?: if (surface.publication.value.nothingLiving) NOTHING_LIVING_MESSAGE else null
        if (message != null) {
            Text(text = message, style = MaterialTheme.typography.bodyMedium)
        }
    }
}

/**
 * The preview session, its thread, and the frame it last drew.
 *
 * One thread owns everything the renderer holds -- the session, the backend that draws into
 * memory, and the world the genome grows in -- because the backend records the thread that created
 * it and refuses work from any other. The interface talks to this object; this object talks to the
 * renderer, always from that one thread.
 *
 * Requests are of two kinds and are carried differently on purpose. What is published is a
 * *state*: the newest genome and the newest view are the ones that matter, so a slider dragged
 * across a hundred pixels publishes once for the frame that follows rather than a hundred times.
 * What the person *did* is a sequence: a pan of a hundred steps is a hundred movements that add
 * up, and dropping the ones in between would move the camera somewhere nobody put it. So gestures
 * are queued and states are replaced.
 */
private class PreviewSurface(
    private val roots: DesktopAssetRoots.Roots,
    private val initialCamera: FloatArray?
) {
    /** How many frames have been drawn into [frameBitmap], read by the drawing phase. */
    val frameVersion = mutableLongStateOf(0L)

    /**
     * The picture the composition draws, or null until the renderer has made one.
     *
     * A plain field rather than state: it changes only when the size does, and the version above is
     * what tells the drawing phase to look at it again.
     */
    var frameBitmap: ImageBitmap? = null
        private set

    /** What the preview says about itself: the Cells it grew, the gene a tap chose, its camera. */
    val publication = mutableStateOf(PreviewPublication.EMPTY)

    /** Why there is no picture, when there is none. Null while the preview is working. */
    val problem = mutableStateOf<String?>(null)

    /**
     * How many pictures have been retired and are waiting to be freed by the interface thread.
     *
     * A size change makes a new bitmap and retires the old one, and the old one is still what the
     * composition drew a moment ago. Skia's bitmap owns its pixels, so freeing it while a frame is
     * being drawn from it is a use-after-free; the count is what tells the interface thread that
     * there is something to free, on the only thread that can know no frame is in flight.
     */
    val retiredPictureCount = mutableLongStateOf(0L)

    val interaction: GenomePreviewInteraction = object : GenomePreviewInteraction {
        override fun handleTap(x: Float, y: Float) {
            queueInteraction(INTERACTION_SELECT_GENE, x, y, 0f)
        }

        override fun handleMove(dx: Float, dy: Float) {
            queueInteraction(INTERACTION_MOVE_VIEWPORT, dx, dy, 0f)
        }

        override fun handleZoom(factor: Float, x: Float, y: Float) {
            queueInteraction(INTERACTION_ZOOM_VIEWPORT, factor, x, y)
        }

        override fun captureGhost() {
            queueInteraction(INTERACTION_CAPTURE_GHOST, 0f, 0f, 0f)
        }

        override fun clearGhost() {
            queueInteraction(INTERACTION_CLEAR_GHOST, 0f, 0f, 0f)
        }
    }

    private val requestedExtent = AtomicReference(IntSize.Zero)
    private val requestedContent = AtomicReference<ContentRequest?>(null)
    private val requestedView = AtomicReference<ViewRequest?>(null)
    private val pendingGestures = ConcurrentLinkedQueue<(Long) -> Unit>()
    private val retiredPictures = ConcurrentLinkedQueue<FrameTarget>()
    private val frameRequested = AtomicBoolean(false)
    private val running = AtomicBoolean(false)

    private var appliedContent: ContentRequest? = null
    private var appliedView: ViewRequest? = null
    private var appliedExtent = IntSize.Zero
    private var appliedCellCountRevision = 0L
    private var appliedSelectionToken = 0L
    private var appliedViewportRevision = 0L
    private var frameTargetRef: FrameTarget? = null
    private var thread: Thread? = null

    fun start() {
        if (!running.compareAndSet(false, true)) return
        val worker = Thread({ run() }, "CellularityGenomePreview")
        worker.isDaemon = true
        thread = worker
        worker.start()
    }

    fun close() {
        running.set(false)
        val worker = thread
        thread = null
        worker?.join(SHUTDOWN_TIMEOUT_MILLIS)
        // The last picture is freed by whoever closed the preview, which is the thread that draws:
        // the session that wrote into it is already stopped, so nothing can write again.
        freeRetiredPictures()
    }

    fun setExtent(size: IntSize) {
        if (size.width <= 0 || size.height <= 0) return
        requestedExtent.set(size)
    }

    /**
     * Frees the pictures the panel has outgrown. Called from the thread that draws, never from the
     * thread that made them.
     */
    fun freeRetiredPictures() {
        while (true) {
            val retired = retiredPictures.poll() ?: return
            retired.close()
        }
    }

    /**
     * Hands over what the editor is showing: the genome as content, the selection, grid and tree
     * as a view. Both are remembered rather than acted on, because the session may only be spoken
     * to from its own thread.
     */
    fun update(preview: GenomePreviewState) {
        requestedContent.set(
            ContentRequest(
                snapshot = preview.snapshot,
                timeHours = preview.timeHours,
                revision = preview.revision
            )
        )
        requestedView.set(
            ViewRequest(
                selectedGene = preview.selectedGene.value,
                gridEnabled = preview.gridEnabled,
                treeEnabled = preview.treeEnabled
            )
        )
    }

    private fun queueInteraction(kind: Int, first: Float, second: Float, third: Float) {
        pendingGestures.add { session ->
            val status = DesktopNativeRenderer.nativeEnqueueGenomePreviewInteraction(
                sessionHandle = session,
                kind = kind,
                first = first,
                second = second,
                third = third
            )
            if (status == INTERACTION_ACCEPTED) frameRequested.set(true)
        }
    }

    private fun run() {
        var anchor = 0L
        var session = 0L
        try {
            anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(
                ANCHOR_EXTENT, ANCHOR_EXTENT
            )
            if (anchor == 0L) {
                problem.value = "The renderer could not create the surface the preview draws through."
                return
            }
            session = DesktopNativeRenderer.nativeCreateGenomePreviewSession(
                windowHandle = anchor,
                shaderRoot = roots.shaderRoot.toString(),
                textureRoot = roots.textureRoot.toString()
            )
            if (session == 0L) {
                problem.value = "The renderer could not be opened for the preview."
                return
            }
            // Where the person left the camera, restored before the first frame is drawn and
            // through the same road a gesture takes, so it cannot arrive by a private route that
            // the languages disagree about.
            initialCamera?.let { camera ->
                queueInteraction(
                    INTERACTION_RESTORE_VIEWPORT,
                    camera[0],
                    camera[1],
                    camera[2]
                )
            }
            while (running.get()) {
                applyGestures(session)
                publishWhatChanged(session)
                val extent = appliedExtent
                if (frameRequested.getAndSet(false) && extent.width > 0 && extent.height > 0) {
                    draw(session, extent)
                }
                Thread.sleep(IDLE_MILLIS)
            }
        } catch (interrupted: InterruptedException) {
            Thread.currentThread().interrupt()
        } catch (failure: Throwable) {
            problem.value = "The preview could not be drawn: " +
                (failure.message ?: failure::class.simpleName) + "."
        } finally {
            while (pendingGestures.poll() != null) {
                // Deliberately dropped: the session these would have been applied to is going
                // away, and a gesture applied to a stopped session is worse than a lost one.
            }
            // The picture is retired with the session that drew into it: the address the renderer
            // held is the memory of this bitmap, and leaving it alive after the session stops
            // would be a leak of exactly the size of the panel.
            frameTargetRef?.let { retiredPictures.add(it) }
            frameTargetRef = null
            if (session != 0L) {
                runCatching { DesktopNativeRenderer.nativeStopSession(session) }
            }
            if (anchor != 0L) {
                runCatching { DesktopNativeRenderer.nativeDestroyWindow(anchor) }
            }
        }
    }

    /** Applies every gesture that arrived, in the order it arrived. */
    private fun applyGestures(session: Long) {
        var gesture = pendingGestures.poll()
        while (gesture != null) {
            gesture(session)
            gesture = pendingGestures.poll()
        }
    }

    /**
     * Publishes the newest content and the newest view, and nothing when neither has moved.
     *
     * The comparison is against what this thread last published rather than against what the
     * editor holds, because a message the endpoint refused -- an exhausted budget, a closing
     * session -- has not been published whatever the editor believes.
     */
    private fun publishWhatChanged(session: Long) {
        val extent = requestedExtent.get()
        val content = requestedContent.get()
        if (content != null && content != appliedContent) {
            val status = DesktopNativeRenderer.nativePublishGenomePreviewContent(
                sessionHandle = session,
                genomeBytes = GenomeDraftWireCodec.encode(content.snapshot),
                simulationTimeHours = content.timeHours
            )
            if (status == PUBLISH_ACCEPTED) {
                appliedContent = content
                frameRequested.set(true)
            }
        }
        val view = requestedView.get()
        val extentChanged = extent != appliedExtent
        if (extent.width > 0 && extent.height > 0
            && (extentChanged || (view != null && view != appliedView))
        ) {
            val published = view ?: appliedView ?: return
            val status = DesktopNativeRenderer.nativePublishGenomePreviewViewState(
                sessionHandle = session,
                selectedGeneIndex = published.selectedGene,
                gridEnabled = published.gridEnabled,
                treeEnabled = published.treeEnabled,
                viewportWidth = extent.width,
                viewportHeight = extent.height
            )
            if (status == PUBLISH_ACCEPTED) {
                appliedView = published
                appliedExtent = extent
                frameRequested.set(true)
            }
        }
    }

    /**
     * Asks for one frame and, when there is one, makes the composition show it.
     *
     * The readback lands directly in the pixels of the bitmap the composition draws: the renderer
     * is given that bitmap's address rather than a buffer of ours, so a frame costs the readback
     * and nothing else.
     */
    private fun draw(session: Long, extent: IntSize) {
        val target = frameTarget(extent) ?: return
        val status = DesktopNativeRenderer.nativeDrawGenomePreviewIntoAddress(
            sessionHandle = session,
            address = target.address,
            rowStride = target.rowStride
        )
        if (status != PREVIEW_DRAWN) {
            // Nothing to draw is an idle state and not a fault: the frame already on screen stays,
            // and the next request draws when the preview is ready.
            return
        }
        target.bitmap.notifyPixelsChanged()
        frameVersion.value = frameVersion.value + 1L
        problem.value = null
        readStatus(session)
    }

    /**
     * Reads what the preview says about itself, and republishes only what has moved.
     *
     * Each part of the account carries the revision it was true at, which is what keeps a Cell
     * count that has not changed from being written to the interface sixty times a second, and
     * what stops a camera from being remembered twice for one movement.
     */
    private fun readStatus(session: Long) {
        val status = DesktopNativeRenderer.nativeReadGenomePreviewStatus(session) ?: return
        if (status.size < STATUS_LENGTH) return
        val cellCount = status[0].toInt()
        val cellCountRevision = status[1]
        val maximumCellCount = status[2].toInt()
        val selectionToken = status[3]
        val selectedGeneIndex = status[4].toInt()
        val viewportRevision = status[5]
        val viewport = floatArrayOf(
            Float.fromBits(status[6].toInt()),
            Float.fromBits(status[7].toInt()),
            Float.fromBits(status[8].toInt())
        )

        val counted = cellCountRevision != appliedCellCountRevision
        val chose = selectionToken != 0L && selectionToken != appliedSelectionToken
        val moved = viewportRevision != appliedViewportRevision
        if (!counted && !chose && !moved) return

        appliedCellCountRevision = cellCountRevision
        appliedSelectionToken = selectionToken
        appliedViewportRevision = viewportRevision

        val previous = publication.value
        publication.value = previous.copy(
            cellCount = if (counted) cellCount else previous.cellCount,
            maximumCellCount = maximumCellCount,
            nothingLiving = counted && cellCount == 0,
            selectedGeneIndex = if (chose && selectedGeneIndex >= 0) selectedGeneIndex else null,
            viewport = if (moved) viewport else previous.viewport
        )
    }

    /**
     * The bitmap the preview is drawn into, made once per size.
     *
     * Rendered at the size the renderer is asked to draw at, which is the panel's own pixel size:
     * the picture is then never scaled on its way to the screen, and a gesture resolved against it
     * lands on the cell it points at.
     */
    private fun frameTarget(extent: IntSize): FrameTarget? {
        val existing = frameTargetRef
        if (existing != null && existing.bitmap.width == extent.width
            && existing.bitmap.height == extent.height
        ) {
            return existing
        }
        val previous = frameTargetRef
        frameTargetRef = null
        val rowStride = extent.width * BYTES_PER_PIXEL
        val created = try {
            val bitmap = Bitmap()
            val allocated = bitmap.allocPixels(
                ImageInfo(
                    extent.width,
                    extent.height,
                    ColorType.RGBA_8888,
                    ColorAlphaType.PREMUL,
                    ColorSpace.sRGB
                ),
                rowStride
            )
            if (!allocated) {
                bitmap.close()
                problem.value = "The preview's picture could not be allocated at " +
                    "${extent.width}x${extent.height}."
                return null
            }
            val pixmap = bitmap.peekPixels()
            if (pixmap == null) {
                bitmap.close()
                problem.value = "The preview's picture has no memory to draw into."
                return null
            }
            val address = pixmap.addr
            if (address == 0L) {
                pixmap.close()
                bitmap.close()
                problem.value = "The preview's picture has no memory to draw into."
                return null
            }
            frameBitmap = bitmap.asComposeImageBitmap()
            FrameTarget(bitmap, pixmap, address, rowStride)
        } catch (failure: Throwable) {
            problem.value = "The preview's picture could not be made: " +
                (failure.message ?: failure::class.simpleName)
            return null
        }
        frameTargetRef = created
        // Retired rather than freed: the composition may be drawing a frame from the old picture
        // at this moment, and the interface thread frees it when it next looks.
        if (previous != null) {
            retiredPictures.add(previous)
            retiredPictureCount.value = retiredPictureCount.value + 1L
        }
        return created
    }

    /**
     * One bitmap, the pixmap that exposes its memory, and the address the renderer writes to.
     *
     * Held together because they have one lifetime: the address only means anything while the
     * bitmap owns the pixels it points at.
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
        const val IDLE_MILLIS = 4L
        const val SHUTDOWN_TIMEOUT_MILLIS = 5_000L
    }
}

/** The newest genome the editor is showing, and the time it is grown for. */
private class ContentRequest(
    val snapshot: GenomeDraft,
    val timeHours: Double,
    val revision: Long
) {
    override fun equals(other: Any?): Boolean = other is ContentRequest &&
        revision == other.revision &&
        timeHours == other.timeHours &&
        snapshot === other.snapshot

    override fun hashCode(): Int = revision.hashCode()
}

/** The newest view: which gene is selected, and whether the grid and the tree are shown. */
private class ViewRequest(
    val selectedGene: Int,
    val gridEnabled: Boolean,
    val treeEnabled: Boolean
) {
    override fun equals(other: Any?): Boolean = other is ViewRequest &&
        selectedGene == other.selectedGene &&
        gridEnabled == other.gridEnabled &&
        treeEnabled == other.treeEnabled

    override fun hashCode(): Int =
        (selectedGene * 31 + gridEnabled.hashCode()) * 31 + treeEnabled.hashCode()
}

/**
 * What the preview says about itself, as the interface needs it.
 *
 * The camera and the selected gene are null until there is one, which is what keeps an editor from
 * dispatching a selection nobody made: the preview starts with no gene chosen and no viewport
 * revision at all.
 */
private data class PreviewPublication(
    val cellCount: Int = 0,
    val maximumCellCount: Int = 0,
    val nothingLiving: Boolean = false,
    val selectedGeneIndex: Int? = null,
    val viewport: FloatArray? = null
) {
    companion object {
        val EMPTY = PreviewPublication()
    }
}

private const val BYTES_PER_PIXEL = 4

/** A preview is drawn into memory, so its anchor only has to be legal for the backend. */
private const val ANCHOR_EXTENT = 16

/** The preview's own report is nine numbers; a shorter one is not an answer. */
private const val STATUS_LENGTH = 9

/** A wheel notch zooms by this factor, the step the plate's own viewport uses. */
private const val WHEEL_ZOOM_STEP = 1.1f

/** What the renderer answers when a frame was drawn, and when there was nothing to draw. */
private const val PREVIEW_DRAWN = 0

/** What the preview's endpoint answers when it took a message. */
private const val PUBLISH_ACCEPTED = 0
private const val INTERACTION_ACCEPTED = 0

/** Interaction kinds, numbered as the phone numbers them. */
private const val INTERACTION_RESTORE_VIEWPORT = 0
private const val INTERACTION_MOVE_VIEWPORT = 1
private const val INTERACTION_ZOOM_VIEWPORT = 2
private const val INTERACTION_SELECT_GENE = 3
private const val INTERACTION_CAPTURE_GHOST = 4
private const val INTERACTION_CLEAR_GHOST = 5

/** What the editor says when a genome grew into nothing, which is an answer about the genome. */
private const val NOTHING_LIVING_MESSAGE = "This genome grows into nothing living yet."
