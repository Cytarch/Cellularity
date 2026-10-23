#include "app/render/NativeVulkanRenderWorker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "app/render/NativeRenderProfilingState.h"
#include "app/render/NativeVulkanWorkerStatusSignal.h"
#include "platform/CurrentThreadName.h"
#include "render/backend/RenderFrameResult.h"
#include "render/backend/abi/VulkanBackendSession.h"
#include "render/backend/abi/VulkanBackendSessionFactory.h"

namespace cellularity::app::render_worker {
namespace {

std::atomic<std::uint64_t> gNextWorkerIdentity{1};

std::uint64_t acquireWorkerIdentity() {
    std::uint64_t candidate =
            gNextWorkerIdentity.load(std::memory_order_relaxed);
    while (candidate != 0) {
        const std::uint64_t next =
                candidate == std::numeric_limits<std::uint64_t>::max()
                ? 0
                : candidate + 1;
        if (gNextWorkerIdentity.compare_exchange_weak(candidate, next,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
            return candidate;
        }
    }
    throw std::overflow_error("Vulkan worker identity is exhausted");
}

bool statusObservableEqual(const NativeVulkanWorkerStatus& left,
        const NativeVulkanWorkerStatus& right) noexcept {
    return left.phase == right.phase
            && left.lastFrameOutcome == right.lastFrameOutcome
            && left.flags == right.flags
            && left.failure == right.failure
            && left.presentedFrames == right.presentedFrames
            && left.swapchainRecreations == right.swapchainRecreations
            && left.failedFrameAttempts == right.failedFrameAttempts
            && left.desiredSurfaceGeneration
            == right.desiredSurfaceGeneration
            && left.appliedSurfaceGeneration
            == right.appliedSurfaceGeneration
            && left.diagnosticsRevision == right.diagnosticsRevision
            && left.desiredWidth == right.desiredWidth
            && left.desiredHeight == right.desiredHeight;
}

WorkerAuthorizationResult mapAuthorization(
        LeaseMutationStatus status) {
    switch (status) {
        case LeaseMutationStatus::Applied:
            return WorkerAuthorizationResult::Authorized;
        case LeaseMutationStatus::AlreadyAuthorized:
            return WorkerAuthorizationResult::AlreadyAuthorized;
        case LeaseMutationStatus::WorkerBusy:
            return WorkerAuthorizationResult::WorkerBusy;
        case LeaseMutationStatus::WorkerClosing:
            return WorkerAuthorizationResult::WorkerClosing;
        case LeaseMutationStatus::StaleSurface:
            return WorkerAuthorizationResult::StaleSurface;
        case LeaseMutationStatus::AttachmentClosing:
            return WorkerAuthorizationResult::AttachmentClosing;
        case LeaseMutationStatus::InvalidArgument:
            return WorkerAuthorizationResult::InvalidArgument;
        case LeaseMutationStatus::GenerationExhausted:
            return WorkerAuthorizationResult::GenerationExhausted;
        case LeaseMutationStatus::DeferredRebind:
        case LeaseMutationStatus::StaleWorker:
        case LeaseMutationStatus::NotAuthorized:
            throw std::logic_error(
                    "Unexpected microscope worker authorization result");
    }
    throw std::logic_error("Unknown microscope worker authorization result");
}

bool operationSucceeded(CellularityVulkanOperationStatusV1 status) noexcept {
    return status == CELLULARITY_VULKAN_OPERATION_OK_V1;
}

bool operationRecoverable(CellularityVulkanOperationStatusV1 status) noexcept {
    return status == CELLULARITY_VULKAN_OPERATION_FAILED_V1;
}

void appendSchedulingDiagnostics(std::string& report,
        const cellularity::platform::
                CurrentThreadSchedulingNormalizationResult& result) {
    report += "normalization_status=";
    report += cellularity::platform::
            currentThreadSchedulingNormalizationStatusName(result.status);
    report += "\ninherited_nice=";
    report += result.inheritedNiceKnown
            ? std::to_string(result.inheritedNice)
            : "unknown";
    report += "\neffective_nice=";
    report += result.effectiveNiceKnown
            ? std::to_string(result.effectiveNice)
            : "unknown";
    report += "\nnormalization_errno="
            + std::to_string(result.errorNumber) + "\n";
}

} // namespace

class WorkerRebindWakeControl final : public RebindWakeControl {
public:
    explicit WorkerRebindWakeControl(
            NativeVulkanRenderWorker& owner) noexcept
            : mOwner(&owner) {}

    void notifyRebindReady() noexcept override {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mOwner != nullptr)
            mOwner->notifyRebindReady();
    }

    void deactivate() noexcept {
        std::lock_guard<std::mutex> lock(mMutex);
        mOwner = nullptr;
    }

private:
    std::mutex mMutex;
    NativeVulkanRenderWorker* mOwner;
};

NativeVulkanRenderWorker::NativeVulkanRenderWorker(
        std::shared_ptr<cellularity::render::abi::VulkanBackendSessionFactory>
                sessionFactory,
        std::shared_ptr<NativeRenderFrameSource> source,
        WorkerContentKind contentKind,
        FramePacingMode initialPacingMode,
        std::shared_ptr<NativeVulkanWorkerStatusSignal> statusSignal,
        CurrentThreadSchedulingNormalizer schedulingNormalizer)
        : mIdentity(acquireWorkerIdentity()),
          mContentKind(contentKind),
          mRebindWakeControl(
                  std::make_shared<WorkerRebindWakeControl>(*this)),
          mSessionFactory(std::move(sessionFactory)),
          mSource(std::move(source)),
          mStatusSignal(std::move(statusSignal)),
          mSchedulingNormalizer(schedulingNormalizer) {
    if (mSessionFactory == nullptr) {
        throw std::invalid_argument(
                "Native Vulkan worker requires a session factory");
    }
    if (mContentKind != WorkerContentKind::Microscope
            && mSource == nullptr) {
        throw std::invalid_argument(
                "Native Vulkan worker requires a frame source");
    }
    if (mSchedulingNormalizer == nullptr) {
        throw std::invalid_argument(
                "Native Vulkan worker requires a scheduling normalizer");
    }
    mDesired.pacingMode = initialPacingMode;
    mStatus.phase = NativeVulkanWorkerPhase::Dormant;
    mFailureDiagnostics = "schema=1\nstatus=not_started\nerror=\n";
    appendSchedulingDiagnostics(
            mFailureDiagnostics, mThreadSchedulingNormalization);
}

NativeVulkanRenderWorker::~NativeVulkanRenderWorker() {
    requestStop();
    if (mThread.joinable()) {
        mThread.join();
    }
    mRebindWakeControl->deactivate();
}

std::uint64_t NativeVulkanRenderWorker::identity() const noexcept {
    return mIdentity;
}

WorkerContentKind NativeVulkanRenderWorker::contentKind() const noexcept {
    return mContentKind;
}

WorkerAuthorizationResult NativeVulkanRenderWorker::authorizeMicroscope(
        std::shared_ptr<NativeRenderAttachmentLease> lease,
        std::uint64_t surfaceToken) {
    if (lease == nullptr || surfaceToken == 0
            || mContentKind != WorkerContentKind::Microscope) {
        return WorkerAuthorizationResult::InvalidArgument;
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (closingLocked()) {
            return WorkerAuthorizationResult::WorkerClosing;
        }
        if (mStarted || mStarting) {
            return WorkerAuthorizationResult::WorkerBusy;
        }
        if (mWorkerBinding.valid()) {
            return mMicroscopeLease == lease
                            && mWorkerBinding.surfaceToken == surfaceToken
                    ? WorkerAuthorizationResult::AlreadyAuthorized
                    : WorkerAuthorizationResult::WorkerBusy;
        }
    }

    const auto authorized = lease->authorizeWorker(surfaceToken, mIdentity);
    const WorkerAuthorizationResult mapped = mapAuthorization(authorized.status);
    if ((mapped != WorkerAuthorizationResult::Authorized
                && mapped != WorkerAuthorizationResult::AlreadyAuthorized)
            || !authorized.value.has_value()) {
        return mapped;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    if (closingLocked() || mStarted || mStarting || mWorkerBinding.valid()) {
        (void)lease->markWorkerClosing(*authorized.value);
        (void)lease->finishWorkerClose(*authorized.value);
        return WorkerAuthorizationResult::WorkerClosing;
    }
    mMicroscopeLease = std::move(lease);
    mWorkerBinding = *authorized.value;
    return mapped;
}

std::optional<WorkerBindingKey>
NativeVulkanRenderWorker::authorizedMicroscopeBinding() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mWorkerBinding.valid()
            ? std::optional<WorkerBindingKey>(mWorkerBinding)
            : std::nullopt;
}

std::weak_ptr<RebindWakeControl>
NativeVulkanRenderWorker::rebindWakeControl() const noexcept {
    return mRebindWakeControl;
}

bool NativeVulkanRenderWorker::installDormantFrameSource(
        std::shared_ptr<NativeRenderFrameSource> source) noexcept {
    if (source == nullptr)
        return false;
    std::lock_guard<std::mutex> lock(mMutex);
    if (mStarted || mStarting || closingLocked() || mSource != nullptr) {
        return false;
    }
    mSource = std::move(source);
    return true;
}

StartWorkerResult NativeVulkanRenderWorker::start() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (closingLocked()) {
        return StartWorkerResult::Closing;
    }
    if (mStarted || mStarting || mSource == nullptr
            || (mContentKind == WorkerContentKind::Microscope
                    && !mWorkerBinding.valid())) {
        return StartWorkerResult::InvalidState;
    }
    mStarting = true;
    refreshStatusLocked();
    try {
        // POSIX threads inherit the caller's scheduling attributes. Android's
        // UI thread may carry an interactive nice boost, so normalization is
        // part of worker bootstrap rather than render-loop policy.
        mThread = std::thread(&NativeVulkanRenderWorker::ownerThreadTrampoline,
                this);
        mStarted = true;
        ++mMutationSerial;
        return StartWorkerResult::Started;
    } catch (...) {
        mStarting = false;
        mStarted = true;
        mThreadExited = true;
        mTerminalFailure = true;
        mDiagnosticsAccepting.store(false, std::memory_order_release);
        cancelPendingDiagnostics();
        mStatus.failure = NativeVulkanWorkerFailure::ThreadStartFailed;
        setFailureReportLocked(
                NativeVulkanWorkerFailure::ThreadStartFailed,
                "Could not create the native Vulkan render thread");
        refreshStatusLocked();
        mExitedWake.notify_all();
        return StartWorkerResult::ThreadStartFailed;
    }
}

