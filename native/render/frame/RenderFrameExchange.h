#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderSourceFrame.h"

namespace cellularity::render {

enum class RenderFrameSlotState : std::uint8_t {
    Free = 0,
    Writing = 1,
    Published = 2,
    Reading = 3,
};

struct RenderFrameSlotMetadata final {
    std::uint64_t publicationTicket = 0;
    std::uint64_t frameSequence = 0;
    std::uint64_t worldEpoch = 0;
};

struct RenderFrameExchangeCounters final {
    std::uint64_t publishedFrameCount = 0;
    std::uint64_t acquiredFrameCount = 0;
    std::uint64_t producerReplacedPublishedFrameCount = 0;
    std::uint64_t consumerDiscardedPublishedFrameCount = 0;
    std::uint64_t incomingDroppedFrameCount = 0;
    std::uint64_t abandonedWriteCount = 0;
    std::uint64_t rejectedPublicationCount = 0;

    [[nodiscard]] std::uint64_t droppedFrameCount() const noexcept {
        return producerReplacedPublishedFrameCount + consumerDiscardedPublishedFrameCount + incomingDroppedFrameCount;
    }
};

struct RenderFrameExchangeSlotStorage final {
    RenderFrameSlotState state = RenderFrameSlotState::Free;
    RenderFrameSlotMetadata metadata{};
    std::uint64_t activeStorageBytes = 0;
    std::uint64_t retainedStorageBytes = 0;
};

template <std::size_t SlotCount>
struct RenderFrameExchangeStorageSnapshot final {
    std::array<RenderFrameExchangeSlotStorage, SlotCount> slots{};
    std::uint64_t totalActiveStorageBytes = 0;
    std::uint64_t totalRetainedStorageBytes = 0;
};

// Fixed-capacity single-producer/single-consumer frame hand-off. Producer and
// consumer endpoints are unique members of the exchange. Calls on either
// endpoint must stay on its one owner thread.
//
// The state word combines a transition generation with the visible state. A
// scanner therefore cannot confuse a reclaimed and republished slot with the
// older PUBLISHED value (the PUBLISHED -> WRITING -> PUBLISHED ABA case).
// Frame payload is touched only by the owner of WRITING or READING. Publication
// is the release transition to PUBLISHED; a successful consumer claim acquires
// that transition before exposing a const frame.
template <typename Frame, typename Traits, std::size_t SlotCount>
class RenderFrameExchange final {
    static_assert(
            SlotCount >= 2 && SlotCount <= 3,
            "Render frame exchange capacity must stay at two or three slots");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
            "Frame exchange requires lock-free 64-bit atomics");

public:
    static constexpr std::size_t kSlotCount = SlotCount;

    class WriteLease final {
    public:
        WriteLease() noexcept = default;
        WriteLease(const WriteLease&) = delete;
        WriteLease& operator=(const WriteLease&) = delete;

        WriteLease(WriteLease&& other) noexcept {
            take(std::move(other));
        }

        WriteLease& operator=(WriteLease&& other) noexcept {
            if (this != &other) {
                abandon();
                take(std::move(other));
            }
            return *this;
        }

        ~WriteLease() {
            abandon();
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return mOwner != nullptr;
        }

        [[nodiscard]] Frame& frame() noexcept {
            assert(mOwner != nullptr);
            return mOwner->mSlots[mSlotIndex].frame;
        }

        [[nodiscard]] std::size_t slotIndex() const noexcept {
            assert(mOwner != nullptr);
            return mSlotIndex;
        }

        // Returns false while the payload is incomplete. The lease remains in
        // WRITING so its producer may complete it or let RAII abandon it to FREE.
        [[nodiscard]] bool publish() noexcept {
            return mOwner != nullptr && mOwner->publish(*this);
        }

        void abandon() noexcept {
            if (mOwner != nullptr) {
                mOwner->abandon(*this);
            }
        }

    private:
        friend class RenderFrameExchange;

        WriteLease(RenderFrameExchange& owner, std::size_t slotIndex,
                std::uint64_t writingControl) noexcept
                : mOwner(&owner), mSlotIndex(slotIndex),
                  mWritingControl(writingControl) {}

        void take(WriteLease&& other) noexcept {
            mOwner = std::exchange(other.mOwner, nullptr);
            mSlotIndex = other.mSlotIndex;
            mWritingControl = other.mWritingControl;
        }

