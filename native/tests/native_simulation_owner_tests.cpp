#include "app/NativeSimulationOwner.h"

#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "app/NativeWorldContext.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeFrameController.h"
#include "app/microscope/interaction/MicroscopeSelectionController.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

#if !defined(CELLULARITY_EXPECT_NATIVE_PROFILE)
#error "The simulation-owner ABI contract requires an explicit profile expectation"
#elif CELLULARITY_EXPECT_NATIVE_PROFILE
#if !defined(CELLULARITY_PROFILE) || !defined(CELLULARITY_RENDER_PROFILE)
#error "cellularity_interaction must propagate the enabled simulation profile contract"
#endif
#elif defined(CELLULARITY_PROFILE) || defined(CELLULARITY_RENDER_PROFILE)
#error "cellularity_interaction propagated a profile contract into a Profile-OFF build"
#endif

using namespace cellularity::tests;

static_assert(!std::is_copy_constructible_v<NativeSimulationOwner>);
static_assert(!std::is_move_constructible_v<NativeSimulationOwner>);
static_assert(!std::is_copy_constructible_v<NativeSimulationOwner::ReadAccess>);
static_assert(
        !std::is_copy_constructible_v<NativeSimulationOwner::WriteAccess>);
static_assert(
        std::is_same_v<
                decltype(std::declval<NativeSimulationOwner::ReadAccess&>().world()),
                const SimulationWorld&>);
static_assert(
        std::is_same_v<
                decltype(std::declval<NativeSimulationOwner::WriteAccess&>().world()),
                SimulationWorld&>);

namespace {

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
using namespace std::chrono_literals;

using cellularity::microscope::kDisplayPacedSimulationTargetUps;
using cellularity::microscope::kIncubateCaptureIntervalNanos;
using cellularity::microscope::MicroscopeSimulationState;
using cellularity::microscope::MicroscopeTemperatureMode;
using cellularity::microscope::ScheduledSourceCaptureCadence;
using cellularity::microscope::temperatureModeOrdinal;
using cellularity::simulation::SimulationBatchStopReason;
using cellularity::simulation::SimulationRatePolicyKind;
using cellularity::simulation::SimulationSchedulerDuration;
using cellularity::simulation::SimulationSchedulerLimits;
using cellularity::simulation::SimulationSchedulerTimePoint;
using cellularity::simulation::SimulationServicePhase;

SimulationSchedulerTimePoint
schedulerTimePoint(SimulationSchedulerDuration elapsed) {
    return SimulationSchedulerTimePoint{} + elapsed;
}

SimulationSchedulerLimits schedulerLimits() {
    return SimulationSchedulerLimits(10ms, 100ms, 1ms, 64, 1.25);
}
#endif

Environment testEnvironment() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    environment.mFoodSpeed = 0.0;
    return environment;
}

void testOwnerCombinesWorldSimulationAndInteractionState() {
    NativeSimulationOwner owner(false, testEnvironment());

    std::uint64_t initialEpoch = 0;
    {
        auto access = owner.read();
        initialEpoch = access.worldEpoch();
        assertTrue(initialEpoch != 0, "Simulation owner starts with a world epoch");
        assertTrue(access.completedUpdateCount() == 0,
                "Simulation owner starts with no completed updates");
        assertTrue(access.microscopeSimulation().temperatureMode() == MicroscopeFrameController::MODE_FREEZE,
                "Simulation owner starts with Freeze cadence state");
        assertTrue(access.microscopeInteraction().toolOrdinal() == 0,
                "Simulation owner starts with the default world tool");
    }

    {
        auto access = owner.write();
        access.microscopeSimulation().setTemperatureMode(
                MicroscopeFrameController::MODE_OBSERVE);
        access.microscopeSimulation().setSlowMotionSpeed(0.25f);
        access.microscopeInteraction().setToolOrdinal(3);
        access.update(false, 1.0);
        assertTrue(access.completedUpdateCount() == 1,
                "Updates and their counter mutate under one owner lease");
    }

    {
        auto access = owner.read();
        assertTrue(access.worldEpoch() == initialEpoch,
                "Ordinary updates preserve the world identity domain");
        assertTrue(access.completedUpdateCount() == 1,
                "Read lease observes the owner update counter");
        assertTrue(access.microscopeSimulation().temperatureMode() == MicroscopeFrameController::MODE_OBSERVE && access.microscopeSimulation().slowMotionSpeed() == 0.25f,
                "Read lease observes owner cadence state");
        assertTrue(access.microscopeInteraction().toolOrdinal() == 3,
                "Read lease observes owner world-interaction state");
    }
}

