/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Looks at what the interface draws, without a window and without a screen.
 *
 * Every report about this port so far has been resolved by somebody looking at it: the surface
 * was in the wrong place, the simulation covered the tabs, the panel that should have been
 * shown was a placeholder. None of those were visible in a test, because a test can check what
 * a function returns and not what a frame contains -- and the difference between "the screen is
 * drawn" and "the screen is drawn correctly" is the whole of what has been going wrong.
 *
 * `ImageComposeScene` closes that gap: it composes and draws into an image in memory, with no
 * window, no monitor and nothing in front of anybody. What the image contains can then be read
 * back as pixels and asserted on, which turns "the panel shows a white rectangle" from a
 * sentence into a number.
 *
 * This is the check that the mechanism works at all. A drawing that is one flat colour and a
 * drawing that has content in it have to be told apart before anything can be said about a real
 * screen, because a reader that cannot tell those apart would report every screen as fine.
 */
class DesktopFrameInspectionTest {

    private val width = 400
    private val height = 300

    /** Draws a composable into an image and returns its pixels as 0xRRGGBB values. */
    private fun draw(content: @androidx.compose.runtime.Composable () -> Unit): IntArray {
        val scene = ImageComposeScene(
            width = width,
            height = height,
            density = Density(1f)
        )
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

    private fun distinctColours(pixels: IntArray): Int = pixels.toHashSet().size

    /**
     * The colour most of the frame is, which is what "the background" means for a drawing.
     *
     * Counted rather than assumed, because a screen that is mostly one colour and a screen that
     * is mostly another are different screens and neither is known in advance.
     */
    private fun mostCommonColour(pixels: IntArray): Int {
        val counts = HashMap<Int, Int>()
        for (pixel in pixels) {
            counts[pixel] = (counts[pixel] ?: 0) + 1
        }
        var best = pixels[0]
        var bestCount = -1
        for ((colour, count) in counts) {
            if (count > bestCount) {
                best = colour
                bestCount = count
            }
        }
        return best
    }

    @Test
    fun aFlatDrawingIsRecognisedAsFlat() {
        val pixels = draw {
            Box(modifier = Modifier.fillMaxSize().background(Color(0xFFFFFFFF)))
        }

        assertTrue(
            "a drawing of one colour has to be reported as having one colour, but " +
                "found ${distinctColours(pixels)}",
            distinctColours(pixels) == 1
        )
    }

    @Test
    fun aDrawingWithContentIsNotFlat() {
        val pixels = draw {
            Box(modifier = Modifier.fillMaxSize().background(Color(0xFFFFFFFF))) {
                Text(text = "content", modifier = Modifier.size(200.dp, 100.dp))
            }
        }

        assertTrue(
            "a drawing with text on it has to have more than one colour, but found " +
                "${distinctColours(pixels)}",
            distinctColours(pixels) > 1
        )
    }

    /**
     * The bounding box of everything that differs from the background.
     *
     * This is the number that would have described the reported faults directly: a picture
     * drawn into the corner of its own viewport is a bounding box much smaller than the frame,
     * and a screen that is one flat rectangle is a bounding box that does not exist. Measuring
     * it here is what makes it possible to say which of those a screen is instead of describing
     * it to somebody and waiting.
     */
    @Test
    fun theExtentOfTheContentCanBeMeasured() {
        val pixels = draw {
            Box(modifier = Modifier.fillMaxSize().background(Color(0xFFFFFFFF))) {
                Box(
                    modifier = Modifier
                        .size(100.dp, 60.dp)
                        .background(Color(0xFF102030))
                )
            }
        }

        val background = mostCommonColour(pixels)
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

        assertTrue("the small rectangle has to be found somewhere", maxX >= 0)
        assertTrue(
            "the content of a quarter-sized rectangle must not span the whole frame, but " +
                "found ${maxX - minX + 1}x${maxY - minY + 1} in ${width}x$height",
            (maxX - minX + 1) < width && (maxY - minY + 1) < height
        )
    }
}
