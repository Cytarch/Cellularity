package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.platform.simulation.MicroscopeFrameSnapshot
import com.cytarch.cellularity.platform.simulation.MicroscopeRuntimePublicationEnvelope
import com.cytarch.cellularity.platform.simulation.MicroscopeRuntimePublicationSource
import java.util.concurrent.atomic.AtomicReference

/** Main-thread cadence owner for session-level microscope publications. */
internal class MicroscopeStatusPoller(
    private val source: MicroscopeRuntimePublicationSource,
    private val receiver: MicroscopeInfoReceiver,
    private val host: Host,
    private val onInputIngressSnapshot: (MicroscopeInputIngressSnapshot) -> Unit = {}
) {
    internal interface Host {
        fun post(action: Runnable): Boolean

        fun postDelayed(action: Runnable, delayMillis: Long): Boolean

        fun removeCallbacks(action: Runnable)
    }

    private var generation = 0L
    private var intervalMillis: Long? = null
    @Volatile
    private var released = false
    private var renderPreparationFailed = false
    private var pollInFlight = false
    private var immediatePollRequested = false
    private val undeliveredPublication = AtomicReference<PendingPublication?>(null)
    private val tick = Runnable(::poll)

    fun update(
        attached: Boolean,
        lifecycleResumed: Boolean,
        statusPollIntervalMillis: Long?
    ) {
        val nextInterval = statusPollIntervalMillis?.takeIf {
            attached && lifecycleResumed && it > 0L && !released
        }
        if (nextInterval == intervalMillis) return
        generation = nextGeneration(generation)
        intervalMillis = nextInterval
        host.removeCallbacks(tick)
        scheduleNext()
    }

    fun release() {
        if (released) return
        released = true
        generation = nextGeneration(generation)
        intervalMillis = null
        undeliveredPublication.set(null)
        host.removeCallbacks(tick)
    }

    fun requestImmediatePoll() {
        if (released || intervalMillis == null) return
        deliverUndeliveredPublication()
        if (released || intervalMillis == null) return
        immediatePollRequested = true
        startPoll()
    }

    private fun poll() {
        deliverUndeliveredPublication()
        if (released || intervalMillis == null) return
        startPoll()
        scheduleNext()
    }

    private fun startPoll() {
        if (released || intervalMillis == null || pollInFlight) return
        val activeGeneration = generation
        pollInFlight = true
        immediatePollRequested = false
        if (!source.tryStartPoll(activeGeneration, ::onPublication)) {
            pollInFlight = false
            immediatePollRequested = true
        }
    }

    private fun onPublication(
        publicationGeneration: Long,
        publication: MicroscopeRuntimePublicationEnvelope
    ) {
        val pending = PendingPublication(publicationGeneration, publication)
        if (!host.post { deliverPublication(pending) }) {
            // View.post can reject transport while the source poll itself has
            // already completed. Retain that completion for the next main-host
            // tick/request so pollInFlight cannot become permanently stuck.
            if (!released) {
                undeliveredPublication.set(pending)
                if (released) undeliveredPublication.compareAndSet(pending, null)
            }
        }
    }

    private fun deliverUndeliveredPublication() {
        undeliveredPublication.getAndSet(null)?.let(::deliverPublication)
    }

    private fun deliverPublication(pending: PendingPublication) {
        pollInFlight = false
        if (
            released ||
            intervalMillis == null ||
            pending.generation != generation
        ) {
            if (!released && intervalMillis != null && immediatePollRequested) {
                startPoll()
            }
            return
        }
        pending.publication.inputIngress?.let(onInputIngressSnapshot)
        pending.publication.frame?.let(::applyFrameSnapshot)
        if (immediatePollRequested) {
            startPoll()
        }
    }

    private fun applyFrameSnapshot(snapshot: MicroscopeFrameSnapshot) {
        if (snapshot.diagnosisRequested) {
            receiver.onInspectionChanged(snapshot.inspection)
        }
        if (snapshot.renderPreparationFailed != renderPreparationFailed) {
            renderPreparationFailed = snapshot.renderPreparationFailed
            receiver.onRenderPreparationFailureChanged(renderPreparationFailed)
        }
    }

    private fun scheduleNext() {
        val delay = intervalMillis ?: return
        host.postDelayed(tick, delay)
    }

    private data class PendingPublication(
        val generation: Long,
        val publication: MicroscopeRuntimePublicationEnvelope
    )

    private companion object {
        fun nextGeneration(current: Long): Long =
            if (current == Long.MAX_VALUE) 1L else current + 1L
    }
}