void testWorldReplacementIsOneOwnerSafePoint() {
    NativeSimulationOwner owner(false, testEnvironment());
    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(100, 100);

    std::uint64_t initialEpoch = 0;
    {
        auto access = owner.write();
        initialEpoch = access.worldEpoch();
        const int selectedCell =
                addAliveCell(access.world().mCellData, CellType::PHAGOCYTE);
        access.world().mCellData.mSelectTag[selectedCell] = 1;
        access.microscopeInteraction().selection().syncFromViewport(
                NativeWorldContext{access.world(), viewport});
        assertTrue(access.microscopeInteraction().hasSelectedCell(),
                "Selection state can refer to the current world domain");

        access.microscopeSimulation().setTemperatureMode(
                MicroscopeFrameController::MODE_INCUBATE);
        access.microscopeSimulation().setLegacyIncubateTargetUps(100.0);
        const auto firstTick =
                std::chrono::steady_clock::time_point(std::chrono::seconds(1));
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
        access.microscopeSimulation().enableScheduler(
                cellularity::simulation::SimulationSchedulerLimits(
                        std::chrono::milliseconds(10), std::chrono::milliseconds(100),
                        std::chrono::milliseconds(1), 4096, 1.25),
                std::chrono::time_point_cast<
                        cellularity::simulation::SimulationSchedulerDuration>(firstTick));
#endif
        const int firstBatch =
                access.microscopeSimulation().planLegacyIncubateSteps(firstTick);
        access.microscopeSimulation().completeLegacyIncubateSteps(firstBatch);
        const int accumulatedBatch =
                access.microscopeSimulation().planLegacyIncubateSteps(
                        firstTick + std::chrono::milliseconds(50));
        assertTrue(accumulatedBatch == 5,
                "Pre-reset cadence state contains elapsed update credit");

        assertTrue(access.markEnvironmentChanged() == 1,
                "Environment revision advances before replacement");
        const auto executionBeforeReplacement =
                access.simulationExecutionSnapshot();
        const std::uint64_t replacementEpoch = access.markWorldContentsReplaced();
        assertTrue(replacementEpoch != initialEpoch && replacementEpoch != 0,
                "World replacement advances the owner epoch");
        assertTrue(access.environmentRevision() == 0,
                "World replacement resets environment revision atomically");
        assertTrue(!access.microscopeInteraction().hasSelectedCell(),
                "World replacement retires stale selection state");
        const auto executionAfterReplacement =
                access.simulationExecutionSnapshot();
        assertTrue(executionAfterReplacement.worldResetCount == executionBeforeReplacement.worldResetCount + 1 && executionAfterReplacement.poolConstructionCount == executionBeforeReplacement.poolConstructionCount,
                "World replacement resets execution scratch generation without "
                "serializing or recreating the session pool");
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
        const auto schedulerSnapshot =
                access.microscopeSimulation().schedulerSnapshot();
        assertTrue(schedulerSnapshot.has_value() && schedulerSnapshot->learningResetCount == 1 && !schedulerSnapshot->updateCostInitialized,
                "World replacement restarts scheduler learning in its owner");
#endif

        const int resetBatch =
                access.microscopeSimulation().planLegacyIncubateSteps(
                        firstTick + std::chrono::milliseconds(100));
        assertTrue(resetBatch == 1,
                "World replacement clears cadence time and update credit");
    }
}

