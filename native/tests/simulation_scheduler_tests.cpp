#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

#include "app/microscope/MicroscopeCommandMailbox.h"
#include "app/simulation/SimulationScheduler.h"
#include "tests/support/TestAssertions.h"

using cellularity::microscope::MicroscopeCommandMailbox;
using cellularity::simulation::executeSimulationBatch;
using cellularity::simulation::executeSimulationServiceCycle;
using cellularity::simulation::SimulationBatchStopReason;
using cellularity::simulation::SimulationRatePolicy;
using cellularity::simulation::SimulationRatePolicyKind;
using cellularity::simulation::SimulationScheduler;
using cellularity::simulation::SimulationSchedulerDuration;
using cellularity::simulation::SimulationSchedulerLimits;
using cellularity::simulation::SimulationSchedulerTimePoint;
using cellularity::simulation::SimulationServicePhase;
using cellularity::tests::assertNear;
using cellularity::tests::assertThrows;
using cellularity::tests::assertTrue;

namespace {
using namespace std::chrono_literals;

static_assert(!std::is_default_constructible_v<SimulationSchedulerLimits>);
static_assert(!std::is_default_constructible_v<SimulationScheduler>);
static_assert(!std::is_copy_constructible_v<SimulationScheduler>);
static_assert(!std::is_move_constructible_v<SimulationScheduler>);

SimulationSchedulerLimits schedulerLimits() {
    return SimulationSchedulerLimits(10ms, 100ms, 1ms, 4096, 1.25);
}

SimulationSchedulerTimePoint timePoint(SimulationSchedulerDuration elapsed) {
    return SimulationSchedulerTimePoint{} + elapsed;
}

void recordObserved(SimulationScheduler& scheduler,
        const cellularity::simulation::SimulationBatchPlan& plan,
        std::uint32_t updates,
        SimulationSchedulerDuration duration) {
    scheduler.recordObservedBatch(
            plan, updates, plan.issuedAt, plan.issuedAt + duration,
            updates == plan.updateQuota
                    ? SimulationBatchStopReason::QuotaCompleted
                    : SimulationBatchStopReason::ServiceDeadlineReached);
}

void testPoliciesCannotEncodeMagicMaxTarget() {
    const auto paused = SimulationRatePolicy::paused();
    const auto maximum = SimulationRatePolicy::maxThroughput();
    const auto target = SimulationRatePolicy::targetRate(120.0);

    assertTrue(paused.kind() == SimulationRatePolicyKind::Paused && !paused.requestedTargetUps().has_value(),
            "Paused scheduler policy has no requested UPS");
    assertTrue(maximum.kind() == SimulationRatePolicyKind::MaxThroughput && !maximum.requestedTargetUps().has_value(),
            "MaxThroughput cannot contain a numeric target");
    assertTrue(target.kind() == SimulationRatePolicyKind::TargetRate && target.requestedTargetUps().has_value() && *target.requestedTargetUps() == 120.0,
            "Only TargetRate owns a requested UPS");
    assertThrows([] {
        (void)SimulationRatePolicy::targetRate(0.0);
    },
            "Zero TargetRate is rejected");
    assertThrows(
            [] {
                (void)SimulationRatePolicy::targetRate(
                        std::numeric_limits<double>::infinity());
            },
            "Infinite TargetRate is rejected");
    assertThrows(
            [] {
                (void)SimulationRatePolicy::targetRate(
                        std::numeric_limits<double>::quiet_NaN());
            },
            "NaN TargetRate is rejected");
}

void testLimitsRequireExplicitFiniteLatencyContract() {
    assertThrows(
            [] {
                (void)SimulationSchedulerLimits(0ns, 100ms, 1ms, 16, 1.25);
            },
            "Zero owner service budget is rejected");
    assertThrows(
            [] {
                (void)SimulationSchedulerLimits(10ms, 0ns, 1ms, 16, 1.25);
            },
            "Zero target lag limit is rejected");
    assertThrows(
            [] {
                (void)SimulationSchedulerLimits(10ms, 100ms, 11ms, 16, 1.25);
            },
            "Clock interval cannot exceed service budget");
    assertThrows(
            [] {
                (void)SimulationSchedulerLimits(10ms, 100ms, 1ms, 0, 1.25);
            },
            "Zero hard update cap is rejected");
    assertThrows(
            [] {
                (void)SimulationSchedulerLimits(10ms, 100ms, 1ms, 16, 0.99);
            },
            "Unsafe update-cost margin is rejected");
    assertThrows(
            [] {
                (void)SimulationSchedulerLimits(
                        10ms, 100ms, 1ms, 16, std::numeric_limits<double>::infinity());
            },
            "Infinite update-cost margin is rejected");
}

void testMaxThroughputUsesDeadlineBoundedQuotaAndLearnsClockStride() {
    SimulationScheduler scheduler(schedulerLimits(), timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(0ns));

    const auto coldPlan = scheduler.planNextBatch(timePoint(0ns), false);
    assertTrue(coldPlan.updateQuota == 1 && coldPlan.clockCheckStride == 1 && !coldPlan.wakeDeadline.has_value(),
            "Uncalibrated MaxThroughput times one update before expanding to "
            "the finite deadline-bounded safety quota");
    recordObserved(scheduler, coldPlan, 1, 100us);

    auto snapshot = scheduler.snapshot();
    assertTrue(snapshot.updateCostInitialized && snapshot.fastUpdateCost == 100us && snapshot.slowUpdateCost == 100us && snapshot.estimatedUpdateCost == 125us && !snapshot.requestedTargetUps.has_value(),
            "Live observation initializes both estimates without a target");

    const auto fastPlan =
            scheduler.planNextBatch(coldPlan.issuedAt + 100us, false);
    assertTrue(fastPlan.updateQuota == 4096 && fastPlan.clockCheckStride == 8,
            "Fast world expands clock-check stride without turning learned "
            "cost into a MaxThroughput rate limiter");
    recordObserved(scheduler, fastPlan, 80, 8ms);

    const auto beforeHeavy =
            scheduler.planNextBatch(fastPlan.issuedAt + 8ms, false);
    recordObserved(scheduler, beforeHeavy, 4, 8ms);
    const auto heavyPlan =
            scheduler.planNextBatch(beforeHeavy.issuedAt + 8ms, false);
    assertTrue(heavyPlan.updateQuota == 4096 && heavyPlan.clockCheckStride == 1,
            "A sudden expensive world immediately increases clock checks "
            "without collapsing the deadline-bounded maximum batch");

    recordObserved(scheduler, heavyPlan, 1, 50us);
    auto recoveringPlan =
            scheduler.planNextBatch(heavyPlan.issuedAt + 50us, false);
    assertTrue(recoveringPlan.updateQuota == 4096 && recoveringPlan.clockCheckStride == 1,
            "A single cheap sample cannot instantly relax conservative "
            "clock checks");

    auto now = recoveringPlan.issuedAt;
    for (int sample = 0; sample < 96; ++sample) {
        recordObserved(scheduler, recoveringPlan, 1, 50us);
        now += 50us;
        recoveringPlan = scheduler.planNextBatch(now, false);
    }
    assertTrue(recoveringPlan.updateQuota == 4096 && recoveringPlan.clockCheckStride >= 15 && recoveringPlan.clockCheckStride <= 16,
            "Sustained cheap updates gradually reduce clock-read overhead "
            "while the deadline remains authoritative");

    scheduler.resetLearningForWorld(now);
    const auto resetPlan = scheduler.planNextBatch(now, false);
    snapshot = scheduler.snapshot();
    assertTrue(resetPlan.updateQuota == 1 && resetPlan.clockCheckStride == 1 && !snapshot.updateCostInitialized && snapshot.learningResetCount == 1 && snapshot.policy == SimulationRatePolicyKind::MaxThroughput,
            "A new world relearns clock cadence locally without changing or "
            "throttling max policy");
}

void testHardCapIsSafetyOnly() {
    const SimulationSchedulerLimits limits(10ms, 100ms, 1ms, 32, 1.0);
    SimulationScheduler scheduler(limits, timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(0ns));
    const auto coldPlan = scheduler.planNextBatch(timePoint(0ns), false);
    recordObserved(scheduler, coldPlan, 1, 1ns);
    const auto plan = scheduler.planNextBatch(timePoint(1ns), false);
    assertTrue(plan.updateQuota == 32,
            "Hard count cap bounds a very cheap world without becoming UPS");
}

void testTargetRateUsesBoundedCreditWithoutForcedUpdates() {
    const SimulationSchedulerLimits limits(10ms, 50ms, 1ms, 4096, 1.25);
    SimulationScheduler scheduler(limits, timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::targetRate(100.0), timePoint(0ns));

    const auto initial = scheduler.planNextBatch(timePoint(0ns), false);
    const auto halfTick = scheduler.planNextBatch(timePoint(5ms), false);
    assertTrue(initial.updateQuota == 0 && initial.wakeDeadline.has_value() && halfTick.updateQuota == 0,
            "TargetRate never forces an update before earning one credit");

    const auto firstTick = scheduler.planNextBatch(timePoint(10ms), false);
    assertTrue(firstTick.updateQuota == 1,
            "TargetRate releases one update at its earned deadline");
    recordObserved(scheduler, firstTick, 1, 100us);

    const auto postUpdateWait =
            scheduler.planNextBatch(timePoint(10100us), false);
    assertTrue(postUpdateWait.updateQuota == 0 && postUpdateWait.wakeDeadline == timePoint(20ms),
            "A productive TargetRate batch is followed by an exact no-work "
            "deadline instead of an idle fallback");

    const auto afterLongStall = scheduler.planNextBatch(timePoint(1010ms), false);
    assertTrue(afterLongStall.updateQuota == 5,
            "TargetRate catch-up debt is capped by the explicit lag window");

    scheduler.setPolicy(SimulationRatePolicy::paused(), timePoint(1010ms));
    scheduler.setPolicy(SimulationRatePolicy::targetRate(100.0), timePoint(10s));
    const auto resumed = scheduler.planNextBatch(timePoint(10s), false);
    assertTrue(resumed.updateQuota == 0 && resumed.wakeDeadline.has_value(),
            "Pause and resume cannot create a catch-up storm");
}

void testBothPoliciesShareOneKernelWithPolicySpecificQuota() {
    const auto limits = schedulerLimits();
    SimulationScheduler maximum(limits, timePoint(0ns));
    SimulationScheduler target(limits, timePoint(0ns));
    maximum.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(0ns));
    target.setPolicy(SimulationRatePolicy::targetRate(100000.0), timePoint(0ns));

