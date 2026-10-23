#include "app/simulation/SimulationScheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cellularity::simulation {
namespace {

constexpr long double kFastEstimateFallWeight = 0.25L;
constexpr long double kSlowEstimateWeight = 0.0625L;

std::uint64_t saturatingIncrement(std::uint64_t value) noexcept {
    return value == std::numeric_limits<std::uint64_t>::max() ? value : value + 1;
}

std::uint64_t saturatingAdd(std::uint64_t left, std::uint64_t right) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

SimulationSchedulerDuration
saturatingDurationAdd(SimulationSchedulerDuration left,
        SimulationSchedulerDuration right) noexcept {
    if (right <= SimulationSchedulerDuration::zero()) {
        return left;
    }
    const auto maximum = SimulationSchedulerDuration::max();
    return right > maximum - left ? maximum : left + right;
}

SimulationSchedulerDuration durationFromNanos(long double nanos) noexcept {
    if (!(nanos > 0.0L)) {
        return SimulationSchedulerDuration::zero();
    }
    const long double maximum = static_cast<long double>(
            std::numeric_limits<SimulationSchedulerDuration::rep>::max());
    if (nanos >= maximum) {
        return SimulationSchedulerDuration::max();
    }
    return SimulationSchedulerDuration{
            static_cast<SimulationSchedulerDuration::rep>(std::ceil(nanos))
    };
}

long double seconds(SimulationSchedulerDuration duration) noexcept {
    return static_cast<long double>(duration.count()) / 1'000'000'000.0L;
}

} // namespace

SimulationSchedulerTimePoint simulationSchedulerNow() noexcept {
    return std::chrono::time_point_cast<SimulationSchedulerDuration>(
            std::chrono::steady_clock::now());
}

SimulationRatePolicy::SimulationRatePolicy(
        SimulationRatePolicyKind kind, std::optional<double> targetUps) noexcept
        : mKind(kind), mTargetUps(targetUps) {}

SimulationRatePolicy SimulationRatePolicy::paused() noexcept {
    return {SimulationRatePolicyKind::Paused, std::nullopt};
}

SimulationRatePolicy SimulationRatePolicy::maxThroughput() noexcept {
    return {SimulationRatePolicyKind::MaxThroughput, std::nullopt};
}

SimulationRatePolicy SimulationRatePolicy::targetRate(double targetUps) {
    if (!std::isfinite(targetUps) || targetUps <= 0.0) {
        throw std::invalid_argument(
                "TargetRate requires a finite target greater than zero");
    }
    return {SimulationRatePolicyKind::TargetRate, targetUps};
}

SimulationRatePolicyKind SimulationRatePolicy::kind() const noexcept {
    return mKind;
}

std::optional<double>
SimulationRatePolicy::requestedTargetUps() const noexcept {
    return mTargetUps;
}

bool SimulationRatePolicy::operator==(
        const SimulationRatePolicy& other) const noexcept {
    return mKind == other.mKind && mTargetUps == other.mTargetUps;
}

bool SimulationRatePolicy::operator!=(
        const SimulationRatePolicy& other) const noexcept {
    return !(*this == other);
}

SimulationSchedulerLimits::SimulationSchedulerLimits(
        SimulationSchedulerDuration ownerServiceBudget,
        SimulationSchedulerDuration targetLagLimit,
        SimulationSchedulerDuration maximumClockCheckInterval,
        std::uint32_t hardBatchUpdateLimit, double updateCostSafetyMargin)
        : mOwnerServiceBudget(ownerServiceBudget), mTargetLagLimit(targetLagLimit),
          mMaximumClockCheckInterval(maximumClockCheckInterval),
          mHardBatchUpdateLimit(hardBatchUpdateLimit),
          mUpdateCostSafetyMargin(updateCostSafetyMargin) {
    if (mOwnerServiceBudget <= SimulationSchedulerDuration::zero()) {
        throw std::invalid_argument(
                "Simulation scheduler owner service budget must be positive");
    }
    if (mTargetLagLimit <= SimulationSchedulerDuration::zero()) {
        throw std::invalid_argument(
                "Simulation scheduler target lag limit must be positive");
    }
    if (mMaximumClockCheckInterval <= SimulationSchedulerDuration::zero() || mMaximumClockCheckInterval > mOwnerServiceBudget) {
        throw std::invalid_argument(
                "Simulation scheduler clock-check interval must be positive and no "
                "greater than the owner service budget");
    }
    if (mHardBatchUpdateLimit == 0) {
        throw std::invalid_argument(
                "Simulation scheduler hard batch limit must be positive");
    }
    if (!std::isfinite(mUpdateCostSafetyMargin) || mUpdateCostSafetyMargin < 1.0) {
        throw std::invalid_argument(
                "Simulation scheduler update-cost safety margin must be finite and "
                "at least one");
    }
}

