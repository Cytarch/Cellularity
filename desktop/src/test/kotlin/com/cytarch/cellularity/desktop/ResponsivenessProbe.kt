/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import com.cytarch.cellularity.desktop.nativebridge.DesktopAssetRoots
import com.cytarch.cellularity.shared.model.SimulationContract
import java.awt.Dimension
import java.awt.Frame
import java.awt.event.KeyEvent
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/**
 * Measures how long the interface takes to answer while the simulation is running.
 *
 * The failure this exists for was not a wrong picture: opening an experiment left the
 * simulation drawing and the interface gone, and the window would not close, because the
 * event thread was busy presenting frames and sleeping between them. A screenshot cannot
 * tell that apart from a working screen -- the picture is right either way -- so the
 * measurement is the latency of the event thread itself.
 *
 * A no-op is posted to the event queue every so often and the time until it runs is
 * recorded. An interface that is answering stays in the low milliseconds; one that is being
 * starved climbs by however long the renderer holds the thread, which in the failing case
 * was unbounded.
 *
 * The probe also switches tabs, because that is the interaction a person would try, and
 * reports the cell count, which is the evidence that the world is still advancing rather
 * than that a still frame was left on screen.
 *
 * Run through `:desktop:runResponsivenessProbe`. It exits non-zero if the interface stayed
 * unresponsive, so it is a check and not only a report.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val frame = AtomicReference<Frame?>(null)
    val panel = AtomicReference<ComposePanel?>(null)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val window = Frame("Cellularity responsiveness probe")
            val composePanel = ComposePanel()
            composePanel.preferredSize = Dimension(1100, 700)
            window.add(composePanel)
            window.pack()
            window.setLocation(40, 40)
            composePanel.setContent {
                MaterialTheme {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        onExit = { }
                    )
                }
            }
            panel.set(composePanel)
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

    // A sampler that keeps the event queue moving and records how long each round trip
    // took. It runs for the whole probe rather than around one action, so the measurement
    // covers opening the experiment, the first frames, and the tab switches.
    val sampler = Thread {
        while (!Thread.currentThread().isInterrupted) {
            val posted = System.nanoTime()
            val done = CountDownLatch(1)
            javax.swing.SwingUtilities.invokeLater { done.countDown() }
            try {
                if (!done.await(5, TimeUnit.SECONDS)) {
                    // A round trip that never completed is the failure itself, and there is
                    // no latency to record that would express it, so it is recorded as the
                    // whole wait.
                    worst.updateAndGet { current -> maxOf(current, 5_000L) }
                    samples.incrementAndGet()
                    continue
                }
            } catch (interrupted: InterruptedException) {
                return@Thread
            }
            val elapsed = (System.nanoTime() - posted) / 1_000_000
            worst.updateAndGet { current -> maxOf(current, elapsed) }
            samples.incrementAndGet()
            try {
                Thread.sleep(50)
            } catch (interrupted: InterruptedException) {
                return@Thread
            }
        }
    }.apply { isDaemon = true; start() }

    // Opening the experiment is what used to take the interface away. The keyboard route is
    // used because it is the one the probe can drive without a display: the press and its
    // release go to the same component the window would deliver them to.
    settle(1_500)
    pressEnter(window)
    settle(20_000)

    println("RESPONSIVE after opening the experiment: worst=${worst.get()} ms over ${samples.get()} samples")

    // A tab switch is what a person would try first when the interface looks stuck.
    val beforeSwitch = worst.get()
    pressTab(window)
    settle(400)
    pressTab(window)
    settle(4_000)
    println("RESPONSIVE after switching tabs: worst=${worst.get()} ms (was $beforeSwitch ms)")

    val worstAfterInteraction = worst.get()
    sampler.interrupt()

    // Whether the world is advancing, read from the surface rather than from the interface,
    // so that a still frame left on screen cannot be mistaken for a running simulation.
    println("RESPONSIVE surface: ${com.cytarch.cellularity.desktop.nativebridge.SurfaceDiagnostics.describe()}")
    val cells = reportCells()
    println("RESPONSIVE cells reported by the surface=$cells")

    settle(1_000)
    javax.swing.SwingUtilities.invokeLater {
        window.isVisible = false
        window.dispose()
    }
    settle(500)

    println("RESPONSIVE worstRoundTrip=${worstAfterInteraction} ms samples=${samples.get()}")
    System.out.flush()

    // The bound is on the worst round trip and is deliberately loose enough to allow the
    // one-off cost of the first composition, the first native frame and the JIT. What it
    // still catches is the failure this probe exists for: a loop holding the event thread
    // does not produce a spike, it produces waits of seconds and then never lets go.
    val healthy = worstAfterInteraction < 1_000 && cells > 0L
    kotlin.system.exitProcess(if (healthy) 0 else 4)
}

/**
 * The cell count the surface last presented.
 *
 * Asked of the surface rather than of a session handle, because the handle is the bridge's
 * business: it is not a small integer and guessing at one answers zero whether the world is
 * empty or the guess was wrong.
 */
private fun reportCells(): Long {
    val described = com.cytarch.cellularity.desktop.nativebridge.SurfaceDiagnostics.describe()
    val match = Regex("cells=(\\d+)").find(described) ?: return 0L
    return match.groupValues[1].toLongOrNull() ?: 0L
}

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}

/** Delivers a keystroke to the window's focus owner, the way the platform would. */
private fun pressKey(window: Frame, keyCode: Int) {
    javax.swing.SwingUtilities.invokeLater {
        val target = java.awt.KeyboardFocusManager.getCurrentKeyboardFocusManager().focusOwner
            ?: window
        val pressed = KeyEvent(
            target,
            KeyEvent.KEY_PRESSED,
            System.currentTimeMillis(),
            0,
            keyCode,
            KeyEvent.CHAR_UNDEFINED
        )
        val released = KeyEvent(
            target,
            KeyEvent.KEY_RELEASED,
            System.currentTimeMillis(),
            0,
            keyCode,
            KeyEvent.CHAR_UNDEFINED
        )
        target.dispatchEvent(pressed)
        target.dispatchEvent(released)
    }
}

private fun pressEnter(window: Frame) = pressKey(window, KeyEvent.VK_ENTER)

private fun pressTab(window: Frame) = pressKey(window, KeyEvent.VK_TAB)