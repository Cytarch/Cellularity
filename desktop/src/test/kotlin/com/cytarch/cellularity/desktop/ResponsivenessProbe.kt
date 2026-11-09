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
import com.cytarch.cellularity.shared.model.SimulationContract
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeState
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTemperature
import com.cytarch.cellularity.shared.ui.play.microscope.MicroscopeTool
import java.awt.Dimension
import java.awt.Frame
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/**
 * Measures whether the interface keeps answering while the plate is being drawn.
 *
 * The failure this exists for was never a wrong picture: opening an experiment left the
 * simulation running and the interface gone, and the window would not close, because one thread
 * was both presenting frames and pumping the events. A screenshot cannot tell that apart from a
 * working screen -- the picture is right either way -- so the measurement is the latency of the
 * event thread itself.
 *
 * The probe composes the microscope tab exactly as the play screen does, so what it measures is
 * the real thing: a real session, real frames and the real composition. It then takes the plate
 * off screen and puts it back, which is what switching tabs does, because that is the
 * interaction a person tries when the interface looks stuck.
 *
 * Run through `:desktop:runResponsivenessProbe`. It exits non-zero when the interface stayed
 * unresponsive or the world never advanced, so it is a check and not only a report.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val frame = AtomicReference<Frame?>(null)
    val visible = mutableStateOf(true)
    val cells = AtomicLong(0L)
    val reportedFailure = AtomicReference<String?>(null)
    val sessionHandle = AtomicLong(0L)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val window = Frame("Cellularity responsiveness probe")
            val composePanel = ComposePanel()
            composePanel.preferredSize = Dimension(1100, 700)
            window.add(composePanel)
            window.pack()
            window.setLocation(40, 40)
            composePanel.setContent {
                val isVisible by visible
                MaterialTheme {
                    MicroscopeTab(
                        state = MicroscopeState(
                            selectedTool = MicroscopeTool.CELL,
                            selectedTemperature = MicroscopeTemperature.OBSERVE
                        ),
                        isVisible = isVisible,
                        assetRoots = DesktopAssetRoots.resolve(),
                        configuration = SimulationContract.defaultConfiguration,
                        substratePath = null,
                        incubateTargetUps = com.cytarch.cellularity.shared.settings.CellularitySettings().incubateTargetUps,
                        // This probe opens no genome editor, so it has no draft to place and says
                        // so. Placement is measured where it exists: in the placement test.
                        placementGenome = null,
                        onPlateActionClick = { },
                        onAction = { },
                        onCellCount = { count -> cells.set(count) },
                        onFailure = { message -> reportedFailure.set(message) },
                        onSessionReady = { handle -> sessionHandle.set(handle) }
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
        System.err.println("RESPONSIVE the event thread never finished building the window")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        System.err.println("RESPONSIVE failed to open: $thrown")
        thrown.printStackTrace()
        kotlin.system.exitProcess(3)
    }

    println("RESPONSIVE renderer available=${com.cytarch.cellularity.desktop.nativebridge.DesktopNativeRenderer.isAvailable}")
    println("RESPONSIVE asset roots present=${DesktopAssetRoots.resolve() != null}")

    val window = frame.get()!!
    val worst = AtomicLong(0L)
    val samples = AtomicLong(0L)
    val overBound = AtomicLong(0L)

    // A sampler that keeps the event queue moving and records how long each round trip took. It
    // runs for the whole probe rather than around one action, so the measurement covers the
    // first composition, the first frames and the visibility switch.
    val sampler = Thread {
        while (!Thread.currentThread().isInterrupted) {
            val posted = System.nanoTime()
            val done = CountDownLatch(1)
            javax.swing.SwingUtilities.invokeLater { done.countDown() }
            try {
                if (!done.await(5, TimeUnit.SECONDS)) {
                    // A round trip that never completed is the failure itself, and there is no
                    // latency to record that would express it, so the whole wait is recorded.
                    worst.updateAndGet { current -> maxOf(current, 5_000L) }
                    overBound.incrementAndGet()
                    samples.incrementAndGet()
                    continue
                }
            } catch (interrupted: InterruptedException) {
                return@Thread
            }
            val elapsed = (System.nanoTime() - posted) / 1_000_000
            worst.updateAndGet { current -> maxOf(current, elapsed) }
            if (elapsed >= RESPONSIVENESS_BOUND_MILLIS) overBound.incrementAndGet()
            samples.incrementAndGet()
            try {
                Thread.sleep(50)
            } catch (interrupted: InterruptedException) {
                return@Thread
            }
        }
    }.apply { isDaemon = true; start() }

    settle(20_000)
    println("RESPONSIVE while the plate is drawn: worst=${worst.get()} ms over ${samples.get()} samples")

    // Switching away and back is the interaction a person tries first when the interface looks
    // stuck, and it is also the state the window design could not survive: hiding the picture
    // used to tear the session down or leave a swapchain that could not be drawn into again.
    val beforeSwitch = worst.get()
    val cellsBeforeSwitch = cells.get()
    javax.swing.SwingUtilities.invokeLater { visible.value = false }
    settle(3_000)
    javax.swing.SwingUtilities.invokeLater { visible.value = true }
    settle(6_000)
    println(
        "RESPONSIVE after hiding and showing the plate: worst=${worst.get()} ms (was $beforeSwitch ms), " +
            "cells ${cellsBeforeSwitch} -> ${cells.get()}"
    )

    val worstAfterInteraction = worst.get()
    sampler.interrupt()

    println("RESPONSIVE session=${if (sessionHandle.get() != 0L) "opened" else "never opened"}")
    println("RESPONSIVE cells=${cells.get()}")
    reportedFailure.get()?.let { message -> println("RESPONSIVE reported failure: $message") }
    println("RESPONSIVE worstRoundTrip=$worstAfterInteraction ms samples=${samples.get()} overBound=${overBound.get()}")

    settle(500)
    javax.swing.SwingUtilities.invokeLater {
        window.isVisible = false
        window.dispose()
    }
    settle(500)
    System.out.flush()

    // The bound is on the worst round trip and is loose enough to allow the one-off cost of the
    // first composition and the first native frames. What it still catches is the failure this
    // probe exists for: a loop holding the event thread does not produce a spike, it produces
    // waits of seconds and then never lets go.
    val healthy = worstAfterInteraction < RESPONSIVENESS_BOUND_MILLIS &&
        cells.get() > 0L &&
        sessionHandle.get() != 0L
    kotlin.system.exitProcess(if (healthy) 0 else 4)
}

/** The bound the worst round trip must stay under, in milliseconds. */
private const val RESPONSIVENESS_BOUND_MILLIS = 1_000L

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}
