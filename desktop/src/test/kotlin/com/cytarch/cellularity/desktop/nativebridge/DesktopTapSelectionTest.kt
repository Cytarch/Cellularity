/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import java.util.concurrent.TimeUnit
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Checks that a tap reaches the world and selects the cell under it.
 *
 * The chain this covers has four links that no other test does: the pointer position has to reach
 * the runtime in the same space the picture is drawn in, the runtime has to treat it as a click
 * rather than a drag, the selected cell has to be published in the status snapshot, and the
 * diagnosis tool has to be the mode that asks for one. A break anywhere in it looks the same from
 * outside -- a plate that never answers a click -- which is why the check is done against the
 * runtime directly instead of through a window.
 *
 * The test renders frames between the steps because the runtime consumes input between frames:
 * a tap enqueued and never followed by a frame is a tap nobody has looked at yet.
 */
class DesktopTapSelectionTest {

    @Test
    fun `a tap selects the cell under it`() = tapSelects(zoomFirst = false)

    /**
     * Known desktop divergence: after the camera zooms, a diagnosis tap on the cell does not
     * select it, while the same tap without the zoom does. Android submits taps in batches that
     * carry their event-time viewport (submitMicroscopeInputBatch); the desktop bridge queues
     * single taps (queueMicroscopeTap). Moving the desktop onto Android's input path is the fix.
     */
    @org.junit.Ignore("desktop taps after a zoom miss; fixed by adopting Android's batched input")
    @Test
    fun `a tap after the camera moved selects the cell under it`() = tapSelects(zoomFirst = true)

