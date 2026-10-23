#include "app/genome_preview/GenomePreviewCaptureRequestStore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "io/DecodeError.h"
#include "io/NativeByteStream.h"

namespace cellularity::app::genome_preview {
namespace {

constexpr std::size_t kNoSlot =
        std::numeric_limits<std::size_t>::max();

void increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

cellularity::GenomeData decodeRetainedGenomeDraft(
        const std::vector<std::uint8_t>& bytes) {
    ByteReader reader(bytes.data(), bytes.size());
    return readGenomeDraft(reader);
}

} // namespace

GenomePreviewCaptureRequestState::GenomePreviewCaptureRequestState(
        std::uint64_t requestIdValue,
        std::uint64_t controllerGenerationValue,
        std::uint64_t attachmentGenerationValue,
        std::uint64_t admissionSequenceValue,
        double maximumSimulationTimeValue,
        const std::uint8_t* bytes,
        std::size_t byteCount,
        RetainedPayloadBudget::Reservation reservationValue)
        : PreviewCaptureRequestState(
                  requestIdValue,
                  cellularity::app::preview::PreviewCaptureRequestKind::Genome,
                  controllerGenerationValue,
                  attachmentGenerationValue),
          admissionSequence(admissionSequenceValue),
          maximumSimulationTime(maximumSimulationTimeValue),
          genomeDraftBytes(bytes, bytes + byteCount),
          decodedGenome(decodeRetainedGenomeDraft(genomeDraftBytes)),
          mInputReservation(std::move(reservationValue)) {
}

GenomePreviewCaptureRequestStore::GenomePreviewCaptureRequestStore(
        std::size_t slotCapacity,
        std::size_t inputByteBudget,
        std::size_t outputByteBudget)
        : mSlotCapacity(slotCapacity),
          mInputBudget(inputByteBudget),
          mOutputBudget(outputByteBudget) {
    if (slotCapacity == 0
            || slotCapacity > kGenomePreviewCaptureSlotCapacity) {
        throw std::invalid_argument(
                "Genome Preview capture slot capacity is outside fixed storage");
    }
}

GenomePreviewCaptureRequestStore::~GenomePreviewCaptureRequestStore() {
    close();
}

GenomePreviewCaptureAdmission
GenomePreviewCaptureRequestStore::enqueue(
        std::uint64_t requestId,
        std::uint64_t controllerGeneration,
        std::uint64_t attachmentGeneration,
        const std::uint8_t* genomeDraftBytes,
        std::size_t genomeDraftByteCount,
        double maximumSimulationTime) {
    if (requestId == 0
            || controllerGeneration == 0
            || attachmentGeneration == 0
            || genomeDraftBytes == nullptr
            || genomeDraftByteCount == 0
            || genomeDraftByteCount
                    > cellularity::kGenomeDraftMaximumWireBytes
            || !cellularity::hasGenomeDraftWireHeader(
                    genomeDraftBytes, genomeDraftByteCount)
            || !std::isfinite(maximumSimulationTime)
            || maximumSimulationTime < 0.0) {
        return {GenomePreviewCaptureAdmissionStatus::InvalidArgument, nullptr};
    }

    std::lock_guard<std::mutex> lock(mMutex);
    if (mClosing) {
        return {GenomePreviewCaptureAdmissionStatus::Closing, nullptr};
    }
    if (findRequestLocked(requestId) != kNoSlot) {
        return {
                GenomePreviewCaptureAdmissionStatus::DuplicateRequestId,
                nullptr,
        };
    }
    pruneTerminalLocked();
    if (mOccupied == mSlotCapacity) {
        increment(mRejectedBusy);
        return {GenomePreviewCaptureAdmissionStatus::Busy, nullptr};
    }
    if (mNextAdmissionSequence == 0) {
        return {
                GenomePreviewCaptureAdmissionStatus::SequenceExhausted,
                nullptr,
        };
    }

    std::optional<RetainedPayloadBudget::Reservation> reservation =
            mInputBudget.tryReserve(genomeDraftByteCount);
    if (!reservation.has_value()) {
        return {
                GenomePreviewCaptureAdmissionStatus::BudgetExceeded,
                nullptr,
        };
    }

    const std::uint64_t admissionSequence = mNextAdmissionSequence;
    std::shared_ptr<GenomePreviewCaptureRequestState> request;
    try {
        request = std::make_shared<GenomePreviewCaptureRequestState>(
                requestId,
                controllerGeneration,
                attachmentGeneration,
                admissionSequence,
                maximumSimulationTime,
                genomeDraftBytes,
                genomeDraftByteCount,
                std::move(*reservation));
    } catch (const std::bad_alloc&) {
        return {
                GenomePreviewCaptureAdmissionStatus::InternalFailure,
                nullptr,
        };
    } catch (const DecodeError&) {
        return {
                GenomePreviewCaptureAdmissionStatus::InvalidArgument,
                nullptr,
        };
    } catch (const std::exception&) {
        return {
                GenomePreviewCaptureAdmissionStatus::InvalidArgument,
                nullptr,
        };
    }

    const auto freeSlot = std::find(mSlots.begin(),
            mSlots.begin() + static_cast<std::ptrdiff_t>(mSlotCapacity),
            nullptr);
    if (freeSlot == mSlots.begin() + static_cast<std::ptrdiff_t>(mSlotCapacity)) {
        increment(mRejectedBusy);
        return {GenomePreviewCaptureAdmissionStatus::Busy, nullptr};
    }
    *freeSlot = request;
    ++mOccupied;
    if (mOccupied > mHighWater) {
        mHighWater = mOccupied;
    }
    mNextAdmissionSequence =
            admissionSequence == std::numeric_limits<std::uint64_t>::max()
            ? 0
            : admissionSequence + 1U;
    increment(mAccepted);
    return {GenomePreviewCaptureAdmissionStatus::Accepted, std::move(request)};
}

std::shared_ptr<GenomePreviewCaptureRequestState>
GenomePreviewCaptureRequestStore::consumePendingOnRenderOwner(
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
                    && request->phase() == PreviewCaptureRequestPhase::Pending
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

bool GenomePreviewCaptureRequestStore::cancel(
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

std::optional<RetainedPayloadBudget::Reservation>
GenomePreviewCaptureRequestStore::tryReserveOutput(
        std::size_t bytes) noexcept {
    return mOutputBudget.tryReserve(bytes);
}

std::size_t GenomePreviewCaptureRequestStore::cancelAttachment(
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

bool GenomePreviewCaptureRequestStore::release(
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

void GenomePreviewCaptureRequestStore::close() noexcept {
    std::array<std::shared_ptr<GenomePreviewCaptureRequestState>,
            kGenomePreviewCaptureSlotCapacity>
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

GenomePreviewCaptureStoreSnapshot
GenomePreviewCaptureRequestStore::snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return {
            mSlotCapacity,
            mOccupied,
            mHighWater,
            mAccepted,
            mRejectedBusy,
            mCancelled,
            mClosing,
            mInputBudget.snapshot(),
            mOutputBudget.snapshot(),
    };
}

std::size_t GenomePreviewCaptureRequestStore::findRequestLocked(
        std::uint64_t requestId) const noexcept {
    for (std::size_t slot = 0; slot < mSlotCapacity; ++slot) {
        if (mSlots[slot] != nullptr
                && mSlots[slot]->requestId == requestId) {
            return slot;
        }
    }
    return kNoSlot;
}

void GenomePreviewCaptureRequestStore::pruneTerminalLocked() noexcept {
    for (std::size_t slot = 0; slot < mSlotCapacity; ++slot) {
        if (mSlots[slot] != nullptr && isTerminal(mSlots[slot]->phase())) {
            mSlots[slot].reset();
            --mOccupied;
        }
    }
}

} // namespace cellularity::app::genome_preview
