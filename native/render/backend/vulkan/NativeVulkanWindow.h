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
#elif defined(CELLULARITY_VULKAN_XLIB)
// The Linux desktop build presents nothing on screen but, like Windows, stands
// its device up against a hidden window's surface; X11 also covers Wayland
// sessions through XWayland, where AWT itself runs.
#define CELLULARITY_VULKAN_SURFACE_EXTENSION_NAME "VK_KHR_xlib_surface"
#else
// No windowed surface is available. Off-screen rendering still works: the
// session creates its instance without a surface extension.
#define CELLULARITY_VULKAN_SURFACE_EXTENSION_NAME ""
#endif

#if defined(__ANDROID__)
#include <android/native_window.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace cellularity::render::vulkan {

#if defined(CELLULARITY_VULKAN_XLIB)
/**
 * The opaque window handle a Linux caller passes through the ABI: an Xlib
 * connection and one of its windows. Kept free of <X11/Xlib.h> so the header
 * stays includable everywhere; the surface translation unit casts it back.
 */
struct XlibWindowHandle {
    void* display = nullptr;       // Display*
    unsigned long window = 0;      // ::Window
    int width = 0;
    int height = 0;
};
#endif

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
 *
 * Defined here rather than in a translation unit: the factory is small, and a
 * session linked from a static library must not depend on the caller also pulling
 * in the platform window object file.
 */
[[nodiscard]] inline NativeVulkanWindowReference createNativeVulkanWindow(
        void* handle) noexcept {
    // The ABI carries the window as an opaque handle because the same seam serves a
    // Win32 HWND on the desktop, so this factory is where the handle becomes concrete.
    // The Windows branch below takes the same type for the same reason.
    auto* window = static_cast<ANativeWindow*>(handle);
    if (window == nullptr) {
        return nullptr;
    }
    class AndroidWindow final : public NativeVulkanWindow {
    public:
        explicit AndroidWindow(ANativeWindow* owned) noexcept
                : mWindow(owned) {}

        ~AndroidWindow() override {
            if (mWindow != nullptr) {
                ANativeWindow_release(mWindow);
            }
        }

        [[nodiscard]] int width() const noexcept override {
            return mWindow != nullptr ? ANativeWindow_getWidth(mWindow) : 0;
        }

        [[nodiscard]] int height() const noexcept override {
            return mWindow != nullptr ? ANativeWindow_getHeight(mWindow) : 0;
        }

        [[nodiscard]] void* nativeHandle() const noexcept override {
            return mWindow;
        }

    private:
        ANativeWindow* mWindow = nullptr;
    };
    return std::make_unique<AndroidWindow>(window);
}
#elif defined(_WIN32)
/**
 * Borrows a window handle. HWNDs are not ref-counted, so the caller keeps
 * ownership and must keep the window alive for the session's lifetime. The client
 * area size is only a fallback for diagnostics; the session receives its extent
 * explicitly through attachWindow and resizeSurface.
 */
[[nodiscard]] inline NativeVulkanWindowReference createNativeVulkanWindow(
        void* hwnd) noexcept {
    if (hwnd == nullptr) {
        return nullptr;
    }
    class Win32Window final : public NativeVulkanWindow {
    public:
        explicit Win32Window(HWND borrowed) noexcept
                : mWindow(borrowed) {}

        [[nodiscard]] int width() const noexcept override {
            RECT client{};
            if (mWindow != nullptr && ::GetClientRect(mWindow, &client) != 0) {
                return static_cast<int>(client.right - client.left);
            }
            return 0;
        }

        [[nodiscard]] int height() const noexcept override {
            RECT client{};
            if (mWindow != nullptr && ::GetClientRect(mWindow, &client) != 0) {
                return static_cast<int>(client.bottom - client.top);
            }
            return 0;
        }

        [[nodiscard]] void* nativeHandle() const noexcept override {
            return mWindow;
        }

    private:
        HWND mWindow = nullptr;
    };
    return std::make_unique<Win32Window>(static_cast<HWND>(hwnd));
}
#elif defined(CELLULARITY_VULKAN_XLIB)
/**
 * Borrows an Xlib window described by an XlibWindowHandle. The caller keeps both
 * the connection and the window alive for the session's lifetime.
 */
[[nodiscard]] inline NativeVulkanWindowReference createNativeVulkanWindow(
        void* handle) noexcept {
    const auto* xlib = static_cast<const XlibWindowHandle*>(handle);
    if (xlib == nullptr || xlib->display == nullptr || xlib->window == 0) {
        return nullptr;
    }
    class XlibWindow final : public NativeVulkanWindow {
    public:
        explicit XlibWindow(const XlibWindowHandle& borrowed) noexcept
                : mHandle(borrowed) {}

        [[nodiscard]] int width() const noexcept override { return mHandle.width; }
        [[nodiscard]] int height() const noexcept override { return mHandle.height; }

        [[nodiscard]] void* nativeHandle() const noexcept override {
            return const_cast<XlibWindowHandle*>(&mHandle);
        }

    private:
        XlibWindowHandle mHandle;
    };
    return std::make_unique<XlibWindow>(*xlib);
}
#endif

/**
 * True when this build can create a presentable window surface. The session uses
 * it to decide whether to require the platform surface extension, so an
 * off-screen host test can run on a machine with no window system.
 */
[[nodiscard]] constexpr bool nativeVulkanWindowSurfaceSupported() noexcept {
#if defined(_WIN32) || defined(__ANDROID__) || defined(CELLULARITY_VULKAN_XLIB)
    return true;
#else
    return false;
#endif
}

} // namespace cellularity::render::vulkan
