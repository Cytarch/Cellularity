/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.desktop.nativebridge.SubstratePreviewCapture
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.ui.play.PlaySaveResult
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Acceptance contract for attaching the desktop play operations to a renderer session.
 *
 * What is tested: an attachment is the current session, not a one-shot assignment. A stored plate
 * replaces its initial surface after its path is resolved, so a save must follow that replacement.
 *
 * Input -> expected examples, checked against real native sessions:
 * 1. attach session A, then save "Initial Session" -> Saved and a readable substrate;
 * 2. attach session B, stop A, then save "Latest Session" -> Saved and a readable substrate.
 *
 * Metric: pass/fail. The second save fails with "session is not one of ours" when operations keep
 * the first gateway, which is the loaded-substrate regression this test rejects.
 */
class DesktopPlaySessionOperationsIntegrationTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun `save follows the most recently attached session`() = runBlocking {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        assumeTrue("no display is available", !java.awt.GraphicsEnvironment.isHeadless())

        val repository = DesktopCellularityFileRepository(
            temporaryFolder.newFolder("storage").toPath()
        )
        val operations = DesktopPlaySessionOperations(
            fileRepository = repository,
            openedPlate = null,
            previewCapture = SubstratePreviewCapture()
        )
        var firstWindow = 0L
        var firstSession = 0L
        var secondWindow = 0L
        var secondSession = 0L
        try {
            val first = createSession(roots!!)
            firstWindow = first.first
            firstSession = first.second
            operations.attachGateway(
                DesktopSimulationGateway(
                    sessionHandle = firstSession,
                    fileRepository = repository
                )
            )
            assertSaveSucceeds(operations, repository, "Initial Session")

            val second = createSession(roots)
            secondWindow = second.first
            secondSession = second.second
            operations.attachGateway(
                DesktopSimulationGateway(
                    sessionHandle = secondSession,
                    fileRepository = repository
                )
            )

            DesktopNativeRenderer.nativeStopSession(firstSession)
            firstSession = 0L
            assertSaveSucceeds(operations, repository, "Latest Session")
        } finally {
            if (firstSession != 0L) DesktopNativeRenderer.nativeStopSession(firstSession)
            if (secondSession != 0L) DesktopNativeRenderer.nativeStopSession(secondSession)
            if (firstWindow != 0L) DesktopNativeRenderer.nativeDestroyWindow(firstWindow)
            if (secondWindow != 0L) DesktopNativeRenderer.nativeDestroyWindow(secondWindow)
        }
    }

    private fun createSession(roots: DesktopAssetRoots.Roots): Pair<Long, Long> {
        val side = 480
        val window = DesktopNativeRenderer.nativeCreateSurfaceAnchorWindow(side, side)
        assertTrue("the renderer did not create its anchor", window != 0L)
        val session = DesktopNativeRenderer.nativeCreateSession(
            window, side, side,
            1_000, 3_500, 1.0,
            roots.shaderRoot.toString(), roots.textureRoot.toString()
        )
        assertTrue("the simulation did not start", session != 0L)
        repeat(60) {
            assertEquals(0, DesktopNativeRenderer.nativeRenderFrame(session))
            Thread.sleep(8)
        }
        return window to session
    }

    private suspend fun assertSaveSucceeds(
        operations: DesktopPlaySessionOperations,
        repository: DesktopCellularityFileRepository,
        name: String
    ) {
        val id = StoredSubstrateId(name)
        val saved = operations.save(id)
        assertTrue("saving through the current session failed: $saved", saved is PlaySaveResult.Saved)
        val inspected = repository.readSubstrateInfo(id)
        assertTrue(
            "the current session did not produce a readable substrate: $inspected",
            inspected is StorageResult.Success
        )
    }
}
