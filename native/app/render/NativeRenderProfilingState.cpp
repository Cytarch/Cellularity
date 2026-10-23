#include "app/render/NativeRenderProfilingState.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "app/render/NativeRenderProfilingLayout.h"

#ifndef CELLULARITY_ENABLE_VULKAN_PROFILING
#define CELLULARITY_ENABLE_VULKAN_PROFILING 0
#endif

namespace cellularity::app::render_worker {
namespace {

template <typename T>
void writeLittleEndian(std::uint8_t* destination,
        std::size_t offset, T value) noexcept {
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        destination[offset + index] = static_cast<std::uint8_t>(
                static_cast<std::uint64_t>(value) >> (index * 8U));
    }
}

void writeDistribution(std::uint8_t* destination, std::size_t offset,
        const NativeRenderProfilingDistribution& distribution) noexcept {
    writeLittleEndian(destination, offset, distribution.sampleCount);
    writeLittleEndian(destination, offset + 8, distribution.p50Nanos);
    writeLittleEndian(destination, offset + 16, distribution.p95Nanos);
    writeLittleEndian(destination, offset + 24, distribution.p99Nanos);
    writeLittleEndian(destination, offset + 32, distribution.maximumNanos);
}

#if CELLULARITY_ENABLE_VULKAN_PROFILING

constexpr std::size_t kSampleCapacity = 2048;

std::uint64_t monotonicNanos() noexcept {
    return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
}

class SampleRing final {
public:
    void reset() noexcept {
        mNext = 0;
        mCount = 0;
    }

    void add(std::uint64_t value) noexcept {
        mValues[mNext] = value;
        mNext = (mNext + 1U) % mValues.size();
        if (mCount < mValues.size())
            ++mCount;
    }

    [[nodiscard]] NativeRenderProfilingDistribution snapshot() const {
        NativeRenderProfilingDistribution result;
        if (mCount == 0)
            return result;
        std::vector<std::uint64_t> sorted(mCount);
        const std::size_t first = mCount == mValues.size() ? mNext : 0;
        for (std::size_t index = 0; index < mCount; ++index) {
            sorted[index] = mValues[(first + index) % mValues.size()];
        }
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](std::size_t numerator) {
            const std::size_t rank = std::max<std::size_t>(
                    1, (sorted.size() * numerator + 99U) / 100U);
            return sorted[std::min(rank, sorted.size()) - 1U];
        };
        result.sampleCount = sorted.size();
        result.p50Nanos = percentile(50);
        result.p95Nanos = percentile(95);
        result.p99Nanos = percentile(99);
        result.maximumNanos = sorted.back();
        return result;
    }

private:
    std::array<std::uint64_t, kSampleCapacity> mValues{};
    std::size_t mNext = 0;
    std::size_t mCount = 0;
};

class NativeRenderProfilingAccumulator final {
public:
    [[nodiscard]] std::uint64_t configuration() const noexcept {
        return mConfiguration.load(std::memory_order_acquire);
    }

    int publish(bool enabled, std::uint64_t generation) noexcept {
        if ((generation & kRenderProfilingEnabledBit) != 0) {
            return static_cast<int>(
                    ProfilingConfigurationPublishResult::InvalidGeneration);
        }
        const std::uint64_t next = generation
                | (enabled ? kRenderProfilingEnabledBit : 0);
        std::lock_guard<std::mutex> lock(mMutex);
        const std::uint64_t current =
                mConfiguration.load(std::memory_order_relaxed);
        if (current == next) {
            return static_cast<int>(
                    ProfilingConfigurationPublishResult::Duplicate);
        }
        if ((current & kRenderProfilingGenerationMask) != generation) {
            resetLocked();
        }
        mConfiguration.store(next, std::memory_order_release);
        return static_cast<int>(ProfilingConfigurationPublishResult::Applied);
    }

    NativeRenderProfilingFrameToken beginFrame() noexcept {
        const std::uint64_t configurationSnapshot = configuration();
        if ((configurationSnapshot & kRenderProfilingEnabledBit) == 0) {
            return {};
        }
        const std::uint64_t startedAt = monotonicNanos();
        std::lock_guard<std::mutex> lock(mMutex);
        if (mConfiguration.load(std::memory_order_relaxed)
                != configurationSnapshot) {
            return {};
        }
        if (mFirstFrameStartedAt == 0)
            mFirstFrameStartedAt = startedAt;
        if (mLastFrameStartedAt != 0 && startedAt >= mLastFrameStartedAt) {
            mFrameIntervals.add(startedAt - mLastFrameStartedAt);
        }
        mLastFrameStartedAt = startedAt;
        ++mCallbackCount;
        return {configurationSnapshot, startedAt};
    }

