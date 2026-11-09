/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerEventType
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Presses a switch on the assembled screen and asks whether anything happens.
 *
 * The switch is drawn correctly -- its two states differ by fifteen hundred pixels, so the state
 * is visible -- and its callback reaches the store, which writes the document and publishes the
 * new value. Every piece checks out separately, and the report is that the switch does not work.
 * The one thing no test has done is press it where it actually is.
 *
 * So the switch is found by looking for the difference between the two frames rather than by
 * asking a test framework for a node. That is deliberate: the previous attempt to find it by its
 * label could not run at all, because string resources do not resolve in a test process, and a
 * position taken from the drawing is both independent of that and closer to what a person does --
 * they aim at the control they can see.
 *
 * A press that reaches the card reports an action. Nothing else in this suite can tell the
 * difference between a click that never arrived and a click that arrived and was ignored.
 */
class DesktopSettingsPressTest {

    private val width = 900
    private val height = 700

    /** Where two frames disagree, as a single point near the middle of the changed area. */
    private fun changedPoint(off: FrameInspection.Frame, on: FrameInspection.Frame): Offset {
        var sumX = 0L
        var sumY = 0L
        var count = 0
        for (y in 0 until height) {
            for (x in 0 until width) {
                if (off.pixels[y * width + x] != on.pixels[y * width + x]) {
                    sumX += x
                    sumY += y
                    count++
                }
            }
        }
        assertTrue("the two states of the switch have to differ somewhere", count > 0)
        return Offset((sumX / count).toFloat(), (sumY / count).toFloat())
    }

    @Test
    fun pressingTheKeepAwakeSwitchReportsAChange() {
        val actions = mutableListOf<SettingsAction>()

        val off = FrameInspection.inspect(width, height) {
            SettingsScreen(
                state = CellularitySettings.Default.copy(keepOn = false),
                onAction = { actions += it }
            )
        }
        val on = FrameInspection.inspect(width, height) {
            SettingsScreen(
                state = CellularitySettings.Default.copy(keepOn = true),
                onAction = { actions += it }
            )
        }

        val target = changedPoint(off, on)
        println("PRESS the keep-awake switch is drawn around (${target.x}, ${target.y})")

        val scene = ImageComposeScene(width = width, height = height, density = androidx.compose.ui.unit.Density(1f))
        try {
            scene.setContent {
                MaterialTheme {
                    SettingsScreen(
                        state = CellularitySettings.Default.copy(keepOn = false),
                        onAction = { actions += it }
                    )
                }
            }
            // Drawn first, because a control that has never been composed has no position to
            // press: the frame the position came from and the frame being pressed have to be the
            // same layout, and the only way to know they are is to draw this one before pressing.
            scene.render()

            scene.sendPointerEvent(PointerEventType.Press, target)
            scene.sendPointerEvent(PointerEventType.Release, target)
            scene.render()

            println("PRESS actions after pressing at ($target): $actions")
            assertTrue(
                "pressing the keep-awake switch where it is drawn reported no change, so the " +
                    "click never reaches the control: pressed at $target and got $actions",
                actions.any { it is SettingsAction.SetKeepOn }
            )
        } finally {
            scene.close()
        }
    }
}
