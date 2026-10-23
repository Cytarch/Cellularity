#include "app/microscope/MicroscopeSimulationState.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/SimulationWorld.h"

namespace cellularity::microscope {
namespace {

constexpr std::int64_t kMaximumLegacyIncubateElapsedNanos = 100000000LL;
constexpr double kMinimumLegacyIncubateTargetUps = 1.0;

} // namespace

bool MicroscopeSimulationState::setTemperatureMode(int temperatureMode) {
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    return setTemperatureModeAt(temperatureMode,
            simulation::simulationSchedulerNow());
#else
    return setTemperatureModeValue(temperatureMode);
#endif
}

bool MicroscopeSimulationState::setTemperatureModeValue(
        int temperatureMode) noexcept {
    if (mTemperatureMode == temperatureMode) {
        return false;
    }
    mTemperatureMode = temperatureMode;
    if (mModeGeneration != std::numeric_limits<std::uint64_t>::max()) {
        ++mModeGeneration;
    }
    resetTiming();
    return true;
}

int MicroscopeSimulationState::temperatureMode() const noexcept {
    return mTemperatureMode;
}

std::uint64_t MicroscopeSimulationState::modeGeneration() const noexcept {
    return mModeGeneration;
}

void MicroscopeSimulationState::setSlowMotionSpeed(
        float slowMotionSpeed) noexcept {
    mSlowMotionSpeed = slowMotionSpeed;
}

float MicroscopeSimulationState::slowMotionSpeed() const noexcept {
    return mSlowMotionSpeed;
}

void MicroscopeSimulationState::setLegacyIncubateTargetUps(
        double targetUps) noexcept {
    if (!std::isfinite(targetUps)) {
        targetUps = 1700.0;
    }
    mLegacyIncubateTargetUps =
            std::max(targetUps, kMinimumLegacyIncubateTargetUps);
}

double MicroscopeSimulationState::legacyIncubateTargetUps() const noexcept {
    return mLegacyIncubateTargetUps;
}

int MicroscopeSimulationState::planLegacyIncubateSteps(
        std::chrono::steady_clock::time_point frameStart) noexcept {
    if (mLastIncubateStepTime.time_since_epoch().count() == 0) {
        mLastIncubateStepTime = frameStart;
    }

    std::int64_t elapsedNanos =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                    frameStart - mLastIncubateStepTime)
                    .count();
    mLastIncubateStepTime = frameStart;
    elapsedNanos = std::clamp<std::int64_t>(elapsedNanos, 0,
            kMaximumLegacyIncubateElapsedNanos);

    mIncubateStepCredit += (static_cast<double>(elapsedNanos) / 1000000000.0) * mLegacyIncubateTargetUps;
    int targetSteps = mIncubateStepCredit >= static_cast<double>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(mIncubateStepCredit);
    return std::max(targetSteps, 1);
}

void MicroscopeSimulationState::completeLegacyIncubateSteps(
        int completedSteps) noexcept {
    mIncubateStepCredit =
            std::max(0.0, mIncubateStepCredit - static_cast<double>(completedSteps));
}

std::int64_t MicroscopeSimulationState::elapsedSinceIncubateCaptureNanos(
        std::chrono::steady_clock::time_point now) const noexcept {
    return mLastIncubateRenderTime.time_since_epoch().count() == 0
            ? std::numeric_limits<std::int64_t>::max()
            : std::chrono::duration_cast<std::chrono::nanoseconds>(
                      now - mLastIncubateRenderTime)
                      .count();
}

void MicroscopeSimulationState::markIncubateCapture(
        std::chrono::steady_clock::time_point capturedAt) noexcept {
    mLastIncubateRenderTime = capturedAt;
}

double MicroscopeSimulationState::dragDeltaTime() const noexcept {
    if (mTemperatureMode == temperatureModeOrdinal(MicroscopeTemperatureMode::Observe)) {
        return SimulationWorld::DELTA_TIME;
    }
    if (mTemperatureMode == temperatureModeOrdinal(MicroscopeTemperatureMode::Slow)) {
        return SimulationWorld::DELTA_TIME * mSlowMotionSpeed;
    }
    return 0.0;
}

void MicroscopeSimulationState::resetForWorldReplacement() noexcept {
    resetTiming();
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    if (mScheduler.has_value()) {
        mScheduler->resetLearningForWorld(
                simulation::simulationSchedulerNow());
    }
#endif
}

void MicroscopeSimulationState::resetTiming() noexcept {
    mLastIncubateRenderTime = {};
    mLastIncubateStepTime = {};
    mIncubateStepCredit = 0.0;
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    resetScheduledSourceCaptureClock();
#endif
}

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
bool MicroscopeSimulationState::setTemperatureModeAt(
        int temperatureMode,
        simulation::SimulationSchedulerTimePoint schedulerNow) {
    if (!setTemperatureModeValue(temperatureMode)) {
        return false;
    }
    applySchedulerPolicy(schedulerNow, true);
    return true;
}

bool MicroscopeSimulationState::setSlowMotionSpeedAt(
        float slowMotionSpeed,
        simulation::SimulationSchedulerTimePoint schedulerNow) noexcept {
    if (mSlowMotionSpeed == slowMotionSpeed) {
        return false;
    }
    mSlowMotionSpeed = slowMotionSpeed;
    if (mTemperatureMode == temperatureModeOrdinal(MicroscopeTemperatureMode::Slow)) {
        // A new simulation delta must not consume rate credit accumulated under
        // the previous delta, even though the TargetRate policy itself is equal.
        applySchedulerPolicy(schedulerNow, true);
    }
    return true;
}

