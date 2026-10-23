#pragma once

#include <cstddef>
#include <cstdint>

namespace cellularity::app::render_worker {

inline constexpr std::uint32_t kNativeVulkanWorkerStatusMagic = 0x31535756U;
inline constexpr std::uint16_t kNativeVulkanWorkerStatusVersion = 1;
inline constexpr std::size_t kNativeVulkanWorkerStatusSize = 96;

enum class NativeVulkanWorkerPhase : std::uint32_t {
    Dormant = 0,
    Starting = 1,
    WaitingForSurface = 2,
    Paused = 3,
    Ready = 4,
    SurfaceLost = 5,
    RecoverableFailure = 6,
    TerminalFailure = 7,
    StopRequested = 8,
    Stopped = 9,
};

enum class NativeVulkanWorkerFailure : std::int32_t {
    None = 0,
    ApiUnavailable = 1,
    AbiMismatch = 2,
    AssetUnavailable = 3,
    ThreadStartFailed = 4,
    SessionCreateFailed = 5,
    SurfaceAttachFailed = 6,
    SurfaceResizeFailed = 7,
    SourceSurfaceFailed = 8,
    FrameFailed = 9,
    SourceFrameFailed = 10,
    BackendControlFailed = 11,
    DiagnosticsFailed = 12,
    InvariantViolation = 13,
    WorkerOutOfMemory = 14,
    WorkerUnhandledException = 15,
    StatusSignalTransportFailed = 16,
};

enum NativeVulkanWorkerStatusFlag : std::uint32_t {
    kWorkerDesiredResumed = 1U << 0,
    kWorkerDesiredSurfacePresent = 1U << 1,
    kWorkerPhysicalSurfaceAttached = 1U << 2,
    kWorkerSourceSurfaceApplied = 1U << 3,
    kWorkerFrameRequestPending = 1U << 4,
    kWorkerPacedRetryPending = 1U << 5,
    kWorkerHasFrameOutcome = 1U << 6,
    kWorkerFailureDiagnosticsUnavailable = 1U << 7,
    kWorkerStatusSignalDeliveryFailed = 1U << 8,
};

struct NativeVulkanWorkerStatus final {
    std::uint64_t revision = 1;
    NativeVulkanWorkerPhase phase = NativeVulkanWorkerPhase::Dormant;
    std::uint32_t lastFrameOutcome = 0;
    std::uint32_t flags = 0;
    NativeVulkanWorkerFailure failure = NativeVulkanWorkerFailure::None;
    std::uint64_t presentedFrames = 0;
    std::uint64_t swapchainRecreations = 0;
    std::uint64_t failedFrameAttempts = 0;
    std::uint64_t desiredSurfaceGeneration = 0;
    std::uint64_t appliedSurfaceGeneration = 0;
    std::uint64_t diagnosticsRevision = 0;
    std::uint32_t desiredWidth = 0;
    std::uint32_t desiredHeight = 0;
};

enum class CompactStatusReadResult : std::uint8_t {
    Ready = 0,
    InvalidBuffer = 1,
};

void encodeNativeVulkanWorkerStatus(
        const NativeVulkanWorkerStatus& status,
        std::uint8_t* destination) noexcept;

} // namespace cellularity::app::render_worker
