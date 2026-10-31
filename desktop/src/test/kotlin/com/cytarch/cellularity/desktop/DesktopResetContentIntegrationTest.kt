/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.desktop.nativebridge.SubstratePreviewCapture
import com.cytarch.cellularity.shared.ui.play.PlaySessionCommandResult
import java.nio.file.Path
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Acceptance contract for the desktop sterilise sequence.
 *
 * What is tested: the content attached to reset acceptance is the pre-command state, while the
 * post-reset content is published through the gateway for the screen's content observer. The
 * shared play machine needs that later observation to release its pending-command lock.
 *
 * Input -> expected examples, checked consecutively against one real session:
 * 1. reset at revision R -> Accepted(R), then gateway revision R + 1;
 * 2. a second reset at R + 1 -> Accepted(R + 1), then gateway revision R + 2.
 *
 * Metric: pass/fail. Returning the newer revision in Accepted reproduces the blocked plate menu
 * and exit buttons, so equality with the pre-command state is required on both iterations.
 */
class DesktopResetContentIntegrationTest {
    @Test
    fun `reset acceptance precedes each post reset content publication`() = runBlocking {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        assumeTrue("no display is available", !java.awt.GraphicsEnvironment.isHeadless())

        val side = 480
        var windowHandle = 0L
        var sessionHandle = 0L
        try {
            windowHandle = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(side, side)
            assertTrue("the renderer did not create its anchor", windowHandle != 0L)
            sessionHandle = DesktopNativeRenderer.nativeCreateSession(
                windowHandle, side, side,
                1_000, 3_500, 1.0,
                roots!!.shaderRoot.toString(), roots.textureRoot.toString()
            )
            assertTrue("the simulation did not start", sessionHandle != 0L)

            // Reset and the environment read are owner-queue operations. Drawing establishes the
            // same running session that the assembled play screen uses.
            repeat(120) {
                assertEquals(0, DesktopNativeRenderer.nativeRenderFrame(sessionHandle))
                Thread.sleep(8)
            }

            val gateway = DesktopSimulationGateway(sessionHandle = sessionHandle)
            gateway.start()
            val operations = DesktopPlaySessionOperations(
                fileRepository = DesktopCellularityFileRepository(
                    Path.of("build", "diagnostics", "reset-content-test")
                ),
                openedPlate = null,
                previewCapture = SubstratePreviewCapture()
            )
            operations.attachGateway(gateway)

            repeat(2) { resetIndex ->
                val before = gateway.contentState.value
                val result = operations.reset()
                assertTrue("reset $resetIndex was not accepted: $result", result is PlaySessionCommandResult.Accepted)
                result as PlaySessionCommandResult.Accepted

                assertEquals(
                    "reset $resetIndex accepted the post-command revision; the observer has no later revision to release the lock",
                    before,
                    result.content
                )
                assertEquals(
                    "reset $resetIndex did not publish its post-command content separately",
                    before.revision + 1,
                    gateway.contentState.value.revision
                )
            }
            gateway.close()
        } finally {
            if (sessionHandle != 0L) DesktopNativeRenderer.nativeStopSession(sessionHandle)
            if (windowHandle != 0L) DesktopNativeRenderer.nativeDestroyWindow(windowHandle)
        }
    }
}