void testExecutionContextIsBoundToOwnerSafePoints() {
    NativeSimulationOwner owner(false, testEnvironment());
    auto access = owner.write();
    const auto initial = access.simulationExecutionSnapshot();
    assertTrue(initial.additionalThreadCount == 0 && !initial.ownerThreadBound && !initial.poolMaterialized,
            "Simulation owner starts with a lazy serial execution context");

    access.setSimulationAdditionalThreadCount(1);
    access.bindSimulationExecutionToCurrentThread();
    const auto bound = access.simulationExecutionSnapshot();
    assertTrue(bound.ownerThreadBound && !bound.poolMaterialized,
            "Owner binding does not eagerly create worker threads");

    access.update1WithExecutionContext(false, 1.0);
    bool rejectedInsideSplitUpdate = false;
    try {
        access.setSimulationAdditionalThreadCount(0);
    } catch (const std::logic_error&) {
        rejectedInsideSplitUpdate = true;
    }
    assertTrue(rejectedInsideSplitUpdate,
            "Worker capacity cannot change inside a split world update");
    const std::uint64_t splitWorldEpoch = access.worldEpoch();
    bool replacementRejectedInsideSplitUpdate = false;
    try {
        (void)access.markWorldContentsReplaced();
    } catch (const std::logic_error&) {
        replacementRejectedInsideSplitUpdate = true;
    }
    assertTrue(replacementRejectedInsideSplitUpdate && access.worldEpoch() == splitWorldEpoch,
            "World replacement cannot reset execution scratch inside a "
            "split update");
    access.update2WithExecutionContext(false, 1.0);
    access.setSimulationAdditionalThreadCount(0);
    access.releaseSimulationExecutionFromCurrentThread();
    const auto released = access.simulationExecutionSnapshot();
    assertTrue(!released.ownerThreadBound && !released.poolMaterialized && access.completedUpdateCount() == 1,
            "Owner release keeps completed simulation state and no workers");
}

void testOwnerLeaseSerializesCrossThreadAccess() {
    NativeSimulationOwner owner(false, testEnvironment());
    std::promise<void> attemptStartedPromise;
    std::future<void> attemptStarted = attemptStartedPromise.get_future();
    std::promise<void> readAcquiredPromise;
    std::future<void> readAcquired = readAcquiredPromise.get_future();
    std::thread reader;
    bool blockedWhileOwned = false;

    {
        auto writeAccess = owner.write();
        reader = std::thread([&] {
            attemptStartedPromise.set_value();
            auto readAccess = owner.read();
            (void)readAccess.worldEpoch();
            readAcquiredPromise.set_value();
        });

        attemptStarted.wait();
        blockedWhileOwned = readAcquired.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
        (void)writeAccess.worldEpoch();
    }

    const bool proceededAfterSafePoint =
            readAcquired.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    reader.join();
    assertTrue(blockedWhileOwned,
            "A second thread cannot enter while an owner lease is active");
    assertTrue(proceededAfterSafePoint,
            "A waiting owner lease proceeds after the safe point completes");
}

#if defined(CELLULARITY_SIMULATION_SCHEDULER)
void testSchedulerModeContractsBelongToOwnerState() {
    MicroscopeSimulationState state;
    const auto initial = schedulerTimePoint(1s);

    auto contract = state.schedulerModeContract();
    assertTrue(contract.ratePolicy.kind() == SimulationRatePolicyKind::Paused && contract.updateSpeed == 1.0 && contract.updateForRendering && contract.sourceCaptureCadence == ScheduledSourceCaptureCadence::DirtyOnly,
            "Freeze owns a paused dirty-only scheduler contract");

    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Observe), initial);
    contract = state.schedulerModeContract();
    assertTrue(contract.ratePolicy.kind() == SimulationRatePolicyKind::TargetRate && contract.ratePolicy.requestedTargetUps() == kDisplayPacedSimulationTargetUps && contract.updateSpeed == 1.0 && contract.updateForRendering && contract.sourceCaptureCadence == ScheduledSourceCaptureCadence::EveryCompletedUpdate,
            "Observe owns the display-paced full-speed scheduler contract");

    state.setSlowMotionSpeed(0.25f);
    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Slow), initial);
    contract = state.schedulerModeContract();
    assertTrue(contract.ratePolicy.kind() == SimulationRatePolicyKind::TargetRate && contract.ratePolicy.requestedTargetUps() == kDisplayPacedSimulationTargetUps && contract.updateSpeed == 0.25 && contract.updateForRendering && contract.sourceCaptureCadence == ScheduledSourceCaptureCadence::EveryCompletedUpdate,
            "Slow retains display cadence and supplies its existing speed");

    state.setLegacyIncubateTargetUps(123.0);
    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Incubate), initial);
    contract = state.schedulerModeContract();
    assertTrue(contract.ratePolicy.kind() == SimulationRatePolicyKind::MaxThroughput && !contract.ratePolicy.requestedTargetUps().has_value() && contract.updateSpeed == 1.0 && !contract.updateForRendering && contract.sourceCaptureCadence == ScheduledSourceCaptureCadence::FixedInterval && contract.minimumSourceCaptureInterval == SimulationSchedulerDuration{kIncubateCaptureIntervalNanos},
            "Incubate owns target-free max throughput and soft 30 Hz capture");

    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::IncubateNoRender),
            initial);
    contract = state.schedulerModeContract();
    assertTrue(contract.ratePolicy.kind() == SimulationRatePolicyKind::MaxThroughput && !contract.ratePolicy.requestedTargetUps().has_value() && contract.updateSpeed == 1.0 && !contract.updateForRendering && contract.sourceCaptureCadence == ScheduledSourceCaptureCadence::Disabled,
            "Incubate No Render shares max throughput without source capture");
}

