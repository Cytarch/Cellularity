#include "app/render/sources/EnvironmentFieldPreviewRenderFrameSource.h"

#include <utility>

#include "app/EnvironmentFieldPreviewEndpoint.h"
#include "app/NativeCellularityController.h"
#include "render/backend/RenderSurfaceSession.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionBinding.h"

namespace cellularity::app::render_worker::sources {

EnvironmentFieldPreviewRenderFrameSource::
        EnvironmentFieldPreviewRenderFrameSource(
                std::shared_ptr<EnvironmentFieldPreviewEndpoint> endpoint,
                std::shared_ptr<NativeCellularityController> controller,
                std::uint64_t endpointGeneration)
        : mEndpoint(std::move(endpoint)),
          mController(std::move(controller)),
          mEndpointGeneration(endpointGeneration) {
}

SourceSurfaceResult EnvironmentFieldPreviewRenderFrameSource::bindBackend(
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

SourceSurfaceResult EnvironmentFieldPreviewRenderFrameSource::
        unbindBackend() noexcept {
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

SourceSurfaceResult EnvironmentFieldPreviewRenderFrameSource::
        onSurfaceAttached(std::uint64_t, int width, int height) noexcept {
    try {
        if (!endpointCurrent() || mController == nullptr
                || width <= 0 || height <= 0) {
            return SourceSurfaceResult::Failed;
        }
        mController->setViewportSurfaceSize(width, height);
        mSurfaceAttached = true;
        return SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult EnvironmentFieldPreviewRenderFrameSource::
        onSurfaceResized(int width, int height) noexcept {
    try {
        if (!endpointCurrent() || mController == nullptr
                || !mSurfaceAttached || width <= 0 || height <= 0) {
            return SourceSurfaceResult::Failed;
        }
        mController->setViewportSurfaceSize(width, height);
        return SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult EnvironmentFieldPreviewRenderFrameSource::
        onSurfaceDetached(SurfaceDetachReason) noexcept {
    mSurfaceAttached = false;
    return SourceSurfaceResult::Applied;
}

SourceFrameResult EnvironmentFieldPreviewRenderFrameSource::
        renderFrame() noexcept {
    try {
        if (!endpointCurrent() || mController == nullptr
                || !mSurfaceAttached) {
            return {SourceFrameStatus::Closing, {}};
        }
        return {SourceFrameStatus::Completed,
                mController->drawEnvironmentFieldPreview()};
    } catch (...) {
        return {SourceFrameStatus::Failed, {}};
    }
}

bool EnvironmentFieldPreviewRenderFrameSource::endpointCurrent()
        const noexcept {
    return mEndpoint != nullptr && mEndpointGeneration != 0
            && mEndpoint->generation() == mEndpointGeneration;
}

} // namespace cellularity::app::render_worker::sources
