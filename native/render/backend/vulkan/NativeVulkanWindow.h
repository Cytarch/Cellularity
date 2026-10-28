#pragma once

// Platform-neutral handle to the native window a Vulkan session presents into.
//
// Why this exists: the Vulkan clear-frame session is roughly five thousand lines
// of platform-independent rendering, but it used to name ANativeWindow directly
// in about a dozen places - a reference wrapper, the surface-creation call, and
// the attach entry points. Naming the window through this interface instead lets
// the same session serve Android and Windows, and confines each platform's
// surface-creation call to one small translation unit.
//
// Ownership: a window reference is ref-counted, matching ANativeWindow. The
// session consumes exactly one reference, including when attachment fails, so
// createNativeVulkanWindow() returns an owned reference and the caller hands it
// over rather than sharing it.

#include <cstdint>
#include <memory>

// The platform surface extension this build intends to use. Declared here so a
// translation unit can decide whether to enable it without including a
// platform-specific Vulkan header.
#if defined(_WIN32)
#define CELLULARITY_VULKAN_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"
#elif defined(__ANDROID__)
#define CELLULARITY_VULKAN_SURFACE_EXTENSION_NAME "VK_KHR_android_surface"
#else
// No windowed surface is available. Off-screen rendering still works: the
// session creates its instance without a surface extension.
#define CELLULARITY_VULKAN_SURFACE_EXTENSION_NAME ""
#endif

namespace cellularity::render::vulkan {

class NativeVulkanWindow;

/**
 * One owned reference to a native window.
 *
 * Callers never deal with the raw platform type. Create a reference with
 * createNativeVulkanWindow(), pass it to attachWindow(), and let the session
 * consume it.
 */
using NativeVulkanWindowReference = std::unique_ptr<NativeVulkanWindow>;

class NativeVulkanWindow {
public:
    virtual ~NativeVulkanWindow() = default;

    NativeVulkanWindow(const NativeVulkanWindow&) = delete;
    NativeVulkanWindow& operator=(const NativeVulkanWindow&) = delete;

    [[nodiscard]] virtual int width() const noexcept = 0;
    [[nodiscard]] virtual int height() const noexcept = 0;

    // The platform handle to hand to the surface-creation call, or nullptr when
    // this window cannot present.
    [[nodiscard]] virtual void* nativeHandle() const noexcept = 0;

protected:
    NativeVulkanWindow() = default;
};

#if defined(__ANDROID__)
/**
 * Adopts one ANativeWindow reference. The caller must already own it, typically
 * from ANativeWindow_fromSurface().
 */
[[nodiscard]] inline NativeVulkanWindowReference createNativeVulkanWindow(
        struct ANativeWindow* window) noexcept;
#elif defined(_WIN32)
/**
 * Borrows a window handle. HWNDs are not ref-counted, so the caller keeps
 * ownership and must keep the window alive for the session's lifetime.
 */
[[nodiscard]] inline NativeVulkanWindowReference createNativeVulkanWindow(
        void* hwnd) noexcept;
#endif

/**
 * True when this build can create a presentable window surface. The session uses
 * it to decide whether to require the platform surface extension, so an
 * off-screen host test can run on a machine with no window system.
 */
[[nodiscard]] constexpr bool nativeVulkanWindowSurfaceSupported() noexcept {
#if defined(_WIN32) || defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

} // namespace cellularity::render::vulkan
