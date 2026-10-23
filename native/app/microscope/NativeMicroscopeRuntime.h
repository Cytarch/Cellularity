#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include "app/NativeSimulationOwner.h"
#include "app/microscope/MicroscopeCommandMailbox.h"
#include "app/microscope/MicroscopePresentationState.h"
#include "concurrency/LatestValueExchange.h"
#include "core/SimulationWorldStageTiming.h"
#include "render/frame/RenderFrameTypes.h"

class NativeInteractionController;
class NativeRenderSession;
class NativeViewport;

namespace cellularity::microscope {

struct NativeMicroscopeRateTelemetry final {
    simulation::SimulationSchedulerSnapshot scheduler{};
    std::uint64_t actualRateSampleSequence = 0;
    bool actualRateValid = false;
    double actualCompletedUps = 0.0;
    double actualSimulationHoursPerSecond = 0.0;
    simulation::SimulationSchedulerDuration actualRateWindow{};
    std::uint64_t actualRateWindowUpdates = 0;
    std::uint64_t ownerServiceCount = 0;
    std::uint64_t worldInteractionServiceCount = 0;
    simulation::SimulationSchedulerDuration lastOwnerServiceInterval{};
    simulation::SimulationSchedulerDuration maximumOwnerServiceInterval{};
    std::uint64_t publishedSourceFrameCount = 0;
    std::uint64_t droppedSourceFrameCount = 0;
    std::uint64_t publishedPreparedFrameCount = 0;
    std::uint64_t droppedPreparedFrameCount = 0;
    std::uint64_t statusPublishedCount = 0;
    std::uint64_t statusConsumedCount = 0;
    std::uint64_t statusProducerReplacedCount = 0;
    std::uint64_t statusConsumerDiscardedCount = 0;
    std::uint64_t statusPublishRetriesExhausted = 0;
};

struct NativeMicroscopeRuntimeSnapshot final {
    std::uint64_t revision = 0;
    std::uint64_t worldEpoch = 0;
    std::uint64_t completedUpdateCount = 0;
    std::uint64_t lastResolvedInputSequence = 0;
    std::uint64_t appliedWorldRevision = 0;
    std::int64_t environmentRevision = 0;
    int cellCount = 0;
    double foodMass = 0.0;
    double simulationTime = 0.0;
    int temperatureMode = 0;
    int toolOrdinal = 0;
    int selectedCellId = -1;
    bool diagnosisRequested = false;
    bool failed = false;
    NativeMicroscopeRateTelemetry rateTelemetry{};
    SimulationWorldStageTimingSnapshot stageTimings{};
    std::shared_ptr<const std::vector<std::uint8_t>> environmentState;
    std::shared_ptr<const std::vector<std::uint8_t>> selectedCellState;
    std::shared_ptr<const std::vector<std::uint8_t>> selectedGenomeState;
};

static_assert(
        std::is_nothrow_copy_constructible_v<NativeMicroscopeRuntimeSnapshot>,
        "Owner failure publication must remain non-blocking and non-throwing");
static_assert(
        std::is_nothrow_copy_assignable_v<NativeMicroscopeRuntimeSnapshot>,
        "Owner status publication must remain non-blocking and non-throwing");
static_assert(
        std::is_nothrow_move_constructible_v<NativeMicroscopeRuntimeSnapshot> && std::is_nothrow_move_assignable_v<NativeMicroscopeRuntimeSnapshot>,
        "Owner status handoff must remain non-blocking and non-throwing");

// Owns the authoritative microscope update thread. It is the only production
// caller that advances SimulationWorld or applies world-facing microscope commands.
// GLES and Android callbacks consume immutable publications only.
class NativeMicroscopeRuntime final {
public:
    NativeMicroscopeRuntime(NativeSimulationOwner& owner,
            NativeInteractionController& interaction,
            NativeRenderSession& renderSession,
            float cameraBoundsRadius,
            const std::atomic<bool>& performanceHudEnabled);
    ~NativeMicroscopeRuntime();

