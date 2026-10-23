#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cellularity::microscope {

inline constexpr std::uint32_t kMicroscopeInputBatchMagic = 0x31424943U;
inline constexpr std::uint32_t kMicroscopeInputResultMagic = 0x31524943U;
inline constexpr std::uint16_t kMicroscopeInputBatchVersion = 1;
inline constexpr std::size_t kMicroscopeInputHeaderSize = 48;
inline constexpr std::size_t kMicroscopeInputCommandRecordSize = 32;
inline constexpr std::size_t kMicroscopeInputCommandCapacity = 64;
inline constexpr std::size_t kMicroscopeInputBufferCapacity =
        kMicroscopeInputHeaderSize
        + kMicroscopeInputCommandCapacity * kMicroscopeInputCommandRecordSize;
inline constexpr std::size_t kMicroscopeInputResultHeaderSize = 64;
inline constexpr std::size_t kMicroscopeInputResultStatusSize = 4;
inline constexpr std::size_t kMicroscopeInputResultBufferCapacity =
        kMicroscopeInputResultHeaderSize
        + kMicroscopeInputCommandCapacity * kMicroscopeInputResultStatusSize;

inline constexpr std::uint32_t kMicroscopeInputAggregateCameraChanged = 1U;
inline constexpr std::uint32_t
        kMicroscopeInputAggregateZoomDisplaySizeChanged = 1U << 1U;
inline constexpr std::uint32_t kMicroscopeInputAggregateZoomEnded = 1U << 2U;

enum class MicroscopeInputCommandKind : std::uint16_t {
    Down = 1,
    Move = 2,
    Up = 3,
    Pan = 4,
    Zoom = 5,
    Fling = 6,
    Tap = 7,
    ZoomEnded = 8,
};

enum class MicroscopeInputCommandWireStatus : std::uint32_t {
    Unused = 0,
    WorldAccepted = 1,
    WorldAcceptedCoalescedPrevious = 2,
    WorldRejectedOrderedCapacity = 3,
    WorldRejectedPathCapacity = 4,
    WorldRejectedPointerState = 5,
    WorldRejectedPointerCapacity = 6,
    WorldRejectedInvalidArgument = 7,
    WorldRejectedSequenceExhausted = 8,
    PresentationApplied = 9,
    PresentationRejectedInteractionGate = 10,
    UiApplied = 11,
    WorldRejectedStalePlacementGenome = 12,
    NotExecutedInternalFailure = 13,
    IndeterminateInternalFailure = 14,
};

enum class MicroscopeInputIngressStatus : std::uint32_t {
    Applied = 0,
    Malformed = 1,
    StaleInputEpoch = 2,
    AttachmentUnavailable = 3,
    Resyncing = 4,
    InactiveSurface = 5,
    Closing = 6,
    InternalFailure = 7,
};

struct MicroscopeInputCommandRecord final {
    MicroscopeInputCommandKind kind = MicroscopeInputCommandKind::Down;
    int pointerId = 0;
    std::array<float, 4> values{};
};

struct MicroscopeInputBatch final {
    std::uint64_t sequence = 0;
    std::int64_t eventTimeNanos = 0;
    std::uint64_t expectedInputEpoch = 0;
    std::int64_t profilingIngressTimestampNanos = 0;
    std::size_t commandCount = 0;
    std::size_t tapCount = 0;
    std::array<MicroscopeInputCommandRecord,
            kMicroscopeInputCommandCapacity>
            commands{};
};

struct MicroscopeInputBatchResult final {
    std::uint64_t batchSequence = 0;
    std::uint64_t inputEpoch = 0;
    std::uint64_t viewportRevision = 0;
    std::uint64_t presentationMotionRevision = 0;
    std::uint32_t zoomDisplaySizeBits = 0;
    std::uint32_t aggregateFlags = 0;
    MicroscopeInputIngressStatus ingressStatus =
            MicroscopeInputIngressStatus::Malformed;
    std::size_t commandCount = 0;
    std::array<MicroscopeInputCommandWireStatus,
            kMicroscopeInputCommandCapacity>
            commandStatuses{};
};

// Establishes the fail-closed status image before each command is invoked.
// The callable is intentionally a template so the production switch remains
// inline and the batch hot path gains no virtual or indirect dispatch.
template <typename Dispatch>
void dispatchValidatedMicroscopeInputBatch(
        const MicroscopeInputBatch& batch,
        MicroscopeInputBatchResult& result,
        Dispatch&& dispatch) {
    result.ingressStatus = MicroscopeInputIngressStatus::Applied;
    result.commandCount = batch.commandCount;
    result.commandStatuses.fill(MicroscopeInputCommandWireStatus::Unused);
    for (std::size_t index = 0; index < batch.commandCount; ++index) {
        result.commandStatuses[index] =
                MicroscopeInputCommandWireStatus::NotExecutedInternalFailure;
    }
    for (std::size_t index = 0; index < batch.commandCount; ++index) {
        result.commandStatuses[index] =
                MicroscopeInputCommandWireStatus::IndeterminateInternalFailure;
        result.commandStatuses[index] = dispatch(index, batch.commands[index]);
    }
}

[[nodiscard]] bool decodeMicroscopeInputBatch(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        MicroscopeInputBatch& batch) noexcept;

[[nodiscard]] std::uint64_t readMicroscopeInputBatchSequence(
        const std::uint8_t* bytes,
        std::size_t byteCount) noexcept;

void initializeMicroscopeInputBatchResult(
        MicroscopeInputBatchResult& result,
        std::uint64_t batchSequence,
        std::uint64_t inputEpoch,
        MicroscopeInputIngressStatus status) noexcept;

void markMicroscopeInputBatchInternalFailure(
        MicroscopeInputBatchResult& result,
        bool dispatchStarted) noexcept;

void writeMicroscopeInputBatchResult(
        std::uint8_t* destination,
        const MicroscopeInputBatchResult& result) noexcept;

inline constexpr std::uint32_t kPresentationMotionSnapshotMagic = 0x31534D43U;
inline constexpr std::uint16_t kPresentationMotionSnapshotVersion = 1;
inline constexpr std::size_t kPresentationMotionSnapshotSize = 32;
inline constexpr std::uint32_t kPresentationMotionInertiaActive = 1U;
inline constexpr std::uint32_t kPresentationMotionCameraActive = 1U << 1U;

struct PresentationMotionSnapshot final {
    std::uint64_t revision = 0;
    std::uint64_t cameraRevision = 0;
    bool inertiaActive = false;
    bool cameraMotionActive = false;
};

void writePresentationMotionSnapshot(
        std::uint8_t* destination,
        const PresentationMotionSnapshot& snapshot) noexcept;

} // namespace cellularity::microscope
