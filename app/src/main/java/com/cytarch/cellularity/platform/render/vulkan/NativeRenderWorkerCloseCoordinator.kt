package com.cytarch.cellularity.platform.render.vulkan

import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.platform.render.AndroidRenderAssetsOwner
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.CoroutineName
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** Performs join and native ownership destruction away from the main looper. */
internal object NativeRenderWorkerCloseCoordinator {
    private const val AWAIT_SLICE_MILLIS = 5_000L
    private const val AWAIT_RETRY_MILLIS = 100L
    private val scope = CoroutineScope(
        SupervisorJob() + Dispatchers.IO + CoroutineName("CellularityVulkanClose")
    )

    fun closeAsync(
        workerHandle: Long,
        assetsOwner: AndroidRenderAssetsOwner,
        listener: NativeVulkanWorkerStatusListener,
        beforeDestroy: (() -> Unit)? = null
    ) {
        scope.launch {
            var joined = false
            var bindingFinalized = false
            var ownerFinalized = false
            var destroyed = false
            try {
                while (true) {
                    when (NativeVulkanRenderWorkerJni.nativeAwaitStopped(
                        workerHandle,
                        AWAIT_SLICE_MILLIS
                    )) {
                        AWAIT_STOPPED -> break
                        AWAIT_TIMED_OUT -> delay(AWAIT_RETRY_MILLIS)
                        else -> error("Native Vulkan worker could not be joined")
                    }
                }
                joined = true
                runCatching {
                    val status = readStatus(workerHandle)
                    VulkanRuntimeDiagnostics.updateCompact(status.toLegacyStatus())
                    if (status.diagnosticsRevision != 0L) {
                        val report = readFailureReport(
                            workerHandle,
                            status.diagnosticsRevision
                        )
                        if (report != null) {
                            VulkanRuntimeDiagnostics.updateFullReport(report)
                        } else if (
                            status.phase ==
                            NativeVulkanWorkerStatus.Phase.TERMINAL_FAILURE
                        ) {
                            VulkanRuntimeDiagnostics.updateFullReport(
                                "schema=1\nstatus=failed\n" +
                                    "failure_code=${status.failure.wireValue}\n" +
                                    "error=Native Vulkan failure diagnostics are unavailable\n"
                            )
                        }
                    }
                }.onFailure { failure ->
                    AppLog.error(
                        "Could not copy final native Vulkan diagnostics",
                        failure
                    )
                }
                check(
                    NativeVulkanRenderWorkerJni.nativeFinalizeWorkerBinding(
                        workerHandle
                    )
                ) { "Native Vulkan worker binding could not be finalized" }
                bindingFinalized = true
                beforeDestroy?.invoke()
                ownerFinalized = true
                NativeVulkanRenderWorkerJni.nativeDestroyStoppedWorker(workerHandle)
                destroyed = true
            } catch (failure: Throwable) {
                AppLog.error("Could not finish native Vulkan worker shutdown", failure)
            } finally {
                if (joined && (!bindingFinalized || !ownerFinalized)) {
                    runCatching {
                        if (!bindingFinalized) {
                            check(
                                NativeVulkanRenderWorkerJni.nativeFinalizeWorkerBinding(
                                    workerHandle
                                )
                            ) { "Native Vulkan worker binding could not be finalized" }
                            bindingFinalized = true
                        }
                        beforeDestroy?.invoke()
                        ownerFinalized = true
                    }.onFailure { failure ->
                        AppLog.error(
                            "Could not finalize the stopped native Vulkan worker binding",
                            failure
                        )
                    }
                }
                if (joined && bindingFinalized && ownerFinalized && !destroyed) {
                    runCatching {
                        NativeVulkanRenderWorkerJni.nativeDestroyStoppedWorker(
                            workerHandle
                        )
                        destroyed = true
                    }.onFailure { failure ->
                        AppLog.error(
                            "Could not destroy the stopped native Vulkan worker",
                            failure
                        )
                    }
                }
                if (destroyed) {
                    listener.close()
                    assetsOwner.close()
                }
            }
        }
    }

    private fun readStatus(workerHandle: Long): NativeVulkanWorkerStatus {
        val buffer = NativeVulkanWorkerStatusLayout.allocateBuffer()
        check(
            NativeVulkanRenderWorkerJni.nativeReadCompactStatus(
                workerHandle,
                buffer
            ) == 0
        ) { "Could not read final native Vulkan worker status" }
        return NativeVulkanWorkerStatusLayout.decode(buffer)
    }

    private fun readFailureReport(
        workerHandle: Long,
        expectedRevision: Long
    ): String? {
        val buffer = NativeVulkanDiagnosticsChunkLayout.allocateBuffer()
        val output = ByteArrayOutputStream()
        var offset = 0L
        var total = -1L
        while (true) {
            buffer.clear()
            val status = NativeVulkanRenderWorkerJni.nativeReadDiagnosticsChunk(
                workerHandle,
                FAILURE_REQUEST_ID,
                expectedRevision,
                offset,
                buffer
            )
            if (status != DIAGNOSTICS_READY) return null
            val chunk = NativeVulkanDiagnosticsChunkLayout.decode(buffer)
            if (chunk.revision != expectedRevision) return null
            if (total == -1L) total = chunk.totalBytes
            if (chunk.totalBytes != total || total < 0L) return null
            if (chunk.payloadBytes == 0) {
                return if (offset == total) {
                    output.toString(StandardCharsets.UTF_8.name())
                } else {
                    null
                }
            }
            val bytes = ByteArray(chunk.payloadBytes)
            val payload = buffer.duplicate()
            payload.position(NativeVulkanDiagnosticsChunkLayout.HEADER_SIZE)
            payload.get(bytes)
            output.write(bytes)
            offset += chunk.payloadBytes
            if (offset == total) {
                return output.toString(StandardCharsets.UTF_8.name())
            }
            if (offset > total) return null
        }
    }

    private const val FAILURE_REQUEST_ID = 0L
    private const val DIAGNOSTICS_READY = 0
    private const val AWAIT_STOPPED = 0
    private const val AWAIT_TIMED_OUT = 1
}
