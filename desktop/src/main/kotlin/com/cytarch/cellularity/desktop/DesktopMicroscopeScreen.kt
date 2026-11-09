/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.MicroscopeRenderSurface
import com.cytarch.cellularity.desktop.nativebridge.SubstratePreviewCapture
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeScreen
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeState
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeStore
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTool

/**
 * The world and the interface that floats over it, arranged the way the phone arranges them.
 *
 * This is the phone's screen, unchanged: the world fills the viewport, the controls and the zoom
 * label sit on top of it, the diagnosis panel slides in over it, and the dialogs are drawn over
 * everything. That is possible here for the same reason it is possible there -- the world is
 * composited below the interface -- but the mechanism differs, because Windows has no compositor
 * that will do it for us. The desktop draws the world as one image inside the interface's own
 * scene, which puts it underneath by construction rather than by luck; the surface file explains
 * why a window of its own could never have worked.
 *
 * The session behind the surface stays mounted while another tab is showing. It owns the Vulkan
 * device and the simulation's world, so unmounting it to save nothing would tear the run down
 * and start a different one when the tab came back.
 */
@Composable
fun MicroscopeTab(
    state: MicroscopeState,
    isVisible: Boolean,
    /** Held WASD keys translated to the same screen delta used by a pointer pan. */
    cameraPanDirection: Offset = Offset.Zero,
    assetRoots: DesktopAssetRoots.Roots?,
    configuration: SimulationConfiguration,
    substratePath: String?,
    // The performance HUD describes the renderer, so the renderer draws it and the setting has
    // to reach the session; the phone sends the same flag from the same shared state.
    performanceHudEnabled: Boolean = false,
    /**
     * The genome the editor is showing, which is what a synthesizer tap places.
     *
     * The phone hands its microscope screen the editor's current draft for exactly this reason;
     * the desktop was handing it nothing, so a tap on empty medium placed the plate's own default
     * Cell -- a creature nobody had built.
     */
    placementGenome: GenomeDraft?,
    /**
     * Where a save of this plate takes its picture from, or null when nothing is saving.
     *
     * Passed down rather than reached for: the surface is the only thing that may draw the prepared
     * preview frame, and the save that needs it lives in a different part of the screen.
     */
    previewCapture: SubstratePreviewCapture? = null,
    onPlateActionClick: () -> Unit,
    onAction: (MicroscopeAction) -> Unit,
    onCellCount: (Long) -> Unit,
    onFailure: (String) -> Unit,
    onSessionReady: (Long) -> Unit
) {
    // Encoded once per draft rather than once per tap: the wire form is what the renderer reads,
    // and a genome that has not changed has no reason to be encoded again. The phone does the same
    // and for the same reason.
    val placementGenomeBytes = remember(placementGenome) {
        placementGenome?.let(GenomeDraftWireCodec::encode)
    }
    MicroscopeScreen(
        state = state,
        isVisible = isVisible,
        modifier = Modifier.fillMaxSize(),
        onPlateActionClick = onPlateActionClick,
        onAction = onAction,
        viewportContent = {
            MicroscopeRenderSurface(
                assetRoots = assetRoots,
                maxCellCount = configuration.maxCellCount,
                maxFoodCount = configuration.maxFoodCount,
                dishRadius = configuration.radius,
                // The mode and the tool are the interface's, and the simulation is only
                // consistent with them while it has been told. Both values are the same numbers
                // the phone sends, because they are read from the same shared state.
                temperatureMode = state.selectedTemperature.nativeMode,
                toolOrdinal = state.selectedTool.nativeOrdinal,
                substratePath = substratePath,
                performanceHudEnabled = performanceHudEnabled,
                placementGenomeBytes = placementGenomeBytes,
                previewCapture = previewCapture,
                // The picture stops being presented while another tab shows, and the world does
                // not: the session keeps running, so coming back costs a frame rather than a
                // restart.
                isVisible = isVisible,
                cameraPanDirection = cameraPanDirection,
                onCellCount = onCellCount,
                onFailure = onFailure,
                onSessionReady = onSessionReady,
                modifier = Modifier.fillMaxSize()
            )
        }
    )
}

/**
 * Selects a temperature both in the interface and, through the surface, in the simulation.
 *
 * The store is the interface's view of the mode; the surface is the simulation's, and it reads
 * the mode from the same state, so dispatching here is all it takes for both to agree.
 */
fun selectTemperature(
    store: MicroscopeStore,
    temperature: MicroscopeTemperature
): Boolean {
    store.dispatch(MicroscopeAction.SelectTemperature(temperature))
    return true
}

/** Selects a tool, for the same reason and by the same route as [selectTemperature]. */
fun selectTool(store: MicroscopeStore, tool: MicroscopeTool): Boolean {
    store.dispatch(MicroscopeAction.SelectTool(tool))
    return true
}

/** The configuration a new plate starts with. */
val defaultDesktopSimulationConfiguration: SimulationConfiguration
    get() = SimulationContract.defaultConfiguration
