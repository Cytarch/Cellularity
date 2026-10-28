/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.simulation.ApplyResult
import com.cytarch.cellularity.shared.simulation.EnvironmentBooleanProperty
import com.cytarch.cellularity.shared.simulation.EnvironmentChange
import com.cytarch.cellularity.shared.simulation.EnvironmentPatch
import com.cytarch.cellularity.shared.ui.play.substrate_info.toSubstrateEnvironmentUiState
import java.awt.Canvas
import java.awt.Color
import java.awt.Component
import java.awt.Dimension
import java.awt.Frame
import java.awt.event.ComponentAdapter
import java.awt.event.ComponentEvent
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Checks that the substrate panel can read and edit a world that is actually running.
 *
 * This is the claim that cannot be established by reading either side. The panel's
 * contract is a snapshot and a patch against a revision, the renderer's is a session
 * handle and a blocking request, and the two only meet if the bytes the renderer returns
 * are the document the codec expects. A wrong field order would decode without error and
 * show plausible numbers, so the test asserts on values the world actually produces rather
 * than on the decode succeeding.
 *
 * Skipped rather than failed when the machine cannot provide a library, shaders or a
 * display: a skip is honest about what was not established, and a failure would claim the
 * port is broken when only the build was incomplete.
 */
class DesktopSimulationGatewayIntegrationTest {

    @Test
    fun `reads a live environment and applies a change against its revision`() {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        assumeTrue("no display is available", !java.awt.GraphicsEnvironment.isHeadless())

        val frame = Frame("Cellularity gateway test")
        val sized = CountDownLatch(1)
        val canvas = Canvas().apply {
            background = Color.BLACK
            preferredSize = Dimension(480, 480)
        }
        canvas.addComponentListener(object : ComponentAdapter() {
            override fun componentResized(event: ComponentEvent) {
                if (event.component.width > 0) sized.countDown()
            }
        })
        frame.add(canvas)
        frame.pack()
        frame.setLocation(60, 60)

        var windowHandle = 0L
        var sessionHandle = 0L
        try {
            frame.isVisible = true
            assertTrue("the canvas never received a size", sized.await(10, TimeUnit.SECONDS))
            val ownerHandle = handleOf(canvas)
            assertTrue("the canvas has no native window", ownerHandle != 0L)

            windowHandle = DesktopNativeRenderer.nativeCreateWindow(
                ownerHandle, 0, 0, canvas.width, canvas.height
            )
            assertTrue("the renderer did not create its window", windowHandle != 0L)
            sessionHandle = DesktopNativeRenderer.nativeCreateSession(
                windowHandle, canvas.width, canvas.height,
                1_000, 3_500, 1.0,
                roots!!.shaderRoot.toString(), roots.textureRoot.toString()
            )
            assertTrue("the simulation did not start", sessionHandle != 0L)

            // Frames must be drawn: the runtime advances the world as it presents, so a
            // session nobody draws never produces anything to read.
            var frames = 0
            while (frames < 120) {
                assertEquals(0, DesktopNativeRenderer.nativeRenderFrame(sessionHandle))
                frames++
                Thread.sleep(8)
            }

            val gateway = DesktopSimulationGateway(sessionHandle = sessionHandle)
            runBlocking {
                gateway.start()

                val environment = gateway.confirmedEnvironment.value
                assertNotNull(
                    "the environment was not decoded, so the panel would show its loading state",
                    environment
                )
                environment!!

                // The world has been running, so these are values it produced rather than
                // the zeroes an unread document would carry.
                assertEquals(
                    "the gene pool is not the length the contract declares",
                    com.cytarch.cellularity.shared.model.EnvironmentContract.GENE_POOL_SIZE,
                    environment.genePool.size
                )
                assertEquals(
                    "the mutation weights are not the length the contract declares",
                    com.cytarch.cellularity.shared.model.EnvironmentContract.MUTATION_WEIGHT_COUNT,
                    environment.mutationWeights.size
                )

                // The revision counts environment changes, not simulation progress, so a
                // world that has simply been running still reports the revision it was
                // created with. This is the baseline the patch below is written against.
                val baselineRevision = environment.revision

                val stats = gateway.stats.value
                assertTrue("the world reported no cells: $stats", stats.cellCount > 0)
                assertTrue("the world reported no elapsed time: $stats", stats.time > 0.0)
                // Printed as well as asserted, so a run leaves a record of what the
                // running world actually contained rather than only that it was non-zero.
                println(
                    "GATEWAY cells=${stats.cellCount} time=${stats.time} " +
                        "updates=${stats.updateCount} revision=${environment.revision} " +
                        "genePool=${environment.genePool.size} " +
                        "weights=${environment.mutationWeights.size}"
                )

                // The panel maps the snapshot before showing it; doing that here is what
                // catches a field the mapper needs and the document does not carry.
                val ui = environment.toSubstrateEnvironmentUiState()
                assertEquals(environment.revision, ui.revision)

                // A change against the revision just read must be accepted.
                val applied = gateway.applyEnvironment(
                    EnvironmentPatch(
                        baseRevision = baselineRevision,
                        changes = listOf(
                            EnvironmentChange.BooleanValue(
                                property = EnvironmentBooleanProperty.AGING,
                                value = true
                            )
                        )
                    )
                )
                assertTrue(
                    "the simulation refused a change based on the revision it just reported: " +
                        "$applied",
                    applied is ApplyResult.Applied
                )

                // And the change must be visible in the next reading, otherwise the
                // panel would snap back to the old value the moment it refreshed.
                gateway.readOnce()
                val after = gateway.confirmedEnvironment.value
                assertNotNull("the environment stopped decoding after a change", after)
                assertEquals(
                    "the change was accepted but not reported back",
                    true,
                    after?.aging
                )
                assertTrue(
                    "an accepted change did not advance the revision: " +
                        "${after?.revision} was $baselineRevision",
                    (after?.revision ?: 0L) > baselineRevision
                )
                println(
                    "GATEWAY change accepted: revision $baselineRevision -> ${after?.revision}, " +
                        "aging=${after?.aging}"
                )

                // A change against a revision the world has left behind must be refused,
                // because the panel's other controls are all still based on it.
                val stale = gateway.applyEnvironment(
                    EnvironmentPatch(
                        baseRevision = baselineRevision,
                        changes = listOf(
                            EnvironmentChange.BooleanValue(
                                property = EnvironmentBooleanProperty.AGING,
                                value = false
                            )
                        )
                    )
                )
                assertTrue(
                    "a change against a stale revision was not refused: $stale",
                    stale is ApplyResult.Rejected || stale is ApplyResult.Failure
                )

                gateway.close()
            }
        } finally {
            if (sessionHandle != 0L) DesktopNativeRenderer.nativeStopSession(sessionHandle)
            if (windowHandle != 0L) DesktopNativeRenderer.nativeDestroyWindow(windowHandle)
            frame.isVisible = false
            frame.dispose()
        }
    }

    @Test
    fun `reports state and refuses work for a session that does not exist`() = runBlocking {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val gateway = DesktopSimulationGateway(sessionHandle = 0L)
        gateway.start()
        // The reads fail and nothing is claimed: no environment, no cells, and a closed
        // session afterwards. Reporting Ready here would be a lie about a dead handle.
        assertEquals(null, gateway.confirmedEnvironment.value)
        assertEquals(0, gateway.stats.value.cellCount)
        gateway.close()
    }

    private companion object {
        fun handleOf(component: Component): Long {
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