    auto maxPlan = maximum.planNextBatch(timePoint(100ms), false);
    auto targetPlan = target.planNextBatch(timePoint(100ms), false);
    recordObserved(maximum, maxPlan, 1, 100us);
    recordObserved(target, targetPlan, 1, 100us);

    maxPlan = maximum.planNextBatch(timePoint(101ms), false);
    targetPlan = target.planNextBatch(timePoint(101ms), false);
    assertTrue(maxPlan.updateQuota == 4096 && targetPlan.updateQuota == 80 && targetPlan.clockCheckStride == maxPlan.clockCheckStride,
            "Both policies share learned clock cadence and one execution "
            "kernel while only TargetRate uses an adaptive credit quota");
}

void testBatchKernelStopsForCommandsAndAdaptsClockChecks() {
    const SimulationSchedulerLimits limits(5ms, 100ms, 1ms, 4096, 1.0);
    SimulationScheduler scheduler(limits, timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(0ns));
    const auto seed = scheduler.planNextBatch(timePoint(0ns), false);
    recordObserved(scheduler, seed, 1, 100us);

    auto current = timePoint(100us);
    const auto commandPlan = scheduler.planNextBatch(current, false);
    std::uint32_t updates = 0;
    const auto commandResult = executeSimulationBatch(
            commandPlan,
            [&] {
                ++updates;
                current += 100us;
            },
            [&] {
                return updates >= 7;
            },
            [&] {
                return current;
            });
    assertTrue(commandResult.completedUpdates == 7 && commandResult.stopReason == SimulationBatchStopReason::ServiceRequested,
            "Command notification stops max-throughput after the current "
            "atomic update");
    scheduler.recordObservedBatch(commandPlan, commandResult);

    const auto deadlinePlan = scheduler.planNextBatch(current, false);
    const auto deadlineResult = executeSimulationBatch(
            deadlinePlan, [&] {
                current += 2ms;
            },
            [] {
                return false;
            },
            [&] {
                return current;
            });
    scheduler.recordObservedBatch(deadlinePlan, deadlineResult);
    const auto snapshot = scheduler.snapshot();
    assertTrue(deadlineResult.completedUpdates == 3 && deadlineResult.stopReason == SimulationBatchStopReason::ServiceDeadlineReached && snapshot.deadlineOverrunCount == 1 && snapshot.maximumDeadlineOverrun <= 2ms,
            "Within-batch clock adaptation bounds a sudden cost increase to "
            "one in-flight update");
}