WorkerCommandAdmission NativeVulkanRenderWorker::attachSurface(
        NativeVulkanSurfaceLease surface,
        int width, int height,
        std::uint64_t generation) noexcept {
    if (!surface || generation == 0 || width < 0 || height < 0) {
        return WorkerCommandAdmission::InvalidArgument;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mStarted && !mStarting) {
        return WorkerCommandAdmission::Dormant;
    }
    if (closingLocked()) {
        return WorkerCommandAdmission::Closing;
    }
    if (generation < mDesired.surfaceGeneration) {
        return WorkerCommandAdmission::StaleGeneration;
    }
    if (!incrementSurfaceIntentLocked()) {
        return WorkerCommandAdmission::GenerationExhausted;
    }
    mDesired.pendingSurface = std::move(surface);
    mDesired.surfaceGeneration = generation;
    mDesired.width = width;
    mDesired.height = height;
    mDesired.surfacePresent = true;
    mRecoverySurfacePending = mRecoverableFailure;
    if (mDesired.retryPending)
        mDesired.frameRequested = true;
    mDesired.retryPending = false;
    mRetryStep = 0;
    mRetryDeadline = {};
    ++mMutationSerial;
    refreshStatusLocked();
    mWake.notify_one();
    return WorkerCommandAdmission::Accepted;
}

WorkerCommandAdmission NativeVulkanRenderWorker::resizeSurface(
        int width, int height,
        std::uint64_t generation) noexcept {
    if (generation == 0 || width < 0 || height < 0) {
        return WorkerCommandAdmission::InvalidArgument;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mStarted && !mStarting)
        return WorkerCommandAdmission::Dormant;
    if (closingLocked())
        return WorkerCommandAdmission::Closing;
    if (!mDesired.surfacePresent
            || generation != mDesired.surfaceGeneration) {
        return WorkerCommandAdmission::StaleGeneration;
    }
    if (!incrementSurfaceIntentLocked()) {
        return WorkerCommandAdmission::GenerationExhausted;
    }
    if (mDesired.retryPending)
        mDesired.frameRequested = true;
    mDesired.retryPending = false;
    mRetryStep = 0;
    mRetryDeadline = {};
    mDesired.width = width;
    mDesired.height = height;
    ++mMutationSerial;
    refreshStatusLocked();
    mWake.notify_one();
    return WorkerCommandAdmission::Accepted;
}

WorkerCommandAdmission NativeVulkanRenderWorker::detachSurface(
        std::uint64_t generation) noexcept {
    if (generation == 0)
        return WorkerCommandAdmission::InvalidArgument;
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mStarted && !mStarting)
        return WorkerCommandAdmission::Dormant;
    if (closingLocked())
        return WorkerCommandAdmission::Closing;
    if (generation != mDesired.surfaceGeneration) {
        return WorkerCommandAdmission::StaleGeneration;
    }
    if (!incrementSurfaceIntentLocked()) {
        return WorkerCommandAdmission::GenerationExhausted;
    }
    mDesired.pendingSurface.reset();
    mDesired.surfacePresent = false;
    mRecoverySurfacePending = false;
    if (mDesired.retryPending)
        mDesired.frameRequested = true;
    mDesired.retryPending = false;
    mRetryStep = 0;
    mRetryDeadline = {};
    mDesired.width = 0;
    mDesired.height = 0;
    ++mMutationSerial;
    refreshStatusLocked();
    mWake.notify_one();
    return WorkerCommandAdmission::Accepted;
}

WorkerCommandAdmission NativeVulkanRenderWorker::resume() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mStarted && !mStarting)
        return WorkerCommandAdmission::Dormant;
    if (closingLocked())
        return WorkerCommandAdmission::Closing;
    if (!incrementSurfaceIntentLocked()) {
        return WorkerCommandAdmission::GenerationExhausted;
    }
    mDesired.resumed = true;
    ++mMutationSerial;
    refreshStatusLocked();
    mWake.notify_one();
    return WorkerCommandAdmission::Accepted;
}

WorkerCommandAdmission NativeVulkanRenderWorker::pause() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mStarted && !mStarting)
        return WorkerCommandAdmission::Dormant;
    if (closingLocked())
        return WorkerCommandAdmission::Closing;
    if (!incrementSurfaceIntentLocked()) {
        return WorkerCommandAdmission::GenerationExhausted;
    }
    mDesired.resumed = false;
    mDesired.pendingSurface.reset();
    mDesired.surfacePresent = false;
    mRecoverySurfacePending = false;
    if (mDesired.retryPending)
        mDesired.frameRequested = true;
    mDesired.width = 0;
    mDesired.height = 0;
    mDesired.retryPending = false;
    mRetryDeadline = {};
    ++mMutationSerial;
    refreshStatusLocked();
    mWake.notify_one();
    return WorkerCommandAdmission::Accepted;
}

FrameRequestAdmission NativeVulkanRenderWorker::requestFrame() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mStarted && !mStarting)
        return FrameRequestAdmission::Dormant;
    if (closingLocked())
        return FrameRequestAdmission::Closing;
    if (mRecoverableFailure && !mRecoverySurfacePending) {
        return FrameRequestAdmission::FailureLatched;
    }
    const bool coalesced = mDesired.frameRequested || mDesired.retryPending;
    mDesired.frameRequested = true;
    mDesired.retryPending = false;
    mRetryStep = 0;
    mRetryDeadline = {};
    ++mMutationSerial;
    refreshStatusLocked();
    mWake.notify_one();
    return coalesced
            ? FrameRequestAdmission::Coalesced
            : FrameRequestAdmission::Accepted;
}

WorkerCommandAdmission NativeVulkanRenderWorker::setFramePacingMode(
        FramePacingMode mode) noexcept {
    if (mode != FramePacingMode::ExternallyPacedContinuous
            && mode != FramePacingMode::OnDemand) {
        return WorkerCommandAdmission::InvalidArgument;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mStarted && !mStarting)
        return WorkerCommandAdmission::Dormant;
    if (closingLocked())
        return WorkerCommandAdmission::Closing;
    mDesired.pacingMode = mode;
    if (mode == FramePacingMode::ExternallyPacedContinuous) {
        mDesired.retryPending = false;
        mRetryStep = 0;
        mRetryDeadline = {};
    }
    ++mMutationSerial;
    refreshStatusLocked();
    mWake.notify_one();
    return WorkerCommandAdmission::Accepted;
}

WorkerCommandAdmission
NativeVulkanRenderWorker::setDemoPerformanceHudEnabled(
        bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mContentKind != WorkerContentKind::Demo) {
        return WorkerCommandAdmission::InvalidArgument;
    }
    if (!mStarted && !mStarting)
        return WorkerCommandAdmission::Dormant;
    if (closingLocked())
        return WorkerCommandAdmission::Closing;
    mDesired.demoPerformanceHudEnabled = enabled;
    ++mMutationSerial;
    mWake.notify_one();
    return WorkerCommandAdmission::Accepted;
}

NativeVulkanWorkerStatus NativeVulkanRenderWorker::status() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mStatus;
}

std::string NativeVulkanRenderWorker::diagnosticsReport() const {
    std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
    return mFailureDiagnostics;
}

WorkerFailureDiagnosticsSnapshot
NativeVulkanRenderWorker::failureDiagnosticsSnapshot() const {
    std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
    return {mFailureDiagnosticsRevision, mFailureDiagnostics};
}