    void finishFrame(NativeRenderProfilingFrameToken token,
            std::uint64_t workerIdentity, bool microscopeWorker,
            cellularity::render::RenderFrameOutcome outcome) noexcept {
        if (!token.active())
            return;
        const std::uint64_t finishedAt = monotonicNanos();
        std::lock_guard<std::mutex> lock(mMutex);
        if (mConfiguration.load(std::memory_order_relaxed)
                != token.configuration) {
            return;
        }
        mCpuFrames.add(finishedAt >= token.startedAtNanos
                        ? finishedAt - token.startedAtNanos
                        : 0);
        mLatestFrameFinishedAt = finishedAt;
        switch (outcome) {
            case cellularity::render::RenderFrameOutcome::Presented:
                ++mPresentedCount;
                if (microscopeWorker) {
                    const auto found = mPendingCameraInputs.find(workerIdentity);
                    if (found != mPendingCameraInputs.end()) {
                        if (finishedAt >= found->second) {
                            mCameraLatencies.add(finishedAt - found->second);
                        }
                        mPendingCameraInputs.erase(found);
                    }
                }
                break;
            case cellularity::render::RenderFrameOutcome::Deferred:
            case cellularity::render::RenderFrameOutcome::SwapchainRecreated:
            case cellularity::render::RenderFrameOutcome::SkippedNotReady:
                ++mDeferredCount;
                break;
            case cellularity::render::RenderFrameOutcome::SurfaceLost:
            case cellularity::render::RenderFrameOutcome::Failed:
                ++mFailedCount;
                break;
        }
    }

    void registerCameraInput(std::uint64_t workerIdentity,
            std::int64_t ingressTimestampNanos) noexcept {
        if (workerIdentity == 0 || ingressTimestampNanos <= 0)
            return;
        const std::uint64_t configurationSnapshot = configuration();
        if ((configurationSnapshot & kRenderProfilingEnabledBit) == 0)
            return;
        try {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mConfiguration.load(std::memory_order_relaxed)
                    != configurationSnapshot) {
                return;
            }
            mPendingCameraInputs.try_emplace(
                    workerIdentity,
                    static_cast<std::uint64_t>(ingressTimestampNanos));
        } catch (...) {
        }
    }

    void discardCameraInput(std::uint64_t workerIdentity) noexcept {
        if (workerIdentity == 0)
            return;
        std::lock_guard<std::mutex> lock(mMutex);
        mPendingCameraInputs.erase(workerIdentity);
    }

    bool snapshot(NativeRenderProfilingSnapshot& destination) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mMutex);
            const std::uint64_t configurationSnapshot =
                    mConfiguration.load(std::memory_order_relaxed);
            destination = {};
            destination.resetGeneration =
                    configurationSnapshot & kRenderProfilingGenerationMask;
            destination.enabled =
                    (configurationSnapshot & kRenderProfilingEnabledBit) != 0;
            destination.callbackCount = mCallbackCount;
            destination.presentedCount = mPresentedCount;
            destination.deferredCount = mDeferredCount;
            destination.failedCount = mFailedCount;
            destination.elapsedNanos = mFirstFrameStartedAt != 0
                            && mLatestFrameFinishedAt >= mFirstFrameStartedAt
                    ? mLatestFrameFinishedAt - mFirstFrameStartedAt
                    : 0;
            destination.cpuFrame = mCpuFrames.snapshot();
            destination.frameInterval = mFrameIntervals.snapshot();
            destination.cameraLatency = mCameraLatencies.snapshot();
            return true;
        } catch (...) {
            destination = {};
            return false;
        }
    }