void testMailboxGenerationInterruptsWithoutChannelScan() {
    const SimulationSchedulerLimits limits(5ms, 100ms, 1ms, 4096, 1.0);
    SimulationScheduler scheduler(limits, timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(0ns));
    const auto seed = scheduler.planNextBatch(timePoint(0ns), false);
    recordObserved(scheduler, seed, 1, 100us);

    MicroscopeCommandMailbox mailbox(1);
    const std::uint64_t observed = mailbox.notificationGeneration();
    auto current = timePoint(100us);
    const auto plan = scheduler.planNextBatch(current, false);
    std::uint32_t updates = 0;
    const auto execution = executeSimulationBatch(
            plan,
            [&] {
                ++updates;
                current += 100us;
                if (updates == 7) {
                    assertTrue(mailbox.enqueueToolOrdinal(3).accepted(),
                            "Setting publication used by interruption test is "
                            "accepted");
                }
            },
            [&] {
                return mailbox.hasNotificationSince(observed);
            },
            [&] {
                return current;
            });

    assertTrue(execution.completedUpdates == 7 && execution.stopReason == SimulationBatchStopReason::ServiceRequested && mailbox.hasPendingOwnerWork(),
            "One generation acquire-load interrupts the batch immediately "
            "after the active update without scanning mailbox channels");
}

