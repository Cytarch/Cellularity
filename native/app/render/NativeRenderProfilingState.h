#pragma once

#include <array>
#include <cstdint>

#include "render/backend/RenderFrameResult.h"

namespace cellularity::app::render_worker {

inline constexpr std::uint64_t kRenderProfilingEnabledBit = UINT64_C(1) << 63U;
inline constexpr std::uint64_t kRenderProfilingGenerationMask =
        ~kRenderProfilingEnabledBit;

struct NativeRenderProfilingDistribution final {
    std::uint64_t sampleCount = 0;
    std::uint64_t p50Nanos = 0;
    std::uint64_t p95Nanos = 0;
    std::uint64_t p99Nanos = 0;
    std::uint64_t maximumNanos = 0;
};

struct NativeRenderProfilingSnapshot final {
    std::uint64_t resetGeneration = 0;
    bool enabled = false;
    std::uint64_t callbackCount = 0;
    std::uint64_t presentedCount = 0;
    std::uint64_t deferredCount = 0;
    std::uint64_t failedCount = 0;
    std::uint64_t elapsedNanos = 0;
    NativeRenderProfilingDistribution cpuFrame;
    NativeRenderProfilingDistribution frameInterval;
    NativeRenderProfilingDistribution cameraLatency;
};

struct NativeRenderProfilingFrameToken final {
    std::uint64_t configuration = 0;
    std::uint64_t startedAtNanos = 0;

    [[nodiscard]] bool active() const noexcept {
        return (configuration & kRenderProfilingEnabledBit) != 0
                && startedAtNanos != 0;
    }
};

[[nodiscard]] bool nativeRenderProfilingAvailable() noexcept;
[[nodiscard]] std::uint64_t nativeRenderProfilingConfiguration() noexcept;
[[nodiscard]] int publishNativeRenderProfilingConfiguration(
        bool enabled, std::uint64_t resetGeneration) noexcept;
[[nodiscard]] NativeRenderProfilingFrameToken beginNativeRenderProfileFrame() noexcept;
void finishNativeRenderProfileFrame(
        NativeRenderProfilingFrameToken token,
        std::uint64_t workerIdentity,
        bool microscopeWorker,
        cellularity::render::RenderFrameOutcome outcome) noexcept;
void registerNativeRenderCameraInput(
        std::uint64_t workerIdentity,
        std::int64_t ingressTimestampNanos) noexcept;
void discardNativeRenderCameraInput(std::uint64_t workerIdentity) noexcept;
[[nodiscard]] bool snapshotNativeRenderProfiling(
        NativeRenderProfilingSnapshot& destination) noexcept;
void encodeNativeRenderProfilingSnapshot(
        const NativeRenderProfilingSnapshot& snapshot,
        std::uint8_t* destination) noexcept;

} // namespace cellularity::app::render_worker