    NativeMicroscopeRuntime(const NativeMicroscopeRuntime&) = delete;
    NativeMicroscopeRuntime& operator=(const NativeMicroscopeRuntime&) = delete;

    void start();
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] NativeMicroscopeRuntimeSnapshot snapshot() const;

private:
    void run() noexcept;
    void runCycle();
    void syncNewestViewport();
    [[nodiscard]] bool serviceOwner(NativeSimulationOwner::WriteAccess& access,
            bool beforeBatch);
    [[nodiscard]] MicroscopeCommandApplyResult
    applyCommand(NativeSimulationOwner::WriteAccess& access,
            const MicroscopeWorldCommand& command);
    void updateWorldInteraction(NativeSimulationOwner::WriteAccess& access);
    void executeScheduledUpdate(NativeSimulationOwner::WriteAccess& access,
            const MicroscopeSchedulerModeContract& contract);
    void captureScheduledIncubateFrame(
            NativeSimulationOwner::WriteAccess& access,
            const MicroscopeSchedulerModeContract& contract);
    void capture(NativeSimulationOwner::WriteAccess& access,
            render::RenderCaptureStage stage, float margin);
    void recordOwnerServiceOpportunity(MicroscopeCommandTimePoint now) noexcept;
    void
    updateRateTelemetry(NativeSimulationOwner::WriteAccess& access,
            const simulation::SimulationSchedulerSnapshot& scheduler,
            MicroscopeCommandTimePoint now,
            NativeMicroscopeRateTelemetry& telemetry);
    void publishSnapshot(NativeMicroscopeRuntimeSnapshot snapshot) noexcept;
    void publishFailureSnapshot() noexcept;
    void publishStatus(NativeSimulationOwner::WriteAccess& access, bool force);

    NativeSimulationOwner& mOwner;
    NativeInteractionController& mInteraction;
    NativeRenderSession& mRenderSession;
    [[maybe_unused]] const std::atomic<bool>& mPerformanceHudEnabled;
    std::unique_ptr<NativeViewport> mOwnerViewport;
    PresentationViewportSnapshot mViewportSnapshot{};
    NativeMicroscopeRuntimeSnapshot mPublishedSnapshot{};
    mutable concurrency::LatestValueExchange<NativeMicroscopeRuntimeSnapshot>
            mSnapshotExchange;
    mutable std::mutex mSnapshotConsumerMutex;
    mutable NativeMicroscopeRuntimeSnapshot mConsumedSnapshot{};
    std::thread mThread;
    std::atomic<bool> mStopRequested{false};
    std::atomic<bool> mRunning{false};
    bool mCaptureDirty = true;
    std::int64_t mLastStatusPublishNanos = 0;
    std::int64_t mLastInertiaStepNanos = 0;
    std::optional<MicroscopeCommandTimePoint> mActualRateWindowStartedAt;
    std::uint64_t mActualRateWindowStartUpdates = 0;
    double mActualRateWindowStartSimulationHours = 0.0;
    std::uint64_t mActualRatePolicyGeneration = 0;
    std::uint64_t mActualRateSampleSequence = 0;
    bool mActualRateValid = false;
    double mActualCompletedUps = 0.0;
    double mActualSimulationHoursPerSecond = 0.0;
    simulation::SimulationSchedulerDuration mActualRateWindow{};
    std::uint64_t mActualRateWindowUpdates = 0;
    std::optional<MicroscopeCommandTimePoint> mLastOwnerServiceAt;
    std::uint64_t mOwnerServiceCount = 0;
    std::uint64_t mWorldInteractionServiceCount = 0;
    simulation::SimulationSchedulerDuration mLastOwnerServiceInterval{};
    simulation::SimulationSchedulerDuration mMaximumOwnerServiceInterval{};
    std::optional<MicroscopeCommandTimePoint> mNextWakeDeadline;
    bool mWaitAfterCycle = true;
    bool mUrgentCapture = false;
#ifdef CELLULARITY_PERFORMANCE_HUD
    std::int64_t mNextStageTimingSampleNanos = 0;
#endif
};

} // namespace cellularity::microscope
