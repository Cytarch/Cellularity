#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "app/simulation/SimulationScheduler.h"
#include "core/GenomePool.h"

namespace cellularity::microscope {

using MicroscopeCommandDuration = std::chrono::nanoseconds;
using MicroscopeCommandTimePoint =
        std::chrono::time_point<std::chrono::steady_clock,
                MicroscopeCommandDuration>;
using MicroscopeCommandGenome = cellularity::GenomeData;

class MicroscopeCommandPayload {
public:
    virtual ~MicroscopeCommandPayload() = default;
};

[[nodiscard]] MicroscopeCommandTimePoint microscopeCommandNow() noexcept;

enum class MicroscopeWorldCommandKind : std::uint8_t {
    TouchDown = 0,
    TouchMovePosition = 1,
    TouchMovePathSegment = 2,
    TouchUp = 3,
    TouchCancel = 4,
    Tap = 5,
    SetTemperatureMode = 6,
    SetToolOrdinal = 7,
    SetRatePolicy = 8,
    SetOptionalTargetUps = 9,
    Reset = 10,
    Reload = 11,
    LifecyclePause = 12,
    LifecycleResume = 13,
    Save = 14,
    Apply = 15,
    Inspect = 16,
    SetSlowMotionSpeed = 17,
};

enum class MicroscopeCommandEnqueueStatus : std::uint8_t {
    Accepted = 0,
    AcceptedAndCoalescedPrevious = 1,
    RejectedOrderedCapacity = 2,
    RejectedPathCapacity = 3,
    RejectedPointerState = 4,
    RejectedPointerCapacity = 5,
    RejectedInvalidArgument = 6,
    RejectedSequenceExhausted = 7,
};

enum class MicroscopeCommandResolution : std::uint8_t {
    Pending = 0,
    Applied = 1,
    RejectedAtIngress = 2,
    RejectedByOwner = 3,
    Coalesced = 4,
    StaleEpoch = 5,
    Failed = 6,
};

enum class MicroscopeCommandWaitResult : std::uint8_t {
    Notified = 0,
    DeadlineReached = 1,
};

struct MicroscopeCommandEnqueueResult final {
    std::uint64_t sequence = 0;
    std::uint64_t worldEpoch = 0;
    std::uint64_t coalescedSequence = 0;
    MicroscopeCommandEnqueueStatus status =
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument;

    [[nodiscard]] bool accepted() const noexcept {
        return status == MicroscopeCommandEnqueueStatus::Accepted || status == MicroscopeCommandEnqueueStatus::AcceptedAndCoalescedPrevious;
    }
};

struct MicroscopeCommandCompletionSnapshot final {
    std::uint64_t sequence = 0;
    std::uint64_t worldEpoch = 0;
    std::uint64_t appliedWorldRevision = 0;
    MicroscopeCommandResolution resolution = MicroscopeCommandResolution::Pending;
    std::int64_t enqueuedNanos = 0;
    std::int64_t resolvedNanos = 0;
};

// Request/reply callers may wait on this object only from their existing
// non-UI worker. Enqueue itself never waits for a completion.
class MicroscopeCommandCompletion final {
public:
    MicroscopeCommandCompletion();
    ~MicroscopeCommandCompletion();

    MicroscopeCommandCompletion(const MicroscopeCommandCompletion&) = delete;
    MicroscopeCommandCompletion&
    operator=(const MicroscopeCommandCompletion&) = delete;

    [[nodiscard]] MicroscopeCommandCompletionSnapshot snapshot() const;
    [[nodiscard]] MicroscopeCommandWaitResult
    waitUntil(MicroscopeCommandTimePoint deadline,
            MicroscopeCommandCompletionSnapshot& snapshot) const;

private:
    friend class MicroscopeCommandMailbox;
    struct State;
    std::unique_ptr<State> mState;

