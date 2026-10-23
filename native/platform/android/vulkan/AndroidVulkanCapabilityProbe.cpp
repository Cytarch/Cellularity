#include "platform/android/vulkan/AndroidVulkanCapabilityProbe.h"

#include "render/backend/vulkan/VulkanResult.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cellularity::android::vulkan {
namespace {

using cellularity::render::vulkan::formatName;
using cellularity::render::vulkan::resultDescription;
using cellularity::render::vulkan::resultName;
using cellularity::render::vulkan::versionString;

constexpr std::uint32_t kProbeSchemaVersion = 1;
constexpr std::uint32_t kRequestedApiVersion = VK_API_VERSION_1_0;
constexpr std::int32_t kMissingQueueFamily = -1;

enum class ProbeStatus {
    Supported,
    Unsupported,
    Failed
};

struct DeviceCapability {
    std::string name;
    std::string type;
    std::uint32_t apiVersion = 0;
    std::uint32_t driverVersion = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::vector<std::uint32_t> graphicsQueueFamilies;
    std::vector<std::uint32_t> presentQueueFamilies;
    std::int32_t selectedGraphicsQueueFamily = kMissingQueueFamily;
    std::int32_t selectedPresentQueueFamily = kMissingQueueFamily;
    bool swapchainExtension = false;
    std::string presentSupportQueryResult = "VK_SUCCESS";
    std::string deviceExtensionQueryResult = "VK_SUCCESS";
    std::string surfaceFormatQueryResult = "VK_SUCCESS";
    std::string surfaceFormat = "none";
    std::string surfaceColorSpace = "none";
    std::string depthFormat = "none";
    bool requiredSurfaceFormatSupported = false;
    bool requiredDepthFormatSupported = false;
    bool compatible = false;
};

struct CapabilityReport {
    ProbeStatus status = ProbeStatus::Failed;
    bool loaderAvailable = true;
    std::uint32_t loaderApiVersion = VK_API_VERSION_1_0;
    std::string instanceVersionQueryResult = "VK_SUCCESS";
    bool surfaceExtension = false;
    bool androidSurfaceExtension = false;
    std::string error;
    std::vector<DeviceCapability> devices;
};

class ProbeObjects {
public:
    ~ProbeObjects() {
        if (mSurface != VK_NULL_HANDLE && mInstance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        }
        if (mInstance != VK_NULL_HANDLE) {
            vkDestroyInstance(mInstance, nullptr);
        }
    }

    ProbeObjects(const ProbeObjects&) = delete;
    ProbeObjects& operator=(const ProbeObjects&) = delete;

    ProbeObjects() = default;

    VkInstance mInstance = VK_NULL_HANDLE;
    VkSurfaceKHR mSurface = VK_NULL_HANDLE;
};

std::string hexadecimal(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << std::setw(8)
           << std::setfill('0') << value;
    return stream.str();
}

std::string sanitizeLineValue(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const unsigned char character : value) {
        if (character == '\n' || character == '\r' || character == '\t') {
            sanitized.push_back(' ');
        } else if (character < 0x20 || character >= 0x7F) {
            sanitized.push_back('?');
        } else {
            sanitized.push_back(static_cast<char>(character));
        }
    }
    return sanitized;
}

std::string deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
            return "other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "integrated_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "discrete_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "virtual_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "cpu";
        default:
            return "unknown";
    }
}

const char* colorSpaceName(VkColorSpaceKHR colorSpace) {
    return colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
            ? "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR"
            : "VK_COLOR_SPACE_OTHER";
}

bool hasExtension(const std::vector<VkExtensionProperties>& extensions,
        const char* requiredName) {
    return std::any_of(extensions.begin(), extensions.end(),
            [requiredName](const VkExtensionProperties& extension) {
                return std::string_view(extension.extensionName) == requiredName;
            });
}

VkResult enumerateInstanceExtensions(
        std::vector<VkExtensionProperties>& extensions) {
    std::uint32_t count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    extensions.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkEnumerateInstanceExtensionProperties(
            nullptr, &count, extensions.data());
    extensions.resize(count);
    return result;
}

VkResult enumeratePhysicalDevices(VkInstance instance,
        std::vector<VkPhysicalDevice>& devices) {
    std::uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    devices.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    devices.resize(count);
    return result;
}

