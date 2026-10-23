#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "app/render/NativeRenderAttachmentLease.h"
#include "app/render/NativeRenderFrameSource.h"
#include "app/render/NativeVulkanDiagnosticsLayout.h"
#include "app/render/NativeVulkanSurfaceLease.h"
#include "app/render/NativeVulkanWorkerStatus.h"
#include "platform/CurrentThreadScheduling.h"

namespace cellularity::render::abi {
class VulkanBackendSession;
class VulkanBackendSessionFactory;
} // namespace cellularity::render::abi

namespace cellularity::app::render_worker {

class NativeVulkanWorkerStatusSignal;
class WorkerRebindWakeControl;

enum class FramePacingMode : std::uint8_t {
    ExternallyPacedContinuous = 0,
    OnDemand = 1,
};

enum class WorkerContentKind : std::uint8_t {
    Microscope = 0,
    GenomePreview = 1,
    EnvironmentFieldPreview = 2,
    Demo = 3,
};

enum class StartWorkerResult : std::uint8_t {
    Started = 0,
    ThreadStartFailed = 1,
    InvalidState = 2,
    Closing = 3,
};

enum class WorkerAuthorizationResult : std::uint8_t {
    Authorized = 0,
    AlreadyAuthorized = 1,
    WorkerBusy = 2,
    WorkerClosing = 3,
    StaleSurface = 4,
    AttachmentClosing = 5,
    InvalidArgument = 6,
    GenerationExhausted = 7,
};

enum class WorkerCommandAdmission : std::uint8_t {
    Accepted = 0,
    StaleGeneration = 1,
    InvalidArgument = 2,
    Dormant = 3,
    Closing = 4,
    GenerationExhausted = 5,
};

enum class FrameRequestAdmission : std::uint8_t {
    Accepted = 0,
    Coalesced = 1,
    Dormant = 2,
    Closing = 3,
    FailureLatched = 4,
};

enum class AwaitStoppedResult : std::uint8_t {
    Stopped = 0,
    TimedOut = 1,
    InvalidState = 2,
};

struct WorkerFailureDiagnosticsSnapshot final {
    std::uint64_t revision = 0;
    std::string report;
};

class NativeVulkanRenderWorker final {
public:
    using CurrentThreadSchedulingNormalizer =
            cellularity::platform::
                    CurrentThreadSchedulingNormalizationResult (*)() noexcept;

    NativeVulkanRenderWorker(
            std::shared_ptr<cellularity::render::abi::
                            VulkanBackendSessionFactory>
                    sessionFactory,
            std::shared_ptr<NativeRenderFrameSource> source,
            WorkerContentKind contentKind,
            FramePacingMode initialPacingMode,
            std::shared_ptr<NativeVulkanWorkerStatusSignal> statusSignal,
            CurrentThreadSchedulingNormalizer schedulingNormalizer =
                    cellularity::platform::
                            normalizeCurrentThreadInheritedPriorityBoost);
    ~NativeVulkanRenderWorker();

    NativeVulkanRenderWorker(const NativeVulkanRenderWorker&) = delete;
    NativeVulkanRenderWorker& operator=(
            const NativeVulkanRenderWorker&) = delete;

    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] WorkerContentKind contentKind() const noexcept;

    [[nodiscard]] WorkerAuthorizationResult authorizeMicroscope(
            std::shared_ptr<NativeRenderAttachmentLease> lease,
            std::uint64_t surfaceToken);
    [[nodiscard]] std::optional<WorkerBindingKey>
    authorizedMicroscopeBinding() const noexcept;
    [[nodiscard]] std::weak_ptr<RebindWakeControl>
    rebindWakeControl() const noexcept;
    [[nodiscard]] bool installDormantFrameSource(
            std::shared_ptr<NativeRenderFrameSource> source) noexcept;
    [[nodiscard]] StartWorkerResult start() noexcept;
    [[nodiscard]] WorkerCommandAdmission attachSurface(
            NativeVulkanSurfaceLease surface,
            int width, int height,
            std::uint64_t generation) noexcept;
    [[nodiscard]] WorkerCommandAdmission resizeSurface(
            int width, int height,
            std::uint64_t generation) noexcept;
    [[nodiscard]] WorkerCommandAdmission detachSurface(
            std::uint64_t generation) noexcept;
    [[nodiscard]] WorkerCommandAdmission resume() noexcept;
    [[nodiscard]] WorkerCommandAdmission pause() noexcept;
    [[nodiscard]] FrameRequestAdmission requestFrame() noexcept;
    [[nodiscard]] WorkerCommandAdmission setFramePacingMode(
            FramePacingMode mode) noexcept;
    [[nodiscard]] WorkerCommandAdmission setDemoPerformanceHudEnabled(
            bool enabled) noexcept;

