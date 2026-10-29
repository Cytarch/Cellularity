/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.awt.ComposePanel
import androidx.compose.ui.awt.ComposeWindow
import com.cytarch.cellularity.desktop.files.DesktopCellularityFileRepository
import com.cytarch.cellularity.shared.model.SimulationContract
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/**
 * Measures whether the interface is still being serviced once the simulation runs.
 *
 * "The window hangs and will not close" is a claim about one thread: the event dispatch
 * thread. Everything the interface does -- laying out, drawing, delivering a click, running a
 * close request -- happens there, so if that thread is busy the window appears frozen, the
 * close button does nothing, and every control is dead, while the simulation keeps drawing
 * perfectly because it is not on that thread at all.
 *
 * That is measurable without looking at anything. Work is posted to the event queue and the
 * time until it runs is recorded: an event thread that is answering stays in the low
 * milliseconds, and one that is blocked reports the whole wait and then keeps reporting it.
 * A screenshot cannot tell these apart, because in both cases the last frame drawn stays on
 * screen.
 *
 * The window is realised outside every monitor: peers have to exist for there to be a
 * session at all, and parked off the desktop it is in front of nobody.
 *
 * Run through `:desktop:runEventThreadProbe`. Exits non-zero when the thread stopped
 * answering.
 */
fun main() {
    val failure = AtomicReference<Throwable?>(null)
    val built = CountDownLatch(1)
    val window = AtomicReference<ComposeWindow?>(null)

    javax.swing.SwingUtilities.invokeLater {
        try {
            val frame = ComposeWindow()
            frame.title = "event thread probe"
            val panel = ComposePanel()
            panel.preferredSize = java.awt.Dimension(1100, 700)
            panel.setContent {
                MaterialTheme {
                    DesktopPlayScreen(
                        configuration = SimulationContract.defaultConfiguration,
                        fileRepository = DesktopCellularityFileRepository(
                            Files.createTempDirectory("cellularity-event-probe")
                                .resolve("substrates")
                        ),
                        onExit = { }
                    )
                }
            }
            frame.contentPane.add(panel)
            frame.pack()
            frame.setLocation(-4600, -4600)
            window.set(frame)
            frame.isVisible = true
        } catch (thrown: Throwable) {
            failure.set(thrown)
        } finally {
            built.countDown()
        }
    }

    if (!built.await(60, TimeUnit.SECONDS)) {
        println("EDT the window was never built")
        kotlin.system.exitProcess(2)
    }
    failure.get()?.let { thrown: Throwable ->
        println("EDT could not open the play screen: $thrown")
        kotlin.system.exitProcess(3)
    }

    val worst = AtomicLong(0L)
    val unanswered = AtomicLong(0L)
    val samples = AtomicLong(0L)
    val stop = AtomicLong(0L)

    // Sampled from a thread of its own, so the measurement does not depend on the thread it
    // is measuring. A round trip that never completes is the failure itself and is counted
    // rather than timed: there is no latency that expresses "forever".
    val sampler = Thread {
        while (stop.get() == 0L) {
            val posted = System.nanoTime()
            val done = CountDownLatch(1)
            javax.swing.SwingUtilities.invokeLater { done.countDown() }
            val answered = try {
                done.await(2, TimeUnit.SECONDS)
            } catch (interrupted: InterruptedException) {
                return@Thread
            }
            if (!answered) {
                unanswered.incrementAndGet()
            } else {
                val elapsed = (System.nanoTime() - posted) / 1_000_000
                worst.updateAndGet { current -> maxOf(current, elapsed) }
            }
            samples.incrementAndGet()
            try {
                Thread.sleep(100)
            } catch (interrupted: InterruptedException) {
                return@Thread
            }
        }
    }.apply { isDaemon = true; start() }

    // Long enough to cover the first frames, the first session and a minute of running.
    repeat(6) { step ->
        settle(10_000)
        println(
            "EDT after ${(step + 1) * 10}s: worst=${worst.get()} ms " +
                "unanswered=${unanswered.get()} of ${samples.get()}"
        )
    }

    stop.set(1L)
    sampler.interrupt()
    settle(500)

    val blocked = unanswered.get() > 0L
    println("EDT worstRoundTrip=${worst.get()} ms unanswered=${unanswered.get()}")
    println(
        if (blocked) {
            "EDT the event thread stopped answering, which is why the window cannot be closed"
        } else {
            "EDT the event thread is answering, so a dead window is something else"
        }
    )

    javax.swing.SwingUtilities.invokeLater {
        window.get()?.isVisible = false
        window.get()?.dispose()
    }
    settle(500)
    kotlin.system.exitProcess(if (blocked) 4 else 0)
}

private fun settle(millis: Long) {
    try {
        Thread.sleep(millis)
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
    }
}
