/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import androidx.compose.ui.awt.ComposeWindow
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.nativebridge.SurfaceDiagnostics
import com.cytarch.cellularity.shared.model.SimulationContract
import java.awt.Dimension
import java.awt.Window
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Reports where the native child window ends up, without showing anything.
 *
 * The report this exists for is "the interface disappears when I start an experiment". A
 * screenshot cannot answer it, because the picture would be right either way, and looking at
 * the screen means taking the screen away from whoever is using it. What can be measured is
 * geometry: the simulation is drawn into a Win32 child window parented to the canvas Compose
 * puts in the layout, so if that child is the size of the whole window rather than the size
 * of its place in the layout, it covers the interface and nothing above it can be seen.
 *
 * The window is deliberately never made visible. AWT does not create peers for an invisible
 * window, so the native side has nothing to parent to and no child window exists -- but every
 * coordinate the report is about is decided by the layout, which does run, and by the bounds
 * AWT assigns, which are readable from the hierarchy. That is exactly the part a screenshot
 * would not have told us either.
 *
 * Run through `:desktop:runLayoutProbe`. It prints a report and exits non-zero if the child
 * would cover the window.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val window = AtomicReference<ComposeWindow?>(null)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val frame = ComposeWindow()
            frame.title = "layout probe"
            val panel = ComposePanel()
            panel.preferredSize = Dimension(1280, 800)
            panel.setContent {
                MaterialTheme {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        fileRepository = DesktopCellularityFileRepository(
                            Files.createTempDirectory("cellularity-layout-probe")
                                .resolve("substrates")
                        ),
                        onExit = { }
                    )
                }
            }
            frame.contentPane.add(panel)
            frame.pack()
            // Parked outside every monitor rather than hidden. A hidden window has no peer,
            // and the canvas then has no bounds to report -- which is what the first run of
            // this probe showed, and it is not an answer to the question. A window that is
            // realised but off the desktop gets its peers, lays the canvas out, and puts
            // nothing in front of anybody.
            frame.setLocation(-4000, -4000)
            window.set(frame)
            frame.isVisible = true
        } catch (thrown: Throwable) {
            failure.set(thrown)
        } finally {
            built.countDown()
        }
    }

    if (!built.await(60, TimeUnit.SECONDS)) {
        println("LAYOUT the composition never finished")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        println("LAYOUT failed: $thrown")
        thrown.printStackTrace()
        kotlin.system.exitProcess(3)
    }

    settle(3_000)

    val frame = window.get()!!
    println("LAYOUT window bounds=${boundsOf(frame)}")
    println("LAYOUT surface: ${SurfaceDiagnostics.describe()}")
    println("LAYOUT canvas: ${describeCanvas(frame)}")
    println("LAYOUT window isShowing=${frame.isShowing}")
    reportCoverage(frame)

    // Every heavyweight child AWT knows about. The native window is not among them -- it is
    // created by the renderer -- but the canvas it is parented to is, and its bounds are
    // what the child is placed inside.
    javax.swing.SwingUtilities.invokeLater {
        frame.isVisible = false
        frame.dispose()
    }
    settle(500)
    kotlin.system.exitProcess(0)
}

/**
 * Reports the numbers that decide whether the simulation covers the interface.
 *
 * The simulation is drawn into a Win32 child window positioned over the canvas it was
 * parented to, and a child window is not clipped by anything Compose paints. So the child
 * covers the interface exactly when the canvas is as large as the window's client area: the
 * navigation bar is the only thing below the content, and its height is known. If the canvas
 * reaches into that height, the bar is behind the simulation, and a person sees a running
 * world with no controls and no way back.
 *
 * Pixel colours would not settle it either -- the child window is native and an offscreen
 * image of the frame does not contain it -- so the geometry is the evidence.
 */
private fun reportCoverage(frame: ComposeWindow) {
    val insets = frame.insets
    val clientHeight = frame.height - insets.top - insets.bottom
    // Asked of the surface rather than found by walking the hierarchy: the frame contains
    // several canvases, and "the largest one" is not the one the renderer was parented to.
    val extent = Regex("canvas=(\\d+)x(\\d+)")
        .find(SurfaceDiagnostics.describe())
        ?.groupValues
    if (extent == null) {
        println("LAYOUT the surface did not report a canvas: ${SurfaceDiagnostics.describe()}")
        return
    }
    val canvasWidth = extent[1].toInt()
    val canvasHeight = extent[2].toInt()
    val barHeight = NAVIGATION_BAR_HEIGHT
    val contentHeight = clientHeight - barHeight
    println(
        "LAYOUT client=${frame.width - insets.left - insets.right}x$clientHeight " +
            "navigationBar=$barHeight contentAboveIt=$contentHeight"
    )
    println(
        "LAYOUT renderSurface=${canvasWidth}x$canvasHeight " +
            "coversNavigationBar=${canvasHeight > contentHeight}"
    )
    if (canvasHeight > contentHeight) {
        println(
            "LAYOUT the simulation would cover the navigation bar by " +
                "${canvasHeight - contentHeight} px, leaving no tab bar and no back button"
        )
    } else {
        println("LAYOUT the navigation bar has its ${barHeight} px to itself")
    }
}


/**
 * The height of the play screen's navigation bar.
 *
 * Read from the launcher's own copy of the shared dimensions rather than guessed: if the
 * shared value ever changes, this number has to change with it, and a stale one would make
 * the report say the interface is covered when it is not.
 */
private const val NAVIGATION_BAR_HEIGHT = 56

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}

private fun boundsOf(window: Window): String =
    "${window.x},${window.y} ${window.width}x${window.height}"

/**
 * Finds the heavyweight canvas and reports its place in the window.
 *
 * The canvas is found by type rather than by walking to a known index, because the layout is
 * the shared screen's and its structure is not this probe's business; what matters is whether
 * a heavyweight child ended up the size of the window or the size of its own area.
 */
private fun describeCanvas(frame: ComposeWindow): String {
    val canvases = mutableListOf<java.awt.Component>()
    fun walk(component: java.awt.Component) {
        if (component is java.awt.Canvas) canvases.add(component)
        if (component is java.awt.Container) {
            component.components.forEach(::walk)
        }
    }
    walk(frame)
    if (canvases.isEmpty()) return "no heavyweight canvas in the hierarchy"
    return canvases.joinToString("; ") { canvas ->
        val onScreen = try {
            val location = canvas.locationOnScreen
            "${location.x},${location.y}"
        } catch (_: RuntimeException) {
            "unknown"
        }
        "canvas ${canvas.width}x${canvas.height} at screen $onScreen " +
            "showing=${canvas.isShowing}"
    }
}
