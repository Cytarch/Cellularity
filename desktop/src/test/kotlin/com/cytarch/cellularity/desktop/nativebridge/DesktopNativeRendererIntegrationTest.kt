/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import java.awt.Canvas
import java.awt.Color
import java.awt.Component
import java.awt.Dimension
import java.awt.Frame
import java.awt.event.ComponentAdapter
import java.awt.event.ComponentEvent
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Checks that the renderer can be driven from the JVM through a real window.
 *
 * This is the part of the desktop port that cannot be established by reading code. The
 * renderer presents into a Win32 child window parented to an AWT canvas, and the whole
 * arrangement depends on a native handle that AWT does not expose in a supported way --
 * if that lookup returns nothing the interface would show a renderer-unavailable message
 * and no amount of unit testing elsewhere would notice.
 *
 * The test is skipped rather than failed when the machine cannot provide what it needs: a
 * library that has not been built, shaders that have not been compiled, or a session with
 * no display. A skip is honest about what was not established; a failure would claim the
 * port is broken when the build simply did not include it.
 */
class DesktopNativeRendererIntegrationTest {

    @Test
    fun `creates a child window inside an awt canvas and presents frames`() {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        assumeTrue("no display is available", !java.awt.GraphicsEnvironment.isHeadless())

        val frame = Frame("Cellularity native renderer test")
        val size = 640
        val sized = CountDownLatch(1)
        val canvas = Canvas().apply {
            background = Color.BLACK
            preferredSize = Dimension(size, size)
        }
        canvas.addComponentListener(object : ComponentAdapter() {
            override fun componentResized(event: ComponentEvent) {
                if (event.component.width > 0 && event.component.height > 0) {
                    sized.countDown()
                }
            }
        })
        frame.add(canvas)
        frame.pack()
        frame.setLocation(50, 50)

        var windowHandle = 0L
        var sessionHandle = 0L
        try {
            frame.isVisible = true
            assertTrue(
                "the canvas never received a size, so AWT did not realise it",
                sized.await(10, TimeUnit.SECONDS)
            )

            val ownerHandle = nativeWindowHandleOf(canvas)
            assertTrue(
                "the AWT canvas has no native window, so the renderer has nothing to " +
                    "attach to. This is usually a missing --add-opens for java.awt, " +
                    "which the launcher sets through JDK_JAVA_OPTIONS.",
                ownerHandle != 0L
            )

            windowHandle = DesktopNativeRenderer.nativeCreateWindow(
                ownerHandle,
                0,
                0,
                canvas.width,
                canvas.height
            )
            assertTrue("the renderer did not create its window", windowHandle != 0L)

            sessionHandle = DesktopNativeRenderer.nativeCreateSession(
                windowHandle,
                canvas.width,
                canvas.height,
                1_000,
                3_500,
                1.0,
                roots!!.shaderRoot.toString(),
                roots.textureRoot.toString()
            )
            assertTrue("the simulation did not start", sessionHandle != 0L)

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
            frame.isVisible = false
            frame.dispose()
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

    private companion object {
        /**
         * The same lookup the interface uses.
         *
         * Repeated here rather than shared so the test does not pass because it and the
         * production path were changed together to agree on something wrong.
         */
        fun nativeWindowHandleOf(component: Component): Long {
            if (!component.isShowing) return 0L
            return try {
                val peerField = Component::class.java.getDeclaredField("peer")
                peerField.isAccessible = true
                val peer: Any = peerField.get(component) ?: return 0L
                val getHWnd = peer.javaClass.methods.firstOrNull { it.name == "getHWnd" }
                    ?: return 0L
                (getHWnd.invoke(peer) as? Long) ?: 0L
            } catch (_: ReflectiveOperationException) {
                0L
            } catch (_: RuntimeException) {
                0L
            } catch (_: LinkageError) {
                0L
            }
        }


    }
}
