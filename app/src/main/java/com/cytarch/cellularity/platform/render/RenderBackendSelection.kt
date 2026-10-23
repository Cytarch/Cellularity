package com.cytarch.cellularity.platform.render

import com.cytarch.cellularity.platform.logging.AppLog
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityReport
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityStatus
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendPreference
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

internal enum class RenderBackendFallbackReason(val diagnosticsName: String) {
    CAPABILITY_UNSUPPORTED("capability_unsupported"),
    CAPABILITY_PROBE_FAILED("capability_probe_failed"),
    VULKAN_RUNTIME_FAILED("vulkan_runtime_failed")
}

internal sealed interface RenderBackendDecision {
    val preference: AndroidRenderBackendPreference

    data class AwaitingCapability(
        override val preference: AndroidRenderBackendPreference
    ) : RenderBackendDecision

    data class Selected(
        override val preference: AndroidRenderBackendPreference,
        val backend: RenderBackendKind,
        val fallbackReason: RenderBackendFallbackReason? = null,
        val detail: String = ""
    ) : RenderBackendDecision

    data class VulkanUnavailable(
        override val preference: AndroidRenderBackendPreference,
        val reason: RenderBackendFallbackReason,
        val detail: String
    ) : RenderBackendDecision
}

internal object RenderBackendResolver {
    fun resolve(
        preference: AndroidRenderBackendPreference,
        capability: VulkanCapabilityReport?,
        sessionVulkanFailure: String?
    ): RenderBackendDecision {
        if (preference == AndroidRenderBackendPreference.OPEN_GL_ES) {
            return RenderBackendDecision.Selected(
                preference = preference,
                backend = RenderBackendKind.OPEN_GL_ES,
                detail = "OpenGL ES was selected explicitly"
            )
        }

        if (!sessionVulkanFailure.isNullOrBlank()) {
            return if (preference == AndroidRenderBackendPreference.AUTO) {
                RenderBackendDecision.Selected(
                    preference = preference,
                    backend = RenderBackendKind.OPEN_GL_ES,
                    fallbackReason = RenderBackendFallbackReason.VULKAN_RUNTIME_FAILED,
                    detail = sessionVulkanFailure
                )
            } else {
                RenderBackendDecision.VulkanUnavailable(
                    preference = preference,
                    reason = RenderBackendFallbackReason.VULKAN_RUNTIME_FAILED,
                    detail = sessionVulkanFailure
                )
            }
        }

        if (capability == null) {
            return RenderBackendDecision.AwaitingCapability(preference)
        }

        if (capability.status == VulkanCapabilityStatus.SUPPORTED) {
            return RenderBackendDecision.Selected(
                preference = preference,
                backend = RenderBackendKind.VULKAN,
                detail = "Vulkan capability requirements are satisfied"
            )
        }

        val reason = when (capability.status) {
            VulkanCapabilityStatus.UNSUPPORTED ->
                RenderBackendFallbackReason.CAPABILITY_UNSUPPORTED
            VulkanCapabilityStatus.FAILED ->
                RenderBackendFallbackReason.CAPABILITY_PROBE_FAILED
            VulkanCapabilityStatus.SUPPORTED -> error("Handled above")
        }
        val detail = capability.error.ifBlank {
            when (capability.status) {
                VulkanCapabilityStatus.UNSUPPORTED ->
                    "The device does not satisfy the Vulkan renderer requirements"
                VulkanCapabilityStatus.FAILED ->
                    "The Vulkan capability probe did not complete successfully"
                VulkanCapabilityStatus.SUPPORTED -> error("Handled above")
            }
        }
        return if (preference == AndroidRenderBackendPreference.AUTO) {
            RenderBackendDecision.Selected(
                preference = preference,
                backend = RenderBackendKind.OPEN_GL_ES,
                fallbackReason = reason,
                detail = detail
            )
        } else {
            RenderBackendDecision.VulkanUnavailable(
                preference = preference,
                reason = reason,
                detail = detail
            )
        }
    }
}