void testShadowObservationCannotBecomeAuthoritative() {
    SimulationScheduler scheduler(schedulerLimits(), timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::targetRate(100000.0),
            timePoint(0ns));
    const auto plan = scheduler.planNextBatch(timePoint(100ms), false);

    // The observed path remains authoritative and may have executed a different
    // count. Recording it trains the model but cannot execute work.
    recordObserved(scheduler, plan, 12, 1200us);
    const auto snapshot = scheduler.snapshot();
    assertTrue(snapshot.observedBatchCount == 1 && snapshot.observedUpdateCount == 12 && snapshot.totalObservedBatchDuration == 1200us && snapshot.maximumObservedBatchDuration == 1200us && snapshot.lastObservedUpdateCount == 12 && snapshot.maximumObservedUpdateCount == 12 && snapshot.quotaExceededCount == 1 && snapshot.estimatedUpdateCost == 125us,
            "Scheduler telemetry preserves batch count, size, time, and "
            "legacy shadow work without constraining it");
    assertThrows([&] {
        recordObserved(scheduler, plan, 12, 1200us);
    },
            "A shadow observation cannot be recorded twice");
}

void testInvalidObservationsAreRejectedWithoutChangingTelemetry() {
    SimulationScheduler scheduler(schedulerLimits(), timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(0ns));

    const auto stalePlan = scheduler.planNextBatch(timePoint(0ns), false);
    const auto currentPlan = scheduler.planNextBatch(timePoint(1ns), false);
    assertThrows([&] {
        recordObserved(scheduler, stalePlan, 1, 100us);
    },
            "A superseded batch plan cannot update scheduler state");
    recordObserved(scheduler, currentPlan, 1, 100us);

    const auto policyPlan = scheduler.planNextBatch(timePoint(101us), false);
    scheduler.setPolicy(SimulationRatePolicy::paused(), timePoint(101us));
    assertThrows([&] {
        recordObserved(scheduler, policyPlan, 1, 100us);
    },
            "An in-flight plan cannot cross a policy generation");

    scheduler.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(101us));
    const auto timestampPlan = scheduler.planNextBatch(timePoint(101us), false);
    assertThrows(
            [&] {
                scheduler.recordObservedBatch(
                        timestampPlan, 1, timestampPlan.issuedAt - 1ns,
                        timestampPlan.issuedAt + 100us,
                        SimulationBatchStopReason::QuotaCompleted);
            },
            "An observation cannot begin before its plan was issued");

    const auto snapshot = scheduler.snapshot();
    assertTrue(snapshot.observedBatchCount == 1 && snapshot.observedUpdateCount == 1,
            "Rejected observations do not alter scheduler telemetry");
}

