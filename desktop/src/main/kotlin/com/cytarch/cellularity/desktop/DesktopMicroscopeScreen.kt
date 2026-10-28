/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
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
import com.cytarch.cellularity.desktop.nativebridge.MicroscopeRenderSurface
import com.cytarch.cellularity.desktop.nativebridge.SurfaceInputEvent
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeScreen
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeStore
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature

/**
 * The running simulation.
 *
 * The chrome is the shared [MicroscopeScreen], so the tool and temperature controls are
 * the same ones the phone shows. What is desktop specific is the viewport: instead of an
 * Android surface view this renders the world through the native renderer into a child
 * window inside this one.
 */
@Composable
fun DesktopMicroscopeScreen(
    configuration: SimulationConfiguration,
    onExit: () -> Unit,
    modifier: Modifier = Modifier
) {
    val store = remember { MicroscopeStore() }
    val state by store.state.collectAsState()

    // Input is handed to the surface as an immutable list rather than appended to a
    // mutable one. The surface republishes the queue whenever the list changes, and a
    // list mutated in place would look unchanged to it, so the events would never be
    // applied at all. Each change therefore allocates, which is cheap next to a frame and
    // is what makes the change observable.
    var pendingInput by remember { mutableStateOf<List<SurfaceInputEvent>>(emptyList()) }
    val queueInput: (SurfaceInputEvent) -> Unit = { event ->
        pendingInput = pendingInput + event
    }
    var cellCount by remember { mutableStateOf(0L) }
    var failure by remember { mutableStateOf<String?>(null) }
    val assetRoots = remember { DesktopAssetRoots.resolve() }

    // A mode or tool change in the interface becomes an instruction to the simulation.
    LaunchedEffect(state.selectedTemperature) {
        queueInput(SurfaceInputEvent.Temperature(state.selectedTemperature.nativeMode))
    }
    LaunchedEffect(state.selectedTool) {
        queueInput(SurfaceInputEvent.Tool(state.selectedTool.nativeOrdinal))
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) {
                    return@onPreviewKeyEvent false
                }
                when (event.key) {
                    Key.Escape -> {
                        onExit()
                        true
                    }

                    // The same keys the native host accepts, so a session is driven
                    // identically whether it runs here or standalone.
                    Key.R -> {
                        queueInput(SurfaceInputEvent.Reset)
                        true
                    }

                    Key.One -> selectTemperature(store, MicroscopeTemperature.FREEZE)
                    Key.Two -> selectTemperature(store, MicroscopeTemperature.OBSERVE)
                    Key.Three -> selectTemperature(store, MicroscopeTemperature.INCUBATE)
                    Key.Four -> selectTemperature(store, MicroscopeTemperature.SLOW)
                    Key.Five -> selectTemperature(
                        store,
                        MicroscopeTemperature.INCUBATE_NO_RENDER
                    )

                    else -> false
                }
            }
    ) {
        MicroscopeScreen(
            state = state,
            isVisible = true,
            modifier = Modifier.fillMaxSize(),
            onPlateActionClick = onExit,
            onAction = { action -> store.dispatch(action) }
        ) {
            Box(modifier = Modifier.fillMaxSize()) {
                MicroscopeRenderSurface(
                    assetRoots = assetRoots,
                    maxCellCount = configuration.maxCellCount,
                    maxFoodCount = configuration.maxFoodCount,
                    dishRadius = configuration.radius,
                    inputEvents = pendingInput,
                    onCellCount = { count -> cellCount = count },
                    onFailure = { message -> failure = message },
                    modifier = Modifier.fillMaxSize()
                )
                // A quiet readout: the phone puts these behind a diagnostics screen, but
                // on a desktop there is room to show that the world is actually running
                // rather than showing a still frame.
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
}

/**
 * Selects a temperature both in the interface and, through the queued event, in the
 * simulation. The store is the interface's view of the mode; the queue is the
 * simulation's, and they are only consistent if both are told.
 */
private fun selectTemperature(
    store: MicroscopeStore,
    temperature: MicroscopeTemperature
): Boolean {
    store.dispatch(MicroscopeAction.SelectTemperature(temperature))
    return true
}

/** The configuration a new plate starts with. */
val defaultDesktopSimulationConfiguration: SimulationConfiguration
    get() = SimulationContract.defaultConfiguration
