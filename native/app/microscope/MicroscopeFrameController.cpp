#include "app/microscope/MicroscopeFrameController.h"

#include <algorithm>
#include <cstdint>
#include <thread>

#include "app/NativeSimulationStepper.h"
#include "app/microscope/MicroscopeInputController.h"
#include "app/microscope/MicroscopeInputTimeline.h"
#include "app/microscope/MicroscopeSimulationState.h"
#include "app/microscope/interaction/MicroscopeDragController.h"
#include "app/microscope/interaction/MicroscopeSelectionController.h"
#include "app/microscope/interaction/MicroscopeTapController.h"
#include "core/SimulationWorld.h"

namespace {

constexpr int64_t MIN_DELTA_NANOS = 1000000000LL / 60;

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
cellularity::simulation::SimulationSchedulerTimePoint
schedulerTimePoint(std::chrono::steady_clock::time_point timePoint) noexcept {
    return std::chrono::time_point_cast<
            cellularity::simulation::SimulationSchedulerDuration>(timePoint);
}
#endif

} // namespace

MicroscopeFrameController::ModeContract
MicroscopeFrameController::modeContract(int temperatureMode) noexcept {
    switch (temperatureMode) {
        case MODE_FREEZE:
            return {
                    UpdatePath::None,
                    UpdateCadence::Disabled,
                    CaptureCadence::DirtyImmediate,
                    CaptureStage::StableWorld,
                    false,
            };
        case MODE_OBSERVE:
        case MODE_SLOW:
            return {
                    UpdatePath::SplitRendering,
                    UpdateCadence::DisplayPaced60Hz,
                    CaptureCadence::EveryDisplayStep,
                    CaptureStage::AfterUpdatePhaseOne,
                    true,
            };
        case MODE_INCUBATE:
            return {
                    UpdatePath::FullWithoutRendering,
                    UpdateCadence::ConfiguredTarget,
                    CaptureCadence::Fixed30Hz,
                    CaptureStage::AfterCompletedUpdate,
                    true,
            };
        case MODE_INCUBATE_NO_RENDER:
            return {
                    UpdatePath::FullWithoutRendering,
                    UpdateCadence::ConfiguredTarget,
                    CaptureCadence::Disabled,
                    CaptureStage::AfterCompletedUpdate,
                    false,
            };
        default:
            // Preserve the old fallback for an unsupported ordinal: no update,
            // dirty-only stable capture, with the former non-Freeze margin.
            return {
                    UpdatePath::None,
                    UpdateCadence::Disabled,
                    CaptureCadence::DirtyImmediate,
                    CaptureStage::StableWorld,
                    true,
            };
    }
}

bool MicroscopeFrameController::captureDue(
        const ModeContract& contract, bool frameDataReady, bool viewportChanged,
        std::int64_t elapsedSinceLastCaptureNanos) noexcept {
    switch (contract.captureCadence) {
        case CaptureCadence::DirtyImmediate:
            return !frameDataReady || viewportChanged;
        case CaptureCadence::EveryDisplayStep:
            return true;
        case CaptureCadence::Fixed30Hz:
            return !frameDataReady || viewportChanged || elapsedSinceLastCaptureNanos >= INCUBATE_CAPTURE_INTERVAL_NANOS;
        case CaptureCadence::Disabled:
            return false;
    }
    return false;
}

void MicroscopeFrameController::invalidateFrameData() {
    mFrameDataReady = false;
}

bool MicroscopeFrameController::frameDataReady() const {
    return mFrameDataReady;
}

bool MicroscopeFrameController::drawFramePending() const {
    return mDrawFramePending;
}

bool MicroscopeFrameController::shouldUploadDrawFrame(
        int temperatureMode) const {
    return modeContract(temperatureMode).captureCadence != CaptureCadence::Fixed30Hz || mDrawFramePending;
}

void MicroscopeFrameController::markDrawFrameConsumed() {
    mDrawFramePending = false;
}

void MicroscopeFrameController::onTemperatureModeChanged() {
    mFrameStartTime = {};
    mDrawFramePending = true;
    invalidateFrameData();
}

