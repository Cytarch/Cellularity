/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.ImageComposeScene
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.unit.Density
import com.cytarch.cellularity.desktop.settings.DesktopCellularitySettingsRepository
import com.cytarch.cellularity.shared.settings.CellularitySettings
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsAction
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsStore
import com.cytarch.cellularity.shared.ui.main_menu.settings.SettingsScreen
import java.nio.file.Path
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

/**
 * Presses a switch and watches the picture change, with a real store behind the screen.
 *
 * Everything about this switch has been checked in pieces and every piece works: the two states
 * are drawn fifteen hundred pixels apart, a press on it where it is drawn reports the action, the
 * reducer turns that into the opposite value, the store writes through the repository and
 * publishes what the repository holds, the repository in the directory the launcher actually uses
 * accepts the write, and a screen collecting that store's state sees the new value.
 *
 * What has never been done is the whole circle at once, and it is the only arrangement the
 * application is in: a press on the drawn control, through the store, through the repository, back
 * out to the pixels. Any one link being right does not imply the circle closes, and a switch that
 * does not close the circle is a switch that does not work however many of its links are correct.
 *
 * Pixels are compared rather than callbacks, because the claim is about what a person sees. A
 * callback that fires and a control that moves are different claims, and the report was about the
 * control.
 */
class DesktopSettingsSwitchCircleTest {

    @get:Rule
    val folder = TemporaryFolder()

    private val width = 900
    private val height = 700


    private fun differing(first: IntArray, second: IntArray): Int {
        var count = 0
        for (index in first.indices) {
            if (first[index] != second[index]) count++
        }
        return count
    }

    /**
     * Where the switch is, found by drawing it in both states and taking the middle of what moved.
     *
     * The position is read off the drawing rather than asked of a test framework, because the
     * framework's own lookup needs string resources and those do not resolve in a test process --
     * the reason an earlier attempt at this could not run at all. It is also closer to what a
     * person does: they aim at the control they can see.
     */
    private fun switchCentre(
        off: FrameInspection.Frame,
        on: FrameInspection.Frame
    ): Offset {
        var sumX = 0L
        var sumY = 0L
        var count = 0
        for (y in 0 until height) {
            for (x in 0 until width) {
                if (off.pixels[y * width + x] != on.pixels[y * width + x]) {
                    sumX += x
                    sumY += y
                    count++
                }
            }
        }
        assertTrue("the two states of the switch have to differ somewhere", count > 0)
        return Offset((sumX / count).toFloat(), (sumY / count).toFloat())
    }

    private fun settingsFile(): Path =
        folder.newFolder("settings").toPath().resolve("settings.json")

    @Test
    fun pressingTheSwitchClosesTheWholeCircleBackToThePixels() {
        val file = settingsFile()
        val repository = DesktopCellularitySettingsRepository(file)
        val store = SettingsStore(repository)
        runBlocking { repository.initialize() }

        val scope = CoroutineScope(Dispatchers.Default)
        val scene = ImageComposeScene(width = width, height = height, density = Density(1f))
        try {
            scene.setContent {
                MaterialTheme {
                    val collected by store.state.collectAsState()
                    SettingsScreen(
                        state = collected,
                        onAction = { action -> scope.launch { store.dispatch(action) } }
                    )
                }
            }
            scene.render()

            // Where the switch is, from the difference between its two states. The value is read
            // from the store rather than assumed, so the drawing used to find the control and the
            // drawing that is pressed describe the same screen.
            val startValue = store.state.value.keepOn
            val offFrame = FrameInspection.inspect(width, height) {
                SettingsScreen(
                    state = store.state.value.copy(keepOn = false),
                    onAction = { }
                )
            }
            val onFrame = FrameInspection.inspect(width, height) {
                SettingsScreen(
                    state = store.state.value.copy(keepOn = true),
                    onAction = { }
                )
            }
            val target = switchCentre(offFrame, onFrame)
            println("CIRCLE the keep-awake switch is drawn around (${target.x}, ${target.y})")
            println("CIRCLE it starts as keepOn=$startValue")

            // The frame the press will change is the scene's, so it is captured from the scene and
            // not from a second composition: a position found in one drawing is only meaningful in
            // the drawing it was found in.
            val beforePixels = captureScene(scene)

            scene.sendPointerEvent(PointerEventType.Press, target)
            scene.sendPointerEvent(PointerEventType.Release, target)

            // The dispatch is a coroutine on another dispatcher, so the store is given a moment to
            // finish before the frame is read. Without this the test would be racing the write and
            // would sometimes pass and sometimes fail, which is worse than either.
            val expected = !startValue
            var waited = 0
            while (store.state.value.keepOn != expected && waited < 5_000) {
                Thread.sleep(50)
                waited += 50
            }
            println("CIRCLE after the press the store holds keepOn=${store.state.value.keepOn}")

            scene.render()
            val afterPixels = captureScene(scene)
            val changed = differing(beforePixels, afterPixels)
            println("CIRCLE the frame changed by $changed pixels")

            assertTrue(
                "pressing the keep-awake switch moved ${changed} pixels of the screen while the " +
                    "store went from $startValue to ${store.state.value.keepOn}: the circle does " +
                    "not close, and this is what a person sees as a switch that does not work",
                changed > 0
            )
            assertTrue(
                "the store never took the new value after the press",
                store.state.value.keepOn == expected
            )
        } finally {
            scene.close()
            scope.coroutineContext[Job]?.cancel()
        }
    }

    private fun captureScene(scene: ImageComposeScene): IntArray {
        val image = scene.render()
        val bitmap = org.jetbrains.skia.Bitmap()
        bitmap.allocN32Pixels(width, height)
        image.readPixels(bitmap)
        val pixels = IntArray(width * height)
        for (y in 0 until height) {
            for (x in 0 until width) {
                pixels[y * width + x] = bitmap.getColor(x, y) and 0xFFFFFF
            }
        }
        return pixels
    }
}
