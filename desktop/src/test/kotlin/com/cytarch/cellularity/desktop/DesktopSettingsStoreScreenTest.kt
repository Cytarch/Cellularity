/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.collectAsState
import com.cytarch.cellularity.desktop.settings.DesktopCellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsStore
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen
import java.nio.file.Files
import java.nio.file.Path
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Drives the store and the screen together, the way the settings page does.
 *
 * Each part of the switch has now been checked on its own and each part works: the control is
 * drawn differently in its two states, a press on it where it is drawn reports the action, the
 * reducer turns that action into the opposite value, the store writes through the repository and
 * publishes what the repository holds, and the repository the launcher actually uses accepts the
 * write in the directory the launcher actually chose.
 *
 * None of that is the same as the arrangement the application is in. The page does not hold the
 * settings itself -- it collects them from the store, and the store's state is the repository's
 * state, so a link that is missing anywhere along that chain leaves the screen drawing a value
 * that never changes while every part of the chain passes its own test. That arrangement is what
 * this reproduces: one repository, one store, and a screen that collects from it.
 *
 * The observation is taken from the screen's own reading rather than from the store's, because
 * the claim being checked is that the interface has something new to draw.
 */
class DesktopSettingsStoreScreenTest {

    @get:Rule
    val folder = TemporaryFolder()

    @Test
    fun aChangeMadeThroughTheStoreReachesWhatTheScreenCollects() {
        val settingsFile: Path = folder.newFolder("settings").toPath().resolve("settings.json")
        val repository = DesktopCellularitySettingsRepository(settingsFile)
        val store = SettingsStore(repository)
        runBlocking { repository.initialize() }

        var observed: CellularitySettings? = null
        val scope = kotlinx.coroutines.CoroutineScope(kotlinx.coroutines.Dispatchers.Default)
        val scene = androidx.compose.ui.ImageComposeScene(
            width = 900,
            height = 700,
            density = androidx.compose.ui.unit.Density(1f)
        )
        try {
            scene.setContent {
                MaterialTheme {
                    val collected by store.state.collectAsState()
                    observed = collected
                    SettingsScreen(
                        state = collected,
                        onAction = { action -> scope.launch { store.dispatch(action) } }
                    )
                }
            }
            scene.render()
            val before = observed
            assertTrue("the screen never read a state to draw", before != null)
            println("STORE the screen starts by drawing keepOn=${before!!.keepOn}")

            // Dispatched the way a click does, through the same handler the screen is given.
            runBlocking { store.dispatch(com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction.SetKeepOn(!before.keepOn)) }

            // Drawn again, which is when a collected value is read.
            scene.render()
            val after = observed
            println("STORE the screen then draws keepOn=${after?.keepOn}")
            assertTrue(
                "the store was dispatched and the repository accepted it, but the screen is " +
                    "still drawing keepOn=${after?.keepOn}: the interface has nothing new to " +
                    "draw, so the switch cannot appear to move",
                after?.keepOn == !before.keepOn
            )
        } finally {
            scene.close()
            scope.cancel()
        }
    }

    private fun kotlinx.coroutines.CoroutineScope.cancel() {
        kotlinx.coroutines.CoroutineScope::class
        this.coroutineContext[kotlinx.coroutines.Job]?.cancel()
    }
}
