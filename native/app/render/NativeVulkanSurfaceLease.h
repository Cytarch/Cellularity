#pragma once

#include <utility>

struct ANativeWindow;

namespace cellularity::app::render_worker {

class NativeVulkanSurfaceLease final {
public:
    using ReleaseFn = void (*)(ANativeWindow*) noexcept;

    NativeVulkanSurfaceLease() noexcept = default;
    NativeVulkanSurfaceLease(ANativeWindow* window, ReleaseFn release) noexcept;
    NativeVulkanSurfaceLease(const NativeVulkanSurfaceLease&) = delete;
    NativeVulkanSurfaceLease& operator=(const NativeVulkanSurfaceLease&) = delete;
    NativeVulkanSurfaceLease(NativeVulkanSurfaceLease&& other) noexcept;
    NativeVulkanSurfaceLease& operator=(NativeVulkanSurfaceLease&& other) noexcept;
    ~NativeVulkanSurfaceLease();

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] ANativeWindow* get() const noexcept;

    // V-27 attach_window consumes the acquired reference for every result.
    [[nodiscard]] ANativeWindow* takeForBackend() noexcept;
    void reset() noexcept;

private:
    ANativeWindow* mWindow = nullptr;
    ReleaseFn mRelease = nullptr;
};

} // namespace cellularity::app::render_worker
