#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace cellularity::simulation {

using SimulationSchedulerDuration = std::chrono::nanoseconds;
using SimulationSchedulerTimePoint =
        std::chrono::time_point<std::chrono::steady_clock,
                SimulationSchedulerDuration>;

[[nodiscard]] SimulationSchedulerTimePoint simulationSchedulerNow() noexcept;

enum class SimulationRatePolicyKind : std::uint8_t {
    Paused = 0,
    TargetRate = 1,
    MaxThroughput = 2,
};

// A max-throughput policy deliberately has no numeric target. Keeping target
// data behind this type prevents a sentinel, infinity, or a device calibration
// from silently turning MaxThroughput back into TargetRate.
class SimulationRatePolicy final {
public:
    [[nodiscard]] static SimulationRatePolicy paused() noexcept;
    [[nodiscard]] static SimulationRatePolicy maxThroughput() noexcept;
    [[nodiscard]] static SimulationRatePolicy targetRate(double targetUps);

    [[nodiscard]] SimulationRatePolicyKind kind() const noexcept;
    [[nodiscard]] std::optional<double> requestedTargetUps() const noexcept;

    [[nodiscard]] bool
    operator==(const SimulationRatePolicy& other) const noexcept;
    [[nodiscard]] bool
    operator!=(const SimulationRatePolicy& other) const noexcept;

private:
    SimulationRatePolicy(SimulationRatePolicyKind kind,
            std::optional<double> targetUps) noexcept;

    SimulationRatePolicyKind mKind;
    std::optional<double> mTargetUps;
};

// Every limit is supplied by the caller. This class has no hardware table,
// persisted calibration, or benchmark-derived production default.
class SimulationSchedulerLimits final {
public:
    SimulationSchedulerLimits(
            SimulationSchedulerDuration ownerServiceBudget,
            SimulationSchedulerDuration targetLagLimit,
            SimulationSchedulerDuration maximumClockCheckInterval,
            std::uint32_t hardBatchUpdateLimit, double updateCostSafetyMargin);

    [[nodiscard]] SimulationSchedulerDuration ownerServiceBudget() const noexcept;
    [[nodiscard]] SimulationSchedulerDuration targetLagLimit() const noexcept;
    [[nodiscard]] SimulationSchedulerDuration
    maximumClockCheckInterval() const noexcept;
    [[nodiscard]] std::uint32_t hardBatchUpdateLimit() const noexcept;
    [[nodiscard]] double updateCostSafetyMargin() const noexcept;

private:
    SimulationSchedulerDuration mOwnerServiceBudget;
    SimulationSchedulerDuration mTargetLagLimit;
    SimulationSchedulerDuration mMaximumClockCheckInterval;
    std::uint32_t mHardBatchUpdateLimit;
    double mUpdateCostSafetyMargin;
};

struct SimulationBatchPlan final {
    std::uint64_t sequence = 0;
    std::uint64_t policyGeneration = 0;
    SimulationRatePolicyKind policy = SimulationRatePolicyKind::Paused;
    SimulationSchedulerTimePoint issuedAt{};
    SimulationSchedulerTimePoint serviceDeadline{};
    std::optional<SimulationSchedulerTimePoint> wakeDeadline;
    SimulationSchedulerDuration maximumClockCheckInterval{};
    std::uint32_t updateQuota = 0;
    std::uint32_t clockCheckStride = 0;
    bool immediateServiceRequested = false;
};

enum class SimulationBatchStopReason : std::uint8_t {
    NoWork = 0,
    QuotaCompleted = 1,
    ServiceRequested = 2,
    ServiceDeadlineReached = 3,
};

struct SimulationBatchExecution final {
    std::uint32_t completedUpdates = 0;
    SimulationSchedulerTimePoint startedAt{};
    SimulationSchedulerTimePoint finishedAt{};
    SimulationBatchStopReason stopReason = SimulationBatchStopReason::NoWork;

    [[nodiscard]] SimulationSchedulerDuration elapsed() const noexcept {
        return finishedAt >= startedAt
                ? std::chrono::duration_cast<SimulationSchedulerDuration>(
                          finishedAt - startedAt)
                : SimulationSchedulerDuration::zero();
    }
};

enum class SimulationServicePhase : std::uint8_t {
    BeforeBatch = 0,
    AfterBatch = 1,
};

struct SimulationServiceCycleResult final {
    SimulationBatchPlan plan;
    SimulationBatchExecution execution;
};

