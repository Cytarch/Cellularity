#pragma once

#include <cstdint>
#include <memory>

#include "app/render/NativeRenderFrameSource.h"

class NativeCellularityController;

namespace cellularity::app {
class EnvironmentFieldPreviewEndpoint;
} // namespace cellularity::app

namespace cellularity::render {
class RenderSurfaceSessionBinding;
} // namespace cellularity::render

namespace cellularity::app::render_worker::sources {

class EnvironmentFieldPreviewRenderFrameSource final
        : public NativeRenderFrameSource {
public:
    EnvironmentFieldPreviewRenderFrameSource(
            std::shared_ptr<EnvironmentFieldPreviewEndpoint> endpoint,
            std::shared_ptr<NativeCellularityController> controller,
            std::uint64_t endpointGeneration);

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
    [[nodiscard]] bool endpointCurrent() const noexcept;

    std::shared_ptr<EnvironmentFieldPreviewEndpoint> mEndpoint;
    std::shared_ptr<NativeCellularityController> mController;
    std::shared_ptr<const cellularity::render::RenderSurfaceSessionBinding>
            mBinding;
    const std::uint64_t mEndpointGeneration;
    bool mSurfaceAttached = false;
};

} // namespace cellularity::app::render_worker::sources
