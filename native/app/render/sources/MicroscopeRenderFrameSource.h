#pragma once

#include <memory>
#include <optional>

#include "app/render/NativeRenderAttachmentLease.h"
#include "app/render/NativeRenderFrameSource.h"

namespace cellularity::render {
class RenderSurfaceSessionBinding;
} // namespace cellularity::render

namespace cellularity::app::render_worker::sources {

class MicroscopeRenderFrameSource final : public NativeRenderFrameSource {
public:
    MicroscopeRenderFrameSource(
            std::shared_ptr<NativeRenderAttachmentLease> lease,
            WorkerBindingKey worker,
            std::weak_ptr<RebindWakeControl> rebindWakeControl);

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
    [[nodiscard]] static SourceSurfaceResult mapSurfaceStatus(
            LeaseMutationStatus status) noexcept;
    void destroyPreviewAttachments(
            NativeCellularityController& controller) noexcept;

    std::shared_ptr<NativeRenderAttachmentLease> mLease;
    WorkerBindingKey mWorker{};
    std::weak_ptr<RebindWakeControl> mRebindWakeControl;
    std::optional<RebindWakeRegistration> mRebindWakeRegistration;
    std::shared_ptr<const cellularity::render::RenderSurfaceSessionBinding>
            mBinding;
    std::optional<AttachmentKey> mAttachment;
    std::uint64_t mGenomePreviewAttachmentGeneration = 0;
    std::uint64_t mSubstratePreviewAttachmentGeneration = 0;
};

} // namespace cellularity::app::render_worker::sources
