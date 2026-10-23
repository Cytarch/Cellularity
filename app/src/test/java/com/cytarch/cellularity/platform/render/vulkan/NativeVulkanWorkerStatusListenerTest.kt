package com.cytarch.cellularity.platform.render.vulkan

import java.util.ArrayDeque
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertEquals
import org.junit.Test

class NativeVulkanWorkerStatusListenerTest {
    @Test
    fun coalescesSignalsUsingUnsignedRevisionAndAuthoritativeStrongestPhase() {
        val posts = ArrayDeque<Runnable>()
        val status = AtomicReference(
            status(
                revision = Long.MIN_VALUE,
                phase = NativeVulkanWorkerStatus.Phase.TERMINAL_FAILURE
            )
        )
        val received = mutableListOf<Pair<Long, Int>>()
        val listener = NativeVulkanWorkerStatusListener(
            listenerGeneration = 7L,
            receiver = { value, actions -> received += value.revision to actions },
            postToMain = { runnable -> posts.addLast(runnable); true },
            statusReaderOverride = status::get
        )

        assertEquals(0, listener.onNativeWorkerStatusSignal(7L, Long.MAX_VALUE, 1))
        assertEquals(0, listener.onNativeWorkerStatusSignal(7L, Long.MIN_VALUE, 2))
        assertEquals(1, posts.size)

        posts.removeFirst().run()

        assertEquals(listOf(Long.MIN_VALUE to 2), received)
    }

    @Test
    fun staleCompactSnapshotFailsClosedWithoutDeliveringAction() {
        val posts = ArrayDeque<Runnable>()
        val closes = AtomicInteger()
        val received = AtomicInteger()
        val listener = NativeVulkanWorkerStatusListener(
            listenerGeneration = 8L,
            receiver = { _, _ -> received.incrementAndGet() },
            postToMain = { runnable -> posts.addLast(runnable); true },
            statusReaderOverride = {
                status(4L, NativeVulkanWorkerStatus.Phase.RECOVERABLE_FAILURE)
            },
            closeOverride = closes::incrementAndGet
        )

        assertEquals(0, listener.onNativeWorkerStatusSignal(8L, 5L, 4))
        posts.removeFirst().run()

        assertEquals(0, received.get())
        assertEquals(1, closes.get())
    }

    @Test
    fun twoRejectedInitialPostsReturnBoundedFailureThenCloseListener() {
        val closes = AtomicInteger()
        val listener = NativeVulkanWorkerStatusListener(
            listenerGeneration = 9L,
            receiver = { _, _ -> },
            postToMain = { false },
            statusReaderOverride = {
                status(1L, NativeVulkanWorkerStatus.Phase.SURFACE_LOST)
            },
            closeOverride = closes::incrementAndGet
        )

        assertEquals(2, listener.onNativeWorkerStatusSignal(9L, 1L, 1))
        assertEquals(1, listener.onNativeWorkerStatusSignal(9L, 1L, 1))
        assertEquals(1, closes.get())
    }

    @Test
    fun rejectedRepostAfterConcurrentSignalClosesHostOnce() {
        val posts = ArrayDeque<Runnable>()
        val postCalls = AtomicInteger()
        val closes = AtomicInteger()
        val current = AtomicReference(
            status(1L, NativeVulkanWorkerStatus.Phase.RECOVERABLE_FAILURE)
        )
        lateinit var listener: NativeVulkanWorkerStatusListener
        listener = NativeVulkanWorkerStatusListener(
            listenerGeneration = 10L,
            receiver = { _, _ ->
                current.set(
                    status(2L, NativeVulkanWorkerStatus.Phase.RECOVERABLE_FAILURE)
                )
                assertEquals(0, listener.onNativeWorkerStatusSignal(10L, 2L, 4))
            },
            postToMain = { runnable ->
                if (postCalls.incrementAndGet() == 1) {
                    posts.addLast(runnable)
                    true
                } else {
                    false
                }
            },
            statusReaderOverride = current::get,
            closeOverride = closes::incrementAndGet
        )

        assertEquals(0, listener.onNativeWorkerStatusSignal(10L, 1L, 4))
        posts.removeFirst().run()

        assertEquals(2, postCalls.get())
        assertEquals(1, closes.get())
    }

    @Test
    fun invalidOrStaleListenerSignalsNeverReachReceiver() {
        val closes = AtomicInteger()
        val received = AtomicInteger()
        val listener = NativeVulkanWorkerStatusListener(
            listenerGeneration = 11L,
            receiver = { _, _ -> received.incrementAndGet() },
            postToMain = { true },
            statusReaderOverride = {
                status(1L, NativeVulkanWorkerStatus.Phase.READY)
            },
            closeOverride = closes::incrementAndGet
        )

        assertEquals(3, listener.onNativeWorkerStatusSignal(11L, 0L, 1))
        assertEquals(3, listener.onNativeWorkerStatusSignal(11L, 1L, 0))
        assertEquals(1, listener.onNativeWorkerStatusSignal(12L, 1L, 1))
        listener.close()
        assertEquals(1, listener.onNativeWorkerStatusSignal(11L, 1L, 1))

        assertEquals(2, closes.get())
        assertEquals(0, received.get())
    }

    private fun status(
        revision: Long,
        phase: NativeVulkanWorkerStatus.Phase
    ): NativeVulkanWorkerStatus = NativeVulkanWorkerStatus(
        revision = revision,
        phase = phase,
        lastFrameOutcome = null,
        flags = 0,
        failure = NativeVulkanWorkerStatus.Failure.NONE,
        presentedFrames = 0L,
        swapchainRecreations = 0L,
        failedFrameAttempts = 0L,
        desiredSurfaceGeneration = 0L,
        appliedSurfaceGeneration = 0L,
        diagnosticsRevision = 0L,
        desiredWidth = 0,
        desiredHeight = 0
    )
}
