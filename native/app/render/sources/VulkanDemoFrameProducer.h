#pragma once

#include <memory>

#include "app/render/NativeRenderFrameSource.h"

namespace cellularity::microscope {
struct MicroscopeDebugHudTelemetry;
} // namespace cellularity::microscope

namespace cellularity::render {
struct PreparedRenderFrame;

namespace abi {
class VulkanRenderSurfaceSessionAdapter;
} // namespace abi
} // namespace cellularity::render

namespace cellularity::app::render_worker::sources {

class VulkanDemoFrameProducer final : public NativeRenderFrameSource {
public:
    VulkanDemoFrameProducer();
    ~VulkanDemoFrameProducer() override;

    void setPerformanceHudEnabled(bool enabled) noexcept override;

    [[nodiscard]] SourceSurfaceResult bindBackend(
            std::shared_ptr<cellularity::render::abi::VulkanBackendSession>
                    session) noexcept override;
    [[nodiscard]] SourceSurfaceResult unbindBackend() noexcept override;
    [[nodiscard]] SourceSurfaceResult onSurfaceAttached(
            std::uint64_t platformGeneration,
            int width, int height) noexcept override;
    [[nodiscard]] SourceSurfaceResult onSurfaceResized(
            int width, int height) noexcept override;
    [[nodiscard]] SourceSurfaceResult onSurfaceDetached(
            SurfaceDetachReason reason) noexcept override;
    [[nodiscard]] SourceFrameResult renderFrame() noexcept override;

private:
    std::unique_ptr<cellularity::render::abi::
                    VulkanRenderSurfaceSessionAdapter>
            mAdapter;
    std::unique_ptr<cellularity::render::PreparedRenderFrame> mFrame;
    std::unique_ptr<cellularity::microscope::MicroscopeDebugHudTelemetry>
            mTelemetry;
    int mWidth = 0;
    int mHeight = 0;
    bool mSurfaceAttached = false;
    bool mPerformanceHudEnabled = false;
};

} // namespace cellularity::app::render_worker::sources
