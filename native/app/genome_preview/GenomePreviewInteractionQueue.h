#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

#include "app/genome_preview/RetainedPayloadBudget.h"

namespace cellularity::app::genome_preview {

struct GenomePreviewContentSnapshot;
struct GenomePreviewViewSnapshot;

// V-19 recorded at most 196 managed render tasks in any characterized leg.
// The profiler did not expose simultaneous queue depth, so V-23 deliberately
// uses that whole-leg count as a conservative high-water upper bound instead
// of inventing a smaller measurement.
inline constexpr std::size_t
        kV19GenomePreviewInteractionHighWaterUpperBound = 196;
inline constexpr std::size_t kGenomePreviewInteractionCapacity =
        boundedCapacityFromObservedHighWater(
                kV19GenomePreviewInteractionHighWaterUpperBound,
                128,
                1024);
static_assert(kGenomePreviewInteractionCapacity == 512);

struct GenomePreviewStateFence final {
    std::shared_ptr<const GenomePreviewContentSnapshot> content;
    std::shared_ptr<const GenomePreviewViewSnapshot> view;
};

struct GenomePreviewRestoreViewport final {
    float cameraX = 0.0F;
    float cameraY = 0.0F;
    float zoom = 1.0F;
};

struct GenomePreviewMoveViewport final {
    float deltaX = 0.0F;
    float deltaY = 0.0F;
};

struct GenomePreviewZoomViewport final {
    float scale = 1.0F;
    float centerX = 0.0F;
    float centerY = 0.0F;
};

struct GenomePreviewSelectGene final {
    float screenX = 0.0F;
    float screenY = 0.0F;
};

struct GenomePreviewCaptureGhost final {
};

struct GenomePreviewClearGhost final {
};

using GenomePreviewInteractionValue = std::variant<
        GenomePreviewRestoreViewport,
        GenomePreviewMoveViewport,
        GenomePreviewZoomViewport,
        GenomePreviewSelectGene,
        GenomePreviewCaptureGhost,
        GenomePreviewClearGhost>;

struct GenomePreviewInteractionInput final {
    std::uint64_t completionToken = 0;
    std::uint64_t controllerGeneration = 0;
    // Zero denotes a controller-scoped operation that may wait for the first
    // surface. Non-zero operations belong to one concrete attachment.
    std::uint64_t attachmentGeneration = 0;
    GenomePreviewInteractionValue value{};
};

struct GenomePreviewInteractionCommand final {
    std::uint64_t sequence = 0;
    GenomePreviewInteractionInput input{};
    GenomePreviewStateFence fence{};
};

enum class GenomePreviewInteractionEnqueueStatus : std::uint8_t {
    Accepted = 0,
    QueueFull = 1,
    Closing = 2,
    InvalidArgument = 3,
};

struct GenomePreviewInteractionEnqueueResult final {
    GenomePreviewInteractionEnqueueStatus status =
            GenomePreviewInteractionEnqueueStatus::InvalidArgument;
    std::uint64_t sequence = 0;

    [[nodiscard]] bool accepted() const noexcept {
        return status == GenomePreviewInteractionEnqueueStatus::Accepted;
    }
};

struct GenomePreviewInteractionQueueSnapshot final {
    std::size_t capacity = 0;
    std::size_t depth = 0;
    std::size_t highWater = 0;
    std::uint64_t accepted = 0;
    std::uint64_t consumed = 0;
    std::uint64_t rejectedFull = 0;
    std::uint64_t rejectedInvalid = 0;
    std::uint64_t staleGenerationDiscarded = 0;
    std::uint64_t cancelledOnClose = 0;
    bool closing = false;
};

/** Latest-only wakeup latch; payload/state storage remains in its own domain. */
class GenomePreviewFrameRequestFlag final {
public:
    GenomePreviewFrameRequestFlag() = default;

    GenomePreviewFrameRequestFlag(const GenomePreviewFrameRequestFlag&) = delete;
    GenomePreviewFrameRequestFlag& operator=(const GenomePreviewFrameRequestFlag&) = delete;

    /** Returns true only for the idle -> requested transition. */
    [[nodiscard]] bool request() noexcept;
    /** Returns true once for one or more adjacent requests. */
    [[nodiscard]] bool consumePendingOnRenderOwner() noexcept;
    void close() noexcept;
    [[nodiscard]] bool pending() const noexcept;

private:
    enum class State : std::uint8_t {
        Idle = 0,
        Requested = 1,
        Closing = 2,
    };

    std::atomic<State> mState{State::Idle};
};

/** Bounded FIFO for viewport, selection and ghost interactions. */
class GenomePreviewInteractionQueue final {
public:
    explicit GenomePreviewInteractionQueue(
            std::size_t capacity = kGenomePreviewInteractionCapacity);
    ~GenomePreviewInteractionQueue();

    GenomePreviewInteractionQueue(const GenomePreviewInteractionQueue&) = delete;
    GenomePreviewInteractionQueue& operator=(const GenomePreviewInteractionQueue&) = delete;
    GenomePreviewInteractionQueue(GenomePreviewInteractionQueue&&) = delete;
    GenomePreviewInteractionQueue& operator=(GenomePreviewInteractionQueue&&) = delete;

    [[nodiscard]] GenomePreviewInteractionEnqueueResult enqueue(
            const GenomePreviewInteractionInput& input,
            GenomePreviewStateFence fence);

    [[nodiscard]] std::optional<GenomePreviewInteractionCommand>
    consumePendingOnRenderOwner(
            std::uint64_t activeControllerGeneration,
            std::uint64_t activeAttachmentGeneration);

    void close() noexcept;
    [[nodiscard]] GenomePreviewInteractionQueueSnapshot snapshot() const noexcept;

private:
    [[nodiscard]] static bool isValid(
            const GenomePreviewInteractionInput& input) noexcept;
    [[nodiscard]] GenomePreviewInteractionCommand popFrontLocked() noexcept;

    mutable std::mutex mMutex;
    std::array<GenomePreviewInteractionCommand,
            kGenomePreviewInteractionCapacity>
            mSlots{};
    const std::size_t mCapacity;
    std::size_t mHead = 0;
    std::size_t mDepth = 0;
    std::size_t mHighWater = 0;
    std::uint64_t mNextSequence = 1;
    std::uint64_t mAccepted = 0;
    std::uint64_t mConsumed = 0;
    std::uint64_t mRejectedFull = 0;
    std::uint64_t mRejectedInvalid = 0;
    std::uint64_t mStaleGenerationDiscarded = 0;
    std::uint64_t mCancelledOnClose = 0;
    bool mClosing = false;
};

} // namespace cellularity::app::genome_preview