void testOwnerExecutesScheduledPoliciesThroughCommonCycle() {
    const auto initial = schedulerTimePoint(1s);

    {
        NativeSimulationOwner owner(false, testEnvironment());
        auto access = owner.write();
        access.microscopeSimulation().enableScheduler(schedulerLimits(), initial);
        auto current = initial;
        std::vector<SimulationServicePhase> phases;
        const auto result = access.executeScheduledSimulationCycle(
                [&](SimulationServicePhase phase) {
                    phases.push_back(phase);
                },
                [] {
                    return false;
                },
                [&] {
                    return current;
                });
        assertTrue(result.plan.policy == SimulationRatePolicyKind::Paused && result.execution.completedUpdates == 0 && result.execution.stopReason == SimulationBatchStopReason::NoWork && access.completedUpdateCount() == 0 && phases == std::vector<SimulationServicePhase>({SimulationServicePhase::BeforeBatch, SimulationServicePhase::AfterBatch}),
                "Freeze services the owner cycle without an automatic update");
    }

    {
        NativeSimulationOwner owner(false, testEnvironment());
        auto access = owner.write();
        auto& state = access.microscopeSimulation();
        state.enableScheduler(schedulerLimits(), initial);
        state.setTemperatureModeAt(
                temperatureModeOrdinal(MicroscopeTemperatureMode::Observe), initial);
        auto current = initial + 16'666'667ns;
        const auto result = access.executeScheduledSimulationCycle(
                [](SimulationServicePhase) {}, [] {
                    return false;
                },
                [&] {
                    return current;
                });
        assertTrue(result.plan.policy == SimulationRatePolicyKind::TargetRate && result.execution.completedUpdates == 1 && result.plan.wakeDeadline == std::nullopt && access.completedUpdateCount() == 1,
                "Observe consumes exactly one earned 60 UPS credit");
        assertNear(SimulationWorld::DELTA_TIME, access.world().mTime, 1e-12,
                "Observe preserves SimulationWorld delta and speed one");
    }

    {
        NativeSimulationOwner owner(false, testEnvironment());
        auto access = owner.write();
        auto& state = access.microscopeSimulation();
        state.setSlowMotionSpeed(0.25f);
        state.enableScheduler(schedulerLimits(), initial);
        state.setTemperatureModeAt(
                temperatureModeOrdinal(MicroscopeTemperatureMode::Slow), initial);
        auto current = initial + 16'666'667ns;
        const auto result = access.executeScheduledSimulationCycle(
                [](SimulationServicePhase) {}, [] {
                    return false;
                },
                [&] {
                    return current;
                });
        assertTrue(result.plan.policy == SimulationRatePolicyKind::TargetRate && result.execution.completedUpdates == 1 && access.completedUpdateCount() == 1,
                "Slow consumes the same display-paced scheduler credit");
        assertNear(SimulationWorld::DELTA_TIME * 0.25, access.world().mTime, 1e-12,
                "Slow passes its existing speed into SimulationWorld update");
    }

    for (const auto mode : {MicroscopeTemperatureMode::Incubate,
                 MicroscopeTemperatureMode::IncubateNoRender}) {
        NativeSimulationOwner owner(false, testEnvironment());
        auto access = owner.write();
        auto& state = access.microscopeSimulation();
        state.enableScheduler(schedulerLimits(), initial);
        state.setTemperatureModeAt(temperatureModeOrdinal(mode), initial);
        auto current = initial;
        const auto result = access.executeScheduledSimulationCycle(
                [](SimulationServicePhase) {},
                [&] {
                    return access.completedUpdateCount() >= 1;
                },
                [&] {
                    return current;
                });
        assertTrue(result.plan.policy == SimulationRatePolicyKind::MaxThroughput && !result.plan.wakeDeadline.has_value() && result.execution.completedUpdates == 1 && access.completedUpdateCount() == 1,
                "Both Incubate modes start target-free max throughput through "
                "the common owner cycle");
        assertNear(SimulationWorld::DELTA_TIME, access.world().mTime, 1e-12,
                "Both Incubate modes preserve speed one");
    }
}