VkResult enumerateDeviceExtensions(VkPhysicalDevice device,
        std::vector<VkExtensionProperties>& extensions) {
    std::uint32_t count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(
            device, nullptr, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    extensions.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkEnumerateDeviceExtensionProperties(
            device, nullptr, &count, extensions.data());
    extensions.resize(count);
    return result;
}

VkResult enumerateSurfaceFormats(VkPhysicalDevice device, VkSurfaceKHR surface,
        std::vector<VkSurfaceFormatKHR>& formats) {
    std::uint32_t count = 0;
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    formats.resize(count);
    if (count == 0) {
        return VK_SUCCESS;
    }
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &count, formats.data());
    formats.resize(count);
    return result;
}

bool selectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats,
        VkSurfaceFormatKHR& selected) {
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
        selected = {
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        };
        return true;
    }
    constexpr std::array<VkFormat, 2> candidates = {
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_B8G8R8A8_UNORM
    };
    for (const VkFormat candidate : candidates) {
        const auto match = std::find_if(formats.begin(), formats.end(),
                [candidate](const VkSurfaceFormatKHR& format) {
                    return format.format == candidate
                            && format.colorSpace
                            == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                });
        if (match != formats.end()) {
            selected = *match;
            return true;
        }
    }
    return false;
}

VkFormat selectDepthFormat(VkPhysicalDevice device) {
    constexpr std::array<VkFormat, 3> candidates = {
            VK_FORMAT_D16_UNORM,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D32_SFLOAT
    };
    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device, candidate, &properties);
        if ((properties.optimalTilingFeatures
                    & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                != 0) {
            return candidate;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

std::string joinQueueFamilies(const std::vector<std::uint32_t>& families) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < families.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << families[index];
    }
    return stream.str();
}

