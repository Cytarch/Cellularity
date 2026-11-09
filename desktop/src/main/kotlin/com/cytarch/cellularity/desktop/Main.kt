/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionFeedback
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionKind
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionNotice
import com.cytarch.cellularity.shared.ui.main_menu.FileCollectionOperation
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuEffect
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuImportedFileKind
import com.cytarch.cellularity.shared.ui.main_menu.storageNotice
import com.cytarch.cellularity.shared.model.SimulationConfiguration
import com.cytarch.cellularity.shared.ui.CellularityApp
import com.cytarch.cellularity.shared.ui.theme.CellularityTheme
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuStore
import java.nio.file.Files
import java.nio.file.Path
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

/**
 * The desktop entry point.
 *
 * The interface itself lives in the shared module and is not duplicated here. What this
 * owns is the desktop-only layer: the window, where settings and substrates live on the
 * filesystem, and the native renderer.
 *
 * [args] is how Windows hands the application a file: choosing Cellularity in "Open with", or
 * double-clicking a `.substrate` or `.genome` once the association is installed, starts the
 * process with the file's path as its argument. Nothing read them, so the gesture looked exactly
 * like an application that had forgotten what it was opened for. A path that is not a file is
 * ignored rather than reported: an argument is not necessarily a document, and a window that
 * complains about its own command line is worse than one that opens.
 */
