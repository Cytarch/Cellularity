package com.cytarch.cellularity.platform.render.microscope

import com.cytarch.cellularity.platform.simulation.MicroscopeFrameSnapshot
import com.cytarch.cellularity.platform.simulation.MicroscopeRuntimePublicationEnvelope
import com.cytarch.cellularity.platform.simulation.MicroscopeRuntimePublicationSource
import com.cytarch.cellularity.shared.model.CellDataConstants
import com.cytarch.cellularity.shared.model.CellInspectionSnapshot
import com.cytarch.cellularity.shared.model.CellSnapshot
import com.cytarch.cellularity.shared.model.CellType
import com.cytarch.cellularity.shared.model.GenomeDraft
import com.cytarch.cellularity.shared.model.SimulationStats
import kotlin.concurrent.thread
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MicroscopeStatusPollerTest {
    @Test
    fun cadenceDoesNotOverlapSlowPollAndDeliversOnlyThroughMainHost() {
        val host = RecordingHost()
        val source = ControllablePublicationSource()
        val receiver = RecordingStatusReceiver()
        val poller = MicroscopeStatusPoller(source, receiver, host)

        poller.update(
            attached = true,
            lifecycleResumed = true,
            statusPollIntervalMillis = 50L
        )
        assertEquals(listOf(50L), host.pendingDelays())

        host.runNextDelayed()
        assertEquals(1, source.acceptedPollCount)
        assertEquals(listOf(50L), host.pendingDelays())

        host.runNextDelayed()
        assertEquals(1, source.acceptedPollCount)
        assertEquals(0, source.rejectedPollCount)

        val publicationThread = thread(name = "test-publication-dispatcher") {
            source.complete(snapshot(diagnosisRequested = true, preparationFailed = true))
        }
        publicationThread.join()

        assertTrue(receiver.inspections.isEmpty())
        assertTrue(receiver.preparationFailures.isEmpty())
        host.runPosted()

        assertEquals(listOf<CellInspectionSnapshot?>(null), receiver.inspections)
        assertEquals(listOf(true), receiver.preparationFailures)
        assertEquals(listOf(Thread.currentThread()), receiver.callbackThreads)
    }

    @Test
    fun detachReconfigurationAndReleaseDiscardOldGenerationResults() {
        val host = RecordingHost()
        val source = ControllablePublicationSource()
        val receiver = RecordingStatusReceiver()
        val poller = MicroscopeStatusPoller(source, receiver, host)

        poller.update(true, true, 50L)
        host.runNextDelayed()
        poller.update(false, true, 50L)
        source.complete(snapshot(diagnosisRequested = true, preparationFailed = true))
        host.runPosted()
        assertTrue(receiver.inspections.isEmpty())
        assertTrue(receiver.preparationFailures.isEmpty())
        assertTrue(host.pendingDelays().isEmpty())

        poller.update(true, true, 100L)
        assertEquals(listOf(100L), host.pendingDelays())
        host.runNextDelayed()
        source.complete(snapshot(diagnosisRequested = true, preparationFailed = true))
        host.runPosted()
        assertEquals(listOf<CellInspectionSnapshot?>(null), receiver.inspections)
        assertEquals(listOf(true), receiver.preparationFailures)

        host.runNextDelayed()
        poller.release()
        source.complete(snapshot(diagnosisRequested = true, preparationFailed = false))
        host.runPosted()
        assertEquals(1, receiver.inspections.size)
        assertEquals(listOf(true), receiver.preparationFailures)
        assertTrue(host.pendingDelays().isEmpty())
    }

    @Test
    fun diagnosisAndPreparationFailureKeepTheirTransitionSemantics() {
        val host = RecordingHost()
        val source = ControllablePublicationSource()
        val receiver = RecordingStatusReceiver()
        val poller = MicroscopeStatusPoller(source, receiver, host)
        val selected = inspection()

        poller.update(true, true, 50L)
        host.pollAndDeliver(source, snapshot(false, false, selected))
        assertTrue(receiver.inspections.isEmpty())
        assertTrue(receiver.preparationFailures.isEmpty())

        host.pollAndDeliver(source, snapshot(true, true, selected))
        host.pollAndDeliver(source, snapshot(true, true, null))
        host.pollAndDeliver(source, snapshot(false, false, null))

        assertEquals(listOf(selected, null), receiver.inspections)
        assertEquals(listOf(true, false), receiver.preparationFailures)
    }

    @Test
    fun immediatePollCoalescesAndDeliversIngressWithoutRuntimeFrame() {
        val host = RecordingHost()
        val source = ControllablePublicationSource()
        val receiver = RecordingStatusReceiver()
        val ingress = mutableListOf<MicroscopeInputIngressSnapshot>()
        val poller = MicroscopeStatusPoller(
            source,
            receiver,
            host,
            ingress::add
        )
        poller.update(true, true, 50L)

        poller.requestImmediatePoll()
        poller.requestImmediatePoll()
        assertEquals(1, source.acceptedPollCount)

        source.completeEnvelope(
            MicroscopeRuntimePublicationEnvelope(
                frame = null,
                inputIngress = MicroscopeInputIngressSnapshot(
                    MicroscopeInputIngressState.RESYNCING,
                    7L
                )
            )
        )
        host.runPosted()
        assertEquals(listOf(MicroscopeInputIngressState.RESYNCING), ingress.map { it.state })
        assertEquals(2, source.acceptedPollCount)

        source.completeEnvelope(
            MicroscopeRuntimePublicationEnvelope(
                frame = null,
                inputIngress = MicroscopeInputIngressSnapshot(
                    MicroscopeInputIngressState.ACCEPTING,
                    8L
                )
            )
        )
        host.runPosted()
        assertEquals(listOf(7L, 8L), ingress.map { it.inputEpoch })
    }

    @Test
    fun rejectedMainHostPostRetainsCompletionAndCannotLoseImmediateWakeup() {
        val host = RecordingHost()
        val source = ControllablePublicationSource()
        val ingress = mutableListOf<MicroscopeInputIngressSnapshot>()
        val poller = MicroscopeStatusPoller(
            source,
            RecordingStatusReceiver(),
            host,
            ingress::add
        )
        poller.update(true, true, 50L)
        poller.requestImmediatePoll()

        host.rejectNextPost = true
        source.completeEnvelope(
            MicroscopeRuntimePublicationEnvelope(
                frame = null,
                inputIngress = MicroscopeInputIngressSnapshot(
                    MicroscopeInputIngressState.RESYNCING,
                    9L
                )
            )
        )
        assertTrue(ingress.isEmpty())

        poller.requestImmediatePoll()

        assertEquals(listOf(9L), ingress.map { it.inputEpoch })
        assertEquals(2, source.acceptedPollCount)
    }

    private class RecordingHost : MicroscopeStatusPoller.Host {
        private data class Delayed(val action: Runnable, val delayMillis: Long)

        private val posted = ArrayDeque<Runnable>()
        private val delayed = ArrayDeque<Delayed>()
        var rejectNextPost = false

        override fun post(action: Runnable): Boolean {
            if (rejectNextPost) {
                rejectNextPost = false
                return false
            }
            posted += action
            return true
        }

        override fun postDelayed(action: Runnable, delayMillis: Long): Boolean {
            delayed += Delayed(action, delayMillis)
            return true
        }

        override fun removeCallbacks(action: Runnable) {
            delayed.removeAll { it.action === action }
            posted.removeAll { it === action }
        }

        fun pendingDelays(): List<Long> = delayed.map(Delayed::delayMillis)

        fun runNextDelayed() {
            delayed.removeFirst().action.run()
        }

        fun runPosted() {
            while (posted.isNotEmpty()) posted.removeFirst().run()
        }

        fun pollAndDeliver(
            source: ControllablePublicationSource,
            snapshot: MicroscopeFrameSnapshot
        ) {
            runNextDelayed()
            source.complete(snapshot)
            runPosted()
        }
    }

    private class ControllablePublicationSource : MicroscopeRuntimePublicationSource {
        override val publicationIntervalMillis: Long = 50L
        var acceptedPollCount = 0
            private set
        var rejectedPollCount = 0
            private set
        private var pending: PendingPoll? = null

        override fun tryStartPoll(
            viewGeneration: Long,
            completion: (Long, MicroscopeRuntimePublicationEnvelope) -> Unit
        ): Boolean {
            if (pending != null) {
                rejectedPollCount += 1
                return false
            }
            acceptedPollCount += 1
            pending = PendingPoll(viewGeneration, completion)
            return true
        }

        fun complete(snapshot: MicroscopeFrameSnapshot?) {
            completeEnvelope(MicroscopeRuntimePublicationEnvelope(snapshot))
        }

        fun completeEnvelope(publication: MicroscopeRuntimePublicationEnvelope) {
            val active = checkNotNull(pending)
            pending = null
            active.completion(active.generation, publication)
        }

        private data class PendingPoll(
            val generation: Long,
            val completion: (Long, MicroscopeRuntimePublicationEnvelope) -> Unit
        )
    }

    private class RecordingStatusReceiver : MicroscopeInfoReceiver {
        val inspections = mutableListOf<CellInspectionSnapshot?>()
        val preparationFailures = mutableListOf<Boolean>()
        val callbackThreads = mutableListOf<Thread>()

        override fun onInspectionChanged(inspection: CellInspectionSnapshot?) {
            inspections += inspection
            callbackThreads += Thread.currentThread()
        }

        override fun onZoomViewHeightChanged(viewHeight: Float?) = Unit

        override fun onRenderPreparationFailureChanged(failed: Boolean) {
            preparationFailures += failed
        }
    }

    private companion object {
        fun snapshot(
            diagnosisRequested: Boolean,
            preparationFailed: Boolean,
            inspection: CellInspectionSnapshot? = null
        ) = MicroscopeFrameSnapshot(
            rendererInvalidated = false,
            stats = SimulationStats(1, 2.0, 3.0, 4),
            diagnosisRequested = diagnosisRequested,
            inspection = inspection,
            renderPreparationFailed = preparationFailed
        )

        fun inspection(): CellInspectionSnapshot = CellInspectionSnapshot(
            cell = CellSnapshot(
                id = 7,
                positionX = 1.0,
                positionY = 2.0,
                positionZ = 0.0,
                radius = 0.02,
                age = 3.0,
                mass = 0.1,
                nitrates = 0.5,
                velocityX = 0.003,
                velocityY = 0.004,
                angle = 0.0,
                angularVelocity = 0.0,
                toxin = 0.25f,
                toxication = 0.0f,
                lipids = 0.0f,
                geneIndex = 0,
                originTag = 0,
                selectTag = 0,
                mutationCount = 2,
                telomeresCount = -1,
                mirrored = false,
                cellType = CellType.NEUROCYTE,
                color = listOf(0.2f, 0.4f, 0.8f),
                signalSubstance = List(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) { 0f },
                newSignalSubstance = List(CellDataConstants.SIGNAL_SUBSTANCE_COUNT) { 0f }
            ),
            genome = GenomeDraft.editorDefaults { listOf(0.2f, 0.4f, 0.8f) }
        )
    }
}