/** Remembers the first Vulkan runtime failure so Auto cannot retry-loop in one process. */
internal class RenderBackendSessionState(
    private val onFirstFailure: (String) -> Unit = { detail ->
        AppLog.error("Vulkan renderer failed: $detail")
        RenderBackendDiagnostics.recordRuntimeFailure(detail)
    }
) {
    private val mutableVulkanFailure = MutableStateFlow<String?>(null)

    val vulkanFailure: StateFlow<String?> = mutableVulkanFailure.asStateFlow()

    fun recordVulkanFailure(detail: String) {
        val normalized = detail.toDiagnosticsLine().ifBlank {
            "Vulkan renderer entered a failed state without an error message"
        }
        synchronized(this) {
            if (mutableVulkanFailure.value != null) return
            mutableVulkanFailure.value = normalized
        }
        onFirstFailure(normalized)
    }
}

internal object RenderBackendDiagnostics {
    @Volatile
    private var preference = "unknown"

    @Volatile
    private var decision = "not_selected"

    @Volatile
    private var selectedBackend = "none"

    @Volatile
    private var fallbackReason = "none"

    @Volatile
    private var detail = ""

    @Volatile
    private var runtimeFailure = "none"

    private var lastLoggedDecision = ""

    @Synchronized
    fun recordPreference(value: AndroidRenderBackendPreference) {
        preference = value.diagnosticsName()
    }

    @Synchronized
    fun recordDecision(value: RenderBackendDecision) {
        preference = value.preference.diagnosticsName()
        when (value) {
            is RenderBackendDecision.AwaitingCapability -> {
                decision = "awaiting_capability"
                selectedBackend = "none"
                fallbackReason = "none"
                detail = "Waiting for the Vulkan capability probe"
            }
            is RenderBackendDecision.Selected -> {
                decision = "selected"
                selectedBackend = value.backend.diagnosticsName()
                fallbackReason = value.fallbackReason?.diagnosticsName ?: "none"
                detail = value.detail.toDiagnosticsLine()
            }
            is RenderBackendDecision.VulkanUnavailable -> {
                decision = "vulkan_unavailable"
                selectedBackend = "none"
                fallbackReason = value.reason.diagnosticsName
                detail = value.detail.toDiagnosticsLine()
            }
        }
        val logMessage =
            "Render backend decision: preference=$preference decision=$decision " +
                "selected=$selectedBackend fallback=$fallbackReason detail=$detail"
        if (logMessage != lastLoggedDecision) {
            lastLoggedDecision = logMessage
            if (value is RenderBackendDecision.VulkanUnavailable) {
                AppLog.error(logMessage)
            } else {
                AppLog.debug(logMessage)
            }
        }
    }

    @Synchronized
    fun recordRuntimeFailure(value: String) {
        runtimeFailure = value.toDiagnosticsLine()
    }

    @Synchronized
    fun snapshot(): String = buildString {
        appendLine("Android render backend")
        appendLine("preference=$preference")
        appendLine("decision=$decision")
        appendLine("selected_backend=$selectedBackend")
        appendLine("fallback_reason=$fallbackReason")
        appendLine("detail=$detail")
        appendLine("session_vulkan_failure=$runtimeFailure")
    }
}

private fun AndroidRenderBackendPreference.diagnosticsName(): String = when (this) {
    AndroidRenderBackendPreference.OPEN_GL_ES -> "open_gl_es"
    AndroidRenderBackendPreference.AUTO -> "auto"
    AndroidRenderBackendPreference.VULKAN -> "vulkan"
}

private fun RenderBackendKind.diagnosticsName(): String = when (this) {
    RenderBackendKind.OPEN_GL_ES -> "open_gl_es"
    RenderBackendKind.VULKAN -> "vulkan"
}

private fun String.toDiagnosticsLine(): String =
    replace(Regex("[\\r\\n\\t]+"), " ").trim().take(1_024)