        void relinquish() noexcept {
            mOwner = nullptr;
        }

        RenderFrameExchange* mOwner = nullptr;
        std::size_t mSlotIndex = 0;
        std::uint64_t mWritingControl = 0;
    };

    class ReadLease final {
    public:
        ReadLease() noexcept = default;
        ReadLease(const ReadLease&) = delete;
        ReadLease& operator=(const ReadLease&) = delete;

        ReadLease(ReadLease&& other) noexcept {
            take(std::move(other));
        }

        ReadLease& operator=(ReadLease&& other) noexcept {
            if (this != &other) {
                release();
                take(std::move(other));
            }
            return *this;
        }

        ~ReadLease() {
            release();
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return mOwner != nullptr;
        }

        [[nodiscard]] const Frame& frame() const noexcept {
            assert(mOwner != nullptr);
            return mOwner->mSlots[mSlotIndex].frame;
        }

        [[nodiscard]] const RenderFrameSlotMetadata& metadata() const noexcept {
            assert(mOwner != nullptr);
            return mMetadata;
        }

        [[nodiscard]] std::size_t slotIndex() const noexcept {
            assert(mOwner != nullptr);
            return mSlotIndex;
        }

        void release() noexcept {
            if (mOwner != nullptr) {
                mOwner->release(*this);
            }
        }

    private:
        friend class RenderFrameExchange;

        ReadLease(RenderFrameExchange& owner, std::size_t slotIndex,
                std::uint64_t readingControl,
                RenderFrameSlotMetadata metadata) noexcept
                : mOwner(&owner), mSlotIndex(slotIndex),
                  mReadingControl(readingControl), mMetadata(metadata) {}

        void take(ReadLease&& other) noexcept {
            mOwner = std::exchange(other.mOwner, nullptr);
            mSlotIndex = other.mSlotIndex;
            mReadingControl = other.mReadingControl;
            mMetadata = other.mMetadata;
        }

        void relinquish() noexcept {
            mOwner = nullptr;
        }

        RenderFrameExchange* mOwner = nullptr;
        std::size_t mSlotIndex = 0;
        std::uint64_t mReadingControl = 0;
        RenderFrameSlotMetadata mMetadata{};
    };

    class Producer final {
    public:
        Producer(const Producer&) = delete;
        Producer& operator=(const Producer&) = delete;
        Producer(Producer&&) = delete;
        Producer& operator=(Producer&&) = delete;

        // Latest-wins producer policy:
        //   1. claim a FREE slot;
        //   2. otherwise replace the oldest unread PUBLISHED slot;
        //   3. otherwise drop the incoming frame immediately.
        // READING is never a writable candidate and this call never waits.
        [[nodiscard]] WriteLease tryBeginWrite() noexcept {
            return mOwner.beginWrite();
        }

    private:
        friend class RenderFrameExchange;

        explicit Producer(RenderFrameExchange& owner) noexcept : mOwner(owner) {}

        RenderFrameExchange& mOwner;
    };

    class Consumer final {
    public:
        Consumer(const Consumer&) = delete;
        Consumer& operator=(const Consumer&) = delete;
        Consumer(Consumer&&) = delete;
        Consumer& operator=(Consumer&&) = delete;

        // Selects the greatest (frameSequence, publicationTicket) for one world
        // epoch. Older unread compatible publications are released immediately;
        // incompatible epochs stay unavailable to this consumer and are never
        // mistaken for the requested world.
        [[nodiscard]] ReadLease
        tryAcquireNewest(std::uint64_t compatibleWorldEpoch) noexcept {
            return mOwner.acquireNewest(compatibleWorldEpoch);
        }

    private:
        friend class RenderFrameExchange;

        explicit Consumer(RenderFrameExchange& owner) noexcept : mOwner(owner) {}

        RenderFrameExchange& mOwner;
    };

    RenderFrameExchange() noexcept : mProducer(*this), mConsumer(*this) {}

    RenderFrameExchange(const RenderFrameExchange&) = delete;
    RenderFrameExchange& operator=(const RenderFrameExchange&) = delete;
    RenderFrameExchange(RenderFrameExchange&&) = delete;
    RenderFrameExchange& operator=(RenderFrameExchange&&) = delete;

    [[nodiscard]] Producer& producer() noexcept {
        return mProducer;
    }

    [[nodiscard]] Consumer& consumer() noexcept {
        return mConsumer;
    }

