#include "app/genome_preview/GenomePreviewEndpoint.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "app/NativeCellularityController.h"
#include "app/NativeGenomePreviewFacade.h"
#include "app/NativeInteractionController.h"
#include "app/NativeRenderSession.h"
#include "app/NativeWorldEditing.h"
#include "app/RenderStateSnapshots.h"
#include "app/genome_preview/GenomeFilePreviewSimulation.h"
#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "render/frame/RenderFrameTypes.h"

namespace cellularity::app::genome_preview {
namespace {

std::atomic<std::uint64_t> gNextGenomePreviewEndpointGeneration{1};

std::uint64_t acquireEndpointGeneration() noexcept {
    std::uint64_t candidate = gNextGenomePreviewEndpointGeneration.load(std::memory_order_relaxed);
    while (candidate != 0) {
        const std::uint64_t next = candidate == std::numeric_limits<std::uint64_t>::max() ? 0 : candidate + 1U;
        if (gNextGenomePreviewEndpointGeneration.compare_exchange_weak(candidate, next, std::memory_order_relaxed, std::memory_order_relaxed)) {
            return candidate;
        }
    }
    return 0;
}

cellularity::render::RenderFrameCaptureMetadata makePreviewCaptureMetadata(const NativeSimulationOwner::WriteAccess& access, cellularity::render::RenderCaptureStage stage, const NativeViewport& viewport) {
    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = access.worldEpoch();
    metadata.completedUpdateId = access.completedUpdateCount();
    metadata.environmentRevision = access.environmentRevision();
    metadata.stage = stage;
    const auto bounds = cellularity::app::toRenderBounds(viewport.viewBounds(0.0F));
    metadata.viewport = cellularity::app::snapshotViewport(viewport, bounds);
    return metadata;
}

} // namespace

GenomePreviewEndpoint::GenomePreviewEndpoint(std::uint64_t generation, int maximumCellCount)
        : mGeneration(generation), mFacade(std::make_unique<NativeGenomePreviewFacade>(maximumCellCount)) {
    if (generation == 0) {
        throw std::invalid_argument("Genome Preview endpoint generation is zero");
    }
    mAcknowledgment.endpointGeneration = generation;
}

GenomePreviewEndpoint::~GenomePreviewEndpoint() {
    close();
}

std::uint64_t GenomePreviewEndpoint::generation() const noexcept {
    return mGeneration;
}

std::uint64_t GenomePreviewEndpoint::surfaceCreated() noexcept {
    std::uint64_t previousAttachmentGeneration = 0;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mLifecycleMutex);
        if (mClosing || mNextAttachmentGeneration == 0) {
            return 0;
        }
        generation = mNextAttachmentGeneration;
        mNextAttachmentGeneration = generation == std::numeric_limits<std::uint64_t>::max() ? 0 : generation + 1U;
        previousAttachmentGeneration = mActiveAttachmentGeneration;
        mActiveAttachmentGeneration = generation;
        {
            std::lock_guard<std::mutex> acknowledgmentLock(mAcknowledgmentMutex);
            mAcknowledgment.attachmentGeneration = generation;
            mAcknowledgment.presentedContentRevision = 0;
            mAcknowledgment.presentedViewRevision = 0;
            mAcknowledgment.presentedInteractionToken = 0;
        }
        (void)mFrameRequest.request();
    }
    if (previousAttachmentGeneration != 0) {
        (void)mCaptureStore.cancelAttachment(previousAttachmentGeneration);
    }
    return generation;
}

void GenomePreviewEndpoint::surfaceDestroyed(std::uint64_t attachmentGeneration) noexcept {
    if (attachmentGeneration == 0) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mLifecycleMutex);
        if (mActiveAttachmentGeneration == attachmentGeneration) {
            mActiveAttachmentGeneration = 0;
            std::lock_guard<std::mutex> acknowledgmentLock(mAcknowledgmentMutex);
            mAcknowledgment.attachmentGeneration = 0;
            mAcknowledgment.presentedContentRevision = 0;
            mAcknowledgment.presentedViewRevision = 0;
            mAcknowledgment.presentedInteractionToken = 0;
        }
    }
    (void)mCaptureStore.cancelAttachment(attachmentGeneration);
}

std::uint64_t GenomePreviewEndpoint::activeAttachmentGeneration() const noexcept {
    std::lock_guard<std::mutex> lock(mLifecycleMutex);
    return mActiveAttachmentGeneration;
}

