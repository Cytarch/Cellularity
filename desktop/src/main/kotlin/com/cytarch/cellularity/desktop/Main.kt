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

    // The whole window is the menu until a plate is opened, then it is the simulation.
    // The session is only created when there is somewhere to put it, which is why the
    // configuration is held here rather than inside the simulation screen.
    var runningSimulation by remember { mutableStateOf<SimulationConfiguration?>(null) }

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

        val configuration = runningSimulation
        if (configuration != null) {
            DesktopPlayScreen(
                configuration = configuration,
                onExit = { runningSimulation = null },
                modifier = Modifier.fillMaxSize()
            )
            return@Window
        }

        val mainMenuStore = remember { MainMenuStore() }
        val mainMenuState by mainMenuStore.state.collectAsState()
        val settings by environment.settingsRepository.settings.collectAsState()

        CellularityApp(
            mainMenuState = mainMenuState,
            onMainMenuAction = { action -> scope.launch { mainMenuStore.dispatch(action) } },
            // The extra top padding exists for phones with a cutout; a window has none.
            avoidCameraCutout = false,
            themeColors = settings.theme,
            themeComponents = settings.themeComponents
        ) { page, collectionRevision ->
            DesktopMainMenuPages(
                page = page,
                collectionRevision = collectionRevision,
                settings = settings,
                environment = environment,
                scope = scope,
                onOpenPlate = { request ->
                    // The freezer reports which plate to open; the launcher decides
                    // that "open" means showing the simulation in this window. The
                    // allocation comes from the request when a saved plate names one,
                    // and from the shared default otherwise.
                    runningSimulation = request.advancedConfiguration?.let {
                        SimulationConfiguration(
                            maxCellCount = it.maximumCells,
                            maxFoodCount = it.maximumFood,
                            radius = it.radius,
                            innerRadius = it.innerRadius
                        )
                    } ?: SimulationContract.defaultConfiguration
                }
            )
        }
    }
}
