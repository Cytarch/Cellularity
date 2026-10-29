/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.fillMaxSize
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
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuStore
import kotlinx.coroutines.launch

/**
 * The desktop entry point.
 *
 * The interface itself lives in the shared module and is not duplicated here. What this
 * owns is the desktop-only layer: the window, where settings and substrates live on the
 * filesystem, and the native renderer.
 */
fun main() = application {
    val windowState = rememberWindowState(size = DpSize(1280.dp, 800.dp))

    // Process-owned, so it is created once rather than per composition. A repository
    // rebuilt on recomposition would drop the settings the user had already changed.
    val environment = remember { DesktopEnvironment.create() }

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
            DesktopPlayScreen(
                configuration = session.configuration,
                fileRepository = environment.fileRepository,
                onExit = { runningSimulation = null },
                modifier = Modifier.fillMaxSize(),
                storedPlateName = session.storedPlateName
            )
            return@Window
        }

        val mainMenuStore = remember { MainMenuStore() }
        // One queue for both collection pages, so a notice from the gene bank and one from
        // the freezer are shown the same way and cannot appear at once.
        val notices = remember { DesktopNoticeQueue() }
        val mainMenuState by mainMenuStore.state.collectAsState()
        val settings by environment.settingsRepository.settings.collectAsState()

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

        CellularityApp(
            mainMenuState = mainMenuState,
            onMainMenuAction = { action -> scope.launch { mainMenuStore.dispatch(action) } },
            // The extra top padding exists for phones with a cutout; a window has none.
            avoidCameraCutout = false,
            themeColors = settings.theme,
            themeComponents = settings.themeComponents
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