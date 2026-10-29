/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import androidx.compose.ui.awt.ComposeWindow
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.model.SimulationContract
import java.awt.Dimension
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Asks Windows who a click would actually reach inside the play screen.
 *
 * This is the question the whole interop stands or falls on, and it cannot be answered from
 * either side. The simulation is a native child window; the interface is drawn by Compose
 * into the same top-level window. Whether a click reaches the simulation depends on which of
 * them Win32 considers to be on top at that pixel, and neither system knows about the other's
 * windows -- reading either one's own opinion would be reading the wrong witness.
 *
 * So a grid of points across the viewport is put to `WindowFromPoint`, and each answer is
 * classified: a window the bridge made, some other window in the same top-level window, or
 * somewhere else. A grid of "bridge" means the simulation is reachable and whatever is wrong
 * lies elsewhere. A grid of "other" means the interface is drawing over the simulation, and
 * that is the same reason the controls do not answer: the two are not layered, they are
 * stacked, and only one of them can be on top.
 *
 * The window is realised outside every monitor, so the session is real and the points are
 * real while nothing appears in front of anybody.
 *
 * Run through `:desktop:runHitTestProbe`. Exits non-zero when the simulation is not reachable.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val window = AtomicReference<ComposeWindow?>(null)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val frame = ComposeWindow()
            frame.title = "hit test probe"
            val panel = ComposePanel()
            panel.preferredSize = Dimension(1100, 700)
            panel.setContent {
                MaterialTheme {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        fileRepository = DesktopCellularityFileRepository(
                            Files.createTempDirectory("cellularity-hit-probe")
                                .resolve("substrates")
                        ),
                        onExit = { }
                    )
                }
            }
            frame.contentPane.add(panel)
            frame.pack()
            frame.setLocation(-4800, -4800)
            window.set(frame)
            frame.isVisible = true
        } catch (thrown: Throwable) {
            failure.set(thrown)
        } finally {
            built.countDown()
        }
    }

    if (!built.await(60, TimeUnit.SECONDS)) {
        println("HIT the window was never built")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        println("HIT could not open the play screen: $thrown")
        kotlin.system.exitProcess(3)
    }

    settle(8_000)

    val frame = window.get()!!
    val description = surfaceDescription()
    println("HIT surface: $description")
    val geometry = windowGeometry()
    if (geometry == null) {
        println("HIT the bridge reported no window geometry")
        kotlin.system.exitProcess(4)
    }
    val offsetX = geometry[6]
    val offsetY = geometry[7]
    val width = geometry[2]
    val height = geometry[3]
    val origin = frame.locationOnScreen
    println("HIT window at screen ${origin.x},${origin.y}; child ${width}x$height at ($offsetX,$offsetY)")

    // Client-area coordinates of the child, since the frame's own origin includes its
    // decorations and the child is placed relative to the client area.
    val insets = frame.insets
    val childScreenX = origin.x + insets.left + offsetX.toInt()
    val childScreenY = origin.y + insets.top + offsetY.toInt()

    // A grid rather than a single point: the corner is where the report says the picture is,
    // and the middle is where a person would click, and they can disagree.
    val fractions = listOf(0.05, 0.25, 0.5, 0.75, 0.95)
    var bridgePoints = 0
    var otherPoints = 0
    var elsewherePoints = 0
    for (fractionY in fractions) {
        val row = fractions.joinToString(" ") { fractionX ->
            val x = childScreenX + (width * fractionX).toInt()
            val y = childScreenY + (height * fractionY).toInt()
            when (windowAtPoint(x, y)) {
                1 -> {
                    bridgePoints++
                    "sim"
                }
                0 -> {
                    otherPoints++
                    "UI "
                }
                else -> {
                    elsewherePoints++
                    "?  "
                }
            }
        }
        println("HIT y=${(height * fractionY).toInt()}: $row")
    }
    println(
        "HIT over the viewport: simulation=$bridgePoints interface=$otherPoints elsewhere=$elsewherePoints"
    )

    // Two more points outside the viewport, where the interface is expected to be reachable.
    // If those are also covered, the problem is not the viewport's layering but the window's.
    val barX = childScreenX + 40
    val barY = childScreenY + height.toInt() + 28
    println("HIT over the navigation bar: ${classify(windowAtPoint(barX, barY))}")

    val reached = bridgePoints > 0
    println(
        if (reached) {
            "HIT the simulation is reachable at $bridgePoints of the sampled points"
        } else {
            "HIT the simulation is not reachable anywhere: the interface is over it"
        }
    )

    javax.swing.SwingUtilities.invokeLater {
        frame.isVisible = false
        frame.dispose()
    }
    settle(500)
    kotlin.system.exitProcess(if (reached) 0 else 5)
}

private fun classify(value: Int): String = when (value) {
    1 -> "simulation"
    0 -> "interface"
    else -> "somewhere else ($value)"
}

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}

private fun surfaceDescription(): String =
    invokeDiagnostics("describe") as? String ?: "the surface did not describe itself"

private fun windowGeometry(): LongArray? {
    val handle = Regex("window=(\\d+)").find(surfaceDescription())?.groupValues?.get(1)
        ?.toLongOrNull() ?: return null
    return invokeRendererValues("nativeWindowGeometry", arrayOf(java.lang.Long.TYPE), arrayOf(handle)) as? LongArray
}

private fun windowAtPoint(x: Int, y: Int): Int =
    invokeRendererValues(
        "nativeWindowAtPoint",
        arrayOf(java.lang.Integer.TYPE, java.lang.Integer.TYPE),
        arrayOf(x, y)
    ) as? Int ?: -2

/**
 * Reaches the launcher's diagnostics and renderer reflectively.
 *
 * Reflection because this probe stands outside the launcher's internals on purpose: it
 * measures the renderer and the operating system rather than trusting the launcher's own
 * account of either.
 */
private fun invokeDiagnostics(name: String): Any? = try {
    val owner = Class.forName(
        "com.cytarch.cellularity.desktop.nativebridge.SurfaceDiagnostics"
    )
    owner.getMethod(name).invoke(owner.getField("INSTANCE").get(null))
} catch (thrown: ReflectiveOperationException) {
    println("HIT could not reach the surface: $thrown")
    null
}

private fun invokeRendererValues(
    name: String,
    types: Array<Class<*>>,
    values: Array<Any>
): Any? = try {
    val renderer = Class.forName(
        "com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer"
    )
    renderer.getMethod(name, *types).invoke(renderer.getField("INSTANCE").get(null), *values)
} catch (thrown: ReflectiveOperationException) {
    println("HIT could not reach the renderer's $name: $thrown")
    null
}
