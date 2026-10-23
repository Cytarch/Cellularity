#include "app/microscope/SubstratePreviewCaptureRequestStore.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace cellularity::app::microscope {
namespace {

constexpr std::size_t kNoSlot =
        std::numeric_limits<std::size_t>::max();

void increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

} // namespace

SubstratePreviewCaptureRequestState::SubstratePreviewCaptureRequestState(
        std::uint64_t requestIdValue,
        std::uint64_t substratePreviewTokenValue,
        std::uint64_t controllerGenerationValue,
        std::uint64_t attachmentGenerationValue,
        std::uint64_t admissionSequenceValue) noexcept
        : PreviewCaptureRequestState(
                  requestIdValue,
                  cellularity::app::preview::PreviewCaptureRequestKind::Substrate,
                  controllerGenerationValue,
                  attachmentGenerationValue),
          substratePreviewToken(substratePreviewTokenValue),
          admissionSequence(admissionSequenceValue) {
}

SubstratePreviewCaptureRequestStore::SubstratePreviewCaptureRequestStore(
        std::size_t slotCapacity,
        std::size_t outputByteBudget)
        : mSlotCapacity(slotCapacity),
          mOutputBudget(outputByteBudget) {
    if (slotCapacity == 0
            || slotCapacity > kSubstratePreviewCaptureSlotCapacity) {
        throw std::invalid_argument(
                "Substrate Preview capture slot capacity is outside fixed storage");
    }
}

SubstratePreviewCaptureRequestStore::~SubstratePreviewCaptureRequestStore() {
    close();
}

SubstratePreviewCaptureAdmission
SubstratePreviewCaptureRequestStore::enqueue(
        std::uint64_t requestId,
        std::uint64_t substratePreviewToken,
        std::uint64_t controllerGeneration,
        std::uint64_t attachmentGeneration) {
    if (requestId == 0
            || substratePreviewToken == 0
            || controllerGeneration == 0
            || attachmentGeneration == 0) {
        return {
                SubstratePreviewCaptureAdmissionStatus::InvalidArgument,
                nullptr,
        };
    }

    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosing) {
        return {SubstratePreviewCaptureAdmissionStatus::Closing, nullptr};
    }
    if (findRequestLocked(requestId) != kNoSlot) {
        return {
                SubstratePreviewCaptureAdmissionStatus::DuplicateRequestId,
                nullptr,
        };
    }
    pruneTerminalLocked();
    if (mOccupied == mSlotCapacity) {
        increment(mRejectedBusy);
        return {SubstratePreviewCaptureAdmissionStatus::Busy, nullptr};
    }
    if (mNextAdmissionSequence == 0) {
        return {
                SubstratePreviewCaptureAdmissionStatus::SequenceExhausted,
                nullptr,
        };
    }

    const std::uint64_t admissionSequence = mNextAdmissionSequence;
    std::shared_ptr<SubstratePreviewCaptureRequestState> request;
    try {
        request = std::make_shared<SubstratePreviewCaptureRequestState>(
                requestId,
                substratePreviewToken,
                controllerGeneration,
                attachmentGeneration,
                admissionSequence);
    } catch (const std::bad_alloc&) {
        return {
                SubstratePreviewCaptureAdmissionStatus::InternalFailure,
                nullptr,
        };
    }

    std::size_t freeSlot = kNoSlot;
    for (std::size_t slot = 0; slot < mSlotCapacity; ++slot) {
        if (mSlots[slot] == nullptr) {
            freeSlot = slot;
            break;
        }
    }
    if (freeSlot == kNoSlot) {
        increment(mRejectedBusy);
        return {SubstratePreviewCaptureAdmissionStatus::Busy, nullptr};
    }
    mSlots[freeSlot] = request;
    ++mOccupied;
    if (mOccupied > mHighWater) {
        mHighWater = mOccupied;
    }
    mNextAdmissionSequence =
            admissionSequence == std::numeric_limits<std::uint64_t>::max()
            ? 0
            : admissionSequence + 1U;
    increment(mAccepted);
    return {
            SubstratePreviewCaptureAdmissionStatus::Accepted,
            std::move(request),
    };
}

