#pragma once

// The one call that differs per window system: turning a native window into a
// VkSurfaceKHR. Everything else in the Vulkan session is platform-independent.
//
// A build with no window system (CELLULARITY_VULKAN_SURFACE_EXTENSION_NAME is
// empty) compiles no definition, which is why the session must consult
// nativeVulkanWindowSurfaceSupported() before calling this.

#include <vulkan/vulkan.h>

#include "render/backend/vulkan/NativeVulkanWindow.h"

namespace cellularity::render::vulkan {

// Creates a presentable surface for the window. Returns the Vulkan result so the
// caller can report the exact failure; *surface is left untouched on failure.
[[nodiscard]] VkResult createPlatformVulkanSurface(VkInstance instance,
        const NativeVulkanWindow& window,
        VkSurfaceKHR* surface) noexcept;

} // namespace cellularity::render::vulkan
