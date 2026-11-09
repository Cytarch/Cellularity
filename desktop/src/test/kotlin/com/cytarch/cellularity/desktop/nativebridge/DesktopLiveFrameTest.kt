/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import java.nio.ByteBuffer
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Checks that the renderer draws the world into a buffer the application owns.
 *
 * This is the path the desktop draws through, and it is the one arrangement that lets the world
 * sit *below* the interface: the frame is pixels in the application's own scene rather than the
 * contents of a window, so the controls, the panels and the dialogs are painted after it and
 * therefore over it. The window a device is created against is an anchor and nothing else --
 * this test never shows a window at all, which is the point.
 *
 * What cannot be established by reading code is checked here by pixels: that a frame arrives at
 * all, that it arrives as a picture rather than a flat colour, and that the world behind it is
 * advancing. A renderer that drew nothing, or drew one colour, or drew once and stopped, would
 * be invisible to every test that only looks at status codes.
 *
 * The test is skipped rather than failed when the machine cannot provide what it needs: a
 * library that has not been built, or shaders that have not been compiled. A skip is honest
 * about what was not established; a failure would claim the port is broken when the build simply
 * did not include it.
 */
class DesktopLiveFrameTest {

    @Test
    fun `draws the world into the caller's buffer as a picture`() {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        val resolvedRoots = roots ?: return

        val width = 480
        val height = 320
        val anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(width, height)
        assertTrue("the renderer did not create the surface it draws through", anchor != 0L)

        var session = 0L
        try {
            session = DesktopNativeRenderer.nativeCreateSession(
                windowHandle = anchor,
                width = width,
                height = height,
                maxCellCount = 1_000,
                maxFoodCount = 3_500,
                dishRadius = 1.0,
                shaderRoot = resolvedRoots.shaderRoot.toString(),
                textureRoot = resolvedRoots.textureRoot.toString()
            )
            assertTrue("the simulation did not start", session != 0L)

            val pixels = ByteBuffer.allocateDirect(width * height * BYTES_PER_PIXEL)
            var frames = 0
            var flatFrames = 0
            var cells = 0L
            // Both questions need the run rather than the call: a frame has to be drawn more
            // than once, and the world has to be given the time it takes to seed itself and
            // step. Frames arrive almost immediately; cells do not, and a renderer that drew an
            // empty plate would look exactly like a slow one if only the first were waited for.
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
            while (System.nanoTime() < deadline
                && (frames < MINIMUM_FRAMES || cells == 0L)
            ) {
                when (val status = DesktopNativeRenderer.nativeRenderFrameInto(
                    sessionHandle = session,
                    pixels = pixels,
                    width = width,
                    height = height,
                    rowStride = width * BYTES_PER_PIXEL
                )) {
                    FRAME_DRAWN -> {
                        frames++
                        if (distinctColours(pixels) <= 1) flatFrames++
                        cells = DesktopNativeRenderer.nativeCellCount(session)
                    }

                    FRAME_IDLE -> Thread.sleep(16)

                    else -> throw AssertionError("frame $frames reported status $status")
                }
            }

            assertTrue("no frames were drawn at all", frames > 0)
            assertTrue(
                "every one of the $frames frames was a single flat colour, so the renderer " +
                    "produced a buffer without a picture in it",
                flatFrames < frames
            )
            assertTrue(
                "the world stayed empty after $frames frames, so the simulation is not advancing",
                cells > 0L
            )
        } finally {
            if (session != 0L) {
                DesktopNativeRenderer.nativeStopSession(session)
            }
            DesktopNativeRenderer.nativeDestroyWindow(anchor)
        }
    }

    /**
     * How many different colours the frame holds, up to a small ceiling.
     *
     * The question is only whether there is a picture at all, so the count stops early: a frame
     * of the plate holds thousands of shades and counting them all would say nothing more than
     * the second distinct one does.
     */
    private fun distinctColours(pixels: ByteBuffer): Int {
        val seen = HashSet<Int>()
        pixels.position(0)
        val sample = pixels.asIntBuffer()
        var index = 0
        while (index < sample.limit()) {
            seen.add(sample.get(index))
            if (seen.size > DISTINCT_COLOUR_CEILING) return seen.size
            index += SAMPLE_STRIDE
        }
        return seen.size
    }

    private companion object {
        const val BYTES_PER_PIXEL = 4

        /** Enough frames to tell "drew once" from "keeps drawing". */
        const val MINIMUM_FRAMES = 5

        /** Sampling stride in pixels: every 97th pixel, which is prime and cheap. */
        const val SAMPLE_STRIDE = 97

        const val DISTINCT_COLOUR_CEILING = 32

        /** The renderer's own status values. */
        const val FRAME_DRAWN = 0
        const val FRAME_IDLE = 3
    }
}
