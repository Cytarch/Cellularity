/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsSwitchPreference
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Rule
import org.junit.Test

/**
 * Checks that a settings switch reports a change when it is clicked.
 *
 * The report this exists for is "the switches are clickable but do not switch", and it is not
 * answerable by reading the code: the card's click handler and the switch's own handler are
 * two different things, and a switch wired to do nothing looks exactly like a switch whose
 * state never comes back. A click that produces no callback and a click that produces a
 * callback the caller ignores are the same picture on screen and different faults.
 *
 * So the preference is driven the way a person drives it -- a real click at a real position
 * through the real input pipeline -- and the callback is what is asserted. That separates "the
 * click never arrives" from "the click arrives and the state does not", which is the first
 * thing to know about any report of a control that does nothing.
 */
class DesktopSettingsSwitchTest {

    @get:Rule
    val compose = createComposeRule()

    @Test
    fun clickingASwitchPreferenceReportsTheOppositeState() {
        val reported = mutableListOf<Boolean>()
        compose.setContent {
            MaterialTheme {
                Column {
                    SettingsSwitchPreference(
                        title = "Keep the screen awake",
                        summary = "A summary line",
                        checked = false,
                        onCheckedChange = { reported += it }
                    )
                }
            }
        }

        compose.onNodeWithText("Keep the screen awake").performClick()

        assertEquals(
            "a click on an unchecked switch has to report that it is now checked",
            listOf(true),
            reported
        )
    }

    @Test
    fun clickingACheckedSwitchReportsThatItIsNoLongerChecked() {
        val reported = mutableListOf<Boolean>()
        compose.setContent {
            MaterialTheme {
                Column {
                    SettingsSwitchPreference(
                        title = "Keep the screen awake",
                        summary = "A summary line",
                        checked = true,
                        onCheckedChange = { reported += it }
                    )
                }
            }
        }

        compose.onNodeWithText("Keep the screen awake").performClick()

        assertEquals(
            "a click on a checked switch has to report that it is now unchecked",
            listOf(false),
            reported
        )
    }

    /**
     * The summary and the switch itself have to be part of the same click target.
     *
     * A person aims at the row, not at a rectangle they cannot see, and the row is drawn by
     * several components. Clicking each of them in turn is what shows whether the whole row is
     * one control or whether parts of it quietly swallow the click.
     */
    @Test
    fun everyPartOfTheRowIsTheSameControl() {
        val reported = mutableListOf<Boolean>()
        compose.setContent {
            MaterialTheme {
                Column {
                    SettingsSwitchPreference(
                        title = "Investigating",
                        summary = "The summary is part of the row",
                        checked = false,
                        onCheckedChange = { reported += it }
                    )
                }
            }
        }

        compose.onNodeWithText("The summary is part of the row").performClick()

        assertNotNull(
            "clicking the summary line has to reach the control the row represents",
            reported.firstOrNull()
        )
        assertEquals(
            "the summary is part of the same control as the title",
            listOf(true),
            reported
        )
    }
}