std::shared_ptr<SubstratePreviewCaptureRequestState>
SubstratePreviewCaptureRequestStore::consumePendingOnRenderOwner(
        std::uint64_t activeControllerGeneration,
        std::uint64_t activeAttachmentGeneration) noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    pruneTerminalLocked();
    for (;;) {
        std::size_t candidateSlot = kNoSlot;
        std::uint64_t candidateSequence =
                std::numeric_limits<std::uint64_t>::max();
        for (std::size_t slot = 0; slot < mSlotCapacity; ++slot) {
            const auto& request = mSlots[slot];
            if (request != nullptr
                    && request->phase()
                            == SubstratePreviewCaptureRequestPhase::Pending
                    && request->admissionSequence < candidateSequence) {
                candidateSlot = slot;
                candidateSequence = request->admissionSequence;
            }
        }
        if (candidateSlot == kNoSlot) {
            return nullptr;
        }

        const auto request = mSlots[candidateSlot];
        if (activeControllerGeneration == 0
                || activeAttachmentGeneration == 0
                || request->controllerGeneration
                        != activeControllerGeneration
                || request->attachmentGeneration
                        != activeAttachmentGeneration) {
            if (request->cancel()) {
                increment(mCancelled);
            }
            mSlots[candidateSlot].reset();
            --mOccupied;
            continue;
        }
        if (request->tryMarkRunning()) {
            return request;
        }
    }
}

bool SubstratePreviewCaptureRequestStore::cancel(
        std::uint64_t requestId) noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    const std::size_t slot = findRequestLocked(requestId);
    if (slot == kNoSlot) {
        return false;
    }
    const auto request = mSlots[slot];
    const bool changed = request->cancel();
    if (changed) {
        increment(mCancelled);
    }
    if (isTerminal(request->phase())) {
        mSlots[slot].reset();
        --mOccupied;
    }
    return changed;
}

std::optional<genome_preview::RetainedPayloadBudget::Reservation>
SubstratePreviewCaptureRequestStore::tryReserveOutput(
        std::size_t bytes) noexcept {
    return mOutputBudget.tryReserve(bytes);
}

std::size_t SubstratePreviewCaptureRequestStore::cancelAttachment(
        std::uint64_t attachmentGeneration) noexcept {
    if (attachmentGeneration == 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    std::size_t cancelled = 0;
    for (std::size_t slot = 0; slot < mSlotCapacity; ++slot) {
        const auto request = mSlots[slot];
        if (request == nullptr
                || request->attachmentGeneration != attachmentGeneration) {
            continue;
        }
        if (request->cancel()) {
            ++cancelled;
            increment(mCancelled);
        }
        if (isTerminal(request->phase())) {
            mSlots[slot].reset();
            --mOccupied;
        }
    }
    return cancelled;
}

bool SubstratePreviewCaptureRequestStore::release(
        std::uint64_t requestId) noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    const std::size_t slot = findRequestLocked(requestId);
    if (slot == kNoSlot || !isTerminal(mSlots[slot]->phase())) {
        return false;
    }
    mSlots[slot].reset();
    --mOccupied;
    return true;
}

void SubstratePreviewCaptureRequestStore::close() noexcept {
    std::array<std::shared_ptr<SubstratePreviewCaptureRequestState>,
            kSubstratePreviewCaptureSlotCapacity>
            requests;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mClosing) {
            return;
        }
        mClosing = true;
        requests = std::move(mSlots);
        mSlots = {};
        mOccupied = 0;
    }
    for (const auto& request : requests) {
        if (request != nullptr && request->cancel()) {
            std::lock_guard<std::mutex> lock(mMutex);
            increment(mCancelled);
        }
    }
}

SubstratePreviewCaptureStoreSnapshot
SubstratePreviewCaptureRequestStore::snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return {
            mSlotCapacity,
            mOccupied,
            mHighWater,
            mAccepted,
            mRejectedBusy,
            mCancelled,
            mClosing,
            mOutputBudget.snapshot(),
    };
}

std::size_t SubstratePreviewCaptureRequestStore::findRequestLocked(
        std::uint64_t requestId) const noexcept {
    for (std::size_t slot = 0; slot < mSlotCapacity; ++slot) {
        if (mSlots[slot] != nullptr
                && mSlots[slot]->requestId == requestId) {
            return slot;
        }
    }
    return kNoSlot;
}

void SubstratePreviewCaptureRequestStore::pruneTerminalLocked() noexcept {
    for (std::size_t slot = 0; slot < mSlotCapacity; ++slot) {
        if (mSlots[slot] != nullptr && isTerminal(mSlots[slot]->phase())) {
            mSlots[slot].reset();
            --mOccupied;
        }
    }
}

} // namespace cellularity::app::microscope
