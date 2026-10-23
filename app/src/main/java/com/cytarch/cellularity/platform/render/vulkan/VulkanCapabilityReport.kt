package com.cytarch.cellularity.platform.render.vulkan

internal enum class VulkanCapabilityStatus {
    SUPPORTED,
    UNSUPPORTED,
    FAILED
}

internal data class VulkanDeviceCapability(
    val name: String,
    val type: String,
    val apiVersion: String,
    val apiVersionRaw: Long,
    val driverVersionRaw: Long,
    val driverVersionHex: String,
    val vendorId: Long,
    val vendorIdHex: String,
    val deviceId: Long,
    val deviceIdHex: String,
    val graphicsQueueFamilies: List<Int>,
    val presentQueueFamilies: List<Int>,
    val selectedGraphicsQueueFamily: Int,
    val selectedPresentQueueFamily: Int,
    val swapchainExtension: Boolean,
    val presentSupportQueryResult: String,
    val deviceExtensionQueryResult: String,
    val surfaceFormatQueryResult: String,
    val surfaceFormat: String,
    val surfaceColorSpace: String,
    val depthFormat: String,
    val requiredSurfaceFormatSupported: Boolean,
    val requiredDepthFormatSupported: Boolean,
    val compatible: Boolean
)

internal data class VulkanCapabilityReport(
    val status: VulkanCapabilityStatus,
    val loaderAvailable: Boolean,
    val requestedApiVersion: String,
    val instanceApiVersion: String,
    val instanceApiVersionRaw: Long,
    val instanceVersionQueryResult: String,
    val instanceSurfaceExtension: Boolean,
    val androidSurfaceExtension: Boolean,
    val devices: List<VulkanDeviceCapability>,
    val error: String,
    val liveVulkanObjectsAfterProbe: Int,
    val raw: String
)

internal object VulkanCapabilityReportParser {
    fun parse(raw: String): VulkanCapabilityReport {
        return runCatching { parseChecked(raw) }.getOrElse { failure ->
            VulkanCapabilityReport(
                status = VulkanCapabilityStatus.FAILED,
                loaderAvailable = false,
                requestedApiVersion = "1.0.0",
                instanceApiVersion = "unknown",
                instanceApiVersionRaw = 0,
                instanceVersionQueryResult = "VK_RESULT_UNKNOWN",
                instanceSurfaceExtension = false,
                androidSurfaceExtension = false,
                devices = emptyList(),
                error = "Malformed Vulkan capability report: ${failure.message ?: "unknown error"}",
                liveVulkanObjectsAfterProbe = 0,
                raw = raw
            )
        }
    }

    private fun parseChecked(raw: String): VulkanCapabilityReport {
        val fields = linkedMapOf<String, String>()
        raw.lineSequence().filter(String::isNotBlank).forEachIndexed { index, line ->
            val separator = line.indexOf('=')
            require(separator > 0) { "line ${index + 1} has no key/value separator" }
            val key = line.substring(0, separator)
            require(fields.put(key, line.substring(separator + 1)) == null) {
                "duplicate field $key"
            }
        }
        require(fields.requiredInt("schema") == 1) { "unsupported schema" }

        val status = when (fields.required("status")) {
            "supported" -> VulkanCapabilityStatus.SUPPORTED
            "unsupported" -> VulkanCapabilityStatus.UNSUPPORTED
            "failed" -> VulkanCapabilityStatus.FAILED
            else -> error("unknown status")
        }
        val deviceCount = fields.requiredInt("physical_device_count")
        require(deviceCount in 0..64) { "invalid physical device count" }
        val devices = List(deviceCount) { index -> fields.parseDevice(index) }
        val liveObjects = fields.requiredInt("live_vulkan_objects_after_probe")
        require(liveObjects == 0) { "probe retained Vulkan objects" }
        val loaderAvailable = fields.requiredBoolean("loader_available")
        if (status == VulkanCapabilityStatus.SUPPORTED) {
            require(loaderAvailable) { "supported result has no Vulkan loader" }
            require(devices.any(VulkanDeviceCapability::compatible)) {
                "supported result has no compatible device"
            }
        }

        return VulkanCapabilityReport(
            status = status,
            loaderAvailable = loaderAvailable,
            requestedApiVersion = fields.required("requested_api_version"),
            instanceApiVersion = fields.required("instance_api_version"),
            instanceApiVersionRaw = fields.requiredLong("instance_api_version_raw"),
            instanceVersionQueryResult = fields.required("instance_version_query_result"),
            instanceSurfaceExtension = fields.requiredBoolean("instance_surface_extension"),
            androidSurfaceExtension = fields.requiredBoolean("android_surface_extension"),
            devices = devices,
            error = fields.required("error"),
            liveVulkanObjectsAfterProbe = liveObjects,
            raw = raw
        )
    }

    private fun Map<String, String>.parseDevice(index: Int): VulkanDeviceCapability {
        val prefix = "device.$index."
        return VulkanDeviceCapability(
            name = required(prefix + "name"),
            type = required(prefix + "type"),
            apiVersion = required(prefix + "api_version"),
            apiVersionRaw = requiredLong(prefix + "api_version_raw"),
            driverVersionRaw = requiredLong(prefix + "driver_version_raw"),
            driverVersionHex = required(prefix + "driver_version_hex"),
            vendorId = requiredLong(prefix + "vendor_id"),
            vendorIdHex = required(prefix + "vendor_id_hex"),
            deviceId = requiredLong(prefix + "device_id"),
            deviceIdHex = required(prefix + "device_id_hex"),
            graphicsQueueFamilies = requiredIntList(prefix + "graphics_queue_families"),
            presentQueueFamilies = requiredIntList(prefix + "present_queue_families"),
            selectedGraphicsQueueFamily = requiredInt(prefix + "selected_graphics_queue_family"),
            selectedPresentQueueFamily = requiredInt(prefix + "selected_present_queue_family"),
            swapchainExtension = requiredBoolean(prefix + "swapchain_extension"),
            presentSupportQueryResult = required(prefix + "present_support_query_result"),
            deviceExtensionQueryResult = required(prefix + "device_extension_query_result"),
            surfaceFormatQueryResult = required(prefix + "surface_format_query_result"),
            surfaceFormat = required(prefix + "surface_format"),
            surfaceColorSpace = required(prefix + "surface_color_space"),
            depthFormat = required(prefix + "depth_format"),
            requiredSurfaceFormatSupported = requiredBoolean(
                prefix + "required_surface_format_supported"
            ),
            requiredDepthFormatSupported = requiredBoolean(
                prefix + "required_depth_format_supported"
            ),
            compatible = requiredBoolean(prefix + "compatible")
        )
    }

    private fun Map<String, String>.required(key: String): String {
        return requireNotNull(this[key]) { "missing field $key" }
    }

    private fun Map<String, String>.requiredBoolean(key: String): Boolean {
        return when (val value = required(key)) {
            "true" -> true
            "false" -> false
            else -> error("invalid boolean $key=$value")
        }
    }

    private fun Map<String, String>.requiredInt(key: String): Int {
        return required(key).toIntOrNull() ?: error("invalid integer $key")
    }

    private fun Map<String, String>.requiredLong(key: String): Long {
        return required(key).toLongOrNull() ?: error("invalid integer $key")
    }

    private fun Map<String, String>.requiredIntList(key: String): List<Int> {
        val value = required(key)
        if (value.isBlank()) return emptyList()
        return value.split(',').map { entry ->
            entry.toIntOrNull() ?: error("invalid integer in $key")
        }
    }
}
