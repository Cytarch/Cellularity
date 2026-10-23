package com.cytarch.cellularity.ui.play_menu.screens.genome_editor

import com.cytarch.cellularity.shared.generated.resources.*
import org.jetbrains.compose.resources.StringResource
import org.jetbrains.compose.resources.getString
import org.jetbrains.compose.resources.stringResource
import org.jetbrains.compose.resources.pluralStringResource

import android.widget.Toast
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerState
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.zIndex
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.shared.model.GenomeContract
import com.cytarch.cellularity.shared.ui.components.CellularityAlertDialog
import com.cytarch.cellularity.shared.ui.components.DefaultLabeledCheckbox
import com.cytarch.cellularity.shared.ui.components.DefaultSlider
import com.cytarch.cellularity.shared.ui.play.genome_editor.GeneIndex
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorAction
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorConfig
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorSection
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorSections
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeEditorState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeFileFailureReason
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeFileState
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeGeneCatalog
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeLoadChoice
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomePreviewOption
import com.cytarch.cellularity.shared.ui.text.formatLocalizedNumber
import com.cytarch.cellularity.shared.ui.theme.CellularityDimensions
import com.cytarch.cellularity.shared.ui.theme.cellularityOutlinedTextFieldColors
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.painterResource
import kotlin.math.roundToInt

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

    LaunchedEffect(viewModel, coordinator) {
        viewModel.effects.collect { effect ->
            viewModel.dispatch(coordinator.effectHandler.handle(effect))
        }
    }
    LaunchedEffect(state.fileState) {
        val terminalState = state.fileState
        val message = terminalState.feedbackMessageResource() ?: return@LaunchedEffect
        Toast.makeText(context, getString(message), Toast.LENGTH_LONG).show()
        viewModel.dispatch(GenomeEditorAction.ClearFileFeedback)
    }

    GenomeEditorContent(
        state = state,
        coordinator = coordinator,
        previewMaxCells = previewMaxCells,
        renderBackendKind = renderBackendKind,
        onRenderBackendFailure = onRenderBackendFailure,
        onPreviewReadyChanged = onPreviewReadyChanged,
        onAction = viewModel::dispatch,
        defaultGeneFactory = GenomeEditorViewModel::createRandomizedEditorGene
    )
}

