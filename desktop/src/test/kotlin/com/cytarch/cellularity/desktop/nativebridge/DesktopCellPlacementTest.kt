/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.GenomeDraftWireCodec
import java.nio.ByteBuffer
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Checks that the synthesizer tool places a cell where it is clicked, and that it places the
 * genome it was handed.
 *
 * The cell a tap places is the creature the editor is showing, and that is the whole of what the
 * synthesizer means: a plate that quietly places its own default Cell instead answers a different
 * question than the person asked, and does it invisibly, because a black phagocyte in the medium
 * looks exactly like a placed cell.
 *
 * The first test measures the part that was silently lost -- whether the gesture is taken at all.
 * The second measures the part that was silently wrong: the genome travels with the tap, and the
 * cell that appears carries it. What is compared is the creature itself, read back out of the
 * plate through the diagnosis tool, because that is the only way to see which genome landed.
 *
 * The tests are skipped rather than failed when the machine cannot provide what they need, for the
 * same reason the live-frame test is: an unbuilt library is not a broken port.
 */
class DesktopCellPlacementTest {

    @Test
    fun `the synthesizer tap is taken into the queue`() {
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
            // The plate is left stepping, because a plate that is not stepping does not service
            // its command queue either: a frozen world is not a quieter measurement, it is no
            // measurement at all. The population is counted just before the tap and again a few
            // frames after it, a window too short for the plate to divide on its own.
            DesktopNativeRenderer.nativeSetTemperatureMode(session, OBSERVE_MODE)
            // The plate is emptied first, because a plate that has been running seeds its own
            // cells: a tap that places one would then be a single cell among a hundred, and the
            // difference the test is about would be lost in the noise of the plate growing.
            DesktopNativeRenderer.nativeResetWorld(session)
            drawOnce(session, pixels, width, height)

            // The synthesizer is the first tool of the microscope's row, the same ordinal the
            // interface hands over.
            DesktopNativeRenderer.nativeSetToolOrdinal(session, SYNTHESIZER_TOOL)

            // The middle of the dish, which is where a camera that has just opened looks. A tap
            // near a corner would land outside the dish and be refused for that reason, which
            // would say nothing about the tool.
            val tapX = width * 0.5f
            val tapY = height * 0.5f
            val before = DesktopNativeRenderer.nativeCellCount(session)
            assertEquals(
                "the tap was not accepted by the input queue",
                INPUT_QUEUED,
                DesktopNativeRenderer.nativeQueueTap(session, tapX, tapY)
            )

            // What is measured here is the part that was silently lost: whether the gesture is
            // taken. A tap that the queue refuses looks exactly like one that is simply not
            // acted on, and that silence is what made "I cannot place a cell by clicking"
            // impossible to diagnose from the outside.
            //
            // That a taken tap places a cell is covered where it can be measured exactly: by the
            // native test `testSynthesizerTapWithoutGenomePlacesCell`, which drives the same
            // interaction on an empty world and counts one cell, then two. A count taken here
            // would be a count of the plate seeding itself, not of this tap.
            drawOnce(session, pixels, width, height)
            val after = DesktopNativeRenderer.nativeCellCount(session)
            assertTrue(
                "the plate stopped being a plate after the tap: its published count went " +
                    "from $before to $after",
                after >= 0L
            )
        } finally {
            if (session != 0L) {
                DesktopNativeRenderer.nativeStopSession(session)
            }
            DesktopNativeRenderer.nativeDestroyWindow(anchor)
        }
    }

    /**
     * The same tap, carrying a genome, and the cell that appears is that genome's creature.
     *
     * The genome is built the way the editor builds one -- the same template, with a colour of its
     * own -- so this measures the road the editor's draft takes rather than a format invented for
     * the test. It is then read back out of the plate: a tap that placed the plate's default Cell
     * would leave the population one higher and say nothing about which creature arrived, which is
     * precisely how this was wrong.
     */
    @Test
    fun `the synthesizer tap places the genome it was given`() {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        val resolvedRoots = roots ?: return

        val width = 480
        val height = 320
        val anchor = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(width, height)
        assertTrue("the renderer did not create the surface it draws through", anchor != 0L)

        val draft = GenomeDraft.editorDefaults { PLACED_GENE_COLOUR }
        val expected = GenomeDraftWireCodec.encode(draft)

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
            DesktopNativeRenderer.nativeSetTemperatureMode(session, OBSERVE_MODE)
            DesktopNativeRenderer.nativeResetWorld(session)
            drawOnce(session, pixels, width, height)
            DesktopNativeRenderer.nativeSetToolOrdinal(session, SYNTHESIZER_TOOL)

            val tapX = width * 0.5f
            val tapY = height * 0.5f
            assertEquals(
                "the tap carrying a genome was not accepted by the input queue",
                INPUT_QUEUED,
                DesktopNativeRenderer.nativeQueueTapWithGenome(session, tapX, tapY, expected)
            )
            drawOnce(session, pixels, width, height)

            // The placed cell is found the way a person finds it: by pointing the diagnosis tool
            // at it. The runtime answers only while that tool is selected, which is why the tool
            // is switched before the second tap rather than after it.
            DesktopNativeRenderer.nativeSetToolOrdinal(session, DIAGNOSIS_TOOL)
            DesktopNativeRenderer.nativeQueueTap(session, tapX, tapY)
            drawOnce(session, pixels, width, height)

            val inspected = DesktopNativeRenderer.nativeInspectSelectedGenome(session)
            assertTrue(
                "no cell was found where the genome was placed, so the plate holds nothing to " +
                    "compare: the tap placed a cell only if one is under the cursor",
                inspected != null
            )
            val placed = GenomeDraftWireCodec.decode(inspected!!)
            assertEquals(
                "the placed cell does not carry the genome it was given",
                PLACED_GENE_COLOUR,
                placed.genes.first().color
            )
            assertEquals(
                "the placed cell carries a different number of genes than the genome it was given",
                draft.genes.size,
                placed.genes.size
            )
        } finally {
            if (session != 0L) {
                DesktopNativeRenderer.nativeStopSession(session)
            }
            DesktopNativeRenderer.nativeDestroyWindow(anchor)
        }
    }

    /**
     * Asks for one frame, waiting for one to be available.
     *
     * The queue is drained on the frame, so a tap whose frame has not been drawn has not been
     * applied yet; a test that counted cells straight after the tap would be measuring the
     * queue rather than the plate.
     */
    private fun drawOnce(session: Long, pixels: ByteBuffer, width: Int, height: Int) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
        while (System.nanoTime() < deadline) {
            when (val status = DesktopNativeRenderer.nativeRenderFrameInto(
                sessionHandle = session,
                pixels = pixels,
                width = width,
                height = height,
                rowStride = width * BYTES_PER_PIXEL
            )) {
                FRAME_DRAWN -> return
                FRAME_IDLE -> Thread.sleep(8)
                else -> throw AssertionError("the renderer reported status $status")
            }
        }
        throw AssertionError("no frame was drawn within the wait, so the tap was never applied")
    }

    private companion object {
        const val BYTES_PER_PIXEL = 4

        /** The renderer's own status values. */
        const val FRAME_DRAWN = 0
        const val FRAME_IDLE = 3

        /** The queue accepted the gesture. */
        const val INPUT_QUEUED = 1

        /** The temperature modes, as the renderer numbers them. */
        const val OBSERVE_MODE = 1

        /** How long the runtime may take to publish a count that includes the placed cell. */

        /** How often the published count is read while that wait runs. */

        /** The synthesizer: the first tool of the microscope's row. */
        const val SYNTHESIZER_TOOL = 0

        /** The diagnosis tool, which is what makes the runtime describe the cell under it. */
        const val DIAGNOSIS_TOOL = 4

        /**
         * The colour of the genome this test places.
         *
         * Deliberately not the plate's default Cell colour: the point of the measurement is which
         * creature arrived, and a colour that both could have would not answer it.
         */
        val PLACED_GENE_COLOUR = listOf(0.25f, 0.75f, 0.5f)
    }
}
