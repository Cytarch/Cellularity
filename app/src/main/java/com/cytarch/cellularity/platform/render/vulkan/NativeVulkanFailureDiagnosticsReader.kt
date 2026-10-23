package com.cytarch.cellularity.platform.render.vulkan

import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineName
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** Copies recoverable-failure reports without doing chunk I/O on the main looper. */
internal class NativeVulkanFailureDiagnosticsReader(
    private val bridge: NativeVulkanRenderWorkerBridge,
    dispatcher: CoroutineDispatcher = Dispatchers.IO
) : AutoCloseable {
    private val scope = CoroutineScope(
        SupervisorJob() + dispatcher + CoroutineName("CellularityVulkanDiagnostics")
    )
    private val desired = AtomicReference<Request?>()
    private val copiedStatusRevision = AtomicLong(0L)
    private val running = AtomicBoolean(false)
    private val closed = AtomicBoolean(false)

    fun capture(status: NativeVulkanWorkerStatus) {
        if (
            closed.get() ||
            status.phase != NativeVulkanWorkerStatus.Phase.RECOVERABLE_FAILURE
        ) {
            return
        }
        val candidate = Request(
            statusRevision = status.revision,
            diagnosticsRevision = status.diagnosticsRevision,
            failure = status.failure,
            diagnosticsUnavailable =
                status.flags and FAILURE_DIAGNOSTICS_UNAVAILABLE != 0
        )
        while (true) {
            val current = desired.get()
            if (
                current != null &&
                java.lang.Long.compareUnsigned(
                    current.statusRevision,
                    candidate.statusRevision
                ) >= 0
            ) {
                return
            }
            if (desired.compareAndSet(current, candidate)) break
        }
        schedule()
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        desired.set(null)
        scope.cancel()
    }

    private fun schedule() {
        if (closed.get() || !running.compareAndSet(false, true)) return
        scope.launch {
            try {
                drain()
            } finally {
                running.set(false)
                val pending = desired.get()
                if (
                    !closed.get() && pending != null &&
                    java.lang.Long.compareUnsigned(
                        pending.statusRevision,
                        copiedStatusRevision.get()
                    ) > 0
                ) {
                    schedule()
                }
            }
        }
    }

    private suspend fun drain() {
        while (!closed.get()) {
            val request = desired.get() ?: return
            val report = if (
                request.diagnosticsUnavailable ||
                request.diagnosticsRevision == 0L
            ) {
                fallbackReport(request)
            } else {
                bridge.readFailureDiagnostics(request.diagnosticsRevision)
                    ?: fallbackReport(request)
            }
            if (closed.get()) return
            val newest = desired.get()
            if (newest !== request) {
                delay(RETRY_DELAY_MILLIS)
                continue
            }
            VulkanRuntimeDiagnostics.updateFullReport(report)
            copiedStatusRevision.set(request.statusRevision)
            if (desired.get() === request) return
            delay(RETRY_DELAY_MILLIS)
        }
    }

    private fun fallbackReport(request: Request): String =
        "schema=1\nstatus=failed\n" +
            "failure_code=${request.failure.wireValue}\n" +
            "error=Native Vulkan failure diagnostics are unavailable\n"

    private data class Request(
        val statusRevision: Long,
        val diagnosticsRevision: Long,
        val failure: NativeVulkanWorkerStatus.Failure,
        val diagnosticsUnavailable: Boolean
    )

    private companion object {
        const val FAILURE_DIAGNOSTICS_UNAVAILABLE = 1 shl 7
        const val RETRY_DELAY_MILLIS = 25L
    }
}
