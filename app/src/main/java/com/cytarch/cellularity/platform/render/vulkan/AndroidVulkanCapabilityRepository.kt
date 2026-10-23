package com.cytarch.cellularity.platform.render.vulkan

import android.view.Surface
import com.cytarch.cellularity.platform.logging.AppLog
import kotlinx.coroutines.CoroutineName
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

internal sealed interface VulkanCapabilityProbeState {
    data object AwaitingSurface : VulkanCapabilityProbeState
    data object Running : VulkanCapabilityProbeState
    data class Complete(val report: VulkanCapabilityReport) : VulkanCapabilityProbeState
}

internal class AndroidVulkanCapabilityRepository {
    private val probeScope = CoroutineScope(
        SupervisorJob() + Dispatchers.Default + CoroutineName("CellularityVulkanCapabilityProbe")
    )
    private val mutableState = MutableStateFlow<VulkanCapabilityProbeState>(
        VulkanCapabilityProbeState.AwaitingSurface
    )

    val state: StateFlow<VulkanCapabilityProbeState> = mutableState.asStateFlow()

    fun requestProbe(surface: Surface): Boolean {
        if (!beginProbe()) return false

        probeScope.launch {
            val raw = try {
                runCatching {
                    NativeVulkanCapabilityBridge.probe(surface)
                }.getOrElse(::unexpectedFailureReport)
            } finally {
                runCatching(surface::release)
            }
            publish(raw)
        }
        return true
    }

    fun reportSurfaceFailure(failure: Throwable): Boolean {
        if (!beginProbe()) return false
        publish(unexpectedFailureReport(failure))
        return true
    }

    fun retry() {
        synchronized(this) {
            if (mutableState.value is VulkanCapabilityProbeState.Complete) {
                mutableState.value = VulkanCapabilityProbeState.AwaitingSurface
            }
        }
    }

    private fun beginProbe(): Boolean {
        synchronized(this) {
            if (mutableState.value != VulkanCapabilityProbeState.AwaitingSurface) {
                return false
            }
            mutableState.value = VulkanCapabilityProbeState.Running
            return true
        }
    }

    private fun publish(raw: String) {
        val report = VulkanCapabilityReportParser.parse(raw)
        VulkanCapabilityDiagnostics.update(report)
        AppLog.debug("Vulkan capability probe completed:\n${report.raw.trimEnd()}")
        mutableState.value = VulkanCapabilityProbeState.Complete(report)
    }

    private fun unexpectedFailureReport(failure: Throwable): String {
        val detail = failure.message
            ?.replace(Regex("[\\r\\n\\t]+"), " ")
            ?.take(512)
            .orEmpty()
        return buildString {
            appendLine("schema=1")
            appendLine("status=failed")
            appendLine("loader_available=false")
            appendLine("requested_api_version=1.0.0")
            appendLine("instance_api_version_raw=0")
            appendLine("instance_api_version=unknown")
            appendLine("instance_version_query_result=VK_RESULT_UNKNOWN")
            appendLine("instance_surface_extension=false")
            appendLine("android_surface_extension=false")
            appendLine("physical_device_count=0")
            append("error=Unexpected Vulkan probe failure: ")
            append(failure::class.java.simpleName)
            if (detail.isNotBlank()) append(": $detail")
            appendLine()
            appendLine("live_vulkan_objects_after_probe=0")
        }
    }
}
