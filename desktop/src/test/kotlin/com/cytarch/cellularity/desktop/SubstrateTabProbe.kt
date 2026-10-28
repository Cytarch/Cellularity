/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.awt.ComposePanel
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import java.awt.Canvas
import java.awt.Color
import java.awt.Dimension
import java.awt.Frame
import java.awt.Rectangle
import java.awt.Robot
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import javax.imageio.ImageIO

/**
 * Opens and closes the substrate panel repeatedly, on a world that is actually running.
 *
 * The panel is not always visible -- it is one tab among several -- and a composable that
 * emits nothing on one pass and a whole screen on the next is the classic way to make
 * Compose fail with a group mismatch, which it does at the moment the tab is first opened
 * rather than at build time. So this toggles it the way the tab bar does, looks for a
 * crash, and photographs it open to show what the world reports.
 *
 * Run through `:desktop:runSubstrateTab`. It exits non-zero if the window or the toggling
 * raised anything; a missing renderer library is reported and skipped rather than failed.
 */
fun main() {
    if (!DesktopNativeRenderer.isAvailable) {
        println("SUBSTRATE the renderer library was not built; nothing to open a session on")
        kotlin.system.exitProcess(0)
    }
    val roots = DesktopAssetRoots.resolve()
    if (roots == null) {
        println("SUBSTRATE the compiled shaders and textures were not found")
        kotlin.system.exitProcess(0)
    }

    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val frame = AtomicReference<Frame?>(null)
    val visibleState = mutableStateOf(true)

    val ownerFrame = Frame("Cellularity substrate tab owner")
    val canvas = Canvas().apply {
        background = Color.BLACK
        preferredSize = Dimension(640, 480)
    }
    ownerFrame.add(canvas)
    ownerFrame.pack()
    ownerFrame.setLocation(0, 0)
    ownerFrame.isVisible = true

    val ownerHandle = nativeWindowHandleOf(canvas)
    check(ownerHandle != 0L) { "the owner canvas has no native window" }
    val windowHandle = DesktopNativeRenderer.nativeCreateWindow(
        ownerHandle, 0, 0, canvas.width, canvas.height
    )
    check(windowHandle != 0L) { "the renderer did not create its window" }
    val sessionHandle = DesktopNativeRenderer.nativeCreateSession(
        windowHandle,
        canvas.width,
        canvas.height,
        1_000,
        3_500,
        1.0,
        roots.shaderRoot.toString(),
        roots.textureRoot.toString()
    )
    check(sessionHandle != 0L) { "the simulation did not start" }

    // Let the world produce something, so the panel has a reading to show rather than its
    // loading state. Off the event thread, which is where the renderer wants to be driven.
    val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15)
    while (System.nanoTime() < deadline && DesktopNativeRenderer.nativeCellCount(sessionHandle) == 0L) {
        DesktopNativeRenderer.nativeRenderFrame(sessionHandle)
        Thread.sleep(16)
    }

    val gateway = DesktopSimulationGateway(sessionHandle = sessionHandle)
    // The panel reads the world on its own schedule; the probe only needs the first reading
    // to have happened, so the gateway is driven on a scope of its own and stopped at the end.
    val gatewayScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    gatewayScope.launch { gateway.start() }

    javax.swing.SwingUtilities.invokeLater {
        try {
            val window = Frame("Cellularity substrate tab")
            val panel = ComposePanel()
            panel.preferredSize = Dimension(1100, 700)
            window.add(panel)
            window.pack()
            window.setLocation(40, 40)
            panel.setContent {
                MaterialTheme {
                    val visible by visibleState
                    SubstrateTab(
                        gateway = gateway,
                        isVisible = visible,
                        plateRadius = 1.0,
                        selectedTemperature = 0,
                        onTemperatureSelected = { },
                        onPlateActionClick = { }
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
        System.err.println("SUBSTRATE the event thread never finished building the window")
        kotlin.system.exitProcess(2)
    }
    settle(3_000)

    // The toggling is the point: each flip changes whether the screen is composed at all,
    // at the same position in the composition.
    var toggles = 0
    repeat(6) { round ->
        visibleState.value = round % 2 == 1
        settle(700)
        toggles++
    }
    visibleState.value = true
    settle(1_500)

    val outputDirectory = File("build").also { it.mkdirs() }
    val captured = capture(frame.get()!!, File(outputDirectory, "substrate-tab.png"))

    val state = gateway.confirmedEnvironment.value
    println("SUBSTRATE window up, toggled $toggles times without raising")
    println("SUBSTRATE confirmedEnvironmentPresent=${state != null}")
    println("SUBSTRATE stats=${gateway.stats.value}")
    println("SUBSTRATE capture=${captured?.name ?: "none"}")
    System.out.flush()

    settle(500)
    javax.swing.SwingUtilities.invokeLater {
        frame.get()?.let { window ->
            window.isVisible = false
            window.dispose()
        }
    }
    gatewayScope.cancel()
    DesktopNativeRenderer.nativeStopSession(sessionHandle)
    DesktopNativeRenderer.nativeDestroyWindow(windowHandle)
    ownerFrame.isVisible = false
    ownerFrame.dispose()
    failure.get()?.let { thrown: Throwable ->
        System.err.println("SUBSTRATE raised: $thrown")
        thrown.printStackTrace()
        kotlin.system.exitProcess(3)
    }
    kotlin.system.exitProcess(0)
}

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}

private fun capture(window: Frame, destination: File): File? = try {
    val image = Robot().createScreenCapture(Rectangle(window.locationOnScreen, window.size))
    ImageIO.write(image, "png", destination)
    destination
} catch (thrown: Throwable) {
    System.err.println("SUBSTRATE could not capture ${destination.name}: $thrown")
    null
}

/** The same lookup the interface uses, needed because the renderer parents a child window. */
private fun nativeWindowHandleOf(component: java.awt.Component): Long {
    if (!component.isShowing) return 0L
    return try {
        val peerField = java.awt.Component::class.java.getDeclaredField("peer")
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
