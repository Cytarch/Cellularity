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
import java.awt.Component
import java.awt.Container
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Dumps the whole component tree of the play screen, in painting order.
 *
 * The reports this exists for -- the simulation drawn into a corner, the interface not
 * answering -- cannot be settled by looking at the screen, and looking at the screen means
 * taking it away from whoever is using the machine. What can be settled is structure: which
 * AWT components exist, how large each is, where each sits inside its parent, and in which
 * order they paint. A component that is the size of the window and lands later in the paint
 * order is what the user's mouse reaches, and a canvas smaller than its allotted area is a
 * layout that did not propagate.
 *
 * The window is realised outside every monitor, so peers exist and the layout runs while
 * nothing appears in front of anybody.
 *
 * Run through `:desktop:runTreeProbe`.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val window = AtomicReference<ComposeWindow?>(null)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val frame = ComposeWindow()
            frame.title = "tree probe"
            val panel = ComposePanel()
            panel.preferredSize = java.awt.Dimension(1100, 700)
            panel.setContent {
                MaterialTheme {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        fileRepository = DesktopCellularityFileRepository(
                            Files.createTempDirectory("cellularity-tree-probe")
                                .resolve("substrates")
                        ),
                        onExit = { }
                    )
                }
            }
            frame.contentPane.add(panel)
            frame.pack()
            frame.setLocation(-4400, -4400)
            window.set(frame)
            frame.isVisible = true
        } catch (thrown: Throwable) {
            failure.set(thrown)
        } finally {
            built.countDown()
        }
    }

    if (!built.await(60, TimeUnit.SECONDS)) {
        println("TREE the window was never built")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        println("TREE could not open the play screen: $thrown")
        kotlin.system.exitProcess(3)
    }

    settle(12_000)

    val frame = window.get()!!
    println("TREE window ${frame.width}x${frame.height} insets=$(${frame.insets})")
    println("TREE contentPane ${frame.contentPane.width}x${frame.contentPane.height}")
    println("TREE surface: ${SurfaceDiagnostics.describe()}")
    println("TREE --- component tree, index 0 is deepest in paint order ---")
    dump(frame, depth = 0)

    javax.swing.SwingUtilities.invokeLater {
        frame.isVisible = false
        frame.dispose()
    }
    settle(500)
    kotlin.system.exitProcess(0)
}

/**
 * Prints one component and everything inside it.
 *
 * The index within the parent is printed because it is the z-order: Swing paints from the
 * last child backwards, so a component with a high index is in front of its siblings. That is
 * the number that says which component a click at a given point actually reaches.
 */
private fun dump(component: Component, depth: Int) {
    val parent = component.parent
    val index = if (parent is Container) parent.getComponentZOrder(component) else -1
    val kind = component.javaClass.simpleName
    val name = component.name?.let { " name=$it" }.orEmpty()
    val location = try {
        val onScreen = component.locationOnScreen
        " screen=${onScreen.x},${onScreen.y}"
    } catch (_: RuntimeException) {
        " screen=unknown"
    }
    println(
        "TREE ${"  ".repeat(depth)}[$index] $kind$name " +
            "${component.x},${component.y} ${component.width}x${component.height}" +
            " showing=${component.isShowing} focusable=${component.isFocusable}$location"
    )
    if (component is Container) {
        component.components.forEach { dump(it, depth + 1) }
    }
}

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}
