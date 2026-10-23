#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace cellularity::concurrency {

enum class LatestValueSlotState : std::uint8_t {
    Free = 0,
    Writing = 1,
    Published = 2,
    Reading = 3,
};

struct LatestValueExchangeCounters final {
    std::uint64_t published = 0;
    std::uint64_t consumed = 0;
    std::uint64_t producerReplaced = 0;
    std::uint64_t consumerDiscarded = 0;
    std::uint64_t publishRetriesExhausted = 0;
};

// Fixed-storage single-producer/single-consumer publication for small value
// snapshots. The producer replaces unread values instead of creating a FIFO;
// the consumer copies the newest complete value and immediately releases its
// slot. Payload access is protected by generation-tagged slot ownership, so no
// mutex, allocation, or wait belongs to the hot publication path.
template <typename Value, std::size_t SlotCount = 3>
class LatestValueExchange final {
    static_assert(SlotCount >= 3,
            "Latest-value exchange needs at least three slots");
    static_assert(std::is_nothrow_copy_assignable<Value>::value,
            "Latest-value payload assignment must not throw");
    static_assert(std::is_nothrow_default_constructible<Value>::value,
            "Latest-value payload reset must not throw");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
            "Latest-value exchange requires lock-free 64-bit atomics");

public:
    LatestValueExchange() = default;
    LatestValueExchange(const LatestValueExchange&) = delete;
    LatestValueExchange& operator=(const LatestValueExchange&) = delete;
    LatestValueExchange(LatestValueExchange&&) = delete;
    LatestValueExchange& operator=(LatestValueExchange&&) = delete;

    [[nodiscard]] bool publish(const Value& value,
            std::uint64_t sequence) noexcept {
        if (sequence == 0) {
            return false;
        }

        for (std::size_t pass = 0; pass < SlotCount + 1; ++pass) {
            if (claimAndPublish(LatestValueSlotState::Free, value, sequence, false)) {
                return true;
            }
            if (claimAndPublish(LatestValueSlotState::Published, value, sequence,
                        true)) {
                return true;
            }
        }

        increment(mPublishRetriesExhausted);
        return false;
    }

    [[nodiscard]] bool
    tryConsumeNewest(Value& destination,
            std::uint64_t* sequence = nullptr) noexcept {
        for (std::size_t attempt = 0; attempt < SlotCount + 1; ++attempt) {
            Candidate newest;
            for (std::size_t slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
                Candidate candidate;
                if (!readCandidate(slotIndex, candidate)) {
                    continue;
                }
                if (!newest.valid || candidate.sequence > newest.sequence) {
                    newest = candidate;
                }
            }
            if (!newest.valid) {
                return false;
            }

            Slot& slot = mSlots[newest.slotIndex];
            std::uint64_t observed = newest.control;
            const std::uint64_t reading =
                    transitionTo(observed, LatestValueSlotState::Reading);
            if (!slot.control.compare_exchange_strong(observed, reading,
                        std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                continue;
            }

            destination = slot.value;
            if (sequence != nullptr) {
                *sequence = newest.sequence;
            }

            for (std::size_t slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
                if (slotIndex == newest.slotIndex) {
                    continue;
                }
                releasePublishedAtOrBefore(slotIndex, newest.sequence);
            }

            slot.value = Value{};
            slot.control.store(transitionTo(reading, LatestValueSlotState::Free),
                    std::memory_order_release);
            increment(mConsumed);
            return true;
        }
        return false;
    }

    [[nodiscard]] std::uint64_t latestPublishedSequence() const noexcept {
        return mLatestPublishedSequence.load(std::memory_order_acquire);
    }

    [[nodiscard]] LatestValueExchangeCounters counters() const noexcept {
        return {
                mPublished.load(std::memory_order_relaxed),
                mConsumed.load(std::memory_order_relaxed),
                mProducerReplaced.load(std::memory_order_relaxed),
                mConsumerDiscarded.load(std::memory_order_relaxed),
                mPublishRetriesExhausted.load(std::memory_order_relaxed),
        };
    }

private:
    static constexpr std::uint64_t kStateMask = 0x3U;
    static constexpr unsigned kStateBits = 2;

    struct Slot final {
        Value value{};
        std::atomic<std::uint64_t> control{
                static_cast<std::uint64_t>(LatestValueSlotState::Free)
        };
        std::atomic<std::uint64_t> sequence{0};
    };

    struct Candidate final {
        bool valid = false;
        std::size_t slotIndex = 0;
        std::uint64_t control = 0;
        std::uint64_t sequence = 0;
    };

    [[nodiscard]] static LatestValueSlotState
    stateOf(std::uint64_t control) noexcept {
        return static_cast<LatestValueSlotState>(control & kStateMask);
    }

    [[nodiscard]] static std::uint64_t
    transitionTo(std::uint64_t control, LatestValueSlotState state) noexcept {
        const std::uint64_t generation = (control >> kStateBits) + 1U;
        return (generation << kStateBits) | static_cast<std::uint64_t>(state);
    }

    [[nodiscard]] bool readCandidate(std::size_t slotIndex,
            Candidate& candidate) const noexcept {
        const Slot& slot = mSlots[slotIndex];
        const std::uint64_t before = slot.control.load(std::memory_order_acquire);
        if (stateOf(before) != LatestValueSlotState::Published) {
            return false;
        }
        const std::uint64_t publishedSequence =
                slot.sequence.load(std::memory_order_relaxed);
        if (slot.control.load(std::memory_order_acquire) != before) {
            return false;
        }
        candidate = {true, slotIndex, before, publishedSequence};
        return true;
    }

    [[nodiscard]] bool claimAndPublish(LatestValueSlotState expectedState,
            const Value& value, std::uint64_t sequence,
            bool replacement) noexcept {
        Candidate candidate;
        for (std::size_t offset = 0; offset < SlotCount; ++offset) {
            const std::size_t slotIndex = (mNextProbe + offset) % SlotCount;
            const Slot& slot = mSlots[slotIndex];
            const std::uint64_t observed =
                    slot.control.load(std::memory_order_acquire);
            if (stateOf(observed) != expectedState) {
                continue;
            }
            if (!candidate.valid || (replacement && slot.sequence.load(std::memory_order_relaxed) < candidate.sequence)) {
                candidate = {true, slotIndex, observed,
                        slot.sequence.load(std::memory_order_relaxed)};
                if (!replacement) {
                    break;
                }
            }
        }
        if (!candidate.valid) {
            return false;
        }

        Slot& slot = mSlots[candidate.slotIndex];
        std::uint64_t observed = candidate.control;
        const std::uint64_t writing =
                transitionTo(observed, LatestValueSlotState::Writing);
        if (!slot.control.compare_exchange_strong(observed, writing,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
            return false;
        }

        slot.value = value;
        slot.sequence.store(sequence, std::memory_order_relaxed);
        slot.control.store(transitionTo(writing, LatestValueSlotState::Published),
                std::memory_order_release);
        mLatestPublishedSequence.store(sequence, std::memory_order_release);
        mNextProbe = (candidate.slotIndex + 1U) % SlotCount;
        increment(mPublished);
        if (replacement) {
            increment(mProducerReplaced);
        }
        return true;
    }

    void releasePublishedAtOrBefore(std::size_t slotIndex,
            std::uint64_t newestSequence) noexcept {
        Slot& slot = mSlots[slotIndex];
        for (;;) {
            const std::uint64_t observed =
                    slot.control.load(std::memory_order_acquire);
            if (stateOf(observed) != LatestValueSlotState::Published || slot.sequence.load(std::memory_order_relaxed) > newestSequence) {
                return;
            }
            std::uint64_t expected = observed;
            const std::uint64_t reading =
                    transitionTo(observed, LatestValueSlotState::Reading);
            if (slot.control.compare_exchange_strong(
                        expected, reading,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                slot.value = Value{};
                slot.control.store(
                        transitionTo(reading, LatestValueSlotState::Free),
                        std::memory_order_release);
                increment(mConsumerDiscarded);
                return;
            }
        }
    }

    static void increment(std::atomic<std::uint64_t>& value) noexcept {
        std::uint64_t observed = value.load(std::memory_order_relaxed);
        while (observed != std::numeric_limits<std::uint64_t>::max() && !value.compare_exchange_weak(observed, observed + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    std::array<Slot, SlotCount> mSlots{};
    std::size_t mNextProbe = 0;
    std::atomic<std::uint64_t> mLatestPublishedSequence{0};
    std::atomic<std::uint64_t> mPublished{0};
    std::atomic<std::uint64_t> mConsumed{0};
    std::atomic<std::uint64_t> mProducerReplaced{0};
    std::atomic<std::uint64_t> mConsumerDiscarded{0};
    std::atomic<std::uint64_t> mPublishRetriesExhausted{0};
};

} // namespace cellularity::concurrency
