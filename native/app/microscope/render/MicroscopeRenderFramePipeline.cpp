#include "app/microscope/render/MicroscopeRenderFramePipeline.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "core/SimulationWorld.h"
#include "platform/CurrentThreadName.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/frame/RenderTimelineClock.h"

namespace cellularity::microscope {
namespace {

using SourceExchange = render::RenderSourceFrameExchange;
using PreparedExchange = render::PreparedRenderFrameExchange;

[[noreturn]] void failInvariant(const char* message) {
    throw std::logic_error(message);
}

const char* failureKindName(RenderPreparationFailureKind kind) noexcept {
    switch (kind) {
        case RenderPreparationFailureKind::None:
            return "none";
        case RenderPreparationFailureKind::Allocation:
            return "allocation";
        case RenderPreparationFailureKind::StandardException:
            return "standard-exception";
        case RenderPreparationFailureKind::UnknownException:
            return "unknown-exception";
    }
    return "invalid";
}

const char* failureStageName(RenderPreparationFailureStage stage) noexcept {
    switch (stage) {
        case RenderPreparationFailureStage::None:
            return "none";
        case RenderPreparationFailureStage::WorkerInitialization:
            return "worker-initialization";
        case RenderPreparationFailureStage::PreparedSlotAcquisition:
            return "prepared-slot-acquisition";
        case RenderPreparationFailureStage::FrameBuild:
            return "frame-build";
        case RenderPreparationFailureStage::FramePublication:
            return "frame-publication";
    }
    return "invalid";
}

std::uint64_t saturatingAdd(std::uint64_t left, std::uint64_t right) noexcept {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

bool boundsAreFiniteAndOrdered(
        const render::RenderViewBounds& bounds) noexcept {
    return std::isfinite(bounds.minX) && std::isfinite(bounds.maxX) && std::isfinite(bounds.minY) && std::isfinite(bounds.maxY) && bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY;
}

bool fullyCovers(const render::RenderViewBounds& coverage,
        const render::RenderViewBounds& visible) noexcept {
    if (!boundsAreFiniteAndOrdered(coverage) || !boundsAreFiniteAndOrdered(visible)) {
        return false;
    }
    const float scale = std::max(
            {1.0f, std::abs(coverage.minX), std::abs(coverage.maxX),
                    std::abs(coverage.minY), std::abs(coverage.maxY), std::abs(visible.minX),
                    std::abs(visible.maxX), std::abs(visible.minY), std::abs(visible.maxY)});
    const float tolerance = scale * 8.0f * std::numeric_limits<float>::epsilon();
    return coverage.minX <= visible.minX + tolerance && coverage.maxX + tolerance >= visible.maxX && coverage.minY <= visible.minY + tolerance && coverage.maxY + tolerance >= visible.maxY;
}

} // namespace

struct MicroscopeRenderFramePipeline::State final {
    struct CaptureSubmission final {
        int visibleCellCount = 0;
        std::uint64_t sourceSequence = 0;
        bool accepted = false;
    };

    explicit State(int maxCellCount) : configuredMaxCellCount(maxCellCount) {
        for (auto& value : sourceActiveStorageBytes) {
            value.store(0, std::memory_order_relaxed);
        }
        for (auto& value : sourceRetainedStorageBytes) {
            value.store(0, std::memory_order_relaxed);
        }
        for (auto& value : preparedActiveStorageBytes) {
            value.store(0, std::memory_order_relaxed);
        }
        for (auto& value : preparedRetainedStorageBytes) {
            value.store(0, std::memory_order_relaxed);
        }

        sourceWrite = sourceExchange.producer().tryBeginWrite();
        if (!sourceWrite) {
            failInvariant("Microscope source exchange has no initial writable slot");
        }
        sourceCapture.emplace(maxCellCount, sourceWrite.frame());
        recordSourceStorage(sourceWrite);

        worker = std::thread([this] {
            workerMain();
        });
        std::unique_lock<std::mutex> lock(workerMutex);
        workerStateChanged.wait(lock, [this] {
            return workerStartupComplete;
        });
        // Worker initialization failures are retained as a controlled state.
        // Construction succeeds so rendering can continue with clear-only output
        // and the application can report the failure through diagnostics.
    }