    void begin(const MicroscopeCommandCompletionSnapshot& snapshot) noexcept;
    void resolve(const MicroscopeCommandCompletionSnapshot& snapshot) noexcept;
};

struct MicroscopeWorldCommand final {
    MicroscopeWorldCommandKind kind = MicroscopeWorldCommandKind::TouchDown;
    std::uint64_t sequence = 0;
    std::uint64_t worldEpoch = 0;
    std::uint64_t pointerGeneration = 0;
    std::uint64_t eventViewportRevision = 0;
    std::uint64_t requestId = 0;
    std::int64_t enqueuedNanos = 0;
    int pointerId = -1;
    int pointerSlot = -1;
    int integerValue = 0;
    double worldX = 0.0;
    double worldY = 0.0;
    double numericValue = 0.0;
    bool hasNumericValue = false;
    simulation::SimulationRatePolicyKind ratePolicy =
            simulation::SimulationRatePolicyKind::Paused;
    std::shared_ptr<const MicroscopeCommandGenome> genome;
    std::shared_ptr<MicroscopeCommandCompletion> completion;
    std::shared_ptr<MicroscopeCommandPayload> payload;
};

enum class MicroscopeCommandApplyDisposition : std::uint8_t {
    Applied = 0,
    Rejected = 1,
};

struct MicroscopeCommandApplyResult final {
    MicroscopeCommandApplyDisposition disposition =
            MicroscopeCommandApplyDisposition::Applied;
    bool changedWorld = false;
    bool requestUrgentCapture = false;

    [[nodiscard]] static MicroscopeCommandApplyResult
    applied(bool changedWorld = false,
            bool requestUrgentCapture = false) noexcept {
        return {MicroscopeCommandApplyDisposition::Applied, changedWorld,
                requestUrgentCapture};
    }

    [[nodiscard]] static MicroscopeCommandApplyResult rejected() noexcept {
        return {MicroscopeCommandApplyDisposition::Rejected, false, false};
    }
};

struct MicroscopeCommandResolvedState final {
    std::uint64_t worldEpoch = 0;
    std::uint64_t lastIssuedInputSequence = 0;
    std::uint64_t lastResolvedInputSequence = 0;
    std::uint64_t appliedWorldRevision = 0;
    std::uint64_t presentedResolvedInputSequence = 0;
    std::uint64_t presentedWorldRevision = 0;
    std::int64_t lastPresentedNanos = 0;
};

struct MicroscopeCommandMailboxCounters final {
    std::uint64_t accepted = 0;
    std::uint64_t applied = 0;
    std::uint64_t appliedWorldChanges = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t rejected = 0;
    std::uint64_t staleEpoch = 0;
    std::uint64_t failed = 0;
    std::uint64_t terminalAccepted = 0;
    std::uint64_t lifecycleAccepted = 0;
    std::uint64_t urgentCaptureRequests = 0;
    std::uint64_t viewportCaptureHints = 0;
    std::uint64_t viewportCaptureHintsCoalesced = 0;
    std::uint64_t viewportCaptureHintsConsumed = 0;
    std::uint64_t notificationCount = 0;
    std::uint32_t orderedDepth = 0;
    std::uint32_t orderedHighWater = 0;
    std::uint32_t pathSegmentDepth = 0;
    std::uint32_t pathSegmentHighWater = 0;
    std::uint32_t activePointerGenerations = 0;
};

struct MicroscopeCommandMailboxSnapshot final {
    MicroscopeCommandResolvedState resolved{};
    MicroscopeCommandMailboxCounters counters{};
    std::uint64_t notificationGeneration = 0;
    std::uint64_t pendingViewportCaptureRevision = 0;
    bool ownerWorkPending = false;
};

struct MicroscopeCommandServiceResult final {
    std::uint32_t processed = 0;
    std::uint32_t applied = 0;
    std::uint32_t rejected = 0;
    std::uint32_t staleEpoch = 0;
    std::uint32_t failed = 0;
    bool changedWorld = false;
    bool urgentCaptureRequested = false;
    bool deadlineReached = false;
    MicroscopeCommandResolvedState resolved{};
};

struct MicroscopeActivePointerCancelResult final {
    std::uint32_t activeBefore = 0;
    std::uint32_t cancelsEnqueued = 0;
    bool invariantSatisfied = true;
};

// Fixed-capacity SPSC ingress for commands that need the simulation owner.
// One externally serialized input/control producer enqueues; one owner thread
// consumes. High-rate position state and latest settings use replaceable
// slots. Ordered/path commands use a bounded ring whose admission leaves
// capacity reserved for active-pointer terminal events and lifecycle barriers.
class MicroscopeCommandMailbox final {
public:
    static constexpr std::uint32_t kOrderedCapacity = 128;
    static constexpr std::uint32_t kPathSegmentCapacity = 32;
    static constexpr std::uint32_t kPointerCapacity = 20;
    static constexpr std::uint32_t kLifecycleReserve = 4;

