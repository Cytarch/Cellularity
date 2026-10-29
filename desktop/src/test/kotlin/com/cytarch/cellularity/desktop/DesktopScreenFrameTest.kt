/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateEditStatus
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoScreen
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoState
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateStatsSectionState
import com.cytarch.cellularity.shared.ui.text.formatCanonicalQuantity
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Looks at the screens as frames rather than as functions.
 *
 * Both screens in this file were reported as showing a blank rectangle, and both were checked by
 * clicking and by reading the code first: the settings switches report a change when clicked and
 * every switch is wired to the store through one path, and the substrate panel reads and edits
 * the running world through a gateway that was already built. Neither check found anything,
 * because neither can see the frame -- and "the controls are there and wired" and "the controls
 * are on the screen" are different claims.
 *
 * So the screens are composed and painted into an image in memory and the image is measured. No
 * window, no monitor, nothing in front of anybody.
 *
 * What is checked is only that each screen paints more than a background. It does not check that
 * the values are right, and it is not meant to: a panel reading the wrong value and a panel
 * reading the right one look the same here, and what was open for months was whether these
 * screens appear at all.
 */
class DesktopScreenFrameTest {

    /**
     * The settings screen and every control on it.
     *
     * Reported as not working while both its halves check out, so the one thing a click cannot
     * see is what the frame contains. A screen painting one colour is a screen where every
     * control is missing, invisible or covered, and that is now distinguishable from a working
     * one without asking anybody to look.
     */
    @Test
    fun theSettingsScreenPaintsItsControls() {
        val actions = mutableListOf<SettingsAction>()
        val frame = FrameInspection.inspect(900, 700) {
            SettingsScreen(
                state = CellularitySettings.Default,
                onAction = { actions += it }
            )
        }

        println("SETTINGS frame: $frame")
        assertTrue(
            "the settings screen painted only a background, so its controls are missing or " +
                "invisible: $frame",
            !frame.isFlat
        )
        // A screen that fills its frame and one that fills part of it are different screens, and
        // only one of them was intended. The ratio is used rather than an equality because a
        // screen one pixel short of its frame is a rounding difference and not a finding.
        assertTrue(
            "the settings screen has to use the frame it is given, but its content covers " +
                "only ${frame.filledHeightPercent} per cent of the height: $frame",
            frame.filledHeightPercent >= 95
        )
    }

    /**
     * The substrate panel in the state the desktop is in before its first read of the world.
     *
     * The panel existed for a long time without ever being drawn, because a private function
     * with the same name had been declared in the file that called it and a private declaration
     * in the same package wins over the one it shadows. It is called now, and this is what says
     * whether being called is the same as being seen.
     */
    @Test
    fun theSubstratePanelPaintsSomethingBeforeTheWorldIsRead() {
        val frame = FrameInspection.inspect(1280, 744) {
            MaterialTheme {
                SubstrateInfoScreen(
                    state = SubstrateInfoState(),
                    environment = null,
                    stats = SubstrateStatsSectionState(),
                    editStatus = SubstrateEditStatus.Idle,
                    showExperimentalFeatures = true,
                    onPlateActionClick = { },
                    selectedTemperature = 0,
                    onTemperatureSelected = { },
                    onCheckboxChanged = { _, _ -> },
                    onSliderChanged = { _, _ -> },
                    onEnvironmentFieldTypeChanged = { _, _ -> },
                    onEnvironmentFieldValueChanged = { _, _, _ -> },
                    onMutationWeightChanged = { _, _ -> },
                    onMutationWeightsReset = { },
                    onGenePoolChanged = { _, _ -> },
                    sliderValueFormatter = ::formatCanonicalQuantity,
                    sliderInputValueFormatter = ::formatCanonicalQuantity
                )
            }
        }

        println("SUBSTRATE frame: $frame")
        assertTrue(
            "the substrate panel painted only one colour, so it appears as a blank rectangle " +
                "rather than as a panel: $frame",
            !frame.isFlat
        )
        // Deliberately not asserted to fill the frame. A panel whose sections are shorter than
        // the window is a panel with room to spare, and a scrolling panel legitimately ends
        // where its content ends -- so the fraction is printed as a question rather than
        // asserted as a verdict. It is 60 per cent at the time of writing, and the rest of the
        // height shows the theme's background, which is a near-white lavender.
        println(
            "SUBSTRATE fills ${frame.filledHeightPercent} per cent of the height it was given"
        )
    }
}