SimulationSchedulerDuration
SimulationSchedulerLimits::ownerServiceBudget() const noexcept {
    return mOwnerServiceBudget;
}

SimulationSchedulerDuration
SimulationSchedulerLimits::targetLagLimit() const noexcept {
    return mTargetLagLimit;
}

SimulationSchedulerDuration
SimulationSchedulerLimits::maximumClockCheckInterval() const noexcept {
    return mMaximumClockCheckInterval;
}

std::uint32_t SimulationSchedulerLimits::hardBatchUpdateLimit() const noexcept {
    return mHardBatchUpdateLimit;
}

double SimulationSchedulerLimits::updateCostSafetyMargin() const noexcept {
    return mUpdateCostSafetyMargin;
}

SimulationScheduler::SimulationScheduler(
        SimulationSchedulerLimits limits, SimulationSchedulerTimePoint initialTime)
        : mLimits(std::move(limits)), mLastTimelineTime(initialTime),
          mLastCreditTime(initialTime) {}

bool SimulationScheduler::setPolicy(const SimulationRatePolicy& policy,
        SimulationSchedulerTimePoint now) {
    now = normalizeTime(now);
    if (mPolicy == policy) {
        return false;
    }
    mPolicy = policy;
    mTargetCredit = 0.0;
    mLastCreditTime = now;
    mPolicyGeneration = saturatingIncrement(mPolicyGeneration);
    return true;
}

void SimulationScheduler::restartPolicyTimeline(
        SimulationSchedulerTimePoint now) noexcept {
    now = normalizeTime(now);
    mTargetCredit = 0.0;
    mLastCreditTime = now;
    mPolicyGeneration = saturatingIncrement(mPolicyGeneration);
}

void SimulationScheduler::resetLearningForWorld(
        SimulationSchedulerTimePoint now) {
    now = normalizeTime(now);
    mTargetCredit = 0.0;
    mLastCreditTime = now;
    mUpdateCostInitialized = false;
    mFastUpdateCostNanos = 0.0L;
    mSlowUpdateCostNanos = 0.0L;
    mLearningResetCount = saturatingIncrement(mLearningResetCount);
    mPolicyGeneration = saturatingIncrement(mPolicyGeneration);
}

SimulationBatchPlan
SimulationScheduler::planNextBatch(SimulationSchedulerTimePoint now,
        bool immediateServiceRequested) {
    now = normalizeTime(now);
    accrueTargetCredit(now);

    SimulationBatchPlan plan;
    plan.sequence = mNextPlanSequence;
    mNextPlanSequence = saturatingIncrement(mNextPlanSequence);
    plan.policyGeneration = mPolicyGeneration;
    plan.policy = mPolicy.kind();
    plan.issuedAt = now;
    plan.serviceDeadline = now + mLimits.ownerServiceBudget();
    plan.maximumClockCheckInterval = mLimits.maximumClockCheckInterval();
    plan.immediateServiceRequested = immediateServiceRequested;

    if (!immediateServiceRequested && mPolicy.kind() != SimulationRatePolicyKind::Paused) {
        if (mPolicy.kind() == SimulationRatePolicyKind::MaxThroughput) {
            // MaxThroughput is bounded by the service deadline inside the shared
            // batch kernel. The learned cost controls only how often that kernel
            // reads the clock; it must not become a second, self-adjusting rate
            // limiter. A transiently expensive non-physics operation in the owner
            // cycle can otherwise collapse the learned quota and amplify service
            // overhead for every following update.
            plan.updateQuota =
                    mUpdateCostInitialized ? mLimits.hardBatchUpdateLimit() : 1;
        } else {
            plan.updateQuota = adaptiveTargetRateUpdateQuota();
            const auto availableCredit =
                    mTargetCredit >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())
                    ? std::numeric_limits<std::uint64_t>::max()
                    : static_cast<std::uint64_t>(mTargetCredit);
            plan.updateQuota = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(plan.updateQuota, availableCredit));
            if (plan.updateQuota == 0) {
                const double targetUps = *mPolicy.requestedTargetUps();
                const long double missingCredit =
                        std::max(0.0L, 1.0L - static_cast<long double>(mTargetCredit));
                const auto untilCredit =
                        durationFromNanos(missingCredit * 1'000'000'000.0L / static_cast<long double>(targetUps));
                const auto boundedWait = untilCredit < mLimits.targetLagLimit()
                        ? untilCredit
                        : mLimits.targetLagLimit();
                plan.wakeDeadline =
                        now + (boundedWait > SimulationSchedulerDuration::zero() ? boundedWait : SimulationSchedulerDuration{1});
            }
        }
    }

    plan.clockCheckStride = adaptiveClockCheckStride(plan.updateQuota);
    mLastPlannedSequence = plan.sequence;
    mPlannedCycleCount = saturatingIncrement(mPlannedCycleCount);
    mLastPlannedUpdateQuota = plan.updateQuota;
    mLastClockCheckStride = plan.clockCheckStride;
    return plan;
}