void testImmediateServiceProducesNoSimulationWork() {
    SimulationScheduler scheduler(schedulerLimits(), timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(0ns));
    const auto plan = scheduler.planNextBatch(timePoint(0ns), true);
    std::uint32_t updates = 0;
    const auto execution = executeSimulationBatch(
            plan, [&] {
                ++updates;
            },
            [] {
                return false;
            },
            [] {
                return timePoint(0ns);
            });
    assertTrue(plan.updateQuota == 0 && updates == 0 && execution.stopReason == SimulationBatchStopReason::ServiceRequested,
            "Pending service is resolved before another simulation update");
}

void testServiceCycleOrdersServiceAroundUpdateOnlyMeasurement() {
    SimulationScheduler scheduler(schedulerLimits(), timePoint(0ns));
    scheduler.setPolicy(SimulationRatePolicy::maxThroughput(), timePoint(0ns));

    auto current = timePoint(0ns);
    std::vector<char> order;
    std::uint32_t updates = 0;
    bool recordedBeforePostService = false;
    const auto result = executeSimulationServiceCycle(
            scheduler,
            [&](SimulationServicePhase phase) {
                if (phase == SimulationServicePhase::BeforeBatch) {
                    order.push_back('B');
                    current += 5ms;
                    return;
                }
                recordedBeforePostService =
                        scheduler.snapshot().observedBatchCount == 1;
                order.push_back('A');
                current += 50ms;
            },
            [&] {
                order.push_back('U');
                ++updates;
                current += 100us;
            },
            [&] {
                return updates >= 1;
            },
            [&] {
                return current;
            });

    const auto snapshot = scheduler.snapshot();
    assertTrue(order == std::vector<char>({'B', 'U', 'A'}) && recordedBeforePostService,
            "Owner service runs before planning and after recording exactly "
            "one bounded batch");
    assertTrue(result.plan.issuedAt == timePoint(5ms) && result.execution.completedUpdates == 1 && result.execution.elapsed() == 100us && snapshot.lastObservedBatchDuration == 100us && snapshot.totalObservedBatchDuration == 100us && snapshot.maximumObservedBatchDuration == 100us && snapshot.maximumObservedUpdateCount == 1 && snapshot.estimatedUpdateCost == 125us,
            "Scheduler learning measures update execution without pre/post "
            "service time");
    assertTrue(result.plan.policy == SimulationRatePolicyKind::MaxThroughput && !result.plan.wakeDeadline.has_value(),
            "A MaxThroughput service cycle introduces no wake deadline");
}

} // namespace

int main() {
    testPoliciesCannotEncodeMagicMaxTarget();
    testLimitsRequireExplicitFiniteLatencyContract();
    testMaxThroughputUsesDeadlineBoundedQuotaAndLearnsClockStride();
    testHardCapIsSafetyOnly();
    testTargetRateUsesBoundedCreditWithoutForcedUpdates();
    testBothPoliciesShareOneKernelWithPolicySpecificQuota();
    testBatchKernelStopsForCommandsAndAdaptsClockChecks();
    testMailboxGenerationInterruptsWithoutChannelScan();
    testShadowObservationCannotBecomeAuthoritative();
    testInvalidObservationsAreRejectedWithoutChangingTelemetry();
    testImmediateServiceProducesNoSimulationWork();
    testServiceCycleOrdersServiceAroundUpdateOnlyMeasurement();
    std::cout << "native simulation scheduler tests passed\n";
    return 0;
}
