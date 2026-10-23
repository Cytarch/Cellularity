#pragma once

#include <atomic>
#include <cstdint>

#include "concurrency/LatestValueExchange.h"
#include "render/frame/RenderFrameTypes.h"

namespace cellularity::microscope {

struct PresentationViewportSnapshot final {
    render::RenderViewportState viewport{};
    std::uint64_t inputSequence = 0;
    std::int64_t publishedNanos = 0;
};

struct PresentationInteractionGate final {
    std::uint64_t revision = 0;
    int toolOrdinal = 0;
    bool hasDraggingPointers = false;
    bool hasSelectedCell = false;
};

struct MicroscopePresentationStateSnapshot final {
    std::uint64_t latestPublishedViewportRevision = 0;
    PresentationInteractionGate interactionGate{};
    concurrency::LatestValueExchangeCounters glViewportExchange{};
    concurrency::LatestValueExchangeCounters ownerViewportExchange{};
};

// The input/gesture reducer publishes complete viewport values here. A GL
// consumer receives a copy and therefore never observes a half-applied zoom or
// replays Pan/Zoom deltas. The exchange is SPSC: one presentation producer and
// one GL consumer, neither of which waits for the simulation owner.
class MicroscopePresentationState final {
public:
    [[nodiscard]] bool
    publishViewport(const PresentationViewportSnapshot& snapshot) noexcept;
    [[nodiscard]] bool
    tryConsumeNewestViewport(PresentationViewportSnapshot& snapshot) noexcept;
    [[nodiscard]] bool tryConsumeNewestOwnerViewport(
            PresentationViewportSnapshot& snapshot) noexcept;

    void publishInteractionGate(int toolOrdinal, bool hasDraggingPointers,
            bool hasSelectedCell) noexcept;
    [[nodiscard]] PresentationInteractionGate interactionGate() const noexcept;

    [[nodiscard]] MicroscopePresentationStateSnapshot snapshot() const noexcept;

private:
    [[nodiscard]] static std::uint64_t
    packInteractionGate(int toolOrdinal, bool hasDraggingPointers,
            bool hasSelectedCell) noexcept;

    concurrency::LatestValueExchange<PresentationViewportSnapshot>
            mGlViewportExchange;
    concurrency::LatestValueExchange<PresentationViewportSnapshot>
            mOwnerViewportExchange;
    std::atomic<std::uint64_t> mLatestViewportRevision{0};
    std::atomic<std::uint64_t> mInteractionGateBits{0};
    std::atomic<std::uint64_t> mInteractionGateRevision{1};
};

} // namespace cellularity::microscope
