package com.cytarch.cellularity.platform.render.vulkan

import android.os.Handler
import android.os.Looper
import androidx.annotation.Keep
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

/** Rare native-to-managed wakeup; ordinary frame completion never crosses JNI. */
@Keep
class NativeVulkanWorkerStatusListener internal constructor(
    private val listenerGeneration: Long,
    private val receiver: (NativeVulkanWorkerStatus, Int) -> Unit,
    private val postToMain: (Runnable) -> Boolean =
        Handler(Looper.getMainLooper())::post,
    private val statusReaderOverride: (() -> NativeVulkanWorkerStatus?)? = null,
    private val closeOverride: (() -> Unit)? = null
) {
    private val pendingRevision = AtomicLong(0L)
    private val pendingActions = AtomicInteger(0)
    private val postScheduled = AtomicBoolean(false)
    private val consecutivePostFailures = AtomicInteger(0)
    private val closed = AtomicBoolean(false)
    private val bridge = java.util.concurrent.atomic.AtomicReference<NativeVulkanRenderWorkerBridge?>()
    private val drain = Runnable { drainSignals() }

    internal fun bind(value: NativeVulkanRenderWorkerBridge) {
        check(bridge.compareAndSet(null, value)) {
            "Vulkan worker status listener is already bound"
        }
    }

    internal fun close() {
        closed.set(true)
        pendingActions.set(0)
        pendingRevision.set(0L)
    }

    @Keep
    fun onNativeWorkerStatusSignal(
        generation: Long,
        statusRevision: Long,
        actionBits: Int
    ): Int = try {
        if (generation != listenerGeneration || closed.get()) {
            DELIVERY_LISTENER_CLOSED
        } else if (statusRevision == 0L || actionBits and KNOWN_ACTIONS == 0) {
            closeBridge()
            DELIVERY_INVALID_SIGNAL
        } else {
            pendingActions.getAndUpdate { it or (actionBits and KNOWN_ACTIONS) }
            updateUnsignedMaximum(pendingRevision, statusRevision)
            if (!postScheduled.compareAndSet(false, true)) {
                consecutivePostFailures.set(0)
                DELIVERY_ACCEPTED
            } else if (postToMain(drain)) {
                consecutivePostFailures.set(0)
                DELIVERY_ACCEPTED
            } else {
                postScheduled.set(false)
                if (consecutivePostFailures.incrementAndGet() >= 2) {
                    closeBridge()
                    DELIVERY_LISTENER_CLOSED
                } else {
                    DELIVERY_RETRYABLE_FAILURE
                }
            }
        }
    } catch (_: Throwable) {
        closeBridge()
        DELIVERY_LISTENER_CLOSED
    }

    private fun drainSignals() {
        try {
            val actions = pendingActions.getAndSet(0)
            val revision = pendingRevision.getAndSet(0L)
            if (!closed.get() && actions != 0 && revision != 0L) {
                val status = runCatching {
                    statusReaderOverride?.invoke() ?: bridge.get()?.readStatus()
                }.getOrNull()
                if (status == null || java.lang.Long.compareUnsigned(
                        status.revision,
                        revision
                    ) < 0
                ) {
                    closeBridge()
                } else {
                    when (status.phase) {
                        NativeVulkanWorkerStatus.Phase.TERMINAL_FAILURE -> {
                            receiver(status, ACTION_TERMINAL_FAILURE)
                            closeBridge()
                        }
                        NativeVulkanWorkerStatus.Phase.RECOVERABLE_FAILURE ->
                            receiver(status, ACTION_RECOVERABLE_FAILURE)
                        NativeVulkanWorkerStatus.Phase.SURFACE_LOST ->
                            receiver(status, ACTION_SURFACE_REATTACH_REQUIRED)
                        NativeVulkanWorkerStatus.Phase.PAUSED,
                        NativeVulkanWorkerStatus.Phase.WAITING_FOR_SURFACE,
                        NativeVulkanWorkerStatus.Phase.READY,
                        NativeVulkanWorkerStatus.Phase.STOP_REQUESTED,
                        NativeVulkanWorkerStatus.Phase.STOPPED -> Unit
                        NativeVulkanWorkerStatus.Phase.DORMANT,
                        NativeVulkanWorkerStatus.Phase.STARTING -> closeBridge()
                    }
                }
            }
        } catch (_: Throwable) {
            closeBridge()
        } finally {
            postScheduled.set(false)
            if (
                !closed.get() &&
                (pendingActions.get() != 0 || pendingRevision.get() != 0L) &&
                postScheduled.compareAndSet(false, true)
            ) {
                if (!postToMain(drain)) {
                    postScheduled.set(false)
                    closeBridge()
                }
            }
        }
    }

    private fun closeBridge() {
        if (closeOverride != null) {
            closeOverride.invoke()
        } else {
            bridge.get()?.closeAsync()
        }
    }

    private companion object {
        const val ACTION_SURFACE_REATTACH_REQUIRED = 1
        const val ACTION_TERMINAL_FAILURE = 1 shl 1
        const val ACTION_RECOVERABLE_FAILURE = 1 shl 2
        const val KNOWN_ACTIONS = ACTION_SURFACE_REATTACH_REQUIRED or
            ACTION_TERMINAL_FAILURE or ACTION_RECOVERABLE_FAILURE

        const val DELIVERY_ACCEPTED = 0
        const val DELIVERY_LISTENER_CLOSED = 1
        const val DELIVERY_RETRYABLE_FAILURE = 2
        const val DELIVERY_INVALID_SIGNAL = 3

        fun updateUnsignedMaximum(target: AtomicLong, candidate: Long) {
            while (true) {
                val current = target.get()
                if (current != 0L && java.lang.Long.compareUnsigned(current, candidate) >= 0) {
                    return
                }
                if (target.compareAndSet(current, candidate)) return
            }
        }
    }
}