DiagnosticsRequestAdmission NativeVulkanRenderWorker::requestDiagnostics(
        std::uint64_t requestId) noexcept {
    if (requestId == 0
            || requestId
                    > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max())) {
        return DiagnosticsRequestAdmission::InvalidRequestId;
    }
    if (!mDiagnosticsAccepting.load(std::memory_order_acquire)) {
        return DiagnosticsRequestAdmission::Closing;
    }
    {
        std::lock_guard<std::mutex> stateLock(mMutex);
        if (closingLocked()
                || !mDiagnosticsAccepting.load(std::memory_order_relaxed)) {
            return DiagnosticsRequestAdmission::Closing;
        }
        std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
        if (mExplicitDiagnosticsState != ExplicitDiagnosticsState::Empty) {
            return DiagnosticsRequestAdmission::Busy;
        }
        if (mLastAcceptedDiagnosticsRequestId
                        == std::numeric_limits<std::uint64_t>::max()
                || requestId != mLastAcceptedDiagnosticsRequestId + 1U) {
            return DiagnosticsRequestAdmission::InvalidRequestId;
        }
        if (mNextDiagnosticsRevision == 0
                || mNextDiagnosticsRevision
                        == std::numeric_limits<std::uint64_t>::max()) {
            return DiagnosticsRequestAdmission::RevisionExhausted;
        }
        mExplicitDiagnostics.clear();
        mExplicitDiagnosticsRequestId = requestId;
        mExplicitDiagnosticsRevision = 0;
        mExplicitDiagnosticsCancellationRequested = false;
        mExplicitDiagnosticsState = ExplicitDiagnosticsState::Pending;
        mLastAcceptedDiagnosticsRequestId = requestId;
        ++mMutationSerial;
    }
    mWake.notify_one();
    return DiagnosticsRequestAdmission::Accepted;
}

DiagnosticsChunkResult NativeVulkanRenderWorker::readDiagnosticsChunk(
        std::uint64_t requestId, std::uint64_t expectedRevision,
        std::uint64_t offset, std::uint8_t* destination,
        std::size_t destinationSize) const noexcept {
    DiagnosticsChunkResult result;
    std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
    const std::string* report = nullptr;
    if (requestId == 0) {
        if (expectedRevision == 0 || mFailureDiagnosticsRevision == 0) {
            result.status = DiagnosticsChunkReadStatus::InvalidRequestId;
            return result;
        }
        result.revision = mFailureDiagnosticsRevision;
        result.totalBytes = mFailureDiagnostics.size();
        if (expectedRevision != mFailureDiagnosticsRevision) {
            result.status = DiagnosticsChunkReadStatus::StaleRevision;
            return result;
        }
        report = &mFailureDiagnostics;
    } else {
        if (requestId != mExplicitDiagnosticsRequestId) {
            result.status = requestId <= mLastRetiredDiagnosticsRequestId
                    ? DiagnosticsChunkReadStatus::Cancelled
                    : DiagnosticsChunkReadStatus::InvalidRequestId;
            return result;
        }
        if (mExplicitDiagnosticsState == ExplicitDiagnosticsState::Pending
                || mExplicitDiagnosticsState
                        == ExplicitDiagnosticsState::Collecting) {
            result.status = DiagnosticsChunkReadStatus::Pending;
            return result;
        }
        if (mExplicitDiagnosticsState == ExplicitDiagnosticsState::Failed) {
            result.status = DiagnosticsChunkReadStatus::Failed;
            return result;
        }
        if (mExplicitDiagnosticsState
                != ExplicitDiagnosticsState::Completed) {
            result.status = DiagnosticsChunkReadStatus::InvalidRequestId;
            return result;
        }
        result.revision = mExplicitDiagnosticsRevision;
        result.totalBytes = mExplicitDiagnostics.size();
        if (expectedRevision != 0
                && expectedRevision != mExplicitDiagnosticsRevision) {
            result.status = DiagnosticsChunkReadStatus::StaleRevision;
            return result;
        }
        report = &mExplicitDiagnostics;
    }
    if (offset > report->size()) {
        result.status = DiagnosticsChunkReadStatus::InvalidOffset;
        return result;
    }
    const std::size_t remaining = report->size()
            - static_cast<std::size_t>(offset);
    const std::size_t count = std::min({remaining, destinationSize,
            kNativeVulkanDiagnosticsMaximumChunkSize});
    if (count != 0) {
        if (destination == nullptr) {
            result.status = DiagnosticsChunkReadStatus::Failed;
            return result;
        }
        std::memcpy(destination,
                report->data() + static_cast<std::size_t>(offset), count);
    }
    result.bytesWritten = static_cast<std::uint32_t>(count);
    result.status = DiagnosticsChunkReadStatus::Ready;
    return result;
}

DiagnosticsReleaseResult NativeVulkanRenderWorker::releaseDiagnostics(
        std::uint64_t requestId,
        std::uint64_t expectedRevision) noexcept {
    if (requestId == 0) {
        return DiagnosticsReleaseResult::InvalidRequestId;
    }
    std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
    if (requestId <= mLastRetiredDiagnosticsRequestId) {
        return DiagnosticsReleaseResult::AlreadyReleased;
    }
    if (requestId != mExplicitDiagnosticsRequestId
            || requestId > mLastAcceptedDiagnosticsRequestId) {
        return DiagnosticsReleaseResult::InvalidRequestId;
    }
    if (mExplicitDiagnosticsState == ExplicitDiagnosticsState::Collecting) {
        if (expectedRevision != 0) {
            return DiagnosticsReleaseResult::StaleRevision;
        }
        mExplicitDiagnosticsCancellationRequested = true;
        return DiagnosticsReleaseResult::CancellationRequested;
    }
    if (mExplicitDiagnosticsState == ExplicitDiagnosticsState::Completed
            && expectedRevision != 0
            && expectedRevision != mExplicitDiagnosticsRevision) {
        return DiagnosticsReleaseResult::StaleRevision;
    }
    if (mExplicitDiagnosticsState != ExplicitDiagnosticsState::Pending
            && mExplicitDiagnosticsState != ExplicitDiagnosticsState::Completed
            && mExplicitDiagnosticsState != ExplicitDiagnosticsState::Failed) {
        return DiagnosticsReleaseResult::InvalidRequestId;
    }
    mLastRetiredDiagnosticsRequestId = requestId;
    mExplicitDiagnosticsRequestId = 0;
    mExplicitDiagnosticsRevision = 0;
    mExplicitDiagnostics.clear();
    mExplicitDiagnosticsCancellationRequested = false;
    mExplicitDiagnosticsState = ExplicitDiagnosticsState::Empty;
    return DiagnosticsReleaseResult::Released;
}

void NativeVulkanRenderWorker::requestStop() noexcept {
    mDiagnosticsAccepting.store(false, std::memory_order_release);
    cancelPendingDiagnostics();
    std::shared_ptr<NativeRenderAttachmentLease> lease;
    WorkerBindingKey binding{};
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mStopRequested)
            return;
        mStopRequested = true;
        mDesired.pendingSurface.reset();
        mDesired.surfacePresent = false;
        mRecoverySurfacePending = false;
        mDesired.frameRequested = false;
        mDesired.retryPending = false;
        mRetryDeadline = {};
        mPendingStatusSignalActions = 0;
        mPendingStatusSignalRevision = 0;
        mStatusSignalRetryDeadline = {};
        mStatusSignalDisabled = true;
        lease = mMicroscopeLease;
        binding = mWorkerBinding;
        ++mMutationSerial;
        refreshStatusLocked();
        mWake.notify_all();
        if (!mStarted && !mStarting) {
            mThreadExited = true;
            mJoined = true;
            mExitedWake.notify_all();
        }
    }
    if (lease != nullptr && binding.valid()) {
        const auto status = lease->markWorkerClosing(binding);
        (void)status;
    }
}

AwaitStoppedResult NativeVulkanRenderWorker::awaitStopped(
        std::chrono::milliseconds timeout) noexcept {
    if (timeout.count() <= 0)
        return AwaitStoppedResult::InvalidState;
    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (!mStopRequested && !mTerminalFailure) {
            return AwaitStoppedResult::InvalidState;
        }
        if (!mExitedWake.wait_for(lock, timeout, [&] {
                return mThreadExited;
            })) {
            return AwaitStoppedResult::TimedOut;
        }
    }
    joinIfExited();
    return AwaitStoppedResult::Stopped;
}

bool NativeVulkanRenderWorker::finalizeBindingAfterJoin() noexcept {
    std::shared_ptr<NativeRenderAttachmentLease> lease;
    WorkerBindingKey binding{};
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mJoined)
            return false;
        if (mBindingFinalized)
            return true;
        lease = mMicroscopeLease;
        binding = mWorkerBinding;
    }
    bool finalized = true;
    if (lease != nullptr && binding.valid()) {
        const auto marked = lease->markWorkerClosing(binding);
        if (marked != LeaseMutationStatus::Applied
                && marked != LeaseMutationStatus::WorkerClosing) {
            finalized = false;
        }
        const auto finished = lease->finishWorkerClose(binding);
        if (finished != LeaseMutationStatus::Applied
                && finished != LeaseMutationStatus::StaleWorker) {
            finalized = false;
        }
    }
    std::lock_guard<std::mutex> lock(mMutex);
    if (finalized) {
        mBindingFinalized = true;
        mMicroscopeLease.reset();
        mWorkerBinding = {};
    }
    return finalized;
}

bool NativeVulkanRenderWorker::readyToDestroy() const noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    return mJoined && mBindingFinalized;
}

