/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Checks the frame reader against drawings whose contents are known.
 *
 * Every report about this port has been settled by somebody looking at it: the surface was in
 * the wrong place, the simulation covered the tabs, the panel that should have been on screen was
 * a placeholder. None of those were visible in a test, because a test checks what a function
 * returns and not what a frame contains -- and the difference between "the screen is drawn" and
 * "the screen is drawn correctly" is the whole of what has been going wrong.
 *
 * Compose can draw into an image in memory, which closes that gap without a window, a monitor or
 * anything appearing in front of anybody. But a reader of frames is itself a thing that can be
 * wrong, and one that reports every frame as fine would be worse than no reader at all: it would
 * turn every following measurement into a false reassurance. So it is checked first against
 * drawings whose answers are known in advance, and only then pointed at a real screen.
 */
class DesktopFrameInspectionTest {

    private val width = 400
    private val height = 300

    @Test
    fun aFlatDrawingIsRecognisedAsFlat() {
        val frame = FrameInspection.inspect(width, height) {
            Box(modifier = Modifier.fillMaxSize().background(Color(0xFFFFFFFF)))
        }

        assertTrue(
            "a drawing of one colour has to be reported as flat, but it was described as " +
                "$frame",
            frame.isFlat
        )
        assertEquals("a flat drawing has no content to measure", 0, frame.contentWidth)
    }

    @Test
    fun aDrawingWithContentIsNotFlat() {
        val frame = FrameInspection.inspect(width, height) {
            Box(modifier = Modifier.fillMaxSize().background(Color(0xFFFFFFFF))) {
                Text(text = "content", modifier = Modifier.size(200.dp, 100.dp))
            }
        }

        assertTrue(
            "a drawing with text on it has to have more than one colour, but it was " +
                "described as $frame",
            !frame.isFlat
        )
    }

    /**
     * The bounding box of everything that differs from the background.
     *
     * This is the number that would have described the reported faults directly: a picture drawn
     * into the corner of its own viewport is a bounding box much smaller than the frame, and a
     * screen that is one flat rectangle is a bounding box that does not exist. Measuring it here
     * on a rectangle whose size is known is what makes it trustworthy when it is pointed at a
     * screen.
     *
     * The rectangle is placed away from the edges on purpose. Whether content reaches the edge is
     * a separate question from how large it is, and the two were confused here first: a small
     * rectangle in the top-left corner touches two edges, so a reader that answers the second
     * question by looking at the edges calls it a filled frame. It is right to; the question was
     * wrong.
     */
    @Test
    fun theExtentOfTheContentIsMeasured() {
        val frame = FrameInspection.inspect(width, height) {
            Box(modifier = Modifier.fillMaxSize().background(Color(0xFFFFFFFF))) {
                Box(
                    modifier = Modifier
                        .size(100.dp, 60.dp)
                        .background(Color(0xFF102030))
                )
            }
        }

        assertEquals(
            "the content of a 100 by 60 rectangle has to measure 100 by 60, but it measured " +
                "${frame.contentWidth} by ${frame.contentHeight}",
            100,
            frame.contentWidth
        )
        assertEquals(60, frame.contentHeight)
        assertTrue(
            "the content of a small rectangle must not span the frame, but it measured " +
                "${frame.contentWidth} by ${frame.contentHeight} in ${width}x$height: $frame",
            frame.contentWidth < width && frame.contentHeight < height
        )
    }

    /**
     * A drawing that covers the frame has to be reported as covering it.
     *
     * The first attempt at this test drew a full-size box on top of another full-size box and
     * expected the reader to say the frame was filled. It said the opposite, correctly: the two
     * boxes were the same colour, so the picture really was one colour, and a frame of one
     * colour has no bounding box to measure. The reader was right and the test was wrong -- but
     * the same measurement also made "the screen is covered" and "the screen is empty"
     * indistinguishable, which is a fault in the reader, and both were fixed.
     *
     * So the drawing here covers the frame and is not uniform: a differently coloured border
     * around the edge. Whether a colour reaches the edge is what the reader now answers.
     */
    @Test
    fun aDrawingThatFillsTheFrameIsReportedAsFillingIt() {
        val frame = FrameInspection.inspect(width, height) {
            Box(modifier = Modifier.fillMaxSize().background(Color(0xFF102030))) {
                Box(
                    modifier = Modifier
                        .size((width - 40).dp, (height - 40).dp)
                        .background(Color(0xFFFFFFFF))
                )
            }
        }

        assertTrue("a full-frame drawing has to be reported as filling it: $frame", frame.fills)
    }

    /**
     * An empty frame is not reported as filled, even though it is also "all background".
     *
     * This is the half that the first version of the reader got wrong: a frame with nothing on
     * it and a frame covered edge to edge both have no content outside the background, so a
     * reader that only measures the bounding box cannot tell a full screen from a blank one.
     */
    @Test
    fun anEmptyFrameIsNotReportedAsFillingItself() {
        val frame = FrameInspection.inspect(width, height) {
            Box(modifier = Modifier.fillMaxSize().background(Color(0xFFFFFFFF)))
        }

        assertTrue(
            "a frame with nothing on it must not be reported as filled: $frame",
            !frame.fills
        )
    }
}
