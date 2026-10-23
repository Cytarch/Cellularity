#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cellularity::microscope {

enum class InputLane : std::uint8_t {
    Presentation = 0,
    WorldOwner = 1,
};

enum class InputKind : std::uint8_t {
    Pan = 0,
    Zoom = 1,
    Fling = 2,
    TouchDown = 3,
    TouchMove = 4,
    TouchUp = 5,
    Tap = 6,
};

enum class InputResolution : std::uint8_t {
    Received = 0,
    PendingOwner = 1,
    Applied = 2,
    Rejected = 3,
    Coalesced = 4,
    Cancelled = 5,
};

struct ResolvedInputState final {
    std::uint64_t lastResolvedInputSequence = 0;
    std::uint64_t appliedWorldRevision = 0;
};

struct InputLatencySample final {
    std::uint64_t inputSequence = 0;
    InputLane lane = InputLane::Presentation;
    InputKind kind = InputKind::Pan;
    InputResolution resolution = InputResolution::Received;
    std::uint64_t eventViewportRevision = 0;
    std::uint64_t publishedViewportRevision = 0;
    std::uint64_t appliedWorldRevision = 0;
    std::uint64_t presentedViewportRevision = 0;
    std::uint64_t presentedResolvedInputSequence = 0;
    std::uint64_t presentedWorldRevision = 0;
    std::int64_t eventReceivedNanos = 0;
    std::int64_t enqueuedOrPublishedNanos = 0;
    std::int64_t ownerResolvedNanos = 0;
    std::int64_t sourceCapturedNanos = 0;
    std::int64_t framePreparedNanos = 0;
    std::int64_t firstPresentedNanos = 0;
    bool changedWorld = false;
};

struct InputTimelineCounters final {
    std::uint64_t received = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t sampleOverwrites = 0;
    std::uint32_t worldQueueHighWater = 0;
};

struct InputTimelineSnapshot final {
    ResolvedInputState state{};
    InputTimelineCounters counters{};
    std::vector<InputLatencySample> samples;
};

// Tracks the synchronous input contract now and supplies opt-in timing
// evidence for the later asynchronous owner/mailbox transition. The sequence
// and revision watermarks are always maintained; timestamp/sample collection
// is compiled out unless CELLULARITY_RENDER_TIMELINE is enabled.
class MicroscopeInputTimeline final {
public:
    [[nodiscard]] std::uint64_t beginInput(InputLane lane, InputKind kind,
            std::uint64_t eventViewportRevision);
    void resolvePresentationInput(std::uint64_t inputSequence, bool accepted,
            std::uint64_t publishedViewportRevision);
    void enqueueWorldInput(std::uint64_t inputSequence, bool accepted,
            std::uint32_t queueDepth,
            bool replacesPendingTap = false);
    void resolveWorldInputs(bool changedWorld);
    void cancelWorldInputs();

    [[nodiscard]] ResolvedInputState resolvedState() const noexcept;
    void markFramePresented(std::uint64_t frameLastResolvedInputSequence,
            std::uint64_t frameWorldRevision,
            std::int64_t sourceCapturedNanos,
            std::int64_t framePreparedNanos,
            std::uint64_t presentedViewportRevision,
            std::int64_t firstPresentedNanos);

    void resetMeasurements();
    [[nodiscard]] InputTimelineSnapshot snapshot() const;

private:
    static constexpr std::size_t SAMPLE_CAPACITY = 2048;

    std::uint64_t mLastInputSequence = 0;
    std::uint64_t mLastResolvedInputSequence = 0;
    std::uint64_t mAppliedWorldRevision = 0;
    std::uint32_t mPendingWorldInputCount = 0;

#ifdef CELLULARITY_RENDER_TIMELINE
    std::array<InputLatencySample, SAMPLE_CAPACITY> mSamples{};
    std::size_t mSampleStart = 0;
    std::size_t mSampleCount = 0;
    InputTimelineCounters mCounters{};

    [[nodiscard]] InputLatencySample*
    findSample(std::uint64_t inputSequence) noexcept;
    [[nodiscard]] InputLatencySample* findNewestPendingTap() noexcept;
    [[nodiscard]] InputLatencySample& appendSample();
#endif

    [[nodiscard]] std::uint64_t nextInputSequence();
    void advanceResolvedWatermarkIfUnblocked() noexcept;
};

} // namespace cellularity::microscope
