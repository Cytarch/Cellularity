/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.unit.Density

/**
 * Draws a composable offscreen and describes the frame it produces.
 *
 * Every report about this port has been settled by somebody looking at it, because a test could
 * check what a function returns and not what a frame contains -- and the difference between "the
 * screen is drawn" and "the screen is drawn correctly" is the whole of what has been going wrong.
 * Compose can draw into an image in memory, so the frame can be inspected here: no window, no
 * monitor, nothing in front of anybody.
 *
 * What is measured is deliberately small. A screen that paints nothing, a screen that paints one
 * flat colour and a screen that paints its controls are three different pictures under these
 * three numbers, and only the third is a working screen. Anything more elaborate would be
 * describing a design rather than detecting a fault.
 *
 * This lives here rather than inside one test because more than one screen needs asking, and a
 * second copy of the measurement would be a second thing to keep correct.
 */
object FrameInspection {

    /** A frame after it has been drawn: how many pixels of each colour it holds, and where. */
    class Frame(
        val width: Int,
        val height: Int,
        private val pixels: IntArray
    ) {
        val distinctColours: Int = pixels.toHashSet().size

        /** The colour most of the frame is, which is what "the background" means for a drawing. */
        val background: Int = run {
            val counts = HashMap<Int, Int>()
            for (pixel in pixels) counts[pixel] = (counts[pixel] ?: 0) + 1
            var best = pixels[0]
            var bestCount = -1
            for ((colour, count) in counts) {
                if (count > bestCount) {
                    best = colour
                    bestCount = count
                }
            }
            best
        }

        /** The bounding box of everything that differs from the background. */
        val contentWidth: Int
        val contentHeight: Int

        init {
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
            contentWidth = if (maxX < 0) 0 else maxX - minX + 1
            contentHeight = if (maxY < 0) 0 else maxY - minY + 1
        }

        /** True when the frame is one colour, which is a screen with nothing visible on it. */
        val isFlat: Boolean get() = distinctColours == 1

        /** True when what was drawn covers most of the frame rather than a part of it. */
        val fills: Boolean
            get() = contentWidth * 10 >= width * 9 && contentHeight * 10 >= height * 9

        override fun toString(): String =
            "colours=$distinctColours background=#${background.toString(16).padStart(6, '0')} " +
                "content=${contentWidth}x$contentHeight in ${width}x$height"
    }

    /** Composes, draws and measures, with the scene closed whether or not drawing succeeded. */
    fun inspect(
        width: Int,
        height: Int,
        content: @Composable () -> Unit
    ): Frame {
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
            Frame(width, height, pixels)
        } finally {
            scene.close()
        }
    }
}