    [[nodiscard]] RenderFrameExchangeCounters counters() const noexcept {
        RenderFrameExchangeCounters result;
        result.publishedFrameCount =
                mPublishedFrameCount.load(std::memory_order_relaxed);
        result.acquiredFrameCount =
                mAcquiredFrameCount.load(std::memory_order_relaxed);
        result.producerReplacedPublishedFrameCount =
                mProducerReplacedPublishedFrameCount.load(std::memory_order_relaxed);
        result.consumerDiscardedPublishedFrameCount =
                mConsumerDiscardedPublishedFrameCount.load(std::memory_order_relaxed);
        result.incomingDroppedFrameCount =
                mIncomingDroppedFrameCount.load(std::memory_order_relaxed);
        result.abandonedWriteCount =
                mAbandonedWriteCount.load(std::memory_order_relaxed);
        result.rejectedPublicationCount =
                mRejectedPublicationCount.load(std::memory_order_relaxed);
        return result;
    }

    [[nodiscard]] std::size_t
    slotCountInState(RenderFrameSlotState state) const noexcept {
        std::size_t count = 0;
        for (const Slot& slot : mSlots) {
            if (stateOf(slot.control.load(std::memory_order_acquire)) == state) {
                ++count;
            }
        }
        return count;
    }

    // Payload vectors are intentionally non-atomic. This diagnostic snapshot is
    // therefore legal only while both endpoints are thread-confined to the
    // caller (the 11.2 same-thread path), or after future worker owners have
    // stopped. It is not a concurrent telemetry API.
    [[nodiscard]] RenderFrameExchangeStorageSnapshot<SlotCount>
    storageSnapshotWhenQuiescent() const noexcept {
        RenderFrameExchangeStorageSnapshot<SlotCount> result;
        for (std::size_t slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
            const Slot& slot = mSlots[slotIndex];
            RenderFrameExchangeSlotStorage& destination = result.slots[slotIndex];
            destination.state = stateOf(slot.control.load(std::memory_order_acquire));
            destination.metadata.publicationTicket =
                    slot.publicationTicket.load(std::memory_order_relaxed);
            destination.metadata.frameSequence =
                    slot.frameSequence.load(std::memory_order_relaxed);
            destination.metadata.worldEpoch =
                    slot.worldEpoch.load(std::memory_order_relaxed);
            destination.activeStorageBytes = Traits::activeStorageBytes(slot.frame);
            destination.retainedStorageBytes =
                    Traits::retainedStorageBytes(slot.frame);
            result.totalActiveStorageBytes = saturatingAdd(
                    result.totalActiveStorageBytes, destination.activeStorageBytes);
            result.totalRetainedStorageBytes = saturatingAdd(
                    result.totalRetainedStorageBytes, destination.retainedStorageBytes);
        }
        return result;
    }

    // Concurrent-safe slot state/metadata view. Payload storage is deliberately
    // reported as zero because vectors may be changing in a WRITING slot. A
    // higher-level owner can combine this with separately published storage
    // telemetry when live diagnostics need byte counts.
    [[nodiscard]] RenderFrameExchangeStorageSnapshot<SlotCount>
    stateSnapshot() const noexcept {
        RenderFrameExchangeStorageSnapshot<SlotCount> result;
        for (std::size_t slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
            const Slot& slot = mSlots[slotIndex];
            RenderFrameExchangeSlotStorage& destination = result.slots[slotIndex];
            destination.state = stateOf(slot.control.load(std::memory_order_acquire));
            destination.metadata.publicationTicket =
                    slot.publicationTicket.load(std::memory_order_relaxed);
            destination.metadata.frameSequence =
                    slot.frameSequence.load(std::memory_order_relaxed);
            destination.metadata.worldEpoch =
                    slot.worldEpoch.load(std::memory_order_relaxed);
        }
        return result;
    }

private:
    static constexpr std::uint64_t kStateMask = 0x3U;
    static constexpr unsigned kStateBits = 2;

    struct Slot final {
        Frame frame{};
        std::atomic<std::uint64_t> control{
                static_cast<std::uint64_t>(RenderFrameSlotState::Free)
        };
        std::atomic<std::uint64_t> publicationTicket{0};
        std::atomic<std::uint64_t> frameSequence{0};
        std::atomic<std::uint64_t> worldEpoch{0};
    };

    struct PublishedCandidate final {
        bool valid = false;
        std::size_t slotIndex = 0;
        std::uint64_t control = 0;
        RenderFrameSlotMetadata metadata{};
    };