void NativeVulkanRenderWorker::ownerThreadTrampoline() noexcept {
    cellularity::platform::setCurrentThreadName("CellularityVk");
    const auto schedulingNormalization = mSchedulingNormalizer();
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mThreadSchedulingNormalization = schedulingNormalization;
    }
    try {
        ownerLoop();
    } catch (const std::bad_alloc&) {
        setTerminalFailure(NativeVulkanWorkerFailure::WorkerOutOfMemory,
                "Native Vulkan worker ran out of memory");
        teardownOnOwnerThread();
    } catch (const std::exception& failure) {
        setTerminalFailure(
                NativeVulkanWorkerFailure::WorkerUnhandledException,
                failure.what());
        teardownOnOwnerThread();
    } catch (...) {
        setTerminalFailure(
                NativeVulkanWorkerFailure::WorkerUnhandledException,
                "Unknown native Vulkan worker failure");
        teardownOnOwnerThread();
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStarting = false;
        mThreadExited = true;
        refreshStatusLocked();
    }
    mExitedWake.notify_all();
}

void NativeVulkanRenderWorker::ownerLoop() {
    const auto created = mSessionFactory->createOnOwnerThread();
    if (!created.succeeded()) {
        const NativeVulkanWorkerFailure failure =
                created.error.code == CELLULARITY_VULKAN_ERROR_ASSET_UNAVAILABLE_V1
                ? NativeVulkanWorkerFailure::AssetUnavailable
                : NativeVulkanWorkerFailure::SessionCreateFailed;
        setTerminalFailure(failure,
                created.error.message_length == 0
                        ? "Could not create the Vulkan backend session"
                        : created.error.message);
        teardownOnOwnerThread();
        return;
    }
    mSession = created.session;

    bool stoppedDuringStartup = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        stoppedDuringStartup = mStopRequested;
    }
    if (stoppedDuringStartup) {
        // Stop-during-start never publishes a backend adapter into the
        // controller.
        teardownOnOwnerThread();
        return;
    }

    while (true) {
        std::uint64_t observedMutation = 0;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            observedMutation = mMutationSerial;
        }
        const SourceSurfaceResult bound = mSource->bindBackend(mSession);
        if (bound == SourceSurfaceResult::Applied) {
            mSourceBound = true;
            break;
        }
        if (bound == SourceSurfaceResult::Closing) {
            requestStop();
            teardownOnOwnerThread();
            return;
        }
        if (bound == SourceSurfaceResult::Failed) {
            setTerminalFailure(NativeVulkanWorkerFailure::SourceSurfaceFailed,
                    "Could not bind the Vulkan frame source");
            teardownOnOwnerThread();
            return;
        }
        std::unique_lock<std::mutex> lock(mMutex);
        if (mStopRequested) {
            lock.unlock();
            teardownOnOwnerThread();
            return;
        }
        if (mMutationSerial != observedMutation)
            continue;
        mWake.wait(lock, [&] {
            return mStopRequested || mMutationSerial != observedMutation;
        });
    }

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStarting = false;
        refreshStatusLocked();
    }

    while (true) {
        std::uint64_t observedMutation = 0;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            observedMutation = mMutationSerial;
        }
        deliverPendingStatusSignalIfDue();
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mStopRequested)
                break;
            if (mTerminalFailure
                    && mPendingStatusSignalActions == 0
                    && !mStatusSignalDeliveryInProgress) {
                break;
            }
            if (mDesired.retryPending && mRetryDeadline.time_since_epoch().count() != 0
                    && std::chrono::steady_clock::now() >= mRetryDeadline) {
                mDesired.retryPending = false;
                mDesired.frameRequested = true;
                mRetryDeadline = {};
                refreshStatusLocked();
            }
        }
        bool terminal = false;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            terminal = mTerminalFailure;
        }
        if (processDiagnosticsRequest())
            continue;
        if (!terminal && applySurfaceIntent())
            continue;
        if (!terminal && renderRequestedFrame())
            continue;

        std::unique_lock<std::mutex> lock(mMutex);
        if (mStopRequested)
            break;
        if (mTerminalFailure
                && mPendingStatusSignalActions == 0
                && !mStatusSignalDeliveryInProgress) {
            break;
        }
        if (mMutationSerial != observedMutation)
            continue;
        const auto wakeDeadline = nextWakeDeadlineLocked();
        if (wakeDeadline.time_since_epoch().count() != 0) {
            mWake.wait_until(lock, wakeDeadline, [&] {
                return mStopRequested
                        || (mTerminalFailure
                                && mPendingStatusSignalActions == 0)
                        || mMutationSerial != observedMutation;
            });
        } else {
            mWake.wait(lock, [&] {
                return mStopRequested
                        || (mTerminalFailure
                                && mPendingStatusSignalActions == 0)
                        || mMutationSerial != observedMutation;
            });
        }
    }
    teardownOnOwnerThread();
}

bool NativeVulkanRenderWorker::applySurfaceIntent() {
    bool detachPendingSource = false;
    SurfaceDetachReason pendingReason = SurfaceDetachReason::Ordinary;
    bool detachPhysical = false;
    SurfaceDetachReason detachReason = SurfaceDetachReason::Ordinary;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        detachPendingSource = mSourceDetachPending;
        pendingReason = mPendingSourceDetachReason;
        const bool replacement = mApplied.physicalSurfaceAttached
                && static_cast<bool>(mDesired.pendingSurface);
        detachPhysical = mApplied.physicalSurfaceAttached
                && (!mDesired.resumed || !mDesired.surfacePresent
                        || replacement
                        || mApplied.surfaceGeneration
                                != mDesired.surfaceGeneration);
        detachReason = !mDesired.resumed
                ? SurfaceDetachReason::Pause
                : SurfaceDetachReason::Ordinary;
    }

    if (detachPendingSource) {
        const SourceSurfaceResult result =
                mSource->onSurfaceDetached(pendingReason);
        if (result == SourceSurfaceResult::DeferredRebind) {
            return false;
        }
        if (result == SourceSurfaceResult::Failed) {
            setTerminalFailure(NativeVulkanWorkerFailure::SourceSurfaceFailed,
                    "Vulkan frame source detach failed");
            return true;
        }
        if (result == SourceSurfaceResult::Closing) {
            requestStop();
            return true;
        }
        std::lock_guard<std::mutex> lock(mMutex);
        mSourceDetachPending = false;
        mApplied.sourceSurfaceApplied = false;
        refreshStatusLocked();
        return true;
    }

    if (detachPhysical) {
        detachAppliedSurface(detachReason);
        return true;
    }

    NativeVulkanSurfaceLease pending;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    int width = 0;
    int height = 0;
    bool attach = false;
    bool sourceAttach = false;
    bool resize = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mDesired.resumed || !mDesired.surfacePresent
                || (mRecoverableFailure && !mRecoverySurfacePending)
                || mDesired.width <= 0 || mDesired.height <= 0) {
            return false;
        }
        generation = mDesired.surfaceGeneration;
        revision = mDesired.surfaceIntentRevision;
        width = mDesired.width;
        height = mDesired.height;
        if (!mApplied.physicalSurfaceAttached && mDesired.pendingSurface) {
            pending = std::move(mDesired.pendingSurface);
            attach = true;
        } else if (mApplied.physicalSurfaceAttached
                && !mApplied.sourceSurfaceApplied) {
            generation = mApplied.surfaceGeneration;
            revision = mApplied.surfaceIntentRevision;
            width = mApplied.width;
            height = mApplied.height;
            sourceAttach = true;
        } else if (mApplied.physicalSurfaceAttached
                && (mApplied.width != width || mApplied.height != height)) {
            resize = true;
        } else {
            return false;
        }
    }

    if (attach) {
        const auto status = mSession->attachWindow(
                pending.takeForBackend(), width, height);
        if (!operationSucceeded(status)) {
            if (operationRecoverable(status)) {
                setRecoverableFailure(
                        NativeVulkanWorkerFailure::SurfaceAttachFailed,
                        "Vulkan surface attachment failed");
            } else {
                setTerminalFailure(
                        NativeVulkanWorkerFailure::BackendControlFailed,
                        "Vulkan surface attachment returned an invalid status");
            }
            return true;
        }
        bool current = false;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            current = !closingLocked() && mDesired.resumed
                    && mDesired.surfacePresent
                    && mDesired.surfaceGeneration == generation
                    && !mDesired.pendingSurface;
            if (current) {
                mApplied.physicalSurfaceAttached = true;
                mApplied.surfaceGeneration = generation;
                mApplied.surfaceIntentRevision = revision;
                mApplied.width = width;
                mApplied.height = height;
                mSurfaceLost = false;
                refreshStatusLocked();
            }
        }
        if (!current) {
            const auto detached = mSession->detachSurface();
            if (!operationSucceeded(detached)) {
                setTerminalFailure(
                        NativeVulkanWorkerFailure::BackendControlFailed,
                        "Could not roll back a stale Vulkan surface");
            }
            return true;
        }
        sourceAttach = true;
    }

    if (sourceAttach) {
        const SourceSurfaceResult result = mSource->onSurfaceAttached(
                generation, width, height);
        if (result == SourceSurfaceResult::DeferredRebind) {
            return false;
        }
        if (result == SourceSurfaceResult::Closing) {
            requestStop();
            return true;
        }
        if (result == SourceSurfaceResult::Failed) {
            detachAppliedSurface(SurfaceDetachReason::Ordinary);
            setTerminalFailure(NativeVulkanWorkerFailure::SourceSurfaceFailed,
                    "Vulkan frame source attachment failed");
            return true;
        }
        bool current = false;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            current = !closingLocked() && mDesired.resumed
                    && mDesired.surfacePresent
                    && mDesired.surfaceGeneration == generation
                    && !mDesired.pendingSurface
                    && mApplied.physicalSurfaceAttached
                    && mApplied.surfaceGeneration == generation;
            if (current) {
                mApplied.sourceSurfaceApplied = true;
                mApplied.width = width;
                mApplied.height = height;
                if (mDesired.width == width && mDesired.height == height) {
                    mApplied.surfaceIntentRevision =
                            mDesired.surfaceIntentRevision;
                } else {
                    mApplied.surfaceIntentRevision = revision;
                }
                mRecoverableFailure = false;
                mRecoverySurfacePending = false;
                mSurfaceLost = false;
                mStatus.failure = NativeVulkanWorkerFailure::None;
                mFailureDiagnosticsUnavailable = false;
                invalidateFrameOutcomeLocked();
                refreshStatusLocked();
            }
        }
        if (!current) {
            {
                std::lock_guard<std::mutex> lock(mMutex);
                // The callback completed successfully even though the intent
                // became stale while it was running. Mark the logical attach
                // locally so ordered rollback delivers its matching detach.
                mApplied.sourceSurfaceApplied = true;
            }
            detachAppliedSurface(SurfaceDetachReason::Ordinary);
        }
        return true;
    }

    if (resize) {
        const auto status = mSession->resizeSurface(width, height);
        if (!operationSucceeded(status)) {
            if (operationRecoverable(status)) {
                setRecoverableFailure(
                        NativeVulkanWorkerFailure::SurfaceResizeFailed,
                        "Vulkan surface resize failed");
            } else {
                setTerminalFailure(
                        NativeVulkanWorkerFailure::BackendControlFailed,
                        "Vulkan surface resize returned an invalid status");
            }
            return true;
        }
        {
            std::lock_guard<std::mutex> lock(mMutex);
            const bool current = !closingLocked() && mDesired.resumed
                    && mDesired.surfacePresent
                    && mDesired.surfaceGeneration == generation
                    && !mDesired.pendingSurface
                    && mDesired.width == width && mDesired.height == height
                    && mApplied.physicalSurfaceAttached
                    && mApplied.surfaceGeneration == generation;
            if (!current)
                return true;
        }
        const SourceSurfaceResult result =
                mSource->onSurfaceResized(width, height);
        if (result == SourceSurfaceResult::DeferredRebind) {
            std::lock_guard<std::mutex> lock(mMutex);
            mApplied.sourceSurfaceApplied = false;
            refreshStatusLocked();
            return false;
        }
        if (result == SourceSurfaceResult::Closing) {
            requestStop();
            return true;
        }
        if (result == SourceSurfaceResult::Failed) {
            setTerminalFailure(NativeVulkanWorkerFailure::SourceSurfaceFailed,
                    "Vulkan frame source resize failed");
            return true;
        }
        std::lock_guard<std::mutex> lock(mMutex);
        if (!closingLocked() && mDesired.resumed
                && mDesired.surfacePresent
                && mDesired.surfaceGeneration == generation
                && !mDesired.pendingSurface && mDesired.width == width
                && mDesired.height == height
                && mApplied.physicalSurfaceAttached
                && mApplied.surfaceGeneration == generation) {
            mApplied.width = width;
            mApplied.height = height;
            mApplied.surfaceIntentRevision =
                    mDesired.surfaceIntentRevision;
            invalidateFrameOutcomeLocked();
            refreshStatusLocked();
        }
        return true;
    }
    return false;
}

