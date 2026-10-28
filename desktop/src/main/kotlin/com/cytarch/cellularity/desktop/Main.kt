/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.cytarch.cellularity.shared.ui.CellularityApp
import com.cytarch.cellularity.shared.ui.main_menu.MainMenuPage
import kotlinx.coroutines.launch

fun main() = application {
    val windowState = rememberWindowState(size = DpSize(1280.dp, 800.dp))
    Window(
        onCloseRequest = ::exitApplication,
        state = windowState,
        title = "Cellularity"
    ) {
        val scope = rememberCoroutineScope()

        LaunchedEffect(Unit) {
            scope.launch { }
        }

        CellularityApp(
            mainMenuState = com.cytarch.cellularity.shared.ui.main_menu.MainMenuState(),
            onMainMenuAction = { },
            avoidCameraCutout = false,
            modifier = Modifier.fillMaxSize()
        ) { _, _ -> }
    }
}
