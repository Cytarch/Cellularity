package com.cytarch.cellularity.ui.play_menu

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.cytarch.cellularity.platform.files.AndroidCellularityFileRepository
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.settings.AndroidDisplaySettings
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.play.PlayAction
import com.cytarch.cellularity.shared.ui.play.PlayState
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoState
import com.cytarch.cellularity.ui.play_menu.screens.genome_editor.GenomeEditorCoordinator
import com.cytarch.cellularity.ui.play_menu.screens.genome_editor.GenomeEditorScreen
import com.cytarch.cellularity.ui.play_menu.screens.genome_editor.GenomeEditorViewModel
import com.cytarch.cellularity.ui.play_menu.screens.genome_editor.GenomeProvider
import com.cytarch.cellularity.ui.play_menu.screens.microscope.MicroscopeScreen
import com.cytarch.cellularity.ui.play_menu.screens.substrate_info.SubstrateInfoScreen
import com.cytarch.cellularity.shared.ui.play.PlayScreen as SharedPlayScreen

/** Android composition adapter. It receives state and explicit dependencies, never an Activity. */
@Composable
internal fun PlayContent(
    state: PlayState,
    viewModel: PlayActivityViewModel,
    genomeEditorViewModel: GenomeEditorViewModel,
    fileRepository: AndroidCellularityFileRepository,
    settings: CellularitySettings,
    displaySettings: AndroidDisplaySettings,
    phoneWidthMeter: Float,
    renderBackendKind: RenderBackendKind?,
    onRenderBackendFailure: (String) -> Unit
) {
    val gateway = remember(viewModel) { viewModel.requireSimulationGateway() }
    val microscopeSurfaceFactory = remember(
        viewModel,
        renderBackendKind,
        onRenderBackendFailure
    ) {
        renderBackendKind?.let { backend ->
            viewModel.requireMicroscopeSurfaceFactory(
                backendKind = backend,
                onRuntimeFailure = onRenderBackendFailure
            )
        }
    }
    val stats by gateway.stats.collectAsStateWithLifecycle()
    val confirmedEnvironment by gateway.environment.collectAsStateWithLifecycle()
    val microscopeState by viewModel.microscopeState.collectAsStateWithLifecycle()
    val substrateInfoState = remember { SubstrateInfoState() }
    val onMicroscopeAction = remember(viewModel) {
        { action: MicroscopeAction -> viewModel.dispatchMicroscope(action) }
    }
    val genomeEditorState by genomeEditorViewModel.state.collectAsStateWithLifecycle()
    val genomeProvider = remember(viewModel) {
        object : GenomeProvider {
            override val genome: GenomeDraft?
                get() = viewModel.selectedGenome()
        }
    }
    val genomeEditorCoordinator = remember(fileRepository, genomeProvider) {
        GenomeEditorCoordinator(fileRepository, genomeProvider)
    }

    BackHandler { viewModel.dispatch(PlayAction.BackRequested) }

    // Keep screen-owned state above this handoff boundary. During a backend
    // change the old Android surface must leave the window for one frame, but
    // an open field editor and unsaved genome state must not be discarded with
    // it.
    if (microscopeSurfaceFactory == null) {
        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            CircularProgressIndicator()
        }
        return
    }

    var genomePreviewReady by remember(microscopeSurfaceFactory) {
        mutableStateOf(false)
    }

    SharedPlayScreen(
        state = state,
        askSterilize = settings.askSterilize,
        avoidCameraCutout = displaySettings.avoidCameraCutout,
        genomeContentReady = genomePreviewReady,
        onAction = viewModel::dispatch,
        microscopeContent = { isVisible, onPlateActionClick ->
            Box(modifier = Modifier.fillMaxSize()) {
                MicroscopeScreen(
                    state = microscopeState,
                    surfaceFactory = microscopeSurfaceFactory,
                    placementGenome = genomeEditorState.preview.snapshot,
                    phoneWidthMeter = phoneWidthMeter,
                    isVisible = isVisible,
                    incubateTargetUps = settings.incubateTargetUps,
                    performanceHudEnabled = settings.performanceHudEnabled,
                    onPlateActionClick = onPlateActionClick,
                    onAction = onMicroscopeAction
                )
            }
        },
        substrateContent = { isVisible, onPlateActionClick ->
            SubstrateInfoScreen(
                state = substrateInfoState,
                simulationGateway = gateway,
                confirmedEnvironment = confirmedEnvironment,
                stats = stats,
                isVisible = isVisible,
                renderBackendKind = microscopeSurfaceFactory.backendKind,
                onRenderBackendFailure = onRenderBackendFailure,
                selectedTemperature = microscopeState.selectedTemperature.nativeMode,
                onTemperatureSelected = { mode ->
                    MicroscopeTemperature.fromNativeMode(mode)?.let { temperature ->
                        onMicroscopeAction(MicroscopeAction.SelectTemperature(temperature))
                    }
                },
                onResetClick = onPlateActionClick
            )
        },
        genomeContent = {
            GenomeEditorScreen(
                viewModel = genomeEditorViewModel,
                coordinator = genomeEditorCoordinator,
                previewMaxCells = settings.genomePreviewMaxCells,
                renderBackendKind = microscopeSurfaceFactory.backendKind,
                onRenderBackendFailure = onRenderBackendFailure,
                onPreviewReadyChanged = { genomePreviewReady = it }
            )
        }
    )
}
