#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>

#include "app/NativeWorldContext.h"
#include "app/microscope/MicroscopeTemperatureMode.h"

class MicroscopeInputController;

namespace cellularity::microscope {
class MicroscopeInputTimeline;
class MicroscopeSimulationState;
} // namespace cellularity::microscope
class MicroscopeDragController;
class MicroscopeSelectionController;
class MicroscopeTapController;
class NativeSimulationStepper;

class MicroscopeFrameController {
public:
    // Interaction/simulation semantic point. The application composition
    // layer maps this to render-frame metadata without making the interaction
    // target depend on rendering.
    enum class CaptureStage : std::uint8_t {
        StableWorld = 0,
        AfterUpdatePhaseOne = 1,
        AfterCompletedUpdate = 2,
    };

    enum class UpdatePath : std::uint8_t {
        None = 0,
        SplitRendering = 1,
        FullWithoutRendering = 2,
    };

    enum class UpdateCadence : std::uint8_t {
        Disabled = 0,
        DisplayPaced60Hz = 1,
        ConfiguredTarget = 2,
    };

    enum class CaptureCadence : std::uint8_t {
        DirtyImmediate = 0,
        EveryDisplayStep = 1,
        Fixed30Hz = 2,
        Disabled = 3,
    };

    struct ModeContract final {
        UpdatePath updatePath = UpdatePath::None;
        UpdateCadence updateCadence = UpdateCadence::Disabled;
        CaptureCadence captureCadence = CaptureCadence::DirtyImmediate;
        CaptureStage captureStage = CaptureStage::StableWorld;
        bool usesExpandedCoverage = false;
    };

    static constexpr int MODE_FREEZE =
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Freeze);
    static constexpr int MODE_OBSERVE =
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Observe);
    static constexpr int MODE_INCUBATE =
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Incubate);
    static constexpr int MODE_SLOW =
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Slow);
    static constexpr int MODE_INCUBATE_NO_RENDER =
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::IncubateNoRender);
    static constexpr std::int64_t INCUBATE_CAPTURE_INTERVAL_NANOS =
            cellularity::microscope::kIncubateCaptureIntervalNanos;

    [[nodiscard]] static ModeContract modeContract(int temperatureMode) noexcept;
    [[nodiscard]] static bool
    captureDue(const ModeContract& contract, bool frameDataReady,
            bool viewportChanged,
            std::int64_t elapsedSinceLastCaptureNanos) noexcept;

    using RenderFrameCapture = std::function<void(
            const SimulationWorld&, const NativeViewport&, float, CaptureStage)>;

    void invalidateFrameData();
    bool frameDataReady() const;
    bool drawFramePending() const;
    bool shouldUploadDrawFrame(int temperatureMode) const;
    void markDrawFrameConsumed();
    void onTemperatureModeChanged();

    bool
    stepFrame(NativeWorldContext context, NativeSimulationStepper& simulation,
            cellularity::microscope::MicroscopeSimulationState& simulationState,
            const RenderFrameCapture& captureRenderFrame,
            MicroscopeInputController& input,
            cellularity::microscope::MicroscopeInputTimeline& inputTimeline,
            MicroscopeDragController& drag, MicroscopeTapController& tap,
            MicroscopeSelectionController& selection, int toolOrdinal,
            bool hasSelectedCell);
    std::array<float, 3> nextBackgroundColor(int temperatureMode);
    void paceBeforeDraw(int temperatureMode);

private:
    std::chrono::steady_clock::time_point mFrameStartTime{};
    std::array<float, 3> mBackgroundColor{0.8f, 0.8f, 1.0f};
    bool mFrameDataReady = false;
    bool mDrawFramePending = true;
};
