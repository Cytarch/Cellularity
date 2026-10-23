#include "app/render/sources/GenomePreviewRenderFrameSource.h"

#include <utility>

#include "app/NativeCellularityController.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"
#include "render/backend/RenderSurfaceSession.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionBinding.h"

namespace cellularity::app::render_worker::sources {

GenomePreviewRenderFrameSource::GenomePreviewRenderFrameSource(
        std::shared_ptr<genome_preview::GenomePreviewEndpoint> endpoint,
        std::shared_ptr<NativeCellularityController> controller,
        std::uint64_t endpointGeneration)
        : mEndpoint(std::move(endpoint)),
          mController(std::move(controller)),
          mEndpointGeneration(endpointGeneration) {
}

SourceSurfaceResult GenomePreviewRenderFrameSource::bindBackend(
        std::shared_ptr<cellularity::render::abi::VulkanBackendSession>
                session) noexcept {
    try {
        if (!endpointCurrent() || mController == nullptr || session == nullptr) {
            return SourceSurfaceResult::Failed;
        }
        auto binding = std::make_shared<const cellularity::render::abi::
                        VulkanRenderSurfaceSessionBinding>(std::move(session));
        auto adapter = binding->createControllerAdapter();
        if (adapter == nullptr) {
            return SourceSurfaceResult::Failed;
        }
        mController->installWorkerRenderSurfaceSession(std::move(adapter));
        mBinding = std::move(binding);
        return SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult GenomePreviewRenderFrameSource::unbindBackend() noexcept {
    try {
        if (mBinding == nullptr) {
            return SourceSurfaceResult::Applied;
        }
        const bool removed = mController != nullptr
                && mController->uninstallRenderSurfaceSession(
                        mBinding->kind(), mBinding->identity());
        mBinding.reset();
        return removed
                ? SourceSurfaceResult::Applied
                : SourceSurfaceResult::Failed;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult GenomePreviewRenderFrameSource::onSurfaceAttached(
        std::uint64_t,
        int width, int height) noexcept {
    try {
        if (!endpointCurrent() || mController == nullptr
                || width <= 0 || height <= 0) {
            return SourceSurfaceResult::Failed;
        }
        // The worker already attached the physical Vulkan surface before this
        // callback. Genome Preview's interaction viewport is intentionally
        // independent from that backing surface: its retained view update owns
        // the visible dimensions used by projection and input mapping.
        mAttachmentGeneration = mEndpoint->surfaceCreated();
        return mAttachmentGeneration == 0
                ? SourceSurfaceResult::Closing
                : SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult GenomePreviewRenderFrameSource::onSurfaceResized(
        int width, int height) noexcept {
    try {
        if (!endpointCurrent() || mController == nullptr
                || mAttachmentGeneration == 0
                || width <= 0 || height <= 0) {
            return SourceSurfaceResult::Failed;
        }
        // Do not overwrite the retained logical viewport with the physical
        // backing dimensions when Android recreates or rotates the Surface.
        return SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult GenomePreviewRenderFrameSource::onSurfaceDetached(
        SurfaceDetachReason) noexcept {
    try {
        if (mAttachmentGeneration != 0 && mEndpoint != nullptr) {
            mEndpoint->surfaceDestroyed(mAttachmentGeneration);
            mAttachmentGeneration = 0;
        }
        return SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceFrameResult GenomePreviewRenderFrameSource::renderFrame() noexcept {
    try {
        if (!endpointCurrent() || mController == nullptr
                || mAttachmentGeneration == 0) {
            return {SourceFrameStatus::Closing, {}};
        }
        return {SourceFrameStatus::Completed,
                mEndpoint->draw(*mController)};
    } catch (...) {
        return {SourceFrameStatus::Failed, {}};
    }
}

bool GenomePreviewRenderFrameSource::endpointCurrent() const noexcept {
    return mEndpoint != nullptr && mEndpointGeneration != 0
            && mEndpoint->generation() == mEndpointGeneration;
}

} // namespace cellularity::app::render_worker::sources
