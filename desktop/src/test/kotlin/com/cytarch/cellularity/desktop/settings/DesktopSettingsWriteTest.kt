/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.settings

import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsStore
import java.nio.file.Files
import java.nio.file.Path
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Checks that changing a setting actually changes it, all the way to the published state.
 *
 * The interface half of this is already checked by clicking a real switch through the real
 * input pipeline, and it is correct. What that leaves untested is everything between the
 * callback and the next frame: the store writes through a repository, the repository publishes
 * a state flow, and the screen reads that flow. A setting whose write is silently dropped, or
 * whose new value is published but equals the old one, produces a control that looks alive and
 * never changes -- which is exactly the shape of the report, and the reason the two halves are
 * tested separately rather than assumed to compose.
 */
class DesktopSettingsWriteTest {

    @get:Rule
    val folder = TemporaryFolder()

    private fun settingsFile(): Path = folder.root.toPath().resolve("settings.json")

    private fun store(): SettingsStore =
        SettingsStore(DesktopCellularitySettingsRepository(settingsFile()))

    @Test
    fun togglingASwitchChangesThePublishedSetting() = runBlocking {
        val store = store()
        val before = store.state.first()

        store.dispatch(SettingsAction.SetKeepOn(!before.keepOn))

        val after = store.state.first()
        assertNotEquals(
            "toggling the keep-awake switch has to change it",
            before.keepOn,
            after.keepOn
        )
        assertTrue(
            "the settings document has to exist after a change",
            Files.isRegularFile(settingsFile())
        )
    }

    @Test
    fun everySwitchOnTheSettingsScreenCanBeChanged() = runBlocking {
        val store = store()
        val before = store.state.first()

        store.dispatch(SettingsAction.SetPerformanceHudEnabled(!before.performanceHudEnabled))
        store.dispatch(SettingsAction.SetAskSterilize(!before.askSterilize))
        store.dispatch(SettingsAction.SetKeepOn(!before.keepOn))

        val after = store.state.first()
        assertNotEquals(
            "the performance overlay switch did not change",
            before.performanceHudEnabled,
            after.performanceHudEnabled
        )
        assertNotEquals(
            "the confirm-before-sterilize switch did not change",
            before.askSterilize,
            after.askSterilize
        )
        assertNotEquals(
            "the keep-awake switch did not change",
            before.keepOn,
            after.keepOn
        )
    }

    /**
     * A change has to survive being read back, or the interface would show it until the next
     * load and then quietly forget it.
     */
    @Test
    fun aChangedSettingSurvivesAReload() = runBlocking {
        val file = settingsFile()
        val first = SettingsStore(DesktopCellularitySettingsRepository(file))
        val before = first.state.first()
        first.dispatch(SettingsAction.SetKeepOn(!before.keepOn))
        val written = first.state.first().keepOn

        val second = DesktopCellularitySettingsRepository(file)
        second.initialize()
        assertEquals(
            "the setting read back after a reload has to be the one that was written",
            written,
            second.settings.value.keepOn
        )
    }

    /**
     * The published flow has to actually emit.
     *
     * A state flow only notifies when the value it is handed differs from the one it holds, so
     * a setting that is written but published as an equal value would leave the interface
     * showing the old one while the file says otherwise. Reading the value again would not
     * catch it; waiting for an emission does.
     */
    @Test
    fun theStateFlowEmitsWhenASwitchChanges() = runBlocking {
        val store = store()
        val before = store.state.first()
        val target = !before.keepOn

        var observed: CellularitySettings? = null
        kotlinx.coroutines.coroutineScope {
            val reader = launch {
                observed = store.state.first { it.keepOn == target }
            }
            store.dispatch(SettingsAction.SetKeepOn(target))
            kotlinx.coroutines.withTimeout(5_000) { reader.join() }
        }

        assertEquals(
            "the published state has to carry the change, not only the file",
            target,
            observed?.keepOn
        )
    }
}
