package com.cytarch.cellularity.platform.render.vulkan

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class VulkanCapabilityReportParserTest {
    @Test
    fun parsesSupportedDeviceAndEverySelectionRequirement() {
        val report = VulkanCapabilityReportParser.parse(supportedReport())

        assertEquals(VulkanCapabilityStatus.SUPPORTED, report.status)
        assertTrue(report.loaderAvailable)
        assertEquals("1.3.280", report.instanceApiVersion)
        assertEquals(1, report.devices.size)
        with(report.devices.single()) {
            assertEquals("Test GPU", name)
            assertEquals(0x1234, vendorId)
            assertEquals(listOf(0, 2), graphicsQueueFamilies)
            assertEquals(listOf(0), presentQueueFamilies)
            assertEquals(0, selectedGraphicsQueueFamily)
            assertEquals(0, selectedPresentQueueFamily)
            assertTrue(swapchainExtension)
            assertEquals("VK_SUCCESS", presentSupportQueryResult)
            assertEquals("VK_SUCCESS", deviceExtensionQueryResult)
            assertEquals("VK_SUCCESS", surfaceFormatQueryResult)
            assertEquals("VK_FORMAT_R8G8B8A8_UNORM", surfaceFormat)
            assertEquals("VK_FORMAT_D24_UNORM_S8_UINT", depthFormat)
            assertTrue(requiredSurfaceFormatSupported)
            assertTrue(requiredDepthFormatSupported)
            assertTrue(compatible)
        }
        assertEquals(0, report.liveVulkanObjectsAfterProbe)
    }

    @Test
    fun parsesUnsupportedLoaderWithNoPhysicalDevices() {
        val report = VulkanCapabilityReportParser.parse(
            topLevelReport(
                status = "unsupported",
                loaderAvailable = true,
                physicalDeviceCount = 0,
                error = "The Vulkan loader enumerated no physical devices"
            )
        )

        assertEquals(VulkanCapabilityStatus.UNSUPPORTED, report.status)
        assertTrue(report.loaderAvailable)
        assertTrue(report.devices.isEmpty())
        assertTrue(report.error.contains("no physical devices"))
    }

    @Test
    fun preservesSymbolicVkResultFromProbeFailure() {
        val report = VulkanCapabilityReportParser.parse(
            topLevelReport(
                status = "failed",
                loaderAvailable = false,
                physicalDeviceCount = 0,
                error = "vkCreateInstance failed: VK_ERROR_INCOMPATIBLE_DRIVER (-9)"
            )
        )

        assertEquals(VulkanCapabilityStatus.FAILED, report.status)
        assertFalse(report.loaderAvailable)
        assertTrue(report.error.contains("VK_ERROR_INCOMPATIBLE_DRIVER"))
        assertTrue(report.raw.contains("VK_ERROR_INCOMPATIBLE_DRIVER"))
    }

    @Test
    fun convertsMalformedNativeTextIntoSafeFailure() {
        val report = VulkanCapabilityReportParser.parse(
            "schema=1\nstatus=supported\nstatus=failed\n"
        )

        assertEquals(VulkanCapabilityStatus.FAILED, report.status)
        assertFalse(report.loaderAvailable)
        assertTrue(report.error.contains("Malformed Vulkan capability report"))
        assertTrue(report.error.contains("duplicate field status"))
    }

    private fun supportedReport(): String {
        return topLevelReport(
            status = "supported",
            loaderAvailable = true,
            physicalDeviceCount = 1,
            error = "",
            devices = """
                device.0.name=Test GPU
                device.0.type=integrated_gpu
                device.0.api_version_raw=4206872
                device.0.api_version=1.3.280
                device.0.driver_version_raw=16909060
                device.0.driver_version_hex=0x01020304
                device.0.vendor_id=4660
                device.0.vendor_id_hex=0x00001234
                device.0.device_id=22136
                device.0.device_id_hex=0x00005678
                device.0.graphics_queue_families=0,2
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
                future_field=ignored
            """.trimIndent()
        )
    }

    private fun topLevelReport(
        status: String,
        loaderAvailable: Boolean,
        physicalDeviceCount: Int,
        error: String,
        devices: String = ""
    ): String {
        return buildString {
            appendLine("schema=1")
            appendLine("status=$status")
            appendLine("loader_available=$loaderAvailable")
            appendLine("requested_api_version=1.0.0")
            appendLine("instance_api_version_raw=4206872")
            appendLine("instance_api_version=1.3.280")
            appendLine("instance_version_query_result=VK_SUCCESS")
            appendLine("instance_surface_extension=true")
            appendLine("android_surface_extension=true")
            appendLine("physical_device_count=$physicalDeviceCount")
            appendLine("error=$error")
            if (devices.isNotBlank()) appendLine(devices)
            appendLine("live_vulkan_objects_after_probe=0")
        }
    }
}
