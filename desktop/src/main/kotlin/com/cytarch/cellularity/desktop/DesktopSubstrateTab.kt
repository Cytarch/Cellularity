/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.ui.Alignment
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
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
import com.cytarch.cellularity.shared.ui.text.formatCanonicalQuantity
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
    // Everything the screen needs is created before the visibility check, and the check
    // decides only whether the screen is called.
    //
    // Returning early used to happen before these, which read as "skip the work while the
    // tab is hidden" but was not that: a composable that returns on one pass and calls a
    // composable on the next changes how many groups the composition emits at the same
    // position, and Compose has no way to reconcile that. It shows up as a crash when the
    // tab is first opened, or worse, as the hidden tab's state being silently discarded.
    // The state is kept across visits on purpose -- see below -- and keeping it means
    // creating it on every visit.
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

    if (!isVisible) return

    // When there is no world to show, say why rather than showing a loading message that implies
    // one is coming. A panel that says "loading" forever and a panel that failed are identical to
    // the code and completely different to a person -- and the difference is the whole of what is
    // needed to tell a slow read from an impossible one. The reason comes from the gateway, which
    // is the only place that saw the answer.
    val readFailure = gateway.environmentReadFailure
    if (environment == null && readFailure != null) {
        Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.padding(24.dp)
            ) {
                Text(
                    text = "The substrate environment could not be read",
                    textAlign = TextAlign.Center
                )
                Text(text = readFailure, textAlign = TextAlign.Center)
            }
        }
        return
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
        // Both of these are quantities in a field that is read back, so both are written
        // the one way the parser accepts. Formatting the display through the machine's
        // locale would write 0,500000 on a machine that uses a comma and the value would
        // be refused the moment it was edited.
        sliderValueFormatter = ::formatCanonicalQuantity,
        sliderInputValueFormatter = ::formatCanonicalQuantity,
        modifier = modifier.fillMaxSize()
    )
}
