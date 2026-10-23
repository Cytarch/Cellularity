#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderFrameExchange.h"

class SimulationWorld;

namespace cellularity::tests {
struct MicroscopeRenderFramePipelineTestAccess;
} // namespace cellularity::tests

namespace cellularity::microscope {

enum class RenderPreparationFailureKind : std::uint8_t {
    None = 0,
    Allocation = 1,
    StandardException = 2,
    UnknownException = 3,
};

enum class RenderPreparationFailureStage : std::uint8_t {
    None = 0,
    WorkerInitialization = 1,
    PreparedSlotAcquisition = 2,
    FrameBuild = 3,
    FramePublication = 4,
};

struct RenderPreparationFailure final {
    RenderPreparationFailureKind kind = RenderPreparationFailureKind::None;
    RenderPreparationFailureStage stage = RenderPreparationFailureStage::None;
    std::uint64_t worldEpoch = 0;
    std::uint64_t sourceSequence = 0;

    [[nodiscard]] explicit operator bool() const noexcept {
        return kind != RenderPreparationFailureKind::None;
    }
};

// A missing compatible identity never falls through to partially prepared GPU
// storage. A coverage miss keeps the last complete same-identity frame while a
// latest-only urgent capture fills the newly exposed region.
enum class RenderUncoveredRegionPolicy : std::uint8_t {
    RetainLastCompleteFrame = 0,
};

enum class RenderPresentationOutcome : std::uint8_t {
    WaitingForFirstCompleteFrame = 0,
    PresentedNewCompleteFrame = 1,
    PresentedRetainedCompleteFrame = 2,
    IdentityBarrierClearOnly = 3,
    CoverageMissRetainedCompleteFrame = 4,
    FailureRetainedCompleteFrame = 5,
    FailureClearOnly = 6,
};

// All fields are sampled through atomics or from the GL-consumer-owned read
// lease. The snapshot is therefore safe while the preparation worker runs;
// individual slot fields are diagnostic observations rather than one global
// transaction.
struct MicroscopeRenderFramePipelineSnapshot final {
    render::RenderFrameExchangeStorageSnapshot<
            render::kRenderFrameExchangeSlotCount>
            sourceStorage{};
    render::RenderFrameExchangeStorageSnapshot<
            render::kRenderFrameExchangeSlotCount>
            preparedStorage{};
    render::RenderFrameExchangeCounters sourceCounters{};
    render::RenderFrameExchangeCounters preparedCounters{};
    render::RenderFrameExchangeDropProfile droppedFrames{};
    bool hasPendingPreparedFrame = false;
    bool hasCurrentPreparedFrame = false;
    bool hasPresentation = false;
    bool workerRunning = false;
    bool workerIdle = false;
    bool workerBuilding = false;
    std::uint64_t pendingPreparedSequence = 0;
    std::uint64_t currentPreparedSequence = 0;
    std::uint64_t currentWorldEpoch = 0;
    std::uint64_t currentModeGeneration = 0;
    std::uint64_t currentPreparedModeGeneration = 0;
    std::uint64_t latestAcceptedSourceSequence = 0;
    std::uint64_t latestPublishedSourceSequence = 0;
    std::uint64_t supersededBuildCount = 0;
    std::uint64_t rejectedInputRollbackCount = 0;
    std::uint64_t rejectedModeGenerationCount = 0;
    std::uint64_t coverageMissCount = 0;
    std::uint64_t urgentCaptureRequestCount = 0;
    std::uint64_t urgentCaptureCoalescedCount = 0;
    std::uint64_t urgentCaptureConsumedCount = 0;
    std::uint64_t urgentCaptureViewportRevision = 0;
    bool urgentCapturePending = false;
    RenderUncoveredRegionPolicy uncoveredRegionPolicy =
            RenderUncoveredRegionPolicy::RetainLastCompleteFrame;
    RenderPresentationOutcome presentationOutcome =
            RenderPresentationOutcome::WaitingForFirstCompleteFrame;
    RenderPreparationFailure failure{};
};

// Concurrent-safe counters used by the owner telemetry publisher. Unlike the
// full diagnostic snapshot, this value never reads GL-consumer-owned leases or
// mutable frame payload storage.
struct MicroscopeRenderFrameExchangeTelemetry final {
    render::RenderFrameExchangeCounters source{};
    render::RenderFrameExchangeCounters prepared{};
};

// Microscope-only CPU frame owner. Production capture() reads SimulationWorld only
// while its caller holds a simulation-owner lease. A dedicated worker consumes
// immutable source slots and owns the source-to-prepared builder. The GL caller
// owns the prepared consumer and latest presentation state. No worker operation
// owns GL/JNI state or retains a SimulationWorld alias.
class MicroscopeRenderFramePipeline final {
public:
    using ViewBounds = render::RenderViewBounds;
    using CaptureMetadata = render::RenderFrameCaptureMetadata;
    using MarkerCaptureRequest = render::RenderMarkerCaptureRequest;
    using PresentationState = render::RenderPresentationState;
    using PreparedFrame = render::PreparedRenderFrame;

