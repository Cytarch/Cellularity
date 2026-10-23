#pragma once

#include <chrono>
#include <cstdint>

#include "app/microscope/MicroscopeTemperatureMode.h"

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
#include <optional>

#include "app/simulation/SimulationScheduler.h"
#endif

namespace cellularity::microscope {

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
enum class ScheduledSourceCaptureCadence : std::uint8_t {
    DirtyOnly = 0,
    EveryCompletedUpdate = 1,
    FixedInterval = 2,
    Disabled = 3,
};

struct MicroscopeSchedulerModeContract final {
    simulation::SimulationRatePolicy ratePolicy;
    double updateSpeed;
    bool updateForRendering;
    ScheduledSourceCaptureCadence sourceCaptureCadence;
    simulation::SimulationSchedulerDuration minimumSourceCaptureInterval;
};
#endif

// Mutable simulation cadence belongs to the world owner. Presentation keeps
// only a mode snapshot; it cannot advance this clock or change update credit.
class MicroscopeSimulationState final {
public:
    bool setTemperatureMode(int temperatureMode);
    [[nodiscard]] int temperatureMode() const noexcept;
    [[nodiscard]] std::uint64_t modeGeneration() const noexcept;

    void setSlowMotionSpeed(float slowMotionSpeed) noexcept;
    [[nodiscard]] float slowMotionSpeed() const noexcept;
    void setLegacyIncubateTargetUps(double targetUps) noexcept;
    [[nodiscard]] double legacyIncubateTargetUps() const noexcept;

    [[nodiscard]] int planLegacyIncubateSteps(
            std::chrono::steady_clock::time_point frameStart) noexcept;
    void completeLegacyIncubateSteps(int completedSteps) noexcept;
    [[nodiscard]] std::int64_t elapsedSinceIncubateCaptureNanos(
            std::chrono::steady_clock::time_point now) const noexcept;
    void markIncubateCapture(
            std::chrono::steady_clock::time_point capturedAt) noexcept;
    [[nodiscard]] double dragDeltaTime() const noexcept;

    void resetForWorldReplacement() noexcept;

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    bool
    setTemperatureModeAt(int temperatureMode,
            simulation::SimulationSchedulerTimePoint schedulerNow);
    bool setSlowMotionSpeedAt(
            float slowMotionSpeed,
            simulation::SimulationSchedulerTimePoint schedulerNow) noexcept;
    [[nodiscard]] MicroscopeSchedulerModeContract schedulerModeContract() const;
    bool setSchedulerLifecyclePaused(
            bool paused, simulation::SimulationSchedulerTimePoint schedulerNow);
    [[nodiscard]] bool schedulerLifecyclePaused() const noexcept;
    // Query only at a completed-update safe point. Record every capture attempt,
    // including a dropped latest-wins publication, so a late consumer cannot
    // create capture debt.
    [[nodiscard]] bool
    scheduledSourceCaptureDue(simulation::SimulationSchedulerTimePoint now) const;
    void markScheduledSourceCaptureAttempt(
            simulation::SimulationSchedulerTimePoint attemptedAt) noexcept;

    void
    enableScheduler(const simulation::SimulationSchedulerLimits& limits,
            simulation::SimulationSchedulerTimePoint initialTime);
    void disableScheduler() noexcept;
    [[nodiscard]] simulation::SimulationScheduler* scheduler() noexcept;
    [[nodiscard]] const simulation::SimulationScheduler*
    scheduler() const noexcept;
    [[nodiscard]] std::optional<simulation::SimulationSchedulerSnapshot>
    schedulerSnapshot() const noexcept;
#endif

private:
    bool setTemperatureModeValue(int temperatureMode) noexcept;
    void resetTiming() noexcept;

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    void
    applySchedulerPolicy(simulation::SimulationSchedulerTimePoint schedulerNow,
            bool restartUnchangedPolicy);
    void resetScheduledSourceCaptureClock() noexcept;
#endif

    int mTemperatureMode = 0;
    std::uint64_t mModeGeneration = 1;
    float mSlowMotionSpeed = 0.05f;
    std::chrono::steady_clock::time_point mLastIncubateRenderTime{};
    std::chrono::steady_clock::time_point mLastIncubateStepTime{};
    double mIncubateStepCredit = 0.0;
    double mLegacyIncubateTargetUps = 1700.0;
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    std::optional<simulation::SimulationScheduler> mScheduler;
    simulation::SimulationSchedulerTimePoint mLastScheduledSourceCaptureAttempt{};
    bool mSchedulerLifecyclePaused = false;
    bool mHasScheduledSourceCaptureAttempt = false;
#endif
};

} // namespace cellularity::microscope
