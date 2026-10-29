/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.unit.Density
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Looks at the settings screen as a frame rather than as a function.
 *
 * The settings switches were reported as not working, and checking them by clicking found
 * nothing wrong: the preference reports a change when it is clicked and every switch on the
 * screen is wired to the store through the same path. So the fault is somewhere a click test
 * cannot see, and the one thing such a test cannot see is what the frame contains.
 *
 * That is what this draws. The screen is composed and painted into an image in memory -- no
 * window, no monitor, nothing in front of anybody -- and the image is then read back as pixels
 * and described. A screen that paints nothing, a screen that paints one flat colour, and a
 * screen that paints its controls are three different pictures, and only the third is a working
 * screen. Saying which one it is does not need anybody to look at it.
 */
class DesktopSettingsFrameTest {

    private val width = 900
    private val height = 700

    /** Draws a composable offscreen and returns the pixels, plus the text of any failure. */
    private fun draw(content: @androidx.compose.runtime.Composable () -> Unit): IntArray {
        val scene = ImageComposeScene(width = width, height = height, density = Density(1f))
        return try {
            scene.setContent { MaterialTheme { content() } }
            val image = scene.render()
            val bitmap = org.jetbrains.skia.Bitmap()
            bitmap.allocN32Pixels(width, height)
            image.readPixels(bitmap)
            val pixels = IntArray(width * height)
            for (y in 0 until height) {
                for (x in 0 until width) {
                    pixels[y * width + x] = bitmap.getColor(x, y) and 0xFFFFFF
                }
            }
            pixels
        } finally {
            scene.close()
        }
    }

    private fun describe(pixels: IntArray): String {
        val counts = HashMap<Int, Int>()
        for (pixel in pixels) counts[pixel] = (counts[pixel] ?: 0) + 1
        val background = counts.maxByOrNull { it.value }!!.key
        var minX = width
        var minY = height
        var maxX = -1
        var maxY = -1
        for (y in 0 until height) {
            for (x in 0 until width) {
                if (pixels[y * width + x] != background) {
                    if (x < minX) minX = x
                    if (y < minY) minY = y
                    if (x > maxX) maxX = x
                    if (y > maxY) maxY = y
                }
            }
        }
        val content = if (maxX < 0) "nothing" else "${maxX - minX + 1}x${maxY - minY + 1}"
        return "colours=${counts.size} background=#%06X content=$content in ${width}x$height"
            .format(background)
    }

    /**
     * The screen has to paint something, and the something has to be more than a background.
     *
     * A screen that renders as one colour is a screen where every control is missing, invisible
     * or covered -- which is the shape of both reports about this screen, and which no test
     * that looks at callbacks can distinguish from a working one.
     */
    @Test
    fun theSettingsScreenPaintsItsControls() {
        val actions = mutableListOf<SettingsAction>()
        val pixels = draw {
            SettingsScreen(
                state = CellularitySettings.Default,
                onAction = { actions += it }
            )
        }

        val report = describe(pixels)
        println("SETTINGS frame: $report")
        assertTrue(
            "the settings screen painted only a background, which means its controls are " +
                "missing or invisible: $report",
            pixels.toHashSet().size > 1
        )
    }
}
