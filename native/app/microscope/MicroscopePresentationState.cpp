#include "app/microscope/MicroscopePresentationState.h"

#include <limits>

namespace cellularity::microscope {
namespace {

constexpr std::uint64_t kToolMask = 0xffffffffULL;
constexpr std::uint64_t kDraggingBit = 1ULL << 32U;
constexpr std::uint64_t kSelectedBit = 1ULL << 33U;

void saturatingIncrement(std::atomic<std::uint64_t>& value) noexcept {
    std::uint64_t observed = value.load(std::memory_order_relaxed);
    while (observed != std::numeric_limits<std::uint64_t>::max() && !value.compare_exchange_weak(observed, observed + 1, std::memory_order_release, std::memory_order_relaxed)) {
    }
}

} // namespace

bool MicroscopePresentationState::publishViewport(
        const PresentationViewportSnapshot& snapshot) noexcept {
    if (snapshot.viewport.revision == 0 || !mGlViewportExchange.publish(snapshot, snapshot.viewport.revision)) {
        return false;
    }
    if (!mOwnerViewportExchange.publish(snapshot, snapshot.viewport.revision)) {
        return false;
    }
    mLatestViewportRevision.store(snapshot.viewport.revision,
            std::memory_order_release);
    return true;
}

bool MicroscopePresentationState::tryConsumeNewestViewport(
        PresentationViewportSnapshot& snapshot) noexcept {
    return mGlViewportExchange.tryConsumeNewest(snapshot);
}

bool MicroscopePresentationState::tryConsumeNewestOwnerViewport(
        PresentationViewportSnapshot& snapshot) noexcept {
    return mOwnerViewportExchange.tryConsumeNewest(snapshot);
}

void MicroscopePresentationState::publishInteractionGate(
        int toolOrdinal, bool hasDraggingPointers, bool hasSelectedCell) noexcept {
    const std::uint64_t next =
            packInteractionGate(toolOrdinal, hasDraggingPointers, hasSelectedCell);
    const std::uint64_t previous =
            mInteractionGateBits.exchange(next, std::memory_order_acq_rel);
    if (previous != next) {
        saturatingIncrement(mInteractionGateRevision);
    }
}

PresentationInteractionGate
MicroscopePresentationState::interactionGate() const noexcept {
    const std::uint64_t bits =
            mInteractionGateBits.load(std::memory_order_acquire);
    PresentationInteractionGate gate;
    gate.revision = mInteractionGateRevision.load(std::memory_order_acquire);
    gate.toolOrdinal = static_cast<int>(static_cast<std::uint32_t>(bits));
    gate.hasDraggingPointers = (bits & kDraggingBit) != 0;
    gate.hasSelectedCell = (bits & kSelectedBit) != 0;
    return gate;
}

MicroscopePresentationStateSnapshot
MicroscopePresentationState::snapshot() const noexcept {
    return {
            mLatestViewportRevision.load(std::memory_order_acquire),
            interactionGate(),
            mGlViewportExchange.counters(),
            mOwnerViewportExchange.counters(),
    };
}

std::uint64_t MicroscopePresentationState::packInteractionGate(
        int toolOrdinal, bool hasDraggingPointers, bool hasSelectedCell) noexcept {
    std::uint64_t bits = static_cast<std::uint32_t>(toolOrdinal) & kToolMask;
    if (hasDraggingPointers) {
        bits |= kDraggingBit;
    }
    if (hasSelectedCell) {
        bits |= kSelectedBit;
    }
    return bits;
}

} // namespace cellularity::microscope
