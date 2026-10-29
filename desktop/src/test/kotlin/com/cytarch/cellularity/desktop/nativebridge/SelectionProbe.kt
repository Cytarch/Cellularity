/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import androidx.compose.ui.awt.ComposeWindow
import com.cytarch.cellularity.desktop.DesktopPlayScreen
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.model.SimulationContract
import java.awt.Dimension
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Asks whether a tap reaches the simulation, by tapping where cells actually are.
 *
 * The first version of this probe tapped the middle of the plate and reported that nothing was
 * selected, which was read as "input does not reach the simulation". That reading was wrong,
 * and the reason is in the selection rule itself: a tap selects a cell only if there is a cell
 * under it, and a plate seeded at random does not have one at its exact centre. Tapping the
 * centre of an empty dish proves nothing about input, and reporting it as a failure would have
 * sent the next hours of work after a fault that was not there.
 *
 * So the probe taps a grid across the dish and counts how many taps select a cell. A grid that
 * selects nothing anywhere is a real fault: the taps are not arriving, or not being converted,
 * or not being applied. A grid that selects at some points and not others is a working input
 * path over a dish with gaps in it, which is what a dish of scattered cells is.
 *
 * A tap that selects is followed by reading that cell's document, because that is what the
 * diagnosis panel will do: the two halves have to agree about which cell was read, and one of
 * them is written by native code that no managed test can check on its own.
 *
 * The window is realised outside every monitor, so a real session runs while nothing appears
 * in front of anybody. Run through `:desktop:runSelectionProbe`.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val window = AtomicReference<ComposeWindow?>(null)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val frame = ComposeWindow()
            frame.title = "selection probe"
            val panel = ComposePanel()
            panel.preferredSize = Dimension(1100, 700)
            panel.setContent {
                MaterialTheme {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        fileRepository = DesktopCellularityFileRepository(
                            Files.createTempDirectory("cellularity-selection-probe")
                                .resolve("substrates")
                        ),
                        onExit = { }
                    )
                }
            }
            frame.contentPane.add(panel)
            frame.pack()
            frame.setLocation(-4950, -4950)
            window.set(frame)
            frame.isVisible = true
        } catch (thrown: Throwable) {
            failure.set(thrown)
        } finally {
            built.countDown()
        }
    }

    if (!built.await(60, TimeUnit.SECONDS)) {
        println("SELECTION the window was never built")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        println("SELECTION could not open the play screen: $thrown")
        kotlin.system.exitProcess(3)
    }

    val frame = window.get()!!
    val session = awaitSession()
    if (session == 0L) {
        println("SELECTION no session started; the surface said: ${SurfaceDiagnostics.describe()}")
        kotlin.system.exitProcess(4)
    }
    val description = SurfaceDiagnostics.describe()
    println("SELECTION session started: $description")
    val extent = Regex("canvas=(\\d+)x(\\d+)").find(description)?.groupValues
    if (extent == null) {
        println("SELECTION the surface did not report its canvas extent")
        kotlin.system.exitProcess(5)
    }
    val width = extent[1].toFloat()
    val height = extent[2].toFloat()
    val cells = DesktopNativeRenderer.nativeCellCount(session)
    println("SELECTION the dish holds $cells cells, in a canvas of ${width.toInt()}x${height.toInt()}")

    var selected = 0
    var refused = 0
    var firstSelectedId = -1
    // A grid across the middle of the viewport. The dish is drawn centred and smaller than the
    // canvas, so the outermost fractions are skipped: a tap outside the dish is a legitimate
    // tap that finds nothing, and including those would only dilute the count this exists for.
    val fractions = listOf(0.30f, 0.40f, 0.50f, 0.60f, 0.70f)
    for (fractionY in fractions) {
        val row = StringBuilder()
        for (fractionX in fractions) {
            val x = width * fractionX
            val y = height * fractionY
            val queued = DesktopNativeRenderer.nativeQueueTap(session, x, y)
            if (queued != 1) {
                refused++
                row.append('R')
                continue
            }
            val id = awaitSelection(session)
            if (id == null) {
                row.append('.')
            } else {
                selected++
                if (firstSelectedId < 0) {
                    // Left selected deliberately. The genome is read from whatever the
                    // simulation currently has selected, so deselecting here -- as the first
                    // version of this probe did, to keep later taps from toggling -- made the
                    // genome read come back empty and look like a fault in the read rather
                    // than in the order the two were done in.
                    firstSelectedId = id
                } else {
                    // Tapped again to deselect, so a later tap is a selection rather than a
                    // toggle that switches off the cell an earlier tap had selected.
                    DesktopNativeRenderer.nativeQueueTap(session, x, y)
                    awaitSelectionCleared(session)
                }
                row.append('#')
            }
        }
        println("SELECTION tap row at y=${(height * fractionY).toInt()}: $row")
    }

    println("SELECTION taps that selected a cell: $selected of ${fractions.size * fractions.size}")
    if (refused > 0) {
        println("SELECTION taps the bridge refused: $refused")
    }

    if (selected == 0) {
        println(
            "SELECTION no tap selected a cell anywhere on the dish: input is not reaching the " +
                "simulation, or is not being applied"
        )
        shutdown(frame)
        kotlin.system.exitProcess(6)
    }

    // The document for a cell the simulation itself says is selected. The two halves of this
    // port have to agree about which cell that is: the identifier comes from the runtime's
    // status and the document is written by the same runtime, so a mismatch means one of them
    // is describing a different cell than the other.
    val document = DesktopNativeRenderer.nativeInspectCell(session, firstSelectedId)
    if (document == null) {
        println("SELECTION the runtime wrote no document for the selected cell $firstSelectedId")
        shutdown(frame)
        kotlin.system.exitProcess(7)
    }
    val decoded = try {
        com.cytarch.cellularity.shared.model.CellSnapshotWireCodec.decode(document)
    } catch (thrown: Throwable) {
        println(
            "SELECTION the document did not decode: ${thrown::class.simpleName}: ${thrown.message}"
        )
        shutdown(frame)
        kotlin.system.exitProcess(8)
    }
    if (decoded == null) {
        println("SELECTION the document decoded to nothing")
        shutdown(frame)
        kotlin.system.exitProcess(9)
    }
    if (decoded.id != firstSelectedId) {
        println("SELECTION the snapshot is for cell ${decoded.id}, not $firstSelectedId")
        shutdown(frame)
        kotlin.system.exitProcess(10)
    }
    println(
        "SELECTION decoded cell ${decoded.id}: type=${decoded.cellType} " +
            "radius=${decoded.radius} mass=${decoded.mass} age=${decoded.age} " +
            "geneIndex=${decoded.geneIndex} mirrored=${decoded.mirrored}"
    )

    val genome = DesktopNativeRenderer.nativeInspectSelectedGenome(session)
    if (genome == null) {
        println("SELECTION the runtime wrote no genome for the selected cell")
    } else {
        val draft = try {
            com.cytarch.cellularity.shared.model.GenomeDraftWireCodec.decode(genome)
        } catch (thrown: Throwable) {
            println(
                "SELECTION the genome did not decode: ${thrown::class.simpleName}: ${thrown.message}"
            )
            shutdown(frame)
            kotlin.system.exitProcess(11)
        }
        println(
            "SELECTION decoded its genome: ${draft.genes.size} genes, " +
                "and the cell's gene ${decoded.geneIndex} is " +
                "${draft.genes.getOrNull(decoded.geneIndex)?.cellType}"
        )
    }

    println("SELECTION input reaches the simulation and the selected cell can be read back")
    shutdown(frame)
    kotlin.system.exitProcess(0)
}

