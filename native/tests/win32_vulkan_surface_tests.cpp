// Windows Vulkan presentation-path contract.
//
// The Vulkan renderer itself is platform-independent and already exercised by the
// other host tests. What the Windows port adds is the windowing seam, and that
// seam has two halves worth pinning down before any of it is trusted:
//
//   1. the window abstraction interprets a raw HWND correctly, and
//   2. the platform surface call turns that window into a VkSurfaceKHR that a
//      real physical device actually accepts for presentation.
//
// Both are checked against the live loader and driver. The window is created but
// never shown, so the test needs no interactive desktop session.
//
// Every enumeration here uses the two-call pattern with VK_INCOMPLETE handling:
// the loader may report a larger count on the second call, and treating that as a
// failure would report a driver problem that does not exist.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "render/backend/vulkan/NativeVulkanSurface.h"
#include "render/backend/vulkan/NativeVulkanWindow.h"
#include "tests/support/TestAssertions.h"
#include "tests/support/Win32ContractWindow.h"

namespace {

using cellularity::render::vulkan::createNativeVulkanWindow;
using cellularity::render::vulkan::createPlatformVulkanSurface;
using cellularity::render::vulkan::NativeVulkanWindowReference;
using cellularity::tests::assertTrue;
using cellularity::tests::OffscreenWin32Window;

// A minimal Vulkan 1.0 instance carrying the surface extensions, which is all that
// the surface and presentation queries require.
//
// VK_KHR_win32_surface is an extension of VK_KHR_surface and cannot be enabled on
// its own. An instance created with only the platform extension is accepted by the
// loader and vkCreateWin32SurfaceKHR still succeeds, but no physical device then
// reports the surface as presentable and it offers no formats. Both names must be
// enabled together, which is what the session does.
class SurfaceProbeInstance final {
public:
    SurfaceProbeInstance() {
        mPlatformExtensionName = CELLULARITY_VULKAN_SURFACE_EXTENSION_NAME;
        if (mPlatformExtensionName == nullptr || mPlatformExtensionName[0] == '\0') {
            throw std::runtime_error(
                    "This build names no Vulkan surface extension");
        }
        mEnabledExtensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
        mEnabledExtensions[1] = mPlatformExtensionName;

        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "CellularityVulkanWin32Contract";
        application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        application.pEngineName = "Cellularity";
        application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        application.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &application;
        createInfo.enabledExtensionCount = 2;
        createInfo.ppEnabledExtensionNames = mEnabledExtensions.data();

        const VkResult result = ::vkCreateInstance(&createInfo, nullptr, &mInstance);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(
                    std::string("vkCreateInstance failed: ")
                    + std::to_string(static_cast<int>(result)));
        }
    }

    ~SurfaceProbeInstance() {
        if (mInstance != VK_NULL_HANDLE) {
            ::vkDestroyInstance(mInstance, nullptr);
        }
    }

    SurfaceProbeInstance(const SurfaceProbeInstance&) = delete;
    SurfaceProbeInstance& operator=(const SurfaceProbeInstance&) = delete;

    [[nodiscard]] VkInstance instance() const noexcept {
        return mInstance;
    }

private:
    VkInstance mInstance = VK_NULL_HANDLE;
    const char* mPlatformExtensionName = nullptr;
    std::array<const char*, 2> mEnabledExtensions{};
};

bool advertisesExtension(const std::string& name) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::uint32_t count = 0;
        VkResult result =
                ::vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        if (result != VK_SUCCESS || count == 0) {
            return false;
        }
        std::vector<VkExtensionProperties> extensions(count);
        result = ::vkEnumerateInstanceExtensionProperties(
                nullptr, &count, extensions.data());
        // VK_INCOMPLETE means the count grew between the calls; retry rather than
        // report a missing extension.
        if (result == VK_INCOMPLETE) {
            continue;
        }
        if (result != VK_SUCCESS) {
            return false;
        }
        extensions.resize(count);
        for (const VkExtensionProperties& extension : extensions) {
            if (name == extension.extensionName) {
                return true;
            }
        }
        return false;
    }
    return false;
}

std::vector<VkPhysicalDevice> enumeratePhysicalDevices(VkInstance instance) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::uint32_t count = 0;
        VkResult result = ::vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (result != VK_SUCCESS || count == 0) {
            return {};
        }
        std::vector<VkPhysicalDevice> devices(count);
        result = ::vkEnumeratePhysicalDevices(instance, &count, devices.data());
        if (result == VK_INCOMPLETE) {
            continue;
        }
        if (result != VK_SUCCESS) {
            return {};
        }
        devices.resize(count);
        return devices;
    }
    return {};
}

void ensureVulkanLoaderIsUsable() {
    // Asking for the instance version is the cheapest way to tell a working
    // loader from a missing one, and it separates "no driver" from "bug here".
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            ::vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    if (enumerateVersion == nullptr) {
        throw std::runtime_error(
                "The Vulkan loader exposes no vkEnumerateInstanceVersion");
    }
    std::uint32_t version = 0;
    const VkResult result = enumerateVersion(&version);
    assertTrue(result == VK_SUCCESS, "vkEnumerateInstanceVersion must succeed");
    assertTrue(version >= VK_API_VERSION_1_0,
            "The loader must report at least Vulkan 1.0");

    std::cout << "vulkan_loader_api_version=" << VK_VERSION_MAJOR(version) << "."
              << VK_VERSION_MINOR(version) << "." << VK_VERSION_PATCH(version)
              << "\n";
}

