/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.model.EnvironmentSnapshot
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateEditStatus
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoScreen
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateInfoState
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateStatsSectionState
import com.cytarch.cellularity.shared.ui.play.substrate_info.toSubstrateEnvironmentUiState
import com.cytarch.cellularity.shared.ui.text.formatCanonicalQuantity
import java.awt.Canvas
import java.awt.Component
import java.awt.Dimension
import java.awt.Frame
import java.awt.event.ComponentAdapter
import java.awt.event.ComponentEvent
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test

/**
 * Looks at the screens as frames rather than as functions.
 *
 * Both screens in this file were reported as showing a blank rectangle, and both were checked by
 * clicking and by reading the code first: the settings switches report a change when clicked and
 * every switch is wired to the store through one path, and the substrate panel reads and edits
 * the running world through a gateway that was already built. Neither check found anything,
 * because neither can see the frame -- and "the controls are there and wired" and "the controls
 * are on the screen" are different claims.
 *
 * So the screens are composed and painted into an image in memory and the image is measured. No
 * window, no monitor, nothing in front of anybody.
 *
 * What is checked is only that each screen paints more than a background. It does not check that
 * the values are right, and it is not meant to: a panel reading the wrong value and a panel
 * reading the right one look the same here, and what was open for months was whether these
 * screens appear at all.
 */
class DesktopScreenFrameTest {

    /**
     * The settings screen and every control on it.
     *
     * Reported as not working while both its halves check out, so the one thing a click cannot
     * see is what the frame contains. A screen painting one colour is a screen where every
     * control is missing, invisible or covered, and that is now distinguishable from a working
     * one without asking anybody to look.
     */
    @Test
    fun theSettingsScreenPaintsItsControls() {
        val actions = mutableListOf<SettingsAction>()
        val frame = FrameInspection.inspect(900, 700) {
            SettingsScreen(
                state = CellularitySettings.Default,
                onAction = { actions += it }
            )
        }

        println("SETTINGS frame: $frame")
        assertTrue(
            "the settings screen painted only a background, so its controls are missing or " +
                "invisible: $frame",
            !frame.isFlat
        )
        // A screen that fills its frame and one that fills part of it are different screens, and
        // only one of them was intended. The ratio is used rather than an equality because a
        // screen one pixel short of its frame is a rounding difference and not a finding.
        assertTrue(
            "the settings screen has to use the frame it is given, but its content covers " +
                "only ${frame.filledHeightPercent} per cent of the height: $frame",
            frame.filledHeightPercent >= 95
        )
    }

    /**
     * The substrate panel in the state the desktop is in before its first read of the world.
     *
     * The panel existed for a long time without ever being drawn, because a private function
     * with the same name had been declared in the file that called it and a private declaration
     * in the same package wins over the one it shadows. It is called now, and this is what says
     * whether being called is the same as being seen.
     */
    @Test
    fun theSubstratePanelPaintsSomethingBeforeTheWorldIsRead() {
        val frame = FrameInspection.inspect(1280, 744) {
            MaterialTheme {
                SubstrateInfoScreen(
                    state = SubstrateInfoState(),
                    environment = null,
                    stats = SubstrateStatsSectionState(),
                    editStatus = SubstrateEditStatus.Idle,
                    showExperimentalFeatures = true,
                    onPlateActionClick = { },
                    selectedTemperature = 0,
                    onTemperatureSelected = { },
                    onCheckboxChanged = { _, _ -> },
                    onSliderChanged = { _, _ -> },
                    onEnvironmentFieldTypeChanged = { _, _ -> },
                    onEnvironmentFieldValueChanged = { _, _, _ -> },
                    onMutationWeightChanged = { _, _ -> },
                    onMutationWeightsReset = { },
                    onGenePoolChanged = { _, _ -> },
                    sliderValueFormatter = ::formatCanonicalQuantity,
                    sliderInputValueFormatter = ::formatCanonicalQuantity
                )
            }
        }

        println("SUBSTRATE frame: $frame")
        assertTrue(
            "the substrate panel painted only one colour, so it appears as a blank rectangle " +
                "rather than as a panel: $frame",
            !frame.isFlat
        )
        // Deliberately not asserted to fill the frame here. A panel with no world to show is a
        // panel with nothing to draw, and the loading branch is legitimately short. What that
        // costs is asserted in the next test, where there is a world and there is no excuse.
        println(
            "SUBSTRATE with no world yet fills ${frame.filledHeightPercent} per cent of the height"
        )
    }

