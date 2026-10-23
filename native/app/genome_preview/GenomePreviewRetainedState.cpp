#include "app/genome_preview/GenomePreviewRetainedState.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

#include "io/DecodeError.h"
#include "io/NativeByteStream.h"

namespace cellularity::app::genome_preview {
namespace {

cellularity::GenomeData decodeRetainedGenomeDraft(
        const std::vector<std::uint8_t>& bytes) {
    ByteReader reader(bytes.data(), bytes.size());
    return readGenomeDraft(reader);
}

} // namespace

GenomePreviewContentSnapshot::GenomePreviewContentSnapshot(
        std::uint64_t revisionValue,
        std::uint64_t producerSequenceValue,
        double simulationTimeHoursValue,
        const std::uint8_t* bytes,
        std::size_t byteCount,
        RetainedPayloadBudget::Reservation reservationValue)
        : revision(revisionValue),
          producerSequence(producerSequenceValue),
          simulationTimeHours(simulationTimeHoursValue),
          genomeDraftBytes(bytes, bytes + byteCount),
          decodedGenome(decodeRetainedGenomeDraft(genomeDraftBytes)),
          mReservation(std::move(reservationValue)) {
}

GenomePreviewRetainedState::GenomePreviewRetainedState(
        std::size_t byteBudget,
        std::uint64_t nextContentRevision,
        std::uint64_t nextViewRevision)
        : mPayloadBudget(byteBudget),
          mNextContentRevision(nextContentRevision),
          mNextViewRevision(nextViewRevision) {
}

GenomePreviewRetainedState::~GenomePreviewRetainedState() {
    close();
}

GenomePreviewStatePublishResult
GenomePreviewRetainedState::publishContent(
        const std::uint8_t* genomeDraftBytes,
        std::size_t genomeDraftByteCount,
        double simulationTimeHours,
        std::uint64_t producerSequence) {
    if (genomeDraftBytes == nullptr
            || genomeDraftByteCount == 0
            || genomeDraftByteCount
                    > cellularity::kGenomeDraftMaximumWireBytes
            || !cellularity::hasGenomeDraftWireHeader(
                    genomeDraftBytes, genomeDraftByteCount)
            || !std::isfinite(simulationTimeHours)
            || simulationTimeHours < 0.0
            || producerSequence == 0) {
        return {GenomePreviewStatePublishStatus::InvalidArgument, 0};
    }

    std::lock_guard<std::mutex> lock(mIngressMutex);
    if (mClosing) {
        return {GenomePreviewStatePublishStatus::Closing, 0};
    }
    if (producerSequence < mLastContentProducerSequence) {
        return {GenomePreviewStatePublishStatus::StaleProducerSequence, 0};
    }
    if (producerSequence == mLastContentProducerSequence) {
        const bool duplicate = mLatestContent != nullptr
                && mLatestContent->simulationTimeHours == simulationTimeHours
                && mLatestContent->genomeDraftBytes.size()
                        == genomeDraftByteCount
                && std::equal(
                        mLatestContent->genomeDraftBytes.begin(),
                        mLatestContent->genomeDraftBytes.end(),
                        genomeDraftBytes);
        return {
                duplicate
                        ? GenomePreviewStatePublishStatus::Duplicate
                        : GenomePreviewStatePublishStatus::ConflictingProducerSequence,
                duplicate ? mLatestContent->revision : 0,
        };
    }
    if (mNextContentRevision == 0) {
        return {GenomePreviewStatePublishStatus::RevisionExhausted, 0};
    }

    std::optional<RetainedPayloadBudget::Reservation> reservation =
            mPayloadBudget.tryReserve(genomeDraftByteCount);
    if (!reservation.has_value()) {
        return {GenomePreviewStatePublishStatus::BudgetExceeded, 0};
    }

    const std::uint64_t revision = mNextContentRevision;
    std::shared_ptr<const GenomePreviewContentSnapshot> candidate;
    try {
        candidate = std::make_shared<const GenomePreviewContentSnapshot>(
                revision,
                producerSequence,
                simulationTimeHours,
                genomeDraftBytes,
                genomeDraftByteCount,
                std::move(*reservation));
    } catch (const std::bad_alloc&) {
        return {GenomePreviewStatePublishStatus::InternalFailure, 0};
    } catch (const DecodeError&) {
        return {GenomePreviewStatePublishStatus::InvalidArgument, 0};
    } catch (const std::exception&) {
        return {GenomePreviewStatePublishStatus::InvalidArgument, 0};
    }

    if (!mContentExchange.publish(candidate, revision)) {
        return {GenomePreviewStatePublishStatus::ExchangeUnavailable, 0};
    }
    mLatestContent = candidate;
    mLastContentProducerSequence = producerSequence;
    mNextContentRevision = nextRevisionAfter(revision);
    return {GenomePreviewStatePublishStatus::Applied, revision};
}

GenomePreviewStatePublishResult
GenomePreviewRetainedState::publishView(
        const GenomePreviewViewUpdate& update,
        std::uint64_t producerSequence) {
    if (update.selectedGeneIndex < -1
            || update.selectedGeneIndex
                    >= static_cast<std::int32_t>(
                            cellularity::MAX_SUPPORTED_GENES_PER_GENOME)
            || update.viewportWidth <= 0
            || update.viewportHeight <= 0
            || producerSequence == 0) {
        return {GenomePreviewStatePublishStatus::InvalidArgument, 0};
    }

    std::lock_guard<std::mutex> lock(mIngressMutex);
    if (mClosing) {
        return {GenomePreviewStatePublishStatus::Closing, 0};
    }
    if (producerSequence < mLastViewProducerSequence) {
        return {GenomePreviewStatePublishStatus::StaleProducerSequence, 0};
    }
    if (producerSequence == mLastViewProducerSequence) {
        const bool duplicate = mLatestView != nullptr
                && viewUpdatesEqual(mLatestView->update, update);
        return {
                duplicate
                        ? GenomePreviewStatePublishStatus::Duplicate
                        : GenomePreviewStatePublishStatus::ConflictingProducerSequence,
                duplicate ? mLatestView->revision : 0,
        };
    }
    if (mNextViewRevision == 0) {
        return {GenomePreviewStatePublishStatus::RevisionExhausted, 0};
    }

    const std::uint64_t revision = mNextViewRevision;
    std::shared_ptr<const GenomePreviewViewSnapshot> candidate;
    try {
        candidate = std::make_shared<const GenomePreviewViewSnapshot>(
                GenomePreviewViewSnapshot{revision, producerSequence, update});
    } catch (const std::bad_alloc&) {
        return {GenomePreviewStatePublishStatus::InternalFailure, 0};
    }
    if (!mViewExchange.publish(candidate, revision)) {
        return {GenomePreviewStatePublishStatus::ExchangeUnavailable, 0};
    }
    mLatestView = candidate;
    mLastViewProducerSequence = producerSequence;
    mNextViewRevision = nextRevisionAfter(revision);
    return {GenomePreviewStatePublishStatus::Applied, revision};
}

GenomePreviewInteractionEnqueueResult
GenomePreviewRetainedState::enqueueInteraction(
        GenomePreviewInteractionQueue& queue,
        const GenomePreviewInteractionInput& input) {
    std::lock_guard<std::mutex> lock(mIngressMutex);
    if (mClosing) {
        return {GenomePreviewInteractionEnqueueStatus::Closing, 0};
    }
    return queue.enqueue(
            input,
            GenomePreviewStateFence{mLatestContent, mLatestView});
}

bool GenomePreviewRetainedState::consumePendingOnRenderOwner(
        GenomePreviewPendingState& destination) noexcept {
    destination = {};
    const bool hasContent =
            mContentExchange.tryConsumeNewest(destination.content);
    const bool hasView = mViewExchange.tryConsumeNewest(destination.view);
    return hasContent || hasView;
}

void GenomePreviewRetainedState::close() noexcept {
    std::lock_guard<std::mutex> lock(mIngressMutex);
    if (mClosing) {
        return;
    }
    mClosing = true;
    mLatestContent.reset();
    mLatestView.reset();
}

GenomePreviewRetainedStateSnapshot
GenomePreviewRetainedState::snapshot() const noexcept {
    std::lock_guard<std::mutex> lock(mIngressMutex);
    return {
            mLatestContent != nullptr ? mLatestContent->revision : 0,
            mLatestView != nullptr ? mLatestView->revision : 0,
            mLastContentProducerSequence,
            mLastViewProducerSequence,
            mClosing,
            mPayloadBudget.snapshot(),
    };
}

bool GenomePreviewRetainedState::viewUpdatesEqual(
        const GenomePreviewViewUpdate& left,
        const GenomePreviewViewUpdate& right) noexcept {
    return left.selectedGeneIndex == right.selectedGeneIndex
            && left.gridEnabled == right.gridEnabled
            && left.treeEnabled == right.treeEnabled
            && left.viewportWidth == right.viewportWidth
            && left.viewportHeight == right.viewportHeight;
}

std::uint64_t GenomePreviewRetainedState::nextRevisionAfter(
        std::uint64_t revision) noexcept {
    return revision == std::numeric_limits<std::uint64_t>::max()
            ? 0
            : revision + 1U;
}

} // namespace cellularity::app::genome_preview