bool NativeVulkanRenderWorker::renderRequestedFrame() {
    FramePacingMode pacing = FramePacingMode::ExternallyPacedContinuous;
    bool demoHud = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!runnableFrameLocked())
            return false;
        mDesired.frameRequested = false;
        pacing = mDesired.pacingMode;
        demoHud = mDesired.demoPerformanceHudEnabled;
        refreshStatusLocked();
    }
    if (mContentKind == WorkerContentKind::Demo) {
        mSource->setPerformanceHudEnabled(demoHud);
    }

    if (!applyProfilingConfiguration())
        return true;

    const NativeRenderProfilingFrameToken profile =
            beginNativeRenderProfileFrame();
    const SourceFrameResult result = mSource->renderFrame();
    if (result.status == SourceFrameStatus::DeferredRebind) {
        finishNativeRenderProfileFrame(profile, mIdentity,
                mContentKind == WorkerContentKind::Microscope,
                cellularity::render::RenderFrameOutcome::Deferred);
        std::lock_guard<std::mutex> lock(mMutex);
        if (!closingLocked()) {
            mSourceFrameDeferred = true;
            mDesired.frameRequested = true;
            refreshStatusLocked();
        }
        return true;
    }
    if (result.status == SourceFrameStatus::Closing) {
        finishNativeRenderProfileFrame(profile, mIdentity,
                mContentKind == WorkerContentKind::Microscope,
                cellularity::render::RenderFrameOutcome::Failed);
        requestStop();
        return true;
    }
    if (result.status == SourceFrameStatus::Failed) {
        finishNativeRenderProfileFrame(profile, mIdentity,
                mContentKind == WorkerContentKind::Microscope,
                cellularity::render::RenderFrameOutcome::Failed);
        setTerminalFailure(NativeVulkanWorkerFailure::SourceFrameFailed,
                "Vulkan frame source failed");
        return true;
    }
    if (!cellularity::render::isValidRenderFrameResult(result.frame)) {
        finishNativeRenderProfileFrame(profile, mIdentity,
                mContentKind == WorkerContentKind::Microscope,
                cellularity::render::RenderFrameOutcome::Failed);
        setTerminalFailure(NativeVulkanWorkerFailure::InvariantViolation,
                "Vulkan frame source returned an invalid result");
        return true;
    }

    const auto outcome = result.frame.outcome;
    finishNativeRenderProfileFrame(profile, mIdentity,
            mContentKind == WorkerContentKind::Microscope, outcome);
    bool surfaceLost = false;
    bool recoverable = false;
    bool presentedCounterExhausted = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStatus.lastFrameOutcome = static_cast<std::uint32_t>(outcome);
        mStatus.flags |= kWorkerHasFrameOutcome;
        switch (outcome) {
            case cellularity::render::RenderFrameOutcome::Presented:
                if (mStatus.presentedFrames
                        == std::numeric_limits<std::uint64_t>::max()) {
                    presentedCounterExhausted = true;
                } else {
                    ++mStatus.presentedFrames;
                }
                mRetryStep = 0;
                mRetryDeadline = {};
                break;
            case cellularity::render::RenderFrameOutcome::Deferred:
            case cellularity::render::RenderFrameOutcome::SkippedNotReady:
                if (mDesired.pacingMode == FramePacingMode::OnDemand
                        && !mDesired.frameRequested) {
                    mDesired.retryPending = true;
                    mRetryDeadline = std::chrono::steady_clock::now()
                            + nextRetryDelayLocked();
                }
                break;
            case cellularity::render::RenderFrameOutcome::SwapchainRecreated:
                ++mStatus.swapchainRecreations;
                if (mDesired.pacingMode == FramePacingMode::OnDemand
                        && !mDesired.frameRequested) {
                    mDesired.retryPending = true;
                    mRetryDeadline = std::chrono::steady_clock::now()
                            + nextRetryDelayLocked();
                }
                break;
            case cellularity::render::RenderFrameOutcome::SurfaceLost:
                mDesired.frameRequested = true;
                mDesired.retryPending = false;
                mSurfaceLost = true;
                surfaceLost = true;
                break;
            case cellularity::render::RenderFrameOutcome::Failed:
                ++mStatus.failedFrameAttempts;
                mDesired.frameRequested = false;
                mDesired.retryPending = false;
                recoverable = true;
                break;
        }
        pacing = mDesired.pacingMode;
        (void)pacing;
        refreshStatusLocked();
    }
    if (surfaceLost) {
        detachAppliedSurface(SurfaceDetachReason::SurfaceLost, true);
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mDesired.surfacePresent = false;
            refreshStatusLocked();
        }
        emitStatusSignal(kWorkerActionSurfaceReattachRequired);
    } else if (recoverable) {
        setRecoverableFailure(NativeVulkanWorkerFailure::FrameFailed,
                "Vulkan frame transaction failed");
    } else if (presentedCounterExhausted) {
        setTerminalFailure(NativeVulkanWorkerFailure::InvariantViolation,
                "Vulkan presented-frame counter is exhausted");
    }
    return true;
}

