package com.cytarch.cellularity.platform.render.vulkan

import com.cytarch.cellularity.platform.render.RenderFrameResultCodec
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class VulkanSurfaceRedrawCoordinatorTest {
    @Test
    fun readyWithoutNewPresentedFrameKeepsRedrawPending() {
        val coordinator = VulkanSurfaceRedrawCoordinator()
        val started = coordinator.beginTarget(3L, 640, 480, status(1L, 7L), 0L)

        val noOutcome = coordinator.observe(
            status(2L, 7L, generation = 3L, width = 640, height = 480),
            1L
        )
        val oldCounter = coordinator.observe(
            status(
                3L,
                7L,
                generation = 3L,
                width = 640,
                height = 480,
                presented = true
            ),
            2L
        )

        assertTrue(started.requestFrame)
        assertTrue(started.keepPolling)
        assertTrue(noOutcome.keepPolling)
        assertTrue(oldCounter.keepPolling)
        assertFalse(coordinator.contentCurrent)
    }

    @Test
    fun matchingGenerationDimensionsAndCounterCompleteEveryCallbackOnce() {
        val coordinator = VulkanSurfaceRedrawCoordinator()
        val callbackCalls = AtomicInteger()
        val started = coordinator.registerCallback(
            4L,
            800,
            600,
            status(10L, 20L),
            0L,
            Runnable(callbackCalls::incrementAndGet)
        )

        val completed = coordinator.observe(
            status(
                11L,
                21L,
                generation = 4L,
                width = 800,
                height = 600,
                presented = true
            ),
            1L
        )
        runCallbacks(completed)
        runCallbacks(coordinator.observe(status(11L, 21L), 2L))

        assertTrue(started.requestFrame)
        assertEquals(VulkanSurfaceRedrawCoordinator.Completion.PRESENTED, completed.completion)
        assertEquals(1, callbackCalls.get())
        assertTrue(coordinator.contentCurrent)
        assertFalse(coordinator.hasPendingTarget)
        assertFalse(completed.keepPolling)
    }

    @Test
    fun staleGenerationAndWrongDimensionsCannotCompleteRedraw() {
        val coordinator = VulkanSurfaceRedrawCoordinator()
        coordinator.beginTarget(5L, 1000, 700, status(1L, 0L), 0L)

        val staleGeneration = coordinator.observe(
            status(2L, 1L, 4L, 1000, 700, presented = true),
            1L
        )
        val wrongDimensions = coordinator.observe(
            status(3L, 2L, 5L, 999, 700, presented = true),
            2L
        )

        assertTrue(staleGeneration.keepPolling)
        assertTrue(wrongDimensions.keepPolling)
        assertTrue(coordinator.hasPendingTarget)
        assertFalse(coordinator.contentCurrent)
    }

    @Test
    fun newerResizeSupersedesTargetWithoutLosingOlderCallbacks() {
        val coordinator = VulkanSurfaceRedrawCoordinator()
        val callbackCalls = AtomicInteger()
        coordinator.registerCallback(
            6L,
            400,
            300,
            status(1L, 0L),
            0L,
            Runnable(callbackCalls::incrementAndGet)
        )

        val superseded = coordinator.beginTarget(
            6L,
            900,
            500,
            status(3L, 1L),
            100L
        )
        val stale = coordinator.observe(
            status(4L, 2L, 6L, 400, 300, presented = true),
            101L
        )
        val current = coordinator.observe(
            status(5L, 2L, 6L, 900, 500, presented = true),
            102L
        )
        runCallbacks(current)

        assertTrue(superseded.requestFrame)
        assertTrue(stale.keepPolling)
        assertEquals(1, callbackCalls.get())
        assertEquals(VulkanSurfaceRedrawCoordinator.Completion.PRESENTED, current.completion)
    }

    @Test
    fun duplicateAndOutOfOrderStatusReadsCannotRevealContent() {
        val coordinator = VulkanSurfaceRedrawCoordinator()
        coordinator.beginTarget(7L, 300, 200, status(10L, 5L), 0L)

        val older = coordinator.observe(
            status(9L, 6L, 7L, 300, 200, presented = true),
            1L
        )
        val duplicate = coordinator.observe(
            status(10L, 6L, 7L, 300, 200, presented = true),
            2L
        )
        val newer = coordinator.observe(
            status(11L, 6L, 7L, 300, 200, presented = true),
            3L
        )

        assertTrue(older.keepPolling)
        assertTrue(duplicate.keepPolling)
        assertEquals(VulkanSurfaceRedrawCoordinator.Completion.PRESENTED, newer.completion)
    }

    @Test
    fun timeoutRemainsBoundedAcrossResizeSupersession() {
        val coordinator = VulkanSurfaceRedrawCoordinator(timeoutNanos = 100L)
        val callbackCalls = AtomicInteger()
        coordinator.registerCallback(
            8L,
            100,
            100,
            status(1L, 0L),
            10L,
            Runnable(callbackCalls::incrementAndGet)
        )
        coordinator.beginTarget(8L, 200, 100, status(2L, 0L), 90L)

        val timedOut = coordinator.observe(status(3L, 0L), 110L)
        runCallbacks(timedOut)

        assertEquals(VulkanSurfaceRedrawCoordinator.Completion.TIMED_OUT, timedOut.completion)
        assertEquals(1, callbackCalls.get())
        assertFalse(timedOut.keepPolling)
        assertFalse(coordinator.hasPendingTarget)
        assertFalse(coordinator.contentCurrent)
    }

    @Test
    fun lifecycleTerminalPathsDrainCallbacksExactlyOnce() {
        val reasons = listOf(
            VulkanSurfaceRedrawCoordinator.Completion.SURFACE_GONE,
            VulkanSurfaceRedrawCoordinator.Completion.PAUSED,
            VulkanSurfaceRedrawCoordinator.Completion.FAILED,
            VulkanSurfaceRedrawCoordinator.Completion.CLOSED
        )

        reasons.forEachIndexed { index, reason ->
            val coordinator = VulkanSurfaceRedrawCoordinator()
            val callbackCalls = AtomicInteger()
            coordinator.registerCallback(
                index + 1L,
                320,
                240,
                status(1L, 0L),
                0L,
                Runnable(callbackCalls::incrementAndGet)
            )
            runCallbacks(coordinator.terminate(reason))
            runCallbacks(coordinator.terminate(reason))

            assertEquals(1, callbackCalls.get())
            assertFalse(coordinator.hasPendingTarget)
            assertFalse(coordinator.contentCurrent)
        }
    }

    @Test
    fun terminalWorkerStatusFailsPendingRedraw() {
        val coordinator = VulkanSurfaceRedrawCoordinator()
        val callbackCalls = AtomicInteger()
        coordinator.registerCallback(
            9L,
            320,
            240,
            status(1L, 0L),
            0L,
            Runnable(callbackCalls::incrementAndGet)
        )

        val failed = coordinator.observe(
            status(
                revision = 2L,
                presentedFrames = 0L,
                phase = NativeVulkanWorkerStatus.Phase.TERMINAL_FAILURE
            ),
            1L
        )
        runCallbacks(failed)

        assertEquals(VulkanSurfaceRedrawCoordinator.Completion.FAILED, failed.completion)
        assertEquals(1, callbackCalls.get())
        assertFalse(failed.keepPolling)
    }

    @Test
    fun newRedrawCallbackRequiresAnotherPresentedFrameForCurrentSurface() {
        val coordinator = VulkanSurfaceRedrawCoordinator()
        coordinator.beginTarget(10L, 500, 500, status(1L, 0L), 0L)
        coordinator.observe(
            status(2L, 1L, 10L, 500, 500, presented = true),
            1L
        )
        val callbackCalls = AtomicInteger()

        val started = coordinator.registerCallback(
            10L,
            500,
            500,
            status(2L, 1L),
            2L,
            Runnable(callbackCalls::incrementAndGet)
        )
        val joined = coordinator.registerCallback(
            10L,
            500,
            500,
            status(2L, 1L),
            2L,
            Runnable(callbackCalls::incrementAndGet)
        )
        val completed = coordinator.observe(
            status(3L, 2L, 10L, 500, 500, presented = true),
            3L
        )
        runCallbacks(completed)

        assertTrue(started.requestFrame)
        assertTrue(started.keepPolling)
        assertFalse(joined.requestFrame)
        assertTrue(joined.keepPolling)
        assertEquals(
            VulkanSurfaceRedrawCoordinator.Completion.PRESENTED,
            completed.completion
        )
        assertEquals(2, callbackCalls.get())
        assertTrue(coordinator.contentCurrent)
    }

    @Test
    fun exhaustedUnsignedPresentedCounterFailsClosed() {
        val coordinator = VulkanSurfaceRedrawCoordinator()
        val callbackCalls = AtomicInteger()

        val exhausted = coordinator.registerCallback(
            11L,
            500,
            300,
            status(1L, -1L),
            0L,
            Runnable(callbackCalls::incrementAndGet)
        )
        runCallbacks(exhausted)

        assertEquals(
            VulkanSurfaceRedrawCoordinator.Completion.COUNTER_EXHAUSTED,
            exhausted.completion
        )
        assertEquals(1, callbackCalls.get())
        assertFalse(exhausted.requestFrame)
        assertFalse(exhausted.keepPolling)
        assertFalse(coordinator.contentCurrent)
    }

    @Test
    fun redrawSafetyFailuresAreVisibleInRuntimeDiagnostics() {
        VulkanRuntimeDiagnostics.resetForTesting()
        VulkanRuntimeDiagnostics.recordSurfaceRedrawTimeout()
        VulkanRuntimeDiagnostics.recordSurfaceRedrawCounterExhausted()

        val report = VulkanRuntimeDiagnostics.snapshot()

        assertTrue(report.contains("surface_redraw_timeout_count=1"))
        assertTrue(report.contains("surface_redraw_counter_exhausted_count=1"))
        VulkanRuntimeDiagnostics.resetForTesting()
    }

    private fun status(
        revision: Long,
        presentedFrames: Long,
        generation: Long = 0L,
        width: Int = 0,
        height: Int = 0,
        presented: Boolean = false,
        phase: NativeVulkanWorkerStatus.Phase = NativeVulkanWorkerStatus.Phase.READY
    ): NativeVulkanWorkerStatus = NativeVulkanWorkerStatus(
        revision = revision,
        phase = phase,
        lastFrameOutcome = if (presented) {
            RenderFrameResultCodec.OUTCOME_PRESENTED
        } else {
            null
        },
        flags = 0,
        failure = NativeVulkanWorkerStatus.Failure.NONE,
        presentedFrames = presentedFrames,
        swapchainRecreations = 0L,
        failedFrameAttempts = 0L,
        desiredSurfaceGeneration = generation,
        appliedSurfaceGeneration = generation,
        diagnosticsRevision = 0L,
        desiredWidth = width,
        desiredHeight = height
    )

    private fun runCallbacks(
        transition: VulkanSurfaceRedrawCoordinator.Transition
    ) {
        transition.callbacks.forEach(Runnable::run)
    }
}
