/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.settings.CellularityThemeColors
import com.cytarch.cellularity.shared.settings.CellularityThemeComponents
import com.cytarch.cellularity.shared.ui.theme.CellularityTheme
import java.awt.Dimension
import java.awt.Frame
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import javax.imageio.ImageIO

/**
 * Opens the play screen on a live simulation and photographs it, over and over.
 *
 * This is the one step of the port that no assertion covers: the journey from the menu into a
 * running plate is something only a window can show, and a mistake in it looks like an empty
 * window rather than a failing test. The probe therefore stays up while the run is watched, and
 * keeps writing pictures of its own window to `build/diagnostics/playscreen`.
 *
 * The pictures used to be impossible. The world lived in a child window of its own, so what the
 * bridge could capture was that window's surface -- which the GPU may never have written the
 * pixels into -- and every capture came back as one flat colour. The frame is drawn by the
 * application's own scene now, in the same window as the interface, so a capture of that window
 * is a picture of everything: the world, and the interface above it.
 *
 * Run through `:desktop:runPlayScreen`, and drive the window with real input while it runs.
 */
fun main() {
    // A repository of its own under the build directory, so a probe never reads or writes the
    // genomes the person running it has saved.
    val probeRepository = DesktopCellularityFileRepository(
        Files.createTempDirectory("cellularity-probe").resolve("substrates")
    )
    val pictures = Path.of("build", "diagnostics", "playscreen")
    Files.createDirectories(pictures)

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
                // The application's own theme, not Material's fallback: the probe exists to show
                // what the application looks like, and a plate drawn outside the theme would show
                // the lilac palette of the default scheme instead of the one the settings chose.
                CellularityTheme(
                    themeColors = CellularityThemeColors.Default,
                    themeComponents = CellularityThemeComponents.Default
                ) {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        fileRepository = probeRepository,
                        onExit = { },
                        // The renderer's own readout, so that what the probe photographs is also
                        // the evidence that the setting reaches the session.
                        performanceHudEnabled = true
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
    println("PLAYSCREEN pictures=${pictures.toAbsolutePath()}")
    System.out.flush()

    // The window is found by asking the bridge, rather than by reflecting into AWT for a handle:
    // the bridge already enumerates windows and reports their titles, and a probe that reaches
    // into the toolkit's internals is a probe that breaks when the toolkit changes.
    var windowHandle = 0L
    val findDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
    while (System.nanoTime() < findDeadline && windowHandle == 0L) {
        windowHandle = findWindowTitled("Cellularity play screen")
        if (windowHandle == 0L) Thread.sleep(500)
    }
    println("PLAYSCREEN window=${if (windowHandle != 0L) "0x" + windowHandle.toString(16) else "not found"}")

    var capture = 0
    val runUntil = System.nanoTime() + TimeUnit.MINUTES.toNanos(20)
    try {
        while (System.nanoTime() < runUntil) {
            Thread.sleep(CAPTURE_INTERVAL_MILLIS)
            if (windowHandle == 0L) continue
            capture++
            val bitmap = pictures.resolve(String.format("%03d.bmp", capture))
            val bytes = DesktopNativeRenderer.nativeCaptureWindow(windowHandle, bitmap.toString())
            if (bytes > 0) {
                toPng(bitmap, pictures.resolve(String.format("%03d.png", capture)))
                Files.deleteIfExists(bitmap)
                println("PLAYSCREEN capture $capture written")
            } else {
                println("PLAYSCREEN capture $capture refused (status $bytes)")
            }
            System.out.flush()
        }
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

/**
 * The handle of the window with this title, from the bridge's own enumeration.
 *
 * The bridge reports one line per window it knows about, with the handle in decimal and the
 * class and title beside it, which is enough to find the frame this probe opened without asking
 * AWT for a handle it does not publish.
 */
private fun findWindowTitled(title: String): Long {
    val described = DesktopNativeRenderer.nativeDescribeWindows() ?: return 0L
    for (line in described.lineSequence()) {
        if (!line.contains(title)) continue
        val match = Regex("hwnd=(0x[0-9a-fA-F]+|\\d+)").find(line) ?: continue
        val text = match.groupValues[1]
        val value = if (text.startsWith("0x")) {
            text.substring(2).toLongOrNull(16)
        } else {
            text.toLongOrNull()
        }
        if (value != null && value != 0L) return value
    }
    return 0L
}

/** Converts a captured bitmap to a picture this repository and a viewer can both read. */
private fun toPng(source: Path, destination: Path) {
    ImageIO.read(source.toFile())?.let { image ->
        ImageIO.write(image, "png", destination.toFile())
    }
}

/** How often a picture is taken while the probe is up. */
private const val CAPTURE_INTERVAL_MILLIS = 2_000L
