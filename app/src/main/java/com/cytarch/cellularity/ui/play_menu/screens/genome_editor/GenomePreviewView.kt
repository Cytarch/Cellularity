package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import android.content.Context
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.findViewTreeLifecycleOwner
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.RenderFrameResultCodec
import com.cytarch.cellularity.platform.render.preview.GenomePreviewCaptureEndpoint
import com.cytarch.cellularity.platform.render.preview.GenomePreviewAcknowledgment
import com.cytarch.cellularity.platform.render.preview.GenomePreviewAcknowledgmentLayout
import com.cytarch.cellularity.platform.render.preview.GenomePreviewPublishResultLayout
import com.cytarch.cellularity.platform.render.preview.NativeGenomePreviewSession
import com.cytarch.cellularity.platform.render.preview.NativePreviewCaptureRequest
import com.cytarch.cellularity.platform.render.preview.OnDemandRenderLifecycle
import com.cytarch.cellularity.platform.render.preview.OnDemandRenderSurfaceHostFactory
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanRenderWorkerBridge
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanWorkerStatus
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewState

class GenomePreviewView(
    context: Context,
    private val previewMaxCells: Int,
    private val backendKind: RenderBackendKind = RenderBackendKind.OPEN_GL_ES,
    onRenderBackendFailure: (String) -> Unit = {},
    initialPreview: GenomePreviewState? = null,
    initialViewport: FloatArray? = null,
    initialViewportWidth: Int = context.resources.displayMetrics.widthPixels.coerceAtLeast(1),
    initialViewportHeight: Int = context.resources.displayMetrics.heightPixels.coerceAtLeast(1),
    initialBackingSurfaceWidth: Int = initialViewportWidth,
    initialBackingSurfaceHeight: Int = initialViewportHeight
) : FrameLayout(context) {
    constructor(context: Context) : this(
        context,
        CellularitySettings.DefaultGenomePreviewMaxCells
    )

    private val previewSession = NativeGenomePreviewSession(
        context.applicationContext,
        previewMaxCells
    )
    private val nativeRenderEndpoint = previewSession.renderEndpoint
    private val endpointGeneration = previewSession.endpointGeneration
    private val publishResult = GenomePreviewPublishResultLayout.allocateBuffer()
    private val acknowledgment = GenomePreviewAcknowledgmentLayout.allocateBuffer()
    private val revealCoordinator = GenomePreviewRevealCoordinator(
        endpointGeneration = endpointGeneration,
        enabled = backendKind == RenderBackendKind.VULKAN
    )
    private val previewCover = View(context).apply {
        setBackgroundColor(GenomePreviewVisualContract.BACKGROUND_ARGB)
        visibility = if (revealCoordinator.coverVisible) VISIBLE else GONE
        isClickable = GenomePreviewVisualContract.COVER_CLICKABLE
        isLongClickable = false
        isFocusable = false
        isEnabled = GenomePreviewVisualContract.COVER_ENABLED
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    private var nextContentSequence = 1L
    private var nextViewSequence = 1L
    private var nextInteractionToken = 1L
    private var lastCellCountRevision = 0L
    private var lastViewportRevision = 0L
    private var lastSelectionToken = 0L
    private var retainedViewport = floatArrayOf(0.0f, 0.0f, 8.0f)
    private var requestedViewportWidth = initialViewportWidth.coerceAtLeast(1)
    private var requestedViewportHeight = initialViewportHeight.coerceAtLeast(1)
    private var backingSurfaceWidth = initialBackingSurfaceWidth.coerceAtLeast(1)
    private var backingSurfaceHeight = initialBackingSurfaceHeight.coerceAtLeast(1)

    @Volatile
    private var renderSurfaceAvailable = false

    @Volatile
    private var released = false

    private var renderThreadResumed = false
    private var hostLifecycleResumed = true
    private var hostLifecycleOwner: LifecycleOwner? = null
    private var displayContentReadyCallback: ((Boolean) -> Unit)? = null
    private var lastPublishedDisplayReady: Boolean? = null
    private val hostLifecycleObserver = object : DefaultLifecycleObserver {
        override fun onResume(owner: LifecycleOwner) {
            hostLifecycleResumed = true
            resumeRenderThreadIfReady()
        }

        override fun onPause(owner: LifecycleOwner) {
            hostLifecycleResumed = false
            pauseRenderThread()
        }

        override fun onDestroy(owner: LifecycleOwner) {
            hostLifecycleResumed = false
            pauseRenderThread()
        }
    }
    private val surfaceLifecycle: OnDemandRenderLifecycle = object : OnDemandRenderLifecycle {
        override fun onSurfaceCreated() {
            if (released) return
            nativeRenderEndpoint.onSurfaceCreated()
            if (backendKind == RenderBackendKind.OPEN_GL_ES) {
                nativeRenderEndpoint.preparePreviewSurfaceResources()
            }
            renderSurfaceAvailable = true
            applyRevealTransition(revealCoordinator.onSurfaceAvailable())
            renderSurface.requestFrame()
        }

        override fun onSurfaceChanged(width: Int, height: Int) {
            if (!released) {
                nativeRenderEndpoint.setViewportSurfaceSize(width, height)
                applyRevealTransition(revealCoordinator.onSurfaceChanged())
            }
        }

        override fun onSurfaceDestroyed() {
            markSurfaceUnavailable()
            nativeRenderEndpoint.onSurfaceDestroyed()
        }

        override fun onRenderFrame(): Long {
            if (released || !renderSurfaceAvailable) {
                return RenderFrameResultCodec.pack(
                    RenderFrameResultCodec.OUTCOME_SKIPPED_NOT_READY,
                    0
                )
            }
            val result = previewSession.draw(acknowledgment)
            consumeAcknowledgment(decodeAcknowledgment())
            return result
        }

        override fun createVulkanRenderWorker(
            context: Context,
            statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
        ): NativeVulkanRenderWorkerBridge = previewSession.borrowVulkanEndpoint().use { endpoint ->
            NativeVulkanRenderWorkerBridge.createPreview(
                context = context,
                contentKind = NativeVulkanRenderWorkerBridge.CONTENT_GENOME_PREVIEW,
                endpointHandle = endpoint.handle,
                endpointGeneration = endpoint.generation,
                statusReceiver = statusReceiver
            )
        }

        override fun onVulkanPlatformSurfaceAvailable() {
            if (released) return
            renderSurfaceAvailable = true
            applyRevealTransition(revealCoordinator.onSurfaceAvailable())
        }

        override fun onVulkanPlatformSurfaceChanged(width: Int, height: Int) {
            if (!released) {
                applyRevealTransition(revealCoordinator.onSurfaceChanged())
            }
        }

        override fun onVulkanPlatformSurfaceContentCurrent(current: Boolean) {
            if (!released) {
                applyRevealTransition(
                    revealCoordinator.onPlatformContentCurrent(current)
                )
            }
        }
    }
    private val renderSurface = OnDemandRenderSurfaceHostFactory.create(
        context = context,
        backendKind = backendKind,
        lifecycle = surfaceLifecycle,
        onSurfaceInvalidated = ::markSurfaceUnavailable,
        onRuntimeFailure = { message ->
            markSurfaceUnavailable()
            onRenderBackendFailure(message)
        }
    )

    private var geneSelectionCallback: ((Int) -> Unit)? = null
    private var viewportChangedCallback: ((FloatArray) -> Unit)? = null
    private var previewCellCountCallback: ((Int) -> Unit)? = null
    private var lastSnapshot: GenomeDraft? = null
    private var lastSelectedGene = -1
    private var lastTimeHours = Double.NaN
    private var lastGridEnabled: Boolean? = null
    private var lastTreeEnabled: Boolean? = null
    private var lastViewportWidth = 0
    private var lastViewportHeight = 0
    private var latestPreview: GenomePreviewState? = null
    private val previewCaptureEndpoint = object : GenomePreviewCaptureEndpoint {
        override fun enqueueGenomePreviewCapture(
            requestId: Long,
            genomeDraftBytes: ByteArray,
            maximumSimulationTime: Double
        ): NativePreviewCaptureRequest? {
            if (!isGenomePreviewCaptureAvailable(
                    released = released,
                    renderSurfaceAvailable = renderSurfaceAvailable,
                    supportsOffscreenCapture = renderSurface.supportsOffscreenCapture
                )
            ) {
                return null
            }
            val request = previewSession.enqueueCapture(
                requestId,
                genomeDraftBytes,
                maximumSimulationTime
            ) ?: return null
            renderSurface.requestFrame()
            return request
        }
    }

    init {
        clipChildren = true
        clipToPadding = true
        initialPreview?.let(::update)
        restoreViewport(initialViewport)
        addView(
            renderSurface.view,
            renderSurfaceLayoutParams()
        )
        addView(
            previewCover,
            LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        bindHostLifecycle()
        resumeRenderThreadIfReady()
    }

    override fun onDetachedFromWindow() {
        pauseRenderThread()
        unbindHostLifecycle()
        super.onDetachedFromWindow()
    }

    private fun bindHostLifecycle() {
        val owner = findViewTreeLifecycleOwner() ?: return
        if (hostLifecycleOwner === owner) return
        unbindHostLifecycle()
        hostLifecycleOwner = owner
        owner.lifecycle.addObserver(hostLifecycleObserver)
        hostLifecycleResumed = owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)
        if (!hostLifecycleResumed) pauseRenderThread()
    }

    private fun unbindHostLifecycle() {
        hostLifecycleOwner?.lifecycle?.removeObserver(hostLifecycleObserver)
        hostLifecycleOwner = null
    }

    private fun resumeRenderThreadIfReady() {
        if (released || !isAttachedToWindow || !hostLifecycleResumed || renderThreadResumed) return
        renderThreadResumed = true
        renderSurface.resumeRendering()
        renderSurface.requestFrame()
    }

    private fun pauseRenderThread() {
        if (!renderThreadResumed) return
        renderThreadResumed = false
        markSurfaceUnavailable()
        renderSurface.pauseRendering()
    }

    private fun markSurfaceUnavailable() {
        renderSurfaceAvailable = false
        applyRevealTransition(revealCoordinator.onSurfaceUnavailable())
    }

    fun setGeneSelectionCallback(callback: ((Int) -> Unit)?) {
        geneSelectionCallback = callback
    }

    fun setViewportChangedCallback(callback: ((FloatArray) -> Unit)?) {
        viewportChangedCallback = callback
    }

    fun setPreviewCellCountCallback(callback: ((Int) -> Unit)?) {
        previewCellCountCallback = callback
    }

    fun setDisplayContentReadyCallback(callback: ((Boolean) -> Unit)?) {
        displayContentReadyCallback = callback
        lastPublishedDisplayReady = null
        publishDisplayContentReady()
    }

    fun updateLogicalViewport(width: Int, height: Int) {
        if (released || width <= 0 || height <= 0) return
        if (requestedViewportWidth == width && requestedViewportHeight == height) return
        requestedViewportWidth = width
        requestedViewportHeight = height
        latestPreview?.let(::update)
    }

    /**
     * Vulkan keeps one full-editor backing Surface while the divider only
     * changes the logical viewport and this FrameLayout's clipping bounds.
     * That prevents SurfaceFlinger from stretching an old-size swapchain
     * image during an interactive resize. GLES retains its established
     * MATCH_PARENT behavior.
     */
    fun updateBackingSurfaceSize(width: Int, height: Int) {
        if (
            released || backendKind != RenderBackendKind.VULKAN ||
            width <= 0 || height <= 0 ||
            (backingSurfaceWidth == width && backingSurfaceHeight == height)
        ) {
            return
        }
        backingSurfaceWidth = width
        backingSurfaceHeight = height
        renderSurface.view.layoutParams = renderSurfaceLayoutParams()
    }

    fun restoreViewport(state: FloatArray?) {
        if (
            released ||
            state == null ||
            state.size < 3 ||
            !state[0].isFinite() ||
            !state[1].isFinite() ||
            !state[2].isFinite() ||
            state[2] <= 0.0f
        ) {
            return
        }
        if (enqueueInteraction(
            INTERACTION_RESTORE_VIEWPORT,
            state[0],
            state[1],
            state[2]
        ) != 0L) {
            retainedViewport = state.copyOf(3)
            renderSurface.requestFrame()
        }
    }

    fun currentViewportState(): FloatArray? {
        if (released) return null

        // The Vulkan worker owns steady-state drawing, so its acknowledgments do
        // not pass through onRenderFrame(). Take one coherent lifecycle snapshot
        // before disposal instead of polling JNI while the editor is visible.
        if (
            backendKind == RenderBackendKind.VULKAN &&
            previewSession.readAcknowledgment(acknowledgment)
        ) {
            retainViewport(decodeAcknowledgment(), notify = false)
        }
        return retainedViewport.clone()
    }

    fun update(preview: GenomePreviewState) {
        if (released) return
        latestPreview = preview
        val contentChanged = preview.snapshot !== lastSnapshot ||
            preview.timeHours != lastTimeHours
        val viewChanged = preview.selectedGene.value != lastSelectedGene ||
            preview.gridEnabled != lastGridEnabled ||
            preview.treeEnabled != lastTreeEnabled ||
            requestedViewportWidth != lastViewportWidth ||
            requestedViewportHeight != lastViewportHeight
        if (!contentChanged && !viewChanged) return

        var accepted = false
        if (contentChanged) {
            val revision = publishContent(
                GenomeDraftWireCodec.encode(preview.snapshot),
                preview.timeHours
            )
            if (revision != 0L) {
                lastSnapshot = preview.snapshot
                lastTimeHours = preview.timeHours
                accepted = true
                applyRevealTransition(
                    revealCoordinator.onContentTargetAccepted(revision)
                )
            }
        }
        if (viewChanged) {
            val revision = publishViewState(
                preview.selectedGene.value,
                preview.gridEnabled,
                preview.treeEnabled,
                requestedViewportWidth,
                requestedViewportHeight
            )
            if (revision != 0L) {
                lastSelectedGene = preview.selectedGene.value
                lastGridEnabled = preview.gridEnabled
                lastTreeEnabled = preview.treeEnabled
                lastViewportWidth = requestedViewportWidth
                lastViewportHeight = requestedViewportHeight
                accepted = true
                applyRevealTransition(
                    revealCoordinator.onViewTargetAccepted(revision)
                )
            }
        }
        if (accepted) renderSurface.requestFrame()
    }

    fun captureGhost() {
        if (released) return
        if (
            enqueueInteraction(
                INTERACTION_CAPTURE_GHOST, 0.0f, 0.0f, 0.0f
            ) != 0L
        ) renderSurface.requestFrame()
    }

    fun clearGhost() {
        if (released) return
        if (
            enqueueInteraction(
                INTERACTION_CLEAR_GHOST, 0.0f, 0.0f, 0.0f
            ) != 0L
        ) renderSurface.requestFrame()
    }

    val maxPreviewShow: Int
        get() = previewMaxCells

    fun handleMove(dx: Float, dy: Float) {
        if (released) return
        if (
            enqueueInteraction(
                INTERACTION_MOVE_VIEWPORT, dx, dy, 0.0f
            ) != 0L
        ) renderSurface.requestFrame()
    }

    fun handleZoom(scale: Float, centerX: Float, centerY: Float) {
        if (released) return
        if (
            enqueueInteraction(
                INTERACTION_ZOOM_VIEWPORT, scale, centerX, centerY
            ) != 0L
        ) renderSurface.requestFrame()
    }

    fun handleTap(x: Float, y: Float) {
        if (released) return
        val token = enqueueInteraction(INTERACTION_SELECT_GENE, x, y, 0.0f)
        if (token == 0L) return
        val transition = revealCoordinator.onInteractionTargetAccepted(token)
        applyRevealTransition(transition)
        if (!transition.requestTrackedFrame) renderSurface.requestFrame()
    }

    internal fun previewCaptureEndpoint(): GenomePreviewCaptureEndpoint =
        previewCaptureEndpoint

    private fun publishContent(
        genomeDraftBytes: ByteArray,
        simulationTimeHours: Double
    ): Long {
        if (nextContentSequence == 0L) return 0L
        previewSession.publishContent(
            genomeDraftBytes,
            simulationTimeHours,
            nextContentSequence,
            publishResult
        )
        val result = GenomePreviewPublishResultLayout.decode(publishResult)
        if (result.accepted) {
            nextContentSequence = incrementOrExhaust(nextContentSequence)
        }
        return if (result.accepted) result.revision else 0L
    }

    private fun publishViewState(
        selectedGeneIndex: Int,
        gridEnabled: Boolean,
        treeEnabled: Boolean,
        viewportWidth: Int,
        viewportHeight: Int
    ): Long {
        if (nextViewSequence == 0L) return 0L
        previewSession.publishViewState(
            selectedGeneIndex,
            gridEnabled,
            treeEnabled,
            viewportWidth,
            viewportHeight,
            nextViewSequence,
            publishResult
        )
        val result = GenomePreviewPublishResultLayout.decode(publishResult)
        if (result.accepted) {
            nextViewSequence = incrementOrExhaust(nextViewSequence)
        }
        return if (result.accepted) result.revision else 0L
    }

    private fun enqueueInteraction(
        kind: Int,
        first: Float,
        second: Float,
        third: Float
    ): Long {
        if (nextInteractionToken == 0L) return 0L
        val token = nextInteractionToken
        val status = previewSession.enqueueInteraction(
            token, kind, first, second, third
        )
        if (status != 0) return 0L
        nextInteractionToken = incrementOrExhaust(token)
        applyRevealTransition(
            revealCoordinator.onInteractionTargetAccepted(token)
        )
        return token
    }

    private fun decodeAcknowledgment(): GenomePreviewAcknowledgment =
        GenomePreviewAcknowledgmentLayout.decode(acknowledgment).also {
            check(it.endpointGeneration == endpointGeneration) {
                "Native Genome Preview acknowledgment is invalid"
            }
        }

    private fun consumeAcknowledgment(value: GenomePreviewAcknowledgment) {
        val cellCountRevision = value.cellCountContentRevision
        if (cellCountRevision != 0L && cellCountRevision != lastCellCountRevision) {
            lastCellCountRevision = cellCountRevision
            post { previewCellCountCallback?.invoke(value.previewCellCount) }
        }

        retainViewport(value, notify = true)

        val selectionToken = value.selectionToken
        if (selectionToken != 0L && selectionToken != lastSelectionToken) {
            lastSelectionToken = selectionToken
            if (value.selectedGeneIndex != -1) {
                post { geneSelectionCallback?.invoke(value.selectedGeneIndex) }
            }
        }
    }

    private fun retainViewport(
        value: GenomePreviewAcknowledgment,
        notify: Boolean
    ) {
        val viewport = viewportStateFromAcknowledgment(value) ?: return
        if (value.viewportRevision == lastViewportRevision) return
        lastViewportRevision = value.viewportRevision
        retainedViewport = viewport
        if (notify) post { viewportChangedCallback?.invoke(viewport.clone()) }
    }

    private fun applyRevealTransition(
        transition: GenomePreviewRevealCoordinator.Transition
    ) {
        val apply = {
            if (!released) {
                previewCover.visibility =
                    if (transition.coverVisible) View.VISIBLE else View.GONE
                publishDisplayContentReady()
                if (transition.readAcknowledgment) {
                    readAcknowledgmentForReveal()
                }
                if (transition.requestTrackedFrame) {
                    renderSurface.requestFrameWithPresentationTracking()
                }
            }
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            apply()
        } else {
            post { apply() }
        }
    }

    private fun readAcknowledgmentForReveal() {
        if (released || !renderSurfaceAvailable) return
        if (!previewSession.readAcknowledgment(acknowledgment)) {
            applyRevealTransition(
                revealCoordinator.onPlatformContentCurrent(false)
            )
            return
        }
        val value = decodeAcknowledgment()
        consumeAcknowledgment(value)
        applyRevealTransition(revealCoordinator.observeAcknowledgment(value))
    }

    private fun publishDisplayContentReady() {
        val ready = renderSurfaceAvailable &&
            (backendKind != RenderBackendKind.VULKAN || !revealCoordinator.coverVisible)
        if (lastPublishedDisplayReady == ready) return
        lastPublishedDisplayReady = ready
        displayContentReadyCallback?.invoke(ready)
    }

    private fun incrementOrExhaust(value: Long): Long =
        if (value == Long.MAX_VALUE) 0L else value + 1L

    private fun renderSurfaceLayoutParams(): LayoutParams =
        if (backendKind == RenderBackendKind.VULKAN) {
            LayoutParams(backingSurfaceWidth, backingSurfaceHeight)
        } else {
            LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        }

    fun release() {
        if (released) return
        released = true
        hostLifecycleResumed = false
        renderSurfaceAvailable = false
        revealCoordinator.release()
        publishDisplayContentReady()
        geneSelectionCallback = null
        viewportChangedCallback = null
        previewCellCountCallback = null
        displayContentReadyCallback = null
        pauseRenderThread()
        unbindHostLifecycle()
        renderSurface.release()
        previewSession.close()
    }

    private companion object {
        const val INTERACTION_RESTORE_VIEWPORT = 0
        const val INTERACTION_MOVE_VIEWPORT = 1
        const val INTERACTION_ZOOM_VIEWPORT = 2
        const val INTERACTION_SELECT_GENE = 3
        const val INTERACTION_CAPTURE_GHOST = 4
        const val INTERACTION_CLEAR_GHOST = 5

    }
}