    [[nodiscard]] NativeVulkanWorkerStatus status() const noexcept;
    [[nodiscard]] std::string diagnosticsReport() const;
    [[nodiscard]] WorkerFailureDiagnosticsSnapshot
    failureDiagnosticsSnapshot() const;
    [[nodiscard]] DiagnosticsRequestAdmission requestDiagnostics(
            std::uint64_t requestId) noexcept;
    [[nodiscard]] DiagnosticsChunkResult readDiagnosticsChunk(
            std::uint64_t requestId, std::uint64_t expectedRevision,
            std::uint64_t offset, std::uint8_t* destination,
            std::size_t destinationSize) const noexcept;
    [[nodiscard]] DiagnosticsReleaseResult releaseDiagnostics(
            std::uint64_t requestId,
            std::uint64_t expectedRevision) noexcept;

    void requestStop() noexcept;
    [[nodiscard]] AwaitStoppedResult awaitStopped(
            std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] bool finalizeBindingAfterJoin() noexcept;
    [[nodiscard]] bool readyToDestroy() const noexcept;

private:
    struct DesiredState final {
        NativeVulkanSurfaceLease pendingSurface;
        std::uint64_t surfaceGeneration = 0;
        std::uint64_t surfaceIntentRevision = 0;
        int width = 0;
        int height = 0;
        bool surfacePresent = false;
        bool resumed = false;
        bool frameRequested = false;
        bool retryPending = false;
        bool demoPerformanceHudEnabled = false;
        FramePacingMode pacingMode =
                FramePacingMode::ExternallyPacedContinuous;
    };

    struct AppliedState final {
        std::uint64_t surfaceGeneration = 0;
        std::uint64_t surfaceIntentRevision = 0;
        int width = 0;
        int height = 0;
        bool physicalSurfaceAttached = false;
        bool sourceSurfaceApplied = false;
    };

    void ownerThreadTrampoline() noexcept;
    void ownerLoop();
    [[nodiscard]] bool applySurfaceIntent();
    [[nodiscard]] bool renderRequestedFrame();
    [[nodiscard]] bool applyProfilingConfiguration();
    [[nodiscard]] bool processDiagnosticsRequest();
    [[nodiscard]] bool buildDiagnosticsReport(
            NativeVulkanWorkerFailure failure, const char* detail,
            bool failed, std::string& destination) noexcept;
    [[nodiscard]] std::uint64_t publishFailureDiagnostics(
            NativeVulkanWorkerFailure failure, const char* detail,
            bool terminal) noexcept;
    void cancelPendingDiagnostics() noexcept;
    void detachAppliedSurface(SurfaceDetachReason reason,
            bool backendAlreadyLost = false) noexcept;
    void teardownOnOwnerThread() noexcept;
    void setRecoverableFailure(
            NativeVulkanWorkerFailure failure,
            const char* detail) noexcept;
    void setTerminalFailure(
            NativeVulkanWorkerFailure failure,
            const char* detail) noexcept;
    void setFailureReportLocked(
            NativeVulkanWorkerFailure failure,
            const char* detail) noexcept;
    void invalidateFrameOutcomeLocked() noexcept;
    void refreshStatusLocked() noexcept;
    void emitStatusSignal(std::uint32_t actionBits) noexcept;
    void deliverPendingStatusSignalIfDue(bool force = false) noexcept;
    [[nodiscard]] std::chrono::steady_clock::time_point
    nextWakeDeadlineLocked() const noexcept;
    [[nodiscard]] bool incrementSurfaceIntentLocked() noexcept;
    [[nodiscard]] bool closingLocked() const noexcept;
    [[nodiscard]] bool runnableFrameLocked() const noexcept;
    [[nodiscard]] std::chrono::milliseconds nextRetryDelayLocked() noexcept;
    void joinIfExited() noexcept;
    void notifyRebindReady() noexcept;