void SimulationScheduler::recordObservedBatch(
        const SimulationBatchPlan& plan,
        const SimulationBatchExecution& execution) {
    recordObservedBatch(plan, execution.completedUpdates, execution.startedAt,
            execution.finishedAt, execution.stopReason);
}

void SimulationScheduler::recordObservedBatch(
        const SimulationBatchPlan& plan, std::uint32_t completedUpdates,
        SimulationSchedulerTimePoint startedAt,
        SimulationSchedulerTimePoint finishedAt,
        SimulationBatchStopReason stopReason) {
    if (plan.sequence == 0 || plan.sequence != mLastPlannedSequence || plan.sequence <= mLastRecordedSequence) {
        throw std::logic_error(
                "Simulation scheduler batch observation is stale or duplicated");
    }
    if (plan.policyGeneration != mPolicyGeneration || plan.policy != mPolicy.kind()) {
        throw std::logic_error(
                "Simulation scheduler policy changed while a batch was in flight");
    }
    if (startedAt < plan.issuedAt || finishedAt < startedAt) {
        throw std::invalid_argument(
                "Simulation scheduler batch observation has invalid timestamps");
    }

    finishedAt = normalizeTime(finishedAt);
    const auto elapsed = std::chrono::duration_cast<SimulationSchedulerDuration>(
            finishedAt - startedAt);
    observeUpdateCost(completedUpdates, elapsed);

    if (mPolicy.kind() == SimulationRatePolicyKind::TargetRate) {
        mTargetCredit =
                std::max(0.0, mTargetCredit - static_cast<double>(completedUpdates));
    }
    if (completedUpdates > plan.updateQuota) {
        mQuotaExceededCount = saturatingIncrement(mQuotaExceededCount);
    }
    if (completedUpdates > 0 && finishedAt > plan.serviceDeadline) {
        const auto overrun =
                std::chrono::duration_cast<SimulationSchedulerDuration>(
                        finishedAt - plan.serviceDeadline);
        mDeadlineOverrunCount = saturatingIncrement(mDeadlineOverrunCount);
        mMaximumDeadlineOverrun = std::max(mMaximumDeadlineOverrun, overrun);
    }

    mObservedBatchCount = saturatingIncrement(mObservedBatchCount);
    mObservedUpdateCount = saturatingAdd(mObservedUpdateCount, completedUpdates);
    mTotalObservedBatchDuration =
            saturatingDurationAdd(mTotalObservedBatchDuration, elapsed);
    mMaximumObservedBatchDuration =
            std::max(mMaximumObservedBatchDuration, elapsed);
    mLastObservedUpdateCount = completedUpdates;
    mMaximumObservedUpdateCount =
            std::max(mMaximumObservedUpdateCount, completedUpdates);
    mLastObservedBatchDuration = elapsed;
    mLastStopReason = stopReason;
    mLastRecordedSequence = plan.sequence;
}

SimulationRatePolicyKind SimulationScheduler::policyKind() const noexcept {
    return mPolicy.kind();
}

SimulationSchedulerSnapshot SimulationScheduler::snapshot() const noexcept {
    SimulationSchedulerSnapshot result;
    result.policy = mPolicy.kind();
    result.requestedTargetUps = mPolicy.requestedTargetUps();
    result.updateCostInitialized = mUpdateCostInitialized;
    result.fastUpdateCost = durationFromNanos(mFastUpdateCostNanos);
    result.slowUpdateCost = durationFromNanos(mSlowUpdateCostNanos);
    result.estimatedUpdateCost = conservativeUpdateCost();
    result.targetCredit = mTargetCredit;
    result.policyGeneration = mPolicyGeneration;
    result.learningResetCount = mLearningResetCount;
    result.plannedCycleCount = mPlannedCycleCount;
    result.observedBatchCount = mObservedBatchCount;
    result.observedUpdateCount = mObservedUpdateCount;
    result.totalObservedBatchDuration = mTotalObservedBatchDuration;
    result.maximumObservedBatchDuration = mMaximumObservedBatchDuration;
    result.quotaExceededCount = mQuotaExceededCount;
    result.deadlineOverrunCount = mDeadlineOverrunCount;
    result.maximumDeadlineOverrun = mMaximumDeadlineOverrun;
    result.lastPlannedUpdateQuota = mLastPlannedUpdateQuota;
    result.lastClockCheckStride = mLastClockCheckStride;
    result.lastObservedUpdateCount = mLastObservedUpdateCount;
    result.maximumObservedUpdateCount = mMaximumObservedUpdateCount;
    result.lastObservedBatchDuration = mLastObservedBatchDuration;
    result.lastStopReason = mLastStopReason;
    return result;
}