    [[nodiscard]] static RenderFrameSlotState
    stateOf(std::uint64_t control) noexcept {
        return static_cast<RenderFrameSlotState>(control & kStateMask);
    }

    [[nodiscard]] static std::uint64_t
    transitionTo(std::uint64_t control, RenderFrameSlotState state) noexcept {
        const std::uint64_t nextGeneration = (control >> kStateBits) + 1U;
        return (nextGeneration << kStateBits) | static_cast<std::uint64_t>(state);
    }

    [[nodiscard]] static bool
    isNewer(const RenderFrameSlotMetadata& left,
            const RenderFrameSlotMetadata& right) noexcept {
        return left.frameSequence > right.frameSequence || (left.frameSequence == right.frameSequence && left.publicationTicket > right.publicationTicket);
    }

    [[nodiscard]] static bool
    isOlder(const RenderFrameSlotMetadata& left,
            const RenderFrameSlotMetadata& right) noexcept {
        return left.frameSequence < right.frameSequence || (left.frameSequence == right.frameSequence && left.publicationTicket < right.publicationTicket);
    }

    [[nodiscard]] static std::uint64_t
    saturatingAdd(std::uint64_t left, std::uint64_t right) noexcept {
        const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        return right > maximum - left ? maximum : left + right;
    }

    [[nodiscard]] bool
    readPublishedCandidate(std::size_t slotIndex,
            PublishedCandidate& candidate) const noexcept {
        const Slot& slot = mSlots[slotIndex];
        const std::uint64_t before = slot.control.load(std::memory_order_acquire);
        if (stateOf(before) != RenderFrameSlotState::Published) {
            return false;
        }

        RenderFrameSlotMetadata metadata;
        metadata.publicationTicket =
                slot.publicationTicket.load(std::memory_order_relaxed);
        metadata.frameSequence = slot.frameSequence.load(std::memory_order_relaxed);
        metadata.worldEpoch = slot.worldEpoch.load(std::memory_order_relaxed);

        // Reject metadata observed across a concurrent slot transition. The
        // generation makes a later PUBLISHED value distinct from the first one.
        if (slot.control.load(std::memory_order_acquire) != before) {
            return false;
        }

        candidate.valid = true;
        candidate.slotIndex = slotIndex;
        candidate.control = before;
        candidate.metadata = metadata;
        return true;
    }