    friend class WorkerRebindWakeControl;

    const std::uint64_t mIdentity;
    const WorkerContentKind mContentKind;
    std::shared_ptr<WorkerRebindWakeControl> mRebindWakeControl;
    std::shared_ptr<cellularity::render::abi::VulkanBackendSessionFactory>
            mSessionFactory;
    std::shared_ptr<NativeRenderFrameSource> mSource;
    std::shared_ptr<NativeVulkanWorkerStatusSignal> mStatusSignal;
    const CurrentThreadSchedulingNormalizer mSchedulingNormalizer;
    std::shared_ptr<cellularity::render::abi::VulkanBackendSession> mSession;
    std::shared_ptr<NativeRenderAttachmentLease> mMicroscopeLease;
    WorkerBindingKey mWorkerBinding{};

    mutable std::mutex mMutex;
    std::condition_variable mWake;
    std::condition_variable mExitedWake;
    DesiredState mDesired;
    AppliedState mApplied;
    NativeVulkanWorkerStatus mStatus;
    cellularity::platform::CurrentThreadSchedulingNormalizationResult
            mThreadSchedulingNormalization;
    std::thread mThread;
    std::chrono::steady_clock::time_point mRetryDeadline{};
    std::uint64_t mMutationSerial = 0;
    std::uint32_t mRetryStep = 0;
    std::uint64_t mAppliedProfilingConfiguration = 0;
    bool mGpuProfilingUnavailable = false;
    bool mStarted = false;
    bool mStarting = false;
    bool mStopRequested = false;
    bool mThreadExited = false;
    bool mJoined = false;
    bool mBindingFinalized = false;
    bool mSurfaceLost = false;
    bool mRecoverableFailure = false;
    bool mRecoverySurfacePending = false;
    bool mTerminalFailure = false;
    bool mFailureDiagnosticsUnavailable = false;
    bool mSourceBound = false;
    bool mSourceFrameDeferred = false;
    bool mSourceDetachPending = false;
    SurfaceDetachReason mPendingSourceDetachReason =
            SurfaceDetachReason::Ordinary;
    bool mStatusSignalDisabled = false;
    bool mStatusSignalDeliveryFailed = false;
    bool mStatusSignalDeliveryInProgress = false;
    std::uint32_t mPendingStatusSignalActions = 0;
    std::uint32_t mStatusSignalFailureCount = 0;
    std::uint64_t mPendingStatusSignalRevision = 0;
    std::chrono::steady_clock::time_point mStatusSignalRetryDeadline{};

    enum class ExplicitDiagnosticsState : std::uint8_t {
        Empty,
        Pending,
        Collecting,
        Completed,
        Failed,
    };

    mutable std::mutex mDiagnosticsMutex;
    std::string mFailureDiagnostics;
    std::string mExplicitDiagnostics;
    ExplicitDiagnosticsState mExplicitDiagnosticsState =
            ExplicitDiagnosticsState::Empty;
    std::uint64_t mExplicitDiagnosticsRequestId = 0;
    std::uint64_t mExplicitDiagnosticsRevision = 0;
    std::uint64_t mLastAcceptedDiagnosticsRequestId = 0;
    std::uint64_t mLastRetiredDiagnosticsRequestId = 0;
    std::uint64_t mNextDiagnosticsRevision = 1;
    std::uint64_t mFailureDiagnosticsRevision = 0;
    bool mExplicitDiagnosticsCancellationRequested = false;
    std::atomic<bool> mDiagnosticsAccepting{true};
};

} // namespace cellularity::app::render_worker