private fun shutdown(frame: ComposeWindow) {
    javax.swing.SwingUtilities.invokeLater {
        frame.isVisible = false
        frame.dispose()
    }
    settle(500)
}

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}

private fun awaitSession(): Long {
    repeat(SESSION_WAIT_STEPS) {
        val match = Regex("session=(\\d+)").find(SurfaceDiagnostics.describe())
        val value = match?.groupValues?.get(1)?.toLongOrNull() ?: 0L
        if (value != 0L) return value
        settle(SESSION_WAIT_MILLIS)
    }
    return 0L
}

/**
 * Waits for the runtime to report a selected cell.
 *
 * The tap is queued and applied on the runtime's own thread, so the selection appears in the
 * status some frames later; polling is the only way to see it, and the timeout is what turns
 * "not yet" into "no".
 */
private fun awaitSelection(session: Long): Int? {
    repeat(SELECTION_WAIT_STEPS) {
        val selected = selectedCellId(session)
        if (selected != null) return selected
        settle(SELECTION_WAIT_MILLIS)
    }
    return null
}

private fun awaitSelectionCleared(session: Long) {
    repeat(SELECTION_WAIT_STEPS) {
        if (selectedCellId(session) == null) return
        settle(SELECTION_WAIT_MILLIS)
    }
}

private fun selectedCellId(session: Long): Int? {
    val stats = DesktopNativeRenderer.nativeReadStats(session) ?: return null
    if (stats.size <= STATUS_SELECTED_CELL_ID) return null
    val selected = stats[STATUS_SELECTED_CELL_ID].toInt()
    return selected.takeIf { it != NO_SELECTION }
}

private const val SESSION_WAIT_STEPS = 60
private const val SESSION_WAIT_MILLIS = 500L
private const val SELECTION_WAIT_STEPS = 20
private const val SELECTION_WAIT_MILLIS = 200L

/** Where the runtime publishes this, documented where the array is filled in the bridge. */
private const val STATUS_SELECTED_CELL_ID = 8
private const val NO_SELECTION = -1
