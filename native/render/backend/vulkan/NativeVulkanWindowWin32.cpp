#include "render/backend/vulkan/NativeVulkanWindow.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <utility>

namespace cellularity::render::vulkan {
namespace {

// HWNDs are not ref-counted, so this wrapper borrows the handle. The session
// already receives explicit width and height through attachWindow and
// resizeSurface, so the client-area size is only a fallback for diagnostics.
class Win32NativeVulkanWindow final : public NativeVulkanWindow {
public:
    explicit Win32NativeVulkanWindow(HWND window) noexcept
            : mWindow(window) {}

    [[nodiscard]] int width() const noexcept override {
        RECT client{};
        if (mWindow != nullptr
                && ::GetClientRect(mWindow, &client) != 0) {
            return static_cast<int>(client.right - client.left);
        }
        return 0;
    }

    [[nodiscard]] int height() const noexcept override {
        RECT client{};
        if (mWindow != nullptr
                && ::GetClientRect(mWindow, &client) != 0) {
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

} // namespace

NativeVulkanWindowReference createNativeVulkanWindow(void* hwnd) noexcept {
    if (hwnd == nullptr) {
        return nullptr;
    }
    return std::make_unique<Win32NativeVulkanWindow>(
            static_cast<HWND>(hwnd));
}

} // namespace cellularity::render::vulkan

#endif // _WIN32