    explicit MicroscopeRenderFramePipeline(int maxCellCount);
    ~MicroscopeRenderFramePipeline();

    MicroscopeRenderFramePipeline(const MicroscopeRenderFramePipeline&) = delete;
    MicroscopeRenderFramePipeline&
    operator=(const MicroscopeRenderFramePipeline&) = delete;
    MicroscopeRenderFramePipeline(MicroscopeRenderFramePipeline&&) = delete;
    MicroscopeRenderFramePipeline&
    operator=(MicroscopeRenderFramePipeline&&) = delete;

    void reset(int maxCellCount);
    void invalidateWorldEpoch(std::uint64_t worldEpoch);
    void invalidateModeGeneration(std::uint64_t modeGeneration);

    // Production microscope path: captures and publishes an owned source frame,
    // wakes the worker, and returns without waiting for geometry preparation.
    int capture(const SimulationWorld& world, const ViewBounds& viewBounds,
            const CaptureMetadata& captureMetadata,
            const MarkerCaptureRequest* markerRequests = nullptr,
            std::size_t markerRequestCount = 0);

    // Deterministic test and benchmark helper. It uses the same worker as the
    // production path, but waits until this source or a newer replacement has
    // produced a complete frame.
    int captureAndPrepare(const SimulationWorld& world, const ViewBounds& viewBounds,
            const CaptureMetadata& captureMetadata,
            const MarkerCaptureRequest* markerRequests = nullptr,
            std::size_t markerRequestCount = 0);

    // GL-consumer operations never wait for the worker. They either replace the
    // retained read lease with the newest compatible complete publication or
    // return the last retained frame. nullptr means that no complete frame for
    // the current epoch exists yet.
    [[nodiscard]] const PreparedFrame* acquireLatestPreparedFrame();
    [[nodiscard]] const PreparedFrame*
    presentLatestPreparedFrame(const PresentationState& presentation);
    [[nodiscard]] const PreparedFrame* preparedFrame() const noexcept;
    [[nodiscard]] const PresentationState& latestPresentation() const;
    [[nodiscard]] bool hasLatestPresentation() const noexcept;
    [[nodiscard]] RenderPreparationFailure failure() const noexcept;
    [[nodiscard]] MicroscopeRenderFrameExchangeTelemetry
    exchangeTelemetry() const noexcept;
    // Consumed by the simulation owner at the next safe frame boundary. Multiple
    // GL misses collapse into one request; the capture always samples the current
    // viewport instead of replaying the viewport that first noticed the miss.
    [[nodiscard]] bool consumeUrgentCaptureRequest() noexcept;

    [[nodiscard]] int visibleCellCount() const noexcept;

    [[nodiscard]] MicroscopeRenderFramePipelineSnapshot
    snapshotWhenQuiescent() const noexcept;

private:
    friend struct cellularity::tests::MicroscopeRenderFramePipelineTestAccess;

    bool setPreparationPausedForTesting(bool paused,
            std::chrono::milliseconds timeout);
    void setPauseAfterBuildForTesting(bool paused);
    bool
    waitUntilPausedAfterBuildForTesting(std::chrono::milliseconds timeout) const;
    bool waitUntilSourcePreparedForTesting(std::uint64_t sourceSequence,
            std::chrono::milliseconds timeout);
    void failNextPreparationForTesting(RenderPreparationFailureKind kind);

    struct State;
    std::unique_ptr<State> mState;
};

} // namespace cellularity::microscope
