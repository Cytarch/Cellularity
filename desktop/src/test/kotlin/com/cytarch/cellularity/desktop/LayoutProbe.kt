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
    reportNativeGeometry()
    reportWindowStack()
    System.out.flush()
}

/**
 * Reports the child window's rectangle next to the room reserved for it.
 *
 * These two are decided by different systems and neither can see the other's numbers. Win32
 * owns the child window's rectangle, set by this bridge. Compose owns the layout, which
 * decides where the interface puts the interop area. A disagreement between them shows up
 * only as a picture of the wrong size in the wrong place -- the simulation drawn into a
 * corner, say -- and it cannot be found by reading either side alone.
 */
private fun reportNativeGeometry() {
    val handle = Regex("window=(\\d+)").find(surfaceDescription())?.groupValues?.get(1)
        ?.toLongOrNull()
    if (handle == null) {
        println("LAYOUT the surface did not report a window handle: ${surfaceDescription()}")
        return
    }
    val geometry = nativeWindowGeometry(handle)
    if (geometry == null || geometry.size < 8) {
        println("LAYOUT the bridge returned no geometry for window $handle")
        return
    }
    val width = geometry[2]
    val height = geometry[3]
    val parentWidth = geometry[4]
    val parentHeight = geometry[5]
    val offsetX = geometry[6]
    val offsetY = geometry[7]
    println(
        "LAYOUT nativeWindow=${width}x$height at ($offsetX,$offsetY) in its parent; " +
            "parentClient=${parentWidth}x$parentHeight"
    )
    // The child covers a band that starts where it was placed. On the play screen that band
    // has to stop above the navigation bar, and it has to start where the interface left room
    // for it -- which is what the offset says.
    println(
        "LAYOUT nativeWindow bottom edge=${offsetY + height} " +
            "parentClientHeight=$parentHeight"
    )
    reportSurfaceExtent()
}

/**
 * Compares the extent the surface presents at with the size of the canvas it sits in.
 *
 * These two are the whole of "the simulation is drawn into a corner". A window created at one
 * size whose surface was never resized keeps presenting into that first extent for the rest
 * of its life, which draws a picture filling part of its own viewport -- and nothing about
 * that is visible from the layout, because the window itself is the right size. So the two
 * numbers are read and compared, and a disagreement is the failure rather than a hint.
 */
private fun reportSurfaceExtent() {
    val session = Regex("session=(\\d+)").find(surfaceDescription())?.groupValues?.get(1)
        ?.toLongOrNull()
        ?.takeIf { it != 0L }
    if (session == null) {
        println("LAYOUT no live session to ask about its surface")
        return
    }
    val extent = nativeSurfaceExtent(session)
    val canvas = Regex("canvas=(\\d+)x(\\d+)").find(surfaceDescription())?.groupValues
    if (extent == null || extent.size < 2) {
        println("LAYOUT the session has presented no frame yet")
        return
    }
    val surfaceWidth = extent[0]
    val surfaceHeight = extent[1]
    if (canvas == null) {
        println("LAYOUT presenting at ${surfaceWidth}x$surfaceHeight; canvas unknown")
        return
    }
    val canvasWidth = canvas[1].toLong()
    val canvasHeight = canvas[2].toLong()
    println(
        "LAYOUT presenting at ${surfaceWidth}x$surfaceHeight " +
            "in a canvas of ${canvasWidth}x$canvasHeight " +
            "fills=${surfaceWidth == canvasWidth && surfaceHeight == canvasHeight}"
    )
    if (surfaceWidth != canvasWidth || surfaceHeight != canvasHeight) {
        println(
            "LAYOUT the picture does not fill its viewport: it is drawn into " +
                "${surfaceWidth}x$surfaceHeight of ${canvasWidth}x$canvasHeight"
        )
    }
}

/**
 * Calls the bridge's surface extent reader reflectively, for the same reason as the rest:
 * the probe measures the renderer rather than trusting the launcher's account of it.
 */
private fun nativeSurfaceExtent(session: Long): LongArray? = try {
    val renderer = Class.forName(
        "com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer"
    )
    val instance = renderer.getField("INSTANCE").get(null)
    renderer.getMethod("nativeSurfaceExtent", java.lang.Long.TYPE).invoke(instance, session)
        as? LongArray
} catch (thrown: ReflectiveOperationException) {
    println("LAYOUT could not read the surface extent: $thrown")
    null
}

/**
 * Prints the process's windows in their real stacking order.
 *
 * This is the measurement that says whether the simulation is underneath the interface or
 * merely next to it: a window listed above the renderer takes clicks before the renderer sees
 * them, and no amount of layout reasoning can show that.
 */
private fun reportWindowStack() {
    val report = try {
        val renderer = Class.forName(
            "com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer"
        )
        val instance = renderer.getField("INSTANCE").get(null)
        renderer.getMethod("nativeDescribeWindows").invoke(instance) as? String
    } catch (thrown: ReflectiveOperationException) {
        println("LAYOUT could not read the window stack: $thrown")
        null
    }
    if (report.isNullOrBlank()) {
        println("LAYOUT the process reported no windows")
        return
    }
    println("LAYOUT --- this process's windows, topmost first ---")
    report.trim().lines().forEach { println("LAYOUT   $it") }
}

/**
 * Calls the bridge's geometry reader reflectively.
 *
 * Reflection because this probe stands outside the launcher's internals on purpose: it
 * measures the renderer rather than trusting the launcher's own account of it, and a direct
 * call would break whenever that signature changes, which is not what the probe is for.
 */
private fun nativeWindowGeometry(handle: Long): LongArray? = try {
    val renderer = Class.forName(
        "com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer"
    )
    val instance = renderer.getField("INSTANCE").get(null)
    renderer.getMethod("nativeWindowGeometry", java.lang.Long.TYPE).invoke(instance, handle)
        as? LongArray
} catch (thrown: ReflectiveOperationException) {
    println("LAYOUT could not read the window geometry: $thrown")
    null
}


/**
 * The height of the play screen's navigation bar.
 *
 * Read from the launcher's own copy of the shared dimensions rather than guessed: if the
 * shared value ever changes, this number has to change with it, and a stale one would make
 * the report say the interface is covered when it is not.
 */
private const val NAVIGATION_BAR_HEIGHT = 56

/**
 * Asks the mounted surface to describe itself, reflectively.
 *
 * Reflection rather than a direct call because this probe stands outside the launcher's
 * internals on purpose: it measures the renderer rather than trusting the launcher's own
 * account of it, and a direct call would stop compiling whenever that object changes shape,
 * which is not what the probe is for.
 */
private fun surfaceDescription(): String = try {
    val diagnostics = Class.forName(
        "com.cytarch.cellularity.desktop.nativebridge.SurfaceDiagnostics"
    )
    val instance = diagnostics.getField("INSTANCE").get(null)
    diagnostics.getMethod("describe").invoke(instance) as? String
        ?: "the surface did not describe itself"
} catch (thrown: ReflectiveOperationException) {
    "the surface could not be described: $thrown"
} catch (thrown: RuntimeException) {
    "the surface could not be described: $thrown"
}

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
