package com.cytarch.cellularity.ui.play_menu.screens.substrate_info

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.model.SimulationStats
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoGatewayController
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoState
import com.cytarch.cellularity.shared.ui.play.substrate_info.toSubstrateEnvironmentUiState
import com.cytarch.cellularity.shared.ui.play.substrate_info.toSubstrateStatsSectionState
import com.cytarch.cellularity.shared.simulation.SimulationGateway
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoScreen as SharedSubstrateInfoScreen
import java.util.Locale
import kotlinx.coroutines.launch

@Composable
fun SubstrateInfoScreen(
    state: SubstrateInfoState,
    simulationGateway: SimulationGateway,
    confirmedEnvironment: EnvironmentSnapshot?,
    stats: SimulationStats,
    isVisible: Boolean,
    renderBackendKind: RenderBackendKind,
    onRenderBackendFailure: (String) -> Unit,
    selectedTemperature: Int,
    onTemperatureSelected: (Int) -> Unit,
    onResetClick: () -> Unit
) {
    val gatewayController = remember(simulationGateway) {
        SubstrateInfoGatewayController(simulationGateway)
    }
    val coroutineScope = rememberCoroutineScope()
    val editStatus by gatewayController.editStatus.collectAsState()
    val environment = remember(confirmedEnvironment) {
        confirmedEnvironment?.toSubstrateEnvironmentUiState()
    }
    val statsSection = remember(stats) { stats.toSubstrateStatsSectionState() }

    SharedSubstrateInfoScreen(
        state = state,
        environment = environment,
        stats = statsSection,
        editStatus = editStatus,
        // B-04 is intentionally deferred; keep the isolated section visible for now.
        showExperimentalFeatures = true,
        onPlateActionClick = onResetClick,
        selectedTemperature = selectedTemperature,
        onTemperatureSelected = onTemperatureSelected,
        onCheckboxChanged = { type, checked ->
            coroutineScope.launch { gatewayController.changeCheckbox(type, checked) }
        },
        onSliderChanged = { type, newValue ->
            coroutineScope.launch { gatewayController.changeSlider(type, newValue) }
        },
        onEnvironmentFieldTypeChanged = { target, type ->
            coroutineScope.launch { gatewayController.changeFieldType(target, type) }
        },
        onEnvironmentFieldValueChanged = { target, type, newValue ->
            coroutineScope.launch { gatewayController.changeFieldValue(target, type, newValue) }
        },
        onMutationWeightChanged = { index, newValue ->
            coroutineScope.launch { gatewayController.changeMutationWeight(index, newValue) }
        },
        onMutationWeightsReset = {
            coroutineScope.launch { gatewayController.resetMutationWeights() }
        },
        onGenePoolChanged = { index, checked ->
            coroutineScope.launch { gatewayController.changeGenePool(index, checked) }
        },
        sliderValueFormatter = ::formatSliderValue,
        sliderInputValueFormatter = ::formatSliderInputValue,
        densityPreviewContent = { target, previewState, onValueChanged ->
            DensityFieldPreview(
                isVisible = isVisible,
                target = target,
                fieldState = previewState,
                backendKind = renderBackendKind,
                onRenderBackendFailure = onRenderBackendFailure,
                onValueChanged = onValueChanged
            )
        }
    )
}

private fun formatSliderValue(value: Double): String {
    return String.format(Locale.getDefault(), "%1$,.6f", value)
}

private fun formatSliderInputValue(value: Double): String {
    return String.format(Locale.US, "%.6f", value)
}
