#include "app/microscope/SubstratePreviewCaptureEndpoint.h"

#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>

#include "app/NativeCellularityController.h"
#include "app/NativeRenderSession.h"

namespace cellularity::app::microscope {
namespace {

std::atomic<std::uint64_t> gNextSubstratePreviewEndpointGeneration{1};

std::uint64_t acquireEndpointGeneration() noexcept {
    std::uint64_t candidate = gNextSubstratePreviewEndpointGeneration.load(std::memory_order_relaxed);
    while (candidate != 0) {
        const std::uint64_t next = candidate == std::numeric_limits<std::uint64_t>::max() ? 0 : candidate + 1U;
        if (gNextSubstratePreviewEndpointGeneration.compare_exchange_weak(candidate, next, std::memory_order_relaxed, std::memory_order_relaxed)) {
            return candidate;
        }
    }
    return 0;
}

} // namespace

SubstratePreviewCaptureEndpoint::SubstratePreviewCaptureEndpoint(std::uint64_t generation)
        : mGeneration(generation) {
    if (generation == 0) {
        throw std::invalid_argument("Substrate Preview endpoint generation is zero");
    }
}

SubstratePreviewCaptureEndpoint::~SubstratePreviewCaptureEndpoint() {
    close();
}

std::uint64_t SubstratePreviewCaptureEndpoint::generation() const noexcept {
    return mGeneration;
}

std::uint64_t SubstratePreviewCaptureEndpoint::surfaceCreated() noexcept {
    std::uint64_t previousAttachmentGeneration = 0;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mLifecycleMutex);
        if (mClosing || mNextAttachmentGeneration == 0) {
            return 0;
        }
        generation = mNextAttachmentGeneration;
        mNextAttachmentGeneration = generation == std::numeric_limits<std::uint64_t>::max() ? 0 : generation + 1U;
        previousAttachmentGeneration = mActiveAttachmentGeneration;
        mActiveAttachmentGeneration = generation;
    }
    if (previousAttachmentGeneration != 0) {
        (void)mStore.cancelAttachment(previousAttachmentGeneration);
    }
    return generation;
}

void SubstratePreviewCaptureEndpoint::surfaceDestroyed(std::uint64_t attachmentGeneration) noexcept {
    if (attachmentGeneration == 0) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mLifecycleMutex);
        if (mActiveAttachmentGeneration == attachmentGeneration) {
            mActiveAttachmentGeneration = 0;
        }
    }
    (void)mStore.cancelAttachment(attachmentGeneration);
}

std::uint64_t SubstratePreviewCaptureEndpoint::activeAttachmentGeneration() const noexcept {
    std::lock_guard<std::mutex> lock(mLifecycleMutex);
    return mActiveAttachmentGeneration;
}

SubstratePreviewCaptureAdmission SubstratePreviewCaptureEndpoint::enqueue(std::uint64_t requestId, std::uint64_t substratePreviewToken, std::uint64_t attachmentGeneration) {
    std::lock_guard<std::mutex> lock(mLifecycleMutex);
    if (mClosing
            || attachmentGeneration == 0
            || attachmentGeneration != mActiveAttachmentGeneration) {
        return {SubstratePreviewCaptureAdmissionStatus::Closing, nullptr};
    }
    return mStore.enqueue(requestId, substratePreviewToken, mGeneration, attachmentGeneration);
}

void SubstratePreviewCaptureEndpoint::applyPendingOnRenderOwner(NativeCellularityController& controller) {
    const std::uint64_t attachmentGeneration = activeAttachmentGeneration();
    while (true) {
        const auto request = mStore.consumePendingOnRenderOwner(mGeneration, attachmentGeneration);
        if (request == nullptr) {
            break;
        }
        try {
            auto reservation = mStore.tryReserveOutput(kSubstratePreviewMaximumRgbaBytes);
            if (!reservation.has_value()) {
                (void)request->fail();
            } else {
                auto bytes = controller.captureSubstratePreviewRgba(request->substratePreviewToken);
                cellularity::app::preview::PreviewCaptureResultHeader header;
                header.requestId = request->requestId;
                header.status = cellularity::app::preview::PreviewCaptureStatus::Completed;
                header.width = static_cast<std::uint32_t>(NativeRenderSession::SUBSTRATE_PREVIEW_PIXELS);
                header.height = static_cast<std::uint32_t>(NativeRenderSession::SUBSTRATE_PREVIEW_PIXELS);
                header.rowStrideBytes = header.width * 4U;
                header.pixelFormat = cellularity::app::preview::PreviewCapturePixelFormat::Rgba8;
                header.origin = cellularity::app::preview::PreviewCaptureOrigin::TopLeft;
                header.alphaMode = cellularity::app::preview::PreviewCaptureAlphaMode::Opaque;
                header.payloadBytes = bytes.size();
                auto& state = static_cast<cellularity::app::preview::PreviewCaptureRequestState&>(*request);
                (void)state.complete(header, std::move(bytes), std::move(*reservation));
            }
        } catch (...) {
            (void)request->fail();
        }
        (void)mStore.release(request->requestId);
    }
}

void SubstratePreviewCaptureEndpoint::close() noexcept {
    {
        std::lock_guard<std::mutex> lock(mLifecycleMutex);
        if (mClosing) {
            return;
        }
        mClosing = true;
        mActiveAttachmentGeneration = 0;
    }
    mStore.close();
}

std::shared_ptr<SubstratePreviewCaptureEndpoint> createSubstratePreviewCaptureEndpoint() {
    const std::uint64_t generation = acquireEndpointGeneration();
    if (generation == 0) {
        throw std::overflow_error("Substrate Preview endpoint generation exhausted");
    }
    return std::make_shared<SubstratePreviewCaptureEndpoint>(generation);
}

} // namespace cellularity::app::microscope
