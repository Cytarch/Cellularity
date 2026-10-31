/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.LaunchedEffect
import kotlinx.coroutines.Dispatchers
import org.jetbrains.compose.resources.stringResource
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.isAltPressed
import androidx.compose.ui.input.key.isCtrlPressed
import androidx.compose.ui.input.key.isShiftPressed
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.unit.dp
import com.cytarch.cellularity.shared.generated.resources.*
import com.cytarch.cellularity.desktop.nativebridge.DesktopCellDiagnosis
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTool
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.ui.play.PlayAction
import com.cytarch.cellularity.shared.ui.play.PlayDialog
import com.cytarch.cellularity.shared.ui.play.PlayEffect
import com.cytarch.cellularity.shared.ui.play.PlayLaunchSource
import com.cytarch.cellularity.shared.ui.play.PlayPlateAction
import com.cytarch.cellularity.shared.ui.play.PlayRestoredUiState
import com.cytarch.cellularity.shared.ui.play.PlayScreen
import com.cytarch.cellularity.shared.ui.play.PlayStore
import com.cytarch.cellularity.shared.ui.play.PlayTab
import com.cytarch.cellularity.shared.ui.play.genome_editor.GenomeProvider
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeAction
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeStore
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import com.cytarch.cellularity.desktop.nativebridge.SubstratePreviewCapture
import kotlinx.coroutines.launch

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
    storedPlateName: String? = null,
    /**
     * Whether the renderer should draw its own performance readout into the frame.
     *
     * The setting is the user's, read from the settings the window already holds; the HUD itself
     * belongs to the renderer, so the value travels to the session rather than being drawn here.
     */
    performanceHudEnabled: Boolean = false,
    /**
     * Whether sterilising the plate asks first.
     *
     * The user's setting, the same one the phone reads, and it is asked of the machine rather than
     * answered here: the difference between a sterilise that asks and one that does not is a dialog
     * the shared screen owns.
     */
    askSterilize: Boolean = false
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
            is StorageResult.Failure -> {
                plateFailure = "The saved plate could not be found (${resolved.error.code})."
                // The same banner the phone shows when a plate cannot be prepared, because it is
                // the same situation: the screen is open and there is nothing to draw in it.
                microscopeStore.dispatch(
                    MicroscopeAction.RenderPreparationFailureObserved(true)
                )
            }
        }
    }

    // The genome editor's own state, held here rather than inside the tab, because the plate needs
    // it too: a synthesizer tap places the genome the editor is showing, which is the phone's
    // arrangement -- its play screen owns the editor state and hands the microscope the draft.
    // A store the tab owned would be invisible to the plate, which is why the plate used to place
    // the default Cell.
    val scope = rememberCoroutineScope()
    val genomeEditorStore = remember(fileRepository) {
        DesktopGenomeEditorStore(
            repository = fileRepository,
            genomeProvider = NoMicroscopeGenome
        ).also { it.attach(scope) }
    }

    /**
     * Where a saved plate's picture is taken from.
     *
     * Created here and handed both to the surface that can draw it and to the operations that need
     * it, because neither owns the other: the surface comes and goes with the screen, and a save has
     * to reach whichever surface is drawing at the moment it runs.
     */
    val previewCapture = remember { SubstratePreviewCapture() }

    // The plate's own name, when this run continues one. It is what a save replaces: the phone's
    // rule is that a plate may overwrite itself and nothing else, and without this the machine
    // would see a stored plate as a name that is already taken.
    val openedPlate = remember(storedPlateName) {
        storedPlateName?.let { StoredSubstrateId(it) }
    }

    // The machine the phone runs, with the desktop's own five operations behind it -- so a save,
    // a reset and the dirty state that follows them behave here because they *are* the phone's
    // sequence rather than a second copy of it that has to be kept in step by hand.
    val operations = remember(fileRepository, openedPlate) {
        DesktopPlaySessionOperations(
            fileRepository = fileRepository,
            openedPlate = openedPlate,
            previewCapture = previewCapture
        )
    }
    val playStore = remember(operations) {
        PlayStore(
            source = openedPlate?.let { PlayLaunchSource.Stored(it) }
                ?: PlayLaunchSource.New(configuration),
            operations = operations,
            // An experiment opens on the plate, not on the panel that describes it.
            //
            // The phone opens on the substrate tab because that is where a person sets the
            // environment up before starting, and the screen it opens on is the one they came for.
            // On a desktop the same reasoning points the other way: the simulation is the reason
            // the window exists, and the tab that shows it should be the one that is showing when
            // the window appears. Opening on the substrate panel made the first thing a person saw
            // a description of a world they had not looked at yet -- and, before the panel was
            // reachable at all, a sentence saying it could not be read.
            restoredUi = PlayRestoredUiState(selectedTab = PlayTab.MICROSCOPE)
        )
    }
    val playState by playStore.state.collectAsState()

    // What the machine says about its own operations, said out loud.
    //
    // This is the half that was missing: the desktop's store wrote its failures into a field nobody
    // read, so saving closed the dialog and said nothing at all -- neither that the plate was
    // written nor that it was refused nor why. The phone shows these as a toast; a desktop window
    // has no toast, so they are shown by the same host the launcher uses, in the same words.
    val notices = remember { DesktopNoticeQueue() }
    LaunchedEffect(playStore) {
        playStore.effects.collect { effect ->
            when (effect) {
                is PlayEffect.ShowNotice -> notices.post(DesktopPlayNotice(effect.notice))
                is PlayEffect.Finish -> onExit()
            }
        }
    }

    // The machine is told the session is starting, which is what the phone's view model does when
    // its activity comes up. Without it nothing was ever opened: the shared state stayed "not
    // started" for the whole run.
    LaunchedEffect(playStore) {
        playStore.dispatch(PlayAction.Start)
    }

    var failure by remember { mutableStateOf<String?>(null) }
    val assetRoots = remember { DesktopAssetRoots.resolve() }

    // The session handle, published by the surface once the renderer has made one. The
    // gateway needs the same session the viewport draws, because the substrate panel must
    // edit the world on screen rather than a second one behind it.
    var sessionHandle by remember { mutableStateOf(0L) }
    var gateway by remember { mutableStateOf<DesktopSimulationGateway?>(null) }
    LaunchedEffect(sessionHandle, fileRepository, openedPlate, previewCapture) {
        val handle = sessionHandle
        if (handle == 0L) return@LaunchedEffect
        val created = DesktopSimulationGateway(
            sessionHandle = handle,
            fileRepository = fileRepository,
            openedPlate = openedPlate,
            previewCapture = previewCapture
        )
        gateway = created
        // Handed to the machine before anything is asked of it: opening a plate is the machine's
        // first command, and it waits here until the renderer has a world.
        operations.attachGateway(created)
        created.start()        // The counters are re-read for as long as the session lives, rather than once at the
        // moment it opened: the substrate panel was reporting the world as it was before the
        // first update, and stayed at zero while the plate filled up. It runs here because this
        // effect is cancelled with the session, so the loop cannot outlive what it reads.
        created.pollStats()
    }

    // What the world says has changed, forwarded the way the phone forwards it.
    //
    // On the phone this is a collector over the gateway's content state, and it is not decoration:
    // sterilising is a *request* until the world reports that it happened, and the machine holds
    // that change pending in the meantime -- so without this the plate's menu and the way out would
    // quietly stop answering after a sterilise, with nothing on screen to say why. The observation
    // is what completes the request.
    LaunchedEffect(gateway, playStore) {
        val current = gateway ?: return@LaunchedEffect
        current.contentState.collect { content ->
            playStore.dispatch(PlayAction.ContentStateObserved(content))
        }
    }

    // A mode or tool change in the interface reaches the simulation through the surface, which
    // reads both from this same state. Nothing has to be queued here: the value is the
    // instruction.

    // The screen asks for the keyboard rather than hoping it arrives.
    //
    // Key events go to whatever has focus, and a window whose content is a plain Box has nothing
    // focused at all: the shortcuts then work only if something else happens to have taken focus
    // first, which is why they worked after one run of the app and not after another. A
    // FocusRequester on the root is the ordinary Compose answer and it is what the tab bar, the
    // dialogs and these shortcuts all rely on.
    val focusRequester = remember { FocusRequester() }
    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
    }

    var heldCameraKeys by remember { mutableStateOf(setOf<Key>()) }
    val cameraInputEnabled = playState.selectedTab == PlayTab.MICROSCOPE &&
        playState.dialog == PlayDialog.NONE
    LaunchedEffect(cameraInputEnabled) {
        if (!cameraInputEnabled) heldCameraKeys = emptySet()
    }

    Box(
        modifier = modifier
            .fillMaxSize()
            .focusRequester(focusRequester)
            .onFocusChanged { focus ->
                // Alt-Tab must not leave a key logically held after its release went to another
                // window. The next physical press starts a fresh movement instead.
                if (!focus.hasFocus) heldCameraKeys = emptySet()
            }
            .focusable()
            .onPreviewKeyEvent { event ->
                val cameraUpdate = if (
                    cameraInputEnabled &&
                    (!event.isCtrlPressed && !event.isAltPressed || event.type == KeyEventType.KeyUp)
                ) {
                    updatedDesktopCameraKeys(heldCameraKeys, event.key, event.type)
                } else {
                    null
                }
                when {
                    // Ctrl+Tab belongs to the experiment even when a text field has focus. Both
                    // press and release are consumed so the key cannot move focus instead.
                    event.key == Key.Tab && event.isCtrlPressed -> {
                        if (event.type == KeyEventType.KeyDown) {
                            val target = cyclePlayTab(
                                current = playState.selectedTab,
                                forward = !event.isShiftPressed
                            )
                            scope.launch { playStore.dispatch(PlayAction.SelectTab(target)) }
                        }
                        true
                    }

                    cameraUpdate != null -> {
                        heldCameraKeys = cameraUpdate
                        true
                    }

                    event.type != KeyEventType.KeyDown -> false

                    else -> when (event.key) {
                        // Escape follows the same route as the back button in the tab bar,
                        // so it asks before discarding rather than leaving outright.
                        Key.Escape -> {
                            scope.launch { playStore.dispatch(PlayAction.BackRequested) }
                            true
                        }

                        // Sterilising from the keyboard is the same request the plate's menu makes --
                        // including whether it asks first -- so the two cannot drift apart: there is
                        // one action, and the setting decides whether it asks.
                        Key.R -> {
                            if (!cameraInputEnabled) return@onPreviewKeyEvent false
                            scope.launch {
                                playStore.dispatch(
                                    PlayAction.SelectPlateAction(
                                        action = PlayPlateAction.STERILIZE,
                                        confirmationRequired = askSterilize
                                    )
                                )
                            }
                            true
                        }

                        else -> {
                            if (!cameraInputEnabled) return@onPreviewKeyEvent false
                            val temperature = desktopTemperatureShortcut(event.key)
                            val tool = desktopToolShortcut(event.key)
                            when {
                                temperature != null -> selectTemperature(microscopeStore, temperature)
                                tool != null -> selectTool(microscopeStore, tool)
                                else -> false
                            }
                        }
                    }
                }
            }
    ) {
        PlayScreen(
            state = playState,
            // Whether sterilising asks first is the user's setting, the same one the phone reads;
            // whether a run is worth asking about is the machine's, and it counts a plate dirty
            // exactly when the file no longer describes it.
            askSterilize = askSterilize,
            avoidCameraCutout = false,
            onAction = { action -> scope.launch { playStore.dispatch(action) } },
            microscopeContent = { isVisible, onPlateActionClick ->
                MicroscopeTab(
                    state = microscopeState,
                    isVisible = isVisible,
                    cameraPanDirection = desktopCameraPanDirection(heldCameraKeys),
                    assetRoots = assetRoots,
                    configuration = configuration,
                    substratePath = substratePath,
                    performanceHudEnabled = performanceHudEnabled,
                    // What a synthesizer tap places: the genome the editor is showing.
                    placementGenome = genomeEditorStore.state.preview.snapshot,
                    // Where a save takes this plate's picture from: the surface that draws it.
                    previewCapture = previewCapture,
                    onPlateActionClick = onPlateActionClick,
                    onAction = { action -> microscopeStore.dispatch(action) },
                    // The count exists for the probes that watch the plate from outside; this
                    // screen shows the plate itself, and the number of cells is read where it is
                    // displayed -- in the substrate tab.
                    onCellCount = {},
                    // A failure to prepare the plate is reported through the interface the phone
                    // reports it through, so the banner over the world is the same banner: the
                    // message is kept for the window title bar and the state drives the banner.
                    onFailure = { message ->
                        failure = message
                        microscopeStore.dispatch(
                            MicroscopeAction.RenderPreparationFailureObserved(true)
                        )
                    },
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
            genomeContent = { GenomeTab(genomeEditorStore) }
        )
    }

    // Reading the selected cell is a read of the runtime's status, and the two are published
    // together, so it is polled rather than pushed by the render thread: the render thread owns
    // the session and must not also be the thread that composes, and a status read is not a
    // frame. The interval is the phone's, because the panel is the same panel and a person
    // reading it cannot tell 250 ms from 500.
    //
    // The reads are blocking, and that is the whole reason this runs on the IO dispatcher. A
    // status read is answered by the thread that owns the session, so the caller waits for it;
    // a composition that waited would stop drawing and stop taking input, and a window that
    // stops taking input for long enough is reported by Windows as not responding. That is the
    // same shape as the fault this whole port is being debugged for, which is why it is worth
    // being explicit: nothing that waits for the runtime may run on the thread that draws.
    //
    // Only the diagnosis tool is polled. The runtime answers "what is this cell" only while
    // that tool is selected -- it publishes the flag that says so -- and asking during the
    // other tools would be asking for a document that was never written.
    LaunchedEffect(sessionHandle, microscopeState.selectedTool) {
        val handle = sessionHandle
        if (handle == 0L || microscopeState.selectedTool != MicroscopeTool.DIAGNOSIS) {
            // Leaving the tool clears the panel: the runtime stops answering, and a panel
            // still showing the last cell it described would be describing a selection that
            // is no longer being read.
            microscopeStore.dispatch(MicroscopeAction.InspectionObserved(null))
            return@LaunchedEffect
        }
        while (true) {
            val inspection = withContext(Dispatchers.IO) {
                DesktopCellDiagnosis.inspectSelectedCell(handle)
            }
            microscopeStore.dispatch(MicroscopeAction.InspectionObserved(inspection))
            delay(CELL_DIAGNOSIS_INTERVAL_MILLIS)
        }
    }
}

/**
 * Shown until the renderer has a session for the panel to read.
 *
 * The window between opening a plate and the first frame is short, but the panel has no
 * environment to show during it, and building it against one that does not exist yet
 * would mean reading a world nobody has created.
 *
 * This is not the panel. The panel is `SubstrateTab` in its own file, and it reads and edits
 * the world that is running. It used to be called `SubstrateTab` as well, and being private to
 * this file it won over the real one in the same package -- so the panel was written,
 * compiled, and never called, and the tab showed a sentence about the desktop build instead.
 * Two functions with one name in one package is a mistake the compiler is happy with, which is
 * why the name now says what it is.
 */
@Composable
private fun SubstrateTabUnavailable(isVisible: Boolean) {
    if (!isVisible) return
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        // The plate's own words for "not ready yet": the tab can be opened before the session
        // exists, and this is the same sentence the phone shows in that state rather than a
        // sentence about the desktop build, which is what used to be here.
        Text(
            text = stringResource(Res.string.substrate_environment_loading_message),
            modifier = Modifier.padding(24.dp)
        )
    }
}

