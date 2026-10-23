#pragma once

#include <cstdint>
#include <memory>

#include "render/backend/RenderFrameResult.h"

namespace cellularity::render::abi {
class VulkanBackendSession;
} // namespace cellularity::render::abi

namespace cellularity::app::render_worker {

enum class SurfaceDetachReason : std::uint8_t {
    Ordinary = 0,
    Pause = 1,
    SurfaceLost = 2,
    Stop = 3,
};

enum class SourceSurfaceResult : std::uint8_t {
    Applied = 0,
    DeferredRebind = 1,
    Closing = 2,
    Failed = 3,
};

enum class SourceFrameStatus : std::uint8_t {
    Completed = 0,
    DeferredRebind = 1,
    Closing = 2,
    Failed = 3,
};

struct SourceFrameResult final {
    SourceFrameStatus status = SourceFrameStatus::Failed;
    cellularity::render::RenderFrameResult frame{};
};

class NativeRenderFrameSource {
public:
    virtual ~NativeRenderFrameSource() = default;

    virtual void setPerformanceHudEnabled(bool) noexcept {}

    // Called only by the worker owner thread. It installs/removes the cheap
    // main-DSO controller adapter; neither call crosses the Vulkan ABI.
    virtual SourceSurfaceResult bindBackend(
            std::shared_ptr<cellularity::render::abi::VulkanBackendSession>
                    session) noexcept = 0;
    virtual SourceSurfaceResult unbindBackend() noexcept = 0;
    virtual SourceSurfaceResult onSurfaceAttached(
            std::uint64_t platformGeneration,
            int width, int height) noexcept = 0;
    virtual SourceSurfaceResult onSurfaceResized(
            int width, int height) noexcept = 0;
    virtual SourceSurfaceResult onSurfaceDetached(
            SurfaceDetachReason reason) noexcept = 0;
    virtual SourceFrameResult renderFrame() noexcept = 0;
};

} // namespace cellularity::app::render_worker
