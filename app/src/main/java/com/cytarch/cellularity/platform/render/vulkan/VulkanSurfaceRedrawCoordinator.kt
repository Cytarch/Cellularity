package com.cytarch.cellularity.platform.render.vulkan

import com.cytarch.cellularity.platform.render.RenderFrameResultCodec

/**
 * Main-thread state machine that ties an Android Surface redraw transaction to
 * a frame presented for the newest Surface generation and dimensions.
 */
internal class VulkanSurfaceRedrawCoordinator(
    private val timeoutNanos: Long = DEFAULT_TIMEOUT_NANOS
) {
    enum class Completion {
        NONE,
        PRESENTED,
        SURFACE_GONE,
        PAUSED,
        FAILED,
        CLOSED,
        TIMED_OUT,
        COUNTER_EXHAUSTED
    }

    data class Transition(
        val callbacks: List<Runnable> = emptyList(),
        val requestFrame: Boolean = false,
        val keepPolling: Boolean = false,
        val completion: Completion = Completion.NONE
    )

    private data class SurfaceKey(
        val generation: Long,
        val width: Int,
        val height: Int
    )

    private data class Target(
        val key: SurfaceKey,
        val baselineStatusRevision: Long,
        val baselinePresentedFrames: Long,
        val startedAtNanos: Long
    )

    private var target: Target? = null
    private val callbacks = ArrayList<Runnable>()

    var contentCurrent: Boolean = false
        private set

    val hasPendingTarget: Boolean
        get() = target != null

    val pendingCallbackCount: Int
        get() = callbacks.size

    init {
        require(timeoutNanos > 0L) { "Surface redraw timeout must be positive" }
    }

    fun beginTarget(
        surfaceGeneration: Long,
        width: Int,
        height: Int,
        baseline: NativeVulkanWorkerStatus,
        nowNanos: Long
    ): Transition = beginTarget(
        key = surfaceKey(surfaceGeneration, width, height),
        baseline = baseline,
        nowNanos = nowNanos
    )

    fun registerCallback(
        surfaceGeneration: Long,
        width: Int,
        height: Int,
        baseline: NativeVulkanWorkerStatus,
        nowNanos: Long,
        callback: Runnable
    ): Transition {
        val key = surfaceKey(surfaceGeneration, width, height)
        callbacks += callback
        return beginTarget(key, baseline, nowNanos)
    }

    fun observe(
        status: NativeVulkanWorkerStatus,
        nowNanos: Long
    ): Transition {
        val pending = target ?: return Transition()
        if (nowNanos - pending.startedAtNanos >= timeoutNanos) {
            return finish(Completion.TIMED_OUT)
        }
        if (
            status.phase == NativeVulkanWorkerStatus.Phase.TERMINAL_FAILURE ||
            status.phase == NativeVulkanWorkerStatus.Phase.STOP_REQUESTED ||
            status.phase == NativeVulkanWorkerStatus.Phase.STOPPED
        ) {
            return finish(Completion.FAILED)
        }
        if (pending.baselinePresentedFrames == UNSIGNED_LONG_MAX) {
            return finish(Completion.COUNTER_EXHAUSTED)
        }
        if (
            java.lang.Long.compareUnsigned(
                status.revision,
                pending.baselineStatusRevision
            ) <= 0
        ) {
            return Transition(keepPolling = true)
        }
        val key = pending.key
        val matchesCurrentSurface =
            status.phase == NativeVulkanWorkerStatus.Phase.READY &&
                status.desiredSurfaceGeneration == key.generation &&
                status.appliedSurfaceGeneration == key.generation &&
                status.desiredWidth == key.width &&
                status.desiredHeight == key.height
        val hasMatchingPresent =
            status.lastFrameOutcome == RenderFrameResultCodec.OUTCOME_PRESENTED &&
                java.lang.Long.compareUnsigned(
                    status.presentedFrames,
                    pending.baselinePresentedFrames
                ) > 0
        return if (matchesCurrentSurface && hasMatchingPresent) {
            finish(Completion.PRESENTED)
        } else {
            Transition(keepPolling = true)
        }
    }

    fun terminate(completion: Completion): Transition {
        require(
            completion != Completion.NONE && completion != Completion.PRESENTED
        ) { "Surface redraw termination requires a terminal reason" }
        return finish(completion)
    }

    private fun beginTarget(
        key: SurfaceKey,
        baseline: NativeVulkanWorkerStatus,
        nowNanos: Long
    ): Transition {
        val pending = target
        if (pending?.key == key) {
            return Transition(keepPolling = true)
        }
        if (baseline.presentedFrames == UNSIGNED_LONG_MAX) {
            return finish(Completion.COUNTER_EXHAUSTED)
        }
        target = Target(
            key = key,
            baselineStatusRevision = baseline.revision,
            baselinePresentedFrames = baseline.presentedFrames,
            startedAtNanos = pending?.startedAtNanos ?: nowNanos
        )
        contentCurrent = false
        return Transition(requestFrame = true, keepPolling = true)
    }

    private fun finish(completion: Completion): Transition {
        target = null
        contentCurrent = completion == Completion.PRESENTED
        val completedCallbacks = if (callbacks.isEmpty()) {
            emptyList()
        } else {
            callbacks.toList().also { callbacks.clear() }
        }
        return Transition(
            callbacks = completedCallbacks,
            completion = completion
        )
    }

    private fun surfaceKey(
        surfaceGeneration: Long,
        width: Int,
        height: Int
    ): SurfaceKey {
        require(surfaceGeneration > 0L) { "Surface generation must be positive" }
        require(width > 0 && height > 0) {
            "Surface redraw dimensions must be positive"
        }
        return SurfaceKey(surfaceGeneration, width, height)
    }

    companion object {
        const val DEFAULT_TIMEOUT_NANOS = 2_000_000_000L
        private const val UNSIGNED_LONG_MAX = -1L
    }
}
