#include "app/microscope/MicroscopeInputTimeline.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#ifdef CELLULARITY_RENDER_TIMELINE
#include <chrono>
#endif

namespace cellularity::microscope {
namespace {

#ifdef CELLULARITY_RENDER_TIMELINE
[[nodiscard]] std::int64_t nowNanos() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
}
#endif

} // namespace

std::uint64_t
MicroscopeInputTimeline::beginInput(InputLane lane, InputKind kind,
        std::uint64_t eventViewportRevision) {
    const std::uint64_t sequence = nextInputSequence();
#ifdef CELLULARITY_RENDER_TIMELINE
    InputLatencySample& sample = appendSample();
    sample = {};
    sample.inputSequence = sequence;
    sample.lane = lane;
    sample.kind = kind;
    sample.eventViewportRevision = eventViewportRevision;
    sample.eventReceivedNanos = nowNanos();
    ++mCounters.received;
#else
    (void)lane;
    (void)kind;
    (void)eventViewportRevision;
#endif
    return sequence;
}

void MicroscopeInputTimeline::resolvePresentationInput(
        std::uint64_t inputSequence, bool accepted,
        std::uint64_t publishedViewportRevision) {
#ifdef CELLULARITY_RENDER_TIMELINE
    if (InputLatencySample* sample = findSample(inputSequence)) {
        const std::int64_t resolvedAt = nowNanos();
        sample->enqueuedOrPublishedNanos = resolvedAt;
        sample->ownerResolvedNanos = resolvedAt;
        sample->publishedViewportRevision = publishedViewportRevision;
        sample->resolution =
                accepted ? InputResolution::Applied : InputResolution::Rejected;
    }
    if (accepted) {
        ++mCounters.accepted;
    } else {
        ++mCounters.rejected;
    }
#else
    (void)inputSequence;
    (void)accepted;
    (void)publishedViewportRevision;
#endif
    advanceResolvedWatermarkIfUnblocked();
}

void MicroscopeInputTimeline::enqueueWorldInput(std::uint64_t inputSequence,
        bool accepted,
        std::uint32_t queueDepth,
        bool replacesPendingTap) {
#ifdef CELLULARITY_RENDER_TIMELINE
    const std::int64_t queuedAt = nowNanos();
    if (replacesPendingTap) {
        if (InputLatencySample* replaced = findNewestPendingTap()) {
            replaced->resolution = InputResolution::Coalesced;
            replaced->ownerResolvedNanos = queuedAt;
            ++mCounters.coalesced;
        }
    }
    if (InputLatencySample* sample = findSample(inputSequence)) {
        sample->enqueuedOrPublishedNanos = queuedAt;
        sample->resolution =
                accepted ? InputResolution::PendingOwner : InputResolution::Rejected;
    }
    if (accepted) {
        ++mCounters.accepted;
    } else {
        ++mCounters.rejected;
    }
    mCounters.worldQueueHighWater =
            std::max(mCounters.worldQueueHighWater, queueDepth);
#else
    (void)inputSequence;
    (void)queueDepth;
#endif

    if (replacesPendingTap && mPendingWorldInputCount > 0) {
        --mPendingWorldInputCount;
    }
    if (accepted) {
        ++mPendingWorldInputCount;
    }
    advanceResolvedWatermarkIfUnblocked();
}

void MicroscopeInputTimeline::resolveWorldInputs(bool changedWorld) {
    if (changedWorld) {
        if (mAppliedWorldRevision == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("Microscope world-input revision exhausted");
        }
        ++mAppliedWorldRevision;
    }

#ifdef CELLULARITY_RENDER_TIMELINE
    const std::int64_t resolvedAt = nowNanos();
    for (std::size_t index = 0; index < mSampleCount; ++index) {
        InputLatencySample& sample =
                mSamples[(mSampleStart + index) % SAMPLE_CAPACITY];
        if (sample.resolution != InputResolution::PendingOwner) {
            continue;
        }
        sample.resolution = InputResolution::Applied;
        sample.ownerResolvedNanos = resolvedAt;
        sample.appliedWorldRevision = mAppliedWorldRevision;
        sample.changedWorld = changedWorld;
    }
#endif

    mPendingWorldInputCount = 0;
    mLastResolvedInputSequence = mLastInputSequence;
}

void MicroscopeInputTimeline::cancelWorldInputs() {
#ifdef CELLULARITY_RENDER_TIMELINE
    const std::int64_t cancelledAt = nowNanos();
    for (std::size_t index = 0; index < mSampleCount; ++index) {
        InputLatencySample& sample =
                mSamples[(mSampleStart + index) % SAMPLE_CAPACITY];
        if (sample.resolution != InputResolution::PendingOwner) {
            continue;
        }
        sample.resolution = InputResolution::Cancelled;
        sample.ownerResolvedNanos = cancelledAt;
        ++mCounters.cancelled;
    }
#endif
    mPendingWorldInputCount = 0;
    mLastResolvedInputSequence = mLastInputSequence;
}