// Both TargetRate and MaxThroughput execute through this kernel. The policy
// changes only the plan. MaxThroughput supplies the finite safety quota and is
// bounded by the service deadline; TargetRate additionally bounds work by
// earned credit and its learned quota. The caller supplies an O(1)
// interruption probe before every update; full command discovery and draining
// stay outside this physics kernel. The more expensive clock read uses an
// adaptive stride. The first update is always timed so a changed workload can
// shorten the stride within the current batch.
template <typename Update, typename ServiceRequested, typename Now>
SimulationBatchExecution
executeSimulationBatch(const SimulationBatchPlan& plan, Update&& update,
        ServiceRequested&& serviceRequested, Now&& now) {
    SimulationBatchExecution result;
    result.startedAt = now();
    result.finishedAt = result.startedAt;

    if (plan.immediateServiceRequested || serviceRequested()) {
        result.stopReason = SimulationBatchStopReason::ServiceRequested;
        return result;
    }
    if (plan.updateQuota == 0) {
        result.stopReason = SimulationBatchStopReason::NoWork;
        return result;
    }

    const std::uint32_t plannedClockCheckStride =
            plan.clockCheckStride == 0 ? 1 : plan.clockCheckStride;
    std::uint32_t clockCheckStride = 1;
    std::uint32_t updatesSinceClockCheck = 0;
    SimulationSchedulerTimePoint previousClockCheck = result.startedAt;

    while (result.completedUpdates < plan.updateQuota) {
        if (serviceRequested()) {
            result.stopReason = SimulationBatchStopReason::ServiceRequested;
            break;
        }

        const bool clockCheckDue = result.completedUpdates == 0 || updatesSinceClockCheck >= clockCheckStride;
        if (clockCheckDue) {
            const SimulationSchedulerTimePoint checkedAt = now();
            if (checkedAt >= plan.serviceDeadline) {
                result.finishedAt = checkedAt;
                result.stopReason = SimulationBatchStopReason::ServiceDeadlineReached;
                return result;
            }
            if (updatesSinceClockCheck > 0 && checkedAt > previousClockCheck) {
                const auto chunkDuration =
                        std::chrono::duration_cast<SimulationSchedulerDuration>(
                                checkedAt - previousClockCheck);
                const auto perUpdate =
                        chunkDuration / static_cast<std::int64_t>(updatesSinceClockCheck);
                const auto remaining =
                        std::chrono::duration_cast<SimulationSchedulerDuration>(
                                plan.serviceDeadline - checkedAt);
                const auto checkWindow = remaining < plan.maximumClockCheckInterval
                        ? remaining
                        : plan.maximumClockCheckInterval;
                if (perUpdate.count() > 0 && checkWindow.count() > 0) {
                    const auto adaptiveStride = static_cast<std::uint64_t>(
                            checkWindow.count() / perUpdate.count());
                    const auto boundedStride =
                            adaptiveStride == 0
                            ? std::uint64_t{1}
                            : (adaptiveStride < plannedClockCheckStride
                                              ? adaptiveStride
                                              : static_cast<std::uint64_t>(plannedClockCheckStride));
                    clockCheckStride = static_cast<std::uint32_t>(boundedStride);
                } else {
                    clockCheckStride = 1;
                }
            }
            previousClockCheck = checkedAt;
            updatesSinceClockCheck = 0;
        }

        update();
        ++result.completedUpdates;
        ++updatesSinceClockCheck;
    }

    result.finishedAt = now();
    if (result.stopReason == SimulationBatchStopReason::NoWork) {
        result.stopReason = result.completedUpdates == plan.updateQuota
                ? SimulationBatchStopReason::QuotaCompleted
                : SimulationBatchStopReason::NoWork;
    }
    return result;
}

struct SimulationSchedulerSnapshot final {
    SimulationRatePolicyKind policy = SimulationRatePolicyKind::Paused;
    std::optional<double> requestedTargetUps;
    bool updateCostInitialized = false;
    SimulationSchedulerDuration fastUpdateCost{};
    SimulationSchedulerDuration slowUpdateCost{};
    SimulationSchedulerDuration estimatedUpdateCost{};
    double targetCredit = 0.0;
    std::uint64_t policyGeneration = 0;
    std::uint64_t learningResetCount = 0;
    std::uint64_t plannedCycleCount = 0;
    std::uint64_t observedBatchCount = 0;
    std::uint64_t observedUpdateCount = 0;
    SimulationSchedulerDuration totalObservedBatchDuration{};
    SimulationSchedulerDuration maximumObservedBatchDuration{};
    std::uint64_t quotaExceededCount = 0;
    std::uint64_t deadlineOverrunCount = 0;
    SimulationSchedulerDuration maximumDeadlineOverrun{};
    std::uint32_t lastPlannedUpdateQuota = 0;
    std::uint32_t lastClockCheckStride = 0;
    std::uint32_t lastObservedUpdateCount = 0;
    std::uint32_t maximumObservedUpdateCount = 0;
    SimulationSchedulerDuration lastObservedBatchDuration{};
    SimulationBatchStopReason lastStopReason = SimulationBatchStopReason::NoWork;
};

