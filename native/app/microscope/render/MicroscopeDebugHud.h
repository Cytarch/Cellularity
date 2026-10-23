#pragma once

#include <chrono>
#include <cstdint>

#include "render/text/PixelTextRenderer.h"

#include "core/SimulationWorldStageTiming.h"

class PixelTextShader;
class NativeGlApi;

namespace cellularity::microscope {

struct MicroscopeDebugHudTelemetry final {
    std::uint64_t rateSampleSequence = 0;
    bool actualRateValid = false;
    double actualCompletedUps = 0.0;
    SimulationWorldStageTimingSnapshot stageTimings{};
};

class MicroscopeDebugHud final {
public:
    [[nodiscard]] const cellularity::render::PixelTextRenderer&
    prepareGeometry(int surfaceWidth, int surfaceHeight,
            const MicroscopeDebugHudTelemetry& telemetry);
    void draw(NativeGlApi& gl, PixelTextShader& shader, int surfaceWidth,
            int surfaceHeight, const MicroscopeDebugHudTelemetry& telemetry);
    void releaseGlResources(NativeGlApi& gl);
    void abandonGlResources() noexcept;

private:
    using Clock = std::chrono::steady_clock;

    void recordPresentedFrame();
    void rebuildGeometry(int surfaceWidth, int surfaceHeight,
            const MicroscopeDebugHudTelemetry& telemetry);

    Clock::time_point mFpsWindowStartedAt{};
    std::uint32_t mFpsWindowFrames = 0;
    double mFramesPerSecond = 0.0;
    std::uint64_t mFpsRevision = 0;
    int mLastSurfaceWidth = 0;
    int mLastSurfaceHeight = 0;
    std::uint64_t mLastRateSampleSequence = ~std::uint64_t{0};
    std::uint64_t mLastStageTimingSequence = ~std::uint64_t{0};
    std::uint64_t mLastFpsRevision = ~std::uint64_t{0};
    cellularity::render::PixelTextRenderer mTextRenderer;
};

} // namespace cellularity::microscope
