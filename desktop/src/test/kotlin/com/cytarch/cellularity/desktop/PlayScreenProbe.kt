/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.model.SimulationContract
import java.awt.Dimension
import java.awt.Frame
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Opens the play screen on a live simulation, to be watched.
 *
 * This is the one step of the port that no assertion covers: the journey from the menu
 * into a running plate is something only a window can show, and a mistake in it looks
 * like an empty window rather than a failing test. The program reports what it built and
 * then stays up; whether the picture is right is for a person to say.
 *
 * Run through `:desktop:runPlayScreen`.
 */
fun main() {
    // A repository of its own under the build directory, so a probe never reads or
    // writes the genomes the person running it has saved.
    val probeRepository = DesktopCellularityFileRepository(
        java.nio.file.Files.createTempDirectory("cellularity-probe").resolve("substrates")
    )

    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val frame = AtomicReference<Frame?>(null)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val window = Frame("Cellularity play screen")
            val panel = ComposePanel()
            panel.preferredSize = Dimension(1100, 700)
            window.add(panel)
            window.pack()
            window.setLocation(40, 40)
            panel.setContent {
                MaterialTheme {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        fileRepository = probeRepository,
                        onExit = { }
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
        System.err.println("PLAYSCREEN the event thread never finished building the window")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        System.err.println("PLAYSCREEN failed to open: $thrown")
        thrown.printStackTrace()
        kotlin.system.exitProcess(3)
    }

    println("PLAYSCREEN renderer available=${DesktopNativeRenderer.isAvailable}")
    println("PLAYSCREEN shader root=${DesktopAssetRoots.resolve()?.shaderRoot}")
    println("PLAYSCREEN the window is up")
    System.out.flush()

    try {
        Thread.sleep(TimeUnit.MINUTES.toMillis(10))
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    } finally {
        frame.get()?.let { window ->
            javax.swing.SwingUtilities.invokeLater {
                window.isVisible = false
                window.dispose()
            }
        }
    }
}
