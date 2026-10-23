#include "app/render/NativeVulkanSurfaceLease.h"

namespace cellularity::app::render_worker {

NativeVulkanSurfaceLease::NativeVulkanSurfaceLease(
        ANativeWindow* window, ReleaseFn release) noexcept
        : mWindow(window),
          mRelease(release) {
}

NativeVulkanSurfaceLease::NativeVulkanSurfaceLease(
        NativeVulkanSurfaceLease&& other) noexcept
        : mWindow(std::exchange(other.mWindow, nullptr)),
          mRelease(std::exchange(other.mRelease, nullptr)) {
}

NativeVulkanSurfaceLease& NativeVulkanSurfaceLease::operator=(
        NativeVulkanSurfaceLease&& other) noexcept {
    if (this != &other) {
        reset();
        mWindow = std::exchange(other.mWindow, nullptr);
        mRelease = std::exchange(other.mRelease, nullptr);
    }
    return *this;
}

NativeVulkanSurfaceLease::~NativeVulkanSurfaceLease() {
    reset();
}

NativeVulkanSurfaceLease::operator bool() const noexcept {
    return mWindow != nullptr;
}

ANativeWindow* NativeVulkanSurfaceLease::get() const noexcept {
    return mWindow;
}

ANativeWindow* NativeVulkanSurfaceLease::takeForBackend() noexcept {
    ANativeWindow* result = mWindow;
    mWindow = nullptr;
    mRelease = nullptr;
    return result;
}

void NativeVulkanSurfaceLease::reset() noexcept {
    ANativeWindow* window = std::exchange(mWindow, nullptr);
    ReleaseFn release = std::exchange(mRelease, nullptr);
    if (window != nullptr && release != nullptr) {
        release(window);
    }
}

} // namespace cellularity::app::render_worker
