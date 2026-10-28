#pragma once

#include <utility>

#include "render/backend/abi/CellularityVulkanBackendAbi.h"

namespace cellularity::app::render_worker {

class NativeVulkanSurfaceLease final {
public:
    using ReleaseFn = void (*)(CellularityVulkanNativeWindowHandleV1) noexcept;

    NativeVulkanSurfaceLease() noexcept = default;
    NativeVulkanSurfaceLease(CellularityVulkanNativeWindowHandleV1 window, ReleaseFn release) noexcept;
    NativeVulkanSurfaceLease(const NativeVulkanSurfaceLease&) = delete;
    NativeVulkanSurfaceLease& operator=(const NativeVulkanSurfaceLease&) = delete;
    NativeVulkanSurfaceLease(NativeVulkanSurfaceLease&& other) noexcept;
    NativeVulkanSurfaceLease& operator=(NativeVulkanSurfaceLease&& other) noexcept;
    ~NativeVulkanSurfaceLease();

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] CellularityVulkanNativeWindowHandleV1 get() const noexcept;

    // V-27 attach_window consumes the acquired reference for every result.
    [[nodiscard]] CellularityVulkanNativeWindowHandleV1 takeForBackend() noexcept;
    void reset() noexcept;

private:
    CellularityVulkanNativeWindowHandleV1 mWindow = nullptr;
    ReleaseFn mRelease = nullptr;
};

} // namespace cellularity::app::render_worker