SimulationSchedulerTimePoint
SimulationScheduler::normalizeTime(SimulationSchedulerTimePoint now) noexcept {
    if (now < mLastTimelineTime) {
        return mLastTimelineTime;
    }
    mLastTimelineTime = now;
    return now;
}

void SimulationScheduler::accrueTargetCredit(
        SimulationSchedulerTimePoint now) noexcept {
    if (now < mLastCreditTime) {
        now = mLastCreditTime;
    }
    if (mPolicy.kind() != SimulationRatePolicyKind::TargetRate) {
        mLastCreditTime = now;
        return;
    }

    const auto elapsed = std::chrono::duration_cast<SimulationSchedulerDuration>(
            now - mLastCreditTime);
    mLastCreditTime = now;
    const long double targetUps =
            static_cast<long double>(*mPolicy.requestedTargetUps());
    const long double earned = seconds(elapsed) * targetUps;
    const long double creditLimit =
            std::max(1.0L, seconds(mLimits.targetLagLimit()) * targetUps);
    const long double maximumCredit =
            static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    const long double boundedLimit = std::min(creditLimit, maximumCredit);
    const long double nextCredit =
            std::min(boundedLimit, static_cast<long double>(mTargetCredit) + earned);
    mTargetCredit = static_cast<double>(nextCredit);
}

std::uint32_t
SimulationScheduler::adaptiveTargetRateUpdateQuota() const noexcept {
    const std::uint32_t hardLimit = mLimits.hardBatchUpdateLimit();
    if (!mUpdateCostInitialized) {
        return 1;
    }

    const auto estimate = conservativeUpdateCost();
    if (estimate <= SimulationSchedulerDuration::zero()) {
        return 1;
    }
    const auto rawQuota = static_cast<std::uint64_t>(
            mLimits.ownerServiceBudget().count() / estimate.count());
    if (rawQuota == 0) {
        return 1;
    }
    return static_cast<std::uint32_t>(
            std::min<std::uint64_t>(rawQuota, hardLimit));
}

std::uint32_t SimulationScheduler::adaptiveClockCheckStride(
        std::uint32_t updateQuota) const noexcept {
    if (updateQuota == 0) {
        return 0;
    }
    if (!mUpdateCostInitialized) {
        return 1;
    }

    const auto estimate = conservativeUpdateCost();
    if (estimate <= SimulationSchedulerDuration::zero()) {
        return 1;
    }
    const auto rawStride = static_cast<std::uint64_t>(
            mLimits.maximumClockCheckInterval().count() / estimate.count());
    const auto positiveStride = std::max<std::uint64_t>(1, rawStride);
    return static_cast<std::uint32_t>(
            std::min<std::uint64_t>(positiveStride, updateQuota));
}

SimulationSchedulerDuration
SimulationScheduler::conservativeUpdateCost() const noexcept {
    if (!mUpdateCostInitialized) {
        return SimulationSchedulerDuration::zero();
    }
    const long double learned =
            std::max(mFastUpdateCostNanos, mSlowUpdateCostNanos);
    return durationFromNanos(
            learned * static_cast<long double>(mLimits.updateCostSafetyMargin()));
}

void SimulationScheduler::observeUpdateCost(
        std::uint32_t completedUpdates,
        SimulationSchedulerDuration elapsed) noexcept {
    if (completedUpdates == 0 || elapsed < SimulationSchedulerDuration::zero()) {
        return;
    }
    const long double sample =
            std::max(1.0L, static_cast<long double>(elapsed.count()) / static_cast<long double>(completedUpdates));
    if (!mUpdateCostInitialized) {
        mFastUpdateCostNanos = sample;
        mSlowUpdateCostNanos = sample;
        mUpdateCostInitialized = true;
        return;
    }

    if (sample >= mFastUpdateCostNanos) {
        mFastUpdateCostNanos = sample;
    } else {
        mFastUpdateCostNanos +=
                (sample - mFastUpdateCostNanos) * kFastEstimateFallWeight;
    }
    mSlowUpdateCostNanos += (sample - mSlowUpdateCostNanos) * kSlowEstimateWeight;
}

} // namespace cellularity::simulation