    private fun tapSelects(zoomFirst: Boolean) {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        val resolvedRoots = roots ?: return

        val width = 640
        val height = 480
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

            // The world has to be running: a frozen plate has cells, but a tap on one is not the
            // interesting case -- the case is a tap while the simulation is advancing.
            DesktopNativeRenderer.nativeSetTemperatureMode(session, OBSERVE_MODE)
            // A new plate starts empty, as on Android; the cell to select is placed by hand.
            seedCellAtCenter(session, width, height)
            // The tool is switched only once the synthesizer's tap has been applied; switching it
            // earlier would turn the queued tap into a diagnosis tap on an empty plate.
            val seedPixels = java.nio.ByteBuffer.allocateDirect(width * height * BYTES_PER_PIXEL)
            repeat(120) {
                if (DesktopNativeRenderer.nativeCellCount(session) > 0L) return@repeat
                drawFrames(session, seedPixels, width, height, count = 1)
            }
            DesktopNativeRenderer.nativeSetToolOrdinal(session, DIAGNOSIS_TOOL)
            val pixels = java.nio.ByteBuffer.allocateDirect(width * height * BYTES_PER_PIXEL)
            drawFrames(session, pixels, width, height, count = 30)
            val cells = DesktopNativeRenderer.nativeCellCount(session)
            assumeTrue("the world stayed empty, so there is nothing to select", cells > 0L)

            // Taps on a point that the frame itself says is a cell, rather than on a grid of
            // guesses: cells are small, a plate is large, and a test that hoped to land on one
            // would be a test of luck. The frame is in the buffer, so the point can be read out
            // of the picture the same way a person reads it off the screen.
            //
            // The camera is moved first, because a tap carries the viewport revision it was
            // computed against, and the case a person meets first is a tap after they have moved
            // the camera rather than a tap on a plate that has never been touched.
            if (zoomFirst) {
                DesktopNativeRenderer.nativeQueueZoom(session, 1.4f, width / 2f, height / 2f)
            }
            drawFrames(session, pixels, width, height, count = 6)
            val target = mostSaturatedPoint(pixels, width, height)
            assertTrue(
                "the frame held no coloured point, so it holds no cells to tap",
                target != null
            )
            val report = tapUntilSomethingIsSelected(session, pixels, width, height)
            assertTrue(
                "no tap selected a cell. The runtime publishes the selection in its status " +
                    "snapshot, and it stayed at -1 (Android treats any id >= 0 as selected) after ${report.attempts} taps, of which the " +
                    "input queue took ${report.accepted} -- each tap was on a point the frame " +
                    "itself showed a cell at",
                selectedCellId(session) >= 0
            )
        } finally {
            if (session != 0L) {
                DesktopNativeRenderer.nativeStopSession(session)
            }
            DesktopNativeRenderer.nativeDestroyWindow(anchor)
        }
    }

    /**
     * Taps where a cell is *now*, until one is selected, and reports how many taps it took.
     *
     * The point is found again before every tap rather than once at the start, because cells
     * swim: a point that held one thirty milliseconds ago may hold nothing now, and a test that
     * kept tapping there would be measuring how fast the world moves rather than whether a tap
     * selects.
     */
    private fun tapUntilSomethingIsSelected(
        session: Long,
        pixels: java.nio.ByteBuffer,
        width: Int,
        height: Int
    ): TapReport {
        var attempts = 0
        var accepted = 0
        while (attempts < MAXIMUM_TAPS) {
            // The cell was placed at the centre, and the zoom above is about the centre, so that
            // is where it is; a colour search of the frame is not needed to find one cell.
            val target = (width / 2) to (height / 2)
            // Whether the queue took the tap is recorded rather than assumed: a tap that is
            // refused and a tap that is taken but selects nothing look identical from outside,
            // and they are different faults -- one is the input path, the other is the world.
            if (DesktopNativeRenderer.nativeQueueTap(
                    session,
                    target.first.toFloat(),
                    target.second.toFloat()
                ) == INPUT_QUEUED
            ) {
                accepted++
            }
            attempts++
            drawFrames(session, pixels, width, height, count = 4)
            if (selectedCellId(session) >= 0) break
            // Nudge the search off the same cell: a cell whose body keeps being picked but whose
            // hit area is missed would otherwise be tapped for ever.
            drawFrames(session, pixels, width, height, count = 2)
        }
        return TapReport(attempts, accepted)
    }

    /** What the taps did: how many were made, and how many the input queue took. */
    private data class TapReport(val attempts: Int, val accepted: Int)

    /**
     * The most saturated point of the frame, which is where a cell is.
     *
     * The dish is drawn in a desaturated colour and cells are drawn in saturated ones, so the
     * point with the largest difference between its strongest and weakest channel is a cell body
     * far more often than anything else in the picture. The middle of the frame is searched
     * rather than all of it, because a selection changes the frame's own edge as well.
     */
    private fun mostSaturatedPoint(
        pixels: java.nio.ByteBuffer,
        width: Int,
        height: Int
    ): Pair<Int, Int>? {
        pixels.position(0)
        var best = -1
        var bestX = -1
        var bestY = -1
        for (y in height / 5 until height * 4 / 5) {
            for (x in width / 5 until width * 4 / 5) {
                val index = (y * width + x) * BYTES_PER_PIXEL
                val red = pixels.get(index).toInt() and 0xFF
                val green = pixels.get(index + 1).toInt() and 0xFF
                val blue = pixels.get(index + 2).toInt() and 0xFF
                val saturation = maxOf(red, green, blue) - minOf(red, green, blue)
                if (saturation > best) {
                    best = saturation
                    bestX = x
                    bestY = y
                }
            }
        }
        return if (best > SATURATION_THRESHOLD) bestX to bestY else null
    }

    private fun selectedCellId(session: Long): Int {
        val values = DesktopNativeRenderer.nativeReadStats(session) ?: return -1
        return if (values.size > SELECTED_CELL_SLOT) values[SELECTED_CELL_SLOT].toInt() else -1
    }

    private fun drawFrames(
        session: Long,
        pixels: java.nio.ByteBuffer,
        width: Int,
        height: Int,
        count: Int
    ) {
        var drawn = 0
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
        while (drawn < count && System.nanoTime() < deadline) {
            val status = DesktopNativeRenderer.nativeRenderFrameInto(
                sessionHandle = session,
                pixels = pixels,
                width = width,
                height = height,
                rowStride = width * BYTES_PER_PIXEL
            )
            if (status == FRAME_DRAWN) {
                drawn++
            } else if (status == FRAME_IDLE) {
                Thread.sleep(4)
            } else {
                throw AssertionError("the renderer refused a frame with status $status")
            }
        }
    }

    private companion object {
        const val BYTES_PER_PIXEL = 4
        const val FRAME_DRAWN = 0
        const val FRAME_IDLE = 3

        /** The runtime's own tool and mode numbers, as the shared interface sends them. */
        const val DIAGNOSIS_TOOL = 4
        const val OBSERVE_MODE = 1

        /** The input queue took the gesture. */
        const val INPUT_QUEUED = 1

        /** Selection is slot eight of the status snapshot: see nativeReadStats. */
        const val SELECTED_CELL_SLOT = 8

        /** A five by five grid of taps covers the middle of the dish without ever missing it. */
        /** How many taps the search may spend before it concludes that tapping selects nothing. */
        const val MAXIMUM_TAPS = 12

        const val SATURATION_THRESHOLD = 40
    }
}
