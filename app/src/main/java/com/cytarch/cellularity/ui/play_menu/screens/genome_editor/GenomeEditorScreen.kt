package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import android.widget.Toast
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import org.jetbrains.compose.resources.StringResource
import org.jetbrains.compose.resources.getString
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorScreen as SharedGenomeEditorScreen
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewHandle
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewMetrics
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewState
import com.cytarch.cellularity.shared.ui.play.genome_editor.sections.GenomePreviewSurfaceGeometry

/**
 * The phone's editor: the shared screen, plus the two things only a phone can supply.
 *
 * The editor itself -- its sections, its pager, its gestures and its layout -- is the one in the
 * shared module, because it is the same editor. What is left here is what a phone adds to it: a
 * Toast for the result of a file operation, and the native preview view, which draws the organism
 * through the platform surface the phone has and a desktop does not.
 *
 * Both arrive at the shared screen as parameters rather than as branches inside it, so a change to
 * the editor is a change in one place and a change to either platform is a change in one file.
 */
@Composable
fun GenomeEditorScreen(
    viewModel: GenomeEditorViewModel,
    coordinator: GenomeEditorCoordinator,
    previewMaxCells: Int,
    renderBackendKind: RenderBackendKind = RenderBackendKind.OPEN_GL_ES,
    onRenderBackendFailure: (String) -> Unit = {},
    onPreviewReadyChanged: (Boolean) -> Unit = {}
) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val context = LocalContext.current

    // The editor's file work is carried out by the coordinator and its answer is dispatched back;
    // the screen only connects the two, because the reducer decides what an answer means.
    LaunchedEffect(viewModel, coordinator) {
        viewModel.effects.collect { effect ->
            viewModel.dispatch(coordinator.effectHandler.handle(effect))
        }
    }

    // Messages are queued rather than shown where they arrive, because reading a string resource
    // is suspending and the editor reports them while composing. The queue keeps two messages in
    // a row in order instead of letting the second cancel the first.
    val pendingMessages = remember { mutableStateListOf<StringResource>() }
    val nextMessage = pendingMessages.firstOrNull()
    LaunchedEffect(nextMessage) {
        val resource = nextMessage ?: return@LaunchedEffect
        Toast.makeText(context, getString(resource), Toast.LENGTH_LONG).show()
        pendingMessages.removeAt(0)
    }

    SharedGenomeEditorScreen(
        state = state,
        onAction = viewModel::dispatch,
        defaultGeneFactory = GenomeEditorViewModel::createRandomizedEditorGene,
        layout = coordinator.layout,
        onMessage = { resource -> pendingMessages.add(resource) },
        preview = { geometry, handle, metrics, modifier ->
            AndroidGenomePreview(
                preview = state.preview,
                geometry = geometry,
                handle = handle,
                metrics = metrics,
                coordinator = coordinator,
                previewMaxCells = previewMaxCells,
                backendKind = renderBackendKind,
                onRenderBackendFailure = onRenderBackendFailure,
                onPreviewReadyChanged = onPreviewReadyChanged,
                onGeneSelected = { geneIndex ->
                    viewModel.dispatch(GenomeEditorAction.SelectGene(GeneIndex(geneIndex)))
                },
                modifier = modifier
            )
        }
    )
}

/**
 * The organism, drawn by the native preview view inside a surface the platform composites.
 *
 * This is the block that used to live inside the editor. It is unchanged in what it does -- the
 * view is created once per backend, updated with the draft, and told about the surface it draws
 * into -- and it now stands outside the editor because the editor must not know what a surface is.
 */
@Composable
private fun AndroidGenomePreview(
    preview: GenomePreviewState,
    geometry: GenomePreviewSurfaceGeometry,
    handle: GenomePreviewHandle,
    metrics: GenomePreviewMetrics,
    coordinator: GenomeEditorCoordinator,
    previewMaxCells: Int,
    backendKind: RenderBackendKind,
    onRenderBackendFailure: (String) -> Unit,
    onPreviewReadyChanged: (Boolean) -> Unit,
    onGeneSelected: (Int) -> Unit,
    modifier: Modifier
) {
    // The view captures its callbacks once, so they read the latest lambdas through state.
    val currentOnPreviewReadyChanged by rememberUpdatedState(onPreviewReadyChanged)
    val currentOnGeneSelected by rememberUpdatedState(onGeneSelected)
    val currentOnRenderBackendFailure by rememberUpdatedState(onRenderBackendFailure)
    // A new backend needs a new view: the old one is bound to the backend it was created on.
    key(backendKind) {
    AndroidView(
        modifier = modifier,
        factory = { context ->
            GenomePreviewView(
                context = context,
                previewMaxCells = previewMaxCells,
                backendKind = backendKind,
                onRenderBackendFailure = { message -> currentOnRenderBackendFailure(message) },
                initialPreview = preview,
                initialViewport = coordinator.restoreViewport(),
                initialViewportWidth = geometry.viewportWidth,
                initialViewportHeight = geometry.viewportHeight,
                initialBackingSurfaceWidth = geometry.backingWidth,
                initialBackingSurfaceHeight = geometry.backingHeight
            ).also { view ->
                coordinator.attachPreview(view)
                view.setGeneSelectionCallback { geneIndex -> currentOnGeneSelected(geneIndex) }
                view.setViewportChangedCallback(coordinator::rememberViewport)
                view.setPreviewCellCountCallback { metrics.cellCount = it }
                view.setDisplayContentReadyCallback { ready -> currentOnPreviewReadyChanged(ready) }
                metrics.maximumCellCount = view.maxPreviewShow
                // The editor holds the gestures and asks whoever is drawing to answer them; this
                // view is that whoever, so it is handed over rather than reached into.
                handle.interaction = view
            }
        },
        update = { view ->
            view.updateBackingSurfaceSize(geometry.backingWidth, geometry.backingHeight)
            view.updateLogicalViewport(geometry.viewportWidth, geometry.viewportHeight)
            view.update(preview)
        },
        onRelease = { view ->
            if (handle.interaction === view) handle.interaction = null
            coordinator.releasePreview(view)
        }
    )
    }
}