@Composable
private fun GenomeEditorContent(
    state: GenomeEditorState,
    coordinator: GenomeEditorCoordinator,
    previewMaxCells: Int,
    renderBackendKind: RenderBackendKind,
    onRenderBackendFailure: (String) -> Unit,
    onPreviewReadyChanged: (Boolean) -> Unit,
    onAction: (GenomeEditorAction) -> Unit,
    defaultGeneFactory: (Int) -> com.cytarch.cellularity.shared.model.GeneDraft
) {
    val density = LocalDensity.current
    val dimensions = CellularityDimensions.current
    val minimumPreviewHeight = PREVIEW_TOP_ACTIONS_HEIGHT +
        PREVIEW_BOTTOM_CONTROLS_TOP_PADDING +
        PREVIEW_TIME_CONTROLS_HEIGHT +
        dimensions.compactControlHeight +
        PREVIEW_RESIZE_HANDLE_THICKNESS
    val minimumPreviewHeightPx = with(density) { minimumPreviewHeight.toPx() }
    val minimumPreviewWidthPx = with(density) { MINIMUM_LANDSCAPE_PREVIEW_WIDTH.toPx() }
    var previewHeightPx by rememberSaveable {
        val restoredHeight = coordinator.restorePreviewHeightDp()?.dp ?: 250.dp
        mutableFloatStateOf(with(density) { restoredHeight.toPx() })
    }
    var previewWidthPx by rememberSaveable {
        val restoredWidth = coordinator.restorePreviewWidthDp()
        mutableFloatStateOf(
            restoredWidth?.let { with(density) { it.dp.toPx() } }
                ?: UNSET_PREVIEW_EXTENT
        )
    }
    var previewSettingsExpanded by rememberSaveable { mutableStateOf(false) }
    val previewHandle = remember { GenomePreviewHandle() }
    var modeActionTarget by remember { mutableStateOf<GeneIndex?>(null) }
    var deleteActionTarget by remember { mutableStateOf<GeneIndex?>(null) }
    val geneCatalog = remember(state.genome) { GenomeEditorSections.geneCatalog(state) }
    val generalScrollState = rememberScrollState()
    val splittingScrollState = rememberScrollState()
    val specificScrollState = rememberScrollState()
    val sectionPagerState = rememberPagerState(
        initialPage = state.selectedSection.ordinal,
        pageCount = { GenomeEditorSection.entries.size }
    )

    val gestures = remember(
        previewHandle,
        state.preview.ghostEnabled,
        state.preview.treeEnabled
    ) {
        GenomeEditorGestureCallbacks(
            onStarted = {
                if (state.preview.ghostEnabled && !state.preview.treeEnabled) {
                    previewHandle.view?.captureGhost()
                }
            },
            onFinished = { previewHandle.view?.clearGhost() }
        )
    }

    LaunchedEffect(state.preview.ghostEnabled) {
        if (!state.preview.ghostEnabled) previewHandle.view?.clearGhost()
    }

    LaunchedEffect(state.selectedSection) {
        val requestedPage = state.selectedSection.ordinal
        if (sectionPagerState.currentPage != requestedPage) {
            sectionPagerState.scrollToPage(requestedPage)
        }
    }

    LaunchedEffect(sectionPagerState) {
        snapshotFlow { sectionPagerState.currentPage }
            .distinctUntilChanged()
            .collect { pageIndex ->
                onAction(
                    GenomeEditorAction.SelectSection(
                        GenomeEditorSection.entries[pageIndex]
                    )
                )
            }
    }

    val onAddGene = {
        if (state.genome.genes.size < GenomeContract.MAX_GENE_COUNT) {
            val insertionIndex = state.selectedGene.value + 1
            onAction(
                GenomeEditorAction.AddGene(
                    after = state.selectedGene,
                    value = defaultGeneFactory(insertionIndex)
                )
            )
        }
    }

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .clipToBounds()
    ) {
        val parentWidthPx = with(density) { maxWidth.toPx() }
        val parentHeightPx = with(density) { maxHeight.toPx() }
        val sideBySide = useSideBySideGenomeEditorLayout(parentWidthPx, parentHeightPx)

        if (sideBySide) {
            val requestedWidthPx = previewWidthPx.takeIf { it != UNSET_PREVIEW_EXTENT }
                ?: parentWidthPx * DEFAULT_LANDSCAPE_PREVIEW_FRACTION
            val visiblePreviewWidthPx = constrainedPreviewExtent(
                requestedExtentPx = requestedWidthPx,
                parentExtentPx = parentWidthPx,
                minimumExtentPx = minimumPreviewWidthPx,
                reservedSecondaryExtentPx = 0.0f
            )
            val previewSurfaceGeometry = genomePreviewSurfaceGeometry(
                visibleWidthPx = visiblePreviewWidthPx,
                visibleHeightPx = parentHeightPx,
                parentWidthPx = parentWidthPx,
                parentHeightPx = parentHeightPx
            )
            Row(modifier = Modifier.fillMaxSize()) {
                GenomePreviewPanel(
                    state = state,
                    modifier = Modifier
                        .width(with(density) { visiblePreviewWidthPx.toDp() })
                        .fillMaxHeight(),
                    resizeDirection = PreviewResizeDirection.WIDTH,
                    coordinator = coordinator,
                    previewMaxCells = previewMaxCells,
                    renderBackendKind = renderBackendKind,
                    onRenderBackendFailure = onRenderBackendFailure,
                    viewportWidth = previewSurfaceGeometry.viewportWidth,
                    viewportHeight = previewSurfaceGeometry.viewportHeight,
                    backingSurfaceWidth = previewSurfaceGeometry.backingWidth,
                    backingSurfaceHeight = previewSurfaceGeometry.backingHeight,
                    onPreviewReadyChanged = onPreviewReadyChanged,
                    geneCatalog = geneCatalog,
                    previewHandle = previewHandle,
                    gestures = gestures,
                    settingsExpanded = previewSettingsExpanded,
                    onSettingsExpanded = { previewSettingsExpanded = it },
                    onResizeDelta = { delta ->
                        val currentWidthPx = constrainedPreviewExtent(
                            requestedExtentPx = requestedWidthPx,
                            parentExtentPx = parentWidthPx,
                            minimumExtentPx = minimumPreviewWidthPx,
                            reservedSecondaryExtentPx = 0.0f
                        )
                        val resizedWidthPx = resizedPreviewExtent(
                            currentExtentPx = currentWidthPx,
                            deltaPx = delta,
                            minimumExtentPx = minimumPreviewWidthPx,
                            maximumExtentPx = parentWidthPx
                        )
                        previewWidthPx = resizedWidthPx
                        coordinator.rememberPreviewWidthDp(
                            with(density) { resizedWidthPx.toDp().value }
                        )
                    },
                    onModeLongClick = { modeActionTarget = it },
                    onAddGene = onAddGene,
                    onAction = onAction
                )
                GenomeEditorParametersPane(
                    state = state,
                    sectionPagerState = sectionPagerState,
                    generalScrollState = generalScrollState,
                    splittingScrollState = splittingScrollState,
                    specificScrollState = specificScrollState,
                    gestures = gestures,
                    defaultGeneFactory = defaultGeneFactory,
                    onAction = onAction,
                    modifier = Modifier
                        .weight(1.0f)
                        .fillMaxHeight()
                )
            }
        } else {
            val visiblePreviewHeightPx = constrainedPreviewExtent(
                requestedExtentPx = previewHeightPx,
                parentExtentPx = parentHeightPx,
                minimumExtentPx = minimumPreviewHeightPx,
                reservedSecondaryExtentPx = 0.0f
            )
            val previewSurfaceGeometry = genomePreviewSurfaceGeometry(
                visibleWidthPx = parentWidthPx,
                visibleHeightPx = visiblePreviewHeightPx,
                parentWidthPx = parentWidthPx,
                parentHeightPx = parentHeightPx
            )
            Column(modifier = Modifier.fillMaxSize()) {
                GenomePreviewPanel(
                    state = state,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(with(density) { visiblePreviewHeightPx.toDp() }),
                    resizeDirection = PreviewResizeDirection.HEIGHT,
                    coordinator = coordinator,
                    previewMaxCells = previewMaxCells,
                    renderBackendKind = renderBackendKind,
                    onRenderBackendFailure = onRenderBackendFailure,
                    viewportWidth = previewSurfaceGeometry.viewportWidth,
                    viewportHeight = previewSurfaceGeometry.viewportHeight,
                    backingSurfaceWidth = previewSurfaceGeometry.backingWidth,
                    backingSurfaceHeight = previewSurfaceGeometry.backingHeight,
                    onPreviewReadyChanged = onPreviewReadyChanged,
                    geneCatalog = geneCatalog,
                    previewHandle = previewHandle,
                    gestures = gestures,
                    settingsExpanded = previewSettingsExpanded,
                    onSettingsExpanded = { previewSettingsExpanded = it },
                    onResizeDelta = { delta ->
                        val currentHeightPx = constrainedPreviewExtent(
                            requestedExtentPx = previewHeightPx,
                            parentExtentPx = parentHeightPx,
                            minimumExtentPx = minimumPreviewHeightPx,
                            reservedSecondaryExtentPx = 0.0f
                        )
                        val resizedHeightPx = resizedPreviewExtent(
                            currentExtentPx = currentHeightPx,
                            deltaPx = delta,
                            minimumExtentPx = minimumPreviewHeightPx,
                            maximumExtentPx = parentHeightPx
                        )
                        previewHeightPx = resizedHeightPx
                        coordinator.rememberPreviewHeightDp(
                            with(density) { resizedHeightPx.toDp().value }
                        )
                    },
                    onModeLongClick = { modeActionTarget = it },
                    onAddGene = onAddGene,
                    onAction = onAction
                )
                GenomeEditorParametersPane(
                    state = state,
                    sectionPagerState = sectionPagerState,
                    generalScrollState = generalScrollState,
                    splittingScrollState = splittingScrollState,
                    specificScrollState = specificScrollState,
                    gestures = gestures,
                    defaultGeneFactory = defaultGeneFactory,
                    onAction = onAction,
                    modifier = Modifier
                        .weight(1.0f)
                        .fillMaxWidth()
                )
            }
        }
    }

    when (val fileState = state.fileState) {
        is GenomeFileState.Choosing -> GenomeLoadDialog(fileState.choices, onAction)
        GenomeFileState.EnteringSaveName -> GenomeSaveDialog(state.genomeName, onAction)
        else -> Unit
    }
    if (state.fileState == GenomeFileState.Listing ||
        state.fileState is GenomeFileState.Loading ||
        state.fileState is GenomeFileState.Saving
    ) {
        Box(
            modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.25f)),
            contentAlignment = Alignment.Center
        ) {
            CircularProgressIndicator()
        }
    }

    modeActionTarget?.let { target ->
        GenomeModeActionDialog(
            selected = state.selectedGene,
            target = target,
            onReset = {
                onAction(GenomeEditorAction.ResetGene(target, defaultGeneFactory(target.value)))
                modeActionTarget = null
            },
            onCopy = {
                onAction(GenomeEditorAction.CopyGene(state.selectedGene, target))
                modeActionTarget = null
            },
            onDuplicate = {
                onAction(GenomeEditorAction.DuplicateGene(target))
                modeActionTarget = null
            },
            onMoveBefore = if (target.value > 0) {
                {
                    onAction(GenomeEditorAction.MoveGene(target, GeneIndex(target.value - 1)))
                    modeActionTarget = null
                }
            } else null,
            onMoveAfter = if (target.value < state.genome.genes.lastIndex) {
                {
                    onAction(GenomeEditorAction.MoveGene(target, GeneIndex(target.value + 1)))
                    modeActionTarget = null
                }
            } else null,
            onDelete = if (state.genome.genes.size > GenomeContract.MIN_GENE_COUNT) {
                {
                    deleteActionTarget = target
                    modeActionTarget = null
                }
            } else null,
            onDismiss = { modeActionTarget = null }
        )
    }
    deleteActionTarget?.let { target ->
        GenomeDeleteGeneDialog(
            target = target,
            genes = geneCatalog,
            onDelete = { replacement ->
                onAction(GenomeEditorAction.DeleteGene(target, replacement))
                deleteActionTarget = null
            },
            onDismiss = { deleteActionTarget = null }
        )
    }
}

