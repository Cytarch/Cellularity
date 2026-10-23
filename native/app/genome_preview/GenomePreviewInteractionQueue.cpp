#include "app/genome_preview/GenomePreviewInteractionQueue.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cellularity::app::genome_preview {
namespace {

void increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

bool finite(float value) noexcept {
    return std::isfinite(value);
}

} // namespace

bool GenomePreviewFrameRequestFlag::request() noexcept {
    State expected = State::Idle;
    return mState.compare_exchange_strong(
            expected,
            State::Requested,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
}

bool GenomePreviewFrameRequestFlag::consumePendingOnRenderOwner() noexcept {
    State expected = State::Requested;
    return mState.compare_exchange_strong(
            expected,
            State::Idle,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
}

void GenomePreviewFrameRequestFlag::close() noexcept {
    mState.store(State::Closing, std::memory_order_release);
}

bool GenomePreviewFrameRequestFlag::pending() const noexcept {
    return mState.load(std::memory_order_acquire) == State::Requested;
}

GenomePreviewInteractionQueue::GenomePreviewInteractionQueue(
        std::size_t capacity)
        : mCapacity(capacity) {
    if (capacity == 0 || capacity > kGenomePreviewInteractionCapacity) {
        throw std::invalid_argument(
                "Genome Preview interaction capacity is outside its fixed storage");
    }
}

GenomePreviewInteractionQueue::~GenomePreviewInteractionQueue() {
    close();
}

GenomePreviewInteractionEnqueueResult
GenomePreviewInteractionQueue::enqueue(
        const GenomePreviewInteractionInput& input,
        GenomePreviewStateFence fence) {
    if (!isValid(input)) {
        std::lock_guard<std::mutex> lock(mMutex);
        increment(mRejectedInvalid);
        return {GenomePreviewInteractionEnqueueStatus::InvalidArgument, 0};
    }

    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosing) {
        return {GenomePreviewInteractionEnqueueStatus::Closing, 0};
    }
    if (mDepth == mCapacity) {
        increment(mRejectedFull);
        return {GenomePreviewInteractionEnqueueStatus::QueueFull, 0};
    }
    if (mNextSequence == 0) {
        increment(mRejectedInvalid);
        return {GenomePreviewInteractionEnqueueStatus::InvalidArgument, 0};
    }

    const std::uint64_t sequence = mNextSequence;
    mNextSequence = sequence == std::numeric_limits<std::uint64_t>::max()
            ? 0
            : sequence + 1U;
    const std::size_t tail = (mHead + mDepth) % mCapacity;
    mSlots[tail] = {sequence, input, std::move(fence)};
    ++mDepth;
    if (mDepth > mHighWater) {
        mHighWater = mDepth;
    }
    increment(mAccepted);
    return {GenomePreviewInteractionEnqueueStatus::Accepted, sequence};
}

std::optional<GenomePreviewInteractionCommand>
GenomePreviewInteractionQueue::consumePendingOnRenderOwner(
        std::uint64_t activeControllerGeneration,
        std::uint64_t activeAttachmentGeneration) {
    std::lock_guard<std::mutex> lock(mMutex);
    while (mDepth != 0) {
        GenomePreviewInteractionCommand command = popFrontLocked();
        const bool staleController = activeControllerGeneration == 0
                || command.input.controllerGeneration
                        != activeControllerGeneration;
        const bool staleAttachment =
                command.input.attachmentGeneration != 0
                && command.input.attachmentGeneration
                        != activeAttachmentGeneration;
        if (staleController || staleAttachment) {
            increment(mStaleGenerationDiscarded);
            continue;
        }
        increment(mConsumed);
        return command;
    }
    return std::nullopt;
}

void GenomePreviewInteractionQueue::close() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosing) {
        return;
    }
    mClosing = true;
    while (mDepth != 0) {
        (void)popFrontLocked();
        increment(mCancelledOnClose);
    }
}

GenomePreviewInteractionQueueSnapshot
GenomePreviewInteractionQueue::snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return {
            mCapacity,
            mDepth,
            mHighWater,
            mAccepted,
            mConsumed,
            mRejectedFull,
            mRejectedInvalid,
            mStaleGenerationDiscarded,
            mCancelledOnClose,
            mClosing,
    };
}

bool GenomePreviewInteractionQueue::isValid(
        const GenomePreviewInteractionInput& input) noexcept {
    if (input.completionToken == 0 || input.controllerGeneration == 0) {
        return false;
    }
    return std::visit(
            [](const auto& value) noexcept {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value,
                                      GenomePreviewRestoreViewport>) {
                    return finite(value.cameraX)
                            && finite(value.cameraY)
                            && finite(value.zoom)
                            && value.zoom > 0.0F;
                } else if constexpr (std::is_same_v<Value,
                                             GenomePreviewMoveViewport>) {
                    return finite(value.deltaX) && finite(value.deltaY);
                } else if constexpr (std::is_same_v<Value,
                                             GenomePreviewZoomViewport>) {
                    return finite(value.scale)
                            && finite(value.centerX)
                            && finite(value.centerY)
                            && value.scale > 0.0F;
                } else if constexpr (std::is_same_v<Value,
                                             GenomePreviewSelectGene>) {
                    return finite(value.screenX) && finite(value.screenY);
                } else {
                    return true;
                }
            },
            input.value);
}

GenomePreviewInteractionCommand
GenomePreviewInteractionQueue::popFrontLocked() noexcept {
    GenomePreviewInteractionCommand command = std::move(mSlots[mHead]);
    mSlots[mHead] = {};
    mHead = (mHead + 1U) % mCapacity;
    --mDepth;
    return command;
}

} // namespace cellularity::app::genome_preview