ResolvedInputState MicroscopeInputTimeline::resolvedState() const noexcept {
    return {mLastResolvedInputSequence, mAppliedWorldRevision};
}

void MicroscopeInputTimeline::markFramePresented(
        std::uint64_t frameLastResolvedInputSequence,
        std::uint64_t frameWorldRevision, std::int64_t sourceCapturedNanos,
        std::int64_t framePreparedNanos, std::uint64_t presentedViewportRevision,
        std::int64_t firstPresentedNanos) {
#ifdef CELLULARITY_RENDER_TIMELINE
    for (std::size_t index = 0; index < mSampleCount; ++index) {
        InputLatencySample& sample =
                mSamples[(mSampleStart + index) % SAMPLE_CAPACITY];
        if (sample.resolution != InputResolution::Applied || sample.firstPresentedNanos != 0) {
            continue;
        }

        bool represented = false;
        if (sample.lane == InputLane::Presentation) {
            represented =
                    sample.publishedViewportRevision != 0 && sample.publishedViewportRevision <= presentedViewportRevision;
        } else {
            represented = sample.inputSequence <= frameLastResolvedInputSequence && sample.appliedWorldRevision <= frameWorldRevision;
        }
        if (!represented) {
            continue;
        }

        if (sample.lane == InputLane::WorldOwner) {
            sample.sourceCapturedNanos = sourceCapturedNanos;
            sample.framePreparedNanos = framePreparedNanos;
        }
        sample.firstPresentedNanos = firstPresentedNanos;
        sample.presentedViewportRevision = presentedViewportRevision;
        sample.presentedResolvedInputSequence = frameLastResolvedInputSequence;
        sample.presentedWorldRevision = frameWorldRevision;
    }
#else
    (void)frameLastResolvedInputSequence;
    (void)frameWorldRevision;
    (void)sourceCapturedNanos;
    (void)framePreparedNanos;
    (void)presentedViewportRevision;
    (void)firstPresentedNanos;
#endif
}

void MicroscopeInputTimeline::resetMeasurements() {
#ifdef CELLULARITY_RENDER_TIMELINE
    mSamples = {};
    mSampleStart = 0;
    mSampleCount = 0;
    mCounters = {};
    mCounters.worldQueueHighWater = mPendingWorldInputCount;
#endif
}

InputTimelineSnapshot MicroscopeInputTimeline::snapshot() const {
    InputTimelineSnapshot result;
    result.state = resolvedState();
#ifdef CELLULARITY_RENDER_TIMELINE
    result.counters = mCounters;
    result.samples.reserve(mSampleCount);
    for (std::size_t index = 0; index < mSampleCount; ++index) {
        result.samples.push_back(
                mSamples[(mSampleStart + index) % SAMPLE_CAPACITY]);
    }
#endif
    return result;
}

#ifdef CELLULARITY_RENDER_TIMELINE
InputLatencySample*
MicroscopeInputTimeline::findSample(std::uint64_t inputSequence) noexcept {
    for (std::size_t index = 0; index < mSampleCount; ++index) {
        InputLatencySample& sample =
                mSamples[(mSampleStart + index) % SAMPLE_CAPACITY];
        if (sample.inputSequence == inputSequence) {
            return &sample;
        }
    }
    return nullptr;
}

InputLatencySample* MicroscopeInputTimeline::findNewestPendingTap() noexcept {
    for (std::size_t reverseIndex = mSampleCount; reverseIndex > 0;
            --reverseIndex) {
        InputLatencySample& sample =
                mSamples[(mSampleStart + reverseIndex - 1) % SAMPLE_CAPACITY];
        if (sample.kind == InputKind::Tap && sample.resolution == InputResolution::PendingOwner) {
            return &sample;
        }
    }
    return nullptr;
}

InputLatencySample& MicroscopeInputTimeline::appendSample() {
    std::size_t index = 0;
    if (mSampleCount < SAMPLE_CAPACITY) {
        index = (mSampleStart + mSampleCount) % SAMPLE_CAPACITY;
        ++mSampleCount;
    } else {
        index = mSampleStart;
        mSampleStart = (mSampleStart + 1) % SAMPLE_CAPACITY;
        ++mCounters.sampleOverwrites;
    }
    return mSamples[index];
}
#endif

std::uint64_t MicroscopeInputTimeline::nextInputSequence() {
    if (mLastInputSequence == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Microscope input sequence exhausted");
    }
    return ++mLastInputSequence;
}

void MicroscopeInputTimeline::advanceResolvedWatermarkIfUnblocked() noexcept {
    if (mPendingWorldInputCount == 0) {
        mLastResolvedInputSequence = mLastInputSequence;
    }
}

} // namespace cellularity::microscope