@Composable
private fun GenomeEditorParametersPane(
    state: GenomeEditorState,
    sectionPagerState: PagerState,
    generalScrollState: ScrollState,
    splittingScrollState: ScrollState,
    specificScrollState: ScrollState,
    gestures: GenomeEditorGestureCallbacks,
    defaultGeneFactory: (Int) -> com.cytarch.cellularity.shared.model.GeneDraft,
    onAction: (GenomeEditorAction) -> Unit,
    modifier: Modifier = Modifier
) {
    val sectionPagerScope = rememberCoroutineScope()
    Column(
        modifier = modifier
            .background(MaterialTheme.colorScheme.background)
            .clipToBounds()
    ) {
        GenomeSectionBar(
            selected = GenomeEditorSection.entries[sectionPagerState.currentPage],
            onSectionSelected = { section ->
                sectionPagerScope.launch {
                    sectionPagerState.scrollToPage(section.ordinal)
                }
            }
        )

        if (!state.validation.isValid) {
            Text(
                text = pluralStringResource(
                    Res.plurals.genome_editor_validation_issue_count,
                    state.validation.issues.size,
                    state.validation.issues.size
                ),
                color = MaterialTheme.colorScheme.error,
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 12.dp, vertical = 6.dp)
            )
        }

        HorizontalPager(
            state = sectionPagerState,
            modifier = Modifier
                .fillMaxWidth()
                .weight(1.0f),
            beyondViewportPageCount = (GenomeEditorSection.entries.size - 1)
                .coerceAtLeast(0)
        ) { pageIndex ->
            val section = GenomeEditorSection.entries[pageIndex]
            val scrollState = when (section) {
                GenomeEditorSection.GENERAL -> generalScrollState
                GenomeEditorSection.SPLITTING -> splittingScrollState
                GenomeEditorSection.SPECIFIC -> specificScrollState
            }
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(scrollState)
                    .padding(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                when (section) {
                    GenomeEditorSection.GENERAL -> GenomeGeneralSection(
                        overview = GenomeEditorSections.overview(state),
                        structure = GenomeEditorSections.structure(state),
                        gestures = gestures,
                        onAction = onAction
                    )
                    GenomeEditorSection.SPLITTING -> GenomeSplittingSection(
                        state = GenomeEditorSections.structure(state),
                        childRules = GenomeEditorSections.childRules(state),
                        gestures = gestures,
                        onAddChildGene = { child ->
                            if (state.genome.genes.size < GenomeContract.MAX_GENE_COUNT) {
                                val insertionIndex = state.selectedGene.value + 1
                                onAction(
                                    GenomeEditorAction.AddChildGene(
                                        child = child,
                                        value = defaultGeneFactory(insertionIndex)
                                    )
                                )
                            }
                        },
                        onAction = onAction
                    )
                    GenomeEditorSection.SPECIFIC -> {
                        val cellSpecific = GenomeEditorSections.cellSpecific(state)
                        val signals = GenomeEditorSections.signals(state)
                        when {
                            cellSpecific.values.isNotEmpty() -> GenomeCellSpecificSection(
                                state = cellSpecific,
                                snapValues = state.snapValues,
                                gestures = gestures,
                                onAction = onAction
                            )
                            signals.hasVisibleControls -> GenomeSignalsSection(
                                state = signals,
                                snapValues = state.snapValues,
                                gestures = gestures,
                                onAction = onAction
                            )
                            else -> GenomeEditorSectionMessage(
                                stringResource(
                                    Res.string.genome_editor_no_specific_settings_message
                                )
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun GenomePreviewPanel(
    state: GenomeEditorState,
    modifier: Modifier,
    resizeDirection: PreviewResizeDirection,
    coordinator: GenomeEditorCoordinator,
    previewMaxCells: Int,
    renderBackendKind: RenderBackendKind,
    onRenderBackendFailure: (String) -> Unit,
    viewportWidth: Int,
    viewportHeight: Int,
    backingSurfaceWidth: Int,
    backingSurfaceHeight: Int,
    onPreviewReadyChanged: (Boolean) -> Unit,
    geneCatalog: GenomeGeneCatalog,
    previewHandle: GenomePreviewHandle,
    gestures: GenomeEditorGestureCallbacks,
    settingsExpanded: Boolean,
    onSettingsExpanded: (Boolean) -> Unit,
    onResizeDelta: (Float) -> Unit,
    onModeLongClick: (GeneIndex) -> Unit,
    onAddGene: () -> Unit,
    onAction: (GenomeEditorAction) -> Unit
) {
    val dimensions = CellularityDimensions.current
    val latestOnResizeDelta by rememberUpdatedState(onResizeDelta)
    val latestOnPreviewReadyChanged by rememberUpdatedState(onPreviewReadyChanged)
    val resizeHandleEndPadding = if (resizeDirection == PreviewResizeDirection.WIDTH) {
        PREVIEW_RESIZE_HANDLE_THICKNESS
    } else {
        0.dp
    }
    val resizeHandleBottomPadding = if (resizeDirection == PreviewResizeDirection.HEIGHT) {
        PREVIEW_RESIZE_HANDLE_THICKNESS
    } else {
        0.dp
    }
    var previewCellCount by remember { mutableIntStateOf(0) }
    var previewMaxShow by remember { mutableIntStateOf(0) }
    var timeSliderChanging by remember { mutableStateOf(false) }
    Box(
        modifier = modifier
            // Keep the clipped panel beneath either renderer equal to the native clear color.
            // GLES resizes asynchronously; Vulkan keeps a larger stable backing Surface and
            // changes only this panel's clip plus its logical viewport.
            .background(GenomePreviewVisualContract.BackgroundColor)
            .clipToBounds()
    ) {
        key(renderBackendKind) {
            AndroidView(
                modifier = Modifier
                    .fillMaxSize()
                    .pointerInput(Unit) {
                        detectTapGestures { previewHandle.view?.handleTap(it.x, it.y) }
                    }
                    .pointerInput(Unit) {
                        detectTransformGestures(panZoomLock = false) { centroid, pan, zoom, _ ->
                            if (pan.x != 0.0f || pan.y != 0.0f) {
                                previewHandle.view?.handleMove(pan.x, pan.y)
                            }
                            if (zoom != 1.0f) {
                                previewHandle.view?.handleZoom(zoom, centroid.x, centroid.y)
                            }
                        }
                    },
                factory = { context ->
                    GenomePreviewView(
                        context = context,
                        previewMaxCells = previewMaxCells,
                        backendKind = renderBackendKind,
                        onRenderBackendFailure = onRenderBackendFailure,
                        initialPreview = state.preview,
                        initialViewport = coordinator.restoreViewport(),
                        initialViewportWidth = viewportWidth,
                        initialViewportHeight = viewportHeight,
                        initialBackingSurfaceWidth = backingSurfaceWidth,
                        initialBackingSurfaceHeight = backingSurfaceHeight
                    ).also { view ->
                        coordinator.attachPreview(view)
                        view.setGeneSelectionCallback {
                            onAction(GenomeEditorAction.SelectGene(GeneIndex(it)))
                        }
                        view.setViewportChangedCallback(coordinator::rememberViewport)
                        view.setPreviewCellCountCallback { previewCellCount = it }
                        view.setDisplayContentReadyCallback {
                            latestOnPreviewReadyChanged(it)
                        }
                        previewHandle.view = view
                        previewMaxShow = view.maxPreviewShow
                    }
                },
                update = { view ->
                    view.updateBackingSurfaceSize(
                        backingSurfaceWidth,
                        backingSurfaceHeight
                    )
                    view.updateLogicalViewport(viewportWidth, viewportHeight)
                    view.update(state.preview)
                },
                onRelease = { view ->
                    if (previewHandle.view === view) previewHandle.view = null
                    coordinator.releasePreview(view)
                }
            )
        }

        Row(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(end = resizeHandleEndPadding)
                .height(PREVIEW_TOP_ACTIONS_HEIGHT)
                .background(
                    MaterialTheme.colorScheme.surfaceVariant,
                    RoundedCornerShape(bottomStart = 16.dp)
                ),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = { onAction(GenomeEditorAction.RequestSave) }) {
                Icon(
                    painter = painterResource(Res.drawable.genome_save),
                    contentDescription = stringResource(Res.string.genome_editor_save_action),
                    tint = Color.White
                )
            }
            IconButton(onClick = { onAction(GenomeEditorAction.RequestLoad) }) {
                Icon(
                    painter = painterResource(Res.drawable.genome_load),
                    contentDescription = stringResource(Res.string.genome_editor_load_action),
                    tint = Color.White
                )
            }
            Box {
                IconButton(onClick = { onSettingsExpanded(true) }) {
                    Icon(
                        imageVector = Icons.Default.Settings,
                        contentDescription = stringResource(Res.string.genome_editor_preview_settings_title),
                        tint = Color.White
                    )
                }
                GenomeEditorDropdownMenu(
                    expanded = settingsExpanded,
                    onDismissRequest = { onSettingsExpanded(false) }
                ) {
                    PreviewOption(
                        stringResource(Res.string.genome_editor_preview_grid_option),
                        GenomePreviewOption.GRID,
                        state,
                        onAction
                    )
                    PreviewOption(
                        stringResource(Res.string.genome_editor_preview_tree_option),
                        GenomePreviewOption.TREE,
                        state,
                        onAction
                    )
                    PreviewOption(
                        stringResource(Res.string.genome_editor_preview_ghost_option),
                        GenomePreviewOption.GHOST,
                        state,
                        onAction
                    )
                    DefaultLabeledCheckbox(
                        label = stringResource(Res.string.genome_editor_snap_values_label),
                        checked = state.snapValues,
                        onCheckedChange = {
                            onAction(GenomeEditorAction.SetSnapValues(it))
                        }
                    )
                }
            }
        }

        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .padding(
                    top = PREVIEW_BOTTOM_CONTROLS_TOP_PADDING,
                    end = resizeHandleEndPadding,
                    bottom = resizeHandleBottomPadding
                )
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(PREVIEW_TIME_CONTROLS_HEIGHT),
                verticalAlignment = Alignment.CenterVertically
            ) {
                PreviewTimeButton(
                    label = stringResource(Res.string.genome_editor_decrease_symbol),
                    onStep = {
                        onAction(
                            GenomeEditorAction.SetPreviewTime(
                                state.preview.timeHours - GenomeEditorConfig.PREVIEW_TIME_BUTTON_STEP_HOURS
                            )
                        )
                    },
                    modifier = Modifier.zIndex(2.0f)
                )
                DefaultSlider(
                    value = (
                        state.preview.timeHours / GenomeEditorConfig.PREVIEW_MAX_TIME_HOURS *
                            GenomeEditorConfig.PREVIEW_TIME_STEPS
                        ).toFloat(),
                    onValueChange = {
                        if (!timeSliderChanging) {
                            timeSliderChanging = true
                            gestures.onStarted()
                        }
                        onAction(
                            GenomeEditorAction.SetPreviewTime(
                                it / GenomeEditorConfig.PREVIEW_TIME_STEPS *
                                    GenomeEditorConfig.PREVIEW_MAX_TIME_HOURS
                            )
                        )
                    },
                    onValueChangeFinished = {
                        if (timeSliderChanging) {
                            timeSliderChanging = false
                            gestures.onFinished()
                        }
                    },
                    valueRange = 0.0f..GenomeEditorConfig.PREVIEW_TIME_STEPS.toFloat(),
                    modifier = Modifier.weight(1.0f)
                )
                PreviewTimeButton(
                    label = stringResource(Res.string.genome_editor_increase_symbol),
                    onStep = {
                        onAction(
                            GenomeEditorAction.SetPreviewTime(
                                state.preview.timeHours + GenomeEditorConfig.PREVIEW_TIME_BUTTON_STEP_HOURS
                            )
                        )
                    },
                    modifier = Modifier.zIndex(2.0f)
                )
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(dimensions.compactControlHeight),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .weight(1.0f)
                        .height(dimensions.compactControlHeight)
                        .background(Color.Black.copy(alpha = 0.5f))
                        .padding(horizontal = dimensions.small),
                    contentAlignment = Alignment.CenterStart
                ) {
                    Text(
                        text = stringResource(
                            Res.string.genome_editor_preview_summary_format,
                            formatLocalizedNumber(
                                state.preview.timeHours,
                                minimumFractionDigits = 2,
                                maximumFractionDigits = 2
                            ),
                            stringResource(Res.string.simulation_hour_abbreviation),
                            previewCellCount,
                            previewMaxShow
                        ),
                        color = Color.White,
                        style = MaterialTheme.typography.bodySmall
                    )
                }
                GenomeGeneDropdown(
                    genes = geneCatalog,
                    selected = state.selectedGene,
                    onSelected = { onAction(GenomeEditorAction.SelectGene(it)) },
                    onLongClick = onModeLongClick,
                    onAddGene = onAddGene,
                    canAddGene = state.genome.genes.size < GenomeContract.MAX_GENE_COUNT
                )
            }
        }

        val resizeHandleModifier = when (resizeDirection) {
            PreviewResizeDirection.HEIGHT -> Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .height(PREVIEW_RESIZE_HANDLE_THICKNESS)
            PreviewResizeDirection.WIDTH -> Modifier
                .align(Alignment.CenterEnd)
                .fillMaxHeight()
                .width(PREVIEW_RESIZE_HANDLE_THICKNESS)
        }
        Box(
            modifier = resizeHandleModifier
                .zIndex(3.0f)
                .pointerInput(resizeDirection) {
                    detectDragGestures { change, dragAmount ->
                        change.consume()
                        latestOnResizeDelta(
                            when (resizeDirection) {
                                PreviewResizeDirection.HEIGHT -> dragAmount.y
                                PreviewResizeDirection.WIDTH -> dragAmount.x
                            }
                        )
                    }
                },
            contentAlignment = Alignment.Center
        ) {
            Box(
                modifier = when (resizeDirection) {
                    PreviewResizeDirection.HEIGHT -> Modifier
                        .width(36.dp)
                        .height(4.dp)
                    PreviewResizeDirection.WIDTH -> Modifier
                        .width(4.dp)
                        .height(36.dp)
                }
                    .background(Color.White.copy(alpha = 0.5f), RoundedCornerShape(2.dp))
            )
        }
    }
}

private class GenomePreviewHandle {
    var view: GenomePreviewView? = null
}

private enum class PreviewResizeDirection {
    HEIGHT,
    WIDTH
}

internal fun useSideBySideGenomeEditorLayout(widthPx: Float, heightPx: Float): Boolean =
    widthPx > heightPx

internal data class GenomePreviewSurfaceGeometry(
    val viewportWidth: Int,
    val viewportHeight: Int,
    val backingWidth: Int,
    val backingHeight: Int
)

/**
 * Keeps the physical Vulkan Surface at the editor bounds while the divider
 * changes only the visible viewport. GLES ignores the explicit backing size
 * and keeps its established MATCH_PARENT child.
 */
internal fun genomePreviewSurfaceGeometry(
    visibleWidthPx: Float,
    visibleHeightPx: Float,
    parentWidthPx: Float,
    parentHeightPx: Float
): GenomePreviewSurfaceGeometry = GenomePreviewSurfaceGeometry(
    viewportWidth = visibleWidthPx.roundToInt().coerceAtLeast(1),
    viewportHeight = visibleHeightPx.roundToInt().coerceAtLeast(1),
    backingWidth = parentWidthPx.roundToInt().coerceAtLeast(1),
    backingHeight = parentHeightPx.roundToInt().coerceAtLeast(1)
)

internal fun resizedPreviewExtent(
    currentExtentPx: Float,
    deltaPx: Float,
    minimumExtentPx: Float,
    maximumExtentPx: Float
): Float = (currentExtentPx + deltaPx).coerceIn(
    minimumExtentPx,
    maximumExtentPx.coerceAtLeast(minimumExtentPx)
)

internal fun constrainedPreviewExtent(
    requestedExtentPx: Float,
    parentExtentPx: Float,
    minimumExtentPx: Float,
    reservedSecondaryExtentPx: Float
): Float {
    if (parentExtentPx <= 0.0f) return requestedExtentPx.coerceAtLeast(minimumExtentPx)
    val effectiveMinimumExtentPx = minimumExtentPx.coerceAtMost(parentExtentPx)
    val maximumExtentPx = (parentExtentPx - reservedSecondaryExtentPx)
        .coerceIn(effectiveMinimumExtentPx, parentExtentPx)
    return requestedExtentPx.coerceIn(effectiveMinimumExtentPx, maximumExtentPx)
}

private const val UNSET_PREVIEW_EXTENT = -1.0f
private const val DEFAULT_LANDSCAPE_PREVIEW_FRACTION = 0.5f
private val PREVIEW_RESIZE_HANDLE_THICKNESS = 20.dp
private val PREVIEW_TOP_ACTIONS_HEIGHT = 48.dp
private val PREVIEW_BOTTOM_CONTROLS_TOP_PADDING = 4.dp
private val PREVIEW_TIME_CONTROLS_HEIGHT = 48.dp
private val MINIMUM_LANDSCAPE_PREVIEW_WIDTH = 280.dp

@Composable
private fun PreviewOption(
    label: String,
    option: GenomePreviewOption,
    state: GenomeEditorState,
    onAction: (GenomeEditorAction) -> Unit
) {
    val enabled = when (option) {
        GenomePreviewOption.GRID -> state.preview.gridEnabled
        GenomePreviewOption.TREE -> state.preview.treeEnabled
        GenomePreviewOption.GHOST -> state.preview.ghostEnabled
    }
    DefaultLabeledCheckbox(label = label, checked = enabled) {
        onAction(GenomeEditorAction.SetPreviewOption(option, it))
    }
}

@Composable
private fun PreviewTimeButton(
    label: String,
    onStep: () -> Unit,
    modifier: Modifier = Modifier
) {
    val scope = rememberCoroutineScope()
    Box(
        modifier = modifier
            .width(42.dp)
            .height(40.dp)
            .pointerInput(onStep) {
                detectTapGestures(
                    onPress = {
                        onStep()
                        val repeatJob = scope.launch {
                            delay(320)
                            while (true) {
                                onStep()
                                delay(70)
                            }
                        }
                        try {
                            tryAwaitRelease()
                        } finally {
                            repeatJob.cancel()
                        }
                    }
                )
            },
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = Color.White, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun GenomeSectionBar(
    selected: GenomeEditorSection,
    onSectionSelected: (GenomeEditorSection) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.primaryContainer)
            .padding(horizontal = 8.dp, vertical = 7.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        GenomeEditorSection.entries.forEach { section ->
            val active = section == selected
            Box(
                modifier = Modifier
                    .weight(1f)
                    .height(38.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (active) MaterialTheme.colorScheme.background else Color.Transparent)
                    .clickable { onSectionSelected(section) }
                    .padding(horizontal = 4.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = section.title(),
                    color = if (active) MaterialTheme.colorScheme.primary
                    else MaterialTheme.colorScheme.background,
                    fontWeight = if (active) FontWeight.Bold else FontWeight.SemiBold
                )
            }
        }
    }
}

@Composable
private fun GenomeEditorSection.title(): String = when (this) {
    GenomeEditorSection.GENERAL -> stringResource(Res.string.genome_editor_tab_general)
    GenomeEditorSection.SPLITTING -> stringResource(Res.string.genome_editor_tab_splitting)
    GenomeEditorSection.SPECIFIC -> stringResource(Res.string.genome_editor_tab_specific)
}

@Composable
private fun GenomeLoadDialog(
    choices: List<GenomeLoadChoice>,
    onAction: (GenomeEditorAction) -> Unit
) {
    CellularityAlertDialog(
        onDismissRequest = { onAction(GenomeEditorAction.DismissLoad) },
        title = { Text(stringResource(Res.string.genome_editor_load_dialog_title)) },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                choices.forEach { choice ->
                    Text(
                        text = when (choice) {
                            GenomeLoadChoice.Microscope ->
                                stringResource(Res.string.genome_editor_load_from_microscope_action)
                            GenomeLoadChoice.DefaultGenome ->
                                stringResource(Res.string.genome_editor_load_default_genome_action)
                            is GenomeLoadChoice.Stored -> choice.name
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onAction(GenomeEditorAction.ChooseLoad(choice)) }
                            .padding(12.dp)
                    )
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = { onAction(GenomeEditorAction.DismissLoad) }) {
                Text(stringResource(Res.string.common_cancel_action))
            }
        }
    )
}

@Composable
private fun GenomeSaveDialog(
    initialName: String,
    onAction: (GenomeEditorAction) -> Unit
) {
    var name by remember(initialName) { mutableStateOf(initialName) }
    CellularityAlertDialog(
        onDismissRequest = { onAction(GenomeEditorAction.DismissSave) },
        title = { Text(stringResource(Res.string.genome_editor_save_dialog_title)) },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                Text(stringResource(Res.string.genome_editor_save_dialog_description))
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Text),
                    modifier = Modifier.fillMaxWidth(),
                    colors = cellularityOutlinedTextFieldColors()
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onAction(GenomeEditorAction.ConfirmSave(name)) }) {
                Text(stringResource(Res.string.genome_editor_save_dialog_action))
            }
        },
        dismissButton = {
            TextButton(onClick = { onAction(GenomeEditorAction.DismissSave) }) {
                Text(stringResource(Res.string.common_cancel_button_label))
            }
        }
    )
}

