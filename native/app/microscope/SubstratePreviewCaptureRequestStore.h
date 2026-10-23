#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "app/NativeRenderSession.h"
#include "app/genome_preview/RetainedPayloadBudget.h"
#include "app/render/preview/NativePreviewCaptureRequestState.h"

namespace cellularity::app::microscope {

inline constexpr std::size_t
        kV19SubstratePreviewMaximumConcurrentCaptureRequests = 1;
inline constexpr std::size_t kSubstratePreviewCaptureSlotCapacity =
        genome_preview::boundedCapacityFromObservedHighWater(
                kV19SubstratePreviewMaximumConcurrentCaptureRequests,
                2,
                32);
static_assert(kSubstratePreviewCaptureSlotCapacity == 2);
inline constexpr std::size_t kSubstratePreviewMaximumRgbaBytes =
        genome_preview::checkedSizeProductOrZero(
                genome_preview::checkedSizeProductOrZero(
                        static_cast<std::size_t>(
                                NativeRenderSession::SUBSTRATE_PREVIEW_PIXELS),
                        static_cast<std::size_t>(
                                NativeRenderSession::SUBSTRATE_PREVIEW_PIXELS)),
                4U);
inline constexpr std::size_t kSubstratePreviewCaptureOutputByteBudget =
        genome_preview::checkedSizeProductOrZero(
                kSubstratePreviewCaptureSlotCapacity,
                kSubstratePreviewMaximumRgbaBytes);
static_assert(kSubstratePreviewCaptureOutputByteBudget != 0);

using SubstratePreviewCaptureRequestPhase =
        cellularity::app::preview::PreviewCaptureRequestPhase;
using cellularity::app::preview::isTerminal;

class SubstratePreviewCaptureRequestState final : public cellularity::app::preview::PreviewCaptureRequestState {
public:
    SubstratePreviewCaptureRequestState(
            std::uint64_t requestIdValue,
            std::uint64_t substratePreviewTokenValue,
            std::uint64_t controllerGenerationValue,
            std::uint64_t attachmentGenerationValue,
            std::uint64_t admissionSequenceValue) noexcept;

    SubstratePreviewCaptureRequestState(const SubstratePreviewCaptureRequestState&) = delete;
    SubstratePreviewCaptureRequestState& operator=(const SubstratePreviewCaptureRequestState&) = delete;

    [[nodiscard]] bool complete() noexcept {
        return completeForTest();
    }

    [[nodiscard]] bool cancel() noexcept {
        return cellularity::app::preview::PreviewCaptureRequestState::cancel()
                == cellularity::app::preview::PreviewCaptureCancelResult::CancellationRequested;
    }

    const std::uint64_t substratePreviewToken;
    const std::uint64_t admissionSequence;
};

enum class SubstratePreviewCaptureAdmissionStatus : std::uint8_t {
    Accepted = 0,
    Busy = 1,
    InvalidArgument = 2,
    Closing = 3,
    DuplicateRequestId = 4,
    SequenceExhausted = 5,
    InternalFailure = 6,
};

struct SubstratePreviewCaptureAdmission final {
    SubstratePreviewCaptureAdmissionStatus status =
            SubstratePreviewCaptureAdmissionStatus::InvalidArgument;
    std::shared_ptr<SubstratePreviewCaptureRequestState> request;

    [[nodiscard]] bool accepted() const noexcept {
        return status == SubstratePreviewCaptureAdmissionStatus::Accepted
                && request != nullptr;
    }
};

struct SubstratePreviewCaptureStoreSnapshot final {
    std::size_t capacity = 0;
    std::size_t occupied = 0;
    std::size_t highWater = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rejectedBusy = 0;
    std::uint64_t cancelled = 0;
    bool closing = false;
    genome_preview::RetainedPayloadBudgetSnapshot outputBudget{};
};

/** Bounded token store for substrate offscreen capture requests. */
class SubstratePreviewCaptureRequestStore final {
public:
    explicit SubstratePreviewCaptureRequestStore(
            std::size_t slotCapacity =
                    kSubstratePreviewCaptureSlotCapacity,
            std::size_t outputByteBudget =
                    kSubstratePreviewCaptureOutputByteBudget);
    ~SubstratePreviewCaptureRequestStore();

    SubstratePreviewCaptureRequestStore(const SubstratePreviewCaptureRequestStore&) = delete;
    SubstratePreviewCaptureRequestStore& operator=(const SubstratePreviewCaptureRequestStore&) = delete;
    SubstratePreviewCaptureRequestStore(SubstratePreviewCaptureRequestStore&&) = delete;
    SubstratePreviewCaptureRequestStore& operator=(SubstratePreviewCaptureRequestStore&&) = delete;

    [[nodiscard]] SubstratePreviewCaptureAdmission enqueue(
            std::uint64_t requestId,
            std::uint64_t substratePreviewToken,
            std::uint64_t controllerGeneration,
            std::uint64_t attachmentGeneration);

    [[nodiscard]] std::shared_ptr<SubstratePreviewCaptureRequestState>
    consumePendingOnRenderOwner(
            std::uint64_t activeControllerGeneration,
            std::uint64_t activeAttachmentGeneration) noexcept;

    [[nodiscard]] bool cancel(std::uint64_t requestId) noexcept;
    [[nodiscard]] std::size_t cancelAttachment(
            std::uint64_t attachmentGeneration) noexcept;
    [[nodiscard]] bool release(std::uint64_t requestId) noexcept;
    [[nodiscard]] std::optional<genome_preview::RetainedPayloadBudget::Reservation>
    tryReserveOutput(std::size_t bytes) noexcept;
    void close() noexcept;
    [[nodiscard]] SubstratePreviewCaptureStoreSnapshot snapshot() const noexcept;

private:
    void pruneTerminalLocked() noexcept;
    [[nodiscard]] std::size_t findRequestLocked(
            std::uint64_t requestId) const noexcept;

    mutable std::mutex mMutex;
    std::array<std::shared_ptr<SubstratePreviewCaptureRequestState>,
            kSubstratePreviewCaptureSlotCapacity>
            mSlots{};
    const std::size_t mSlotCapacity;
    genome_preview::RetainedPayloadBudget mOutputBudget;
    std::size_t mOccupied = 0;
    std::size_t mHighWater = 0;
    std::uint64_t mNextAdmissionSequence = 1;
    std::uint64_t mAccepted = 0;
    std::uint64_t mRejectedBusy = 0;
    std::uint64_t mCancelled = 0;
    bool mClosing = false;
};

} // namespace cellularity::app::microscope
