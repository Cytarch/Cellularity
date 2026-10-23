#include "app/microscope/NativeMicroscopeRuntime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "app/NativeInteractionController.h"
#include "app/NativeRenderSession.h"
#include "app/NativeWorldContext.h"
#include "app/NativeWorldEditing.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeFrameController.h"
#include "app/microscope/MicroscopeRuntimePublicationContract.h"
#include "app/microscope/NativeMicroscopeRequest.h"
#include "app/microscope/interaction/MicroscopeDragController.h"
#include "app/microscope/interaction/MicroscopeInteraction.h"
#include "app/microscope/interaction/MicroscopeSelectionController.h"
#include "app/microscope/render/MicroscopeRenderFramePipeline.h"
#include "core/SimulationWorld.h"
#include "core/EnvironmentBatch.h"
#include "core/GenomeMetadata.h"
#include "formats/CellSnapshotWireCodec.h"
#include "formats/GenomeDraftCodec.h"
#include "formats/SubstrateFileCodec.h"
#include "platform/CurrentThreadName.h"

namespace cellularity::microscope {
namespace {

using namespace std::chrono_literals;

constexpr auto kOwnerServiceBudget = 4ms;
constexpr auto kTargetLagLimit = 100ms;
constexpr auto kMaximumClockCheckInterval = 500us;
constexpr std::uint32_t kHardBatchUpdateLimit = 4096;
constexpr double kUpdateCostSafetyMargin = 1.25;
constexpr auto kCommandServiceBudget = 2ms;
constexpr std::uint32_t kHardCommandLimit = 128;
constexpr auto kIdleWakeInterval = 250ms;
constexpr std::int64_t kInertiaIntervalNanos = 1'000'000'000LL / 60;
constexpr auto kActualRateWindow = 500ms;
#ifdef CELLULARITY_PERFORMANCE_HUD
constexpr auto kStageTimingSampleInterval = 500ms;
#endif
constexpr int kDiagnosisToolOrdinal = 4;

std::int64_t timePointNanos(MicroscopeCommandTimePoint timePoint) noexcept {
    return timePoint.time_since_epoch().count();
}

std::uint64_t incrementRevision(std::uint64_t revision) noexcept {
    return revision == std::numeric_limits<std::uint64_t>::max() ? revision
                                                                 : revision + 1;
}

} // namespace

NativeMicroscopeRuntime::NativeMicroscopeRuntime(
        NativeSimulationOwner& owner, NativeInteractionController& interaction,
        NativeRenderSession& renderSession, float cameraBoundsRadius,
        const std::atomic<bool>& performanceHudEnabled)
        : mOwner(owner), mInteraction(interaction), mRenderSession(renderSession),
          mPerformanceHudEnabled(performanceHudEnabled),
          mOwnerViewport(std::make_unique<NativeViewport>(cameraBoundsRadius)) {}

NativeMicroscopeRuntime::~NativeMicroscopeRuntime() {
    stop();
}

void NativeMicroscopeRuntime::start() {
    if (mRunning.load(std::memory_order_acquire) || mThread.joinable()) {
        return;
    }
    mStopRequested.store(false, std::memory_order_release);
    mRenderSession.initializeMicroscopePipeline();
    {
        auto access = mOwner.write();
        access.microscopeSimulation().enableScheduler(
                simulation::SimulationSchedulerLimits(
                        kOwnerServiceBudget, kTargetLagLimit, kMaximumClockCheckInterval,
                        kHardBatchUpdateLimit, kUpdateCostSafetyMargin),
                simulation::simulationSchedulerNow());
    }
    mThread = std::thread([this] {
        run();
    });
}

void NativeMicroscopeRuntime::stop() noexcept {
    mStopRequested.store(true, std::memory_order_release);
    mInteraction.wakeOwner();
    if (mThread.joinable()) {
        mThread.join();
    }
}

bool NativeMicroscopeRuntime::running() const noexcept {
    return mRunning.load(std::memory_order_acquire);
}

NativeMicroscopeRuntimeSnapshot NativeMicroscopeRuntime::snapshot() const {
    const std::lock_guard<std::mutex> lock(mSnapshotConsumerMutex);
    NativeMicroscopeRuntimeSnapshot newest;
    if (mSnapshotExchange.tryConsumeNewest(newest)) {
        mConsumedSnapshot = std::move(newest);
    }
    return mConsumedSnapshot;
}

void NativeMicroscopeRuntime::run() noexcept {
    cellularity::platform::setCurrentThreadName("CellularitySim");
    mRunning.store(true, std::memory_order_release);
    bool executionContextBound = false;
    try {
        {
            auto access = mOwner.write();
            access.bindSimulationExecutionToCurrentThread();
            executionContextBound = true;
        }
        while (!mStopRequested.load(std::memory_order_acquire)) {
            runCycle();
            if (mStopRequested.load(std::memory_order_acquire) || !mWaitAfterCycle) {
                continue;
            }

            auto& mailbox = mInteraction.commandMailbox();
            const auto observed = mailbox.notificationGeneration();
            // Observe the generation before the full boundary scan. A producer that
            // races either operation then makes the wait predicate true; shutdown is
            // rechecked for the same reason. This prevents a notification from being
            // absorbed into the baseline immediately before sleeping.
            if (mStopRequested.load(std::memory_order_acquire) || mailbox.hasPendingOwnerWork()) {
                continue;
            }
            auto deadline = mNextWakeDeadline.value_or(microscopeCommandNow() + kIdleWakeInterval);
            if (mInteraction.viewportInertiaActive()) {
                const auto inertiaDeadline =
                        microscopeCommandNow() + MicroscopeCommandDuration{kInertiaIntervalNanos};
                deadline = std::min(deadline, inertiaDeadline);
            }
            (void)mailbox.waitForNotification(observed, deadline);
        }
    } catch (...) {
        publishFailureSnapshot();
    }
    if (executionContextBound) {
        // The pool is blocking-by-default, but stopping a simulation session still
        // joins every worker. A later start rebinds this same runtime-only context
        // and materializes a fresh pool on the new simulation thread if needed.
        try {
            auto access = mOwner.write();
            access.releaseSimulationExecutionFromCurrentThread();
        } catch (...) {
            publishFailureSnapshot();
        }
    }
    mRunning.store(false, std::memory_order_release);
}

void NativeMicroscopeRuntime::runCycle() {
    const std::int64_t nowNanos = timePointNanos(microscopeCommandNow());
    if (mInteraction.viewportInertiaActive() && (mLastInertiaStepNanos == 0 || nowNanos - mLastInertiaStepNanos >= kInertiaIntervalNanos)) {
        (void)mInteraction.stepViewportInertia();
        mLastInertiaStepNanos = nowNanos;
    }
    syncNewestViewport();

    auto access = mOwner.write();
#ifdef CELLULARITY_PERFORMANCE_HUD
    if (mPerformanceHudEnabled.load(std::memory_order_relaxed)) {
        if (mNextStageTimingSampleNanos == 0 || nowNanos >= mNextStageTimingSampleNanos) {
            access.world().requestStageTimingSample();
            mNextStageTimingSampleNanos =
                    nowNanos + std::chrono::duration_cast<MicroscopeCommandDuration>(kStageTimingSampleInterval).count();
        }
    } else {
        mNextStageTimingSampleNanos = 0;
    }
#endif
    auto& state = access.microscopeSimulation();
    auto* scheduler = state.scheduler();
    if (scheduler == nullptr) {
        throw std::logic_error("Microscope runtime requires a scheduler");
    }

    (void)serviceOwner(access, true);
    updateWorldInteraction(access);

    auto& mailbox = mInteraction.commandMailbox();
    // The full multi-channel scan belongs to this service boundary. Once the
    // batch starts, one acquire-load generation comparison is sufficient to
    // notice every completed producer publication and stop after the currently
    // active atomic update.
    const std::uint64_t interruptionGeneration = mailbox.notificationGeneration();
    const bool ownerWorkPending = mailbox.hasPendingOwnerWork();
    const auto batchContract = state.schedulerModeContract();
    const auto plan = scheduler->planNextBatch(
            simulation::simulationSchedulerNow(), ownerWorkPending);
    const auto execution = simulation::executeSimulationBatch(
            plan, [&] {
                executeScheduledUpdate(access, batchContract);
            },
            [&] {
                return mStopRequested.load(std::memory_order_acquire) || mailbox.hasNotificationSince(interruptionGeneration);
            },
            [] {
                return simulation::simulationSchedulerNow();
            });
    scheduler->recordObservedBatch(plan, execution);

    if (serviceOwner(access, false)) {
        updateWorldInteraction(access);
    }

    const auto contract = state.schedulerModeContract();
    if (!state.schedulerLifecyclePaused() && execution.completedUpdates > 0 && contract.sourceCaptureCadence == ScheduledSourceCaptureCadence::FixedInterval) {
        captureScheduledIncubateFrame(access, contract);
    }
    if (!state.schedulerLifecyclePaused() && contract.sourceCaptureCadence == ScheduledSourceCaptureCadence::DirtyOnly && mCaptureDirty) {
        capture(access, render::RenderCaptureStage::StableWorld, 0.0f);
        mUrgentCapture = false;
        mCaptureDirty = false;
    }

    publishStatus(access, false);
    mNextWakeDeadline = plan.wakeDeadline;
    // A productive TargetRate plan has no wake deadline: only the following
    // no-work plan can calculate the remaining-credit deadline. Replan
    // immediately after completed or service-interrupted work, and sleep only
    // when the scheduler explicitly reports that this cycle had no work.
    mWaitAfterCycle =
            execution.stopReason == simulation::SimulationBatchStopReason::NoWork;
}

void NativeMicroscopeRuntime::syncNewestViewport() {
    PresentationViewportSnapshot newest;
    if (!mInteraction.tryConsumeNewestOwnerViewport(newest)) {
        return;
    }
    mViewportSnapshot = newest;
    mOwnerViewport->setSurfaceSize(newest.viewport.surfaceWidth,
            newest.viewport.surfaceHeight);
    mOwnerViewport->setCamera(newest.viewport.cameraX, newest.viewport.cameraY,
            newest.viewport.zoom);
}

bool NativeMicroscopeRuntime::serviceOwner(
        NativeSimulationOwner::WriteAccess& access, bool beforeBatch) {
    recordOwnerServiceOpportunity(microscopeCommandNow());
    auto& mailbox = mInteraction.commandMailbox();
    const bool viewportCaptureRequested =
            mailbox.consumeViewportCaptureHint().has_value();
    if (viewportCaptureRequested) {
        syncNewestViewport();
        mCaptureDirty = true;
    }
    const bool urgentCaptureRequested =
            mRenderSession.consumeUrgentMicroscopeCaptureRequest();
    if (urgentCaptureRequested) {
        syncNewestViewport();
        mUrgentCapture = true;
        mCaptureDirty = true;
    }

    const auto serviceStarted = microscopeCommandNow();
    const auto result = access.serviceMicroscopeCommands(
            mailbox, serviceStarted + kCommandServiceBudget, kHardCommandLimit,
            [this](NativeSimulationOwner::WriteAccess& ownerAccess,
                    const MicroscopeWorldCommand& command) {
                return applyCommand(ownerAccess, command);
            },
            [] {
                return microscopeCommandNow();
            });
    mCaptureDirty =
            mCaptureDirty || result.changedWorld || result.urgentCaptureRequested;
    mUrgentCapture = mUrgentCapture || result.urgentCaptureRequested;
    mInteraction.synchronizePresentationInteraction(
            access.microscopeInteraction());

    if (beforeBatch && result.failed != 0) {
        throw std::runtime_error("Microscope owner command failed");
    }
    return viewportCaptureRequested || urgentCaptureRequested || result.processed != 0;
}

MicroscopeCommandApplyResult NativeMicroscopeRuntime::applyCommand(
        NativeSimulationOwner::WriteAccess& access,
        const MicroscopeWorldCommand& command) {
    SimulationWorld& world = access.world();
    NativeWorldContext context{world, *mOwnerViewport};
    auto& interaction = access.microscopeInteraction();
    auto& drag = interaction.drag();
    auto& selection = interaction.selection();
    auto& simulationState = access.microscopeSimulation();

    switch (command.kind) {
        case MicroscopeWorldCommandKind::TouchDown: {
            const bool changed =
                    drag.beginDragSlot(context, command.pointerSlot, command.pointerId,
                            command.worldX, command.worldY);
            return MicroscopeCommandApplyResult::applied(changed, changed);
        }
        case MicroscopeWorldCommandKind::TouchMovePosition:
        case MicroscopeWorldCommandKind::TouchMovePathSegment: {
            if (!drag.isDragging(command.pointerSlot) || drag.pointerId(command.pointerSlot) != command.pointerId) {
                return MicroscopeCommandApplyResult::applied();
            }
            const bool changed = drag.dragSlot(context, command.pointerSlot,
                    command.worldX, command.worldY, 0.0);
            if (changed) {
                drag.setWorldPosition(command.pointerSlot, command.worldX,
                        command.worldY);
            } else {
                drag.endDragSlot(context, command.pointerSlot);
            }
            return MicroscopeCommandApplyResult::applied(changed, changed);
        }
        case MicroscopeWorldCommandKind::TouchUp:
        case MicroscopeWorldCommandKind::TouchCancel: {
            const bool changed = drag.isDragging(command.pointerSlot);
            drag.endDragSlot(context, command.pointerSlot);
            return MicroscopeCommandApplyResult::applied(changed, changed);
        }
        case MicroscopeWorldCommandKind::Tap: {
            const bool hadSelection = world.mCellData.getIdFromSelectTag(1) != -1;
            const auto result = MicroscopeInteraction::applyMicroscopeTapAtWorld(
                    world, command.worldX, command.worldY, interaction.toolOrdinal(),
                    hadSelection,
                    command.genome != nullptr ? command.genome.get() : nullptr, false);
            selection.syncFromViewport(context);
            return MicroscopeCommandApplyResult::applied(result.changed,
                    result.changed);
        }
        case MicroscopeWorldCommandKind::SetTemperatureMode: {
            const bool changed = simulationState.setTemperatureModeAt(
                    command.integerValue, simulation::simulationSchedulerNow());
            if (changed) {
                for (int slot = 0; slot < MicroscopeDragController::MAX_POINTERS;
                        ++slot) {
                    drag.endDragSlot(context, slot);
                }
                mInteraction.setPresentationTemperatureMode(command.integerValue);
                mRenderSession.invalidateModeGeneration(simulationState.modeGeneration());
            }
            return MicroscopeCommandApplyResult::applied(false, changed);
        }
        case MicroscopeWorldCommandKind::SetToolOrdinal:
            interaction.setToolOrdinal(command.integerValue);
            return MicroscopeCommandApplyResult::applied();
        case MicroscopeWorldCommandKind::SetSlowMotionSpeed:
            return MicroscopeCommandApplyResult::applied(
                    false, simulationState.setSlowMotionSpeedAt(static_cast<float>(command.numericValue), simulation::simulationSchedulerNow()));
        case MicroscopeWorldCommandKind::SetOptionalTargetUps:
            if (command.hasNumericValue) {
                simulationState.setLegacyIncubateTargetUps(command.numericValue);
            }
            return MicroscopeCommandApplyResult::applied();
        case MicroscopeWorldCommandKind::LifecyclePause:
            simulationState.setSchedulerLifecyclePaused(
                    true, simulation::simulationSchedulerNow());
            return MicroscopeCommandApplyResult::applied();
        case MicroscopeWorldCommandKind::LifecycleResume:
            simulationState.setSchedulerLifecyclePaused(
                    false, simulation::simulationSchedulerNow());
            return MicroscopeCommandApplyResult::applied(false, true);
        case MicroscopeWorldCommandKind::Reset:
        case MicroscopeWorldCommandKind::Save:
        case MicroscopeWorldCommandKind::Apply:
        case MicroscopeWorldCommandKind::Inspect: {
            const auto request =
                    std::dynamic_pointer_cast<NativeMicroscopeRequest>(command.payload);
            if (request == nullptr) {
                return MicroscopeCommandApplyResult::rejected();
            }
            try {
                switch (request->operation) {
                    case NativeMicroscopeRequestOperation::ResetWorld: {
                        if (command.kind != MicroscopeWorldCommandKind::Reset) {
                            return MicroscopeCommandApplyResult::rejected();
                        }
                        resetWorldContents(world);
                        access.resetCompletedUpdateCount();
                        world.updateGrids();
                        const std::uint64_t worldEpoch = access.markWorldContentsReplaced();
                        mInteraction.advanceCommandWorldEpoch(worldEpoch);
                        mRenderSession.invalidateWorldEpoch(worldEpoch);
                        request->hasOutput = true;
                        return MicroscopeCommandApplyResult::applied(true, true);
                    }
                    case NativeMicroscopeRequestOperation::SaveSubstrate:
                        if (command.kind != MicroscopeWorldCommandKind::Save) {
                            return MicroscopeCommandApplyResult::rejected();
                        }
                        writeSubstratePath(request->path, world);
                        {
                            render::RenderFrameCaptureMetadata metadata;
                            metadata.worldEpoch = access.worldEpoch();
                            metadata.modeGeneration = simulationState.modeGeneration();
                            metadata.completedUpdateId = access.completedUpdateCount();
                            metadata.environmentRevision = access.environmentRevision();
                            request->outputToken =
                                    mRenderSession.tryPrepareSubstratePreviewFrame(world, metadata);
                        }
                        request->hasOutput = true;
                        return MicroscopeCommandApplyResult::applied();
                    case NativeMicroscopeRequestOperation::ApplyEnvironmentPatch: {
                        if (command.kind != MicroscopeWorldCommandKind::Apply) {
                            return MicroscopeCommandApplyResult::rejected();
                        }
                        EnvironmentBatch::ApplyResult result = EnvironmentBatch::applyPatch(
                                world.mEnvironment, access.environmentRevision(),
                                std::move(request->inputBytes));
                        if (result.applied) {
                            const std::int64_t appliedRevision = access.markEnvironmentChanged();
                            if (appliedRevision != result.revision) {
                                throw std::logic_error(
                                        "Environment revision changed during owner patch");
                            }
                        }
                        request->outputBytes = EnvironmentBatch::writeApplyResult(result);
                        request->hasOutput = true;
                        return MicroscopeCommandApplyResult::applied(result.applied,
                                result.applied);
                    }
                    case NativeMicroscopeRequestOperation::InspectCell:
                        if (command.kind != MicroscopeWorldCommandKind::Inspect) {
                            return MicroscopeCommandApplyResult::rejected();
                        }
                        if (request->cellId >= 0 && request->cellId < world.mCellData.mCellCount) {
                            request->outputBytes = writeCellSnapshotWire(world, request->cellId);
                            request->hasOutput = true;
                        }
                        return MicroscopeCommandApplyResult::applied();
                    case NativeMicroscopeRequestOperation::InspectEnvironment:
                        if (command.kind != MicroscopeWorldCommandKind::Inspect) {
                            return MicroscopeCommandApplyResult::rejected();
                        }
                        request->outputBytes = EnvironmentBatch::writeState(
                                world.mEnvironment, access.environmentRevision());
                        request->hasOutput = true;
                        return MicroscopeCommandApplyResult::applied();
                    case NativeMicroscopeRequestOperation::InspectStats:
                        if (command.kind != MicroscopeWorldCommandKind::Inspect) {
                            return MicroscopeCommandApplyResult::rejected();
                        }
                        request->completedUpdateCount = access.completedUpdateCount();
                        request->cellCount = world.mCellData.mCellCount;
                        request->foodMass = world.mFoodData.getFoodMass();
                        request->simulationTime = world.mTime;
                        request->hasOutput = true;
                        return MicroscopeCommandApplyResult::applied();
                    case NativeMicroscopeRequestOperation::InspectSelectedGenome: {
                        if (command.kind != MicroscopeWorldCommandKind::Inspect) {
                            return MicroscopeCommandApplyResult::rejected();
                        }
                        const int selectedCellId = world.mCellData.getIdFromSelectTag(1);
                        if (selectedCellId >= 0 && selectedCellId < world.mCellData.mCellCount) {
                            MicroscopeCommandGenome genome =
                                    world.mCellData.materializeGenome(selectedCellId);
                            const GenomeMetadata metadata{
                                    world.mCellData.activeGeneIndex(selectedCellId)
                            };
                            if (!applyGenomeMetadataToCompatibilityFlags(
                                        genome.data(), genome.size(), metadata)) {
                                throw std::logic_error("Selected Cell has an invalid active Gene index");
                            }
                            request->outputBytes = writeGenomeDraft(genome);
                            request->hasOutput = true;
                        }
                        return MicroscopeCommandApplyResult::applied();
                    }
                    case NativeMicroscopeRequestOperation::
                            InspectRuntimeConfiguration:
                        if (command.kind != MicroscopeWorldCommandKind::Inspect) {
                            return MicroscopeCommandApplyResult::rejected();
                        }
                        request->runtimeConfiguration = {
                                simulationState.temperatureMode(),
                                interaction.toolOrdinal(),
                                simulationState.slowMotionSpeed(),
                                simulationState.legacyIncubateTargetUps(),
                                simulationState.schedulerLifecyclePaused(),
                                mPerformanceHudEnabled.load(
                                        std::memory_order_relaxed)
                        };
                        request->hasOutput = true;
                        return MicroscopeCommandApplyResult::applied();
                }
            } catch (...) {
                request->failure = std::current_exception();
                return MicroscopeCommandApplyResult::rejected();
            }
            return MicroscopeCommandApplyResult::rejected();
        }
        case MicroscopeWorldCommandKind::SetRatePolicy:
        case MicroscopeWorldCommandKind::Reload:
            return MicroscopeCommandApplyResult::rejected();
    }
    return MicroscopeCommandApplyResult::rejected();
}

void NativeMicroscopeRuntime::updateWorldInteraction(
        NativeSimulationOwner::WriteAccess& access) {
    mWorldInteractionServiceCount =
            incrementRevision(mWorldInteractionServiceCount);
    SimulationWorld& world = access.world();
    if (world.getUpdateStage() != SimulationWorld::UPDATE_STAGE_CAN_TOUCH) {
        return;
    }
    NativeWorldContext context{world, *mOwnerViewport};
    auto& interaction = access.microscopeInteraction();
    auto& drag = interaction.drag();
    bool changed = false;
    for (int slot = 0; slot < MicroscopeDragController::MAX_POINTERS; ++slot) {
        if (!drag.isDragging(slot)) {
            continue;
        }
        if (!drag.dragSlot(context, slot, drag.worldX(slot), drag.worldY(slot),
                    access.microscopeSimulation().dragDeltaTime())) {
            drag.endDragSlot(context, slot);
        } else {
            changed = true;
        }
    }

    const float oldCameraX = mOwnerViewport->cameraX();
    const float oldCameraY = mOwnerViewport->cameraY();
    const float oldZoom = mOwnerViewport->zoom();
    const int selectedCellId = interaction.selection().update(context);
    changed = changed || selectedCellId != -1;
    if (mOwnerViewport->cameraX() != oldCameraX || mOwnerViewport->cameraY() != oldCameraY || mOwnerViewport->zoom() != oldZoom) {
        (void)mInteraction.publishOwnerCameraIfCurrent(
                mViewportSnapshot.viewport.revision, mOwnerViewport->cameraX(),
                mOwnerViewport->cameraY(), mOwnerViewport->zoom());
        syncNewestViewport();
    }
    mCaptureDirty = mCaptureDirty || changed;
    mInteraction.synchronizePresentationInteraction(interaction);
}

void NativeMicroscopeRuntime::executeScheduledUpdate(
        NativeSimulationOwner::WriteAccess& access,
        const MicroscopeSchedulerModeContract& contract) {
    if (contract.sourceCaptureCadence == ScheduledSourceCaptureCadence::EveryCompletedUpdate) {
        const float margin = std::max(mViewportSnapshot.viewport.viewWidth,
                                     mViewportSnapshot.viewport.viewHeight)
                * 0.1f;
        access.update1WithExecutionContext(true, contract.updateSpeed);
        capture(access, render::RenderCaptureStage::AfterUpdatePhaseOne, margin);
        access.update2WithExecutionContext(true, contract.updateSpeed);
        mCaptureDirty = false;
        return;
    }

    access.updateWithExecutionContext(contract.updateForRendering,
            contract.updateSpeed);
}

void NativeMicroscopeRuntime::captureScheduledIncubateFrame(
        NativeSimulationOwner::WriteAccess& access,
        const MicroscopeSchedulerModeContract& contract) {
    auto& state = access.microscopeSimulation();
    const auto checkedAt = simulation::simulationSchedulerNow();
    if (contract.sourceCaptureCadence != ScheduledSourceCaptureCadence::FixedInterval || (!state.scheduledSourceCaptureDue(checkedAt) && !mUrgentCapture)) {
        return;
    }

    const float margin = std::max(mViewportSnapshot.viewport.viewWidth,
                                 mViewportSnapshot.viewport.viewHeight)
            * 0.1f;
    capture(access, render::RenderCaptureStage::AfterCompletedUpdate, margin);
    // Schedule from completion rather than from the pre-capture check. A slow
    // source copy must reduce source cadence instead of creating back-to-back
    // capture debt that can consume every owner cycle.
    state.markScheduledSourceCaptureAttempt(simulation::simulationSchedulerNow());
    mUrgentCapture = false;
    mCaptureDirty = false;
}

void NativeMicroscopeRuntime::capture(
        NativeSimulationOwner::WriteAccess& access,
        render::RenderCaptureStage stage, float margin) {
    if (mViewportSnapshot.viewport.surfaceWidth <= 0 || mViewportSnapshot.viewport.surfaceHeight <= 0) {
        return;
    }
    render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = access.worldEpoch();
    metadata.modeGeneration = access.microscopeSimulation().modeGeneration();
    metadata.completedUpdateId = access.completedUpdateCount();
    metadata.stage = stage;
    metadata.environmentRevision = access.environmentRevision();
    metadata.viewport = mViewportSnapshot.viewport;
    const auto resolved = mInteraction.commandMailbox().resolvedState();
    metadata.resolvedInput.lastResolvedInputSequence =
            resolved.lastResolvedInputSequence;
    metadata.resolvedInput.appliedWorldRevision = resolved.appliedWorldRevision;
    access.world().ensureFoodSpatialIndexForCapture();
    (void)mRenderSession.captureMicroscopeFrame(
            access.world(), mViewportSnapshot.viewport, margin, metadata);
}

void NativeMicroscopeRuntime::recordOwnerServiceOpportunity(
        MicroscopeCommandTimePoint now) noexcept {
    if (mLastOwnerServiceAt.has_value() && now >= *mLastOwnerServiceAt) {
        mLastOwnerServiceInterval =
                std::chrono::duration_cast<simulation::SimulationSchedulerDuration>(
                        now - *mLastOwnerServiceAt);
        mMaximumOwnerServiceInterval =
                std::max(mMaximumOwnerServiceInterval, mLastOwnerServiceInterval);
    }
    mLastOwnerServiceAt = now;
    mOwnerServiceCount = incrementRevision(mOwnerServiceCount);
}

void NativeMicroscopeRuntime::updateRateTelemetry(
        NativeSimulationOwner::WriteAccess& access,
        const simulation::SimulationSchedulerSnapshot& scheduler,
        MicroscopeCommandTimePoint now, NativeMicroscopeRateTelemetry& telemetry) {
    const std::uint64_t completedUpdates = access.completedUpdateCount();
    const double simulationHours = access.world().mTime;
    const bool restartWindow =
            !mActualRateWindowStartedAt.has_value() || mActualRatePolicyGeneration != scheduler.policyGeneration || completedUpdates < mActualRateWindowStartUpdates || simulationHours < mActualRateWindowStartSimulationHours;
    if (restartWindow) {
        mActualRateWindowStartedAt = now;
        mActualRateWindowStartUpdates = completedUpdates;
        mActualRateWindowStartSimulationHours = simulationHours;
        mActualRatePolicyGeneration = scheduler.policyGeneration;
        mActualRateSampleSequence = incrementRevision(mActualRateSampleSequence);
        mActualRateValid = false;
        mActualCompletedUps = 0.0;
        mActualSimulationHoursPerSecond = 0.0;
        mActualRateWindow = simulation::SimulationSchedulerDuration::zero();
        mActualRateWindowUpdates = 0;
    } else {
        const auto elapsed =
                std::chrono::duration_cast<simulation::SimulationSchedulerDuration>(
                        now - *mActualRateWindowStartedAt);
        if (elapsed >= kActualRateWindow) {
            const std::uint64_t elapsedUpdates =
                    completedUpdates - mActualRateWindowStartUpdates;
            const double elapsedSeconds =
                    static_cast<double>(elapsed.count()) / 1'000'000'000.0;
            const double elapsedSimulationHours = std::max(
                    0.0, simulationHours - mActualRateWindowStartSimulationHours);
            mActualRateSampleSequence = incrementRevision(mActualRateSampleSequence);
            mActualRateValid = elapsedSeconds > 0.0;
            mActualCompletedUps =
                    mActualRateValid
                    ? static_cast<double>(elapsedUpdates) / elapsedSeconds
                    : 0.0;
            mActualSimulationHoursPerSecond =
                    mActualRateValid ? elapsedSimulationHours / elapsedSeconds : 0.0;
            mActualRateWindow = elapsed;
            mActualRateWindowUpdates = elapsedUpdates;
            mActualRateWindowStartedAt = now;
            mActualRateWindowStartUpdates = completedUpdates;
            mActualRateWindowStartSimulationHours = simulationHours;
        }
    }

    telemetry.scheduler = scheduler;
    telemetry.actualRateSampleSequence = mActualRateSampleSequence;
    telemetry.actualRateValid = mActualRateValid;
    telemetry.actualCompletedUps = mActualCompletedUps;
    telemetry.actualSimulationHoursPerSecond = mActualSimulationHoursPerSecond;
    telemetry.actualRateWindow = mActualRateWindow;
    telemetry.actualRateWindowUpdates = mActualRateWindowUpdates;
    telemetry.ownerServiceCount = mOwnerServiceCount;
    telemetry.worldInteractionServiceCount = mWorldInteractionServiceCount;
    telemetry.lastOwnerServiceInterval = mLastOwnerServiceInterval;
    telemetry.maximumOwnerServiceInterval = mMaximumOwnerServiceInterval;

    const auto renderTelemetry =
            mRenderSession.microscopeFrameExchangeTelemetry();
    telemetry.publishedSourceFrameCount =
            renderTelemetry.source.publishedFrameCount;
    telemetry.droppedSourceFrameCount =
            renderTelemetry.source.droppedFrameCount();
    telemetry.publishedPreparedFrameCount =
            renderTelemetry.prepared.publishedFrameCount;
    telemetry.droppedPreparedFrameCount =
            renderTelemetry.prepared.droppedFrameCount();

    const auto publication = mSnapshotExchange.counters();
    telemetry.statusPublishedCount = publication.published;
    telemetry.statusConsumedCount = publication.consumed;
    telemetry.statusProducerReplacedCount = publication.producerReplaced;
    telemetry.statusConsumerDiscardedCount = publication.consumerDiscarded;
    telemetry.statusPublishRetriesExhausted = publication.publishRetriesExhausted;
}

void NativeMicroscopeRuntime::publishSnapshot(
        NativeMicroscopeRuntimeSnapshot snapshot) noexcept {
    mPublishedSnapshot = snapshot;
    (void)mSnapshotExchange.publish(snapshot, snapshot.revision);
}

void NativeMicroscopeRuntime::publishFailureSnapshot() noexcept {
    NativeMicroscopeRuntimeSnapshot failed = mPublishedSnapshot;
    failed.revision = incrementRevision(failed.revision);
    failed.failed = true;
    publishSnapshot(std::move(failed));
}

void NativeMicroscopeRuntime::publishStatus(
        NativeSimulationOwner::WriteAccess& access, bool force) {
    const MicroscopeCommandTimePoint now = microscopeCommandNow();
    const std::int64_t nowNanos = timePointNanos(now);
    if (!force && mLastStatusPublishNanos != 0 && nowNanos - mLastStatusPublishNanos < kPublicationIntervalNanos) {
        return;
    }
    mLastStatusPublishNanos = nowNanos;

    const SimulationWorld& world = access.world();
    const NativeMicroscopeRuntimeSnapshot& previous = mPublishedSnapshot;
    NativeMicroscopeRuntimeSnapshot next = previous;
    next.revision = incrementRevision(next.revision);
    next.worldEpoch = access.worldEpoch();
    next.completedUpdateCount = access.completedUpdateCount();
    const auto resolved = mInteraction.commandMailbox().resolvedState();
    next.lastResolvedInputSequence = resolved.lastResolvedInputSequence;
    next.appliedWorldRevision = resolved.appliedWorldRevision;
    next.environmentRevision = access.environmentRevision();
    next.cellCount = world.mCellData.mCellCount;
    next.foodMass = world.mFoodData.getFoodMass();
    next.simulationTime = world.mTime;
    next.temperatureMode = access.microscopeSimulation().temperatureMode();
    next.toolOrdinal = access.microscopeInteraction().toolOrdinal();
    next.diagnosisRequested = next.toolOrdinal == kDiagnosisToolOrdinal;
    next.selectedCellId = world.mCellData.getIdFromSelectTag(1);
    next.failed = false;
#ifdef CELLULARITY_PERFORMANCE_HUD
    if (mPerformanceHudEnabled.load(std::memory_order_relaxed)) {
        next.stageTimings = world.stageTimingSnapshot();
    }
#endif

    const auto scheduler = access.microscopeSimulation().schedulerSnapshot();
    if (!scheduler.has_value()) {
        throw std::logic_error("Microscope runtime telemetry requires a scheduler");
    }
    updateRateTelemetry(access, *scheduler, now, next.rateTelemetry);

    if (next.environmentState == nullptr || next.worldEpoch != previous.worldEpoch || next.environmentRevision != previous.environmentRevision) {
        next.environmentState = std::make_shared<const std::vector<std::uint8_t>>(
                EnvironmentBatch::writeState(world.mEnvironment,
                        access.environmentRevision()));
    }

    if (next.diagnosisRequested && next.selectedCellId >= 0 && next.selectedCellId < world.mCellData.mCellCount) {
        next.selectedCellState = std::make_shared<const std::vector<std::uint8_t>>(
                writeCellSnapshotWire(world, next.selectedCellId));
        MicroscopeCommandGenome genome =
                world.mCellData.materializeGenome(next.selectedCellId);
        const GenomeMetadata metadata{
                world.mCellData.activeGeneIndex(next.selectedCellId)
        };
        if (!applyGenomeMetadataToCompatibilityFlags(
                    genome.data(), genome.size(), metadata)) {
            throw std::logic_error("Selected Cell has an invalid active Gene index");
        }
        next.selectedGenomeState =
                std::make_shared<const std::vector<std::uint8_t>>(
                        writeGenomeDraft(genome));
    } else {
        next.selectedCellState.reset();
        next.selectedGenomeState.reset();
    }

    publishSnapshot(std::move(next));
}

} // namespace cellularity::microscope
