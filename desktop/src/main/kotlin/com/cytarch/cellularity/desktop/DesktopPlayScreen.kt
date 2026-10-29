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
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.SurfaceInputEvent
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.ui.play.PlayAction
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayScreen
import com.cytarch.cellularity.shared.ui.play.PlayTab
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeProvider
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
    fileRepository: DesktopCellularityFileRepository,
    onExit: () -> Unit,
    modifier: Modifier = Modifier,
    /**
     * A saved plate to continue, or null for a new one.
     *
     * Named by the substrate's display name rather than by a path, because that is what the
     * rest of the application calls a plate and because resolving it is the repository's
     * job: the name-to-path mapping, and the refusal of a name that would leave the storage
     * directory, belong in one place and are already there.
     */
    storedPlateName: String? = null
) {
    val microscopeStore = remember { MicroscopeStore() }
    val microscopeState by microscopeStore.state.collectAsState()

    // The path the renderer will decode. Held as state because it is read from the disk, and
    // the surface must not be created before it is known: a surface told "new plate" cannot
    // become one told "this file", so arriving late is the same as not arriving.
    var substratePath by remember(storedPlateName) { mutableStateOf<String?>(null) }
    var plateFailure by remember(storedPlateName) { mutableStateOf<String?>(null) }
    LaunchedEffect(storedPlateName) {
        val name = storedPlateName ?: return@LaunchedEffect
        when (val resolved = fileRepository.substrateFilePath(StoredSubstrateId(name))) {
            is StorageResult.Success -> substratePath = resolved.value.toString()
            is StorageResult.Failure -> plateFailure =
                "The saved plate could not be found (${resolved.error.code})."
        }
    }

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
                    // A failure to resolve the plate is reported through the same channel
                    // as a failure to open it, so a person sees one message either way
                    // rather than a screen that looks fine and never draws anything.
                    failure = failure ?: plateFailure,
                    substratePath = substratePath,
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
            genomeContent = { GenomeTab(fileRepository) }
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
 * The editor itself is shared: the same sections, the same reducer, and now the same effect
 * handler the phone uses. What differs is only where the bytes go -- a directory in the
 * user's profile rather than the app's private storage -- and that the preview is not
 * available, which the editor says rather than showing an empty box.
 *
 * The store is remembered for the life of the screen, not per tab visit, so switching to
 * the microscope and back does not throw away work in progress.
 */
@Composable
private fun GenomeTab(fileRepository: DesktopCellularityFileRepository) {
    // A scope that outlives recomposition: a save in flight must not be cancelled because
    // the screen redrew, or the state would say "saving" with nothing saving.
    val scope = rememberCoroutineScope()
    val store = remember(fileRepository) {
        DesktopGenomeEditorStore(
            repository = fileRepository,
            genomeProvider = NoMicroscopeGenome
        ).also { it.attach(scope) }
    }
    DesktopGenomeEditor(store = store, modifier = Modifier.fillMaxSize())
}

/**
 * Says that there is no genome to take from the plate.
 *
 * Selecting a cell and editing its genome is what the phone offers, and it needs a way to
 * read a cell's genome out of the running simulation, which this bridge does not have yet.
 * Answering "none" makes the editor report that as the reason the option failed instead of
 * offering a choice that would do nothing.
 */
private object NoMicroscopeGenome : GenomeProvider {
    override val genome: GenomeDraft? = null
}