bool NativeVulkanRenderWorker::processDiagnosticsRequest() {
    std::uint64_t requestId = 0;
    {
        std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
        if (mExplicitDiagnosticsState != ExplicitDiagnosticsState::Pending) {
            return false;
        }
        requestId = mExplicitDiagnosticsRequestId;
        mExplicitDiagnosticsState = ExplicitDiagnosticsState::Collecting;
    }

    std::string report;
    const bool built = buildDiagnosticsReport(
            NativeVulkanWorkerFailure::None, nullptr, false, report);
    {
        std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
        if (mExplicitDiagnosticsRequestId != requestId
                || mExplicitDiagnosticsState
                        != ExplicitDiagnosticsState::Collecting) {
            return true;
        }
        if (mExplicitDiagnosticsCancellationRequested
                || !mDiagnosticsAccepting.load(std::memory_order_relaxed)) {
            mLastRetiredDiagnosticsRequestId = requestId;
            mExplicitDiagnosticsRequestId = 0;
            mExplicitDiagnosticsRevision = 0;
            mExplicitDiagnostics.clear();
            mExplicitDiagnosticsCancellationRequested = false;
            mExplicitDiagnosticsState = ExplicitDiagnosticsState::Empty;
            return true;
        }
        if (!built || mNextDiagnosticsRevision == 0
                || mNextDiagnosticsRevision
                        == std::numeric_limits<std::uint64_t>::max()) {
            mExplicitDiagnostics.clear();
            mExplicitDiagnosticsState = ExplicitDiagnosticsState::Failed;
            return true;
        }
        mExplicitDiagnostics = std::move(report);
        mExplicitDiagnosticsRevision = mNextDiagnosticsRevision++;
        mExplicitDiagnosticsState = ExplicitDiagnosticsState::Completed;
    }
    return true;
}

bool NativeVulkanRenderWorker::buildDiagnosticsReport(
        NativeVulkanWorkerFailure failure, const char* detail,
        bool failed, std::string& destination) noexcept {
    std::uint64_t backendRevision = 0;
    bool backendSnapshotActive = false;
    try {
        std::string report = "schema=1\nstatus=";
        report += failed ? "failed\n" : "ready\n";
        report += "worker_id=" + std::to_string(mIdentity) + "\n";
        report += "failure_code="
                + std::to_string(static_cast<std::int32_t>(failure)) + "\n";
        report += "error=";
        if (detail != nullptr)
            report += detail;
        report += "\n";

        cellularity::platform::
                CurrentThreadSchedulingNormalizationResult scheduling;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            scheduling = mThreadSchedulingNormalization;
        }
        appendSchedulingDiagnostics(report, scheduling);

        if (mSession == nullptr || !mSession->ownerThread()) {
            report += "backend_diagnostics_status=unavailable\n";
            destination = std::move(report);
            return true;
        }

        std::uint64_t backendBytes = 0;
        const auto sizeStatus = mSession->getDiagnosticsSize(
                backendRevision, backendBytes);
        if (sizeStatus != CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1
                || backendRevision == 0) {
            report += "backend_diagnostics_status="
                    + std::to_string(sizeStatus) + "\n";
            destination = std::move(report);
            return true;
        }
        backendSnapshotActive = true;
        const std::size_t prefixSize = report.size();
        report += "backend_report:\n";
        const std::size_t payloadOffset = report.size();
        if (backendBytes > std::numeric_limits<std::size_t>::max()
                || backendBytes > report.max_size() - payloadOffset) {
            const auto release =
                    mSession->releaseDiagnosticsSnapshot(backendRevision);
            backendSnapshotActive = false;
            report.resize(prefixSize);
            report += "backend_diagnostics_status=oversized\n";
            report += "backend_diagnostics_release_status="
                    + std::to_string(release) + "\n";
            destination = std::move(report);
            return true;
        }
        report.resize(payloadOffset + static_cast<std::size_t>(backendBytes));
        std::uint64_t offset = 0;
        CellularityVulkanDiagnosticsStatusV1 readStatus =
                CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
        while (offset < backendBytes) {
            const std::uint64_t remaining = backendBytes - offset;
            const std::uint32_t capacity = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(remaining,
                            CELLULARITY_VULKAN_DIAGNOSTICS_CHUNK_BYTES_V1));
            std::uint32_t written = 0;
            readStatus = mSession->readDiagnosticsChunk(backendRevision,
                    offset,
                    reinterpret_cast<std::uint8_t*>(report.data()
                            + payloadOffset
                            + static_cast<std::size_t>(offset)),
                    capacity, written);
            if (readStatus != CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1
                    || written == 0 || written > capacity) {
                break;
            }
            offset += written;
        }
        const auto releaseStatus =
                mSession->releaseDiagnosticsSnapshot(backendRevision);
        backendSnapshotActive = false;
        if (readStatus != CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1
                || offset != backendBytes
                || releaseStatus != CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1) {
            report.resize(prefixSize);
            report += "backend_diagnostics_status="
                    + std::to_string(readStatus) + "\n";
            report += "backend_diagnostics_release_status="
                    + std::to_string(releaseStatus) + "\n";
        } else if (report.empty() || report.back() != '\n') {
            report += '\n';
        }
        destination = std::move(report);
        return true;
    } catch (...) {
        if (backendSnapshotActive && mSession != nullptr) {
            (void)mSession->releaseDiagnosticsSnapshot(backendRevision);
        }
        return false;
    }
}

std::uint64_t NativeVulkanRenderWorker::publishFailureDiagnostics(
        NativeVulkanWorkerFailure failure, const char* detail,
        bool terminal) noexcept {
    std::string report;
    if (!buildDiagnosticsReport(failure, detail, true, report)) {
        std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
        mFailureDiagnostics.clear();
        mFailureDiagnosticsRevision = 0;
        return 0;
    }
    std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
    std::uint64_t revision = 0;
    if (terminal) {
        revision = std::numeric_limits<std::uint64_t>::max();
    } else if (mNextDiagnosticsRevision != 0
            && mNextDiagnosticsRevision
                    != std::numeric_limits<std::uint64_t>::max()) {
        revision = mNextDiagnosticsRevision++;
    }
    if (revision == 0) {
        mFailureDiagnostics.clear();
        mFailureDiagnosticsRevision = 0;
        return 0;
    }
    mFailureDiagnostics = std::move(report);
    mFailureDiagnosticsRevision = revision;
    return revision;
}

void NativeVulkanRenderWorker::cancelPendingDiagnostics() noexcept {
    std::lock_guard<std::mutex> lock(mDiagnosticsMutex);
    if (mExplicitDiagnosticsState == ExplicitDiagnosticsState::Collecting) {
        mExplicitDiagnosticsCancellationRequested = true;
        return;
    }
    if (mExplicitDiagnosticsState != ExplicitDiagnosticsState::Pending)
        return;
    mLastRetiredDiagnosticsRequestId = mExplicitDiagnosticsRequestId;
    mExplicitDiagnosticsRequestId = 0;
    mExplicitDiagnosticsRevision = 0;
    mExplicitDiagnostics.clear();
    mExplicitDiagnosticsCancellationRequested = false;
    mExplicitDiagnosticsState = ExplicitDiagnosticsState::Empty;
}

bool NativeVulkanRenderWorker::applyProfilingConfiguration() {
    const std::uint64_t target = nativeRenderProfilingConfiguration();
    if (target == mAppliedProfilingConfiguration)
        return true;
    const bool oldEnabled =
            (mAppliedProfilingConfiguration & kRenderProfilingEnabledBit) != 0;
    const bool newEnabled = (target & kRenderProfilingEnabledBit) != 0;
    const bool generationChanged =
            (mAppliedProfilingConfiguration & kRenderProfilingGenerationMask)
            != (target & kRenderProfilingGenerationMask);

    bool controlTerminal = false;
    const auto apply = [&](bool enabled) {
        const auto status = mSession->setProfilingEnabled(enabled);
        if (status == CELLULARITY_VULKAN_OPERATION_OK_V1)
            return true;
        if (status == CELLULARITY_VULKAN_OPERATION_UNSUPPORTED_V1
                || status == CELLULARITY_VULKAN_OPERATION_FAILED_V1) {
            mGpuProfilingUnavailable = true;
            return false;
        }
        setTerminalFailure(
                NativeVulkanWorkerFailure::BackendControlFailed,
                "Vulkan backend rejected profiling configuration");
        controlTerminal = true;
        return false;
    };

    if (generationChanged && oldEnabled) {
        if (!apply(false)) {
            if (!controlTerminal)
                mAppliedProfilingConfiguration = target;
            return !controlTerminal;
        }
    } else if (!generationChanged && oldEnabled && !newEnabled) {
        if (!apply(false)) {
            if (!controlTerminal)
                mAppliedProfilingConfiguration = target;
            return !controlTerminal;
        }
    }
    if (newEnabled && (!oldEnabled || generationChanged)) {
        if (!apply(true)) {
            if (!controlTerminal)
                mAppliedProfilingConfiguration = target;
            return !controlTerminal;
        }
    }
    mAppliedProfilingConfiguration = target;
    return true;
}

