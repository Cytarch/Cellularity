#include "app/render/sources/MicroscopeRenderFrameSource.h"

#include <utility>

#include "app/NativeCellularityController.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionBinding.h"

namespace cellularity::app::render_worker::sources {

MicroscopeRenderFrameSource::MicroscopeRenderFrameSource(
        std::shared_ptr<NativeRenderAttachmentLease> lease,
        WorkerBindingKey worker,
        std::weak_ptr<RebindWakeControl> rebindWakeControl)
        : mLease(std::move(lease)),
          mWorker(worker),
          mRebindWakeControl(std::move(rebindWakeControl)) {
}

SourceSurfaceResult MicroscopeRenderFrameSource::bindBackend(
        std::shared_ptr<cellularity::render::abi::VulkanBackendSession>
                session) noexcept {
    try {
        if (mLease == nullptr || !mWorker.valid() || session == nullptr) {
            return SourceSurfaceResult::Failed;
        }
        auto binding = std::make_shared<const cellularity::render::abi::
                        VulkanRenderSurfaceSessionBinding>(std::move(session));
        const LeaseMutationStatus status =
                mLease->installBackendBinding(mWorker, binding);
        if (status != LeaseMutationStatus::Applied) {
            return mapSurfaceStatus(status);
        }
        auto registration = mLease->registerRebindWake(
                mWorker, mRebindWakeControl);
        if (registration.status != LeaseMutationStatus::Applied
                || !registration.value.has_value()) {
            (void)mLease->removeBackendBinding(mWorker);
            return mapSurfaceStatus(registration.status);
        }
        mBinding = std::move(binding);
        mRebindWakeRegistration = std::move(*registration.value);
        return SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult MicroscopeRenderFrameSource::unbindBackend() noexcept {
    try {
        if (mLease == nullptr || !mWorker.valid()) {
            return SourceSurfaceResult::Failed;
        }
        const LeaseMutationStatus status =
                mLease->removeBackendBinding(mWorker);
        if (status == LeaseMutationStatus::Applied) {
            mRebindWakeRegistration.reset();
            mBinding.reset();
        }
        return mapSurfaceStatus(status);
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult MicroscopeRenderFrameSource::onSurfaceAttached(
        std::uint64_t platformGeneration,
        int width, int height) noexcept {
    try {
        if (mLease == nullptr || platformGeneration == 0
                || width <= 0 || height <= 0) {
            return SourceSurfaceResult::Failed;
        }
        auto admission = mLease->tryAcquireSurfaceOperation(mWorker);
        if (admission.status != LeaseMutationStatus::Applied
                || !admission.guard.has_value()) {
            return mapSurfaceStatus(admission.status);
        }
        auto activation = mLease->activateSurface(
                mWorker, platformGeneration, width, height);
        if (activation.status != LeaseMutationStatus::Applied
                || !activation.value.has_value()) {
            return mapSurfaceStatus(activation.status);
        }
        NativeCellularityController& controller =
                admission.guard->controller();
        controller.setViewportSurfaceSize(width, height);
        controller.invalidateMicroscopeFrameData();
        mGenomePreviewAttachmentGeneration =
                controller.genomePreviewSurfaceCreated();
        mSubstratePreviewAttachmentGeneration =
                controller.substratePreviewSurfaceCreated();
        if (mGenomePreviewAttachmentGeneration == 0
                || mSubstratePreviewAttachmentGeneration == 0) {
            destroyPreviewAttachments(controller);
            (void)mLease->deactivateSurface(*activation.value);
            return SourceSurfaceResult::Failed;
        }
        mAttachment = *activation.value;
        return SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult MicroscopeRenderFrameSource::onSurfaceResized(
        int width, int height) noexcept {
    try {
        if (mLease == nullptr || !mAttachment.has_value()
                || width <= 0 || height <= 0) {
            return SourceSurfaceResult::Failed;
        }
        auto admission = mLease->tryAcquireSurfaceOperation(mWorker);
        if (admission.status != LeaseMutationStatus::Applied
                || !admission.guard.has_value()) {
            return mapSurfaceStatus(admission.status);
        }
        const LeaseMutationStatus status = mLease->updateSurfaceDimensions(
                *mAttachment, width, height);
        if (status != LeaseMutationStatus::Applied) {
            return mapSurfaceStatus(status);
        }
        NativeCellularityController& controller =
                admission.guard->controller();
        controller.setViewportSurfaceSize(width, height);
        controller.invalidateMicroscopeFrameData();
        return SourceSurfaceResult::Applied;
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceSurfaceResult MicroscopeRenderFrameSource::onSurfaceDetached(
        SurfaceDetachReason) noexcept {
    try {
        if (mLease == nullptr) {
            return SourceSurfaceResult::Failed;
        }
        if (!mAttachment.has_value()) {
            return SourceSurfaceResult::Applied;
        }
        auto admission = mLease->tryAcquireSurfaceOperation(mWorker);
        if (admission.status != LeaseMutationStatus::Applied
                || !admission.guard.has_value()) {
            return mapSurfaceStatus(admission.status);
        }
        destroyPreviewAttachments(admission.guard->controller());
        const LeaseMutationStatus status =
                mLease->deactivateSurface(*mAttachment);
        if (status == LeaseMutationStatus::Applied
                || status == LeaseMutationStatus::StaleSurface) {
            mAttachment.reset();
            return SourceSurfaceResult::Applied;
        }
        return mapSurfaceStatus(status);
    } catch (...) {
        return SourceSurfaceResult::Failed;
    }
}

SourceFrameResult MicroscopeRenderFrameSource::renderFrame() noexcept {
    try {
        if (mLease == nullptr || !mAttachment.has_value()) {
            return {SourceFrameStatus::DeferredRebind, {}};
        }
        auto acquired = mLease->tryAcquireFrame(*mAttachment);
        if (acquired.status == FrameAcquireStatus::StaleKey) {
            const auto latest = mLease->snapshotAttachment(mWorker);
            if (latest.status == AttachmentLookupStatus::Ready
                    && latest.key.has_value()) {
                mAttachment = *latest.key;
                acquired = mLease->tryAcquireFrame(*mAttachment);
            }
        }
        if (acquired.status == FrameAcquireStatus::Acquired
                && acquired.guard.has_value()) {
            return {SourceFrameStatus::Completed,
                    acquired.guard->controller()
                            .drawMicroscopeForCurrentMode()};
        }
        switch (acquired.status) {
            case FrameAcquireStatus::Rebinding:
            case FrameAcquireStatus::StaleKey:
                return {SourceFrameStatus::DeferredRebind, {}};
            case FrameAcquireStatus::WorkerClosing:
            case FrameAcquireStatus::AttachmentClosing:
                return {SourceFrameStatus::Closing, {}};
            case FrameAcquireStatus::InactiveSurface:
                return {SourceFrameStatus::Completed,
                        cellularity::render::makeRenderFrameResult(
                                cellularity::render::RenderFrameOutcome::
                                        SkippedNotReady)};
            case FrameAcquireStatus::Acquired:
                break;
        }
        return {SourceFrameStatus::Failed, {}};
    } catch (...) {
        return {SourceFrameStatus::Failed, {}};
    }
}

SourceSurfaceResult MicroscopeRenderFrameSource::mapSurfaceStatus(
        LeaseMutationStatus status) noexcept {
    switch (status) {
        case LeaseMutationStatus::Applied:
        case LeaseMutationStatus::AlreadyAuthorized:
            return SourceSurfaceResult::Applied;
        case LeaseMutationStatus::DeferredRebind:
            return SourceSurfaceResult::DeferredRebind;
        case LeaseMutationStatus::WorkerClosing:
        case LeaseMutationStatus::AttachmentClosing:
            return SourceSurfaceResult::Closing;
        case LeaseMutationStatus::StaleWorker:
        case LeaseMutationStatus::StaleSurface:
        case LeaseMutationStatus::WorkerBusy:
        case LeaseMutationStatus::NotAuthorized:
        case LeaseMutationStatus::InvalidArgument:
        case LeaseMutationStatus::GenerationExhausted:
            return SourceSurfaceResult::Failed;
    }
    return SourceSurfaceResult::Failed;
}

void MicroscopeRenderFrameSource::destroyPreviewAttachments(
        NativeCellularityController& controller) noexcept {
    if (mGenomePreviewAttachmentGeneration != 0) {
        controller.genomePreviewSurfaceDestroyed(
                mGenomePreviewAttachmentGeneration);
        mGenomePreviewAttachmentGeneration = 0;
    }
    if (mSubstratePreviewAttachmentGeneration != 0) {
        controller.substratePreviewSurfaceDestroyed(
                mSubstratePreviewAttachmentGeneration);
        mSubstratePreviewAttachmentGeneration = 0;
    }
}

} // namespace cellularity::app::render_worker::sources
