#include "render/backend/vulkan/NativeVulkanWindow.h"

#if defined(__ANDROID__)

#include <android/native_window.h>

#include <utility>

namespace cellularity::render::vulkan {
namespace {

class AndroidNativeVulkanWindow final : public NativeVulkanWindow {
public:
    explicit AndroidNativeVulkanWindow(ANativeWindow* window) noexcept
            : mWindow(window) {}

    ~AndroidNativeVulkanWindow() override {
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

} // namespace

NativeVulkanWindowReference createNativeVulkanWindow(
        ANativeWindow* window) noexcept {
    if (window == nullptr) {
        return nullptr;
    }
    return std::make_unique<AndroidNativeVulkanWindow>(window);
}

} // namespace cellularity::render::vulkan

#endif // __ANDROID__