bool MicroscopeFrameController::stepFrame(
        NativeWorldContext context, NativeSimulationStepper& simulation,
        cellularity::microscope::MicroscopeSimulationState& simulationState,
        const RenderFrameCapture& captureRenderFrame,
        MicroscopeInputController& input,
        cellularity::microscope::MicroscopeInputTimeline& inputTimeline,
        MicroscopeDragController& drag, MicroscopeTapController& tap,
        MicroscopeSelectionController& selection, int toolOrdinal,
        bool hasSelectedCell) {
    SimulationWorld& world = context.world;
    NativeViewport& viewport = context.viewport;
    const int temperatureMode = simulationState.temperatureMode();
    bool selectedCellActive = hasSelectedCell;
    if (world.getUpdateStage() == SimulationWorld::UPDATE_STAGE_CAN_TOUCH) {
        bool inputChangedWorld = false;
        if (tap.applyQueuedTap(context, input, selection, toolOrdinal)) {
            invalidateFrameData();
            inputChangedWorld = true;
        }
        if (input.processQueues(context, drag, toolOrdinal,
                    simulationState.dragDeltaTime())) {
            invalidateFrameData();
            inputChangedWorld = true;
        }
        inputTimeline.resolveWorldInputs(inputChangedWorld);
        selectedCellActive = selection.update(context) != -1;
        if (selectedCellActive) {
            invalidateFrameData();
        }
    }

    bool viewportChanged = false;
    if (!selectedCellActive && input.stepViewportInertia(viewport)) {
        viewportChanged = true;
    }

    const ModeContract contract = modeContract(temperatureMode);
    float margin = 0.0f;
    if (contract.usesExpandedCoverage) {
        margin = std::max(viewport.viewWidth(), viewport.viewHeight()) * 0.1f;
    }

    if (contract.updatePath == UpdatePath::SplitRendering) {
        const double speed = temperatureMode == MODE_OBSERVE
                ? 1.0
                : simulationState.slowMotionSpeed();
        simulation.update1(world, true, speed);
        captureRenderFrame(world, viewport, margin, contract.captureStage);
        simulation.update2(world, true, speed);
        mFrameDataReady = true;
        return true;
    }

    if (contract.updatePath == UpdatePath::FullWithoutRendering) {
        auto frameStart = std::chrono::steady_clock::now();
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
        std::optional<cellularity::simulation::SimulationBatchPlan> shadowPlan;
        auto* scheduler = simulationState.scheduler();
        if (scheduler != nullptr) {
            shadowPlan =
                    scheduler->planNextBatch(schedulerTimePoint(frameStart), false);
        }
#endif
        const int targetSteps = simulationState.planLegacyIncubateSteps(frameStart);

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
        auto shadowBatchStarted = frameStart;
        if (shadowPlan.has_value()) {
            shadowBatchStarted = std::chrono::steady_clock::now();
        }
#endif

        for (int i = 0; i < targetSteps; i++) {
            simulation.update(world, false, 1.0);
        }
        simulationState.completeLegacyIncubateSteps(targetSteps);

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
        const auto shadowBatchFinished = std::chrono::steady_clock::now();
        if (shadowPlan.has_value()) {
            const auto schedulerFinished = schedulerTimePoint(shadowBatchFinished);
            scheduler->recordObservedBatch(
                    *shadowPlan, static_cast<std::uint32_t>(targetSteps),
                    schedulerTimePoint(shadowBatchStarted), schedulerFinished,
                    schedulerFinished >= shadowPlan->serviceDeadline
                            ? cellularity::simulation::SimulationBatchStopReason::
                                      ServiceDeadlineReached
                            : cellularity::simulation::SimulationBatchStopReason::
                                      QuotaCompleted);
        }
#endif

        if (contract.captureCadence == CaptureCadence::Disabled) {
            return false;
        }

        auto afterSim = std::chrono::steady_clock::now();
        const std::int64_t elapsedSinceLastCaptureNanos =
                simulationState.elapsedSinceIncubateCaptureNanos(afterSim);
        const bool renderDue =
                captureDue(contract, mFrameDataReady, viewportChanged,
                        elapsedSinceLastCaptureNanos);
        if (renderDue) {
            captureRenderFrame(world, viewport, margin, contract.captureStage);
            simulationState.markIncubateCapture(std::chrono::steady_clock::now());
            mFrameDataReady = true;
            mDrawFramePending = true;
            return true;
        }

        return false;
    }

    if (captureDue(contract, mFrameDataReady, viewportChanged, 0)) {
        captureRenderFrame(world, viewport, margin, contract.captureStage);
        mFrameDataReady = true;
        return true;
    }

    return false;
}

std::array<float, 3>
MicroscopeFrameController::nextBackgroundColor(int temperatureMode) {
    std::array<float, 3> targetColor{};
    switch (temperatureMode) {
        case MODE_FREEZE:
            targetColor = {0.85f, 0.85f, 1.0f};
            break;
        case MODE_OBSERVE:
            targetColor = {0.73f, 0.7f, 0.95f};
            break;
        case MODE_INCUBATE:
            targetColor = {0.9f, 0.75f, 0.65f};
            break;
        case MODE_SLOW:
            targetColor = {0.85f, 1.0f, 0.85f};
            break;
        default:
            targetColor = {0.8f, 0.8f, 1.0f};
            break;
    }
    for (int i = 0; i < 3; i++) {
        mBackgroundColor[i] += (targetColor[i] - mBackgroundColor[i]) * 0.1f;
    }
    return mBackgroundColor;
}

void MicroscopeFrameController::paceBeforeDraw(int temperatureMode) {
    // Preserve the existing 60 Hz cap, but spend its idle interval before GL
    // polls the prepared exchange. The preparation worker can then overlap time
    // that was previously slept after an already-stale draw.
    auto now = std::chrono::steady_clock::now();
    if (temperatureMode != MODE_INCUBATE && mFrameStartTime.time_since_epoch().count() != 0) {
        auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - mFrameStartTime);
        if (delta.count() > 0 && delta.count() < MIN_DELTA_NANOS) {
            std::this_thread::sleep_for(
                    std::chrono::nanoseconds(MIN_DELTA_NANOS - delta.count()));
            now = std::chrono::steady_clock::now();
        }
    }
    mFrameStartTime = now;
}
