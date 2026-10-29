/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.nativebridge

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import androidx.compose.ui.awt.ComposeWindow
import com.cytarch.cellularity.desktop.DesktopPlayScreen
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.model.CellSnapshotWireCodec
import com.cytarch.cellularity.shared.model.SimulationContract
import java.awt.Dimension
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Proves that a tap on the plate selects a cell and that the cell can be read back.
 *
 * The objective this covers is "input reaches the simulation, not only the camera". Two
 * things have to be true for that and neither was ever checked on the desktop. A tap has to
 * change the simulation's own selection, which is only visible through the status the runtime
 * publishes. And `nativeInspectCell`, which the launcher has been able to call since the
 * bridge was written, has to return a document the managed side can actually decode -- it had
 * never been called even once, so a wrong byte order or a field the native side does not
 * write would have stayed invisible until someone clicked a cell and saw nothing.
 *
 * Both halves are checked here against a live session rather than against a fixture, because
 * the thing that could be wrong is the agreement between two implementations, and a fixture
 * written by one of them cannot disagree with itself.
 *
 * The window is realised outside every monitor. AWT only creates peers for a window that is
 * shown, and without a peer there is no handle to parent the renderer to and no session, so
 * the run has to be a real one; parked off the desktop it is in front of nobody.
 *
 * Run through `:desktop:runSelectionProbe`. Exits non-zero when the tap does not select, when
 * the snapshot does not decode, or when the two disagree about which cell was read.
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
            frame.setLocation(-4200, -4200)
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
        thrown.printStackTrace()
        kotlin.system.exitProcess(3)
    }

    val frame = window.get()!!
    val session = awaitSession()
    if (session == 0L) {
        println("SELECTION no session started; the surface said: ${SurfaceDiagnostics.describe()}")
        kotlin.system.exitProcess(4)
    }
    println("SELECTION session started: ${SurfaceDiagnostics.describe()}")

    val extent = canvasExtent()
    if (extent == null) {
        println("SELECTION the surface did not report its canvas extent")
        kotlin.system.exitProcess(5)
    }
    val (width, height) = extent

    // The middle of the plate is where a person would click. A corner would land on the dish
    // wall, which is a legitimate place to click and a poor place to expect a cell.
    val before = readStats(session)
    println("SELECTION before the tap: selectedCellId=${before?.getOrNull(8)} cells=${before?.getOrNull(3)}")

    val queued = DesktopNativeRenderer.nativeQueueTap(
        session,
        width / 2f,
        height / 2f
    )
    println("SELECTION tap queued: result=$queued")

    val selectedId = awaitSelection(session)
    if (selectedId == null) {
        println("SELECTION the tap did not select a cell; the status never reported one")
        shutdown(frame)
        kotlin.system.exitProcess(6)
    }
    println("SELECTION the tap selected cell $selectedId")

    val document = DesktopNativeRenderer.nativeInspectCell(session, selectedId)
    if (document == null) {
        println("SELECTION the runtime had no document for cell $selectedId")
        shutdown(frame)
        kotlin.system.exitProcess(7)
    }
    println("SELECTION the runtime returned ${document.size} bytes for it")

    val decoded = try {
        CellSnapshotWireCodec.decode(document)
    } catch (thrown: Throwable) {
        println("SELECTION the document did not decode: ${thrown::class.simpleName}: ${thrown.message}")
        shutdown(frame)
        kotlin.system.exitProcess(8)
    }
    if (decoded == null) {
        println("SELECTION the document decoded to nothing")
        shutdown(frame)
        kotlin.system.exitProcess(9)
    }

    // The two halves have to agree about which cell was read, or the panel would describe one
    // cell while the interface had selected another.
    if (decoded.id != selectedId) {
        println("SELECTION the snapshot is for cell ${decoded.id}, not $selectedId")
        shutdown(frame)
        kotlin.system.exitProcess(10)
    }

    println(
        "SELECTION decoded cell ${decoded.id}: type=${decoded.cellType} " +
            "radius=${decoded.radius} mass=${decoded.mass} age=${decoded.age} " +
            "position=(${decoded.positionX}, ${decoded.positionY}) mirrored=${decoded.mirrored}"
    )
    println("SELECTION input reached the simulation and the cell could be read back")

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

/** Waits for the surface to publish a session, up to a point where waiting is pointless. */
private fun awaitSession(): Long {
    repeat(SESSION_WAIT_STEPS) {
        sessionHandleFromDiagnostics()?.let { return it }
        settle(SESSION_WAIT_MILLIS)
    }
    return 0L
}

/**
 * The live session, taken from the surface's own report.
 *
 * Read out of the description rather than reached for through the composition: the probe is
 * outside the interface on purpose, so that what it measures is the renderer and not the
 * interface's opinion of it.
 */
private fun sessionHandleFromDiagnostics(): Long? {
    val match = Regex("session=(\\d+)").find(SurfaceDiagnostics.describe()) ?: return null
    val value = match.groupValues[1].toLongOrNull() ?: return null
    return value.takeIf { it != 0L }
}

private fun canvasExtent(): Pair<Int, Int>? {
    val match = Regex("canvas=(\\d+)x(\\d+)").find(SurfaceDiagnostics.describe()) ?: return null
    val width = match.groupValues[1].toIntOrNull() ?: return null
    val height = match.groupValues[2].toIntOrNull() ?: return null
    if (width <= 0 || height <= 0) return null
    return width to height
}

private fun readStats(session: Long): LongArray? =
    DesktopNativeRenderer.nativeReadStats(session)

/**
 * Waits for the runtime to report a selected cell.
 *
 * The tap is queued and applied on the runtime's own thread, so the selection appears in the
 * status some frames later; polling is the only way to see it, and the timeout is what turns
 * "not yet" into "never".
 */
private fun awaitSelection(session: Long): Int? {
    repeat(SELECTION_WAIT_STEPS) {
        val stats = readStats(session)
        val selected = stats?.getOrNull(STATUS_SELECTED_CELL_ID)?.toInt()
        if (selected != null && selected >= 0) return selected
        settle(SELECTION_WAIT_MILLIS)
    }
    return null
}

private const val SESSION_WAIT_STEPS = 60
private const val SESSION_WAIT_MILLIS = 500L
private const val SELECTION_WAIT_STEPS = 40
private const val SELECTION_WAIT_MILLIS = 250L

/**
 * Where the selected cell appears in the runtime's status.
 *
 * The order is the bridge's, documented where the array is filled: revision, worldEpoch,
 * completedUpdateCount, cellCount, foodAsWholeUnits, simulationTimeMillis, temperatureMode,
 * toolOrdinal, selectedCellId, failedFlag.
 */
private const val STATUS_SELECTED_CELL_ID = 8