// The migration starts with this model in observational mode. It owns no
// SimulationWorld and performs no work by itself: the current path may record what it
// actually executed, while an owner can execute a returned plan through
// executeSimulationBatch(). The model is owner-thread confined; cross-thread
// command notification is supplied as an input to the plan and batch kernel.
class SimulationScheduler final {
public:
    SimulationScheduler(SimulationSchedulerLimits limits,
            SimulationSchedulerTimePoint initialTime);
    SimulationScheduler(const SimulationScheduler&) = delete;
    SimulationScheduler& operator=(const SimulationScheduler&) = delete;
    SimulationScheduler(SimulationScheduler&&) = delete;
    SimulationScheduler& operator=(SimulationScheduler&&) = delete;

    bool setPolicy(const SimulationRatePolicy& policy,
            SimulationSchedulerTimePoint now);
    void restartPolicyTimeline(SimulationSchedulerTimePoint now) noexcept;
    void resetLearningForWorld(SimulationSchedulerTimePoint now);

    [[nodiscard]] SimulationBatchPlan
    planNextBatch(SimulationSchedulerTimePoint now,
            bool immediateServiceRequested);

    void recordObservedBatch(const SimulationBatchPlan& plan,
            const SimulationBatchExecution& execution);
    void recordObservedBatch(const SimulationBatchPlan& plan,
            std::uint32_t completedUpdates,
            SimulationSchedulerTimePoint startedAt,
            SimulationSchedulerTimePoint finishedAt,
            SimulationBatchStopReason stopReason);

    [[nodiscard]] SimulationRatePolicyKind policyKind() const noexcept;
    [[nodiscard]] SimulationSchedulerSnapshot snapshot() const noexcept;

private:
    [[nodiscard]] SimulationSchedulerTimePoint
    normalizeTime(SimulationSchedulerTimePoint now) noexcept;
    void accrueTargetCredit(SimulationSchedulerTimePoint now) noexcept;
    [[nodiscard]] std::uint32_t adaptiveTargetRateUpdateQuota() const noexcept;
    [[nodiscard]] std::uint32_t
    adaptiveClockCheckStride(std::uint32_t updateQuota) const noexcept;
    [[nodiscard]] SimulationSchedulerDuration
    conservativeUpdateCost() const noexcept;
    void observeUpdateCost(std::uint32_t completedUpdates,
            SimulationSchedulerDuration elapsed) noexcept;

    SimulationSchedulerLimits mLimits;
    SimulationRatePolicy mPolicy = SimulationRatePolicy::paused();
    SimulationSchedulerTimePoint mLastTimelineTime;
    SimulationSchedulerTimePoint mLastCreditTime;
    double mTargetCredit = 0.0;
    bool mUpdateCostInitialized = false;
    long double mFastUpdateCostNanos = 0.0L;
    long double mSlowUpdateCostNanos = 0.0L;
    std::uint64_t mPolicyGeneration = 1;
    std::uint64_t mLearningResetCount = 0;
    std::uint64_t mNextPlanSequence = 1;
    std::uint64_t mLastPlannedSequence = 0;
    std::uint64_t mLastRecordedSequence = 0;
    std::uint64_t mPlannedCycleCount = 0;
    std::uint64_t mObservedBatchCount = 0;
    std::uint64_t mObservedUpdateCount = 0;
    SimulationSchedulerDuration mTotalObservedBatchDuration{};
    SimulationSchedulerDuration mMaximumObservedBatchDuration{};
    std::uint64_t mQuotaExceededCount = 0;
    std::uint64_t mDeadlineOverrunCount = 0;
    SimulationSchedulerDuration mMaximumDeadlineOverrun{};
    std::uint32_t mLastPlannedUpdateQuota = 0;
    std::uint32_t mLastClockCheckStride = 0;
    std::uint32_t mLastObservedUpdateCount = 0;
    std::uint32_t mMaximumObservedUpdateCount = 0;
    SimulationSchedulerDuration mLastObservedBatchDuration{};
    SimulationBatchStopReason mLastStopReason = SimulationBatchStopReason::NoWork;
};

// One owner service cycle has a fixed ordering: service pending work, plan and
// execute at most one bounded simulation batch, record only the batch interval,
// then service again. Waiting on a TargetRate wake deadline belongs to the
// caller and MaxThroughput therefore never sleeps or yields in this function.
// The service callback must not destroy or replace scheduler while the cycle
// is active.
template <typename Service, typename Update, typename ServiceRequested,
        typename Now>
SimulationServiceCycleResult
executeSimulationServiceCycle(SimulationScheduler& scheduler, Service&& service,
        Update&& update,
        ServiceRequested&& serviceRequested, Now&& now) {
    service(SimulationServicePhase::BeforeBatch);

    SimulationServiceCycleResult result;
    result.plan = scheduler.planNextBatch(now(), serviceRequested());
    result.execution =
            executeSimulationBatch(result.plan, update, serviceRequested, now);
    scheduler.recordObservedBatch(result.plan, result.execution);

    service(SimulationServicePhase::AfterBatch);
    return result;
}

} // namespace cellularity::simulation