    /**
     * The substrate panel with a world behind it, which is the state it is meant to be in.
     *
     * The panel only ever showed its loading message, and the reason was upstream of it: the
     * gateway took a single reading when the session opened and then declared itself ready
     * whether or not anything had come back, so a first read that failed left the confirmed
     * environment null for the rest of the run and nothing tried again. The panel is not broken;
     * it was never given anything to show.
     *
     * This is the other half of that finding. Given a reading of a real world, mapped through the
     * same mapper the gateway uses, the panel has to use the frame it is given -- and if it does
     * not, then the empty part was the panel's own fault and the retry would have been beside the
     * point.
     *
     * The world is a real one: a session is created through the renderer and its environment read
     * back over the native contract. A hand-built snapshot would prove only that the panel can
     * draw a document nobody produces.
     */
    @Test
    fun theSubstratePanelFillsTheFrameWhenTheWorldHasAnswered() {
        assumeTrue("the renderer library was not built", DesktopNativeRenderer.isAvailable)
        val roots = DesktopAssetRoots.resolve()
        assumeTrue("the compiled shaders and textures were not found", roots != null)
        assumeTrue("no display is available", !java.awt.GraphicsEnvironment.isHeadless())

        val snapshot = readEnvironmentFromARealWorld(roots!!)
        assumeTrue("the world did not answer a request for its environment", snapshot != null)

        val environment = snapshot!!.toSubstrateEnvironmentUiState()
        val frame = FrameInspection.inspect(1280, 744) {
            MaterialTheme {
                SubstrateInfoScreen(
                    state = SubstrateInfoState(),
                    environment = environment,
                    stats = SubstrateStatsSectionState(),
                    editStatus = SubstrateEditStatus.Idle,
                    showExperimentalFeatures = true,
                    onPlateActionClick = { },
                    selectedTemperature = 0,
                    onTemperatureSelected = { },
                    onCheckboxChanged = { _, _ -> },
                    onSliderChanged = { _, _ -> },
                    onEnvironmentFieldTypeChanged = { _, _ -> },
                    onEnvironmentFieldValueChanged = { _, _, _ -> },
                    onMutationWeightChanged = { _, _ -> },
                    onMutationWeightsReset = { },
                    onGenePoolChanged = { _, _ -> },
                    sliderValueFormatter = ::formatCanonicalQuantity,
                    sliderInputValueFormatter = ::formatCanonicalQuantity
                )
            }
        }

        println("SUBSTRATE with a world frame: $frame")
        assertTrue(
            "the panel has revision ${environment.revision} to show and still does not use the " +
                "frame it is given: $frame",
            frame.filledHeightPercent >= 95
        )
    }
}

/**
 * Starts a real world and reads its environment, the same way the renderer's own test does.
 *
 * A session is created against a real canvas on a real window and drawn until it presents,
 * because the runtime advances the world as it presents and a session nobody draws never
 * produces anything to read. The environment is then taken through the gateway rather than
 * through the renderer directly, so what comes back is the document the panel is actually
 * handed in the application, decoded by the same codec and mapped by the same mapper.
 *
 * Returns null when the world does not answer, so a caller can skip rather than fail: a machine
 * without a library, shaders or a display has not established anything, and saying otherwise
 * would report the port as broken when only the build was incomplete.
 */
private fun readEnvironmentFromARealWorld(roots: DesktopAssetRoots.Roots): EnvironmentSnapshot? {
    val frame = Frame("Cellularity frame test")
    val sized = CountDownLatch(1)
    val canvas = Canvas().apply {
        background = java.awt.Color.BLACK
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
    return try {
        frame.isVisible = true
        if (!sized.await(10, TimeUnit.SECONDS)) {
            println("WORLD canvas was never sized")
            return null
        }
        val ownerHandle = nativeWindowHandleOf(canvas)
        println("WORLD owner=$ownerHandle canvas=${canvas.width}x${canvas.height}")
        if (ownerHandle == 0L) return null

        windowHandle = DesktopNativeRenderer.nativeCreateWindow(
            ownerHandle, 0, 0, canvas.width, canvas.height
        )
        println("WORLD window=$windowHandle")
        if (windowHandle == 0L) return null
        sessionHandle = DesktopNativeRenderer.nativeCreateSession(
            windowHandle, canvas.width, canvas.height,
            1_000, 3_500, 1.0,
            roots.shaderRoot.toString(), roots.textureRoot.toString()
        )
        println("WORLD session=$sessionHandle")
        if (sessionHandle == 0L) return null

        var frames = 0
        while (frames < 120) {
            DesktopNativeRenderer.nativeRenderFrame(sessionHandle)
            frames++
            Thread.sleep(8)
        }

        val gateway = DesktopSimulationGateway(sessionHandle = sessionHandle)
        runBlocking {
            gateway.start()
            val read = gateway.confirmedEnvironment.value
            println(
                "WORLD read=" + (read?.let { "revision ${it.revision}" } ?: "nothing") +
                    " state=" + gateway.state.value
            )
            read
        }
    } finally {
        if (sessionHandle != 0L) DesktopNativeRenderer.nativeStopSession(sessionHandle)
        if (windowHandle != 0L) DesktopNativeRenderer.nativeDestroyWindow(windowHandle)
        frame.isVisible = false
        frame.dispose()
    }
}

/**
 * The native window an AWT component was given.
 *
 * There is no public accessor for this, so the peer has to be reached by reflection. Reaching the
 * handle field directly was tried first and silently returned nothing -- a peer that has no such
 * field is not an error, it is just a peer without one, so the failure was a zero rather than an
 * exception and the test skipped itself instead of reporting that it could not look. The peer's
 * own `getHWnd` is the accessor that actually exists, and it is asked for by name.
 *
 * This lives beside the tests that need it rather than inside one of them, because two copies of
 * a reflection helper is two chances for one of them to be quietly wrong -- which is exactly what
 * happened.
 */
internal fun nativeWindowHandleOf(component: Component): Long {
    if (!component.isShowing) return 0L
    return try {
        val peerField = Component::class.java.getDeclaredField("peer")
        peerField.isAccessible = true
        val peer: Any = peerField.get(component) ?: return 0L
        val getHWnd = peer.javaClass.methods.firstOrNull { it.name == "getHWnd" } ?: return 0L
        (getHWnd.invoke(peer) as? Long) ?: 0L
    } catch (_: ReflectiveOperationException) {
        0L
    } catch (_: RuntimeException) {
        0L
    } catch (_: LinkageError) {
        0L
    }
}