/**
 * The genome panel.
 *
 * The editor itself is shared: the same sections, the same reducer, and the same effect handler
 * the phone uses. What differs is only where the bytes go -- a directory in the user's profile
 * rather than the app's private storage.
 *
 * The store belongs to the play screen rather than to this tab, because the plate places the
 * genome the editor is showing: a store this tab owned would go away with the tab and be
 * invisible to the microscope.
 */
@Composable
private fun GenomeTab(store: DesktopGenomeEditorStore) {
    DesktopGenomeEditor(store = store, modifier = Modifier.fillMaxSize())
}

/**
 * Says that there is no genome to take from the plate.
 *
 * Selecting a cell and editing its genome is what the phone offers, and it needs a way to
 * read a cell's genome out of the running simulation. Taking a genome *from* the plate is the
 * direction that is still missing: the bridge can inspect a selected Cell, but nothing polls it
 * into a value the editor's provider can answer from, and a provider that blocked the interface
 * thread on the render thread would be the freeze this port was debugged for.
 *
 * Answering "none" makes the editor report that as the reason the option failed instead of
 * offering a choice that would do nothing.
 */
private object NoMicroscopeGenome : GenomeProvider {
    override val genome: GenomeDraft? = null
}
/**
 * How often the diagnosis panel is refreshed while it is showing.
 *
 * The phone's interval, because it is the same panel and the same runtime: a status read is
 * cheap, and a person cannot tell four refreshes a second from two.
 */
private const val CELL_DIAGNOSIS_INTERVAL_MILLIS = 500L