GenomePreviewStatePublishResult GenomePreviewEndpoint::publishContent(const std::uint8_t* genomeDraftBytes, std::size_t genomeDraftByteCount, double simulationTimeHours, std::uint64_t producerSequence, std::uint64_t expectedGeneration) {
    if (expectedGeneration == 0 || expectedGeneration != mGeneration) {
        return {GenomePreviewStatePublishStatus::Closing, 0};
    }
    const auto result = mRetainedState.publishContent(genomeDraftBytes, genomeDraftByteCount, simulationTimeHours, producerSequence);
    if (result.accepted()) {
        (void)mFrameRequest.request();
    }
    return result;
}

GenomePreviewStatePublishResult GenomePreviewEndpoint::publishViewState(const GenomePreviewViewUpdate& update, std::uint64_t producerSequence, std::uint64_t expectedGeneration) {
    if (expectedGeneration == 0 || expectedGeneration != mGeneration) {
        return {GenomePreviewStatePublishStatus::Closing, 0};
    }
    const auto result = mRetainedState.publishView(update, producerSequence);
    if (result.accepted()) {
        (void)mFrameRequest.request();
    }
    return result;
}

GenomePreviewInteractionEnqueueResult GenomePreviewEndpoint::enqueueInteraction(const GenomePreviewInteractionInput& input, std::uint64_t expectedGeneration) {
    if (expectedGeneration == 0 || expectedGeneration != mGeneration || input.controllerGeneration != mGeneration) {
        return {GenomePreviewInteractionEnqueueStatus::Closing, 0};
    }
    const auto result = mRetainedState.enqueueInteraction(mInteractionQueue, input);
    if (result.accepted()) {
        (void)mFrameRequest.request();
    }
    return result;
}

GenomePreviewCaptureAdmission GenomePreviewEndpoint::enqueueFileCapture(std::uint64_t requestId, const std::uint8_t* genomeDraftBytes, std::size_t genomeDraftByteCount, double maximumSimulationTime, std::uint64_t controllerGeneration, std::uint64_t attachmentGeneration) {
    std::lock_guard<std::mutex> lock(mLifecycleMutex);
    if (mClosing
            || controllerGeneration != mGeneration
            || attachmentGeneration == 0
            || attachmentGeneration != mActiveAttachmentGeneration) {
        return {GenomePreviewCaptureAdmissionStatus::Closing, nullptr};
    }
    auto result = mCaptureStore.enqueue(requestId, controllerGeneration, attachmentGeneration, genomeDraftBytes, genomeDraftByteCount, maximumSimulationTime);
    if (result.accepted()) {
        (void)mFrameRequest.request();
    }
    return result;
}

void GenomePreviewEndpoint::applyPendingOnRenderOwner(NativeCellularityController& controller) {
    const std::uint64_t attachmentGeneration = activeAttachmentGeneration();
    while (true) {
        auto command = mInteractionQueue.consumePendingOnRenderOwner(mGeneration, attachmentGeneration);
        if (!command.has_value()) {
            break;
        }
        applyFenceOnRenderOwner(controller, command->fence);
        applyInteractionOnRenderOwner(controller, *command);
        mAppliedInteractionToken = command->input.completionToken;
    }

    GenomePreviewPendingState pending;
    if (mRetainedState.consumePendingOnRenderOwner(pending)) {
        applyContentOnRenderOwner(controller, pending.content);
        applyViewOnRenderOwner(controller, pending.view);
    }
    executeCapturesOnRenderOwner(controller);
    (void)mFrameRequest.consumePendingOnRenderOwner();
}

cellularity::render::RenderFrameResult GenomePreviewEndpoint::draw(NativeCellularityController& controller) {
    applyPendingOnRenderOwner(controller);
    const std::uint64_t attachmentGeneration = activeAttachmentGeneration();
    auto access = controller.writeWorld();
    NativeWorldContext context{access.world(), controller.mInteraction->viewport()};
    const auto metadata = makePreviewCaptureMetadata(access, cellularity::render::RenderCaptureStage::GenomePreview, context.viewport);
    const auto result = mFacade->draw(context, controller.mRenderSession->surfaceSession(), metadata);
    publishPresentedAcknowledgment(attachmentGeneration, result);
    publishViewportAcknowledgment(controller);
    return result;
}

GenomePreviewAcknowledgment GenomePreviewEndpoint::readAcknowledgment() const noexcept {
    std::lock_guard<std::mutex> lock(mAcknowledgmentMutex);
    return mAcknowledgment;
}

