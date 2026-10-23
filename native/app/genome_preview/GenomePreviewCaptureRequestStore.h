#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "app/genome_preview/GenomeFilePreviewSimulation.h"
#include "app/genome_preview/RetainedPayloadBudget.h"
#include "app/render/preview/NativePreviewCaptureRequestState.h"
#include "formats/GenomeDraftCodec.h"

namespace cellularity::app::genome_preview {

inline constexpr std::size_t
        kV19GenomePreviewMaximumConcurrentCaptureRequests = 1;
inline constexpr std::size_t kGenomePreviewCaptureSlotCapacity =
        boundedCapacityFromObservedHighWater(
                kV19GenomePreviewMaximumConcurrentCaptureRequests,
                2,
                32);
inline constexpr std::size_t kGenomePreviewCaptureInputByteBudget =
        checkedSizeProductOrZero(
                kGenomePreviewCaptureSlotCapacity,
                cellularity::kGenomeDraftMaximumWireBytes);
static_assert(kGenomePreviewCaptureSlotCapacity == 2);
static_assert(kGenomePreviewCaptureInputByteBudget != 0);
inline constexpr std::size_t kGenomePreviewMaximumRgbaBytes =
        checkedSizeProductOrZero(
                checkedSizeProductOrZero(
                        static_cast<std::size_t>(
                                cellularity::genome_preview::kGenomeFilePreviewPixels),
                        static_cast<std::size_t>(
                                cellularity::genome_preview::kGenomeFilePreviewPixels)),
                4U);
inline constexpr std::size_t kGenomePreviewCaptureOutputByteBudget =
        checkedSizeProductOrZero(
                kGenomePreviewCaptureSlotCapacity,
                kGenomePreviewMaximumRgbaBytes);
static_assert(kGenomePreviewCaptureOutputByteBudget != 0);

using PreviewCaptureRequestPhase =
        cellularity::app::preview::PreviewCaptureRequestPhase;
using cellularity::app::preview::isTerminal;

class GenomePreviewCaptureRequestState final : public cellularity::app::preview::PreviewCaptureRequestState {
public:
    GenomePreviewCaptureRequestState(
            std::uint64_t requestIdValue,
            std::uint64_t controllerGenerationValue,
            std::uint64_t attachmentGenerationValue,
            std::uint64_t admissionSequenceValue,
            double maximumSimulationTimeValue,
            const std::uint8_t* bytes,
            std::size_t byteCount,
            RetainedPayloadBudget::Reservation reservationValue);

    GenomePreviewCaptureRequestState(const GenomePreviewCaptureRequestState&) = delete;
    GenomePreviewCaptureRequestState& operator=(const GenomePreviewCaptureRequestState&) = delete;

    [[nodiscard]] bool complete() noexcept {
        return completeForTest();
    }

    [[nodiscard]] bool cancel() noexcept {
        return cellularity::app::preview::PreviewCaptureRequestState::cancel()
                == cellularity::app::preview::PreviewCaptureCancelResult::CancellationRequested;
    }

    const std::uint64_t admissionSequence;
    const double maximumSimulationTime;
    const std::vector<std::uint8_t> genomeDraftBytes;
    const cellularity::GenomeData decodedGenome;

private:
    RetainedPayloadBudget::Reservation mInputReservation;
};

enum class GenomePreviewCaptureAdmissionStatus : std::uint8_t {
    Accepted = 0,
    Busy = 1,
    InvalidArgument = 2,
    Closing = 3,
    BudgetExceeded = 4,
    DuplicateRequestId = 5,
    SequenceExhausted = 6,
    InternalFailure = 7,
};

struct GenomePreviewCaptureAdmission final {
    GenomePreviewCaptureAdmissionStatus status =
            GenomePreviewCaptureAdmissionStatus::InvalidArgument;
    std::shared_ptr<GenomePreviewCaptureRequestState> request;

    [[nodiscard]] bool accepted() const noexcept {
        return status == GenomePreviewCaptureAdmissionStatus::Accepted
                && request != nullptr;
    }
};

struct GenomePreviewCaptureStoreSnapshot final {
    std::size_t capacity = 0;
    std::size_t occupied = 0;
    std::size_t highWater = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rejectedBusy = 0;
    std::uint64_t cancelled = 0;
    bool closing = false;
    RetainedPayloadBudgetSnapshot inputBudget{};
    RetainedPayloadBudgetSnapshot outputBudget{};
};

/** Bounded store for independent Genome file-preview requests. */
class GenomePreviewCaptureRequestStore final {
public:
    explicit GenomePreviewCaptureRequestStore(
            std::size_t slotCapacity = kGenomePreviewCaptureSlotCapacity,
            std::size_t inputByteBudget =
                    kGenomePreviewCaptureInputByteBudget,
            std::size_t outputByteBudget =
                    kGenomePreviewCaptureOutputByteBudget);
    ~GenomePreviewCaptureRequestStore();

    GenomePreviewCaptureRequestStore(const GenomePreviewCaptureRequestStore&) = delete;
    GenomePreviewCaptureRequestStore& operator=(const GenomePreviewCaptureRequestStore&) = delete;
    GenomePreviewCaptureRequestStore(GenomePreviewCaptureRequestStore&&) = delete;
    GenomePreviewCaptureRequestStore& operator=(GenomePreviewCaptureRequestStore&&) = delete;

    [[nodiscard]] GenomePreviewCaptureAdmission enqueue(
            std::uint64_t requestId,
            std::uint64_t controllerGeneration,
            std::uint64_t attachmentGeneration,
            const std::uint8_t* genomeDraftBytes,
            std::size_t genomeDraftByteCount,
            double maximumSimulationTime);

    [[nodiscard]] std::shared_ptr<GenomePreviewCaptureRequestState>
    consumePendingOnRenderOwner(
            std::uint64_t activeControllerGeneration,
            std::uint64_t activeAttachmentGeneration) noexcept;

    [[nodiscard]] bool cancel(std::uint64_t requestId) noexcept;
    [[nodiscard]] std::size_t cancelAttachment(
            std::uint64_t attachmentGeneration) noexcept;
    [[nodiscard]] bool release(std::uint64_t requestId) noexcept;
    [[nodiscard]] std::optional<RetainedPayloadBudget::Reservation>
    tryReserveOutput(std::size_t bytes) noexcept;
    void close() noexcept;
    [[nodiscard]] GenomePreviewCaptureStoreSnapshot snapshot() const noexcept;

private:
    void pruneTerminalLocked() noexcept;
    [[nodiscard]] std::size_t findRequestLocked(
            std::uint64_t requestId) const noexcept;

    mutable std::mutex mMutex;
    std::array<std::shared_ptr<GenomePreviewCaptureRequestState>,
            kGenomePreviewCaptureSlotCapacity>
            mSlots{};
    const std::size_t mSlotCapacity;
    RetainedPayloadBudget mInputBudget;
    RetainedPayloadBudget mOutputBudget;
    std::size_t mOccupied = 0;
    std::size_t mHighWater = 0;
    std::uint64_t mNextAdmissionSequence = 1;
    std::uint64_t mAccepted = 0;
    std::uint64_t mRejectedBusy = 0;
    std::uint64_t mCancelled = 0;
    bool mClosing = false;
};

} // namespace cellularity::app::genome_preview
