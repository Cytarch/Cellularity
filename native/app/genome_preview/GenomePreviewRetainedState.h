#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "app/genome_preview/GenomePreviewInteractionQueue.h"
#include "app/genome_preview/RetainedPayloadBudget.h"
#include "concurrency/LatestValueExchange.h"
#include "formats/GenomeDraftCodec.h"

namespace cellularity::app::genome_preview {

inline constexpr std::size_t kGenomePreviewRetainedOwnershipPositions =
        kGenomePreviewInteractionCapacity + 4U;
inline constexpr std::size_t kGenomePreviewRetainedByteBudget =
        checkedSizeProductOrZero(
                kGenomePreviewRetainedOwnershipPositions,
                cellularity::kGenomeDraftMaximumWireBytes);
static_assert(kGenomePreviewRetainedByteBudget != 0);

struct GenomePreviewContentSnapshot final {
    GenomePreviewContentSnapshot(
            std::uint64_t revisionValue,
            std::uint64_t producerSequenceValue,
            double simulationTimeHoursValue,
            const std::uint8_t* bytes,
            std::size_t byteCount,
            RetainedPayloadBudget::Reservation reservationValue);

    GenomePreviewContentSnapshot(const GenomePreviewContentSnapshot&) = delete;
    GenomePreviewContentSnapshot& operator=(const GenomePreviewContentSnapshot&) = delete;

    const std::uint64_t revision;
    const std::uint64_t producerSequence;
    const double simulationTimeHours;
    const std::vector<std::uint8_t> genomeDraftBytes;
    const cellularity::GenomeData decodedGenome;

private:
    RetainedPayloadBudget::Reservation mReservation;
};

struct GenomePreviewViewUpdate final {
    std::int32_t selectedGeneIndex = -1;
    bool gridEnabled = false;
    bool treeEnabled = false;
    std::int32_t viewportWidth = 0;
    std::int32_t viewportHeight = 0;
};

struct GenomePreviewViewSnapshot final {
    std::uint64_t revision = 0;
    std::uint64_t producerSequence = 0;
    GenomePreviewViewUpdate update{};
};

enum class GenomePreviewStatePublishStatus : std::uint8_t {
    Applied = 0,
    Duplicate = 1,
    StaleProducerSequence = 2,
    ConflictingProducerSequence = 3,
    InvalidArgument = 4,
    BudgetExceeded = 5,
    RevisionExhausted = 6,
    ExchangeUnavailable = 7,
    Closing = 8,
    InternalFailure = 9,
};

struct GenomePreviewStatePublishResult final {
    GenomePreviewStatePublishStatus status =
            GenomePreviewStatePublishStatus::InvalidArgument;
    std::uint64_t revision = 0;

    [[nodiscard]] bool accepted() const noexcept {
        return status == GenomePreviewStatePublishStatus::Applied
                || status == GenomePreviewStatePublishStatus::Duplicate;
    }
};

struct GenomePreviewPendingState final {
    std::shared_ptr<const GenomePreviewContentSnapshot> content;
    std::shared_ptr<const GenomePreviewViewSnapshot> view;
};

struct GenomePreviewRetainedStateSnapshot final {
    std::uint64_t contentRevision = 0;
    std::uint64_t viewRevision = 0;
    std::uint64_t lastContentProducerSequence = 0;
    std::uint64_t lastViewProducerSequence = 0;
    bool closing = false;
    RetainedPayloadBudgetSnapshot payloadBudget{};
};

/**
 * Standalone V-23 state primitive. One ingress lock sequences independent
 * latest content/view publications with dependent interaction fences. Full
 * decode validates the immutable ingress copy before publication; simulation
 * and drawing remain render-owner work.
 */
class GenomePreviewRetainedState final {
public:
    explicit GenomePreviewRetainedState(
            std::size_t byteBudget = kGenomePreviewRetainedByteBudget,
            std::uint64_t nextContentRevision = 1,
            std::uint64_t nextViewRevision = 1);
    ~GenomePreviewRetainedState();

    GenomePreviewRetainedState(const GenomePreviewRetainedState&) = delete;
    GenomePreviewRetainedState& operator=(const GenomePreviewRetainedState&) = delete;
    GenomePreviewRetainedState(GenomePreviewRetainedState&&) = delete;
    GenomePreviewRetainedState& operator=(GenomePreviewRetainedState&&) = delete;

    [[nodiscard]] GenomePreviewStatePublishResult publishContent(
            const std::uint8_t* genomeDraftBytes,
            std::size_t genomeDraftByteCount,
            double simulationTimeHours,
            std::uint64_t producerSequence);

    [[nodiscard]] GenomePreviewStatePublishResult publishView(
            const GenomePreviewViewUpdate& update,
            std::uint64_t producerSequence);

    [[nodiscard]] GenomePreviewInteractionEnqueueResult enqueueInteraction(
            GenomePreviewInteractionQueue& queue,
            const GenomePreviewInteractionInput& input);

    [[nodiscard]] bool consumePendingOnRenderOwner(
            GenomePreviewPendingState& destination) noexcept;

    void close() noexcept;
    [[nodiscard]] GenomePreviewRetainedStateSnapshot snapshot() const noexcept;

private:
    [[nodiscard]] static bool viewUpdatesEqual(
            const GenomePreviewViewUpdate& left,
            const GenomePreviewViewUpdate& right) noexcept;
    [[nodiscard]] static std::uint64_t nextRevisionAfter(
            std::uint64_t revision) noexcept;

    mutable std::mutex mIngressMutex;
    RetainedPayloadBudget mPayloadBudget;
    concurrency::LatestValueExchange<
            std::shared_ptr<const GenomePreviewContentSnapshot>>
            mContentExchange;
    concurrency::LatestValueExchange<
            std::shared_ptr<const GenomePreviewViewSnapshot>>
            mViewExchange;
    std::shared_ptr<const GenomePreviewContentSnapshot> mLatestContent;
    std::shared_ptr<const GenomePreviewViewSnapshot> mLatestView;
    std::uint64_t mNextContentRevision;
    std::uint64_t mNextViewRevision;
    std::uint64_t mLastContentProducerSequence = 0;
    std::uint64_t mLastViewProducerSequence = 0;
    bool mClosing = false;
};

} // namespace cellularity::app::genome_preview