private:
    void resetLocked() noexcept {
        mCpuFrames.reset();
        mFrameIntervals.reset();
        mCameraLatencies.reset();
        mPendingCameraInputs.clear();
        mCallbackCount = 0;
        mPresentedCount = 0;
        mDeferredCount = 0;
        mFailedCount = 0;
        mFirstFrameStartedAt = 0;
        mLastFrameStartedAt = 0;
        mLatestFrameFinishedAt = 0;
    }

    std::atomic<std::uint64_t> mConfiguration{0};
    std::mutex mMutex;
    SampleRing mCpuFrames;
    SampleRing mFrameIntervals;
    SampleRing mCameraLatencies;
    std::unordered_map<std::uint64_t, std::uint64_t> mPendingCameraInputs;
    std::uint64_t mCallbackCount = 0;
    std::uint64_t mPresentedCount = 0;
    std::uint64_t mDeferredCount = 0;
    std::uint64_t mFailedCount = 0;
    std::uint64_t mFirstFrameStartedAt = 0;
    std::uint64_t mLastFrameStartedAt = 0;
    std::uint64_t mLatestFrameFinishedAt = 0;
};

NativeRenderProfilingAccumulator& accumulator() {
    static NativeRenderProfilingAccumulator state;
    return state;
}

#endif

} // namespace

bool nativeRenderProfilingAvailable() noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
    return true;
#else
    return false;
#endif
}

std::uint64_t nativeRenderProfilingConfiguration() noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
    return accumulator().configuration();
#else
    return 0;
#endif
}

int publishNativeRenderProfilingConfiguration(
        bool enabled, std::uint64_t resetGeneration) noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
    return accumulator().publish(enabled, resetGeneration);
#else
    (void)enabled;
    (void)resetGeneration;
    return static_cast<int>(ProfilingConfigurationPublishResult::Unavailable);
#endif
}

NativeRenderProfilingFrameToken beginNativeRenderProfileFrame() noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
    return accumulator().beginFrame();
#else
    return {};
#endif
}

void finishNativeRenderProfileFrame(
        NativeRenderProfilingFrameToken token,
        std::uint64_t workerIdentity,
        bool microscopeWorker,
        cellularity::render::RenderFrameOutcome outcome) noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
    accumulator().finishFrame(
            token, workerIdentity, microscopeWorker, outcome);
#else
    (void)token;
    (void)workerIdentity;
    (void)microscopeWorker;
    (void)outcome;
#endif
}

void registerNativeRenderCameraInput(
        std::uint64_t workerIdentity,
        std::int64_t ingressTimestampNanos) noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
    accumulator().registerCameraInput(workerIdentity, ingressTimestampNanos);
#else
    (void)workerIdentity;
    (void)ingressTimestampNanos;
#endif
}

void discardNativeRenderCameraInput(std::uint64_t workerIdentity) noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
    accumulator().discardCameraInput(workerIdentity);
#else
    (void)workerIdentity;
#endif
}

bool snapshotNativeRenderProfiling(
        NativeRenderProfilingSnapshot& destination) noexcept {
#if CELLULARITY_ENABLE_VULKAN_PROFILING
    return accumulator().snapshot(destination);
#else
    destination = {};
    return false;
#endif
}

void encodeNativeRenderProfilingSnapshot(
        const NativeRenderProfilingSnapshot& snapshot,
        std::uint8_t* destination) noexcept {
    std::fill_n(destination, kNativeRenderProfilingSize, std::uint8_t{0});
    writeLittleEndian(destination, 0, kNativeRenderProfilingMagic);
    writeLittleEndian(destination, 4, kNativeRenderProfilingVersion);
    writeLittleEndian(destination, 6,
            static_cast<std::uint16_t>(kNativeRenderProfilingSize));
    writeLittleEndian(destination, 8, snapshot.resetGeneration);
    writeLittleEndian(destination, 16,
            static_cast<std::uint32_t>(snapshot.enabled ? 1 : 0));
    writeLittleEndian(destination, 24, snapshot.callbackCount);
    writeLittleEndian(destination, 32, snapshot.presentedCount);
    writeLittleEndian(destination, 40, snapshot.deferredCount);
    writeLittleEndian(destination, 48, snapshot.failedCount);
    writeLittleEndian(destination, 56, snapshot.elapsedNanos);
    writeDistribution(destination, 64, snapshot.cpuFrame);
    writeDistribution(destination, 104, snapshot.frameInterval);
    writeDistribution(destination, 144, snapshot.cameraLatency);
}

} // namespace cellularity::app::render_worker
