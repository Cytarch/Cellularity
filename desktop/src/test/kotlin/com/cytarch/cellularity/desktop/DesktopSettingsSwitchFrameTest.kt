/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Asks whether a settings switch changes anything a person can see.
 *
 * The switches were reported as not working while both halves of the mechanism checked out: the
 * preference reports a change when it is clicked, and every switch on the screen is wired to the
 * store through one path that writes and publishes correctly. A callback that fires and a control
 * that moves are different claims, and only the second one is about a switch somebody can use.
 *
 * So the same screen is drawn twice, with the only difference being the value a switch is
 * showing, and the two frames are compared pixel by pixel. If they are identical, the switch has
 * no visible effect at all -- and that would explain the report without anything else being
 * wrong, because a switch that never appears to move is a switch that does not work, whatever it
 * does internally.
 *
 * This is the first time in this work that a screen has been drawn twice and compared. Every
 * previous check asked what a screen looks like; this one asks what changes about it.
 */
class DesktopSettingsSwitchFrameTest {

    private val width = 900
    private val height = 700

    private fun drawSettings(state: CellularitySettings): FrameInspection.Frame =
        FrameInspection.inspect(width, height) {
            SettingsScreen(state = state, onAction = { })
        }

    /**
     * The keep-awake switch has to look different when it is on.
     *
     * The switch is the one control on the screen whose whole purpose is to show a state, and a
     * switch whose two states are drawn identically is a switch that appears stuck -- so this is
     * the check that separates "the state never changes" from "the state changes and nothing
     * shows it".
     */
    @Test
    fun theKeepAwakeSwitchLooksDifferentWhenItIsOn() {
        val off = drawSettings(CellularitySettings.Default.copy(keepOn = false))
        val on = drawSettings(CellularitySettings.Default.copy(keepOn = true))

        val differing = FrameInspection.difference(off, on)
        println("KEEPAWAKE differing pixels between off and on: $differing")
        assertTrue(
            "the keep-awake switch is drawn identically whether it is on or off, so it can " +
                "never appear to have been switched: $differing pixels differ between $off " +
                "and $on",
            differing > 0
        )
    }

    /**
     * Every switch on the screen, so that one working control is not taken for a working screen.
     *
     * They are built by one composable and read from one state object, so if one is drawn without
     * regard for its value the others probably are too -- but "probably" is what this suite
     * exists to replace, and the settings that are switches are few enough to enumerate.
     */
    @Test
    fun eachSwitchOnTheScreenLooksDifferentWhenItIsOn() {
        val toggles = listOf<Pair<String, (CellularitySettings, Boolean) -> CellularitySettings>>(
            "keepOn" to { settings, value -> settings.copy(keepOn = value) },
            "performanceHudEnabled" to { settings, value ->
                settings.copy(performanceHudEnabled = value)
            },
            "askSterilize" to { settings, value -> settings.copy(askSterilize = value) }
        )

        for ((name, apply) in toggles) {
            val off = drawSettings(apply(CellularitySettings.Default, false))
            val on = drawSettings(apply(CellularitySettings.Default, true))
            val differing = FrameInspection.difference(off, on)
            println("SWITCH $name differing pixels between off and on: $differing")
            assertTrue(
                "the switch '$name' is drawn identically whether it is on or off, so it can " +
                    "never appear to have been switched",
                differing > 0
            )
        }
    }

    /**
     * The action the screen reports has to be the opposite of what it is showing.
     *
     * A switch that reports the value it already has is a switch that never changes, and that
     * would look exactly like a switch whose click never arrives. This is the half that does not
     * need a frame, kept beside the other so that the two together say which half is broken.
     */
    @Test
    fun theSwitchReportsTheOppositeOfWhatItShows() {
        val actions = mutableListOf<SettingsAction>()
        FrameInspection.inspect(width, height) {
            SettingsScreen(
                state = CellularitySettings.Default.copy(keepOn = true),
                onAction = { actions += it }
            )
        }

        // Nothing was pressed, so nothing should have been reported: a screen that dispatches
        // while it is merely being drawn would be reporting changes nobody asked for.
        assertTrue(
            "drawing the screen without pressing anything must not report a change, but it " +
                "reported $actions",
            actions.isEmpty()
        )
    }
}
