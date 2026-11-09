#include "render/backend/vulkan/NativeVulkanSurface.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// The platform surface structure (VkWin32SurfaceCreateInfoKHR) and the
// vkCreateWin32SurfaceKHR declaration live in vulkan_win32.h.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

namespace cellularity::render::vulkan {

VkResult createPlatformVulkanSurface(VkInstance instance,
        const NativeVulkanWindow& window,
        VkSurfaceKHR* surface) noexcept {
    if (instance == VK_NULL_HANDLE || surface == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const HWND hwnd = static_cast<HWND>(window.nativeHandle());
    if (hwnd == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // The instance a Win32 surface is created against must be the HINSTANCE that
    // owns the window class, not the executable's module handle. The driver uses
    // it to resolve the window's owning module when it builds the presentation
    // path, and passing the wrong one yields a surface that is created
    // successfully but that no physical device reports as presentable.
    const HINSTANCE windowInstance =
            reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    const HINSTANCE moduleInstance = ::GetModuleHandleW(nullptr);
    const HINSTANCE owner =
            windowInstance != nullptr ? windowInstance : moduleInstance;

    const VkWin32SurfaceCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            nullptr,
            0,
            owner,
            hwnd,
    };
    return vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, surface);
}

} // namespace cellularity::render::vulkan

#elif defined(__ANDROID__)

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <android/native_window.h>

namespace cellularity::render::vulkan {

VkResult createPlatformVulkanSurface(VkInstance instance,
        const NativeVulkanWindow& window,
        VkSurfaceKHR* surface) noexcept {
    if (instance == VK_NULL_HANDLE || surface == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto* nativeWindow = static_cast<ANativeWindow*>(window.nativeHandle());
    if (nativeWindow == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const VkAndroidSurfaceCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            nullptr,
            0,
            nativeWindow,
    };
    return vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, surface);
}

} // namespace cellularity::render::vulkan

#elif defined(CELLULARITY_VULKAN_XLIB)

#include <X11/Xlib.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>

namespace cellularity::render::vulkan {

VkResult createPlatformVulkanSurface(VkInstance instance,
        const NativeVulkanWindow& window,
        VkSurfaceKHR* surface) noexcept {
    if (instance == VK_NULL_HANDLE || surface == nullptr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const auto* handle =
            static_cast<const XlibWindowHandle*>(window.nativeHandle());
    if (handle == nullptr || handle->display == nullptr || handle->window == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkXlibSurfaceCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            nullptr,
            0,
            static_cast<Display*>(handle->display),
            static_cast<::Window>(handle->window),
    };
    return vkCreateXlibSurfaceKHR(instance, &createInfo, nullptr, surface);
}

} // namespace cellularity::render::vulkan

#endif