MicroscopeSchedulerModeContract
MicroscopeSimulationState::schedulerModeContract() const {
    switch (mTemperatureMode) {
        case temperatureModeOrdinal(MicroscopeTemperatureMode::Observe):
            return {simulation::SimulationRatePolicy::targetRate(
                            kDisplayPacedSimulationTargetUps),
                    1.0, true, ScheduledSourceCaptureCadence::EveryCompletedUpdate,
                    simulation::SimulationSchedulerDuration::zero()};
        case temperatureModeOrdinal(MicroscopeTemperatureMode::Slow):
            return {simulation::SimulationRatePolicy::targetRate(
                            kDisplayPacedSimulationTargetUps),
                    static_cast<double>(mSlowMotionSpeed), true,
                    ScheduledSourceCaptureCadence::EveryCompletedUpdate,
                    simulation::SimulationSchedulerDuration::zero()};
        case temperatureModeOrdinal(MicroscopeTemperatureMode::Incubate):
            return {
                    simulation::SimulationRatePolicy::maxThroughput(), 1.0, false,
                    ScheduledSourceCaptureCadence::FixedInterval,
                    simulation::SimulationSchedulerDuration{kIncubateCaptureIntervalNanos}
            };
        case temperatureModeOrdinal(MicroscopeTemperatureMode::IncubateNoRender):
            return {simulation::SimulationRatePolicy::maxThroughput(), 1.0, false,
                    ScheduledSourceCaptureCadence::Disabled,
                    simulation::SimulationSchedulerDuration::zero()};
        case temperatureModeOrdinal(MicroscopeTemperatureMode::Freeze):
        default:
            return {simulation::SimulationRatePolicy::paused(), 1.0, true,
                    ScheduledSourceCaptureCadence::DirtyOnly,
                    simulation::SimulationSchedulerDuration::zero()};
    }
}

bool MicroscopeSimulationState::setSchedulerLifecyclePaused(
        bool paused, simulation::SimulationSchedulerTimePoint schedulerNow) {
    if (mSchedulerLifecyclePaused == paused) {
        return false;
    }
    mSchedulerLifecyclePaused = paused;
    resetTiming();
    applySchedulerPolicy(schedulerNow, true);
    return true;
}

bool MicroscopeSimulationState::schedulerLifecyclePaused() const noexcept {
    return mSchedulerLifecyclePaused;
}

bool MicroscopeSimulationState::scheduledSourceCaptureDue(
        simulation::SimulationSchedulerTimePoint now) const {
    if (mSchedulerLifecyclePaused) {
        return false;
    }
    const MicroscopeSchedulerModeContract contract = schedulerModeContract();
    switch (contract.sourceCaptureCadence) {
        case ScheduledSourceCaptureCadence::EveryCompletedUpdate:
            return true;
        case ScheduledSourceCaptureCadence::FixedInterval:
            if (!mHasScheduledSourceCaptureAttempt) {
                return true;
            }
            return now >= mLastScheduledSourceCaptureAttempt && now - mLastScheduledSourceCaptureAttempt >= contract.minimumSourceCaptureInterval;
        case ScheduledSourceCaptureCadence::DirtyOnly:
        case ScheduledSourceCaptureCadence::Disabled:
            return false;
    }
    return false;
}

void MicroscopeSimulationState::markScheduledSourceCaptureAttempt(
        simulation::SimulationSchedulerTimePoint attemptedAt) noexcept {
    if (!mHasScheduledSourceCaptureAttempt || attemptedAt > mLastScheduledSourceCaptureAttempt) {
        mLastScheduledSourceCaptureAttempt = attemptedAt;
    }
    mHasScheduledSourceCaptureAttempt = true;
}

void MicroscopeSimulationState::enableScheduler(
        const simulation::SimulationSchedulerLimits& limits,
        simulation::SimulationSchedulerTimePoint initialTime) {
    mScheduler.emplace(limits, initialTime);
    resetScheduledSourceCaptureClock();
    applySchedulerPolicy(initialTime, false);
}

void MicroscopeSimulationState::disableScheduler() noexcept {
    mScheduler.reset();
    resetScheduledSourceCaptureClock();
}

simulation::SimulationScheduler*
MicroscopeSimulationState::scheduler() noexcept {
    return mScheduler.has_value() ? &*mScheduler : nullptr;
}

const simulation::SimulationScheduler*
MicroscopeSimulationState::scheduler() const noexcept {
    return mScheduler.has_value() ? &*mScheduler : nullptr;
}

std::optional<simulation::SimulationSchedulerSnapshot>
MicroscopeSimulationState::schedulerSnapshot() const noexcept {
    return mScheduler.has_value()
            ? std::optional<simulation::SimulationSchedulerSnapshot>(
                      mScheduler->snapshot())
            : std::nullopt;
}

void MicroscopeSimulationState::applySchedulerPolicy(
        simulation::SimulationSchedulerTimePoint schedulerNow,
        bool restartUnchangedPolicy) {
    if (!mScheduler.has_value()) {
        return;
    }
    const bool policyChanged = mScheduler->setPolicy(
            mSchedulerLifecyclePaused ? simulation::SimulationRatePolicy::paused()
                                      : schedulerModeContract().ratePolicy,
            schedulerNow);
    if (restartUnchangedPolicy && !policyChanged) {
        // Observe and Slow intentionally share TargetRate(60), but switching speed
        // is still a semantic discontinuity and must not inherit old rate credit.
        mScheduler->restartPolicyTimeline(schedulerNow);
    }
}

void MicroscopeSimulationState::resetScheduledSourceCaptureClock() noexcept {
    mLastScheduledSourceCaptureAttempt = {};
    mHasScheduledSourceCaptureAttempt = false;
}
#endif

} // namespace cellularity::microscope
