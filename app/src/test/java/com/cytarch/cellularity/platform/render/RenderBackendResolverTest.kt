package com.cytarch.cellularity.platform.render

import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityReport
import com.cytarch.cellularity.platform.render.vulkan.VulkanCapabilityReportParser
import com.cytarch.cellularity.shared.settings.AndroidRenderBackendPreference
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RenderBackendResolverTest {
    @Test
    fun explicitGlesNeedsNeitherCapabilityNorVulkanRuntimeRecovery() {
        val decision = RenderBackendResolver.resolve(
            preference = AndroidRenderBackendPreference.OPEN_GL_ES,
            capability = null,
            sessionVulkanFailure = "previous Vulkan failure"
        ) as RenderBackendDecision.Selected

        assertEquals(RenderBackendKind.OPEN_GL_ES, decision.backend)
        assertNull(decision.fallbackReason)
    }

    @Test
    fun autoSelectsVulkanOnlyAfterSupportedCapabilityReport() {
        assertTrue(
            RenderBackendResolver.resolve(
                AndroidRenderBackendPreference.AUTO,
                capability = null,
                sessionVulkanFailure = null
            ) is RenderBackendDecision.AwaitingCapability
        )

        val decision = RenderBackendResolver.resolve(
            AndroidRenderBackendPreference.AUTO,
            capability = capability("supported", ""),
            sessionVulkanFailure = null
        ) as RenderBackendDecision.Selected

        assertEquals(RenderBackendKind.VULKAN, decision.backend)
        assertNull(decision.fallbackReason)
    }

    @Test
    fun autoFallsBackBeforeSurfaceCreationWhenCapabilityIsUnsupported() {
        val decision = RenderBackendResolver.resolve(
            AndroidRenderBackendPreference.AUTO,
            capability = capability("unsupported", "No compatible Vulkan device"),
            sessionVulkanFailure = null
        ) as RenderBackendDecision.Selected

        assertEquals(RenderBackendKind.OPEN_GL_ES, decision.backend)
        assertEquals(
            RenderBackendFallbackReason.CAPABILITY_UNSUPPORTED,
            decision.fallbackReason
        )
        assertEquals("No compatible Vulkan device", decision.detail)
    }

    @Test
    fun explicitVulkanExposesCapabilityFailureInsteadOfHidingIt() {
        val decision = RenderBackendResolver.resolve(
            AndroidRenderBackendPreference.VULKAN,
            capability = capability("failed", "VK_ERROR_INCOMPATIBLE_DRIVER"),
            sessionVulkanFailure = null
        ) as RenderBackendDecision.VulkanUnavailable

        assertEquals(
            RenderBackendFallbackReason.CAPABILITY_PROBE_FAILED,
            decision.reason
        )
        assertEquals("VK_ERROR_INCOMPATIBLE_DRIVER", decision.detail)
    }

    @Test
    fun knownRuntimeFailureCannotRetryVulkanAutomaticallyInSameSession() {
        val auto = RenderBackendResolver.resolve(
            AndroidRenderBackendPreference.AUTO,
            capability = capability("supported", ""),
            sessionVulkanFailure = "vkCreateSwapchainKHR failed"
        ) as RenderBackendDecision.Selected
        val explicit = RenderBackendResolver.resolve(
            AndroidRenderBackendPreference.VULKAN,
            capability = capability("supported", ""),
            sessionVulkanFailure = "vkCreateSwapchainKHR failed"
        ) as RenderBackendDecision.VulkanUnavailable

        assertEquals(RenderBackendKind.OPEN_GL_ES, auto.backend)
        assertEquals(RenderBackendFallbackReason.VULKAN_RUNTIME_FAILED, auto.fallbackReason)
        assertEquals(RenderBackendFallbackReason.VULKAN_RUNTIME_FAILED, explicit.reason)
    }

    @Test
    fun sessionRetainsOnlyTheFirstRuntimeFailure() {
        val recorded = mutableListOf<String>()
        val session = RenderBackendSessionState(recorded::add)

        session.recordVulkanFailure("first failure\nwith detail")
        session.recordVulkanFailure("later failure")

        assertEquals("first failure with detail", session.vulkanFailure.value)
        assertEquals(listOf("first failure with detail"), recorded)
    }

    private fun capability(status: String, error: String): VulkanCapabilityReport {
        val supportedDevice = if (status == "supported") {
            """
            device.0.name=Test GPU
            device.0.type=integrated_gpu
            device.0.api_version_raw=4194304
            device.0.api_version=1.0.0
            device.0.driver_version_raw=1
            device.0.driver_version_hex=0x00000001
            device.0.vendor_id=1
            device.0.vendor_id_hex=0x00000001
            device.0.device_id=2
            device.0.device_id_hex=0x00000002
            device.0.graphics_queue_families=0
            device.0.present_queue_families=0
            device.0.selected_graphics_queue_family=0
            device.0.selected_present_queue_family=0
            device.0.swapchain_extension=true
            device.0.present_support_query_result=VK_SUCCESS
            device.0.device_extension_query_result=VK_SUCCESS
            device.0.surface_format_query_result=VK_SUCCESS
            device.0.surface_format=VK_FORMAT_R8G8B8A8_UNORM
            device.0.surface_color_space=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
            device.0.depth_format=VK_FORMAT_D24_UNORM_S8_UINT
            device.0.required_surface_format_supported=true
            device.0.required_depth_format_supported=true
            device.0.compatible=true
            """.trimIndent()
        } else {
            ""
        }
        val deviceCount = if (status == "supported") 1 else 0
        return VulkanCapabilityReportParser.parse(
            buildString {
                appendLine("schema=1")
                appendLine("status=$status")
                appendLine("loader_available=${status != "failed"}")
                appendLine("requested_api_version=1.0.0")
                appendLine("instance_api_version_raw=4194304")
                appendLine("instance_api_version=1.0.0")
                appendLine("instance_version_query_result=VK_SUCCESS")
                appendLine("instance_surface_extension=true")
                appendLine("android_surface_extension=true")
                appendLine("physical_device_count=$deviceCount")
                appendLine("error=$error")
                if (supportedDevice.isNotEmpty()) appendLine(supportedDevice)
                appendLine("live_vulkan_objects_after_probe=0")
            }
        )
    }
}
