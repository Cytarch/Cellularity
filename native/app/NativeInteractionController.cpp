#include "app/NativeInteractionController.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

#include "app/RenderStateSnapshots.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeFrameController.h"
#include "app/microscope/MicroscopeInputController.h"
#include "app/microscope/MicroscopeSimulationState.h"
#include "app/microscope/interaction/MicroscopeSelectionController.h"
#include "app/microscope/interaction/MicroscopeWorldInteractionState.h"
#include "render/frame/RenderTimelineClock.h"

namespace {

cellularity::microscope::MicroscopeInputCommandWireStatus
wireStatus(cellularity::microscope::MicroscopeCommandEnqueueStatus status) noexcept {
    using EnqueueStatus =
            cellularity::microscope::MicroscopeCommandEnqueueStatus;
    using WireStatus =
            cellularity::microscope::MicroscopeInputCommandWireStatus;
    switch (status) {
        case EnqueueStatus::Accepted:
            return WireStatus::WorldAccepted;
        case EnqueueStatus::AcceptedAndCoalescedPrevious:
            return WireStatus::WorldAcceptedCoalescedPrevious;
        case EnqueueStatus::RejectedOrderedCapacity:
            return WireStatus::WorldRejectedOrderedCapacity;
        case EnqueueStatus::RejectedPathCapacity:
            return WireStatus::WorldRejectedPathCapacity;
        case EnqueueStatus::RejectedPointerState:
            return WireStatus::WorldRejectedPointerState;
        case EnqueueStatus::RejectedPointerCapacity:
            return WireStatus::WorldRejectedPointerCapacity;
        case EnqueueStatus::RejectedInvalidArgument:
            return WireStatus::WorldRejectedInvalidArgument;
        case EnqueueStatus::RejectedSequenceExhausted:
            return WireStatus::WorldRejectedSequenceExhausted;
    }
    return WireStatus::WorldRejectedInvalidArgument;
}

std::uint32_t floatBits(float value) noexcept {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

NativeInteractionController::NativeInteractionController(
        float cameraBoundsRadius, std::uint64_t initialWorldEpoch)
        : mViewport(std::make_unique<NativeViewport>(cameraBoundsRadius)),
          mFrame(std::make_unique<MicroscopeFrameController>()),
          mInput(std::make_unique<MicroscopeInputController>()),
          mCommandMailbox(initialWorldEpoch) {
    publishViewport(0, false);
}

NativeInteractionController::~NativeInteractionController() = default;

void NativeInteractionController::setViewportSurfaceSize(int width,
        int height) {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    mViewport->setSurfaceSize(width, height);
    publishViewport(0, true);
}

void NativeInteractionController::setViewportCamera(float cameraX,
        float cameraY, float zoom) {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    mViewport->setCamera(cameraX, cameraY, zoom);
    invalidateFrameData();
    publishViewport(0, true);
}

void NativeInteractionController::moveViewportByScreenDelta(float dx,
        float dy) {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    mViewport->moveByScreenDelta(dx, dy);
    invalidateFrameData();
    publishViewport(0, true);
}

void NativeInteractionController::zoomViewportAt(float factor, float screenX,
        float screenY) {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    mViewport->zoomAt(factor, screenX, screenY);
    invalidateFrameData();
    publishViewport(0, true);
}

void NativeInteractionController::abandonViewportSurface() {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    mViewport->setSurfaceSize(0, 0);
    publishViewport(0, false);
}

void NativeInteractionController::publishViewportIfChanged() {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    if (mViewport->revision() != mLastPublishedViewportRevision) {
        publishViewport(0, true);
    }
}

std::array<float, 7> NativeInteractionController::viewportState() const {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    return {mViewport->cameraX(),
            mViewport->cameraY(),
            mViewport->zoom(),
            mViewport->viewWidth(),
            mViewport->viewHeight(),
            mViewport->pixelPerWorldCoord(),
            mInput->viewportInertiaActive() ? 1.0f : 0.0f};
}

std::uint64_t NativeInteractionController::viewportRevision() const {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    return mViewport->revision();
}

float NativeInteractionController::viewportScreenToWorldX(
        float screenX) const {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    return mViewport->screenToWorldX(screenX);
}

float NativeInteractionController::viewportScreenToWorldY(
        float screenY) const {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    return mViewport->screenToWorldY(screenY);
}

void NativeInteractionController::invalidateFrameData() {
    mFrame->invalidateFrameData();
}

void NativeInteractionController::onTemperatureModeChanged(
        int temperatureMode,
        cellularity::microscope::MicroscopeWorldInteractionState& worldInteraction) {
    mPresentationTemperatureMode.store(temperatureMode,
            std::memory_order_release);
    mFrame->onTemperatureModeChanged();
    clearInputQueues(worldInteraction);
}

bool NativeInteractionController::stepFrame(
        NativeWorldContext context, NativeSimulationStepper& simulation,
        cellularity::microscope::MicroscopeSimulationState& simulationState,
        cellularity::microscope::MicroscopeWorldInteractionState& worldInteraction,
        const MicroscopeFrameController::RenderFrameCapture& captureRenderFrame) {
    const bool captured = mFrame->stepFrame(
            context, simulation, simulationState, captureRenderFrame, *mInput,
            mInputTimeline, worldInteraction.drag(), worldInteraction.tap(),
            worldInteraction.selection(), worldInteraction.toolOrdinal(),
            worldInteraction.hasSelectedCell());
    synchronizePresentationInteraction(worldInteraction);
    publishViewportIfChanged();
    return captured;
}

bool NativeInteractionController::stepViewportInertia() {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    bool changed = mInput->stepViewportInertia(*mViewport);
    if (changed) {
        invalidateFrameData();
        publishViewport(0, true);
    }
    refreshPresentationMotionLocked();
    return changed;
}

bool NativeInteractionController::viewportInertiaActive() const {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    return mInput->viewportInertiaActive();
}

void NativeInteractionController::setViewportFlingVelocity(float velocityX,
        float velocityY) {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    mInput->setViewportFlingVelocity(velocityX, velocityY);
    refreshPresentationMotionLocked();
}

void NativeInteractionController::clearInputQueues(
        cellularity::microscope::MicroscopeWorldInteractionState& worldInteraction) {
    mInputTimeline.cancelWorldInputs();
    mInput->clear();
    worldInteraction.clearDragging();
    synchronizePresentationInteraction(worldInteraction);
}

void NativeInteractionController::queueTouchDown(int pointerId, float screenX,
        float screenY) {
    const std::uint64_t sequence = mInputTimeline.beginInput(
            cellularity::microscope::InputLane::WorldOwner,
            cellularity::microscope::InputKind::TouchDown, mViewport->revision());
    const bool accepted =
            mInput->queueTouchDown(*mViewport, pointerId, screenX, screenY);
    mInputTimeline.enqueueWorldInput(
            sequence, accepted,
            static_cast<std::uint32_t>(mInput->queuedWorldInputCount()));
}

void NativeInteractionController::queueTouchMove(int pointerId, float screenX,
        float screenY) {
    const std::uint64_t sequence = mInputTimeline.beginInput(
            cellularity::microscope::InputLane::WorldOwner,
            cellularity::microscope::InputKind::TouchMove, mViewport->revision());
    const bool accepted =
            mInput->queueTouchMove(*mViewport, pointerId, screenX, screenY);
    mInputTimeline.enqueueWorldInput(
            sequence, accepted,
            static_cast<std::uint32_t>(mInput->queuedWorldInputCount()));
}

void NativeInteractionController::queueTouchUp(int pointerId) {
    const std::uint64_t sequence = mInputTimeline.beginInput(
            cellularity::microscope::InputLane::WorldOwner,
            cellularity::microscope::InputKind::TouchUp, mViewport->revision());
    const bool accepted = mInput->queueTouchUp(pointerId);
    mInputTimeline.enqueueWorldInput(
            sequence, accepted,
            static_cast<std::uint32_t>(mInput->queuedWorldInputCount()));
}

void NativeInteractionController::queueTap(
        float screenX, float screenY,
        const cellularity::GenomeData* genome) {
    const bool replacesPendingTap = mInput->hasQueuedTap();
    const std::uint64_t sequence = mInputTimeline.beginInput(
            cellularity::microscope::InputLane::WorldOwner,
            cellularity::microscope::InputKind::Tap, mViewport->revision());
    mInput->queueTap(*mViewport, screenX, screenY, genome);
    mInputTimeline.enqueueWorldInput(
            sequence, true,
            static_cast<std::uint32_t>(mInput->queuedWorldInputCount()),
            replacesPendingTap);
}

bool NativeInteractionController::queuePan(float dx, float dy) {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    const std::uint64_t sequence = mInputTimeline.beginInput(
            cellularity::microscope::InputLane::Presentation,
            cellularity::microscope::InputKind::Pan, mViewport->revision());
    const auto gate = mPresentationState.interactionGate();
    const bool accepted =
            mInput->queuePan(*mViewport, gate.hasDraggingPointers, gate.toolOrdinal,
                    gate.hasSelectedCell, dx, dy);
    mInputTimeline.resolvePresentationInput(sequence, accepted,
            mViewport->revision());
    if (accepted) {
        publishViewport(sequence, true);
    }
    return accepted;
}

bool NativeInteractionController::queueZoom(float factor, float screenX,
        float screenY) {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    const std::uint64_t sequence = mInputTimeline.beginInput(
            cellularity::microscope::InputLane::Presentation,
            cellularity::microscope::InputKind::Zoom, mViewport->revision());
    const auto gate = mPresentationState.interactionGate();
    const bool accepted =
            mInput->queueZoom(*mViewport, gate.hasDraggingPointers, gate.toolOrdinal,
                    gate.hasSelectedCell, factor, screenX, screenY);
    mInputTimeline.resolvePresentationInput(sequence, accepted,
            mViewport->revision());
    if (accepted) {
        publishViewport(sequence, true);
    }
    return accepted;
}

bool NativeInteractionController::queueFling(float velocityX, float velocityY) {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    const std::uint64_t sequence = mInputTimeline.beginInput(
            cellularity::microscope::InputLane::Presentation,
            cellularity::microscope::InputKind::Fling, mViewport->revision());
    const auto gate = mPresentationState.interactionGate();
    const bool accepted = mInput->queueFling(
            gate.hasDraggingPointers, gate.toolOrdinal, velocityX, velocityY);
    mInputTimeline.resolvePresentationInput(sequence, accepted,
            mViewport->revision());
    if (accepted) {
        mCommandMailbox.notifyOwner();
    }
    refreshPresentationMotionLocked();
    return accepted;
}

void NativeInteractionController::submitInputBatch(
        const cellularity::microscope::MicroscopeInputBatch& batch,
        std::shared_ptr<const cellularity::microscope::MicroscopeCommandGenome>
                tapGenome,
        cellularity::microscope::MicroscopeInputBatchResult& result,
        bool rejectTapForStalePlacementGenome) {
    using CommandKind = cellularity::microscope::MicroscopeInputCommandKind;
    using WireStatus =
            cellularity::microscope::MicroscopeInputCommandWireStatus;

    if (inputAdmissionClosed()) {
        result.ingressStatus =
                cellularity::microscope::MicroscopeInputIngressStatus::Closing;
        result.commandCount = 0;
        return;
    }

    const std::lock_guard<std::mutex> presentationLock(mPresentationMutex);
    const std::lock_guard<std::mutex> ingressLock(mIngressMutex);

    bool viewportChanged = false;
    std::uint64_t finalPresentationSequence = 0;
    cellularity::microscope::dispatchValidatedMicroscopeInputBatch(
            batch, result,
            [&](std::size_t,
                    const cellularity::microscope::MicroscopeInputCommandRecord&
                            command) -> WireStatus {
                switch (command.kind) {
                    case CommandKind::Down: {
                        const auto admission = mCommandMailbox.enqueueTouchDown(
                                command.pointerId,
                                mViewport->screenToWorldX(command.values[0]),
                                mViewport->screenToWorldY(command.values[1]),
                                mViewport->revision());
                        return wireStatus(admission.status);
                    }
                    case CommandKind::Move: {
                        const auto admission =
                                mCommandMailbox.enqueueTouchMovePosition(
                                        command.pointerId,
                                        mViewport->screenToWorldX(command.values[0]),
                                        mViewport->screenToWorldY(command.values[1]),
                                        mViewport->revision());
                        return wireStatus(admission.status);
                    }
                    case CommandKind::Up: {
                        const auto admission =
                                mCommandMailbox.enqueueTouchUp(command.pointerId);
                        return wireStatus(admission.status);
                    }
                    case CommandKind::Tap: {
                        if (rejectTapForStalePlacementGenome) {
                            return WireStatus::
                                    WorldRejectedStalePlacementGenome;
                        }
                        const auto admission = mCommandMailbox.enqueueTap(
                                mViewport->screenToWorldX(command.values[0]),
                                mViewport->screenToWorldY(command.values[1]),
                                mViewport->revision(), tapGenome);
                        return wireStatus(admission.status);
                    }
                    case CommandKind::Pan: {
                        const std::uint64_t sequence = mInputTimeline.beginInput(
                                cellularity::microscope::InputLane::Presentation,
                                cellularity::microscope::InputKind::Pan,
                                mViewport->revision());
                        const auto gate = mPresentationState.interactionGate();
                        const bool accepted = mInput->queuePan(
                                *mViewport, gate.hasDraggingPointers, gate.toolOrdinal,
                                gate.hasSelectedCell, command.values[0],
                                command.values[1]);
                        mInputTimeline.resolvePresentationInput(
                                sequence, accepted, mViewport->revision());
                        if (accepted) {
                            viewportChanged = true;
                            finalPresentationSequence = sequence;
                            result.aggregateFlags |=
                                    cellularity::microscope::
                                            kMicroscopeInputAggregateCameraChanged;
                        }
                        return accepted
                                ? WireStatus::PresentationApplied
                                : WireStatus::PresentationRejectedInteractionGate;
                    }
                    case CommandKind::Zoom: {
                        const std::uint64_t sequence = mInputTimeline.beginInput(
                                cellularity::microscope::InputLane::Presentation,
                                cellularity::microscope::InputKind::Zoom,
                                mViewport->revision());
                        const auto gate = mPresentationState.interactionGate();
                        const bool accepted = mInput->queueZoom(
                                *mViewport, gate.hasDraggingPointers, gate.toolOrdinal,
                                gate.hasSelectedCell, command.values[0],
                                command.values[1], command.values[2]);
                        mInputTimeline.resolvePresentationInput(
                                sequence, accepted, mViewport->revision());
                        if (accepted) {
                            viewportChanged = true;
                            finalPresentationSequence = sequence;
                            result.aggregateFlags |=
                                    cellularity::microscope::
                                            kMicroscopeInputAggregateCameraChanged
                                    | cellularity::microscope::
                                            kMicroscopeInputAggregateZoomDisplaySizeChanged;
                            result.zoomDisplaySizeBits =
                                    floatBits(mViewport->viewWidth() * 0.0005F);
                        }
                        return accepted
                                ? WireStatus::PresentationApplied
                                : WireStatus::PresentationRejectedInteractionGate;
                    }
                    case CommandKind::Fling: {
                        const std::uint64_t sequence = mInputTimeline.beginInput(
                                cellularity::microscope::InputLane::Presentation,
                                cellularity::microscope::InputKind::Fling,
                                mViewport->revision());
                        const auto gate = mPresentationState.interactionGate();
                        const bool accepted = mInput->queueFling(
                                gate.hasDraggingPointers, gate.toolOrdinal,
                                command.values[0], command.values[1]);
                        mInputTimeline.resolvePresentationInput(
                                sequence, accepted, mViewport->revision());
                        if (accepted) {
                            mCommandMailbox.notifyOwner();
                            result.aggregateFlags |=
                                    cellularity::microscope::
                                            kMicroscopeInputAggregateCameraChanged;
                        }
                        return accepted
                                ? WireStatus::PresentationApplied
                                : WireStatus::PresentationRejectedInteractionGate;
                    }
                    case CommandKind::ZoomEnded:
                        result.aggregateFlags |=
                                cellularity::microscope::
                                        kMicroscopeInputAggregateZoomEnded;
                        return WireStatus::UiApplied;
                }
                return WireStatus::WorldRejectedInvalidArgument;
            });

    if (viewportChanged) {
        invalidateFrameData();
        publishViewport(finalPresentationSequence, true);
    }
    refreshPresentationMotionLocked();
    result.viewportRevision = mViewport->revision();
    result.presentationMotionRevision =
            mPresentationMotionSnapshot.revision;
}

cellularity::microscope::PresentationMotionSnapshot
NativeInteractionController::presentationMotionSnapshot() const noexcept {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    return mPresentationMotionSnapshot;
}

bool NativeInteractionController::requestInputResync() noexcept {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    const auto cancelled = mCommandMailbox.enqueueActivePointerCancels();
    return cancelled.invariantSatisfied;
}

std::uint32_t
NativeInteractionController::activePointerGenerationCount() const noexcept {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.activePointerGenerationCount();
}

void NativeInteractionController::closeInputAdmission() noexcept {
    mInputAdmissionClosed.store(true, std::memory_order_release);
}

bool NativeInteractionController::inputAdmissionClosed() const noexcept {
    return mInputAdmissionClosed.load(std::memory_order_acquire);
}

NativeViewport& NativeInteractionController::viewport() {
    return *mViewport;
}

const NativeViewport& NativeInteractionController::viewport() const {
    return *mViewport;
}

void NativeInteractionController::synchronizePresentationInteraction(
        const cellularity::microscope::MicroscopeWorldInteractionState& worldInteraction) noexcept {
    mPresentationState.publishInteractionGate(
            worldInteraction.toolOrdinal(), worldInteraction.hasDraggingPointers(),
            worldInteraction.hasSelectedCell());
}

bool NativeInteractionController::tryConsumeNewestPresentationViewport(
        cellularity::microscope::PresentationViewportSnapshot& snapshot) noexcept {
    return mPresentationState.tryConsumeNewestViewport(snapshot);
}

bool NativeInteractionController::tryConsumeNewestOwnerViewport(
        cellularity::microscope::PresentationViewportSnapshot& snapshot) noexcept {
    return mPresentationState.tryConsumeNewestOwnerViewport(snapshot);
}

bool NativeInteractionController::publishOwnerCameraIfCurrent(
        std::uint64_t sourceViewportRevision, float cameraX, float cameraY,
        float zoom) noexcept {
    const std::lock_guard<std::mutex> lock(mPresentationMutex);
    if (mViewport->revision() != sourceViewportRevision || !std::isfinite(cameraX) || !std::isfinite(cameraY) || !std::isfinite(zoom)) {
        return false;
    }
    mViewport->setCamera(cameraX, cameraY, zoom);
    publishViewport(0, true);
    return true;
}

cellularity::microscope::MicroscopePresentationStateSnapshot
NativeInteractionController::presentationStateSnapshot() const noexcept {
    return mPresentationState.snapshot();
}

cellularity::microscope::MicroscopeCommandMailbox&
NativeInteractionController::commandMailbox() noexcept {
    return mCommandMailbox;
}

const cellularity::microscope::MicroscopeCommandMailbox&
NativeInteractionController::commandMailbox() const noexcept {
    return mCommandMailbox;
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerTouchDown(int pointerId, float screenX,
        float screenY) {
    double worldX = 0.0;
    double worldY = 0.0;
    std::uint64_t revision = 0;
    {
        const std::lock_guard<std::mutex> lock(mPresentationMutex);
        worldX = mViewport->screenToWorldX(screenX);
        worldY = mViewport->screenToWorldY(screenY);
        revision = mViewport->revision();
    }
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueTouchDown(pointerId, worldX, worldY, revision);
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerTouchMove(int pointerId, float screenX,
        float screenY,
        bool pathDependent) {
    double worldX = 0.0;
    double worldY = 0.0;
    std::uint64_t revision = 0;
    {
        const std::lock_guard<std::mutex> lock(mPresentationMutex);
        worldX = mViewport->screenToWorldX(screenX);
        worldY = mViewport->screenToWorldY(screenY);
        revision = mViewport->revision();
    }
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return pathDependent
            ? mCommandMailbox.enqueueTouchMovePathSegment(
                      pointerId, worldX, worldY, revision)
            : mCommandMailbox.enqueueTouchMovePosition(pointerId, worldX,
                      worldY, revision);
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerTouchUp(int pointerId) {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueTouchUp(pointerId);
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerTap(
        float screenX, float screenY,
        std::shared_ptr<const cellularity::microscope::MicroscopeCommandGenome>
                genome) {
    double worldX = 0.0;
    double worldY = 0.0;
    std::uint64_t revision = 0;
    {
        const std::lock_guard<std::mutex> lock(mPresentationMutex);
        worldX = mViewport->screenToWorldX(screenX);
        worldY = mViewport->screenToWorldY(screenY);
        revision = mViewport->revision();
    }
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueTap(worldX, worldY, revision,
            std::move(genome));
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerTemperatureMode(int temperatureMode) {
    setPresentationTemperatureMode(temperatureMode);
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueTemperatureMode(temperatureMode);
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerToolOrdinal(int toolOrdinal) {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueToolOrdinal(toolOrdinal);
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerSlowMotionSpeed(
        float slowMotionSpeed) {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueSlowMotionSpeed(slowMotionSpeed);
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerOptionalTargetUps(double targetUps) {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueOptionalTargetUps(targetUps);
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerLifecycle(bool paused) {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueBarrier(
            paused ? cellularity::microscope::MicroscopeWorldCommandKind::LifecyclePause
                   : cellularity::microscope::MicroscopeWorldCommandKind::LifecycleResume);
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerBarrier(
        cellularity::microscope::MicroscopeWorldCommandKind kind,
        std::uint64_t requestId,
        std::shared_ptr<cellularity::microscope::MicroscopeCommandCompletion>
                completion,
        std::shared_ptr<cellularity::microscope::MicroscopeCommandPayload> payload) {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueBarrier(
            kind, requestId, std::move(completion),
            cellularity::microscope::microscopeCommandNow(), std::move(payload));
}

cellularity::microscope::MicroscopeCommandEnqueueResult
NativeInteractionController::enqueueOwnerRequest(
        cellularity::microscope::MicroscopeWorldCommandKind kind,
        std::uint64_t requestId,
        std::shared_ptr<cellularity::microscope::MicroscopeCommandCompletion>
                completion,
        std::shared_ptr<cellularity::microscope::MicroscopeCommandPayload> payload) {
    const std::lock_guard<std::mutex> lock(mIngressMutex);
    return mCommandMailbox.enqueueRequest(
            kind, requestId, std::move(completion),
            cellularity::microscope::microscopeCommandNow(), std::move(payload));
}

void NativeInteractionController::wakeOwner() noexcept {
    mCommandMailbox.notifyOwner();
}

void NativeInteractionController::advanceCommandWorldEpoch(
        std::uint64_t worldEpoch) {
    mCommandMailbox.advanceWorldEpoch(worldEpoch);
}

bool NativeInteractionController::consumeViewportCaptureHint() noexcept {
    return mCommandMailbox.consumeViewportCaptureHint().has_value();
}

std::array<float, 3> NativeInteractionController::nextBackgroundColor() {
    return mFrame->nextBackgroundColor(
            mPresentationTemperatureMode.load(std::memory_order_acquire));
}

void NativeInteractionController::setPresentationTemperatureMode(
        int temperatureMode) noexcept {
    mPresentationTemperatureMode.store(temperatureMode,
            std::memory_order_release);
}

int NativeInteractionController::presentationTemperatureMode() const noexcept {
    return mPresentationTemperatureMode.load(std::memory_order_acquire);
}

bool NativeInteractionController::shouldUploadDrawFrame() const {
    return mFrame->shouldUploadDrawFrame(
            mPresentationTemperatureMode.load(std::memory_order_acquire));
}

void NativeInteractionController::markDrawFrameConsumed() {
    mFrame->markDrawFrameConsumed();
}

void NativeInteractionController::paceBeforeDraw() {
    mFrame->paceBeforeDraw(
            mPresentationTemperatureMode.load(std::memory_order_acquire));
}

cellularity::microscope::ResolvedInputState
NativeInteractionController::resolvedInputState() const noexcept {
    return mInputTimeline.resolvedState();
}

void NativeInteractionController::markFramePresented(
        std::uint64_t frameLastResolvedInputSequence,
        std::uint64_t frameWorldRevision, std::int64_t sourceCapturedNanos,
        std::int64_t framePreparedNanos, std::uint64_t presentedViewportRevision,
        std::int64_t firstPresentedNanos) {
    mInputTimeline.markFramePresented(
            frameLastResolvedInputSequence, frameWorldRevision, sourceCapturedNanos,
            framePreparedNanos, presentedViewportRevision, firstPresentedNanos);
}

void NativeInteractionController::resetInputTimelineMeasurements() {
    mInputTimeline.resetMeasurements();
}

cellularity::microscope::InputTimelineSnapshot
NativeInteractionController::inputTimelineSnapshot() const {
    return mInputTimeline.snapshot();
}

void NativeInteractionController::publishViewport(
        std::uint64_t inputSequence, bool requestOwnerCapture) noexcept {
    const auto visibleBounds =
            cellularity::app::toRenderBounds(mViewport->viewBounds(0.0f));
    cellularity::microscope::PresentationViewportSnapshot snapshot;
    snapshot.viewport =
            cellularity::app::snapshotViewport(*mViewport, visibleBounds);
    snapshot.inputSequence = inputSequence;
    snapshot.publishedNanos = cellularity::render::renderTimelineNowNanos();
    if (mPresentationState.publishViewport(snapshot)) {
        mLastPublishedViewportRevision = snapshot.viewport.revision;
    }
    if (requestOwnerCapture) {
        mCommandMailbox.publishViewportCaptureHint(mViewport->revision());
    }
    refreshPresentationMotionLocked();
}

void NativeInteractionController::refreshPresentationMotionLocked() noexcept {
    const std::uint64_t cameraRevision = mViewport->revision();
    const bool inertiaActive = mInput->viewportInertiaActive();
    const bool cameraMotionActive = inertiaActive;
    if (mPresentationMotionSnapshot.revision != 0
            && mPresentationMotionSnapshot.cameraRevision == cameraRevision
            && mPresentationMotionSnapshot.inertiaActive == inertiaActive
            && mPresentationMotionSnapshot.cameraMotionActive
                    == cameraMotionActive) {
        return;
    }
    if (mPresentationMotionSnapshot.revision
            != std::numeric_limits<std::uint64_t>::max()) {
        ++mPresentationMotionSnapshot.revision;
    }
    mPresentationMotionSnapshot.cameraRevision = cameraRevision;
    mPresentationMotionSnapshot.inertiaActive = inertiaActive;
    mPresentationMotionSnapshot.cameraMotionActive = cameraMotionActive;
}