void testOwnerServicesModeCommandBeforePlanning() {
    NativeSimulationOwner owner(false, testEnvironment());
    auto access = owner.write();
    auto& state = access.microscopeSimulation();
    const auto initial = schedulerTimePoint(1s);
    state.enableScheduler(schedulerLimits(), initial);

    auto current = initial;
    bool commandApplied = false;
    const auto result = access.executeScheduledSimulationCycle(
            [&](SimulationServicePhase phase) {
                if (phase == SimulationServicePhase::BeforeBatch) {
                    commandApplied = state.setTemperatureModeAt(
                            temperatureModeOrdinal(MicroscopeTemperatureMode::Incubate),
                            current);
                }
            },
            [] {
                return false;
            },
            [&] {
                return current;
            });

    assertTrue(commandApplied && result.plan.policy == SimulationRatePolicyKind::MaxThroughput && result.execution.completedUpdates == 1,
            "A pending mode command is applied before the next batch plan");
}

void testOwnerScheduledBatchStopsForServiceNotification() {
    NativeSimulationOwner owner(false, testEnvironment());
    auto access = owner.write();
    auto& state = access.microscopeSimulation();
    const auto initial = schedulerTimePoint(1s);
    state.enableScheduler(schedulerLimits(), initial);
    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Incubate), initial);

    auto current = initial;
    std::uint64_t updatesAccountedByClock = 0;
    auto measuredNow = [&] {
        const std::uint64_t completed = access.completedUpdateCount();
        current +=
                100us * static_cast<std::int64_t>(completed - updatesAccountedByClock);
        updatesAccountedByClock = completed;
        return current;
    };
    const auto seed = access.executeScheduledSimulationCycle(
            [](SimulationServicePhase) {}, [] {
                return false;
            },
            measuredNow);
    assertTrue(seed.execution.completedUpdates == 1,
            "The cold max-throughput cycle measures one update");

    const std::uint64_t beforeBatch = access.completedUpdateCount();
    std::vector<SimulationServicePhase> phases;
    const auto interrupted = access.executeScheduledSimulationCycle(
            [&](SimulationServicePhase phase) {
                phases.push_back(phase);
            },
            [&] {
                return access.completedUpdateCount() >= beforeBatch + 3;
            },
            measuredNow);
    const auto snapshot = state.schedulerSnapshot();
    assertTrue(interrupted.execution.completedUpdates == 3 && interrupted.execution.stopReason == SimulationBatchStopReason::ServiceRequested && access.completedUpdateCount() == beforeBatch + 3,
            "A command notification ends the batch after its current update");
    assertTrue(
            phases == std::vector<SimulationServicePhase>({SimulationServicePhase::BeforeBatch, SimulationServicePhase::AfterBatch}) && snapshot.has_value() && snapshot->observedUpdateCount == access.completedUpdateCount(),
            "Interrupted work is recorded before the post-batch service point");
}