@Composable
private fun GenomeModeActionDialog(
    selected: GeneIndex,
    target: GeneIndex,
    onReset: () -> Unit,
    onCopy: () -> Unit,
    onDuplicate: () -> Unit,
    onMoveBefore: (() -> Unit)?,
    onMoveAfter: (() -> Unit)?,
    onDelete: (() -> Unit)?,
    onDismiss: () -> Unit
) {
    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Text(
                stringResource(
                    Res.string.genome_editor_mode_title_format,
                    stringResource(Res.string.genome_editor_gene_mode_prefix),
                    target.value + 1
                )
            )
        },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                Text(
                    text = stringResource(
                        Res.string.genome_editor_load_default_to_mode_action_format,
                        stringResource(Res.string.genome_editor_gene_mode_prefix),
                        target.value + 1
                    ),
                    modifier = Modifier.fillMaxWidth().clickable(onClick = onReset).padding(12.dp)
                )
                Text(
                    text = stringResource(
                        Res.string.genome_editor_copy_mode_action_format,
                        stringResource(Res.string.genome_editor_gene_mode_prefix),
                        selected.value + 1,
                        target.value + 1
                    ),
                    modifier = Modifier.fillMaxWidth().clickable(onClick = onCopy).padding(12.dp)
                )
                Text(
                    text = stringResource(
                        Res.string.genome_editor_duplicate_gene_action_format,
                        stringResource(Res.string.genome_editor_gene_mode_prefix),
                        target.value + 1
                    ),
                    modifier = Modifier.fillMaxWidth().clickable(onClick = onDuplicate).padding(12.dp)
                )
                onMoveBefore?.let { action ->
                    Text(
                        text = stringResource(Res.string.genome_editor_move_gene_before_action),
                        modifier = Modifier.fillMaxWidth().clickable(onClick = action).padding(12.dp)
                    )
                }
                onMoveAfter?.let { action ->
                    Text(
                        text = stringResource(Res.string.genome_editor_move_gene_after_action),
                        modifier = Modifier.fillMaxWidth().clickable(onClick = action).padding(12.dp)
                    )
                }
                onDelete?.let { action ->
                    Text(
                        text = stringResource(Res.string.genome_editor_delete_gene_full_action),
                        color = MaterialTheme.colorScheme.error,
                        modifier = Modifier.fillMaxWidth().clickable(onClick = action).padding(12.dp)
                    )
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.common_cancel_action))
            }
        }
    )
}