fun main(args: Array<String>) {
    // Process-owned, so it is created once rather than per composition. A repository rebuilt on
    // recomposition would drop the settings the user had already changed. It is created before
    // the window because what it holds decides how the window speaks.
    val environment = DesktopEnvironment.create()

    val openedFiles = args
        .mapNotNull { argument -> argument.takeIf(String::isNotBlank) }
        .map(Path::of)
        .filter(Files::isRegularFile)

    // The language the interface speaks.
    //
    // On Android the stored language reaches the platform through AppCompatDelegate, which the
    // activity reads when it inflates. A desktop has no such mechanism: the shared interface asks
    // Compose for its strings, and Compose on the JVM resolves them with the default locale --
    // reading each resource file once, with whatever locale was current at that moment. Nothing
    // applied the setting, so the language picker in Settings changed a stored value and nothing
    // else, and the interface stayed in whatever language Windows was set to while the setting
    // said otherwise.
    //
    // It is applied here, before the window exists, and deliberately not on every change: because
    // each resource file keeps the strings it was first read with, rebuilding the interface after
    // a change produces a window that is half in each language. A change therefore takes effect at
    // the next launch -- a limit of the resource mechanism rather than a preference, and the
    // reason the reader is not told otherwise on screen.
    runBlocking { environment.settingsRepository.initialize() }
    val storedLanguage = environment.settingsRepository.settings.value.language
    java.util.Locale.setDefault(java.util.Locale.forLanguageTag(storedLanguage.languageTag))

    application {
    val windowState = rememberWindowState(size = DpSize(1280.dp, 800.dp))

    // The settings as the interface sees them, read here rather than inside the window: the
    // play screen is a branch of its own below.
    val settings by environment.settingsRepository.settings.collectAsState()

    // The whole window is the menu until a plate is opened, then it is the simulation. The
    // session is only created when there is somewhere to put it, which is why this is held
    // here rather than inside the simulation screen.
    //
    // Both halves are kept because they answer different questions: the configuration is
    // the allocation the renderer is given, and the plate name is what makes the run a
    // continuation of a saved experiment rather than a new one. A stored plate carries its
    // own configuration too, so this is not two ways of saying the same thing -- it is the
    // difference between "a world this shape" and "that world".
    var runningSimulation by remember { mutableStateOf<DesktopPlaySession?>(null) }


        Window(
            onCloseRequest = {
                environment.close()
                exitApplication()
            },
            state = windowState,
            title = "Cellularity"
        ) {
            val scope = rememberCoroutineScope()

            LaunchedEffect(environment) {
                environment.initialize()
            }

            val session = runningSimulation
            if (session != null) {
                // The plate is composed inside the same theme as the menu around it.
                //
                // It used to be composed outside it, because the menu brings its own theme and
                // the plate was a sibling branch rather than a child. What that looked like was
                // the experiment turning lilac -- Material's own colours -- while the settings
                // that had just been chosen were still sitting in the menu behind it. One theme
                // for both halves of the application, which is what the phone has.
                CellularityTheme(
                    themeColors = settings.theme,
                    themeComponents = settings.themeComponents
                ) {
                    DesktopPlayScreen(
                        configuration = session.configuration,
                        fileRepository = environment.fileRepository,
                        onExit = { runningSimulation = null },
                        modifier = Modifier.fillMaxSize(),
                        storedPlateName = session.storedPlateName,
                        performanceHudEnabled = settings.performanceHudEnabled,
                        askSterilize = settings.askSterilize,
                        incubateTargetUps = settings.incubateTargetUps,
                        genomePreviewMaxCells = settings.genomePreviewMaxCells
                    )
                }
                return@Window
            }

            val mainMenuStore = remember { MainMenuStore() }
            // One queue for both collection pages, so a notice from the gene bank and one from
            // the freezer are shown the same way and cannot appear at once.
            val notices = remember { DesktopNoticeQueue() }
            val mainMenuState by mainMenuStore.state.collectAsState()

            // The menu's own effects, which the launcher used to discard entirely. One of them
            // is "open this substrate", and dropping it is why tapping a saved plate did
            // nothing at all: the store had asked for the plate to be opened and nobody was
            // listening. The other two are import outcomes, which are reported like every other
            // file operation rather than passing in silence.
            LaunchedEffect(mainMenuStore, notices) {
                mainMenuStore.effects.collect { effect ->
                    when (effect) {
                        is MainMenuEffect.OpenSubstrate -> runningSimulation =
                            DesktopPlaySession(
                                configuration = SimulationContract.defaultConfiguration,
                                storedPlateName = effect.substrateId.value
                            )

                        MainMenuEffect.ImportSucceeded -> notices.post(
                            DesktopFileNotice(
                                FileCollectionNotice(
                                    operation = FileCollectionOperation.IMPORT,
                                    feedback = FileCollectionFeedback.SAVED
                                )
                            )
                        )

                        // The reason is turned into words by the shared mapping, not by a
                        // second when here: which message a storage failure deserves depends on
                        // whether it was a plate or a genome, and that mapping already exists
                        // and is already exhaustive. A copy would be a second place to be wrong.
                        is MainMenuEffect.ImportFailed -> notices.post(
                            DesktopFileNotice(
                            storageNotice(
                                kind = when (effect.fileKind) {
                                    MainMenuImportedFileKind.GENOME -> FileCollectionKind.GENOMES
                                    else -> FileCollectionKind.SUBSTRATES
                                },
                                operation = FileCollectionOperation.IMPORT,
                                error = effect.error
                            )
                            )
                        )
                    }
                }
            }

            // Files the application was opened with are brought in here, once the store that
            // reports them exists. The loop is sequential on purpose: the store takes one import
            // at a time, and the outcome of the first -- including "a plate with that name is
            // already here" -- is what the person sees before the second is attempted.
            LaunchedEffect(mainMenuStore) {
                openedFiles.forEach { source ->
                    DesktopMainMenuImport.run(
                        repository = environment.fileRepository,
                        store = mainMenuStore,
                        source = source
                    )
                }
            }

            CellularityApp(
                mainMenuState = mainMenuState,
                onMainMenuAction = { action -> scope.launch { mainMenuStore.dispatch(action) } },
                // The extra top padding exists for phones with a cutout; a window has none.
                avoidCameraCutout = false,
                themeColors = settings.theme,
                themeComponents = settings.themeComponents,
                // The window's own chooser. A file can also arrive as a command-line argument --
                // that is what "Open with" is -- and this is for the person who is already looking
                // at the menu and has a file somewhere.
                onImportRequest = {
                    val chosen = DesktopMainMenuImport.chooseFile(
                        java.awt.KeyboardFocusManager.getCurrentKeyboardFocusManager()
                            .activeWindow as? java.awt.Frame
                    )
                    if (chosen != null) {
                        scope.launch {
                            DesktopMainMenuImport.run(
                                repository = environment.fileRepository,
                                store = mainMenuStore,
                                source = chosen
                            )
                        }
                    }
                }
            ) { page, collectionRevision ->
                DesktopNoticeHost(
                    notice = notices.pending,
                    onNoticeShown = notices::clear
                ) {
                DesktopMainMenuPages(
                    page = page,
                    collectionRevision = collectionRevision,
                    settings = settings,
                    environment = environment,
                    scope = scope,
                    notices = notices,
                    onOpenPlate = { request ->
                        // The freezer reports which plate to open; the launcher decides
                        // that "open" means showing the simulation in this window. The
                        // allocation comes from the request when a saved plate names one,
                        // and from the shared default otherwise.
                        runningSimulation = DesktopPlaySession(
                            configuration = request.advancedConfiguration?.let {
                                SimulationConfiguration(
                                    maxCellCount = it.maximumCells,
                                    maxFoodCount = it.maximumFood,
                                    radius = it.radius,
                                    innerRadius = it.innerRadius
                                )
                            } ?: SimulationContract.defaultConfiguration,
                            storedPlateName = request.storedSubstrateId?.value
                        )
                    }
                )
                }
            }
        }
    }
}

/**
 * One run of the simulation, and what it is a run of.
 *
 * The configuration is what the renderer is allocated; the plate name is what makes this a
 * continuation of a saved experiment. They are not two spellings of the same thing: opening
 * a stored plate reads the world from its file, so the name is what the run actually is,
 * while the configuration is the fallback the decoder needs for a file that predates
 * recording it.
 */
data class DesktopPlaySession(
    val configuration: SimulationConfiguration,
    val storedPlateName: String? = null
)