void testSchedulerLifecyclePauseHasNoCatchUp() {
    NativeSimulationOwner owner(false, testEnvironment());
    auto access = owner.write();
    auto& state = access.microscopeSimulation();
    const auto initial = schedulerTimePoint(1s);
    state.enableScheduler(schedulerLimits(), initial);
    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Observe), initial);

    assertTrue(state.setSchedulerLifecyclePaused(true, initial + 5ms),
            "Lifecycle pause changes the owner scheduler state");
    auto current = initial + 10s;
    const auto paused = access.executeScheduledSimulationCycle(
            [](SimulationServicePhase) {}, [] {
                return false;
            },
            [&] {
                return current;
            });
    assertTrue(paused.plan.policy == SimulationRatePolicyKind::Paused && paused.execution.completedUpdates == 0 && access.completedUpdateCount() == 0,
            "Background wall time creates no automatic simulation work");

    assertTrue(state.setSchedulerLifecyclePaused(false, current),
            "Lifecycle resume restores the current mode policy");
    const auto resumed = access.executeScheduledSimulationCycle(
            [](SimulationServicePhase) {}, [] {
                return false;
            },
            [&] {
                return current;
            });
    assertTrue(resumed.plan.policy == SimulationRatePolicyKind::TargetRate && resumed.execution.completedUpdates == 0 && resumed.plan.wakeDeadline.has_value(),
            "Resume starts TargetRate with zero accumulated credit");

    current += 16'666'667ns;
    const auto firstEarned = access.executeScheduledSimulationCycle(
            [](SimulationServicePhase) {}, [] {
                return false;
            },
            [&] {
                return current;
            });
    assertTrue(firstEarned.execution.completedUpdates == 1 && access.completedUpdateCount() == 1,
            "Only post-resume wall time earns the first Observe update");

    state.setSchedulerLifecyclePaused(true, current);
    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Incubate), current);
    auto snapshot = state.schedulerSnapshot();
    assertTrue(snapshot.has_value() && snapshot->policy == SimulationRatePolicyKind::Paused,
            "A mode change cannot restart updates while lifecycle-paused");
    state.setSchedulerLifecyclePaused(false, current);
    snapshot = state.schedulerSnapshot();
    assertTrue(snapshot.has_value() && snapshot->policy == SimulationRatePolicyKind::MaxThroughput && !snapshot->requestedTargetUps.has_value(),
            "Resume selects the latest mode without restoring stale credit");
}

void testEquivalentTargetModeChangeResetsCredit() {
    MicroscopeSimulationState state;
    const auto initial = schedulerTimePoint(1s);
    state.enableScheduler(schedulerLimits(), initial);
    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Observe), initial);

    auto* scheduler = state.scheduler();
    assertTrue(scheduler != nullptr, "Mode-credit test has a scheduler instance");
    const auto seed = scheduler->planNextBatch(initial + 16'666'667ns, false);
    scheduler->recordObservedBatch(seed, 1, seed.issuedAt, seed.issuedAt + 100us,
            SimulationBatchStopReason::QuotaCompleted);
    const auto switchTime = initial + 100ms;
    const auto credited = scheduler->planNextBatch(switchTime, false);
    assertTrue(credited.updateQuota > 1,
            "Observe accumulates bounded target credit before a mode switch");

    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Slow), switchTime);
    const auto afterSwitch = scheduler->planNextBatch(switchTime, false);
    const auto snapshot = scheduler->snapshot();
    assertTrue(afterSwitch.policy == SimulationRatePolicyKind::TargetRate && afterSwitch.updateQuota == 0 && afterSwitch.wakeDeadline.has_value() && snapshot.updateCostInitialized && snapshot.targetCredit == 0.0,
            "Observe-to-Slow discards old credit but preserves live cost "
            "learning despite sharing TargetRate");
}

