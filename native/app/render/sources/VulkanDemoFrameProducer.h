#pragma once

#include <memory>
#include <optional>

#include "app/render/NativeRenderFrameSource.h"
#include "render/frame/RenderFrameTypes.h"

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

    // Replaces the fixed demo camera with one the caller supplies.
    //
    // The demo presentation exists so the fixture renders the same way everywhere
    // and is deliberately not interactive. A host that lets the user move the
    // camera needs to supply the viewport instead, and doing it here rather than in
    // the host keeps this producer the single owner of the frame and the surface
    // session, so the interactive path and the fixture path differ only in where
    // the presentation comes from.
    void setPresentationOverride(
            const cellularity::render::RenderPresentationState& presentation)
            noexcept;

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
    std::optional<cellularity::render::RenderPresentationState>
            mPresentationOverride;
};

} // namespace cellularity::app::render_worker::sources