void GenomePreviewEndpoint::close() noexcept {
    {
        std::lock_guard<std::mutex> lock(mLifecycleMutex);
        if (mClosing) {
            return;
        }
        mClosing = true;
        mActiveAttachmentGeneration = 0;
    }
    mFrameRequest.close();
    mInteractionQueue.close();
    mCaptureStore.close();
    mRetainedState.close();
    std::lock_guard<std::mutex> acknowledgmentLock(mAcknowledgmentMutex);
    mAcknowledgment.attachmentGeneration = 0;
    mAcknowledgment.presentedContentRevision = 0;
    mAcknowledgment.presentedViewRevision = 0;
    mAcknowledgment.presentedInteractionToken = 0;
}

void GenomePreviewEndpoint::applyFenceOnRenderOwner(NativeCellularityController& controller, const GenomePreviewStateFence& fence) {
    applyContentOnRenderOwner(controller, fence.content);
    applyViewOnRenderOwner(controller, fence.view);
}

void GenomePreviewEndpoint::applyContentOnRenderOwner(NativeCellularityController& controller, const std::shared_ptr<const GenomePreviewContentSnapshot>& content) {
    if (content == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mAcknowledgmentMutex);
        if (content->revision <= mAcknowledgment.appliedContentRevision) {
            return;
        }
    }

    auto access = controller.writeWorld();
    SimulationWorld& world = access.world();
    resetGenomePreviewWorld(world);
    if (!content->decodedGenome.empty()) {
        (void)addUserCellToWorld(world, 0.0, 0.0, content->decodedGenome);
    }
    NativeWorldContext context{world, controller.mInteraction->viewport()};
    mFacade->resetTreeState(context);
    world.mEnvironment.mRandom.setSeed(1337);
    int extra = 0;
    const int maximumTimeSteps = static_cast<int>(std::round(std::max(0.0, content->simulationTimeHours) / SimulationWorld::DELTA_TIME));
    for (int timestepIndex = 0; timestepIndex < maximumTimeSteps && extra <= 20; ++timestepIndex) {
        mFacade->beginTreeStep(context);
        access.update(false, 1.0);
        mFacade->recordTreeSplits(context);
        if (world.mCellData.mCellCount >= world.mEnvironment.maxCellCount) {
            ++extra;
        }
    }
    mFacade->beginTreeStep(context);
    access.update(true, 0.0);
    mFacade->recordTreeSplits(context);

    std::lock_guard<std::mutex> lock(mAcknowledgmentMutex);
    mAcknowledgment.appliedContentRevision = content->revision;
    mAcknowledgment.cellCountContentRevision = content->revision;
    mAcknowledgment.previewCellCount = world.mCellData.mCellCount;
}

void GenomePreviewEndpoint::applyViewOnRenderOwner(NativeCellularityController& controller, const std::shared_ptr<const GenomePreviewViewSnapshot>& view) {
    if (view == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mAcknowledgmentMutex);
        if (view->revision <= mAcknowledgment.appliedViewRevision) {
            return;
        }
    }
    mFacade->setGridEnabled(view->update.gridEnabled);
    mFacade->setSelectedGeneIndex(view->update.selectedGeneIndex);
    controller.setViewportPresentationSize(
            view->update.viewportWidth, view->update.viewportHeight);
    auto access = controller.writeWorld();
    NativeWorldContext context{access.world(), controller.mInteraction->viewport()};
    mFacade->setTreeEnabled(context, view->update.treeEnabled);
    controller.mInteraction->publishViewportIfChanged();
    std::lock_guard<std::mutex> lock(mAcknowledgmentMutex);
    mAcknowledgment.appliedViewRevision = view->revision;
}