DeviceCapability inspectDevice(VkPhysicalDevice device, VkSurfaceKHR surface) {
    DeviceCapability report;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    report.name = sanitizeLineValue(properties.deviceName);
    report.type = deviceTypeName(properties.deviceType);
    report.apiVersion = properties.apiVersion;
    report.driverVersion = properties.driverVersion;
    report.vendorId = properties.vendorID;
    report.deviceId = properties.deviceID;

    std::uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueProperties(queueCount);
    if (queueCount > 0) {
        vkGetPhysicalDeviceQueueFamilyProperties(
                device, &queueCount, queueProperties.data());
        queueProperties.resize(queueCount);
    }

    for (std::uint32_t index = 0; index < queueCount; ++index) {
        if ((queueProperties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            report.graphicsQueueFamilies.push_back(index);
        }
        VkBool32 presentSupported = VK_FALSE;
        const VkResult presentResult = vkGetPhysicalDeviceSurfaceSupportKHR(
                device, index, surface, &presentSupported);
        if (presentResult != VK_SUCCESS) {
            if (report.presentSupportQueryResult == "VK_SUCCESS") {
                report.presentSupportQueryResult = resultName(presentResult);
            }
        } else if (presentSupported == VK_TRUE) {
            report.presentQueueFamilies.push_back(index);
        }
    }

    for (const std::uint32_t graphics : report.graphicsQueueFamilies) {
        if (std::find(report.presentQueueFamilies.begin(),
                    report.presentQueueFamilies.end(), graphics)
                != report.presentQueueFamilies.end()) {
            report.selectedGraphicsQueueFamily = static_cast<std::int32_t>(graphics);
            report.selectedPresentQueueFamily = static_cast<std::int32_t>(graphics);
            break;
        }
    }
    if (report.selectedGraphicsQueueFamily == kMissingQueueFamily
            && !report.graphicsQueueFamilies.empty()
            && !report.presentQueueFamilies.empty()) {
        report.selectedGraphicsQueueFamily =
                static_cast<std::int32_t>(report.graphicsQueueFamilies.front());
        report.selectedPresentQueueFamily =
                static_cast<std::int32_t>(report.presentQueueFamilies.front());
    }

    std::vector<VkExtensionProperties> extensions;
    const VkResult extensionResult = enumerateDeviceExtensions(device, extensions);
    report.deviceExtensionQueryResult = resultName(extensionResult);
    report.swapchainExtension = extensionResult == VK_SUCCESS
            && hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    std::vector<VkSurfaceFormatKHR> formats;
    const VkResult formatResult = enumerateSurfaceFormats(device, surface, formats);
    report.surfaceFormatQueryResult = resultName(formatResult);
    VkSurfaceFormatKHR selectedSurfaceFormat{};
    const bool hasRequiredSurfaceFormat = formatResult == VK_SUCCESS
            && selectSurfaceFormat(formats, selectedSurfaceFormat);
    report.requiredSurfaceFormatSupported = hasRequiredSurfaceFormat;
    if (hasRequiredSurfaceFormat) {
        report.surfaceFormat = formatName(selectedSurfaceFormat.format);
        report.surfaceColorSpace = colorSpaceName(selectedSurfaceFormat.colorSpace);
    }

    const VkFormat depthFormat = selectDepthFormat(device);
    report.requiredDepthFormatSupported = depthFormat != VK_FORMAT_UNDEFINED;
    if (depthFormat != VK_FORMAT_UNDEFINED) {
        report.depthFormat = formatName(depthFormat);
    }

    report.compatible = report.selectedGraphicsQueueFamily != kMissingQueueFamily
            && report.selectedPresentQueueFamily != kMissingQueueFamily
            && report.presentSupportQueryResult == "VK_SUCCESS"
            && report.deviceExtensionQueryResult == "VK_SUCCESS"
            && report.surfaceFormatQueryResult == "VK_SUCCESS"
            && report.swapchainExtension
            && hasRequiredSurfaceFormat
            && depthFormat != VK_FORMAT_UNDEFINED;
    return report;
}

CapabilityReport collectCapabilities(ANativeWindow* window) {
    CapabilityReport report;
    if (window == nullptr) {
        report.error = "Android native window is unavailable";
        return report;
    }

    const auto enumerateInstanceVersion =
            reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
                    vkGetInstanceProcAddr(VK_NULL_HANDLE,
                            "vkEnumerateInstanceVersion"));
    if (enumerateInstanceVersion != nullptr) {
        const VkResult versionResult =
                enumerateInstanceVersion(&report.loaderApiVersion);
        report.instanceVersionQueryResult = resultName(versionResult);
        if (versionResult != VK_SUCCESS) {
            report.error = resultDescription(
                    "vkEnumerateInstanceVersion", versionResult);
            return report;
        }
    }

    std::vector<VkExtensionProperties> instanceExtensions;
    const VkResult extensionResult =
            enumerateInstanceExtensions(instanceExtensions);
    if (extensionResult != VK_SUCCESS) {
        report.error = resultDescription(
                "vkEnumerateInstanceExtensionProperties", extensionResult);
        return report;
    }
    report.surfaceExtension =
            hasExtension(instanceExtensions, VK_KHR_SURFACE_EXTENSION_NAME);
    report.androidSurfaceExtension =
            hasExtension(instanceExtensions, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
    if (!report.surfaceExtension || !report.androidSurfaceExtension) {
        report.status = ProbeStatus::Unsupported;
        report.error = "Required Vulkan Android surface extensions are unavailable";
        return report;
    }

    const VkApplicationInfo applicationInfo{
            VK_STRUCTURE_TYPE_APPLICATION_INFO,
            nullptr,
            "Cellularity capability probe",
            1,
            "Cellularity",
            1,
            kRequestedApiVersion
    };
    const std::array<const char*, 2> requiredExtensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    const VkInstanceCreateInfo instanceCreateInfo{
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            nullptr,
            0,
            &applicationInfo,
            0,
            nullptr,
            static_cast<std::uint32_t>(requiredExtensions.size()),
            requiredExtensions.data()
    };

    ProbeObjects objects;
    VkResult result = vkCreateInstance(
            &instanceCreateInfo, nullptr, &objects.mInstance);
    if (result != VK_SUCCESS) {
        report.status = result == VK_ERROR_INCOMPATIBLE_DRIVER
                ? ProbeStatus::Unsupported
                : ProbeStatus::Failed;
        report.error = resultDescription("vkCreateInstance", result);
        return report;
    }

    const VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo{
            VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            nullptr,
            0,
            window
    };
    result = vkCreateAndroidSurfaceKHR(objects.mInstance,
            &surfaceCreateInfo, nullptr, &objects.mSurface);
    if (result != VK_SUCCESS) {
        report.error = resultDescription("vkCreateAndroidSurfaceKHR", result);
        return report;
    }

    std::vector<VkPhysicalDevice> devices;
    result = enumeratePhysicalDevices(objects.mInstance, devices);
    if (result != VK_SUCCESS) {
        report.error = resultDescription("vkEnumeratePhysicalDevices", result);
        return report;
    }
    report.devices.reserve(devices.size());
    for (const VkPhysicalDevice device : devices) {
        report.devices.push_back(inspectDevice(device, objects.mSurface));
    }

    if (std::any_of(report.devices.begin(), report.devices.end(),
                [](const DeviceCapability& device) {
                    return device.compatible;
                })) {
        report.status = ProbeStatus::Supported;
    } else {
        report.status = ProbeStatus::Unsupported;
        report.error = report.devices.empty()
                ? "The Vulkan loader enumerated no physical devices"
                : "No physical device satisfies the Cellularity Vulkan 1.0 baseline";
    }
    return report;
}

const char* statusName(ProbeStatus status) {
    switch (status) {
        case ProbeStatus::Supported:
            return "supported";
        case ProbeStatus::Unsupported:
            return "unsupported";
        case ProbeStatus::Failed:
            return "failed";
    }
    return "failed";
}

void appendField(std::ostringstream& stream, std::string_view key,
        std::string_view value) {
    stream << key << '=' << sanitizeLineValue(value) << '\n';
}

void appendField(std::ostringstream& stream, std::string_view key,
        std::uint32_t value) {
    stream << key << '=' << value << '\n';
}

void appendField(std::ostringstream& stream, std::string_view key,
        std::int32_t value) {
    stream << key << '=' << value << '\n';
}

void appendField(std::ostringstream& stream, std::string_view key, bool value) {
    stream << key << '=' << (value ? "true" : "false") << '\n';
}

std::string serializeReport(const CapabilityReport& report) {
    std::ostringstream stream;
    appendField(stream, "schema", kProbeSchemaVersion);
    appendField(stream, "status", std::string_view(statusName(report.status)));
    appendField(stream, "loader_available", report.loaderAvailable);
    appendField(stream, "requested_api_version", versionString(kRequestedApiVersion));
    appendField(stream, "instance_api_version_raw", report.loaderApiVersion);
    appendField(stream, "instance_api_version", versionString(report.loaderApiVersion));
    appendField(stream, "instance_version_query_result",
            report.instanceVersionQueryResult);
    appendField(stream, "instance_surface_extension", report.surfaceExtension);
    appendField(stream, "android_surface_extension",
            report.androidSurfaceExtension);
    appendField(stream, "physical_device_count",
            static_cast<std::uint32_t>(report.devices.size()));
    appendField(stream, "error", report.error);

    for (std::size_t index = 0; index < report.devices.size(); ++index) {
        const DeviceCapability& device = report.devices[index];
        const std::string prefix = "device." + std::to_string(index) + '.';
        appendField(stream, prefix + "name", device.name);
        appendField(stream, prefix + "type", device.type);
        appendField(stream, prefix + "api_version_raw", device.apiVersion);
        appendField(stream, prefix + "api_version", versionString(device.apiVersion));
        appendField(stream, prefix + "driver_version_raw", device.driverVersion);
        appendField(stream, prefix + "driver_version_hex",
                hexadecimal(device.driverVersion));
        appendField(stream, prefix + "vendor_id", device.vendorId);
        appendField(stream, prefix + "vendor_id_hex", hexadecimal(device.vendorId));
        appendField(stream, prefix + "device_id", device.deviceId);
        appendField(stream, prefix + "device_id_hex", hexadecimal(device.deviceId));
        appendField(stream, prefix + "graphics_queue_families",
                joinQueueFamilies(device.graphicsQueueFamilies));
        appendField(stream, prefix + "present_queue_families",
                joinQueueFamilies(device.presentQueueFamilies));
        appendField(stream, prefix + "selected_graphics_queue_family",
                device.selectedGraphicsQueueFamily);
        appendField(stream, prefix + "selected_present_queue_family",
                device.selectedPresentQueueFamily);
        appendField(stream, prefix + "swapchain_extension",
                device.swapchainExtension);
        appendField(stream, prefix + "present_support_query_result",
                device.presentSupportQueryResult);
        appendField(stream, prefix + "device_extension_query_result",
                device.deviceExtensionQueryResult);
        appendField(stream, prefix + "surface_format_query_result",
                device.surfaceFormatQueryResult);
        appendField(stream, prefix + "surface_format", device.surfaceFormat);
        appendField(stream, prefix + "surface_color_space",
                device.surfaceColorSpace);
        appendField(stream, prefix + "depth_format", device.depthFormat);
        appendField(stream, prefix + "required_surface_format_supported",
                device.requiredSurfaceFormatSupported);
        appendField(stream, prefix + "required_depth_format_supported",
                device.requiredDepthFormatSupported);
        appendField(stream, prefix + "compatible", device.compatible);
    }

    // collectCapabilities owns all Vulkan handles in an automatic RAII scope.
    // Serialization starts only after that scope has been destroyed.
    appendField(stream, "live_vulkan_objects_after_probe", 0U);
    return stream.str();
}

} // namespace

std::string probeCapabilities(ANativeWindow* window) {
    return serializeReport(collectCapabilities(window));
}

} // namespace cellularity::android::vulkan