void NativeVulkanRenderWorker::detachAppliedSurface(
        SurfaceDetachReason reason,
        bool backendAlreadyLost) noexcept {
    bool notifySource = false;
    bool detachBackend = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        notifySource = mApplied.sourceSurfaceApplied;
        detachBackend = mApplied.physicalSurfaceAttached && !backendAlreadyLost;
    }
    SourceSurfaceResult sourceResult = SourceSurfaceResult::Applied;
    if (notifySource) {
        sourceResult = mSource->onSurfaceDetached(reason);
    }
    CellularityVulkanOperationStatusV1 backendResult =
            CELLULARITY_VULKAN_OPERATION_OK_V1;
    if (detachBackend && mSession != nullptr) {
        backendResult = mSession->detachSurface();
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mApplied.physicalSurfaceAttached = false;
        mApplied.surfaceGeneration = 0;
        mApplied.width = 0;
        mApplied.height = 0;
        invalidateFrameOutcomeLocked();
        if (sourceResult == SourceSurfaceResult::Applied
                || sourceResult == SourceSurfaceResult::Closing) {
            mApplied.sourceSurfaceApplied = false;
            mSourceDetachPending = false;
        } else if (sourceResult == SourceSurfaceResult::DeferredRebind) {
            mSourceDetachPending = true;
            mPendingSourceDetachReason = reason;
        }
        refreshStatusLocked();
    }
    if (sourceResult == SourceSurfaceResult::Failed) {
        setTerminalFailure(NativeVulkanWorkerFailure::SourceSurfaceFailed,
                "Vulkan frame source detach failed");
    } else if (sourceResult == SourceSurfaceResult::Closing) {
        requestStop();
    }
    if (!operationSucceeded(backendResult)) {
        setTerminalFailure(NativeVulkanWorkerFailure::BackendControlFailed,
                "Vulkan backend surface detach failed");
    }
}

void NativeVulkanRenderWorker::teardownOnOwnerThread() noexcept {
    mDiagnosticsAccepting.store(false, std::memory_order_release);
    cancelPendingDiagnostics();
    detachAppliedSurface(SurfaceDetachReason::Stop);
    while (true) {
        std::uint64_t observedMutation = 0;
        SurfaceDetachReason reason = SurfaceDetachReason::Stop;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (!mSourceDetachPending)
                break;
            observedMutation = mMutationSerial;
            reason = mPendingSourceDetachReason;
        }
        const SourceSurfaceResult result =
                mSource->onSurfaceDetached(reason);
        if (result == SourceSurfaceResult::DeferredRebind) {
            std::unique_lock<std::mutex> lock(mMutex);
            if (mMutationSerial != observedMutation)
                continue;
            mWake.wait(lock, [&] {
                return mMutationSerial != observedMutation;
            });
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mSourceDetachPending = false;
            mApplied.sourceSurfaceApplied = false;
            refreshStatusLocked();
        }
        if (result == SourceSurfaceResult::Failed) {
            setTerminalFailure(NativeVulkanWorkerFailure::SourceSurfaceFailed,
                    "Vulkan frame source detach failed during shutdown");
        }
        break;
    }
    if (mSourceBound && mSource != nullptr) {
        while (true) {
            std::uint64_t observedMutation = 0;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                observedMutation = mMutationSerial;
            }
            const auto result = mSource->unbindBackend();
            if (result != SourceSurfaceResult::DeferredRebind) {
                if (result == SourceSurfaceResult::Failed) {
                    setTerminalFailure(
                            NativeVulkanWorkerFailure::SourceSurfaceFailed,
                            "Vulkan frame source unbind failed");
                }
                break;
            }
            std::unique_lock<std::mutex> lock(mMutex);
            if (mMutationSerial != observedMutation)
                continue;
            mWake.wait(lock, [&] {
                return mMutationSerial != observedMutation;
            });
        }
        mSourceBound = false;
    }
    if (mSession != nullptr) {
        mSession->closeOnOwnerThread();
        mSession.reset();
    }
    discardNativeRenderCameraInput(mIdentity);
    std::lock_guard<std::mutex> lock(mMutex);
    mDesired.pendingSurface.reset();
    mDesired.surfacePresent = false;
    mRecoverySurfacePending = false;
    mDesired.frameRequested = false;
    mDesired.retryPending = false;
    mSourceFrameDeferred = false;
    mApplied = {};
    refreshStatusLocked();
}

void NativeVulkanRenderWorker::setRecoverableFailure(
        NativeVulkanWorkerFailure failure,
        const char* detail) noexcept {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mTerminalFailure || mStopRequested)
            return;
    }
    const std::uint64_t diagnosticsRevision =
            publishFailureDiagnostics(failure, detail, false);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mTerminalFailure || mStopRequested)
            return;
        mRecoverableFailure = true;
        mRecoverySurfacePending = failure
                        == NativeVulkanWorkerFailure::FrameFailed
                && (mRecoverySurfacePending
                        || static_cast<bool>(mDesired.pendingSurface)
                        || mDesired.surfaceIntentRevision
                                != mApplied.surfaceIntentRevision);
        mStatus.failure = failure;
        mDesired.frameRequested = false;
        mDesired.retryPending = false;
        mStatus.diagnosticsRevision = diagnosticsRevision;
        mFailureDiagnosticsUnavailable = diagnosticsRevision == 0;
        refreshStatusLocked();
    }
    emitStatusSignal(kWorkerActionRecoverableFailure);
}

void NativeVulkanRenderWorker::setTerminalFailure(
        NativeVulkanWorkerFailure failure,
        const char* detail) noexcept {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mTerminalFailure)
            return;
    }
    mDiagnosticsAccepting.store(false, std::memory_order_release);
    cancelPendingDiagnostics();
    const std::uint64_t diagnosticsRevision =
            publishFailureDiagnostics(failure, detail, true);
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mTerminalFailure) {
            mTerminalFailure = true;
            mRecoverableFailure = false;
            mStatus.failure = failure;
            mDesired.frameRequested = false;
            mDesired.retryPending = false;
            mStatus.diagnosticsRevision = diagnosticsRevision;
            mFailureDiagnosticsUnavailable = diagnosticsRevision == 0;
            first = true;
        }
        ++mMutationSerial;
        refreshStatusLocked();
        mWake.notify_all();
    }
    if (first)
        emitStatusSignal(kWorkerActionTerminalFailure);
}

void NativeVulkanRenderWorker::setFailureReportLocked(
        NativeVulkanWorkerFailure failure,
        const char* detail) noexcept {
    try {
        std::string report = "schema=1\nstatus=failed\nfailure_code="
                + std::to_string(static_cast<std::int32_t>(failure))
                + "\nerror=" + (detail == nullptr ? "" : detail) + "\n";
        appendSchedulingDiagnostics(
                report, mThreadSchedulingNormalization);
        std::lock_guard<std::mutex> diagnosticsLock(mDiagnosticsMutex);
        std::uint64_t revision = 0;
        if (mTerminalFailure) {
            revision = std::numeric_limits<std::uint64_t>::max();
        } else if (mNextDiagnosticsRevision != 0
                && mNextDiagnosticsRevision
                        != std::numeric_limits<std::uint64_t>::max()) {
            revision = mNextDiagnosticsRevision++;
        }
        if (revision == 0) {
            mFailureDiagnosticsUnavailable = true;
            mFailureDiagnostics.clear();
            mFailureDiagnosticsRevision = 0;
            mStatus.diagnosticsRevision = 0;
            return;
        }
        mFailureDiagnostics = std::move(report);
        mFailureDiagnosticsRevision = revision;
        mStatus.diagnosticsRevision = revision;
        mFailureDiagnosticsUnavailable = false;
    } catch (...) {
        mFailureDiagnosticsUnavailable = true;
        mStatus.diagnosticsRevision = 0;
    }
}

void NativeVulkanRenderWorker::invalidateFrameOutcomeLocked() noexcept {
    mStatus.flags &= ~kWorkerHasFrameOutcome;
    mStatus.lastFrameOutcome = 0;
}

