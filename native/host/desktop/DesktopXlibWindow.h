#pragma once

// The Linux desktop bridge's only contact with Xlib.
//
// Xlib's headers define macros such as None, Success, Status and Bool that collide
// with ordinary identifiers in the simulation and renderer, so they are confined to
// DesktopXlibWindow.cpp and the bridge sees only these two functions.

#include "render/backend/vulkan/NativeVulkanWindow.h"

namespace cellularity::desktop {

// Creates a hidden, never-mapped window on the bridge's own display connection, for
// the Vulkan device to be created against. Returns nullptr when no X display is
// available (for example a Wayland session without XWayland) or the window could
// not be created. The returned handle is owned by the caller and released with
// destroyXlibAnchorWindow.
[[nodiscard]] cellularity::render::vulkan::XlibWindowHandle* createXlibAnchorWindow(
        int width, int height) noexcept;

// Resizes a window created by createXlibAnchorWindow and updates the extent its
// handle reports. Returns false when the window could not be resized.
[[nodiscard]] bool resizeXlibAnchorWindow(
        cellularity::render::vulkan::XlibWindowHandle* window, int width, int height) noexcept;

// Destroys a window created by createXlibAnchorWindow and frees its handle.
void destroyXlibAnchorWindow(cellularity::render::vulkan::XlibWindowHandle* window) noexcept;

} // namespace cellularity::desktop
