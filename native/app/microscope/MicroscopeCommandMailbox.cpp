#include "app/microscope/MicroscopeCommandMailbox.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "concurrency/LatestValueExchange.h"

namespace cellularity::microscope {
namespace {

using LatestCommandExchange =
        concurrency::LatestValueExchange<MicroscopeWorldCommand>;

static_assert(std::is_nothrow_copy_assignable_v<MicroscopeWorldCommand>);

constexpr std::size_t kLatestSettingCount = 5;

enum class PointerProducerState : std::uint8_t {
    Free = 0,
    Active = 1,
    Closing = 2,
    Releasing = 3,
};

[[nodiscard]] std::int64_t
timePointNanos(MicroscopeCommandTimePoint timePoint) noexcept {
    return timePoint.time_since_epoch().count();
}

void saturatingIncrement(std::atomic<std::uint64_t>& value) noexcept {
    std::uint64_t observed = value.load(std::memory_order_relaxed);
    while (observed != std::numeric_limits<std::uint64_t>::max() && !value.compare_exchange_weak(observed, observed + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

void updateMaximum(std::atomic<std::uint32_t>& value,
        std::uint32_t candidate) noexcept {
    std::uint32_t observed = value.load(std::memory_order_relaxed);
    while (observed < candidate && !value.compare_exchange_weak(observed, candidate, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

[[nodiscard]] bool isTerminal(MicroscopeWorldCommandKind kind) noexcept {
    return kind == MicroscopeWorldCommandKind::TouchUp || kind == MicroscopeWorldCommandKind::TouchCancel;
}

[[nodiscard]] bool
isLifecycleBarrier(MicroscopeWorldCommandKind kind) noexcept {
    return kind == MicroscopeWorldCommandKind::Reset || kind == MicroscopeWorldCommandKind::Reload || kind == MicroscopeWorldCommandKind::LifecyclePause || kind == MicroscopeWorldCommandKind::LifecycleResume;
}

[[nodiscard]] bool isRequest(MicroscopeWorldCommandKind kind) noexcept {
    return kind == MicroscopeWorldCommandKind::Save || kind == MicroscopeWorldCommandKind::Apply || kind == MicroscopeWorldCommandKind::Inspect;
}

[[nodiscard]] std::optional<std::size_t>
latestSettingIndex(MicroscopeWorldCommandKind kind) noexcept {
    switch (kind) {
        case MicroscopeWorldCommandKind::SetTemperatureMode:
            return 0;
        case MicroscopeWorldCommandKind::SetToolOrdinal:
            return 1;
        case MicroscopeWorldCommandKind::SetRatePolicy:
            return 2;
        case MicroscopeWorldCommandKind::SetOptionalTargetUps:
            return 3;
        case MicroscopeWorldCommandKind::SetSlowMotionSpeed:
            return 4;
        default:
            return std::nullopt;
    }
}

template <typename T>
void storeMaximum(std::atomic<T>& destination, T value) noexcept {
    T observed = destination.load(std::memory_order_relaxed);
    while (observed < value && !destination.compare_exchange_weak(observed, value, std::memory_order_release, std::memory_order_relaxed)) {
    }
}

} // namespace

struct MicroscopeCommandCompletion::State final {
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    MicroscopeCommandCompletionSnapshot snapshot{};
};

MicroscopeCommandCompletion::MicroscopeCommandCompletion()
        : mState(std::make_unique<State>()) {}

MicroscopeCommandCompletion::~MicroscopeCommandCompletion() = default;

MicroscopeCommandCompletionSnapshot
MicroscopeCommandCompletion::snapshot() const {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    return mState->snapshot;
}

MicroscopeCommandWaitResult MicroscopeCommandCompletion::waitUntil(
        MicroscopeCommandTimePoint deadline,
        MicroscopeCommandCompletionSnapshot& snapshot) const {
    std::unique_lock<std::mutex> lock(mState->mutex);
    const bool resolved = mState->changed.wait_until(lock, deadline, [this] {
        return mState->snapshot.resolution != MicroscopeCommandResolution::Pending;
    });
    snapshot = mState->snapshot;
    return resolved ? MicroscopeCommandWaitResult::Notified
                    : MicroscopeCommandWaitResult::DeadlineReached;
}

void MicroscopeCommandCompletion::begin(
        const MicroscopeCommandCompletionSnapshot& snapshot) noexcept {
    const std::lock_guard<std::mutex> lock(mState->mutex);
    mState->snapshot = snapshot;
    mState->snapshot.resolution = MicroscopeCommandResolution::Pending;
}

void MicroscopeCommandCompletion::resolve(
        const MicroscopeCommandCompletionSnapshot& snapshot) noexcept {
    {
        const std::lock_guard<std::mutex> lock(mState->mutex);
        mState->snapshot = snapshot;
    }
    mState->changed.notify_all();
}

class OrderedCommandRing final {
public:
    [[nodiscard]] bool enqueue(const MicroscopeWorldCommand& command) noexcept {
        const std::uint64_t head = mHead.load(std::memory_order_relaxed);
        const std::uint64_t tail = mTail.load(std::memory_order_acquire);
        if (head - tail >= MicroscopeCommandMailbox::kOrderedCapacity) {
            return false;
        }
        mCommands[head % MicroscopeCommandMailbox::kOrderedCapacity] = command;
        mHead.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool peek(MicroscopeWorldCommand& command) const noexcept {
        const std::uint64_t tail = mTail.load(std::memory_order_relaxed);
        if (tail == mHead.load(std::memory_order_acquire)) {
            return false;
        }
        command = mCommands[tail % MicroscopeCommandMailbox::kOrderedCapacity];
        return true;
    }

    [[nodiscard]] bool pop(MicroscopeWorldCommand& command) noexcept {
        const std::uint64_t tail = mTail.load(std::memory_order_relaxed);
        if (tail == mHead.load(std::memory_order_acquire)) {
            return false;
        }
        command = mCommands[tail % MicroscopeCommandMailbox::kOrderedCapacity];
        mTail.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::uint32_t depth() const noexcept {
        const std::uint64_t head = mHead.load(std::memory_order_acquire);
        const std::uint64_t tail = mTail.load(std::memory_order_acquire);
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(
                head - tail, MicroscopeCommandMailbox::kOrderedCapacity));
    }

private:
    std::array<MicroscopeWorldCommand, MicroscopeCommandMailbox::kOrderedCapacity>
            mCommands{};
    std::atomic<std::uint64_t> mHead{0};
    std::atomic<std::uint64_t> mTail{0};
};

struct LatestCommandChannel final {
    LatestCommandExchange exchange;
    std::atomic<std::uint64_t> consumedSequence{0};
    std::atomic<std::uint64_t> deferredSequence{0};
    std::uint64_t producerLastPublishedSequence = 0;
    std::optional<MicroscopeWorldCommand> deferred;
};

struct PointerCommandChannel final {
    LatestCommandChannel position;
    std::atomic<PointerProducerState> producerState{PointerProducerState::Free};
    std::atomic<int> pointerId{-1};
    std::atomic<std::uint64_t> generation{0};
};

struct MicroscopeCommandMailbox::State final {
    explicit State(std::uint64_t initialWorldEpoch)
            : worldEpoch(initialWorldEpoch), producerEpoch(initialWorldEpoch) {}

    OrderedCommandRing ordered;
    std::array<PointerCommandChannel, kPointerCapacity> pointers{};
    std::array<LatestCommandChannel, kLatestSettingCount> latestSettings{};

    std::atomic<std::uint64_t> worldEpoch;
    std::uint64_t producerEpoch;
    std::uint64_t producerSequence = 0;
    std::uint32_t terminalReservations = 0;

    std::atomic<std::uint32_t> pathSegmentDepth{0};
    std::atomic<std::uint32_t> lifecycleDepth{0};
    std::atomic<std::uint64_t> inFlightSequence{0};

    std::atomic<std::uint64_t> lastIssuedSequence{0};
    std::atomic<std::uint64_t> lastResolvedSequence{0};
    std::atomic<std::uint64_t> watermarkRefreshThrough{0};
    std::atomic<std::uint64_t> appliedWorldRevision{0};
    std::atomic<std::uint64_t> presentedResolvedSequence{0};
    std::atomic<std::uint64_t> presentedWorldRevision{0};
    std::atomic<std::int64_t> lastPresentedNanos{0};

    std::atomic<std::uint64_t> pendingViewportCaptureRevision{0};
    std::atomic<std::uint64_t> notificationGeneration{1};
    mutable std::mutex notificationMutex;
    mutable std::condition_variable notificationChanged;

    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> applied{0};
    std::atomic<std::uint64_t> appliedWorldChanges{0};
    std::atomic<std::uint64_t> coalesced{0};
    std::atomic<std::uint64_t> rejected{0};
    std::atomic<std::uint64_t> staleEpoch{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> terminalAccepted{0};
    std::atomic<std::uint64_t> lifecycleAccepted{0};
    std::atomic<std::uint64_t> urgentCaptureRequests{0};
    std::atomic<std::uint64_t> viewportCaptureHints{0};
    std::atomic<std::uint64_t> viewportCaptureHintsCoalesced{0};
    std::atomic<std::uint64_t> viewportCaptureHintsConsumed{0};
    std::atomic<std::uint64_t> notificationCount{0};
    std::atomic<std::uint32_t> orderedHighWater{0};
    std::atomic<std::uint32_t> pathSegmentHighWater{0};

    void notifyOwner() noexcept {
        saturatingIncrement(notificationGeneration);
        saturatingIncrement(notificationCount);
        notificationChanged.notify_one();
    }

    void synchronizeProducerEpoch() noexcept {
        const std::uint64_t current = worldEpoch.load(std::memory_order_acquire);
        if (producerEpoch == current) {
            return;
        }
        producerEpoch = current;
        terminalReservations = 0;
        for (PointerCommandChannel& pointer : pointers) {
            pointer.producerState.store(PointerProducerState::Free,
                    std::memory_order_release);
            pointer.pointerId.store(-1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> issueSequence() noexcept {
        if (producerSequence == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        ++producerSequence;
        return producerSequence;
    }

    void publishIssued(std::uint64_t sequence) noexcept {
        lastIssuedSequence.store(sequence, std::memory_order_release);
        notifyOwner();
    }

    [[nodiscard]] MicroscopeWorldCommand
    command(MicroscopeWorldCommandKind kind, std::uint64_t sequence,
            MicroscopeCommandTimePoint enqueuedAt) const noexcept {
        MicroscopeWorldCommand result;
        result.kind = kind;
        result.sequence = sequence;
        result.worldEpoch = producerEpoch;
        result.enqueuedNanos = timePointNanos(enqueuedAt);
        return result;
    }

    void recordAccepted() noexcept {
        saturatingIncrement(accepted);
    }

    void recordRejected() noexcept {
        saturatingIncrement(rejected);
    }

    void recordOrderedDepth() noexcept {
        updateMaximum(orderedHighWater, ordered.depth());
    }

    [[nodiscard]] std::uint32_t activePointerGenerationCount() const noexcept {
        std::uint32_t count = 0;
        for (const PointerCommandChannel& pointer : pointers) {
            if (pointer.producerState.load(std::memory_order_acquire) != PointerProducerState::Free) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::optional<std::size_t>
    findActivePointer(int requestedPointerId) const noexcept {
        for (std::size_t index = 0; index < pointers.size(); ++index) {
            const PointerCommandChannel& pointer = pointers[index];
            if (pointer.producerState.load(std::memory_order_acquire) == PointerProducerState::Active && pointer.pointerId.load(std::memory_order_relaxed) == requestedPointerId) {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> findFreePointer() const noexcept {
        for (std::size_t index = 0; index < pointers.size(); ++index) {
            if (pointers[index].producerState.load(std::memory_order_acquire) == PointerProducerState::Free) {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool normalOrderedAdmission(
            std::uint32_t additionalTerminalReservations = 0) const noexcept {
        return static_cast<std::uint64_t>(ordered.depth()) + 1U + terminalReservations + additionalTerminalReservations + kLifecycleReserve <= kOrderedCapacity;
    }

    bool publishLatest(LatestCommandChannel& channel,
            const MicroscopeWorldCommand& command,
            MicroscopeCommandEnqueueResult& result) {
        if (!channel.exchange.publish(command, command.sequence)) {
            result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
            recordRejected();
            publishIssued(command.sequence);
            return false;
        }
        const std::uint64_t previous = channel.producerLastPublishedSequence;
        channel.producerLastPublishedSequence = command.sequence;
        if (previous > channel.consumedSequence.load(std::memory_order_acquire)) {
            result.status =
                    MicroscopeCommandEnqueueStatus::AcceptedAndCoalescedPrevious;
            result.coalescedSequence = previous;
            saturatingIncrement(coalesced);
        } else {
            result.status = MicroscopeCommandEnqueueStatus::Accepted;
        }
        recordAccepted();
        publishIssued(command.sequence);
        return true;
    }
};

MicroscopeCommandTimePoint microscopeCommandNow() noexcept {
    return std::chrono::time_point_cast<MicroscopeCommandDuration>(
            std::chrono::steady_clock::now());
}

MicroscopeCommandMailbox::MicroscopeCommandMailbox(
        std::uint64_t initialWorldEpoch) {
    if (initialWorldEpoch == 0) {
        throw std::invalid_argument(
                "Microscope command mailbox requires a non-zero world epoch");
    }
    mState = std::make_unique<State>(initialWorldEpoch);
}

MicroscopeCommandMailbox::~MicroscopeCommandMailbox() = default;

namespace {

MicroscopeCommandEnqueueResult
sequenceExhaustedResult(std::uint64_t worldEpoch) noexcept {
    return {0, worldEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedSequenceExhausted};
}

} // namespace

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueTouchDown(
        int pointerId, double worldX, double worldY,
        std::uint64_t eventViewportRevision,
        MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command = state.command(
            MicroscopeWorldCommandKind::TouchDown, *sequence, enqueuedAt);
    command.pointerId = pointerId;
    command.worldX = worldX;
    command.worldY = worldY;
    command.eventViewportRevision = eventViewportRevision;
    MicroscopeCommandEnqueueResult result{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };

    if (pointerId < 0 || eventViewportRevision == 0 || !std::isfinite(worldX) || !std::isfinite(worldY)) {
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    if (state.findActivePointer(pointerId).has_value()) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedPointerState;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    const auto pointerSlot = state.findFreePointer();
    if (!pointerSlot.has_value()) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedPointerCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    if (!state.normalOrderedAdmission(1)) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }

    PointerCommandChannel& pointer = state.pointers[*pointerSlot];
    std::uint64_t generation = pointer.generation.load(std::memory_order_relaxed);
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedSequenceExhausted;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    ++generation;
    pointer.pointerId.store(pointerId, std::memory_order_relaxed);
    pointer.generation.store(generation, std::memory_order_relaxed);
    pointer.producerState.store(PointerProducerState::Active,
            std::memory_order_release);
    command.pointerSlot = static_cast<int>(*pointerSlot);
    command.pointerGeneration = generation;

    if (!state.ordered.enqueue(command)) {
        pointer.pointerId.store(-1, std::memory_order_relaxed);
        pointer.producerState.store(PointerProducerState::Free,
                std::memory_order_release);
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    ++state.terminalReservations;
    result.status = MicroscopeCommandEnqueueStatus::Accepted;
    state.recordAccepted();
    state.recordOrderedDepth();
    state.publishIssued(*sequence);
    return result;
}

MicroscopeCommandEnqueueResult
MicroscopeCommandMailbox::enqueueTouchMovePosition(
        int pointerId, double worldX, double worldY,
        std::uint64_t eventViewportRevision,
        MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeCommandEnqueueResult result{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    if (pointerId < 0 || eventViewportRevision == 0 || !std::isfinite(worldX) || !std::isfinite(worldY)) {
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    const auto pointerSlot = state.findActivePointer(pointerId);
    if (!pointerSlot.has_value()) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedPointerState;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }

    PointerCommandChannel& pointer = state.pointers[*pointerSlot];
    MicroscopeWorldCommand command = state.command(
            MicroscopeWorldCommandKind::TouchMovePosition, *sequence, enqueuedAt);
    command.pointerId = pointerId;
    command.pointerSlot = static_cast<int>(*pointerSlot);
    command.pointerGeneration =
            pointer.generation.load(std::memory_order_relaxed);
    command.worldX = worldX;
    command.worldY = worldY;
    command.eventViewportRevision = eventViewportRevision;
    state.publishLatest(pointer.position, command, result);
    return result;
}

MicroscopeCommandEnqueueResult
MicroscopeCommandMailbox::enqueueTouchMovePathSegment(
        int pointerId, double worldX, double worldY,
        std::uint64_t eventViewportRevision,
        MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeCommandEnqueueResult result{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    if (pointerId < 0 || eventViewportRevision == 0 || !std::isfinite(worldX) || !std::isfinite(worldY)) {
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    const auto pointerSlot = state.findActivePointer(pointerId);
    if (!pointerSlot.has_value()) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedPointerState;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    const std::uint32_t previousPathDepth =
            state.pathSegmentDepth.fetch_add(1, std::memory_order_acq_rel);
    if (previousPathDepth >= kPathSegmentCapacity) {
        state.pathSegmentDepth.fetch_sub(1, std::memory_order_acq_rel);
        result.status = MicroscopeCommandEnqueueStatus::RejectedPathCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    if (!state.normalOrderedAdmission()) {
        state.pathSegmentDepth.fetch_sub(1, std::memory_order_acq_rel);
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }

    PointerCommandChannel& pointer = state.pointers[*pointerSlot];
    MicroscopeWorldCommand command = state.command(
            MicroscopeWorldCommandKind::TouchMovePathSegment, *sequence, enqueuedAt);
    command.pointerId = pointerId;
    command.pointerSlot = static_cast<int>(*pointerSlot);
    command.pointerGeneration =
            pointer.generation.load(std::memory_order_relaxed);
    command.worldX = worldX;
    command.worldY = worldY;
    command.eventViewportRevision = eventViewportRevision;
    if (!state.ordered.enqueue(command)) {
        state.pathSegmentDepth.fetch_sub(1, std::memory_order_acq_rel);
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    const std::uint32_t pathDepth = previousPathDepth + 1;
    updateMaximum(state.pathSegmentHighWater, pathDepth);
    result.status = MicroscopeCommandEnqueueStatus::Accepted;
    state.recordAccepted();
    state.recordOrderedDepth();
    state.publishIssued(*sequence);
    return result;
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueTerminalCommand(
        MicroscopeWorldCommandKind kind, int pointerId,
        MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeCommandEnqueueResult result{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    if (pointerId < 0) {
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    const auto pointerSlot = state.findActivePointer(pointerId);
    if (!pointerSlot.has_value()) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedPointerState;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }

    PointerCommandChannel& pointer = state.pointers[*pointerSlot];
    pointer.producerState.store(PointerProducerState::Closing,
            std::memory_order_release);
    MicroscopeWorldCommand command = state.command(kind, *sequence, enqueuedAt);
    command.pointerId = pointerId;
    command.pointerSlot = static_cast<int>(*pointerSlot);
    command.pointerGeneration =
            pointer.generation.load(std::memory_order_relaxed);
    if (!state.ordered.enqueue(command)) {
        pointer.producerState.store(PointerProducerState::Active,
                std::memory_order_release);
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    if (state.terminalReservations > 0) {
        --state.terminalReservations;
    }
    result.status = MicroscopeCommandEnqueueStatus::Accepted;
    state.recordAccepted();
    saturatingIncrement(state.terminalAccepted);
    state.recordOrderedDepth();
    state.publishIssued(*sequence);
    return result;
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueTouchUp(
        int pointerId, MicroscopeCommandTimePoint enqueuedAt) {
    return enqueueTerminalCommand(MicroscopeWorldCommandKind::TouchUp, pointerId,
            enqueuedAt);
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueTouchCancel(
        int pointerId, MicroscopeCommandTimePoint enqueuedAt) {
    return enqueueTerminalCommand(MicroscopeWorldCommandKind::TouchCancel,
            pointerId, enqueuedAt);
}

MicroscopeActivePointerCancelResult
MicroscopeCommandMailbox::enqueueActivePointerCancels(
        MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    MicroscopeActivePointerCancelResult result;
    result.activeBefore = state.activePointerGenerationCount();
    for (PointerCommandChannel& pointer : state.pointers) {
        if (pointer.producerState.load(std::memory_order_acquire)
                != PointerProducerState::Active) {
            continue;
        }
        const int pointerId = pointer.pointerId.load(std::memory_order_relaxed);
        const auto admission = enqueueTerminalCommand(
                MicroscopeWorldCommandKind::TouchCancel,
                pointerId,
                enqueuedAt);
        if (!admission.accepted()) {
            result.invariantSatisfied = false;
            continue;
        }
        ++result.cancelsEnqueued;
    }
    return result;
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueTap(
        double worldX, double worldY, std::uint64_t eventViewportRevision,
        std::shared_ptr<const MicroscopeCommandGenome> genome,
        MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command =
            state.command(MicroscopeWorldCommandKind::Tap, *sequence, enqueuedAt);
    command.worldX = worldX;
    command.worldY = worldY;
    command.eventViewportRevision = eventViewportRevision;
    command.genome = std::move(genome);
    MicroscopeCommandEnqueueResult result{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    if (eventViewportRevision == 0 || !std::isfinite(worldX) || !std::isfinite(worldY)) {
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    if (!state.normalOrderedAdmission() || !state.ordered.enqueue(command)) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        return result;
    }
    result.status = MicroscopeCommandEnqueueStatus::Accepted;
    state.recordAccepted();
    state.recordOrderedDepth();
    state.publishIssued(*sequence);
    return result;
}

MicroscopeCommandEnqueueResult
MicroscopeCommandMailbox::enqueueLatestSettingCommand(
        MicroscopeWorldCommand command, MicroscopeCommandTimePoint rejectedAt) {
    State& state = *mState;
    MicroscopeCommandEnqueueResult result{
            command.sequence, command.worldEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    const auto index = latestSettingIndex(command.kind);
    if (!index.has_value()) {
        state.recordRejected();
        state.publishIssued(command.sequence);
        if (command.completion != nullptr) {
            command.completion->resolve({
                    command.sequence,
                    command.worldEpoch,
                    state.appliedWorldRevision.load(std::memory_order_acquire),
                    MicroscopeCommandResolution::RejectedAtIngress,
                    command.enqueuedNanos,
                    timePointNanos(rejectedAt),
            });
        }
        return result;
    }
    state.publishLatest(state.latestSettings[*index], command, result);
    return result;
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueTemperatureMode(
        int temperatureMode, MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command = state.command(
            MicroscopeWorldCommandKind::SetTemperatureMode, *sequence, enqueuedAt);
    command.integerValue = temperatureMode;
    return enqueueLatestSettingCommand(std::move(command), enqueuedAt);
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueToolOrdinal(
        int toolOrdinal, MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command = state.command(
            MicroscopeWorldCommandKind::SetToolOrdinal, *sequence, enqueuedAt);
    command.integerValue = toolOrdinal;
    return enqueueLatestSettingCommand(std::move(command), enqueuedAt);
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueSlowMotionSpeed(
        double slowMotionSpeed, MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command = state.command(
            MicroscopeWorldCommandKind::SetSlowMotionSpeed, *sequence, enqueuedAt);
    command.numericValue = slowMotionSpeed;
    command.hasNumericValue = true;
    MicroscopeCommandEnqueueResult invalid{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    if (!std::isfinite(slowMotionSpeed) || slowMotionSpeed < 0.0) {
        state.recordRejected();
        state.publishIssued(*sequence);
        return invalid;
    }
    return enqueueLatestSettingCommand(std::move(command), enqueuedAt);
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueRatePolicy(
        simulation::SimulationRatePolicyKind policy,
        std::optional<double> targetUps, MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command = state.command(
            MicroscopeWorldCommandKind::SetRatePolicy, *sequence, enqueuedAt);
    command.ratePolicy = policy;
    command.hasNumericValue = targetUps.has_value();
    command.numericValue = targetUps.value_or(0.0);
    MicroscopeCommandEnqueueResult invalid{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    const bool targetPolicy =
            policy == simulation::SimulationRatePolicyKind::TargetRate;
    const bool validPolicy =
            policy == simulation::SimulationRatePolicyKind::Paused || targetPolicy || policy == simulation::SimulationRatePolicyKind::MaxThroughput;
    if (!validPolicy || targetPolicy != targetUps.has_value() || (targetUps.has_value() && (!std::isfinite(*targetUps) || *targetUps <= 0.0))) {
        state.recordRejected();
        state.publishIssued(*sequence);
        return invalid;
    }
    return enqueueLatestSettingCommand(std::move(command), enqueuedAt);
}

MicroscopeCommandEnqueueResult
MicroscopeCommandMailbox::enqueueOptionalTargetUps(
        std::optional<double> targetUps, MicroscopeCommandTimePoint enqueuedAt) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command = state.command(
            MicroscopeWorldCommandKind::SetOptionalTargetUps, *sequence, enqueuedAt);
    command.hasNumericValue = targetUps.has_value();
    command.numericValue = targetUps.value_or(0.0);
    MicroscopeCommandEnqueueResult invalid{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    if (targetUps.has_value() && (!std::isfinite(*targetUps) || *targetUps <= 0.0)) {
        state.recordRejected();
        state.publishIssued(*sequence);
        return invalid;
    }
    return enqueueLatestSettingCommand(std::move(command), enqueuedAt);
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueBarrier(
        MicroscopeWorldCommandKind kind, std::uint64_t requestId,
        std::shared_ptr<MicroscopeCommandCompletion> completion,
        MicroscopeCommandTimePoint enqueuedAt,
        std::shared_ptr<MicroscopeCommandPayload> payload) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command = state.command(kind, *sequence, enqueuedAt);
    command.requestId = requestId;
    command.completion = std::move(completion);
    command.payload = std::move(payload);
    if (command.completion != nullptr) {
        command.completion->begin({
                command.sequence,
                command.worldEpoch,
                state.appliedWorldRevision.load(std::memory_order_acquire),
                MicroscopeCommandResolution::Pending,
                command.enqueuedNanos,
                0,
        });
    }
    MicroscopeCommandEnqueueResult result{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    if (!isLifecycleBarrier(kind)) {
        state.recordRejected();
        state.publishIssued(*sequence);
        if (command.completion != nullptr) {
            command.completion->resolve({
                    command.sequence,
                    command.worldEpoch,
                    state.appliedWorldRevision.load(std::memory_order_acquire),
                    MicroscopeCommandResolution::RejectedAtIngress,
                    command.enqueuedNanos,
                    timePointNanos(enqueuedAt),
            });
        }
        return result;
    }
    const std::uint32_t previousLifecycleDepth =
            state.lifecycleDepth.fetch_add(1, std::memory_order_acq_rel);
    if (previousLifecycleDepth >= kLifecycleReserve) {
        state.lifecycleDepth.fetch_sub(1, std::memory_order_acq_rel);
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        if (command.completion != nullptr) {
            command.completion->resolve({
                    command.sequence,
                    command.worldEpoch,
                    state.appliedWorldRevision.load(std::memory_order_acquire),
                    MicroscopeCommandResolution::RejectedAtIngress,
                    command.enqueuedNanos,
                    timePointNanos(enqueuedAt),
            });
        }
        return result;
    }
    if (state.ordered.depth() >= kOrderedCapacity || !state.ordered.enqueue(command)) {
        state.lifecycleDepth.fetch_sub(1, std::memory_order_acq_rel);
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        if (command.completion != nullptr) {
            command.completion->resolve({
                    command.sequence,
                    command.worldEpoch,
                    state.appliedWorldRevision.load(std::memory_order_acquire),
                    MicroscopeCommandResolution::RejectedAtIngress,
                    command.enqueuedNanos,
                    timePointNanos(enqueuedAt),
            });
        }
        return result;
    }
    result.status = MicroscopeCommandEnqueueStatus::Accepted;
    state.recordAccepted();
    saturatingIncrement(state.lifecycleAccepted);
    state.recordOrderedDepth();
    state.publishIssued(*sequence);
    return result;
}

MicroscopeCommandEnqueueResult MicroscopeCommandMailbox::enqueueRequest(
        MicroscopeWorldCommandKind kind, std::uint64_t requestId,
        std::shared_ptr<MicroscopeCommandCompletion> completion,
        MicroscopeCommandTimePoint enqueuedAt,
        std::shared_ptr<MicroscopeCommandPayload> payload) {
    State& state = *mState;
    state.synchronizeProducerEpoch();
    const auto sequence = state.issueSequence();
    if (!sequence.has_value()) {
        return sequenceExhaustedResult(state.producerEpoch);
    }
    MicroscopeWorldCommand command = state.command(kind, *sequence, enqueuedAt);
    command.requestId = requestId;
    command.completion = std::move(completion);
    command.payload = std::move(payload);
    if (command.completion != nullptr) {
        command.completion->begin({
                command.sequence,
                command.worldEpoch,
                state.appliedWorldRevision.load(std::memory_order_acquire),
                MicroscopeCommandResolution::Pending,
                command.enqueuedNanos,
                0,
        });
    }
    MicroscopeCommandEnqueueResult result{
            *sequence, state.producerEpoch, 0,
            MicroscopeCommandEnqueueStatus::RejectedInvalidArgument
    };
    if (!isRequest(kind) || command.completion == nullptr || requestId == 0) {
        state.recordRejected();
        state.publishIssued(*sequence);
        if (command.completion != nullptr) {
            command.completion->resolve({
                    command.sequence,
                    command.worldEpoch,
                    state.appliedWorldRevision.load(std::memory_order_acquire),
                    MicroscopeCommandResolution::RejectedAtIngress,
                    command.enqueuedNanos,
                    timePointNanos(enqueuedAt),
            });
        }
        return result;
    }
    if (!state.normalOrderedAdmission() || !state.ordered.enqueue(command)) {
        result.status = MicroscopeCommandEnqueueStatus::RejectedOrderedCapacity;
        state.recordRejected();
        state.publishIssued(*sequence);
        if (command.completion != nullptr) {
            command.completion->resolve({
                    command.sequence,
                    command.worldEpoch,
                    state.appliedWorldRevision.load(std::memory_order_acquire),
                    MicroscopeCommandResolution::RejectedAtIngress,
                    command.enqueuedNanos,
                    timePointNanos(enqueuedAt),
            });
        }
        return result;
    }
    result.status = MicroscopeCommandEnqueueStatus::Accepted;
    state.recordAccepted();
    state.recordOrderedDepth();
    state.publishIssued(*sequence);
    return result;
}

void MicroscopeCommandMailbox::publishViewportCaptureHint(
        std::uint64_t viewportRevision) noexcept {
    if (viewportRevision == 0) {
        return;
    }
    State& state = *mState;
    const std::uint64_t previous = state.pendingViewportCaptureRevision.exchange(
            viewportRevision, std::memory_order_acq_rel);
    saturatingIncrement(state.viewportCaptureHints);
    if (previous != 0) {
        saturatingIncrement(state.viewportCaptureHintsCoalesced);
    }
    state.notifyOwner();
}

std::optional<std::uint64_t>
MicroscopeCommandMailbox::consumeViewportCaptureHint() noexcept {
    const std::uint64_t revision =
            mState->pendingViewportCaptureRevision.exchange(
                    0, std::memory_order_acq_rel);
    if (revision == 0) {
        return std::nullopt;
    }
    saturatingIncrement(mState->viewportCaptureHintsConsumed);
    return revision;
}

bool MicroscopeCommandMailbox::hasPendingOwnerWork() const noexcept {
    const State& state = *mState;
    if (state.ordered.depth() != 0 || state.inFlightSequence.load(std::memory_order_acquire) != 0 || state.lastIssuedSequence.load(std::memory_order_acquire) != state.watermarkRefreshThrough.load(std::memory_order_acquire) || state.pendingViewportCaptureRevision.load(std::memory_order_acquire) != 0) {
        return true;
    }
    for (const PointerCommandChannel& pointer : state.pointers) {
        const LatestCommandChannel& position = pointer.position;
        if (position.deferredSequence.load(std::memory_order_acquire) != 0 || position.exchange.latestPublishedSequence() > position.consumedSequence.load(std::memory_order_acquire)) {
            return true;
        }
    }
    for (const LatestCommandChannel& setting : state.latestSettings) {
        if (setting.deferredSequence.load(std::memory_order_acquire) != 0 || setting.exchange.latestPublishedSequence() > setting.consumedSequence.load(std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

std::uint64_t
MicroscopeCommandMailbox::notificationGeneration() const noexcept {
    return mState->notificationGeneration.load(std::memory_order_acquire);
}

bool MicroscopeCommandMailbox::hasNotificationSince(
        std::uint64_t observedGeneration) const noexcept {
    return notificationGeneration() != observedGeneration;
}

MicroscopeCommandWaitResult MicroscopeCommandMailbox::waitForNotification(
        std::uint64_t observedGeneration,
        MicroscopeCommandTimePoint deadline) const {
    State& state = *mState;
    std::unique_lock<std::mutex> lock(state.notificationMutex);
    const bool notified = state.notificationChanged.wait_until(
            lock, deadline, [&state, observedGeneration] {
                return state.notificationGeneration.load(std::memory_order_acquire) != observedGeneration;
            });
    return notified ? MicroscopeCommandWaitResult::Notified
                    : MicroscopeCommandWaitResult::DeadlineReached;
}

void MicroscopeCommandMailbox::notifyOwner() noexcept {
    mState->notifyOwner();
}

void MicroscopeCommandMailbox::advanceWorldEpoch(std::uint64_t worldEpoch,
        MicroscopeCommandTimePoint) {
    if (worldEpoch == 0) {
        throw std::invalid_argument("Microscope command epoch cannot be zero");
    }
    State& state = *mState;
    // Capture the retired-epoch boundary before publishing the new epoch. A
    // producer that observes the new epoch must never be included in the
    // already-resolved range by this transition.
    const std::uint64_t retiredSequenceBoundary =
            state.lastIssuedSequence.load(std::memory_order_acquire);
    const std::uint64_t previous =
            state.worldEpoch.exchange(worldEpoch, std::memory_order_acq_rel);
    if (previous == worldEpoch) {
        return;
    }
    state.appliedWorldRevision.store(0, std::memory_order_release);
    state.presentedResolvedSequence.store(0, std::memory_order_release);
    state.presentedWorldRevision.store(0, std::memory_order_release);
    state.lastPresentedNanos.store(0, std::memory_order_release);
    state.pendingViewportCaptureRevision.store(0, std::memory_order_release);
    state.lastResolvedSequence.store(retiredSequenceBoundary,
            std::memory_order_release);
    state.notifyOwner();
}

void MicroscopeCommandMailbox::markPresented(
        std::uint64_t worldEpoch, std::uint64_t resolvedInputSequence,
        std::uint64_t appliedWorldRevision,
        MicroscopeCommandTimePoint presentedAt) noexcept {
    State& state = *mState;
    if (worldEpoch != state.worldEpoch.load(std::memory_order_acquire)) {
        return;
    }
    const std::uint64_t resolvedLimit =
            state.lastResolvedSequence.load(std::memory_order_acquire);
    const std::uint64_t worldLimit =
            state.appliedWorldRevision.load(std::memory_order_acquire);
    storeMaximum(state.presentedResolvedSequence,
            std::min(resolvedInputSequence, resolvedLimit));
    storeMaximum(state.presentedWorldRevision,
            std::min(appliedWorldRevision, worldLimit));
    state.lastPresentedNanos.store(timePointNanos(presentedAt),
            std::memory_order_release);
}

MicroscopeCommandResolvedState
MicroscopeCommandMailbox::resolvedState() const noexcept {
    const State& state = *mState;
    return {
            state.worldEpoch.load(std::memory_order_acquire),
            state.lastIssuedSequence.load(std::memory_order_acquire),
            state.lastResolvedSequence.load(std::memory_order_acquire),
            state.appliedWorldRevision.load(std::memory_order_acquire),
            state.presentedResolvedSequence.load(std::memory_order_acquire),
            state.presentedWorldRevision.load(std::memory_order_acquire),
            state.lastPresentedNanos.load(std::memory_order_acquire),
    };
}

MicroscopeCommandMailboxSnapshot
MicroscopeCommandMailbox::snapshot() const noexcept {
    const State& state = *mState;
    MicroscopeCommandMailboxSnapshot result;
    result.resolved = resolvedState();
    result.counters.accepted = state.accepted.load(std::memory_order_relaxed);
    result.counters.applied = state.applied.load(std::memory_order_relaxed);
    result.counters.appliedWorldChanges =
            state.appliedWorldChanges.load(std::memory_order_relaxed);
    result.counters.coalesced = state.coalesced.load(std::memory_order_relaxed);
    result.counters.rejected = state.rejected.load(std::memory_order_relaxed);
    result.counters.staleEpoch = state.staleEpoch.load(std::memory_order_relaxed);
    result.counters.failed = state.failed.load(std::memory_order_relaxed);
    result.counters.terminalAccepted =
            state.terminalAccepted.load(std::memory_order_relaxed);
    result.counters.lifecycleAccepted =
            state.lifecycleAccepted.load(std::memory_order_relaxed);
    result.counters.urgentCaptureRequests =
            state.urgentCaptureRequests.load(std::memory_order_relaxed);
    result.counters.viewportCaptureHints =
            state.viewportCaptureHints.load(std::memory_order_relaxed);
    result.counters.viewportCaptureHintsCoalesced =
            state.viewportCaptureHintsCoalesced.load(std::memory_order_relaxed);
    result.counters.viewportCaptureHintsConsumed =
            state.viewportCaptureHintsConsumed.load(std::memory_order_relaxed);
    result.counters.notificationCount =
            state.notificationCount.load(std::memory_order_relaxed);
    result.counters.orderedDepth = state.ordered.depth();
    result.counters.orderedHighWater =
            state.orderedHighWater.load(std::memory_order_relaxed);
    result.counters.pathSegmentDepth =
            state.pathSegmentDepth.load(std::memory_order_relaxed);
    result.counters.pathSegmentHighWater =
            state.pathSegmentHighWater.load(std::memory_order_relaxed);
    result.counters.activePointerGenerations =
            state.activePointerGenerationCount();
    result.notificationGeneration =
            state.notificationGeneration.load(std::memory_order_acquire);
    result.pendingViewportCaptureRevision =
            state.pendingViewportCaptureRevision.load(std::memory_order_acquire);
    result.ownerWorkPending = hasPendingOwnerWork();
    return result;
}

std::uint32_t
MicroscopeCommandMailbox::activePointerGenerationCount() const noexcept {
    return mState->activePointerGenerationCount();
}

bool MicroscopeCommandMailbox::tryTakeNext(MicroscopeWorldCommand& command) {
    State& state = *mState;
    if (state.inFlightSequence.load(std::memory_order_acquire) != 0) {
        throw std::logic_error(
                "Microscope command mailbox already has an in-flight command");
    }

    for (std::size_t attempt = 0;
            attempt < kPointerCapacity + kLatestSettingCount + 4; ++attempt) {
        MicroscopeWorldCommand orderedCommand;
        const bool hasOrdered = state.ordered.peek(orderedCommand);
        std::uint64_t nextSequence =
                hasOrdered ? orderedCommand.sequence
                           : std::numeric_limits<std::uint64_t>::max();
        LatestCommandChannel* nextLatest = nullptr;

        auto consider = [&](LatestCommandChannel& channel) {
            std::uint64_t pending =
                    channel.deferredSequence.load(std::memory_order_acquire);
            if (pending == 0) {
                const std::uint64_t published =
                        channel.exchange.latestPublishedSequence();
                const std::uint64_t consumed =
                        channel.consumedSequence.load(std::memory_order_acquire);
                if (published > consumed) {
                    pending = published;
                }
            }
            if (pending != 0 && pending < nextSequence) {
                nextSequence = pending;
                nextLatest = &channel;
            }
        };
        for (PointerCommandChannel& pointer : state.pointers) {
            consider(pointer.position);
        }
        for (LatestCommandChannel& setting : state.latestSettings) {
            consider(setting);
        }

        if (!hasOrdered && nextLatest == nullptr) {
            return false;
        }
        if (nextLatest == nullptr) {
            if (!state.ordered.pop(command)) {
                continue;
            }
            if (command.kind == MicroscopeWorldCommandKind::TouchMovePathSegment) {
                state.pathSegmentDepth.fetch_sub(1, std::memory_order_acq_rel);
            }
            if (isLifecycleBarrier(command.kind)) {
                state.lifecycleDepth.fetch_sub(1, std::memory_order_acq_rel);
            }
            state.inFlightSequence.store(command.sequence, std::memory_order_release);
            return true;
        }

        if (nextLatest->deferred.has_value()) {
            command = std::move(*nextLatest->deferred);
            nextLatest->deferred.reset();
            state.inFlightSequence.store(command.sequence, std::memory_order_release);
            nextLatest->deferredSequence.store(0, std::memory_order_release);
            return true;
        }

        MicroscopeWorldCommand newest;
        std::uint64_t consumedSequence = 0;
        if (!nextLatest->exchange.tryConsumeNewest(newest, &consumedSequence)) {
            continue;
        }
        nextLatest->deferred = std::move(newest);
        nextLatest->deferredSequence.store(consumedSequence,
                std::memory_order_release);
        nextLatest->consumedSequence.store(consumedSequence,
                std::memory_order_release);
        // Rescan every lane with the exact consumed sequence. A concurrent
        // producer may have advanced this channel after selection; deferring the
        // payload prevents it from overtaking an older command in another lane.
        continue;
    }
    return false;
}

void MicroscopeCommandMailbox::finishTaken(
        const MicroscopeWorldCommand& command,
        MicroscopeCommandResolution resolution, bool changedWorld,
        bool requestUrgentCapture, MicroscopeCommandTimePoint resolvedAt) noexcept {
    State& state = *mState;
    if (resolution == MicroscopeCommandResolution::Applied) {
        saturatingIncrement(state.applied);
    } else if (resolution == MicroscopeCommandResolution::RejectedByOwner) {
        saturatingIncrement(state.rejected);
    } else if (resolution == MicroscopeCommandResolution::StaleEpoch) {
        saturatingIncrement(state.staleEpoch);
    } else if (resolution == MicroscopeCommandResolution::Failed) {
        saturatingIncrement(state.failed);
    }

    if (changedWorld) {
        saturatingIncrement(state.appliedWorldRevision);
        saturatingIncrement(state.appliedWorldChanges);
    }
    if (requestUrgentCapture) {
        saturatingIncrement(state.urgentCaptureRequests);
    }

    if (isTerminal(command.kind) && command.pointerSlot >= 0 && static_cast<std::uint32_t>(command.pointerSlot) < kPointerCapacity) {
        PointerCommandChannel& pointer =
                state.pointers[static_cast<std::size_t>(command.pointerSlot)];
        if (pointer.generation.load(std::memory_order_acquire) == command.pointerGeneration) {
            PointerProducerState expected = PointerProducerState::Closing;
            if (pointer.producerState.compare_exchange_strong(
                        expected, PointerProducerState::Releasing,
                        std::memory_order_acq_rel, std::memory_order_acquire)) {
                pointer.pointerId.store(-1, std::memory_order_relaxed);
                pointer.producerState.store(PointerProducerState::Free,
                        std::memory_order_release);
            }
        }
    }

    if (command.completion != nullptr) {
        command.completion->resolve({
                command.sequence,
                command.worldEpoch,
                state.appliedWorldRevision.load(std::memory_order_acquire),
                resolution,
                command.enqueuedNanos,
                timePointNanos(resolvedAt),
        });
    }
    state.inFlightSequence.store(0, std::memory_order_release);
    refreshResolvedWatermark();
}

void MicroscopeCommandMailbox::refreshResolvedWatermark() noexcept {
    State& state = *mState;
    // Take the sequence boundary before scanning lanes. A producer publishes a
    // payload before lastIssuedSequence; loading the boundary afterwards could
    // otherwise resolve a command that appeared between the scan and that load.
    const std::uint64_t refreshThrough =
            state.lastIssuedSequence.load(std::memory_order_acquire);
    std::uint64_t oldestPending = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t inFlight =
            state.inFlightSequence.load(std::memory_order_acquire);
    if (inFlight != 0) {
        oldestPending = inFlight;
    }

    MicroscopeWorldCommand ordered;
    if (state.ordered.peek(ordered)) {
        oldestPending = std::min(oldestPending, ordered.sequence);
    }
    for (const PointerCommandChannel& pointer : state.pointers) {
        const LatestCommandChannel& position = pointer.position;
        std::uint64_t pending =
                position.deferredSequence.load(std::memory_order_acquire);
        if (pending == 0 && position.exchange.latestPublishedSequence() > position.consumedSequence.load(std::memory_order_acquire)) {
            pending = position.exchange.latestPublishedSequence();
        }
        if (pending != 0) {
            oldestPending = std::min(oldestPending, pending);
        }
    }
    for (const LatestCommandChannel& setting : state.latestSettings) {
        std::uint64_t pending =
                setting.deferredSequence.load(std::memory_order_acquire);
        if (pending == 0 && setting.exchange.latestPublishedSequence() > setting.consumedSequence.load(std::memory_order_acquire)) {
            pending = setting.exchange.latestPublishedSequence();
        }
        if (pending != 0) {
            oldestPending = std::min(oldestPending, pending);
        }
    }

    const std::uint64_t candidate =
            oldestPending == std::numeric_limits<std::uint64_t>::max()
            ? refreshThrough
            : std::min(refreshThrough,
                      oldestPending == 0 ? 0 : oldestPending - 1);
    storeMaximum(state.lastResolvedSequence, candidate);
    state.watermarkRefreshThrough.store(refreshThrough,
            std::memory_order_release);
}

std::uint64_t MicroscopeCommandMailbox::currentWorldEpoch() const noexcept {
    return mState->worldEpoch.load(std::memory_order_acquire);
}

} // namespace cellularity::microscope
