/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.Modifier
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.desktop.nativebridge.MicroscopeRenderSurface
import com.cytarch.cellularity.desktop.nativebridge.SurfaceDiagnostics
import com.cytarch.cellularity.shared.model.SimulationContract
import java.awt.Dimension
import java.awt.Frame
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import javax.swing.SwingUtilities

/**
 * Opens the viewport by itself, on a visible tab, and asks Windows whether it can be seen.
 *
 * Every check of the viewport so far has gone through the play screen, which opens on the
 * substrate tab -- so the viewport was correctly hidden, and everything measured about it was
 * measured through a window that was not on screen. The one claim that has never been tested is
 * the simple one: when the viewport is the visible tab, does a window appear, does it present
 * frames, and does the rest of Windows agree that it is there.
 *
 * It is worth testing on its own rather than through the interface because the two failures it
 * separates look identical from the interface. A picture that is not drawn and a picture that is
 * drawn into a window nobody can see are both "nothing on screen", and the fixes are different.
 *
 * Run through `:desktop:runSurface`. It exits non-zero if the renderer, the shaders or a display
 * are missing, because a skip would claim the port is fine when only the build was incomplete.
 */
fun main() {
    if (!DesktopNativeRenderer.isAvailable) {
        System.err.println("SURFACE the renderer library was not built")
        kotlin.system.exitProcess(2)
    }
    val roots = DesktopAssetRoots.resolve()
    if (roots == null) {
        System.err.println("SURFACE the compiled shaders and textures were not found")
        kotlin.system.exitProcess(2)
    }
    if (java.awt.GraphicsEnvironment.isHeadless()) {
        System.err.println("SURFACE no display is available")
        kotlin.system.exitProcess(2)
    }

    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val frame = AtomicReference<Frame?>(null)

    SwingUtilities.invokeLater {
        try {
            val window = Frame("Cellularity surface")
            val panel = ComposePanel()
            panel.preferredSize = Dimension(1100, 700)
            window.add(panel)
            window.pack()
            window.setLocation(40, 40)
            panel.setContent {
                MaterialTheme {
                    // The viewport is the visible tab here, which is the whole point: this is the
                    // state the interface is in when a person is looking at the simulation.
                    MicroscopeRenderSurface(
                        assetRoots = roots,
                        maxCellCount = SimulationContract.defaultConfiguration.maxCellCount,
                        maxFoodCount = SimulationContract.defaultConfiguration.maxFoodCount,
                        dishRadius = SimulationContract.defaultConfiguration.radius,
                        inputEvents = emptyList(),
                        onCellCount = { },
                        onFailure = { message -> System.err.println("SURFACE failure: $message") },
                        isVisible = true,
                        modifier = Modifier.fillMaxSize()
                    )
                }
            }
            frame.set(window)
            window.isVisible = true
        } catch (thrown: Throwable) {
            failure.set(thrown)
        } finally {
            built.countDown()
        }
    }

    if (!built.await(90, TimeUnit.SECONDS)) {
        System.err.println("SURFACE the event thread never finished building the window")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        System.err.println("SURFACE failed to open: $thrown")
        thrown.printStackTrace()
        kotlin.system.exitProcess(3)
    }

    // Long enough for the surface to be created, laid out, and for frames to be presented: a
    // viewport that is about to work shows it within a second, and waiting longer only makes the
    // difference between "not yet" and "not at all" harder to read.
    Thread.sleep(8_000)

    val described = SurfaceDiagnostics.describe()
    println("SURFACE $described")
    val windowHandle = Regex("window=(\\d+)").find(described)?.groupValues?.get(1)?.toLongOrNull()
    if (windowHandle == null || windowHandle == 0L) {
        System.err.println("SURFACE the renderer never created a window")
        close(frame)
        kotlin.system.exitProcess(4)
    }

    println("SURFACE scaling: ${DesktopNativeRenderer.nativeDescribeScaling(windowHandle)}")
    val geometry = DesktopNativeRenderer.nativeWindowGeometry(windowHandle)
    if (geometry != null && geometry.size >= 4) {
        val centreX = (geometry[0] + geometry[2] / 2).toInt()
        val centreY = (geometry[1] + geometry[3] / 2).toInt()
        val under = DesktopNativeRenderer.nativeWindowAtPoint(centreX, centreY)
        println("SURFACE at ($centreX, $centreY) the window under the cursor is $under")
    }

    val second = SurfaceDiagnostics.describe()
    println("SURFACE later $second")
    val presented = Regex("frames=(\\d+)").find(second)?.groupValues?.get(1)?.toIntOrNull() ?: -1
    val cells = Regex("cells=(\\d+)").find(second)?.groupValues?.get(1)?.toIntOrNull() ?: -1
    val lastFrame = Regex("lastFrame=(\\d+)").find(second)?.groupValues?.get(1)?.toIntOrNull() ?: -1
    println("SURFACE presented=$presented cells=$cells lastFrameStatus=$lastFrame")

    close(frame)

    if (presented <= 0) {
        System.err.println("SURFACE the renderer presented no frames while its tab was visible")
        kotlin.system.exitProcess(5)
    }
    if (lastFrame != 0) {
        System.err.println("SURFACE the renderer reported a failed frame: $lastFrame")
        kotlin.system.exitProcess(6)
    }
    println("SURFACE ok")
}

private fun close(frame: AtomicReference<Frame?>) {
    frame.get()?.let { window ->
        SwingUtilities.invokeLater {
            window.isVisible = false
            window.dispose()
        }
    }
}
