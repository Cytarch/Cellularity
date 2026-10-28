/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import com.cytarch.cellularity.shared.model.EnvironmentFieldType
import com.cytarch.cellularity.shared.ui.play.substrate_info.DensityFieldValueType
import com.cytarch.cellularity.shared.ui.play.substrate_info.EnvironmentFieldTarget
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoGatewayController
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoScreen
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoState
import com.cytarch.cellularity.shared.ui.play.substrate_info.toSubstrateEnvironmentUiState
import com.cytarch.cellularity.shared.ui.play.substrate_info.toSubstrateStatsSectionState
import java.util.Locale
import kotlinx.coroutines.launch

/**
 * The substrate panel, reading and editing the world that is actually running.
 *
 * Every control maps to a typed change that goes to the simulation as a patch against the
 * revision the panel last saw, so an edit made against a stale reading is refused rather
 * than applied to a world that has moved on. None of that lives here: the shared
 * controller owns it, and this only decides when to call it.
 */
@Composable
fun SubstrateTab(
    gateway: DesktopSimulationGateway,
    isVisible: Boolean,
    plateRadius: Double,
    selectedTemperature: Int,
    onTemperatureSelected: (Int) -> Unit,
    onPlateActionClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    // The screen is built even when another tab is showing, because it is cheap and
    // rebuilding it would restart the section and scroll positions the user had set.
    if (!isVisible) return

    val state = remember { SubstrateInfoState() }
    val controller = remember(gateway) { SubstrateInfoGatewayController(gateway) }
    val scope = rememberCoroutineScope()

    // Only a confirmed reading is shown. The interface's own flow always has a value,
    // because the shared screens require one, so the absence is taken from here instead;
    // the panel then shows its loading state rather than zeroes that look like data.
    val confirmed by gateway.confirmedEnvironment.collectAsState()
    val stats by gateway.stats.collectAsState()
    val editStatus by controller.editStatus.collectAsState()

    val environment = remember(confirmed) { confirmed?.toSubstrateEnvironmentUiState() }
    val statsSection = remember(stats, plateRadius) {
        // The summary header shows how full the dish is, which it cannot know from the
        // simulation alone because the capacity is a property of the run.
        stats.toSubstrateStatsSectionState()
    }

    SubstrateInfoScreen(
        state = state,
        environment = environment,
        stats = statsSection,
        editStatus = editStatus,
        // The experimental section is deferred work on the phone too, and the shared
        // screen keeps it visible in isolation; the desktop follows that.
        showExperimentalFeatures = true,
        onPlateActionClick = onPlateActionClick,
        selectedTemperature = selectedTemperature,
        onTemperatureSelected = onTemperatureSelected,
        onCheckboxChanged = { type, checked ->
            scope.launch { controller.changeCheckbox(type, checked) }
        },
        onSliderChanged = { type, value ->
            scope.launch { controller.changeSlider(type, value) }
        },
        onEnvironmentFieldTypeChanged = { target: EnvironmentFieldTarget, type ->
            scope.launch { controller.changeFieldType(target, type) }
        },
        onEnvironmentFieldValueChanged = { target, component: DensityFieldValueType, value ->
            scope.launch { controller.changeFieldValue(target, component, value) }
        },
        onMutationWeightChanged = { index, value ->
            scope.launch { controller.changeMutationWeight(index, value) }
        },
        onMutationWeightsReset = {
            scope.launch { controller.resetMutationWeights() }
        },
        onGenePoolChanged = { index, checked ->
            scope.launch { controller.changeGenePool(index, checked) }
        },
        sliderValueFormatter = { value -> String.format(Locale.getDefault(), "%1$,.6f", value) },
        sliderInputValueFormatter = { value -> String.format(Locale.US, "%.6f", value) },
        modifier = modifier.fillMaxSize()
    )
}