    explicit MicroscopeCommandMailbox(std::uint64_t initialWorldEpoch);
    ~MicroscopeCommandMailbox();

    MicroscopeCommandMailbox(const MicroscopeCommandMailbox&) = delete;
    MicroscopeCommandMailbox&
    operator=(const MicroscopeCommandMailbox&) = delete;
    MicroscopeCommandMailbox(MicroscopeCommandMailbox&&) = delete;
    MicroscopeCommandMailbox& operator=(MicroscopeCommandMailbox&&) = delete;

    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueTouchDown(
            int pointerId, double worldX, double worldY,
            std::uint64_t eventViewportRevision,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueTouchMovePosition(
            int pointerId, double worldX, double worldY,
            std::uint64_t eventViewportRevision,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueTouchMovePathSegment(
            int pointerId, double worldX, double worldY,
            std::uint64_t eventViewportRevision,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueTouchUp(
            int pointerId,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueTouchCancel(
            int pointerId,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult
    enqueueTap(double worldX, double worldY, std::uint64_t eventViewportRevision,
            std::shared_ptr<const MicroscopeCommandGenome> genome = nullptr,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());

    // Producer-side resync helper. Active pointers consume the terminal slots
    // reserved by their accepted TouchDown; Closing pointers already have a
    // terminal command in flight and are not enqueued twice.
    [[nodiscard]] MicroscopeActivePointerCancelResult
    enqueueActivePointerCancels(
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());

    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueTemperatureMode(
            int temperatureMode,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueToolOrdinal(
            int toolOrdinal,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueSlowMotionSpeed(
            double slowMotionSpeed,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueRatePolicy(
            simulation::SimulationRatePolicyKind policy,
            std::optional<double> targetUps,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueOptionalTargetUps(
            std::optional<double> targetUps,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow());

    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueBarrier(
            MicroscopeWorldCommandKind kind, std::uint64_t requestId = 0,
            std::shared_ptr<MicroscopeCommandCompletion> completion = nullptr,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow(),
            std::shared_ptr<MicroscopeCommandPayload> payload = nullptr);
    [[nodiscard]] MicroscopeCommandEnqueueResult enqueueRequest(
            MicroscopeWorldCommandKind kind, std::uint64_t requestId,
            std::shared_ptr<MicroscopeCommandCompletion> completion,
            MicroscopeCommandTimePoint enqueuedAt = microscopeCommandNow(),
            std::shared_ptr<MicroscopeCommandPayload> payload = nullptr);

    // Camera publication itself bypasses the owner. This atomic hint only asks
    // the owner for newest-viewport coverage and collapses all intermediate
    // revisions.
    void publishViewportCaptureHint(std::uint64_t viewportRevision) noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
    consumeViewportCaptureHint() noexcept;

    [[nodiscard]] bool hasPendingOwnerWork() const noexcept;
    [[nodiscard]] std::uint64_t notificationGeneration() const noexcept;
    // O(1) batch-interruption probe. A full channel scan remains a service-
    // boundary operation; the physics hot path compares only this generation.
    [[nodiscard]] bool
    hasNotificationSince(std::uint64_t observedGeneration) const noexcept;
    [[nodiscard]] MicroscopeCommandWaitResult
    waitForNotification(std::uint64_t observedGeneration,
            MicroscopeCommandTimePoint deadline) const;
    // Wakes the owner for shutdown or another out-of-band lifecycle condition
    // without adding a command or mutating simulation state.
    void notifyOwner() noexcept;

    // Called only by the simulation owner at the world-replacement safe point.
    // New ingress immediately receives the new epoch; old queued work is then
    // resolved as stale without entering the handler.
    void advanceWorldEpoch(
            std::uint64_t worldEpoch,
            MicroscopeCommandTimePoint changedAt = microscopeCommandNow());

    void markPresented(
            std::uint64_t worldEpoch, std::uint64_t resolvedInputSequence,
            std::uint64_t appliedWorldRevision,
            MicroscopeCommandTimePoint presentedAt = microscopeCommandNow()) noexcept;

    [[nodiscard]] MicroscopeCommandResolvedState resolvedState() const noexcept;
    [[nodiscard]] std::uint32_t activePointerGenerationCount() const noexcept;
    [[nodiscard]] MicroscopeCommandMailboxSnapshot snapshot() const noexcept;

    template <typename Apply, typename Now>
    MicroscopeCommandServiceResult
    serviceUntil(MicroscopeCommandTimePoint deadline,
            std::uint32_t hardCommandLimit, Apply&& apply, Now&& now) {
        MicroscopeCommandServiceResult result;
        while (result.processed < hardCommandLimit) {
            const MicroscopeCommandTimePoint checkedAt = now();
            if (checkedAt >= deadline) {
                result.deadlineReached = hasPendingOwnerWork();
                break;
            }

            MicroscopeWorldCommand command;
            if (!tryTakeNext(command)) {
                break;
            }
            ++result.processed;

            if (command.worldEpoch != currentWorldEpoch()) {
                finishTaken(command, MicroscopeCommandResolution::StaleEpoch, false,
                        false, checkedAt);
                ++result.staleEpoch;
                continue;
            }

            try {
                const MicroscopeCommandApplyResult applied = apply(command);
                if (applied.disposition == MicroscopeCommandApplyDisposition::Rejected) {
                    finishTaken(command, MicroscopeCommandResolution::RejectedByOwner,
                            false, false, now());
                    ++result.rejected;
                    continue;
                }

                finishTaken(command, MicroscopeCommandResolution::Applied,
                        applied.changedWorld, applied.requestUrgentCapture, now());
                ++result.applied;
                result.changedWorld = result.changedWorld || applied.changedWorld;
                result.urgentCaptureRequested =
                        result.urgentCaptureRequested || applied.requestUrgentCapture;
            } catch (...) {
                finishTaken(command, MicroscopeCommandResolution::Failed, false, false,
                        now());
                ++result.failed;
                result.resolved = resolvedState();
                throw;
            }
        }
        refreshResolvedWatermark();
        result.resolved = resolvedState();
        return result;
    }

private:
    struct State;
    std::unique_ptr<State> mState;

    [[nodiscard]] MicroscopeCommandEnqueueResult
    enqueueTerminalCommand(MicroscopeWorldCommandKind kind, int pointerId,
            MicroscopeCommandTimePoint enqueuedAt);
    [[nodiscard]] MicroscopeCommandEnqueueResult
    enqueueLatestSettingCommand(MicroscopeWorldCommand command,
            MicroscopeCommandTimePoint rejectedAt);
    [[nodiscard]] bool tryTakeNext(MicroscopeWorldCommand& command);
    void finishTaken(const MicroscopeWorldCommand& command,
            MicroscopeCommandResolution resolution, bool changedWorld,
            bool requestUrgentCapture,
            MicroscopeCommandTimePoint resolvedAt) noexcept;
    void refreshResolvedWatermark() noexcept;
    [[nodiscard]] std::uint64_t currentWorldEpoch() const noexcept;
};

} // namespace cellularity::microscope