@Composable
private fun GenomeDeleteGeneDialog(
    target: GeneIndex,
    genes: GenomeGeneCatalog,
    onDelete: (GeneIndex) -> Unit,
    onDismiss: () -> Unit
) {
    val initialReplacement = if (target.value > 0) target.value - 1 else 1
    var replacement by remember(target, genes.options.size) {
        mutableStateOf(GeneIndex(initialReplacement))
    }
    CellularityAlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(Res.string.genome_editor_delete_gene_dialog_title)) },
        text = {
            Column {
                Text(stringResource(Res.string.genome_editor_delete_gene_replacement_label))
                GenomeGeneDropdown(
                    genes = genes,
                    selected = replacement,
                    onSelected = { replacement = it },
                    isEnabled = { it != target }
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onDelete(replacement) }) {
                Text(stringResource(Res.string.genome_editor_delete_gene_full_action))
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(Res.string.common_cancel_action))
            }
        }
    )
}

private fun GenomeFileState.feedbackMessageResource(): StringResource? = when (this) {
    is GenomeFileState.Loaded -> Res.string.genome_editor_genome_loaded_message
    is GenomeFileState.Saved -> Res.string.file_operation_saved_message
    is GenomeFileState.Failed -> when (reason) {
        GenomeFileFailureReason.BAD_NAME -> Res.string.file_operation_invalid_name_message
        GenomeFileFailureReason.NEWER_VERSION -> Res.string.file_operation_newer_version_message
        GenomeFileFailureReason.NO_MICROSCOPE_CELL -> Res.string.microscope_no_cell_selected_message
        GenomeFileFailureReason.TOO_LARGE -> Res.string.file_operation_size_limit_exceeded_message
        GenomeFileFailureReason.WRITE_FAILED -> Res.string.file_operation_save_failed_message
        else -> Res.string.file_operation_load_failed_message
    }
    else -> null
}
