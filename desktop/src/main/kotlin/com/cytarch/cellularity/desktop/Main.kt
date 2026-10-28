/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
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

    Window(
        onCloseRequest = {
            environment.close()
            exitApplication()
        },
        state = windowState,
        title = "Cellularity"
    ) {
        val scope = rememberCoroutineScope()
        val mainMenuStore = remember { MainMenuStore() }
        val mainMenuState by mainMenuStore.state.collectAsState()
        val settings by environment.settingsRepository.settings.collectAsState()

        LaunchedEffect(environment) {
            environment.initialize()
        }

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
                scope = scope
            )
        }
    }
}