    ~State() {
        stopAndJoin();
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    const int configuredMaxCellCount;
    SourceExchange sourceExchange;
    PreparedExchange preparedExchange;
    std::optional<render::RenderSourceCapture> sourceCapture;
    SourceExchange::WriteLease sourceWrite;
    PreparedExchange::ReadLease currentPrepared;

    std::atomic<int> visibleCellCount{0};

    std::array<std::atomic<std::uint64_t>, render::kRenderFrameExchangeSlotCount>
            sourceActiveStorageBytes{};
    std::array<std::atomic<std::uint64_t>, render::kRenderFrameExchangeSlotCount>
            sourceRetainedStorageBytes{};
    std::array<std::atomic<std::uint64_t>, render::kRenderFrameExchangeSlotCount>
            preparedActiveStorageBytes{};
    std::array<std::atomic<std::uint64_t>, render::kRenderFrameExchangeSlotCount>
            preparedRetainedStorageBytes{};

    std::atomic<bool> acceptingCaptures{true};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> stopInitiated{false};
    std::atomic<bool> workerRunning{false};
    std::atomic<bool> workerIdle{false};
    std::atomic<bool> workerBuilding{false};
    std::atomic<bool> workerPausedForTesting{false};
    std::atomic<bool> workerPausedAfterBuildForTesting{false};
    std::atomic<bool> hasAcceptedSource{false};
    std::atomic<bool> hasExpectedModeGeneration{false};
    std::atomic<std::uint64_t> latestAcceptedWorldEpoch{0};
    std::atomic<std::uint64_t> latestAcceptedModeGeneration{0};
    std::atomic<std::uint64_t> latestAcceptedSourceSequence{0};
    std::atomic<std::uint64_t> latestPublishedSourceSequence{0};
    std::atomic<std::uint64_t> latestPreparedSequence{0};
    std::atomic<std::uint64_t> supersededBuildCount{0};
    std::atomic<std::uint64_t> rejectedInputRollbackCount{0};
    std::atomic<std::uint64_t> rejectedModeGenerationCount{0};
    std::atomic<std::uint64_t> coverageMissCount{0};
    std::atomic<bool> urgentCapturePending{false};
    std::atomic<std::uint64_t> urgentCaptureViewportRevision{0};
    std::atomic<std::uint64_t> urgentCaptureRequestCount{0};
    std::atomic<std::uint64_t> urgentCaptureCoalescedCount{0};
    std::atomic<std::uint64_t> urgentCaptureConsumedCount{0};
    std::atomic<RenderPresentationOutcome> presentationOutcome{
            RenderPresentationOutcome::WaitingForFirstCompleteFrame
    };
    std::atomic<RenderPreparationFailureKind> failNextPreparationKindForTesting{
            RenderPreparationFailureKind::None
    };

    // Failure context is written first and published by failureKind with a
    // release store. Readers acquire failureKind before sampling the context.
    std::atomic<RenderPreparationFailureKind> failureKind{
            RenderPreparationFailureKind::None
    };
    std::atomic<RenderPreparationFailureStage> failureStage{
            RenderPreparationFailureStage::None
    };
    std::atomic<std::uint64_t> failureWorldEpoch{0};
    std::atomic<std::uint64_t> failureSourceSequence{0};

    std::mutex workerMutex;
    std::condition_variable workerWake;
    std::condition_variable workerStateChanged;
    std::uint64_t sourcePublicationGeneration = 0;
    bool workerStartupComplete = false;
    bool pauseRequestedForTesting = false;
    bool pauseAfterBuildRequestedForTesting = false;

    std::mutex completionMutex;
    std::condition_variable preparationCompleted;

    // Only deterministic test and benchmark submissions wait here. Production
    // capture remains a single non-blocking admission attempt and drops an
    // incoming source when every fixed slot is momentarily unavailable.
    std::mutex sourceAvailabilityMutex;
    std::condition_variable sourceSlotAvailable;
    std::atomic<std::uint64_t> sourceAvailabilityGeneration{0};

    render::RenderPresentationState latestPresentation{};
    bool presentationValid = false;
    std::uint64_t lastPresentedWorldEpoch = 0;
    std::uint64_t lastPresentedModeGeneration = 0;
    std::uint64_t lastPresentedFrameSequence = 0;
    std::thread worker;

    void recordSourceStorage(SourceExchange::WriteLease& lease) noexcept {
        const std::size_t slot = lease.slotIndex();
        sourceActiveStorageBytes[slot].store(lease.frame().activeStorageBytes(),
                std::memory_order_relaxed);
        sourceRetainedStorageBytes[slot].store(lease.frame().retainedStorageBytes(),
                std::memory_order_relaxed);
    }

    void recordPreparedStorage(PreparedExchange::WriteLease& lease) noexcept {
        const std::size_t slot = lease.slotIndex();
        preparedActiveStorageBytes[slot].store(lease.frame().activeStorageBytes(),
                std::memory_order_relaxed);
        preparedRetainedStorageBytes[slot].store(
                lease.frame().retainedStorageBytes(), std::memory_order_relaxed);
    }

    [[nodiscard]] RenderPreparationFailure failureSnapshot() const noexcept {
        RenderPreparationFailure result;
        result.kind = failureKind.load(std::memory_order_acquire);
        if (result.kind == RenderPreparationFailureKind::None) {
            return result;
        }
        result.stage = failureStage.load(std::memory_order_relaxed);
        result.worldEpoch = failureWorldEpoch.load(std::memory_order_relaxed);
        result.sourceSequence =
                failureSourceSequence.load(std::memory_order_relaxed);
        return result;
    }

    [[noreturn]] void
    throwWorkerFailure(const RenderPreparationFailure& workerFailure) const {
        throw std::runtime_error(
                std::string("Microscope preparation worker failed: kind=") + failureKindName(workerFailure.kind) + ", stage=" + failureStageName(workerFailure.stage) + ", epoch=" + std::to_string(workerFailure.worldEpoch) + ", sourceSequence=" + std::to_string(workerFailure.sourceSequence));
    }

    void throwIfWorkerFailed() const {
        const RenderPreparationFailure currentFailure = failureSnapshot();
        if (currentFailure) {
            throwWorkerFailure(currentFailure);
        }
    }

    void recordWorkerFailure(RenderPreparationFailureKind kind,
            RenderPreparationFailureStage stage,
            std::uint64_t worldEpoch,
            std::uint64_t sourceSequence) noexcept {
        if (failureKind.load(std::memory_order_relaxed) != RenderPreparationFailureKind::None) {
            return;
        }
        failureStage.store(stage, std::memory_order_relaxed);
        failureWorldEpoch.store(worldEpoch, std::memory_order_relaxed);
        failureSourceSequence.store(sourceSequence, std::memory_order_relaxed);
        failureKind.store(kind, std::memory_order_release);
        acceptingCaptures.store(false, std::memory_order_release);
        {
            const std::lock_guard<std::mutex> lock(completionMutex);
            (void)lock;
        }
        preparationCompleted.notify_all();
        sourceSlotAvailable.notify_all();
    }

    void signalSourceSlotAvailable() noexcept {
        sourceAvailabilityGeneration.fetch_add(1, std::memory_order_release);
        sourceSlotAvailable.notify_all();
    }

    static RenderPreparationFailureKind currentFailureKind() noexcept {
        try {
            throw;
        } catch (const std::bad_alloc&) {
            return RenderPreparationFailureKind::Allocation;
        } catch (const std::exception&) {
            return RenderPreparationFailureKind::StandardException;
        } catch (...) {
            return RenderPreparationFailureKind::UnknownException;
        }
    }

    void throwInjectedPreparationFailureForTesting() {
        const RenderPreparationFailureKind injected =
                failNextPreparationKindForTesting.exchange(
                        RenderPreparationFailureKind::None, std::memory_order_acq_rel);
        switch (injected) {
            case RenderPreparationFailureKind::None:
                return;
            case RenderPreparationFailureKind::Allocation:
                throw std::bad_alloc();
            case RenderPreparationFailureKind::StandardException:
                throw std::runtime_error("Injected render preparation failure");
            case RenderPreparationFailureKind::UnknownException:
                throw 1;
        }
    }

    void signalWorkerStartup() noexcept {
        {
            const std::lock_guard<std::mutex> lock(workerMutex);
            workerStartupComplete = true;
        }
        workerStateChanged.notify_all();
    }

    void pauseAfterBuildIfRequestedForTesting() {
        std::unique_lock<std::mutex> lock(workerMutex);
        if (!pauseAfterBuildRequestedForTesting) {
            return;
        }
        workerPausedAfterBuildForTesting.store(true, std::memory_order_release);
        workerStateChanged.notify_all();
        workerWake.wait(lock, [this] {
            return stopRequested.load(std::memory_order_acquire) || !pauseAfterBuildRequestedForTesting;
        });
        workerPausedAfterBuildForTesting.store(false, std::memory_order_release);
        workerStateChanged.notify_all();
    }

    void workerMain() noexcept {
        cellularity::platform::setCurrentThreadName("CellularityPrep");
        PreparedExchange::WriteLease preparedWrite;
        std::optional<render::RenderFrameBuilder> frameBuilder;
        try {
            preparedWrite = preparedExchange.producer().tryBeginWrite();
            if (!preparedWrite) {
                failInvariant(
                        "Microscope prepared exchange has no initial writable slot");
            }
            frameBuilder.emplace(configuredMaxCellCount, preparedWrite.frame());
            recordPreparedStorage(preparedWrite);
        } catch (...) {
            recordWorkerFailure(currentFailureKind(),
                    RenderPreparationFailureStage::WorkerInitialization,
                    0, 0);
            signalWorkerStartup();
            return;
        }

        workerRunning.store(true, std::memory_order_release);
        signalWorkerStartup();

        std::uint64_t observedPublicationGeneration = 0;
        while (!stopRequested.load(std::memory_order_acquire)) {
            {
                std::unique_lock<std::mutex> lock(workerMutex);
                workerIdle.store(true, std::memory_order_release);
                for (;;) {
                    if (stopRequested.load(std::memory_order_acquire)) {
                        break;
                    }
                    if (pauseRequestedForTesting) {
                        workerPausedForTesting.store(true, std::memory_order_release);
                        workerStateChanged.notify_all();
                        workerWake.wait(lock, [this] {
                            return stopRequested.load(std::memory_order_acquire) || !pauseRequestedForTesting;
                        });
                        workerPausedForTesting.store(false, std::memory_order_release);
                        workerStateChanged.notify_all();
                        continue;
                    }
                    if (sourcePublicationGeneration != observedPublicationGeneration) {
                        observedPublicationGeneration = sourcePublicationGeneration;
                        break;
                    }
                    workerWake.wait(lock, [this, observedPublicationGeneration] {
                        return stopRequested.load(std::memory_order_acquire) || pauseRequestedForTesting || sourcePublicationGeneration != observedPublicationGeneration;
                    });
                }
                workerIdle.store(false, std::memory_order_release);
                if (stopRequested.load(std::memory_order_acquire)) {
                    break;
                }
            }

            const std::uint64_t requestedEpoch =
                    latestAcceptedWorldEpoch.load(std::memory_order_acquire);
            SourceExchange::ReadLease readableSource =
                    sourceExchange.consumer().tryAcquireNewest(requestedEpoch);
            if (!readableSource) {
                continue;
            }
            // Acquiring newest may also have discarded older publications. Wake a
            // compatibility producer that could have observed the transition before
            // those slots became FREE.
            signalSourceSlotAvailable();

            const std::uint64_t sourceSequence =
                    readableSource.frame().captureSequence;
            const std::uint64_t sourceEpoch =
                    readableSource.frame().capture.worldEpoch;
            const std::uint64_t sourceModeGeneration =
                    readableSource.frame().capture.modeGeneration;
            if (sourceSequence != latestAcceptedSourceSequence.load(std::memory_order_acquire) || sourceEpoch != latestAcceptedWorldEpoch.load(std::memory_order_acquire) || (hasExpectedModeGeneration.load(std::memory_order_acquire) && sourceModeGeneration != latestAcceptedModeGeneration.load(std::memory_order_acquire))) {
                readableSource.release();
                signalSourceSlotAvailable();
                continue;
            }

            workerBuilding.store(true, std::memory_order_release);
            RenderPreparationFailureStage activeStage =
                    RenderPreparationFailureStage::PreparedSlotAcquisition;
            try {
                while (!preparedWrite && !stopRequested.load(std::memory_order_acquire)) {
                    preparedWrite = preparedExchange.producer().tryBeginWrite();
                    if (!preparedWrite) {
                        // The GL consumer can briefly own both its retained frame and the
                        // candidate it is replacing. This transition is bounded inside
                        // acquireNewest(); retry without treating it as a worker failure.
                        std::this_thread::yield();
                    }
                }
                if (!preparedWrite) {
                    readableSource.release();
                    signalSourceSlotAvailable();
                    workerBuilding.store(false, std::memory_order_release);
                    continue;
                }

                activeStage = RenderPreparationFailureStage::FrameBuild;
                throwInjectedPreparationFailureForTesting();
                frameBuilder->build(readableSource.frame(), preparedWrite.frame());
                recordPreparedStorage(preparedWrite);
                pauseAfterBuildIfRequestedForTesting();

                // A newer source with the same identity does not invalidate work that
                // is already complete. Publishing it gives the GL consumer continuous
                // geometry while the next loop immediately coalesces to the newest
                // source. Epoch and mode transitions remain hard barriers.
                const bool identityInvalidated =
                        sourceEpoch != latestAcceptedWorldEpoch.load(std::memory_order_acquire) || (hasExpectedModeGeneration.load(std::memory_order_acquire) && sourceModeGeneration != latestAcceptedModeGeneration.load(std::memory_order_acquire));
                if (stopRequested.load(std::memory_order_acquire) || identityInvalidated) {
                    preparedWrite.abandon();
                    readableSource.release();
                    signalSourceSlotAvailable();
                    if (identityInvalidated) {
                        supersededBuildCount.fetch_add(1, std::memory_order_relaxed);
                    }
                    workerBuilding.store(false, std::memory_order_release);
                    continue;
                }

                // Geometry owns every value it needs. Release the source slot before
                // publishing completion so a blocking caller cannot wake while the
                // worker still retains source storage.
                readableSource.release();
                signalSourceSlotAvailable();
                const std::uint64_t preparedSequence =
                        preparedWrite.frame().identity.captureSequence;
                // This is the final payload mutation. RenderFrameExchange publishes it
                // with a release transition and exposes only const READING leases.
                preparedWrite.frame().valid = true;
                activeStage = RenderPreparationFailureStage::FramePublication;
                if (!preparedWrite.publish()) {
                    failInvariant(
                            "Microscope preparation worker rejected a complete frame");
                }

                {
                    const std::lock_guard<std::mutex> lock(completionMutex);
                    latestPreparedSequence.store(preparedSequence,
                            std::memory_order_relaxed);
                    latestPublishedSourceSequence.store(sourceSequence,
                            std::memory_order_release);
                }
                preparationCompleted.notify_all();
            } catch (...) {
                readableSource.release();
                signalSourceSlotAvailable();
                if (preparedWrite) {
                    recordPreparedStorage(preparedWrite);
                    preparedWrite.abandon();
                }
                workerBuilding.store(false, std::memory_order_release);
                recordWorkerFailure(currentFailureKind(), activeStage, sourceEpoch,
                        sourceSequence);
                break;
            }
            workerBuilding.store(false, std::memory_order_release);
        }

        if (preparedWrite) {
            preparedWrite.abandon();
        }
        workerPausedForTesting.store(false, std::memory_order_release);
        workerPausedAfterBuildForTesting.store(false, std::memory_order_release);
        workerIdle.store(false, std::memory_order_release);
        workerBuilding.store(false, std::memory_order_release);
        workerRunning.store(false, std::memory_order_release);
        workerStateChanged.notify_all();
        preparationCompleted.notify_all();
    }

    SourceExchange::WriteLease beginSourceWrite(bool waitForWritableSlot) {
        SourceExchange::WriteLease result = std::move(sourceWrite);
        while (!result) {
            const std::uint64_t observedGeneration =
                    sourceAvailabilityGeneration.load(std::memory_order_acquire);
            result = sourceExchange.producer().tryBeginWrite();
            if (result || !waitForWritableSlot) {
                return result;
            }

            std::unique_lock<std::mutex> lock(sourceAvailabilityMutex);
            sourceSlotAvailable.wait(lock, [this, observedGeneration] {
                return sourceAvailabilityGeneration.load(std::memory_order_acquire) != observedGeneration || static_cast<bool>(failureSnapshot()) || stopRequested.load(std::memory_order_acquire);
            });
            lock.unlock();
            throwIfWorkerFailed();
            if (stopRequested.load(std::memory_order_acquire)) {
                failInvariant("Microscope frame pipeline stopped before capture");
            }
        }
        return result;
    }

    CaptureSubmission
    captureImpl(const SimulationWorld& world,
            const render::RenderViewBounds& viewBounds,
            render::RenderFrameCaptureMetadata captureMetadata,
            const render::RenderMarkerCaptureRequest* markerRequests,
            std::size_t markerRequestCount, bool waitForWritableSlot) {
        const RenderPreparationFailure existingFailure = failureSnapshot();
        if (existingFailure) {
            if (waitForWritableSlot) {
                throwWorkerFailure(existingFailure);
            }
            return {visibleCellCount.load(std::memory_order_acquire), 0, false};
        }
        if (!acceptingCaptures.load(std::memory_order_acquire)) {
            failInvariant("Microscope frame pipeline is stopping");
        }
        if (hasExpectedModeGeneration.load(std::memory_order_acquire)) {
            const std::uint64_t expectedModeGeneration =
                    latestAcceptedModeGeneration.load(std::memory_order_acquire);
            if (captureMetadata.modeGeneration < expectedModeGeneration) {
                rejectedModeGenerationCount.fetch_add(1, std::memory_order_relaxed);
                return {visibleCellCount.load(std::memory_order_acquire), 0, false};
            }
            if (captureMetadata.modeGeneration > expectedModeGeneration) {
                latestAcceptedModeGeneration.store(captureMetadata.modeGeneration,
                        std::memory_order_release);
            }
        }
        if (captureMetadata.timeline.captureRequestedNanos == 0) {
            captureMetadata.timeline.captureRequestedNanos =
                    render::renderTimelineNowNanos();
        }

        SourceExchange::WriteLease writableSource =
                beginSourceWrite(waitForWritableSlot);
        if (!writableSource) {
            return {visibleCellCount.load(std::memory_order_acquire), 0, false};
        }

        int capturedVisibleCount = 0;
        try {
            capturedVisibleCount = sourceCapture->capture(
                    world, viewBounds, captureMetadata, writableSource.frame(),
                    markerRequests, markerRequestCount);
        } catch (...) {
            recordSourceStorage(writableSource);
            writableSource.abandon();
            throw;
        }

        const std::uint64_t sourceSequence = writableSource.frame().captureSequence;
        const std::uint64_t worldEpoch = writableSource.frame().capture.worldEpoch;
        const std::uint64_t modeGeneration =
                writableSource.frame().capture.modeGeneration;
        recordSourceStorage(writableSource);

        if (!acceptingCaptures.load(std::memory_order_acquire)) {
            writableSource.abandon();
            const RenderPreparationFailure captureFailure = failureSnapshot();
            if (captureFailure && !waitForWritableSlot) {
                return {visibleCellCount.load(std::memory_order_acquire), 0, false};
            }
            throwIfWorkerFailed();
            failInvariant("Microscope frame pipeline stopped during capture");
        }

        // Commit the acceptance watermark before the release-publication. A
        // worker that is still awake because of an earlier source may otherwise
        // acquire this complete slot in the narrow interval between publish() and
        // the watermark update, mistake it for stale work, and free the only copy.
        // If it observes the watermark before this slot becomes PUBLISHED, the
        // generation notification below makes it retry after publication.
        latestAcceptedWorldEpoch.store(worldEpoch, std::memory_order_release);
        latestAcceptedModeGeneration.store(modeGeneration,
                std::memory_order_release);
        hasExpectedModeGeneration.store(true, std::memory_order_release);
        latestAcceptedSourceSequence.store(sourceSequence,
                std::memory_order_release);
        hasAcceptedSource.store(true, std::memory_order_release);
        if (!writableSource.publish()) {
            writableSource.abandon();
            failInvariant("Microscope source exchange rejected a complete frame");
        }

        visibleCellCount.store(capturedVisibleCount, std::memory_order_release);
        {
            // The worker never holds this mutex while acquiring/building a frame.
            // The short hand-off lock prevents a condition-variable lost wake and
            // cannot couple capture latency to worker or GL work.
            const std::lock_guard<std::mutex> lock(workerMutex);
            ++sourcePublicationGeneration;
        }
        workerWake.notify_one();
        return {capturedVisibleCount, sourceSequence, true};
    }

    void waitUntilSourcePrepared(std::uint64_t sourceSequence) {
        std::unique_lock<std::mutex> lock(completionMutex);
        preparationCompleted.wait(lock, [this, sourceSequence] {
            return latestPublishedSourceSequence.load(std::memory_order_acquire) >= sourceSequence || static_cast<bool>(failureSnapshot()) || stopRequested.load(std::memory_order_acquire);
        });
        lock.unlock();
        throwIfWorkerFailed();
        if (latestPublishedSourceSequence.load(std::memory_order_acquire) < sourceSequence) {
            failInvariant("Microscope preparation stopped before frame completion");
        }
    }

    bool waitUntilSourcePrepared(std::uint64_t sourceSequence,
            std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(completionMutex);
        const bool completed =
                preparationCompleted.wait_for(lock, timeout, [this, sourceSequence] {
                    return latestPublishedSourceSequence.load(
                                   std::memory_order_acquire)
                            >= sourceSequence
                            || static_cast<bool>(failureSnapshot()) || stopRequested.load(std::memory_order_acquire);
                });
        return completed && !static_cast<bool>(failureSnapshot()) && latestPublishedSourceSequence.load(std::memory_order_acquire) >= sourceSequence;
    }

    [[nodiscard]] bool matchesExpectedIdentity(
            const render::PreparedRenderFrame& frame) const noexcept {
        if (hasAcceptedSource.load(std::memory_order_acquire) && frame.identity.worldEpoch != latestAcceptedWorldEpoch.load(std::memory_order_acquire)) {
            return false;
        }
        return !hasExpectedModeGeneration.load(std::memory_order_acquire) || frame.identity.modeGeneration == latestAcceptedModeGeneration.load(std::memory_order_acquire);
    }

    void requestUrgentCapture(std::uint64_t viewportRevision) noexcept {
        std::uint64_t recordedRevision =
                urgentCaptureViewportRevision.load(std::memory_order_relaxed);
        while (recordedRevision < viewportRevision && !urgentCaptureViewportRevision.compare_exchange_weak(recordedRevision, viewportRevision, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
        if (urgentCapturePending.exchange(true, std::memory_order_release)) {
            urgentCaptureCoalescedCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            urgentCaptureRequestCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const render::PreparedRenderFrame* acquireLatestPrepared(
            bool throwOnWorkerFailure = true,
            const render::RenderViewBounds* requiredCoverage = nullptr) {
        if (throwOnWorkerFailure) {
            throwIfWorkerFailed();
        }
        if (!hasAcceptedSource.load(std::memory_order_acquire)) {
            if (currentPrepared && !matchesExpectedIdentity(currentPrepared.frame())) {
                currentPrepared.release();
            }
            return currentPrepared ? &currentPrepared.frame() : nullptr;
        }

        const std::uint64_t expectedEpoch =
                latestAcceptedWorldEpoch.load(std::memory_order_acquire);
        if (currentPrepared && !matchesExpectedIdentity(currentPrepared.frame())) {
            currentPrepared.release();
        }

        PreparedExchange::ReadLease newest =
                preparedExchange.consumer().tryAcquireNewest(expectedEpoch);
        if (newest) {
            if (!matchesExpectedIdentity(newest.frame())) {
                rejectedModeGenerationCount.fetch_add(1, std::memory_order_relaxed);
                newest.release();
                return currentPrepared ? &currentPrepared.frame() : nullptr;
            }
            bool rollsBack = false;
            if (currentPrepared) {
                const render::PreparedRenderFrame& current = currentPrepared.frame();
                const render::PreparedRenderFrame& candidate = newest.frame();
                rollsBack = candidate.identity.captureSequence <= current.identity.captureSequence || candidate.resolvedInput.lastResolvedInputSequence < current.resolvedInput.lastResolvedInputSequence || candidate.resolvedInput.appliedWorldRevision < current.resolvedInput.appliedWorldRevision;
            }
            if (rollsBack) {
                rejectedInputRollbackCount.fetch_add(1, std::memory_order_relaxed);
                newest.release();
            } else {
                const bool currentCovers =
                        currentPrepared && requiredCoverage != nullptr && fullyCovers(currentPrepared.frame().viewport.coverageBounds, *requiredCoverage);
                const bool newestCovers =
                        requiredCoverage == nullptr || fullyCovers(newest.frame().viewport.coverageBounds, *requiredCoverage);
                if (currentCovers && !newestCovers) {
                    // A newer capture for a different camera must not evict the only
                    // complete frame that can represent the current viewport.
                    newest.release();
                } else {
                    currentPrepared = std::move(newest);
                }
            }
        }
        return currentPrepared ? &currentPrepared.frame() : nullptr;
    }

    const render::PreparedRenderFrame*
    presentLatest(const render::RenderPresentationState& presentation) {
        if (!presentationValid || presentation.viewport.revision >= latestPresentation.viewport.revision) {
            latestPresentation = presentation;
            presentationValid = true;
        }

        const RenderPreparationFailure workerFailure = failureSnapshot();
        const render::RenderViewBounds& visibleBounds =
                latestPresentation.viewport.visibleBounds;
        const render::PreparedRenderFrame* frame =
                acquireLatestPrepared(false, &visibleBounds);
        if (frame == nullptr) {
            const bool identityBarrier =
                    hasAcceptedSource.load(std::memory_order_acquire) || hasExpectedModeGeneration.load(std::memory_order_acquire);
            presentationOutcome.store(
                    workerFailure
                            ? RenderPresentationOutcome::FailureClearOnly
                            : (identityBarrier
                                              ? RenderPresentationOutcome::IdentityBarrierClearOnly
                                              : RenderPresentationOutcome::WaitingForFirstCompleteFrame),
                    std::memory_order_release);
            return nullptr;
        }

        if (!fullyCovers(frame->viewport.coverageBounds, visibleBounds)) {
            coverageMissCount.fetch_add(1, std::memory_order_relaxed);
            if (!workerFailure) {
                requestUrgentCapture(latestPresentation.viewport.revision);
            }
            presentationOutcome.store(
                    workerFailure
                            ? RenderPresentationOutcome::FailureRetainedCompleteFrame
                            : RenderPresentationOutcome::CoverageMissRetainedCompleteFrame,
                    std::memory_order_release);
            return frame;
        }

        const bool repeated =
                lastPresentedWorldEpoch == frame->identity.worldEpoch && lastPresentedModeGeneration == frame->identity.modeGeneration && lastPresentedFrameSequence == frame->identity.captureSequence;
        lastPresentedWorldEpoch = frame->identity.worldEpoch;
        lastPresentedModeGeneration = frame->identity.modeGeneration;
        lastPresentedFrameSequence = frame->identity.captureSequence;
        urgentCapturePending.store(false, std::memory_order_release);
        presentationOutcome.store(
                workerFailure
                        ? RenderPresentationOutcome::FailureRetainedCompleteFrame
                        : (repeated
                                          ? RenderPresentationOutcome::PresentedRetainedCompleteFrame
                                          : RenderPresentationOutcome::PresentedNewCompleteFrame),
                std::memory_order_release);
        return frame;
    }

    bool setPausedForTesting(bool paused, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(workerMutex);
        pauseRequestedForTesting = paused;
        workerWake.notify_all();
        return workerStateChanged.wait_for(lock, timeout, [this, paused] {
            return !workerRunning.load(std::memory_order_acquire) || workerPausedForTesting.load(std::memory_order_acquire) == paused;
        });
    }

    void setPauseAfterBuildForTesting(bool paused) {
        {
            const std::lock_guard<std::mutex> lock(workerMutex);
            pauseAfterBuildRequestedForTesting = paused;
        }
        workerWake.notify_all();
    }

    bool
    waitUntilPausedAfterBuildForTesting(std::chrono::milliseconds timeout) const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (workerPausedAfterBuildForTesting.load(std::memory_order_acquire)) {
                return true;
            }
            if (!workerRunning.load(std::memory_order_acquire)) {
                return false;
            }
            std::this_thread::yield();
        }
        return workerPausedAfterBuildForTesting.load(std::memory_order_acquire);
    }

    void stopAndJoin() noexcept {
        if (stopInitiated.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        acceptingCaptures.store(false, std::memory_order_release);
        {
            const std::lock_guard<std::mutex> lock(workerMutex);
            stopRequested.store(true, std::memory_order_release);
        }
        workerWake.notify_all();
        {
            const std::lock_guard<std::mutex> lock(completionMutex);
            (void)lock;
        }
        preparationCompleted.notify_all();
        sourceSlotAvailable.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
    }
};

MicroscopeRenderFramePipeline::MicroscopeRenderFramePipeline(int maxCellCount)
        : mState(std::make_unique<State>(maxCellCount)) {}

MicroscopeRenderFramePipeline::~MicroscopeRenderFramePipeline() = default;

void MicroscopeRenderFramePipeline::reset(int maxCellCount) {
    std::unique_ptr<State> previous = std::move(mState);
    previous->stopAndJoin();
    previous.reset();
    mState = std::make_unique<State>(maxCellCount);
}

void MicroscopeRenderFramePipeline::invalidateWorldEpoch(
        std::uint64_t worldEpoch) {
    if (worldEpoch == 0) {
        throw std::invalid_argument("Microscope world epoch must be non-zero");
    }
    mState->latestAcceptedWorldEpoch.store(worldEpoch, std::memory_order_release);
    mState->hasAcceptedSource.store(true, std::memory_order_release);
}

void MicroscopeRenderFramePipeline::invalidateModeGeneration(
        std::uint64_t modeGeneration) {
    if (modeGeneration == 0) {
        throw std::invalid_argument("Microscope mode generation must be non-zero");
    }
    if (mState->hasExpectedModeGeneration.load(std::memory_order_acquire) && modeGeneration < mState->latestAcceptedModeGeneration.load(std::memory_order_acquire)) {
        mState->rejectedModeGenerationCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    mState->latestAcceptedModeGeneration.store(modeGeneration,
            std::memory_order_release);
    mState->hasExpectedModeGeneration.store(true, std::memory_order_release);
}

int MicroscopeRenderFramePipeline::capture(
        const SimulationWorld& world, const ViewBounds& viewBounds,
        const CaptureMetadata& captureMetadata,
        const MarkerCaptureRequest* markerRequests,
        std::size_t markerRequestCount) {
    return mState
            ->captureImpl(world, viewBounds, captureMetadata, markerRequests,
                    markerRequestCount, false)
            .visibleCellCount;
}

int MicroscopeRenderFramePipeline::captureAndPrepare(
        const SimulationWorld& world, const ViewBounds& viewBounds,
        const CaptureMetadata& captureMetadata,
        const MarkerCaptureRequest* markerRequests,
        std::size_t markerRequestCount) {
    const State::CaptureSubmission submission =
            mState->captureImpl(world, viewBounds, captureMetadata, markerRequests,
                    markerRequestCount, true);
    if (submission.accepted) {
        mState->waitUntilSourcePrepared(submission.sourceSequence);
    }
    return submission.visibleCellCount;
}

const MicroscopeRenderFramePipeline::PreparedFrame*
MicroscopeRenderFramePipeline::acquireLatestPreparedFrame() {
    return mState->acquireLatestPrepared();
}

const MicroscopeRenderFramePipeline::PreparedFrame*
MicroscopeRenderFramePipeline::presentLatestPreparedFrame(
        const PresentationState& presentation) {
    if (presentation.viewport.surfaceWidth < 0 || presentation.viewport.surfaceHeight < 0) {
        throw std::invalid_argument(
                "Microscope frame received a negative presentation surface size");
    }
    return mState->presentLatest(presentation);
}

const MicroscopeRenderFramePipeline::PreparedFrame*
MicroscopeRenderFramePipeline::preparedFrame() const noexcept {
    return mState->currentPrepared ? &mState->currentPrepared.frame() : nullptr;
}

const MicroscopeRenderFramePipeline::PresentationState&
MicroscopeRenderFramePipeline::latestPresentation() const {
    if (!mState->presentationValid) {
        failInvariant("Microscope frame pipeline has no presentation snapshot");
    }
    return mState->latestPresentation;
}

bool MicroscopeRenderFramePipeline::hasLatestPresentation() const noexcept {
    return mState->presentationValid;
}

RenderPreparationFailure
MicroscopeRenderFramePipeline::failure() const noexcept {
    return mState->failureSnapshot();
}

bool MicroscopeRenderFramePipeline::consumeUrgentCaptureRequest() noexcept {
    if (!mState->urgentCapturePending.exchange(false,
                std::memory_order_acq_rel)) {
        return false;
    }
    mState->urgentCaptureConsumedCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

int MicroscopeRenderFramePipeline::visibleCellCount() const noexcept {
    return mState->visibleCellCount.load(std::memory_order_acquire);
}

MicroscopeRenderFrameExchangeTelemetry
MicroscopeRenderFramePipeline::exchangeTelemetry() const noexcept {
    return {mState->sourceExchange.counters(),
            mState->preparedExchange.counters()};
}

MicroscopeRenderFramePipelineSnapshot
MicroscopeRenderFramePipeline::snapshotWhenQuiescent() const noexcept {
    MicroscopeRenderFramePipelineSnapshot snapshot;
    snapshot.sourceStorage = mState->sourceExchange.stateSnapshot();
    snapshot.preparedStorage = mState->preparedExchange.stateSnapshot();
    for (std::size_t slot = 0; slot < render::kRenderFrameExchangeSlotCount;
            ++slot) {
        auto& sourceSlot = snapshot.sourceStorage.slots[slot];
        sourceSlot.activeStorageBytes =
                mState->sourceActiveStorageBytes[slot].load(std::memory_order_relaxed);
        sourceSlot.retainedStorageBytes =
                mState->sourceRetainedStorageBytes[slot].load(
                        std::memory_order_relaxed);
        snapshot.sourceStorage.totalActiveStorageBytes =
                saturatingAdd(snapshot.sourceStorage.totalActiveStorageBytes,
                        sourceSlot.activeStorageBytes);
        snapshot.sourceStorage.totalRetainedStorageBytes =
                saturatingAdd(snapshot.sourceStorage.totalRetainedStorageBytes,
                        sourceSlot.retainedStorageBytes);

        auto& preparedSlot = snapshot.preparedStorage.slots[slot];
        preparedSlot.activeStorageBytes =
                mState->preparedActiveStorageBytes[slot].load(
                        std::memory_order_relaxed);
        preparedSlot.retainedStorageBytes =
                mState->preparedRetainedStorageBytes[slot].load(
                        std::memory_order_relaxed);
        snapshot.preparedStorage.totalActiveStorageBytes =
                saturatingAdd(snapshot.preparedStorage.totalActiveStorageBytes,
                        preparedSlot.activeStorageBytes);
        snapshot.preparedStorage.totalRetainedStorageBytes =
                saturatingAdd(snapshot.preparedStorage.totalRetainedStorageBytes,
                        preparedSlot.retainedStorageBytes);

        snapshot.hasPendingPreparedFrame =
                snapshot.hasPendingPreparedFrame || preparedSlot.state == render::RenderFrameSlotState::Published;
    }

    snapshot.sourceCounters = mState->sourceExchange.counters();
    snapshot.preparedCounters = mState->preparedExchange.counters();
    snapshot.droppedFrames = render::captureRenderFrameExchangeDropProfile(
            mState->sourceExchange, mState->preparedExchange);
    snapshot.hasPendingPreparedFrame =
            snapshot.hasPendingPreparedFrame || mState->workerBuilding.load(std::memory_order_acquire);
    snapshot.hasCurrentPreparedFrame = static_cast<bool>(mState->currentPrepared);
    snapshot.hasPresentation = mState->presentationValid;
    snapshot.workerRunning =
            mState->workerRunning.load(std::memory_order_acquire);
    snapshot.workerIdle = mState->workerIdle.load(std::memory_order_acquire);
    snapshot.workerBuilding =
            mState->workerBuilding.load(std::memory_order_acquire);
    snapshot.pendingPreparedSequence =
            mState->latestPreparedSequence.load(std::memory_order_acquire);
    snapshot.currentWorldEpoch =
            mState->latestAcceptedWorldEpoch.load(std::memory_order_acquire);
    snapshot.currentModeGeneration =
            mState->latestAcceptedModeGeneration.load(std::memory_order_acquire);
    if (mState->currentPrepared) {
        snapshot.currentPreparedSequence =
                mState->currentPrepared.frame().identity.captureSequence;
        snapshot.currentWorldEpoch =
                mState->currentPrepared.frame().identity.worldEpoch;
        snapshot.currentPreparedModeGeneration =
                mState->currentPrepared.frame().identity.modeGeneration;
    }
    snapshot.latestAcceptedSourceSequence =
            mState->latestAcceptedSourceSequence.load(std::memory_order_acquire);
    snapshot.latestPublishedSourceSequence =
            mState->latestPublishedSourceSequence.load(std::memory_order_acquire);
    snapshot.supersededBuildCount =
            mState->supersededBuildCount.load(std::memory_order_relaxed);
    snapshot.rejectedInputRollbackCount =
            mState->rejectedInputRollbackCount.load(std::memory_order_relaxed);
    snapshot.rejectedModeGenerationCount =
            mState->rejectedModeGenerationCount.load(std::memory_order_relaxed);
    snapshot.coverageMissCount =
            mState->coverageMissCount.load(std::memory_order_relaxed);
    snapshot.urgentCaptureRequestCount =
            mState->urgentCaptureRequestCount.load(std::memory_order_relaxed);
    snapshot.urgentCaptureCoalescedCount =
            mState->urgentCaptureCoalescedCount.load(std::memory_order_relaxed);
    snapshot.urgentCaptureConsumedCount =
            mState->urgentCaptureConsumedCount.load(std::memory_order_relaxed);
    snapshot.urgentCaptureViewportRevision =
            mState->urgentCaptureViewportRevision.load(std::memory_order_relaxed);
    snapshot.urgentCapturePending =
            mState->urgentCapturePending.load(std::memory_order_acquire);
    snapshot.presentationOutcome =
            mState->presentationOutcome.load(std::memory_order_acquire);
    snapshot.failure = mState->failureSnapshot();
    return snapshot;
}

bool MicroscopeRenderFramePipeline::setPreparationPausedForTesting(
        bool paused, std::chrono::milliseconds timeout) {
    return mState->setPausedForTesting(paused, timeout);
}

void MicroscopeRenderFramePipeline::setPauseAfterBuildForTesting(bool paused) {
    mState->setPauseAfterBuildForTesting(paused);
}

bool MicroscopeRenderFramePipeline::waitUntilPausedAfterBuildForTesting(
        std::chrono::milliseconds timeout) const {
    return mState->waitUntilPausedAfterBuildForTesting(timeout);
}

bool MicroscopeRenderFramePipeline::waitUntilSourcePreparedForTesting(
        std::uint64_t sourceSequence, std::chrono::milliseconds timeout) {
    return mState->waitUntilSourcePrepared(sourceSequence, timeout);
}

void MicroscopeRenderFramePipeline::failNextPreparationForTesting(
        RenderPreparationFailureKind kind) {
    if (kind == RenderPreparationFailureKind::None) {
        throw std::invalid_argument(
                "Injected preparation failure kind must not be None");
    }
    mState->failNextPreparationKindForTesting.store(kind,
            std::memory_order_release);
}

} // namespace cellularity::microscope