void verifySurfaceExtensionIsAdvertised() {
    const std::string expected = CELLULARITY_VULKAN_SURFACE_EXTENSION_NAME;
    assertTrue(advertisesExtension(expected),
            "The loader must advertise " + expected
                    + "; the Windows build cannot present without it");
    std::cout << "surface_extension=" << expected << "\n";
}

void verifyWindowAbstractionReadsTheHandle() {
    OffscreenWin32Window window;
    NativeVulkanWindowReference reference =
            createNativeVulkanWindow(window.handle());
    assertTrue(reference != nullptr,
            "A valid HWND must produce a window reference");
    assertTrue(reference->nativeHandle() == window.handle(),
            "The window reference must hand back the same HWND");

    // WS_OVERLAPPEDWINDOW adds a frame, so the client area is smaller than the
    // requested window. Only the sign is contractual; the exact size depends on
    // the system theme.
    assertTrue(reference->width() > 0 && reference->height() > 0,
            "The window reference must report a non-empty client area");

    assertTrue(createNativeVulkanWindow(nullptr) == nullptr,
            "A null handle must not produce a window reference");

    std::cout << "window_client_area=" << reference->width() << "x"
              << reference->height() << "\n";
}

void verifySurfaceCreationAndPresentationSupport() {
    OffscreenWin32Window window;
    NativeVulkanWindowReference reference =
            createNativeVulkanWindow(window.handle());
    assertTrue(reference != nullptr, "The test window must wrap");

    SurfaceProbeInstance instance;

    // Devices are enumerated before the surface exists, which is the order the
    // production session uses and the order the standalone contract probe was
    // validated in.
    const std::vector<VkPhysicalDevice> devices =
            enumeratePhysicalDevices(instance.instance());
    assertTrue(!devices.empty(),
            "The machine must expose at least one Vulkan physical device");

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult surfaceResult =
            createPlatformVulkanSurface(instance.instance(), *reference, &surface);
    assertTrue(surfaceResult == VK_SUCCESS,
            "createPlatformVulkanSurface must succeed for a real HWND, got "
                    + std::to_string(static_cast<int>(surfaceResult)));
    assertTrue(surface != VK_NULL_HANDLE,
            "A successful surface call must produce a handle");

    // A surface the driver refuses to present is the failure this test exists to
    // catch, and the observed numbers are printed so the cause is visible without
    // a debugger.
    std::size_t presentable = 0;
    std::uint32_t surfaceFormats = 0;
    std::uint32_t presentModes = 0;
    for (const VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        ::vkGetPhysicalDeviceProperties(device, &properties);

        std::uint32_t familyCount = 0;
        ::vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        for (std::uint32_t index = 0; index < familyCount; ++index) {
            VkBool32 supported = VK_FALSE;
            if (::vkGetPhysicalDeviceSurfaceSupportKHR(
                        device, index, surface, &supported)
                            == VK_SUCCESS
                    && supported == VK_TRUE) {
                ++presentable;
                break;
            }
        }

        std::uint32_t formatCount = 0;
        const VkResult formatResult = ::vkGetPhysicalDeviceSurfaceFormatsKHR(
                device, surface, &formatCount, nullptr);
        std::uint32_t modeCount = 0;
        const VkResult modeResult = ::vkGetPhysicalDeviceSurfacePresentModesKHR(
                device, surface, &modeCount, nullptr);

        std::cout << "device=" << properties.deviceName
                  << " queue_families=" << familyCount
                  << " formats_result=" << static_cast<int>(formatResult)
                  << " formats=" << formatCount
                  << " modes_result=" << static_cast<int>(modeResult)
                  << " modes=" << modeCount << "\n";

        surfaceFormats += formatCount;
        presentModes += modeCount;
    }

    ::vkDestroySurfaceKHR(instance.instance(), surface, nullptr);

    std::cout << "physical_device_count=" << devices.size() << "\n";
    std::cout << "devices_with_present_queue=" << presentable << "\n";
    std::cout << "surface_formats=" << surfaceFormats << "\n";
    std::cout << "surface_present_modes=" << presentModes << "\n";

    assertTrue(presentable == devices.size(),
            "Every enumerated physical device must offer a queue family that can "
            "present to a Win32 surface; without one the desktop port cannot show "
            "a frame");
    assertTrue(surfaceFormats > 0,
            "A presentable Win32 surface must offer at least one format");
    assertTrue(presentModes > 0,
            "A presentable Win32 surface must offer at least one present mode");
}

} // namespace

int main() {
    try {
        ensureVulkanLoaderIsUsable();
        verifySurfaceExtensionIsAdvertised();
        verifyWindowAbstractionReadsTheHandle();
        verifySurfaceCreationAndPresentationSupport();
        std::cout << "win32 vulkan presentation contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "win32 vulkan presentation contract failed: " << error.what()
                  << "\n";
        return 1;
    }
}
