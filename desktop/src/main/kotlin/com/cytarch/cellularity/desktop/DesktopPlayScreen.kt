/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.SurfaceInputEvent
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.ui.play.PlayAction
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayScreen
import com.cytarch.cellularity.shared.ui.play.PlayTab
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeStore
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature

/**
 * The running simulation and the panels beside it.
 *
 * The chrome is the shared [PlayScreen], so the tab bar, the plate menu and the dialogs
 * are the ones the phone shows rather than a desktop approximation. What is desktop
 * specific is the viewport: the world renders through the native renderer into a child
 * window inside this one instead of into an Android surface.
 */
@Composable
fun DesktopPlayScreen(
    configuration: SimulationConfiguration,
    onExit: () -> Unit,
    modifier: Modifier = Modifier
) {
    val microscopeStore = remember { MicroscopeStore() }
    val microscopeState by microscopeStore.state.collectAsState()

    // Input is handed to the surface as an immutable list rather than appended to a
    // mutable one. The surface republishes the queue whenever the list changes, and a
    // list mutated in place would look unchanged to it, so the events would never be
    // applied at all.
    var pendingInput by remember { mutableStateOf<List<SurfaceInputEvent>>(emptyList()) }
    val queueInput: (SurfaceInputEvent) -> Unit = { event ->
        pendingInput = pendingInput + event
    }

    // The store resets the world by queueing a reset for the renderer to apply, because
    // the world belongs to the renderer's thread and not to the interface.
    val playStore = remember {
        DesktopPlayStore(source = PlayLaunchSource.New(configuration)) {
            pendingInput = pendingInput + SurfaceInputEvent.Reset
        }
    }
    val playState = playStore.state

    var cellCount by remember { mutableStateOf(0L) }
    var failure by remember { mutableStateOf<String?>(null) }
    val assetRoots = remember { DesktopAssetRoots.resolve() }

    // The session handle, published by the surface once the renderer has made one. The
    // gateway needs the same session the viewport draws, because the substrate panel must
    // edit the world on screen rather than a second one behind it.
    var sessionHandle by remember { mutableStateOf(0L) }
    var gateway by remember { mutableStateOf<DesktopSimulationGateway?>(null) }
    LaunchedEffect(sessionHandle) {
        val handle = sessionHandle
        if (handle == 0L) return@LaunchedEffect
        val created = DesktopSimulationGateway(sessionHandle = handle)
        gateway = created
        created.start()
    }

    // A mode or tool change in the interface becomes an instruction to the simulation.
    LaunchedEffect(microscopeState.selectedTemperature) {
        queueInput(SurfaceInputEvent.Temperature(microscopeState.selectedTemperature.nativeMode))
    }
    LaunchedEffect(microscopeState.selectedTool) {
        queueInput(SurfaceInputEvent.Tool(microscopeState.selectedTool.nativeOrdinal))
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) {
                    return@onPreviewKeyEvent false
                }
                when (event.key) {
                    // Escape follows the same route as the back button in the tab bar,
                    // so it asks before discarding rather than leaving outright.
                    Key.Escape -> {
                        playStore.dispatch(PlayAction.BackRequested)
                        true
                    }

                    Key.R -> {
                        queueInput(SurfaceInputEvent.Reset)
                        true
                    }

                    Key.One -> selectTemperature(
                        microscopeStore,
                        MicroscopeTemperature.FREEZE
                    )
                    Key.Two -> selectTemperature(
                        microscopeStore,
                        MicroscopeTemperature.OBSERVE
                    )
                    Key.Three -> selectTemperature(
                        microscopeStore,
                        MicroscopeTemperature.INCUBATE
                    )
                    Key.Four -> selectTemperature(microscopeStore, MicroscopeTemperature.SLOW)
                    Key.Five -> selectTemperature(
                        microscopeStore,
                        MicroscopeTemperature.INCUBATE_NO_RENDER
                    )

                    else -> false
                }
            }
    ) {
        PlayScreen(
            state = playState,
            // Sterilising a substrate that has been changed is the case that asks; a
            // desktop run is never dirty until saving exists, so nothing asks yet.
            askSterilize = false,
            avoidCameraCutout = false,
            onAction = { action ->
                if (action == PlayAction.ConfirmLeave) {
                    onExit()
                } else {
                    playStore.dispatch(action)
                }
            },
            microscopeContent = { isVisible, onPlateActionClick ->
                MicroscopeTab(
                    state = microscopeState,
                    isVisible = isVisible,
                    assetRoots = assetRoots,
                    configuration = configuration,
                    pendingInput = pendingInput,
                    cellCount = cellCount,
                    failure = failure,
                    onPlateActionClick = onPlateActionClick,
                    onAction = { action -> microscopeStore.dispatch(action) },
                    onCellCount = { count -> cellCount = count },
                    onFailure = { message -> failure = message },
                    onSessionReady = { handle -> sessionHandle = handle }
                )
            },
            substrateContent = { isVisible, onPlateActionClick ->
                val current = gateway
                if (current == null) {
                    SubstrateTabUnavailable(isVisible)
                } else {
                    SubstrateTab(
                        gateway = current,
                        isVisible = isVisible,
                        plateRadius = configuration.radius,
                        selectedTemperature = microscopeState.selectedTemperature.nativeMode,
                        onTemperatureSelected = { mode ->
                            MicroscopeTemperature.fromNativeMode(mode)?.let { temperature ->
                                microscopeStore.dispatch(
                                    MicroscopeAction.SelectTemperature(temperature)
                                )
                            }
                        },
                        onPlateActionClick = onPlateActionClick
                    )
                }
            },
            genomeContent = { GenomeTab() }
        )
    }
}

/**
 * Shown until the renderer has a session for the panel to read.
 *
 * The window between opening a plate and the first frame is short, but the panel has no
 * environment to show during it, and building it against one that does not exist yet
 * would mean reading a world nobody has created.
 */
@Composable
private fun SubstrateTabUnavailable(isVisible: Boolean) {
    if (!isVisible) return
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Text(
            text = "Waiting for the simulation to start.",
            modifier = Modifier.padding(24.dp)
        )
    }
}

/**
 * The substrate panel.
 *
 * The screen needs a confirmed environment snapshot, which arrives over the native
 * environment contract. Until the desktop bridge reads it, this says so instead of
 * showing the loading message forever, which is what the screen would otherwise do and
 * would look like a hang rather than a gap.
 */
@Composable
private fun SubstrateTab(isVisible: Boolean, onPlateActionClick: () -> Unit) {
    if (!isVisible) return
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Text(
            text = "The substrate environment is not readable from the desktop build yet.",
            modifier = Modifier.padding(24.dp)
        )
    }
}

/**
 * The genome panel.
 *
 * The editor itself is shared: the same sections and the same reducer the phone uses. What
 * the phone adds around them -- a native preview and a file repository -- is not here, and
 * the editor says so rather than quietly discarding a genome it cannot save.
 *
 * The store is remembered for the life of the screen, not per tab visit, so switching to
 * the microscope and back does not throw away work in progress.
 */
@Composable
private fun GenomeTab() {
    val store = remember { DesktopGenomeEditorStore() }
    DesktopGenomeEditor(store = store, modifier = Modifier.fillMaxSize())
}