void GenomePreviewEndpoint::applyInteractionOnRenderOwner(NativeCellularityController& controller, const GenomePreviewInteractionCommand& command) {
    std::visit(
            [&](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, GenomePreviewRestoreViewport>) {
                    controller.mInteraction->setViewportCamera(value.cameraX, value.cameraY, value.zoom);
                    publishViewportAcknowledgment(controller);
                } else if constexpr (std::is_same_v<Value, GenomePreviewMoveViewport>) {
                    controller.mInteraction->moveViewportByScreenDelta(value.deltaX, value.deltaY);
                    publishViewportAcknowledgment(controller);
                } else if constexpr (std::is_same_v<Value, GenomePreviewZoomViewport>) {
                    controller.mInteraction->zoomViewportAt(value.scale, value.centerX, value.centerY);
                    publishViewportAcknowledgment(controller);
                } else if constexpr (std::is_same_v<Value, GenomePreviewSelectGene>) {
                    const double worldX = controller.mInteraction->viewportScreenToWorldX(value.screenX);
                    const double worldY = controller.mInteraction->viewportScreenToWorldY(value.screenY);
                    auto access = controller.readWorld();
                    const int selected = mFacade->selectGeneAt(access.world(), worldX, worldY);
                    std::lock_guard<std::mutex> lock(mAcknowledgmentMutex);
                    mAcknowledgment.selectionToken = command.input.completionToken;
                    mAcknowledgment.selectedGeneIndex = selected;
                } else if constexpr (std::is_same_v<Value, GenomePreviewCaptureGhost>) {
                    auto access = controller.writeWorld();
                    NativeWorldContext context{access.world(), controller.mInteraction->viewport()};
                    const auto metadata = makePreviewCaptureMetadata(access, cellularity::render::RenderCaptureStage::GenomePreviewGhost, context.viewport);
                    mFacade->captureGhost(context, metadata);
                } else if constexpr (std::is_same_v<Value, GenomePreviewClearGhost>) {
                    mFacade->clearGhost();
                }
            },
            command.input.value);
}

void GenomePreviewEndpoint::executeCapturesOnRenderOwner(NativeCellularityController& controller) {
    const std::uint64_t attachmentGeneration = activeAttachmentGeneration();
    while (true) {
        const auto request = mCaptureStore.consumePendingOnRenderOwner(mGeneration, attachmentGeneration);
        if (request == nullptr) {
            break;
        }
        try {
            auto reservation = mCaptureStore.tryReserveOutput(kGenomePreviewMaximumRgbaBytes);
            if (!reservation.has_value()) {
                (void)request->fail();
            } else {
                auto bytes = mFacade->captureFilePreviewRgba(request->decodedGenome, request->maximumSimulationTime, controller.mRenderSession->surfaceSession());
                cellularity::app::preview::PreviewCaptureResultHeader header;
                header.requestId = request->requestId;
                header.status = cellularity::app::preview::PreviewCaptureStatus::Completed;
                header.width = static_cast<std::uint32_t>(cellularity::genome_preview::kGenomeFilePreviewPixels);
                header.height = static_cast<std::uint32_t>(cellularity::genome_preview::kGenomeFilePreviewPixels);
                header.rowStrideBytes = header.width * 4U;
                header.pixelFormat = cellularity::app::preview::PreviewCapturePixelFormat::Rgba8;
                header.origin = cellularity::app::preview::PreviewCaptureOrigin::TopLeft;
                header.alphaMode = cellularity::app::preview::PreviewCaptureAlphaMode::Straight;
                header.payloadBytes = bytes.size();
                auto& state = static_cast<cellularity::app::preview::PreviewCaptureRequestState&>(*request);
                (void)state.complete(header, std::move(bytes), std::move(*reservation));
            }
        } catch (...) {
            (void)request->fail();
        }
        (void)mCaptureStore.release(request->requestId);
    }
}

void GenomePreviewEndpoint::publishPresentedAcknowledgment(
        std::uint64_t attachmentGeneration,
        cellularity::render::RenderFrameResult result) noexcept {
    if (attachmentGeneration == 0
            || !cellularity::render::isPrimaryContentPresented(result)) {
        return;
    }
    std::lock_guard<std::mutex> lifecycleLock(mLifecycleMutex);
    if (mClosing || mActiveAttachmentGeneration != attachmentGeneration) {
        return;
    }
    std::lock_guard<std::mutex> acknowledgmentLock(mAcknowledgmentMutex);
    mAcknowledgment.presentedContentRevision =
            mAcknowledgment.appliedContentRevision;
    mAcknowledgment.presentedViewRevision = mAcknowledgment.appliedViewRevision;
    mAcknowledgment.presentedInteractionToken = mAppliedInteractionToken;
}

void GenomePreviewEndpoint::publishViewportAcknowledgment(NativeCellularityController& controller) {
    const auto state = controller.mInteraction->viewportState();
    std::lock_guard<std::mutex> lock(mAcknowledgmentMutex);
    mAcknowledgment.viewportRevision = controller.mInteraction->viewportRevision();
    mAcknowledgment.viewport = {state[0], state[1], state[2]};
}

std::shared_ptr<GenomePreviewEndpoint> createGenomePreviewEndpoint(int maximumCellCount) {
    const std::uint64_t generation = acquireEndpointGeneration();
    if (generation == 0) {
        throw std::overflow_error("Genome Preview endpoint generation exhausted");
    }
    return std::make_shared<GenomePreviewEndpoint>(generation, maximumCellCount);
}

} // namespace cellularity::app::genome_preview