void NativeVulkanRenderWorker::refreshStatusLocked() noexcept {
    NativeVulkanWorkerStatus next = mStatus;
    if (mTerminalFailure) {
        next.phase = NativeVulkanWorkerPhase::TerminalFailure;
    } else if (mThreadExited) {
        next.phase = NativeVulkanWorkerPhase::Stopped;
    } else if (mStopRequested) {
        next.phase = NativeVulkanWorkerPhase::StopRequested;
    } else if (mRecoverableFailure) {
        next.phase = NativeVulkanWorkerPhase::RecoverableFailure;
    } else if (!mStarted && !mStarting) {
        next.phase = NativeVulkanWorkerPhase::Dormant;
    } else if (mStarting) {
        next.phase = NativeVulkanWorkerPhase::Starting;
    } else if (!mDesired.resumed) {
        next.phase = NativeVulkanWorkerPhase::Paused;
    } else if (mSurfaceLost) {
        next.phase = NativeVulkanWorkerPhase::SurfaceLost;
    } else if (!mApplied.physicalSurfaceAttached
            || !mApplied.sourceSurfaceApplied
            || !mDesired.surfacePresent
            || mApplied.surfaceIntentRevision
                    != mDesired.surfaceIntentRevision) {
        next.phase = NativeVulkanWorkerPhase::WaitingForSurface;
    } else {
        next.phase = NativeVulkanWorkerPhase::Ready;
    }
    next.flags &= kWorkerHasFrameOutcome;
    if (mDesired.resumed)
        next.flags |= kWorkerDesiredResumed;
    if (mDesired.surfacePresent)
        next.flags |= kWorkerDesiredSurfacePresent;
    if (mApplied.physicalSurfaceAttached) {
        next.flags |= kWorkerPhysicalSurfaceAttached;
    }
    if (mApplied.sourceSurfaceApplied) {
        next.flags |= kWorkerSourceSurfaceApplied;
    }
    if (mDesired.frameRequested)
        next.flags |= kWorkerFrameRequestPending;
    if (mDesired.retryPending)
        next.flags |= kWorkerPacedRetryPending;
    if (mFailureDiagnosticsUnavailable) {
        next.flags |= kWorkerFailureDiagnosticsUnavailable;
    }
    if (mStatusSignalDeliveryFailed) {
        next.flags |= kWorkerStatusSignalDeliveryFailed;
    }
    next.desiredSurfaceGeneration = mDesired.surfaceGeneration;
    next.appliedSurfaceGeneration = mApplied.physicalSurfaceAttached
            ? mApplied.surfaceGeneration
            : 0;
    next.desiredWidth = static_cast<std::uint32_t>(
            std::max(mDesired.width, 0));
    next.desiredHeight = static_cast<std::uint32_t>(
            std::max(mDesired.height, 0));
    if (statusObservableEqual(next, mStatus))
        return;
    if (mStatus.revision == std::numeric_limits<std::uint64_t>::max()) {
        return;
    }
    if (mStatus.revision
            == std::numeric_limits<std::uint64_t>::max() - 1U) {
        next.revision = std::numeric_limits<std::uint64_t>::max();
        next.phase = NativeVulkanWorkerPhase::TerminalFailure;
        next.failure = NativeVulkanWorkerFailure::InvariantViolation;
        mTerminalFailure = true;
        mDiagnosticsAccepting.store(false, std::memory_order_release);
    } else {
        next.revision = mStatus.revision + 1U;
    }
    mStatus = next;
}

void NativeVulkanRenderWorker::emitStatusSignal(
        std::uint32_t actionBits) noexcept {
    constexpr std::uint32_t kKnownActions =
            kWorkerActionSurfaceReattachRequired
            | kWorkerActionTerminalFailure
            | kWorkerActionRecoverableFailure;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mStatusSignalDisabled || mStatusSignal == nullptr
                || mStopRequested) {
            return;
        }
        const std::uint32_t known = actionBits & kKnownActions;
        if (known == 0 || known != actionBits || mStatus.revision == 0) {
            mStatusSignalDeliveryFailed = true;
            mStatusSignalDisabled = true;
            if (!mTerminalFailure) {
                mTerminalFailure = true;
                mDiagnosticsAccepting.store(false, std::memory_order_release);
                mRecoverableFailure = false;
                mStatus.failure = NativeVulkanWorkerFailure::InvariantViolation;
                setFailureReportLocked(mStatus.failure,
                        "Native Vulkan worker produced an invalid status signal");
            }
            ++mMutationSerial;
            refreshStatusLocked();
            mWake.notify_all();
            return;
        }
        mPendingStatusSignalActions |= known;
        mPendingStatusSignalRevision = std::max(
                mPendingStatusSignalRevision, mStatus.revision);
        if (mStatusSignalFailureCount == 0) {
            mStatusSignalRetryDeadline = {};
        }
        ++mMutationSerial;
        mWake.notify_all();
    }
    deliverPendingStatusSignalIfDue(true);
}

void NativeVulkanRenderWorker::deliverPendingStatusSignalIfDue(
        bool force) noexcept {
    std::shared_ptr<NativeVulkanWorkerStatusSignal> signal;
    std::uint64_t revision = 0;
    std::uint32_t actions = 0;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto now = std::chrono::steady_clock::now();
        if (mStatusSignalDisabled || mStatusSignal == nullptr
                || mStopRequested || mStatusSignalDeliveryInProgress
                || mPendingStatusSignalActions == 0
                || (!force
                        && mStatusSignalRetryDeadline.time_since_epoch().count()
                                != 0
                        && now < mStatusSignalRetryDeadline)) {
            return;
        }
        mStatusSignalDeliveryInProgress = true;
        signal = mStatusSignal;
        revision = mPendingStatusSignalRevision;
        actions = mPendingStatusSignalActions;
    }

    const StatusSignalDelivery delivery = signal->deliver(revision, actions);

    std::lock_guard<std::mutex> lock(mMutex);
    mStatusSignalDeliveryInProgress = false;
    if (mStopRequested) {
        mPendingStatusSignalActions = 0;
        mPendingStatusSignalRevision = 0;
        mStatusSignalRetryDeadline = {};
        return;
    }
    switch (delivery) {
        case StatusSignalDelivery::Accepted:
            mStatusSignalFailureCount = 0;
            mStatusSignalRetryDeadline = {};
            if (mPendingStatusSignalRevision == revision) {
                mPendingStatusSignalActions &= ~actions;
                if (mPendingStatusSignalActions == 0) {
                    mPendingStatusSignalRevision = 0;
                }
            }
            break;
        case StatusSignalDelivery::ListenerClosed:
            mPendingStatusSignalActions = 0;
            mPendingStatusSignalRevision = 0;
            mStatusSignalFailureCount = 0;
            mStatusSignalRetryDeadline = {};
            mStatusSignalDisabled = true;
            break;
        case StatusSignalDelivery::RetryableFailure:
            ++mStatusSignalFailureCount;
            if (mStatusSignalFailureCount == 1) {
                mStatusSignalRetryDeadline =
                        std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(25);
                break;
            }
            [[fallthrough]];
        case StatusSignalDelivery::InvalidSignal: {
            mPendingStatusSignalActions = 0;
            mPendingStatusSignalRevision = 0;
            mStatusSignalRetryDeadline = {};
            mStatusSignalDisabled = true;
            mStatusSignalDeliveryFailed = true;
            if (!mTerminalFailure) {
                mTerminalFailure = true;
                mDiagnosticsAccepting.store(false, std::memory_order_release);
                mRecoverableFailure = false;
                mStatus.failure = delivery == StatusSignalDelivery::InvalidSignal
                        ? NativeVulkanWorkerFailure::InvariantViolation
                        : NativeVulkanWorkerFailure::StatusSignalTransportFailed;
                setFailureReportLocked(mStatus.failure,
                        "Vulkan worker status listener rejected a signal");
            }
            break;
        }
    }
    ++mMutationSerial;
    refreshStatusLocked();
    mWake.notify_all();
}

std::chrono::steady_clock::time_point
NativeVulkanRenderWorker::nextWakeDeadlineLocked() const noexcept {
    const bool frameDeadline =
            mRetryDeadline.time_since_epoch().count() != 0;
    const bool signalDeadline = mPendingStatusSignalActions != 0
            && mStatusSignalRetryDeadline.time_since_epoch().count() != 0;
    if (frameDeadline && signalDeadline) {
        return std::min(mRetryDeadline, mStatusSignalRetryDeadline);
    }
    if (frameDeadline)
        return mRetryDeadline;
    if (signalDeadline)
        return mStatusSignalRetryDeadline;
    return {};
}

bool NativeVulkanRenderWorker::incrementSurfaceIntentLocked() noexcept {
    if (mDesired.surfaceIntentRevision
            == std::numeric_limits<std::uint64_t>::max()) {
        mTerminalFailure = true;
        mDiagnosticsAccepting.store(false, std::memory_order_release);
        mStatus.failure = NativeVulkanWorkerFailure::InvariantViolation;
        setFailureReportLocked(mStatus.failure,
                "Vulkan surface intent revision is exhausted");
        ++mMutationSerial;
        refreshStatusLocked();
        mWake.notify_all();
        return false;
    }
    ++mDesired.surfaceIntentRevision;
    return mDesired.surfaceIntentRevision != 0;
}

bool NativeVulkanRenderWorker::closingLocked() const noexcept {
    return mStopRequested || mTerminalFailure || mThreadExited;
}

bool NativeVulkanRenderWorker::runnableFrameLocked() const noexcept {
    return mDesired.frameRequested && !closingLocked()
            && !mRecoverableFailure && !mSourceFrameDeferred
            && mDesired.resumed
            && mDesired.surfacePresent
            && mApplied.physicalSurfaceAttached
            && mApplied.sourceSurfaceApplied
            && mDesired.width > 0 && mDesired.height > 0;
}

std::chrono::milliseconds
NativeVulkanRenderWorker::nextRetryDelayLocked() noexcept {
    static constexpr std::array<std::int64_t, 4> kDelays{8, 16, 32, 64};
    const std::size_t index = std::min<std::size_t>(
            mRetryStep, kDelays.size() - 1U);
    if (mRetryStep < kDelays.size() - 1U)
        ++mRetryStep;
    return std::chrono::milliseconds(kDelays[index]);
}

void NativeVulkanRenderWorker::joinIfExited() noexcept {
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mJoined)
            return;
        if (!mThreadExited)
            return;
        if (mThread.joinable()) {
            thread = std::move(mThread);
        }
        mJoined = true;
    }
    if (thread.joinable())
        thread.join();
}

void NativeVulkanRenderWorker::notifyRebindReady() noexcept {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mThreadExited)
        return;
    mSourceFrameDeferred = false;
    mRetryDeadline = {};
    ++mMutationSerial;
    mWake.notify_all();
}

} // namespace cellularity::app::render_worker