void testSlowSpeedChangeResetsCredit() {
    MicroscopeSimulationState state;
    const auto initial = schedulerTimePoint(1s);
    state.enableScheduler(schedulerLimits(), initial);
    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Slow), initial);

    auto* scheduler = state.scheduler();
    assertTrue(scheduler != nullptr,
            "Slow-speed credit test has a scheduler instance");
    const auto seed = scheduler->planNextBatch(initial + 16'666'667ns, false);
    scheduler->recordObservedBatch(seed, 1, seed.issuedAt, seed.issuedAt + 100us,
            SimulationBatchStopReason::QuotaCompleted);
    const auto changeTime = initial + 100ms;
    const auto credited = scheduler->planNextBatch(changeTime, false);
    assertTrue(credited.updateQuota > 1,
            "Slow accumulates bounded target credit before speed changes");

    assertTrue(state.setSlowMotionSpeedAt(0.25f, changeTime),
            "A different Slow multiplier is a semantic scheduler change");
    const auto afterChange = scheduler->planNextBatch(changeTime, false);
    const auto snapshot = scheduler->snapshot();
    assertTrue(afterChange.policy == SimulationRatePolicyKind::TargetRate && afterChange.updateQuota == 0 && afterChange.wakeDeadline.has_value() && snapshot.updateCostInitialized && snapshot.targetCredit == 0.0 && state.schedulerModeContract().updateSpeed == 0.25,
            "Slow speed change discards old credit while preserving live "
            "cost learning");
}

void testScheduledSourceCaptureCadenceHasNoDebt() {
    MicroscopeSimulationState state;
    const auto initial = schedulerTimePoint(1s);
    const auto interval =
            SimulationSchedulerDuration{kIncubateCaptureIntervalNanos};
    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Incubate), initial);

    assertTrue(state.scheduledSourceCaptureDue(initial),
            "Incubate may publish its first completed state immediately");
    const auto firstCaptureCompleted = initial + 5ms;
    state.markScheduledSourceCaptureAttempt(firstCaptureCompleted);
    assertTrue(
            !state.scheduledSourceCaptureDue(firstCaptureCompleted + interval - 1ns) && state.scheduledSourceCaptureDue(firstCaptureCompleted + interval),
            "Incubate source cadence is capped from capture completion so an "
            "expensive source copy cannot create back-to-back debt");

    const auto lateAttempt = initial + interval * 10;
    state.markScheduledSourceCaptureAttempt(lateAttempt);
    assertTrue(
            !state.scheduledSourceCaptureDue(lateAttempt) && !state.scheduledSourceCaptureDue(lateAttempt + interval - 1ns) && state.scheduledSourceCaptureDue(lateAttempt + interval),
            "A late or dropped capture schedules from its latest attempt "
            "without catch-up debt");

    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::IncubateNoRender),
            lateAttempt);
    assertTrue(!state.scheduledSourceCaptureDue(lateAttempt + interval),
            "Incubate No Render never requests a source frame");

    state.setTemperatureModeAt(
            temperatureModeOrdinal(MicroscopeTemperatureMode::Observe), lateAttempt);
    assertTrue(state.scheduledSourceCaptureDue(lateAttempt),
            "Observe permits capture after every completed update");
    state.setSchedulerLifecyclePaused(true, lateAttempt);
    assertTrue(!state.scheduledSourceCaptureDue(lateAttempt + interval),
            "Lifecycle pause suppresses scheduled source publication");
}
#endif

} // namespace

int main() {
    testOwnerCombinesWorldSimulationAndInteractionState();
    testWorldReplacementIsOneOwnerSafePoint();
    testExecutionContextIsBoundToOwnerSafePoints();
    testOwnerLeaseSerializesCrossThreadAccess();
#if defined(CELLULARITY_SIMULATION_SCHEDULER)
    testSchedulerModeContractsBelongToOwnerState();
    testOwnerExecutesScheduledPoliciesThroughCommonCycle();
    testOwnerServicesModeCommandBeforePlanning();
    testOwnerScheduledBatchStopsForServiceNotification();
    testSchedulerLifecyclePauseHasNoCatchUp();
    testEquivalentTargetModeChangeResetsCredit();
    testSlowSpeedChangeResetsCredit();
    testScheduledSourceCaptureCadenceHasNoDebt();
#endif
    std::cout << "native simulation owner tests passed\n";
    return 0;
}
