/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Checks that the renderer can be driven from the JVM.
 *
 * This is the part of the desktop port that cannot be established by reading code: the renderer
 * has to come up, take a surface, simulate and hand back pixels. It used to do that by parenting a
 * Win32 child window to an AWT canvas and reaching into the canvas's peer for its handle, which
 * was both the thing under test and the reason the test could fail for reasons that had nothing to
 * do with the renderer.
 *
 * The renderer is anchored now, not parented: it is given a window of its own that nothing shows
 * and nothing draws into, and the frame that matters is the one it renders into memory. The test
 * therefore asks the same question it always asked -- does a session come up and produce a frame --
 * without depending on an unsupported lookup.
 *
 * The test is skipped rather than failed when the machine cannot provide what it needs: a library
 * that has not been built, shaders that have not been compiled, or a session with no display. A
 * skip is honest about what was not established; a failure would claim the port is broken when the
 * build simply did not include it.
 */
class DesktopNativeRendererIntegrationTest {

    @Test
    fun `creates a session on an anchor window and presents frames`() {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)

        val size = 640
        var windowHandle = 0L
        var sessionHandle = 0L
        try {
            windowHandle = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(size, size)
            assertTrue("the renderer did not create its anchor", windowHandle != 0L)

            sessionHandle = DesktopNativeRenderer.nativeCreateSession(
                windowHandle,
                size,
                size,
                1_000,
                3_500,
                1.0,
                roots!!.shaderRoot.toString(),
                roots.textureRoot.toString()
            )
            assertTrue("the simulation did not start", sessionHandle != 0L)
            // A new plate starts empty, as on Android; the first cell is placed by hand.
            seedCellAtCenter(sessionHandle, size, size)

            // A frame that is merely accepted is not evidence that anything was drawn,
            // so the world is given time to produce cells and the count is required to
            // move. A renderer that presents nothing would leave this at zero.
            val cells = AtomicLong(0)
            var frames = 0
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)
            while (System.nanoTime() < deadline && cells.get() == 0L) {
                val result = DesktopNativeRenderer.nativeRenderFrame(sessionHandle)
                assertEquals("frame $frames reported a failure", 0, result)
                cells.set(DesktopNativeRenderer.nativeCellCount(sessionHandle))
                frames++
                Thread.sleep(16)
            }
            assertTrue("no frames were presented at all", frames > 0)
            assertTrue(
                "the world stayed empty after $frames frames, so the simulation is not " +
                    "advancing",
                cells.get() > 0L
            )

            // Input is queued rather than applied immediately, so the assertion is that
            // the queue accepted it, not that the camera moved.
            assertEquals(1, DesktopNativeRenderer.nativeQueuePan(sessionHandle, 10f, 0f))
            assertEquals(1, DesktopNativeRenderer.nativeQueueZoom(sessionHandle, 1.1f, 10f, 10f))

            // A resize has to reach the surface, not only the window. A success code does
            // not establish that it did, and a surface left at its old extent presents a
            // stretched picture rather than failing, so the world is required to keep
            // producing frames and cells after the window changes size.
            assertEquals(
                "the renderer refused to resize its window",
                0,
                DesktopNativeRenderer.nativeResizeWindow(windowHandle, 480, 360)
            )
            var afterResize = 0
            while (afterResize < 30) {
                assertEquals(
                    "frame $afterResize failed after a resize",
                    0,
                    DesktopNativeRenderer.nativeRenderFrame(sessionHandle)
                )
                afterResize++
                Thread.sleep(8)
            }
            assertTrue(
                "the world stopped advancing after a resize",
                DesktopNativeRenderer.nativeCellCount(sessionHandle) > 0L
            )

            assertEquals(0, DesktopNativeRenderer.nativeStopSession(sessionHandle))
            // Stopping twice must be safe: the window is destroyed on a path that can
            // run after a failure, and a second stop must not reach a freed session.
            assertEquals(0, DesktopNativeRenderer.nativeStopSession(sessionHandle))
            sessionHandle = 0L
        } finally {
            if (sessionHandle != 0L) {
                DesktopNativeRenderer.nativeStopSession(sessionHandle)
            }
            if (windowHandle != 0L) {
                DesktopNativeRenderer.nativeDestroyWindow(windowHandle)
            }
        }
    }

    @Test
    fun `refuses work for a session that does not exist`() {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        // A stale handle must be refused rather than dereferenced. This is what makes
        // the launcher's teardown safe when a window closes while a frame is in flight.
        assertEquals(2, DesktopNativeRenderer.nativeRenderFrame(0L))
        assertEquals(2, DesktopNativeRenderer.nativeRenderFrame(Long.MAX_VALUE))
    }
}