    [[nodiscard]] WriteLease beginWrite() noexcept {
        for (std::size_t offset = 0; offset < SlotCount; ++offset) {
            const std::size_t slotIndex = (mNextFreeProbe + offset) % SlotCount;
            Slot& slot = mSlots[slotIndex];
            std::uint64_t observed = slot.control.load(std::memory_order_relaxed);
            if (stateOf(observed) != RenderFrameSlotState::Free) {
                continue;
            }

            const std::uint64_t writing =
                    transitionTo(observed, RenderFrameSlotState::Writing);
            if (slot.control.compare_exchange_strong(observed, writing,
                        std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                Traits::prepareForWrite(slot.frame);
                mNextFreeProbe = (slotIndex + 1U) % SlotCount;
                return WriteLease(*this, slotIndex, writing);
            }
        }

        std::array<bool, SlotCount> attempted{};
        for (std::size_t attempt = 0; attempt < SlotCount; ++attempt) {
            PublishedCandidate oldest;
            for (std::size_t slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
                if (attempted[slotIndex]) {
                    continue;
                }
                PublishedCandidate candidate;
                if (!readPublishedCandidate(slotIndex, candidate)) {
                    continue;
                }
                if (!oldest.valid || candidate.metadata.publicationTicket < oldest.metadata.publicationTicket) {
                    oldest = candidate;
                }
            }

            if (!oldest.valid) {
                break;
            }
            attempted[oldest.slotIndex] = true;

            Slot& slot = mSlots[oldest.slotIndex];
            std::uint64_t observed = oldest.control;
            const std::uint64_t writing =
                    transitionTo(observed, RenderFrameSlotState::Writing);
            if (slot.control.compare_exchange_strong(observed, writing,
                        std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                Traits::prepareForWrite(slot.frame);
                mProducerReplacedPublishedFrameCount.fetch_add(
                        1, std::memory_order_relaxed);
                mNextFreeProbe = (oldest.slotIndex + 1U) % SlotCount;
                return WriteLease(*this, oldest.slotIndex, writing);
            }
        }

        mIncomingDroppedFrameCount.fetch_add(1, std::memory_order_relaxed);
        return {};
    }

    [[nodiscard]] bool publish(WriteLease& lease) noexcept {
        Slot& slot = mSlots[lease.mSlotIndex];
        if (!Traits::isPublishable(slot.frame)) {
            mRejectedPublicationCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        RenderFrameSlotMetadata metadata;
        metadata.publicationTicket = ++mNextPublicationTicket;
        metadata.frameSequence = Traits::frameSequence(slot.frame);
        metadata.worldEpoch = Traits::worldEpoch(slot.frame);
        slot.publicationTicket.store(metadata.publicationTicket,
                std::memory_order_relaxed);
        slot.frameSequence.store(metadata.frameSequence, std::memory_order_relaxed);
        slot.worldEpoch.store(metadata.worldEpoch, std::memory_order_relaxed);

        const std::uint64_t published =
                transitionTo(lease.mWritingControl, RenderFrameSlotState::Published);
        slot.control.store(published, std::memory_order_release);
        mPublishedFrameCount.fetch_add(1, std::memory_order_relaxed);
        lease.relinquish();
        return true;
    }

    void abandon(WriteLease& lease) noexcept {
        Slot& slot = mSlots[lease.mSlotIndex];
        Traits::prepareForWrite(slot.frame);
        slot.control.store(
                transitionTo(lease.mWritingControl, RenderFrameSlotState::Free),
                std::memory_order_release);
        mAbandonedWriteCount.fetch_add(1, std::memory_order_relaxed);
        lease.relinquish();
    }

    [[nodiscard]] ReadLease
    acquireNewest(std::uint64_t compatibleWorldEpoch) noexcept {
        std::array<bool, SlotCount> attempted{};
        for (std::size_t attempt = 0; attempt < SlotCount; ++attempt) {
            PublishedCandidate newest;
            for (std::size_t slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
                if (attempted[slotIndex]) {
                    continue;
                }
                PublishedCandidate candidate;
                if (!readPublishedCandidate(slotIndex, candidate) || candidate.metadata.worldEpoch != compatibleWorldEpoch) {
                    continue;
                }
                if (!newest.valid || isNewer(candidate.metadata, newest.metadata)) {
                    newest = candidate;
                }
            }

            if (!newest.valid) {
                return {};
            }
            attempted[newest.slotIndex] = true;

            Slot& slot = mSlots[newest.slotIndex];
            std::uint64_t observed = newest.control;
            const std::uint64_t reading =
                    transitionTo(observed, RenderFrameSlotState::Reading);
            if (!slot.control.compare_exchange_strong(observed, reading,
                        std::memory_order_acquire,
                        std::memory_order_relaxed)) {
                continue;
            }

            mAcquiredFrameCount.fetch_add(1, std::memory_order_relaxed);
            discardOlderPublished(newest.metadata, newest.slotIndex);
            return ReadLease(*this, newest.slotIndex, reading, newest.metadata);
        }
        return {};
    }

    void discardOlderPublished(const RenderFrameSlotMetadata& selected,
            std::size_t selectedSlotIndex) noexcept {
        for (std::size_t slotIndex = 0; slotIndex < SlotCount; ++slotIndex) {
            if (slotIndex == selectedSlotIndex) {
                continue;
            }

            PublishedCandidate candidate;
            if (!readPublishedCandidate(slotIndex, candidate) || candidate.metadata.worldEpoch != selected.worldEpoch || !isOlder(candidate.metadata, selected)) {
                continue;
            }

            Slot& slot = mSlots[slotIndex];
            std::uint64_t observed = candidate.control;
            const std::uint64_t free =
                    transitionTo(observed, RenderFrameSlotState::Free);
            if (slot.control.compare_exchange_strong(observed, free,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                mConsumerDiscardedPublishedFrameCount.fetch_add(
                        1, std::memory_order_relaxed);
            }
        }
    }

    void release(ReadLease& lease) noexcept {
        Slot& slot = mSlots[lease.mSlotIndex];
        slot.control.store(
                transitionTo(lease.mReadingControl, RenderFrameSlotState::Free),
                std::memory_order_release);
        lease.relinquish();
    }

    std::array<Slot, SlotCount> mSlots{};
    // Only Producer touches the ticket source. Published per-slot copies are
    // atomic because Consumer scans those concurrently.
    std::uint64_t mNextPublicationTicket = 0;
    std::atomic<std::uint64_t> mPublishedFrameCount{0};
    std::atomic<std::uint64_t> mAcquiredFrameCount{0};
    std::atomic<std::uint64_t> mProducerReplacedPublishedFrameCount{0};
    std::atomic<std::uint64_t> mConsumerDiscardedPublishedFrameCount{0};
    std::atomic<std::uint64_t> mIncomingDroppedFrameCount{0};
    std::atomic<std::uint64_t> mAbandonedWriteCount{0};
    std::atomic<std::uint64_t> mRejectedPublicationCount{0};
    std::size_t mNextFreeProbe = 0;
    Producer mProducer;
    Consumer mConsumer;
};

struct RenderSourceFrameExchangeTraits final {
    static void prepareForWrite(RenderSourceFrame& frame) noexcept {
        frame.valid = false;
    }

    [[nodiscard]] static bool
    isPublishable(const RenderSourceFrame& frame) noexcept {
        return frame.valid && frame.layoutVersion == kRenderSourceFrameLayoutVersion;
    }

    [[nodiscard]] static std::uint64_t
    frameSequence(const RenderSourceFrame& frame) noexcept {
        return frame.captureSequence;
    }

    [[nodiscard]] static std::uint64_t
    worldEpoch(const RenderSourceFrame& frame) noexcept {
        return frame.capture.worldEpoch;
    }

    [[nodiscard]] static std::uint64_t
    activeStorageBytes(const RenderSourceFrame& frame) noexcept {
        return frame.activeStorageBytes();
    }

    [[nodiscard]] static std::uint64_t
    retainedStorageBytes(const RenderSourceFrame& frame) noexcept {
        return frame.retainedStorageBytes();
    }
};

struct PreparedRenderFrameExchangeTraits final {
    static void prepareForWrite(PreparedRenderFrame& frame) noexcept {
        frame.valid = false;
        frame.geometryReady = false;
    }

    [[nodiscard]] static bool
    isPublishable(const PreparedRenderFrame& frame) noexcept {
        return frame.valid && frame.geometryReady && frame.layout.version == kPreparedRenderFrameLayoutVersion;
    }

    [[nodiscard]] static std::uint64_t
    frameSequence(const PreparedRenderFrame& frame) noexcept {
        return frame.identity.captureSequence;
    }

    [[nodiscard]] static std::uint64_t
    worldEpoch(const PreparedRenderFrame& frame) noexcept {
        return frame.identity.worldEpoch;
    }

    [[nodiscard]] static std::uint64_t
    activeStorageBytes(const PreparedRenderFrame& frame) noexcept {
        return frame.activeStorageBytes();
    }

    [[nodiscard]] static std::uint64_t
    retainedStorageBytes(const PreparedRenderFrame& frame) noexcept {
        return frame.retainedStorageBytes();
    }
};

inline constexpr std::size_t kRenderFrameExchangeSlotCount = 3;

using RenderSourceFrameExchange =
        RenderFrameExchange<RenderSourceFrame, RenderSourceFrameExchangeTraits,
                kRenderFrameExchangeSlotCount>;
using PreparedRenderFrameExchange =
        RenderFrameExchange<PreparedRenderFrame, PreparedRenderFrameExchangeTraits,
                kRenderFrameExchangeSlotCount>;

// Snapshot shape consumed by future runtime profiling. Keeping the two values
// named separately prevents source loss and prepared loss from being merged.
struct RenderFrameExchangeDropProfile final {
    std::uint64_t droppedSourceFrameCount = 0;
    std::uint64_t droppedPreparedFrameCount = 0;
};

[[nodiscard]] inline RenderFrameExchangeDropProfile
captureRenderFrameExchangeDropProfile(
        const RenderSourceFrameExchange& sourceExchange,
        const PreparedRenderFrameExchange& preparedExchange) noexcept {
    return {sourceExchange.counters().droppedFrameCount(),
            preparedExchange.counters().droppedFrameCount()};
}

extern template class RenderFrameExchange<RenderSourceFrame,
        RenderSourceFrameExchangeTraits,
        kRenderFrameExchangeSlotCount>;
extern template class RenderFrameExchange<PreparedRenderFrame,
        PreparedRenderFrameExchangeTraits,
        kRenderFrameExchangeSlotCount>;

} // namespace cellularity::render
