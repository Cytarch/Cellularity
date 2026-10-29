/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.MicroscopeRenderSurface
import com.cytarch.cellularity.desktop.nativebridge.SurfaceInputEvent
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeScreen
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeState
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeStore
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature

/**
 * The world, plus the readout the phone keeps behind a diagnostics screen.
 *
 * The surface stays mounted while another tab is showing. The session behind it owns the
 * Vulkan device and the simulation thread, so unmounting it to save nothing would tear
 * the run down and start a different one when the tab came back.
 */
@Composable
fun MicroscopeTab(
    state: MicroscopeState,
    isVisible: Boolean,
    assetRoots: DesktopAssetRoots.Roots?,
    configuration: SimulationConfiguration,
    pendingInput: List<SurfaceInputEvent>,
    cellCount: Long,
    failure: String?,
    substratePath: String?,
    onPlateActionClick: () -> Unit,
    onAction: (MicroscopeAction) -> Unit,
    onCellCount: (Long) -> Unit,
    onFailure: (String) -> Unit,
    onSessionReady: (Long) -> Unit
) {
    MicroscopeScreen(
        state = state,
        isVisible = isVisible,
        modifier = Modifier.fillMaxSize(),
        onPlateActionClick = onPlateActionClick,
        onAction = onAction
    ) {
        Box(modifier = Modifier.fillMaxSize()) {
            MicroscopeRenderSurface(
                assetRoots = assetRoots,
                maxCellCount = configuration.maxCellCount,
                maxFoodCount = configuration.maxFoodCount,
                dishRadius = configuration.radius,
                inputEvents = pendingInput,
                onCellCount = onCellCount,
                onFailure = onFailure,
                onSessionReady = onSessionReady,
                substratePath = substratePath,
                // The simulation is drawn by a native window, which is in front of everything
                // the interface draws. Staying mounted while another tab shows is deliberate --
                // the session owns the device and the simulation thread -- but staying visible
                // is not: it would cover whatever tab is actually open, and take the clicks
                // meant for it.
                isVisible = isVisible,
                modifier = Modifier.fillMaxSize()
            )
            // A quiet readout: the phone puts these behind a diagnostics screen, but on
            // a desktop there is room to show that the world is actually running rather
            // than showing a still frame.
            Text(
                text = buildString {
                    append("cells ")
                    append(cellCount)
                    append("   ")
                    append(state.selectedTemperature.name.lowercase())
                    append("   ")
                    append(state.selectedTool.name.lowercase())
                    append("   R reset   Esc back")
                    failure?.let { append("   ").append(it) }
                },
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .fillMaxWidth()
                    .padding(8.dp)
            )
        }
    }
}

/**
 * Selects a temperature both in the interface and, through the queued event, in the
 * simulation. The store is the interface's view of the mode; the queue is the
 * simulation's, and they are only consistent if both are told.
 */
fun selectTemperature(
    store: MicroscopeStore,
    temperature: MicroscopeTemperature
): Boolean {
    store.dispatch(MicroscopeAction.SelectTemperature(temperature))
    return true
}

/** The configuration a new plate starts with. */
val defaultDesktopSimulationConfiguration: SimulationConfiguration
    get() = SimulationContract.defaultConfiguration

