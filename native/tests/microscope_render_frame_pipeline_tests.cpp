#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/NativeCellularityController.h"
#include "app/NativeRenderSession.h"
#include "app/RenderStateSnapshots.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeInputController.h"
#include "app/microscope/NativeMicroscopeRuntime.h"
#include "app/microscope/render/MicroscopeRenderFramePipeline.h"
#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/EnvironmentBatch.h"
#include "core/RendererMemoryModel.h"
#include "formats/SubstrateFileCodec.h"
#include "io/NativeByteStream.h"
#include "render/NativeSimulationWorldRenderer.h"
#include "render/backend/gles/GlesRenderSurfaceSession.h"
#include "tests/support/MicroscopeRenderFramePipelineTestAccess.h"
#include "tests/support/RenderingTestDoubles.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

using cellularity::microscope::MicroscopeRenderFramePipeline;
using cellularity::microscope::RenderPreparationFailureKind;
using cellularity::microscope::RenderPreparationFailureStage;
using cellularity::microscope::RenderPresentationOutcome;
using cellularity::render::PreparedRenderFrame;
using cellularity::render::RenderFrameSlotState;
using cellularity::tests::assertTrue;
using cellularity::tests::FakeGlApi;
using cellularity::tests::FakeRenderAssets;
using cellularity::tests::FakeTextureLoader;
using cellularity::tests::MicroscopeRenderFramePipelineTestAccess;

namespace {

void installGlesBackend(NativeCellularityController& controller,
        NativeGlApi& gl) {
    controller.installRenderSurfaceSession(
            std::make_unique<cellularity::render::gles::GlesRenderSurfaceSession>(
                    gl));
}

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

class BlockingClearGlApi final : public FakeGlApi {
public:
    void clear(int mask) override {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mEntered = true;
            mCondition.notify_all();
            mCondition.wait(lock, [this] {
                return mReleased;
            });
        }
        FakeGlApi::clear(mask);
    }

    bool waitUntilBlocked(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCondition.wait_for(lock, timeout, [this] {
            return mEntered;
        });
    }

    void release() {
        const std::lock_guard<std::mutex> lock(mMutex);
        mReleased = true;
        mCondition.notify_all();
    }

private:
    std::mutex mMutex;
    std::condition_variable mCondition;
    bool mEntered = false;
    bool mReleased = false;
};

template <typename T>
bool vectorsHaveExactBytes(const std::vector<T>& left,
        const std::vector<T>& right) {
    return left.size() == right.size() && (left.empty() || std::memcmp(left.data(), right.data(), left.size() * sizeof(T)) == 0);
}

bool boundsEqual(const cellularity::render::RenderViewBounds& left,
        const cellularity::render::RenderViewBounds& right) {
    return left.minX == right.minX && left.maxX == right.maxX && left.minY == right.minY && left.maxY == right.maxY;
}

bool markersEqual(const std::vector<cellularity::render::RenderMarker>& left,
        const std::vector<cellularity::render::RenderMarker>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].x != right[index].x || left[index].y != right[index].y || left[index].radius != right[index].radius || left[index].alpha != right[index].alpha || left[index].selectedCellRuntimeId != right[index].selectedCellRuntimeId) {
            return false;
        }
    }
    return true;
}

bool preparedFramesEqual(const PreparedRenderFrame& left,
        const PreparedRenderFrame& right) {
    return left.geometryReady == right.geometryReady && left.identity.captureSequence == right.identity.captureSequence && left.identity.worldEpoch == right.identity.worldEpoch && left.identity.modeGeneration == right.identity.modeGeneration && left.identity.completedUpdateId == right.identity.completedUpdateId && left.identity.captureStage == right.identity.captureStage && left.environment.revision == right.environment.revision && left.environment.previewMode == right.environment.previewMode && left.environment.lightAngle == right.environment.lightAngle && left.environment.lightRange == right.environment.lightRange && left.environment.lightIntensity == right.environment.lightIntensity && left.environment.dishRadius == right.environment.dishRadius && left.environment.backgroundColor == right.environment.backgroundColor && left.viewport.revision == right.viewport.revision && left.viewport.coverageRevision == right.viewport.coverageRevision && boundsEqual(left.viewport.visibleBounds, right.viewport.visibleBounds) && boundsEqual(left.viewport.coverageBounds, right.viewport.coverageBounds) && left.viewport.mvpMatrix == right.viewport.mvpMatrix && left.viewport.cameraX == right.viewport.cameraX && left.viewport.cameraY == right.viewport.cameraY && left.viewport.zoom == right.viewport.zoom && left.viewport.viewWidth == right.viewport.viewWidth && left.viewport.viewHeight == right.viewport.viewHeight && left.viewport.surfaceWidth == right.viewport.surfaceWidth && left.viewport.surfaceHeight == right.viewport.surfaceHeight && left.resolvedInput.lastResolvedInputSequence == right.resolvedInput.lastResolvedInputSequence && left.resolvedInput.appliedWorldRevision == right.resolvedInput.appliedWorldRevision && left.layout.version == right.layout.version && left.layout.geometryRevision == right.layout.geometryRevision && left.layout.instanceRevision == right.layout.instanceRevision && left.layout.markerRevision == right.layout.markerRevision && left.interpolation.mode == right.interpolation.mode && left.interpolation.sourceCaptureSequence == right.interpolation.sourceCaptureSequence && left.interpolation.targetCaptureSequence == right.interpolation.targetCaptureSequence && left.interpolation.alpha == right.interpolation.alpha && left.diagnostics.buildNanoseconds == right.diagnostics.buildNanoseconds && left.diagnostics.sourceCellCount == right.diagnostics.sourceCellCount && left.diagnostics.sourceFoodCount == right.diagnostics.sourceFoodCount && left.diagnostics.sourceDeadCellCount == right.diagnostics.sourceDeadCellCount && left.diagnostics.sourceTopologyEdgeCount == right.diagnostics.sourceTopologyEdgeCount && left.diagnostics.visibleCellCount == right.diagnostics.visibleCellCount && left.diagnostics.normalizedContactChordCount == right.diagnostics.normalizedContactChordCount && markersEqual(left.markers, right.markers) && vectorsHaveExactBytes(left.cellTriangleCoords, right.cellTriangleCoords) && vectorsHaveExactBytes(left.cellTextureCoords, right.cellTextureCoords) && vectorsHaveExactBytes(left.cellCenterCoords, right.cellCenterCoords) && vectorsHaveExactBytes(left.cellColors, right.cellColors) && vectorsHaveExactBytes(left.cellIndices, right.cellIndices) && vectorsHaveExactBytes(left.foodInstanceAttributes, right.foodInstanceAttributes) && vectorsHaveExactBytes(left.foodVertexAttributes, right.foodVertexAttributes) && vectorsHaveExactBytes(left.deadCellInstanceAttributes, right.deadCellInstanceAttributes) && vectorsHaveExactBytes(left.deadCellVertexAttributes, right.deadCellVertexAttributes) && vectorsHaveExactBytes(left.linkTriangleCoords, right.linkTriangleCoords) && vectorsHaveExactBytes(left.linkTextureCoords, right.linkTextureCoords) && left.cellVertexCountBefore == right.cellVertexCountBefore && left.cellVertexCount == right.cellVertexCount && left.cellIndexCountBefore == right.cellIndexCountBefore && left.cellIndexCount == right.cellIndexCount && left.bunchStart == right.bunchStart && left.bunchSize == right.bunchSize && left.bunchIndexStart == right.bunchIndexStart && left.bunchIndexSize == right.bunchIndexSize && left.bunches == right.bunches && left.cellDrawVertexCount == right.cellDrawVertexCount && left.cellDrawIndexCount == right.cellDrawIndexCount && left.cellCenterFloatCount == right.cellCenterFloatCount && left.foodInstanceCount == right.foodInstanceCount && left.deadCellInstanceCount == right.deadCellInstanceCount && left.drawLinksCount == right.drawLinksCount;
}

std::unique_ptr<SimulationWorld> createWorld(int maxCellCount = 8) {
    Environment environment =
            Environment::constructDefaultEnvironment(maxCellCount, 4, 1.0);
    environment.mLightAngle = 0.25;
    environment.mLightRange = 0.45;
    environment.mLightIntensity = 0.75;
    auto world = std::make_unique<SimulationWorld>(false, environment);
    CellData& cells = world->mCellData;
    const int first =
            cellularity::tests::addAliveCell(cells, CellType::PHAGOCYTE);
    const int second =
            cellularity::tests::addAliveCell(cells, CellType::NEUROCYTE);
    cells.mPositionX[first] = -0.02;
    cells.mPositionY[first] = 0.0;
    cells.mRadius[first] = 0.02;
    cells.mPositionX[second] = 0.025;
    cells.mPositionY[second] = 0.0;
    cells.mRadius[second] = 0.018;
    world->mFoodData.addFood(0.0, 0.04, Environment::MAX_FOOD_SIZE, 0.2f,
            world->mEnvironment);
    world->updateGrids();
    return world;
}

cellularity::render::RenderFrameCaptureMetadata
captureMetadata(std::uint64_t worldEpoch,
        const cellularity::render::RenderViewBounds& visible,
        const cellularity::render::RenderViewBounds& coverage,
        std::uint64_t modeGeneration = 1) {
    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = worldEpoch;
    metadata.modeGeneration = modeGeneration;
    metadata.completedUpdateId = 17;
    metadata.stage =
            cellularity::render::RenderCaptureStage::AfterCompletedUpdate;
    metadata.environmentRevision = 3;
    metadata.resolvedInput.lastResolvedInputSequence = 23;
    metadata.resolvedInput.appliedWorldRevision = 11;
    metadata.viewport.revision = 5;
    metadata.viewport.coverageRevision = 5;
    metadata.viewport.visibleBounds = visible;
    metadata.viewport.coverageBounds = coverage;
    metadata.viewport.cameraX = 0.1f;
    metadata.viewport.cameraY = -0.2f;
    metadata.viewport.zoom = 1.25f;
    metadata.viewport.viewWidth = 0.4f;
    metadata.viewport.viewHeight = 0.3f;
    metadata.viewport.surfaceWidth = 320;
    metadata.viewport.surfaceHeight = 240;
    return metadata;
}

cellularity::render::RenderPresentationState presentationForBounds(
        std::uint64_t revision, float matrixMarker,
        const std::array<float, 3>& background,
        const cellularity::render::RenderViewBounds& visibleBounds) {
    cellularity::render::RenderPresentationState state;
    state.viewport.revision = revision;
    state.viewport.visibleBounds = visibleBounds;
    state.viewport.coverageBounds = state.viewport.visibleBounds;
    state.viewport.mvpMatrix = cellularity::app::identityMatrix();
    state.viewport.mvpMatrix[0] = matrixMarker;
    state.viewport.cameraX = matrixMarker * 0.1f;
    state.viewport.cameraY = matrixMarker * -0.1f;
    state.viewport.zoom = matrixMarker;
    state.viewport.viewWidth = 0.2f;
    state.viewport.viewHeight = 0.16f;
    state.viewport.surfaceWidth = 320;
    state.viewport.surfaceHeight = 240;
    state.backgroundColor = background;
    return state;
}

cellularity::render::RenderPresentationState
presentation(std::uint64_t revision, float matrixMarker,
        const std::array<float, 3>& background) {
    return presentationForBounds(revision, matrixMarker, background,
            {-0.1f, 0.1f, -0.08f, 0.08f});
}

template <std::size_t SlotCount>
std::size_t countState(
        const cellularity::render::RenderFrameExchangeStorageSnapshot<SlotCount>& snapshot,
        RenderFrameSlotState state) {
    return static_cast<std::size_t>(
            std::count_if(snapshot.slots.begin(), snapshot.slots.end(),
                    [state](const auto& slot) {
                        return slot.state == state;
                    }));
}

void testWorkerOwnershipTransitions() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};
    const auto metadata = captureMetadata(7, view, view);

    auto state = pipeline.snapshotWhenQuiescent();
    assertTrue(
            state.workerRunning && countState(state.sourceStorage, RenderFrameSlotState::Writing) == 1 && countState(state.preparedStorage, RenderFrameSlotState::Writing) == 1,
            "Pipeline starts one worker with one producer-owned slot per exchange");

    assertTrue(pipeline.captureAndPrepare(*world, view, metadata) == 2,
            "Synchronous adapter waits for the dedicated worker");
    state = pipeline.snapshotWhenQuiescent();
    assertTrue(
            state.sourceCounters.publishedFrameCount == 1 && state.sourceCounters.acquiredFrameCount == 1 && countState(state.sourceStorage, RenderFrameSlotState::Free) == 3,
            "Worker releases its immutable source before reporting completion");
    assertTrue(
            state.preparedCounters.publishedFrameCount == 1 && countState(state.preparedStorage, RenderFrameSlotState::Published) == 1 && pipeline.preparedFrame() == nullptr,
            "Worker publishes complete geometry without touching the GL consumer");

    const auto* first = pipeline.presentLatestPreparedFrame(
            presentation(9, 1.5f, {0.7f, 0.8f, 0.9f}));
    assertTrue(first != nullptr && first->valid && first->geometryReady,
            "GL consumer acquires only a fully complete frame");
    const std::uint64_t firstSequence = first->identity.captureSequence;

    pipeline.captureAndPrepare(*world, view, metadata);
    state = pipeline.snapshotWhenQuiescent();
    assertTrue(
            countState(state.preparedStorage, RenderFrameSlotState::Reading) == 1 && countState(state.preparedStorage, RenderFrameSlotState::Published) == 1,
            "Worker cannot overwrite the frame retained by the GL consumer");
    const auto* second = pipeline.acquireLatestPreparedFrame();
    state = pipeline.snapshotWhenQuiescent();
    assertTrue(second != nullptr && second->identity.captureSequence > firstSequence && countState(state.preparedStorage, RenderFrameSlotState::Reading) == 1 && state.sourceCounters.droppedFrameCount() == 0 && state.preparedCounters.droppedFrameCount() == 0,
            "Sequential worker hand-off advances monotonically without loss");
}

void testWorkerOutputMatchesSynchronousReference() {
    auto world = createWorld();
    const cellularity::render::RenderViewBounds visible{-0.12f, 0.12f, -0.1f,
            0.1f};
    const cellularity::render::RenderViewBounds coverage{-0.2f, 0.2f, -0.18f,
            0.18f};
    const auto metadata = captureMetadata(19, visible, coverage);

    NativeSimulationWorldRenderer synchronous(world->mEnvironment.maxCellCount);
    MicroscopeRenderFramePipeline worker(world->mEnvironment.maxCellCount);
    const int synchronousVisible =
            synchronous.copyToDraw(*world, coverage, metadata);
    const int workerVisible =
            worker.captureAndPrepare(*world, coverage, metadata);
    const PreparedRenderFrame& reference = synchronous.preparedFrame();
    const PreparedRenderFrame* candidate = worker.acquireLatestPreparedFrame();

    assertTrue(synchronousVisible == workerVisible,
            "Worker and synchronous paths return the same visible count");
    assertTrue(candidate != nullptr && candidate->valid && !reference.valid && preparedFramesEqual(reference, *candidate),
            "Every complete worker keyframe is byte-exact to the synchronous "
            "geometry reference");
}

void testCompletedCompatibleBuildPublishesBeforeNewestSource() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};

    MicroscopeRenderFramePipelineTestAccess::setPauseAfterBuild(pipeline, true);
    auto first = captureMetadata(29, view, view, 4);
    first.completedUpdateId = 1;
    first.resolvedInput.lastResolvedInputSequence = 1;
    first.resolvedInput.appliedWorldRevision = 1;
    pipeline.capture(*world, view, first);
    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::waitUntilPausedAfterBuild(
                    pipeline),
            "Worker deterministically pauses after completing the first build");

    auto newest = captureMetadata(29, view, view, 4);
    newest.completedUpdateId = 2;
    newest.resolvedInput.lastResolvedInputSequence = 2;
    newest.resolvedInput.appliedWorldRevision = 2;
    pipeline.capture(*world, view, newest);
    const auto whilePaused = pipeline.snapshotWhenQuiescent();
    MicroscopeRenderFramePipelineTestAccess::setPauseAfterBuild(pipeline, false);

    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::waitUntilSourcePrepared(
                    pipeline, whilePaused.latestAcceptedSourceSequence),
            "Worker immediately prepares newest source after publishing completed "
            "compatible work");
    const auto* presented = pipeline.presentLatestPreparedFrame(
            presentation(30, 1.0f, {0.2f, 0.3f, 0.4f}));
    const auto completed = pipeline.snapshotWhenQuiescent();
    assertTrue(completed.preparedCounters.publishedFrameCount == 2 && completed.supersededBuildCount == 0 && presented != nullptr && presented->identity.completedUpdateId == 2 && completed.latestPublishedSourceSequence == completed.latestAcceptedSourceSequence,
            "A newer same-identity source cannot discard an already completed "
            "frame, and newest source follows without replay");
}

void testBlockedWorkerCoalescesToNewestSource() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};
    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline, true),
            "Test gate pauses preparation outside all producer locks");

    constexpr std::uint64_t kCaptureCount = 64;
    for (std::uint64_t index = 1; index <= kCaptureCount; ++index) {
        auto metadata = captureMetadata(31, view, view);
        metadata.completedUpdateId = index;
        metadata.resolvedInput.lastResolvedInputSequence = index;
        metadata.resolvedInput.appliedWorldRevision = index;
        metadata.viewport.revision = index;
        metadata.viewport.coverageRevision = index;
        world->mCellData.mPositionX[0] += 0.0001;
        world->updateGrids();
        assertTrue(pipeline.capture(*world, view, metadata) == 2,
                "Paused worker never blocks source capture");
    }

    const auto blocked = pipeline.snapshotWhenQuiescent();
    const auto liveCounters = pipeline.exchangeTelemetry();
    assertTrue(
            blocked.workerRunning && blocked.workerIdle && blocked.sourceCounters.publishedFrameCount == kCaptureCount && blocked.sourceCounters.droppedFrameCount() > 0 && blocked.preparedCounters.publishedFrameCount == 0 && liveCounters.source.publishedFrameCount == blocked.sourceCounters.publishedFrameCount && liveCounters.source.droppedFrameCount() == blocked.sourceCounters.droppedFrameCount() && liveCounters.prepared.publishedFrameCount == 0 && blocked.sourceStorage.slots.size() == 3 && blocked.preparedStorage.slots.size() == 3,
            "Blocked worker keeps fixed capacity while source publications coalesce");

    assertTrue(MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline,
                       false),
            "Preparation worker resumes");
    assertTrue(MicroscopeRenderFramePipelineTestAccess::waitUntilSourcePrepared(
                       pipeline, blocked.latestAcceptedSourceSequence),
            "Worker completes the newest retained source after resume");
    const auto* frame = pipeline.presentLatestPreparedFrame(
            presentation(80, 2.0f, {0.3f, 0.4f, 0.5f}));
    const auto resumed = pipeline.snapshotWhenQuiescent();
    assertTrue(
            frame != nullptr && frame->resolvedInput.lastResolvedInputSequence == kCaptureCount && frame->resolvedInput.appliedWorldRevision == kCaptureCount && frame->identity.completedUpdateId == kCaptureCount && resumed.sourceCounters.acquiredFrameCount == 1 && resumed.latestPublishedSourceSequence == resumed.latestAcceptedSourceSequence,
            "Resume builds only newest state and never replays the source backlog");
}

void testBlockedGlConsumerKeepsWorkerBounded() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};

    auto firstMetadata = captureMetadata(41, view, view);
    firstMetadata.resolvedInput.lastResolvedInputSequence = 1;
    firstMetadata.resolvedInput.appliedWorldRevision = 1;
    pipeline.captureAndPrepare(*world, view, firstMetadata);
    const auto* retained = pipeline.presentLatestPreparedFrame(
            presentation(42, 1.0f, {0.2f, 0.3f, 0.4f}));
    assertTrue(retained != nullptr, "GL consumer retains the first frame");

    constexpr std::uint64_t kNewestRevision = 40;
    for (std::uint64_t revision = 2; revision <= kNewestRevision; ++revision) {
        auto metadata = captureMetadata(41, view, view);
        metadata.completedUpdateId = revision;
        metadata.resolvedInput.lastResolvedInputSequence = revision;
        metadata.resolvedInput.appliedWorldRevision = revision;
        pipeline.captureAndPrepare(*world, view, metadata);
    }

    const auto blocked = pipeline.snapshotWhenQuiescent();
    assertTrue(
            blocked.preparedCounters.publishedFrameCount == kNewestRevision && blocked.preparedCounters.droppedFrameCount() > 0 && countState(blocked.preparedStorage, RenderFrameSlotState::Reading) == 1 && blocked.preparedStorage.slots.size() == 3,
            "A stalled GL consumer cannot stop worker publication or grow storage");

    const auto* newest = pipeline.presentLatestPreparedFrame(
            presentation(43, 1.1f, {0.25f, 0.35f, 0.45f}));
    assertTrue(
            newest != nullptr && newest->resolvedInput.lastResolvedInputSequence == kNewestRevision && newest->resolvedInput.appliedWorldRevision == kNewestRevision,
            "First draw after a GL stall skips directly to newest prepared state");
}

void testEpochAndInputRollbackBarriers() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};

    auto initial = captureMetadata(51, view, view);
    initial.resolvedInput.lastResolvedInputSequence = 10;
    initial.resolvedInput.appliedWorldRevision = 10;
    pipeline.captureAndPrepare(*world, view, initial);
    const auto* first = pipeline.presentLatestPreparedFrame(
            presentation(100, 1.25f, {0.2f, 0.3f, 0.4f}));
    assertTrue(first != nullptr, "Initial epoch frame is presentable");
    const auto* firstAddress = first;
    const auto capturedViewport = first->viewport;

    auto rollback = captureMetadata(51, view, view);
    rollback.resolvedInput.lastResolvedInputSequence = 9;
    rollback.resolvedInput.appliedWorldRevision = 9;
    pipeline.captureAndPrepare(*world, view, rollback);
    const auto* afterRollback = pipeline.presentLatestPreparedFrame(
            presentation(99, 0.5f, {0.8f, 0.1f, 0.1f}));
    assertTrue(
            afterRollback == firstAddress && pipeline.latestPresentation().viewport.revision == 100 && afterRollback->viewport.revision == capturedViewport.revision && pipeline.snapshotWhenQuiescent().rejectedInputRollbackCount == 1,
            "Older input and presentation revisions cannot roll the display back");

    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline, true),
            "Worker pauses before the epoch-barrier source");
    pipeline.invalidateWorldEpoch(52);
    assertTrue(
            pipeline.presentLatestPreparedFrame(
                    presentation(101, 1.5f, {0.4f, 0.5f, 0.6f}))
                    == nullptr,
            "Explicit replacement barrier hides foreign geometry before capture");
    auto replacement = captureMetadata(52, view, view);
    replacement.resolvedInput.lastResolvedInputSequence = 11;
    replacement.resolvedInput.appliedWorldRevision = 11;
    pipeline.capture(*world, view, replacement);
    const auto replacementState = pipeline.snapshotWhenQuiescent();
    assertTrue(pipeline.presentLatestPreparedFrame(
                       presentation(101, 1.5f, {0.4f, 0.5f, 0.6f}))
                    == nullptr,
            "Pending replacement source cannot expose partial geometry");
    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline,
                    false)
                    && MicroscopeRenderFramePipelineTestAccess::waitUntilSourcePrepared(
                            pipeline, replacementState.latestAcceptedSourceSequence),
            "Replacement epoch preparation completes after resume");
    const auto* replacementFrame = pipeline.presentLatestPreparedFrame(
            presentation(102, 1.75f, {0.5f, 0.6f, 0.7f}));
    assertTrue(replacementFrame != nullptr && replacementFrame->identity.worldEpoch == 52 && replacementFrame->resolvedInput.appliedWorldRevision == 11,
            "Only a complete current-epoch frame crosses the barrier");
}

void testModeGenerationBarrierRejectsLateFrames() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};

    auto firstMetadata = captureMetadata(56, view, view, 10);
    firstMetadata.resolvedInput.lastResolvedInputSequence = 10;
    pipeline.captureAndPrepare(*world, view, firstMetadata);
    const auto* first = pipeline.presentLatestPreparedFrame(
            presentation(110, 1.0f, {0.2f, 0.3f, 0.4f}));
    assertTrue(first != nullptr && first->identity.modeGeneration == 10,
            "Initial mode generation is presentable");

    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline, true),
            "Worker pauses before a late old-mode source");
    auto lateOldMode = captureMetadata(56, view, view, 10);
    lateOldMode.resolvedInput.lastResolvedInputSequence = 11;
    pipeline.capture(*world, view, lateOldMode);
    pipeline.invalidateModeGeneration(11);
    assertTrue(pipeline.presentLatestPreparedFrame(
                       presentation(111, 1.1f, {0.3f, 0.4f, 0.5f}))
                    == nullptr,
            "Mode transition immediately hides the prior generation");

    const auto beforeRejectedCapture = pipeline.snapshotWhenQuiescent();
    pipeline.capture(*world, view, lateOldMode);
    const auto afterRejectedCapture = pipeline.snapshotWhenQuiescent();
    assertTrue(afterRejectedCapture.latestAcceptedSourceSequence == beforeRejectedCapture.latestAcceptedSourceSequence && afterRejectedCapture.rejectedModeGenerationCount == beforeRejectedCapture.rejectedModeGenerationCount + 1,
            "A late source cannot roll the required mode generation back");

    assertTrue(MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline,
                       false),
            "Worker resumes after the mode barrier");
    auto replacement = captureMetadata(56, view, view, 11);
    replacement.resolvedInput.lastResolvedInputSequence = 12;
    pipeline.captureAndPrepare(*world, view, replacement);
    const auto* second = pipeline.presentLatestPreparedFrame(
            presentation(112, 1.2f, {0.4f, 0.5f, 0.6f}));
    assertTrue(
            second != nullptr && second->identity.modeGeneration == 11 && second->resolvedInput.lastResolvedInputSequence == 12,
            "Only the complete replacement mode generation crosses the barrier");

    pipeline.invalidateModeGeneration(12);
    assertTrue(
            pipeline.presentLatestPreparedFrame(
                    presentation(113, 1.3f, {0.5f, 0.6f, 0.7f}))
                    == nullptr,
            "Returning to a prior mode ordinal still requires a new generation");
    auto returnedMode = captureMetadata(56, view, view, 12);
    returnedMode.resolvedInput.lastResolvedInputSequence = 13;
    pipeline.captureAndPrepare(*world, view, returnedMode);
    const auto* third = pipeline.presentLatestPreparedFrame(
            presentation(114, 1.4f, {0.6f, 0.7f, 0.8f}));
    assertTrue(third != nullptr && third->identity.modeGeneration == 12,
            "A -> B -> A semantics cannot reuse the first A frame");
}

void testControlledPreparationFailureRetainsOnlyCompleteCoverage() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds coverage{-0.3f, 0.3f, -0.3f,
            0.3f};
    auto metadata = captureMetadata(57, coverage, coverage, 20);
    pipeline.captureAndPrepare(*world, coverage, metadata);
    const auto* retained = pipeline.presentLatestPreparedFrame(
            presentation(120, 1.0f, {0.2f, 0.3f, 0.4f}));
    assertTrue(retained != nullptr && retained->valid && retained->geometryReady,
            "Failure test starts with one complete retained frame");
    const auto beforeFailure = pipeline.snapshotWhenQuiescent();

    MicroscopeRenderFramePipelineTestAccess::failNextPreparation(
            pipeline, RenderPreparationFailureKind::Allocation);
    auto failingMetadata = captureMetadata(57, coverage, coverage, 20);
    failingMetadata.completedUpdateId = 18;
    pipeline.capture(*world, coverage, failingMetadata);
    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!pipeline.failure() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }

    const auto* afterFailure = pipeline.presentLatestPreparedFrame(
            presentation(121, 1.1f, {0.3f, 0.4f, 0.5f}));
    const auto failed = pipeline.snapshotWhenQuiescent();
    assertTrue(
            afterFailure == retained && failed.failure.kind == RenderPreparationFailureKind::Allocation && failed.failure.stage == RenderPreparationFailureStage::FrameBuild && failed.preparedCounters.publishedFrameCount == beforeFailure.preparedCounters.publishedFrameCount && failed.presentationOutcome == RenderPresentationOutcome::FailureRetainedCompleteFrame,
            "Worker failure exposes state and retains the last complete frame only");

    bool productionCaptureThrew = false;
    try {
        pipeline.capture(*world, coverage, metadata);
    } catch (...) {
        productionCaptureThrew = true;
    }
    assertTrue(!productionCaptureThrew,
            "Production capture remains controlled after worker failure");

    bool blockingCaptureThrew = false;
    try {
        pipeline.captureAndPrepare(*world, coverage, metadata);
    } catch (const std::runtime_error&) {
        blockingCaptureThrew = true;
    }
    assertTrue(blockingCaptureThrew,
            "Blocking capture helper still reports worker failure");

    const cellularity::render::RenderViewBounds uncovered{0.5f, 0.7f, 0.5f, 0.7f};
    assertTrue(
            pipeline.presentLatestPreparedFrame(presentationForBounds(
                    122, 1.2f, {0.4f, 0.5f, 0.6f}, uncovered))
                            == retained
                    && pipeline.preparedFrame() == retained && pipeline.snapshotWhenQuiescent().presentationOutcome == RenderPresentationOutcome::FailureRetainedCompleteFrame,
            "Failure keeps the last complete same-identity geometry instead of "
            "flashing a wholly empty scene outside captured coverage");
}

void testAllFixedSlotsReportBoundedStorageAndStopReset() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};
    std::array<std::uint64_t, cellularity::render::kRenderFrameExchangeSlotCount>
            sourceRetainedHighWater{};
    std::array<std::uint64_t, cellularity::render::kRenderFrameExchangeSlotCount>
            preparedRetainedHighWater{};

    for (std::uint64_t frame = 0; frame < 6; ++frame) {
        world->mCellData.mPositionX[0] += 0.001;
        world->updateGrids();
        pipeline.captureAndPrepare(*world, view, captureMetadata(61, view, view));
        const auto snapshot = pipeline.snapshotWhenQuiescent();
        for (std::size_t slot = 0; slot < sourceRetainedHighWater.size(); ++slot) {
            sourceRetainedHighWater[slot] =
                    std::max(sourceRetainedHighWater[slot],
                            snapshot.sourceStorage.slots[slot].retainedStorageBytes);
            preparedRetainedHighWater[slot] =
                    std::max(preparedRetainedHighWater[slot],
                            snapshot.preparedStorage.slots[slot].retainedStorageBytes);
        }
    }

    const auto beforeReset = pipeline.snapshotWhenQuiescent();
    cellularity::memory::RendererMemoryEstimate memoryEstimate;
    assertTrue(
            cellularity::memory::tryEstimateRendererMemory(
                    world->mEnvironment.maxCellCount, world->mEnvironment.maxFoodCount,
                    sizeof(void*) * CHAR_BIT, memoryEstimate)
                    && beforeReset.sourceStorage.totalRetainedStorageBytes + beforeReset.preparedStorage.totalRetainedStorageBytes <= memoryEstimate.maximumReusablePayloadBytes,
            "Checked renderer memory model covers all live worker slots");
    assertTrue(
            std::all_of(
                    sourceRetainedHighWater.begin(), sourceRetainedHighWater.end(),
                    [](std::uint64_t bytes) {
                        return bytes > sizeof(cellularity::render::RenderSourceFrame);
                    })
                    && std::all_of(preparedRetainedHighWater.begin(),
                            preparedRetainedHighWater.end(),
                            [](std::uint64_t bytes) {
                                return bytes > sizeof(PreparedRenderFrame);
                            }),
            "Live atomic telemetry observes retained storage in all six slots");

    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline, true),
            "Worker can be stopped from a condition-variable pause");
    pipeline.capture(*world, view, captureMetadata(62, view, view));
    pipeline.reset(4);
    const auto afterReset = pipeline.snapshotWhenQuiescent();
    assertTrue(
            afterReset.workerRunning && afterReset.sourceCounters.publishedFrameCount == 0 && afterReset.preparedCounters.publishedFrameCount == 0 && countState(afterReset.sourceStorage, RenderFrameSlotState::Writing) == 1 && countState(afterReset.preparedStorage, RenderFrameSlotState::Writing) == 1 && !afterReset.hasCurrentPreparedFrame && !afterReset.hasPresentation && !afterReset.failure && pipeline.visibleCellCount() == 0,
            "Reset wakes and joins the old worker before replacing all epoch state");
    assertTrue(afterReset.sourceStorage.totalRetainedStorageBytes + afterReset.preparedStorage.totalRetainedStorageBytes < beforeReset.sourceStorage.totalRetainedStorageBytes + beforeReset.preparedStorage.totalRetainedStorageBytes,
            "Reset to a smaller capacity releases exchange high-water");
}

bool drawSessionUntilReady(NativeRenderSession& session,
        const NativeViewport& viewport,
        const std::array<float, 3>& background) {
    for (int attempt = 0; attempt < 10000; ++attempt) {
        if (cellularity::render::isPrimaryContentPresented(
                    session.drawMicroscope(viewport, background))) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

void testFirstFrameClearAndSurfaceContextRecreation() {
    auto world = createWorld();
    NativeViewport viewport(10.0f);
    viewport.setSurfaceSize(320, 240);
    viewport.setCamera(0.0f, 0.0f, 5.0f);

    FakeGlApi gl;
    FakeRenderAssets assets;
    FakeTextureLoader textures;
    NativeRenderSession session(world->mEnvironment.maxCellCount,
            std::make_unique<cellularity::render::gles::GlesRenderSurfaceSession>(
                    gl));
    session.loadResources(*world, assets, textures);
    const std::size_t initialDrawCount = gl.drawEvents.size();
    const std::size_t initialUploadCount =
            gl.bufferDataCalls.size() + gl.bufferSubDataCalls.size();
    const auto firstFrame =
            session.drawMicroscope(viewport, {0.2f, 0.3f, 0.4f});
    assertTrue(
            firstFrame.outcome == cellularity::render::RenderFrameOutcome::Presented && firstFrame.flags == cellularity::render::kFallbackClearPresented && gl.clearCount == 1 && gl.lastClearMask == (gl.colorBufferBit() | gl.depthBufferBit()) && gl.lastViewport == std::array<int, 4>{0, 0, 320, 240} && gl.drawEvents.size() == initialDrawCount && gl.bufferDataCalls.size() + gl.bufferSubDataCalls.size() == initialUploadCount,
            "Before the first complete frame GL performs clear-only presentation");

    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 68;
    metadata.modeGeneration = 1;
    metadata.stage = cellularity::render::RenderCaptureStage::StableWorld;
    session.captureMicroscopeFrame(*world, viewport, 0.2f, metadata);
    assertTrue(drawSessionUntilReady(session, viewport, {0.2f, 0.3f, 0.4f}),
            "First complete async frame becomes drawable");
    const auto* retained = session.preparedFrame();
    assertTrue(retained != nullptr && retained->valid && retained->geometryReady,
            "Surface test retains one complete CPU frame");
    const std::uint64_t retainedSequence = retained->identity.captureSequence;

    session.abandonResources();
    assertTrue(!session.hasResources() && !session.hasBackendGpuBuffers(),
            "Context loss abandons only GL-owned state");
    session.loadResources(*world, assets, textures);
    const std::size_t uploadsBeforeRecreatedDraw =
            gl.bufferDataCalls.size() + gl.bufferSubDataCalls.size();
    const std::size_t drawsBeforeRecreatedDraw = gl.drawEvents.size();
    viewport.moveByScreenDelta(4.0f, -3.0f);
    assertTrue(
            cellularity::render::isPrimaryContentPresented(session.drawMicroscope(viewport, {0.4f, 0.5f, 0.6f})) && session.preparedFrame() == retained && retained->identity.captureSequence == retainedSequence && gl.bufferDataCalls.size() + gl.bufferSubDataCalls.size() > uploadsBeforeRecreatedDraw && gl.drawEvents.size() > drawsBeforeRecreatedDraw,
            "Recreated GL context reuploads and draws the retained complete frame");
}

void testBlockedPanZoomUsesCoverageAndUrgentLatestWinsCapture() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    NativeViewport viewport(10.0f);
    viewport.setSurfaceSize(320, 240);
    viewport.setCamera(0.0f, 0.0f, 5.0f);
    MicroscopeInputController input;

    auto visible = cellularity::app::toRenderBounds(viewport.viewBounds(0.0f));
    float margin = std::max(viewport.viewWidth(), viewport.viewHeight()) * 0.1f;
    auto coverage = cellularity::app::toRenderBounds(viewport.viewBounds(margin));
    auto initial = captureMetadata(69, visible, coverage, 30);
    initial.viewport = cellularity::app::snapshotViewport(viewport, coverage);
    pipeline.captureAndPrepare(*world, coverage, initial);
    const auto* retained =
            pipeline.presentLatestPreparedFrame(presentationForBounds(
                    viewport.revision(), 1.0f, {0.2f, 0.3f, 0.4f}, visible));
    assertTrue(retained != nullptr,
            "Pan/Zoom stress starts with a complete frame");

    assertTrue(input.queuePan(viewport, false, 0, false, 4.0f, -3.0f),
            "Small production Pan input is accepted");
    visible = cellularity::app::toRenderBounds(viewport.viewBounds(0.0f));
    const auto* overscanFrame =
            pipeline.presentLatestPreparedFrame(presentationForBounds(
                    viewport.revision(), 2.0f, {0.3f, 0.4f, 0.5f}, visible));
    assertTrue(overscanFrame == retained && pipeline.snapshotWhenQuiescent().urgentCaptureRequestCount == 0,
            "Measured overscan covers an ordinary Pan without a recapture");

    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline, true),
            "Pan/Zoom stress blocks the preparation worker");
    constexpr std::uint64_t kGestureCount = 600;
    for (std::uint64_t gesture = 1; gesture <= kGestureCount; ++gesture) {
        assertTrue(input.queuePan(viewport, false, 0, false, 1.5f, -0.75f),
                "High-rate Pan input remains accepted");
        const float zoomFactor = gesture % 2 == 0 ? 1.0004f : 0.9998f;
        assertTrue(
                input.queueZoom(viewport, false, 0, false, zoomFactor, 160.0f, 120.0f),
                "High-rate Zoom input remains accepted");
        visible = cellularity::app::toRenderBounds(viewport.viewBounds(0.0f));
        margin = std::max(viewport.viewWidth(), viewport.viewHeight()) * 0.1f;
        coverage = cellularity::app::toRenderBounds(viewport.viewBounds(margin));
        auto metadata = captureMetadata(69, visible, coverage, 30);
        metadata.completedUpdateId = gesture;
        metadata.resolvedInput.lastResolvedInputSequence = gesture;
        metadata.resolvedInput.appliedWorldRevision = gesture;
        metadata.viewport = cellularity::app::snapshotViewport(viewport, coverage);
        pipeline.capture(*world, coverage, metadata);
    }

    for (int draw = 0; draw < 8; ++draw) {
        assertTrue(pipeline.presentLatestPreparedFrame(presentationForBounds(
                           viewport.revision(), 9.0f, {0.5f, 0.6f, 0.7f}, visible))
                        == retained,
                "Uncovered camera retains complete compatible geometry while "
                "worker is blocked");
    }
    const auto blocked = pipeline.snapshotWhenQuiescent();
    assertTrue(
            blocked.sourceStorage.slots.size() == 3 && blocked.preparedStorage.slots.size() == 3 && blocked.latestAcceptedSourceSequence > 0 && blocked.coverageMissCount == 8 && blocked.urgentCaptureRequestCount == 1 && blocked.urgentCaptureCoalescedCount == 7 && blocked.urgentCapturePending && blocked.urgentCaptureViewportRevision == viewport.revision() && blocked.presentationOutcome == RenderPresentationOutcome::CoverageMissRetainedCompleteFrame,
            "Blocked Pan/Zoom remains fixed-capacity and coalesces urgent capture");
    assertTrue(pipeline.consumeUrgentCaptureRequest() && !pipeline.consumeUrgentCaptureRequest(),
            "Simulation owner consumes exactly one urgent request");

    assertTrue(
            MicroscopeRenderFramePipelineTestAccess::pausePreparation(pipeline,
                    false)
                    && MicroscopeRenderFramePipelineTestAccess::waitUntilSourcePrepared(
                            pipeline, blocked.latestAcceptedSourceSequence),
            "Worker resumes directly at the newest coalesced gesture source");
    const auto* newest =
            pipeline.presentLatestPreparedFrame(presentationForBounds(
                    viewport.revision(), 10.0f, {0.6f, 0.7f, 0.8f}, visible));
    const auto resumed = pipeline.snapshotWhenQuiescent();
    assertTrue(newest != nullptr && newest->resolvedInput.lastResolvedInputSequence == kGestureCount && newest->resolvedInput.appliedWorldRevision == kGestureCount && newest->viewport.revision == viewport.revision() && newest->identity.modeGeneration == 30 && pipeline.latestPresentation().viewport.mvpMatrix[0] == 10.0f && resumed.latestPublishedSourceSequence == resumed.latestAcceptedSourceSequence && resumed.urgentCaptureConsumedCount == 1 && !resumed.urgentCapturePending,
            "First draw after resume uses newest frame and camera without "
            "FIFO replay");
}

void testControllerCoverageMissRecapturesActualGestureCamera() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(320, 240);
    controller.setViewportCamera(0.0f, 0.0f, 5.0f);
    {
        auto access = controller.writeWorld();
        const int cell = cellularity::tests::addAliveCell(access.world().mCellData,
                CellType::PHAGOCYTE);
        access.world().mCellData.mPositionX[cell] = 0.0;
        access.world().mCellData.mPositionY[cell] = 0.0;
        access.world().mCellData.mRadius[cell] = 0.02;
        access.world().updateGrids();
    }

    FakeGlApi gl;
    FakeRenderAssets assets;
    FakeTextureLoader textures;
    installGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textures);
    assertTrue(controller.stepMicroscopeFrame(),
            "Freeze mode captures its initial production frame");
    for (int attempt = 0; attempt < 1000 && gl.drawElementsCalls.empty();
            ++attempt) {
        (void)controller.drawMicroscope();
    }
    assertTrue(!gl.drawElementsCalls.empty(),
            "Initial production microscope frame reaches GL");

    gl.drawEvents.clear();
    gl.drawElementsCalls.clear();
    gl.uniformMatrixCalls.clear();
    const std::size_t uploadsBeforeCoverageMiss =
            gl.bufferDataCalls.size() + gl.bufferSubDataCalls.size();
    assertTrue(controller.queueMicroscopePan(80.0f, 0.0f),
            "Actual production Pan changes the current viewport");
    const auto newestViewport = controller.viewportState();
    (void)controller.drawMicroscope();
    assertTrue(
            !gl.drawEvents.empty() && !gl.uniformMatrixCalls.empty() && gl.bufferDataCalls.size() + gl.bufferSubDataCalls.size() == uploadsBeforeCoverageMiss,
            "A true coverage miss redraws retained geometry with the newest camera "
            "without a repeated dynamic upload or whole-scene flash");

    assertTrue(controller.stepMicroscopeFrame(),
            "Next safe point converts urgent miss into immediate recapture");
    gl.drawEvents.clear();
    gl.drawElementsCalls.clear();
    gl.uniformMatrixCalls.clear();
    for (int attempt = 0; attempt < 1000 && gl.drawElementsCalls.empty();
            ++attempt) {
        (void)controller.drawMicroscope();
    }
    assertTrue(
            !gl.drawElementsCalls.empty() && !gl.uniformMatrixCalls.empty() && controller.viewportState() == newestViewport,
            "Urgent replacement draws without rolling the production camera back");

    controller.releaseRendererResources();
}

void testProductionCaptureStagesRefreshFoodSpatialIndexAfterNoRender() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    environment.mobileFood = true;
    environment.mViscosity = 0.0;
    environment.mTestFoodDecay = 0.0;
    environment.mFoodSpeed = 0.0;
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(320, 240);
    controller.setViewportCamera(0.0f, 0.0f, 1.0f);
    {
        auto access = controller.writeWorld();
        const int food = access.world().mFoodData.addFood(
                0.0, 0.0, 0.1, 0.0f, access.world().mEnvironment);
        access.world().mFoodData.mVelocityX[food] = 0.05f;
        access.world().updateGrids(false);
        assertTrue(!access.world().foodSpatialIndexIsFresh(),
                "Consumer-free world leaves Food index lazy before capture");
    }

    assertTrue(controller.stepMicroscopeFrame(),
            "Freeze captures a StableWorld frame");
    {
        const auto access = controller.readWorld();
        assertTrue(access.world().foodSpatialIndexIsFresh(),
                "StableWorld capture refreshes Food index under its owner");
    }

    controller.setMicroscopeTemperatureMode(
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Observe));
    {
        auto access = controller.writeWorld();
        access.world().mFoodData.addFood(
                0.1, 0.0, 0.1, 0.0f, access.world().mEnvironment);
        assertTrue(!access.world().foodSpatialIndexIsFresh(),
                "Food add invalidates the index before Observe capture");
    }
    assertTrue(controller.stepMicroscopeFrame(),
            "Observe captures after update phase one");
    {
        const auto access = controller.readWorld();
        assertTrue(access.world().foodSpatialIndexIsFresh(),
                "AfterUpdatePhaseOne capture refreshes Food index");
    }

    controller.setMicroscopeIncubateTargetUps(2'000.0);
    controller.setMicroscopeTemperatureMode(
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::
                            IncubateNoRender));
    const std::uint64_t updatesBeforeNoRender = controller.completedUpdateCount();
    (void)controller.stepMicroscopeFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    (void)controller.stepMicroscopeFrame();
    assertTrue(controller.completedUpdateCount() > updatesBeforeNoRender,
            "No Render fixture completes at least one simulation update");
    {
        const auto access = controller.readWorld();
        assertTrue(!access.world().foodSpatialIndexIsFresh(),
                "Moving Food remains stale throughout No Render updates");
    }

    controller.setMicroscopeTemperatureMode(
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Incubate));
    assertTrue(controller.stepMicroscopeFrame(),
            "First Incubate frame captures immediately after No Render");
    {
        const auto access = controller.readWorld();
        assertTrue(access.world().foodSpatialIndexIsFresh(),
                "AfterCompletedUpdate capture refreshes the deferred Food index");
    }
}

void testProductionOwnerAdvancesAndAppliesInputWhileGlIsBlocked() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(320, 240);
    controller.setViewportCamera(0.0f, 0.0f, 1.0f);
    controller.setMicroscopeTemperatureMode(
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Observe));

    BlockingClearGlApi gl;
    FakeRenderAssets assets;
    FakeTextureLoader textures;
    installGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textures);
    controller.startMicroscopeRuntime();

    assertTrue(waitUntil(
                       [&] {
                           const auto snapshot = controller.microscopeRuntimeSnapshot();
                           return !snapshot.failed && snapshot.completedUpdateCount > 0;
                       },
                       std::chrono::seconds(2)),
            "Production owner starts updating without a GL callback");
    const auto beforeBlock = controller.microscopeRuntimeSnapshot();

    std::exception_ptr drawFailure;
    std::thread glThread([&] {
        try {
            (void)controller.drawMicroscope();
        } catch (...) {
            drawFailure = std::current_exception();
        }
    });
    assertTrue(gl.waitUntilBlocked(std::chrono::seconds(2)),
            "Test GL call reaches its artificial blocking point");

    controller.setMicroscopeToolOrdinal(2);
    assertTrue(waitUntil(
                       [&] {
                           return controller.microscopeRuntimeSnapshot()
                                          .lastResolvedInputSequence
                                   > beforeBlock.lastResolvedInputSequence;
                       },
                       std::chrono::seconds(1)),
            "Owner resolves a UI command while the GL thread is blocked");

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const auto duringBlock = controller.microscopeRuntimeSnapshot();
    assertTrue(!duringBlock.failed && duringBlock.completedUpdateCount > beforeBlock.completedUpdateCount,
            "Completed updates continue during a 600 ms blocked GL window");

    gl.release();
    glThread.join();
    controller.stopMicroscopeRuntime();
    controller.releaseRendererResources();
    if (drawFailure != nullptr) {
        std::rethrow_exception(drawFailure);
    }
}

void testProductionOwnerRateAndLifecycleAreIndependentFromGlCadence() {
    using namespace std::chrono_literals;

    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(320, 240);
    controller.setViewportCamera(0.0f, 0.0f, 1.0f);
    controller.setMicroscopeTemperatureMode(
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Observe));

    FakeGlApi gl;
    FakeRenderAssets assets;
    FakeTextureLoader textures;
    installGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textures);
    controller.startMicroscopeRuntime();
    assertTrue(waitUntil(
                       [&] {
                           return controller.microscopeRuntimeSnapshot()
                                          .completedUpdateCount
                                   >= 5;
                       },
                       2s),
            "TargetRate owner starts independently from render callbacks");

    constexpr auto kRateObservationWindow = 1200ms;
    constexpr double kMinimumAcceptedTargetUps = 45.0;
    constexpr double kMaximumAcceptedTargetUps = 75.0;
    constexpr double kMaximumCameraRateDifference = 12.0;
    const auto measuredUps = [](std::uint64_t completedUpdates,
                                     std::chrono::steady_clock::duration elapsed) {
        return static_cast<double>(completedUpdates) / std::chrono::duration<double>(elapsed).count();
    };

    const auto idleBefore = controller.inspectMicroscopeStats();
    const auto idleStarted = std::chrono::steady_clock::now();
    std::this_thread::sleep_until(idleStarted + kRateObservationWindow);
    const auto idleAfter = controller.inspectMicroscopeStats();
    const auto idleFinished = std::chrono::steady_clock::now();
    const std::uint64_t idleCompletedUpdates =
            idleAfter.completedUpdateCount - idleBefore.completedUpdateCount;
    const double idleUps =
            measuredUps(idleCompletedUpdates, idleFinished - idleStarted);

    const auto cameraBefore = controller.inspectMicroscopeStats();
    const auto cameraStarted = std::chrono::steady_clock::now();
    const auto cameraFinishedAt = cameraStarted + kRateObservationWindow;
    auto nextCameraNotification = cameraStarted;
    std::uint64_t cameraNotificationCount = 0;
    while (std::chrono::steady_clock::now() < cameraFinishedAt) {
        const float zoom = cameraNotificationCount % 2 == 0 ? 1.0f : 1.01f;
        controller.setViewportCamera(0.0f, 0.0f, zoom);
        ++cameraNotificationCount;
        nextCameraNotification += 8ms;
        std::this_thread::sleep_until(
                std::min(nextCameraNotification, cameraFinishedAt));
    }
    const auto cameraAfter = controller.inspectMicroscopeStats();
    const auto cameraFinished = std::chrono::steady_clock::now();
    const std::uint64_t cameraCompletedUpdates =
            cameraAfter.completedUpdateCount - cameraBefore.completedUpdateCount;
    const double cameraUps =
            measuredUps(cameraCompletedUpdates, cameraFinished - cameraStarted);

    const auto afterCameraIdleBefore = controller.inspectMicroscopeStats();
    const auto afterCameraIdleStarted = std::chrono::steady_clock::now();
    std::this_thread::sleep_until(afterCameraIdleStarted + kRateObservationWindow);
    const auto afterCameraIdleAfter = controller.inspectMicroscopeStats();
    const auto afterCameraIdleFinished = std::chrono::steady_clock::now();
    const std::uint64_t afterCameraIdleCompletedUpdates =
            afterCameraIdleAfter.completedUpdateCount - afterCameraIdleBefore.completedUpdateCount;
    const double afterCameraIdleUps =
            measuredUps(afterCameraIdleCompletedUpdates,
                    afterCameraIdleFinished - afterCameraIdleStarted);

    const bool idleTargetRateCorrect = idleUps >= kMinimumAcceptedTargetUps && idleUps <= kMaximumAcceptedTargetUps;
    const bool cameraTargetRateCorrect = cameraUps >= kMinimumAcceptedTargetUps && cameraUps <= kMaximumAcceptedTargetUps;
    const bool afterCameraIdleTargetRateCorrect =
            afterCameraIdleUps >= kMinimumAcceptedTargetUps && afterCameraIdleUps <= kMaximumAcceptedTargetUps;
    const bool cameraDoesNotClockTargetRate =
            std::abs(idleUps - cameraUps) <= kMaximumCameraRateDifference && std::abs(cameraUps - afterCameraIdleUps) <= kMaximumCameraRateDifference;
    if (!idleTargetRateCorrect || !cameraTargetRateCorrect || !afterCameraIdleTargetRateCorrect || !cameraDoesNotClockTargetRate) {
        std::cerr << "TargetRate without GL/input=" << idleUps
                  << " UPS, with high-rate camera notifications=" << cameraUps
                  << " UPS, after camera notifications stop=" << afterCameraIdleUps
                  << " UPS, notifications=" << cameraNotificationCount << '\n';
    }
    assertTrue(
            idleTargetRateCorrect && cameraTargetRateCorrect && afterCameraIdleTargetRateCorrect && cameraDoesNotClockTargetRate,
            "Observe sustains requested 60 UPS before, during, and after camera "
            "notifications without using input as the scheduler clock");

    std::array<std::uint64_t, 4> completedByCadence{};
    constexpr std::array<int, 4> kRenderFrequencies{30, 60, 90, 120};
    for (std::size_t index = 0; index < kRenderFrequencies.size(); ++index) {
        const auto before = controller.inspectMicroscopeStats();
        const auto started = std::chrono::steady_clock::now();
        const auto finished = started + 600ms;
        const auto interval =
                std::chrono::nanoseconds(1'000'000'000LL / kRenderFrequencies[index]);
        auto nextDraw = started;
        while (std::chrono::steady_clock::now() < finished) {
            (void)controller.drawMicroscope();
            nextDraw += interval;
            std::this_thread::sleep_until(std::min(nextDraw, finished));
        }
        const auto after = controller.inspectMicroscopeStats();
        completedByCadence[index] =
                after.completedUpdateCount - before.completedUpdateCount;
    }

    const auto [minimumTargetUpdates, maximumTargetUpdates] =
            std::minmax_element(completedByCadence.begin(), completedByCadence.end());
    const bool targetRateIndependent =
            *minimumTargetUpdates >= 27 && *maximumTargetUpdates <= 48 && *maximumTargetUpdates - *minimumTargetUpdates <= 10;
    if (!targetRateIndependent) {
        std::cerr << "completed updates by render cadence: 30="
                  << completedByCadence[0] << " 60=" << completedByCadence[1]
                  << " 90=" << completedByCadence[2]
                  << " 120=" << completedByCadence[3] << '\n';
    }
    assertTrue(targetRateIndependent,
            "Render callback frequencies 30/60/90/120 all sustain requested "
            "60 UPS instead of merely matching at an arbitrary positive rate");

    const auto targetTelemetry = controller.microscopeRuntimeSnapshot();
    assertTrue(
            targetTelemetry.rateTelemetry.scheduler.policy == cellularity::simulation::SimulationRatePolicyKind::TargetRate && targetTelemetry.rateTelemetry.scheduler.requestedTargetUps == cellularity::microscope::kDisplayPacedSimulationTargetUps && targetTelemetry.rateTelemetry.actualRateValid && targetTelemetry.rateTelemetry.actualCompletedUps >= kMinimumAcceptedTargetUps && targetTelemetry.rateTelemetry.actualCompletedUps <= kMaximumAcceptedTargetUps && targetTelemetry.rateTelemetry.actualRateWindow >= 500ms && targetTelemetry.rateTelemetry.actualRateWindowUpdates > 0 && targetTelemetry.rateTelemetry.scheduler.observedBatchCount > 0 && targetTelemetry.rateTelemetry.scheduler.observedUpdateCount > 0 && targetTelemetry.rateTelemetry.scheduler.totalObservedBatchDuration >= targetTelemetry.rateTelemetry.scheduler.lastObservedBatchDuration && targetTelemetry.rateTelemetry.scheduler.maximumObservedBatchDuration >= targetTelemetry.rateTelemetry.scheduler.lastObservedBatchDuration && targetTelemetry.rateTelemetry.scheduler.maximumObservedUpdateCount > 0 && targetTelemetry.rateTelemetry.ownerServiceCount > targetTelemetry.rateTelemetry.scheduler.observedBatchCount && targetTelemetry.rateTelemetry.maximumOwnerServiceInterval > 0ns && targetTelemetry.rateTelemetry.publishedSourceFrameCount > 0 && targetTelemetry.rateTelemetry.publishedPreparedFrameCount > 0 && targetTelemetry.rateTelemetry.statusProducerReplacedCount > 0 && targetTelemetry.rateTelemetry.statusPublishRetriesExhausted == 0,
            "Owner publication exposes target, actual rate, batch, service, render "
            "drop, and latest-only status telemetry without using render cadence");

    controller.setMicroscopeTemperatureMode(
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::
                            IncubateNoRender));
    assertTrue(
            waitUntil(
                    [&] {
                        return controller.microscopeRuntimeSnapshot().temperatureMode == cellularity::microscope::temperatureModeOrdinal(cellularity::microscope::MicroscopeTemperatureMode::IncubateNoRender);
                    },
                    2s),
            "Owner applies No Render mode without a GL callback");
    const auto beforeSurfaceLoss = controller.inspectMicroscopeStats();
    const auto beforeMaximumTelemetry =
            controller.microscopeRuntimeSnapshot().rateTelemetry;
    const std::uint64_t worldEpoch =
            controller.microscopeRuntimeSnapshot().worldEpoch;
    controller.abandonRendererResources();
    std::this_thread::sleep_for(300ms);
    const auto afterSurfaceLoss = controller.inspectMicroscopeStats();
    const auto afterMaximumTelemetry =
            controller.microscopeRuntimeSnapshot().rateTelemetry;
    const std::uint64_t maxThroughputUpdates =
            afterSurfaceLoss.completedUpdateCount - beforeSurfaceLoss.completedUpdateCount;
    const std::uint64_t maximumObservedUpdates =
            afterMaximumTelemetry.scheduler.observedUpdateCount - beforeMaximumTelemetry.scheduler.observedUpdateCount;
    const std::uint64_t maximumInteractionPasses =
            afterMaximumTelemetry.worldInteractionServiceCount - beforeMaximumTelemetry.worldInteractionServiceCount;
    assertTrue(
            maxThroughputUpdates > 0 && maxThroughputUpdates * 2 > *maximumTargetUpdates && maximumObservedUpdates > maximumInteractionPasses * 2 && maximumInteractionPasses > 0 && controller.microscopeRuntimeSnapshot().worldEpoch == worldEpoch && !controller.microscopeRuntimeSnapshot().failed,
            "MaxThroughput keeps world interaction at bounded owner-cycle frequency "
            "instead of repeating it for every update");

    assertTrue(
            waitUntil(
                    [&] {
                        const auto telemetry =
                                controller.microscopeRuntimeSnapshot().rateTelemetry;
                        return telemetry.scheduler.policy == cellularity::simulation::SimulationRatePolicyKind::MaxThroughput && !telemetry.scheduler.requestedTargetUps.has_value() && telemetry.actualRateValid && telemetry.actualCompletedUps > 0.0 && telemetry.actualRateWindow >= 500ms;
                    },
                    2s),
            "MaxThroughput publishes measured UPS while requested target remains "
            "structurally absent");

    const auto beforePauseSequence =
            controller.microscopeRuntimeSnapshot().lastResolvedInputSequence;
    controller.setMicroscopeLifecyclePaused(true);
    assertTrue(waitUntil(
                       [&] {
                           return controller.microscopeRuntimeSnapshot()
                                          .lastResolvedInputSequence
                                   > beforePauseSequence;
                       },
                       2s),
            "Lifecycle pause is resolved by the owner without GL");
    const auto pausedStart = controller.microscopeRuntimeSnapshot();
    std::this_thread::sleep_for(600ms);
    const auto pausedEnd = controller.microscopeRuntimeSnapshot();
    const std::uint64_t pausedOwnerServiceDelta =
            pausedEnd.rateTelemetry.ownerServiceCount - pausedStart.rateTelemetry.ownerServiceCount;
    assertTrue(
            pausedEnd.completedUpdateCount == pausedStart.completedUpdateCount && pausedOwnerServiceDelta <= 8,
            "Lifecycle pause stops updates without accumulating callback debt or "
            "busy-polling the owner loop");

    const auto beforeResumeSequence =
            controller.microscopeRuntimeSnapshot().lastResolvedInputSequence;
    controller.setMicroscopeLifecyclePaused(false);
    assertTrue(
            waitUntil(
                    [&] {
                        const auto snapshot = controller.microscopeRuntimeSnapshot();
                        return snapshot.lastResolvedInputSequence > beforeResumeSequence && snapshot.completedUpdateCount > pausedEnd.completedUpdateCount;
                    },
                    2s),
            "Lifecycle resume restarts MaxThroughput without a render callback");
    controller.stopMicroscopeRuntime();
}

void testProductionOwnerExecutesRequestReplyOperations() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(320, 240);
    controller.setViewportCamera(0.0f, 0.0f, 1.0f);
    {
        auto access = controller.writeWorld();
        const int cell = cellularity::tests::addAliveCell(access.world().mCellData,
                CellType::PHAGOCYTE);
        auto genome = access.world().mCellData.materializeGenome(cell);
        for (int geneIndex = 0; geneIndex < CellDataConstants::MAX_GENES_COUNT;
                ++geneIndex) {
            Gene& gene = genome[static_cast<std::size_t>(geneIndex)];
            cellularity::GeneBuilder builder(gene);
            builder.setSplitMass(CellDataConstants::MIN_SPLIT_MASS);
            builder.fix();
            builder.setInitialCompatibilityFlag(geneIndex == 0);
        }
        assertTrue(
                access.world().mCellData.replaceGenome(
                        cell, genome, cellularity::GeneIndex(0)),
                "Owner fixture imports canonical Genome");
        access.world().updateGrids();
    }
    controller.startMicroscopeRuntime();
    assertTrue(waitUntil(
                       [&] {
                           const auto snapshot = controller.microscopeRuntimeSnapshot();
                           return snapshot.revision > 0 && snapshot.cellCount == 1;
                       },
                       std::chrono::seconds(2)),
            "Owner publishes its initial immutable status");

    controller.setMicroscopeToolOrdinal(4);
    assertTrue(waitUntil(
                       [&] {
                           const auto snapshot = controller.microscopeRuntimeSnapshot();
                           return snapshot.diagnosisRequested && snapshot.toolOrdinal == 4 && !snapshot.failed;
                       },
                       std::chrono::seconds(2)),
            "Owner applies diagnosis mode before the selection command");
    controller.queueMicroscopeTap(160.0f, 120.0f, nullptr);
    cellularity::microscope::NativeMicroscopeRuntimeSnapshot diagnosisSnapshot;
    const bool diagnosisPublished = waitUntil(
            [&] {
                diagnosisSnapshot = controller.microscopeRuntimeSnapshot();
                return diagnosisSnapshot.diagnosisRequested && diagnosisSnapshot.selectedCellId == 0 && diagnosisSnapshot.selectedCellState != nullptr && diagnosisSnapshot.selectedGenomeState != nullptr;
            },
            std::chrono::seconds(2));
    if (!diagnosisPublished) {
        const auto access = controller.readWorld();
        std::cerr << "diagnosis publication state: revision="
                  << diagnosisSnapshot.revision
                  << " tool=" << diagnosisSnapshot.toolOrdinal
                  << " requested=" << diagnosisSnapshot.diagnosisRequested
                  << " selected=" << diagnosisSnapshot.selectedCellId
                  << " cells=" << diagnosisSnapshot.cellCount << " worldSelected="
                  << access.world().mCellData.getIdFromSelectTag(1)
                  << " failed=" << diagnosisSnapshot.failed << '\n';
    }
    assertTrue(diagnosisPublished,
            "Diagnosis selection is serialized at an owner safe point");
    const auto retainedCellState = diagnosisSnapshot.selectedCellState;
    const auto retainedGenomeState = diagnosisSnapshot.selectedGenomeState;
    const std::vector<std::uint8_t> originalCellState = *retainedCellState;
    const std::vector<std::uint8_t> originalGenomeState = *retainedGenomeState;

    controller.setMicroscopeToolOrdinal(2);
    assertTrue(waitUntil(
                       [&] {
                           const auto newest = controller.microscopeRuntimeSnapshot();
                           return newest.revision > diagnosisSnapshot.revision && !newest.diagnosisRequested && newest.selectedCellState == nullptr && newest.selectedGenomeState == nullptr;
                       },
                       std::chrono::seconds(2)),
            "A newer status replaces diagnosis payloads without retaining "
            "stale UI state");
    assertTrue(*retainedCellState == originalCellState && *retainedGenomeState == originalGenomeState,
            "Previously published inspection payloads remain immutable after "
            "newer owner publications");

    const auto cell = controller.inspectMicroscopeCell(0);
    const auto environmentState = controller.inspectMicroscopeEnvironment();
    assertTrue(cell.has_value() && !cell->empty() && !environmentState.empty(),
            "Inspection requests return owner-captured value payloads");

    const auto savePath =
            std::filesystem::temp_directory_path() / ("cellularity-owner-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".substrate");
    (void)controller.writeMicroscopeSubstratePath(savePath.string());
    const auto savedInspection =
            inspectSubstratePath(savePath.string(), false, environment);
    std::error_code removeError;
    const bool removed = std::filesystem::remove(savePath, removeError);
    assertTrue(savedInspection.cellCount == 1 && removed && !removeError,
            "Save request serializes an owner-safe-point snapshot");

    ByteWriter patch;
    patch.writeInt(EnvironmentBatch::PATCH_MAGIC);
    patch.writeInt(EnvironmentBatch::SCHEMA_VERSION);
    patch.writeLong(0);
    patch.writeInt(1);
    patch.writeInt(1);
    patch.writeInt(5);
    patch.writeBool(false);
    ByteReader applyResult(
            controller.applyMicroscopeEnvironmentPatch(patch.takeBytes()));
    assertTrue(applyResult.readInt() == EnvironmentBatch::RESULT_MAGIC && applyResult.readInt() == EnvironmentBatch::SCHEMA_VERSION && applyResult.readBool() && applyResult.readLong() == 1 && applyResult.readInt() == 0 && applyResult.remaining() == 0,
            "Environment patch executes at the owner safe point");

    const auto beforeReset = controller.microscopeRuntimeSnapshot();
    controller.resetMicroscopeWorld();
    assertTrue(waitUntil(
                       [&] {
                           const auto snapshot = controller.microscopeRuntimeSnapshot();
                           return snapshot.worldEpoch != beforeReset.worldEpoch && snapshot.cellCount == 0 && snapshot.environmentRevision == 0;
                       },
                       std::chrono::seconds(2)),
            "Reset is an epoch-changing owner barrier");
    controller.stopMicroscopeRuntime();
}

void testSessionDrawRefreshesEverySwapWithoutRepeatUpload() {
    auto world = createWorld();
    NativeViewport viewport(1.1f);
    viewport.setSurfaceSize(320, 240);
    viewport.setCamera(0.0f, 0.0f, 1.0f);

    FakeGlApi gl;
    FakeRenderAssets assets;
    FakeTextureLoader textures;
    NativeRenderSession session(world->mEnvironment.maxCellCount,
            std::make_unique<cellularity::render::gles::GlesRenderSurfaceSession>(
                    gl));
    session.loadResources(*world, assets, textures);

    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 71;
    metadata.stage = cellularity::render::RenderCaptureStage::StableWorld;
    session.captureMicroscopeFrame(*world, viewport, 0.1f, metadata);
    assertTrue(drawSessionUntilReady(session, viewport, {0.2f, 0.3f, 0.4f}),
            "Microscope draws when its first async frame becomes complete");
    const auto* const retainedFrame = session.preparedFrame();
    assertTrue(retainedFrame != nullptr && retainedFrame->valid,
            "Session retains one complete readable frame");
    const std::uint64_t retainedViewportRevision =
            retainedFrame->viewport.revision;
    const std::size_t uploadedBufferCount = gl.bufferDataCalls.size();
    const std::size_t uploadedSubDataCount = gl.bufferSubDataCalls.size();
    const int clearCount = gl.clearCount;
    const std::size_t drawEventCount = gl.drawEvents.size();
    assertTrue(
            cellularity::render::isPrimaryContentPresented(session.drawMicroscope(viewport, {0.2f, 0.3f, 0.4f})) && gl.clearCount == clearCount + 1 && gl.drawEvents.size() > drawEventCount && gl.bufferDataCalls.size() == uploadedBufferCount && gl.bufferSubDataCalls.size() == uploadedSubDataCount,
            "An unchanged presentation fully refreshes the next swap buffer without "
            "repeating dynamic uploads");

    viewport.setCamera(0.15f, -0.1f, 1.75f);
    const auto expectedMatrix =
            viewport.worldMatrix(cellularity::app::identityMatrix());
    gl.uniformMatrixCalls.clear();
    gl.uniform3fCalls.clear();
    const std::array<float, 3> newestBackground{0.65f, 0.55f, 0.45f};
    assertTrue(cellularity::render::isPrimaryContentPresented(
                       session.drawMicroscope(viewport, newestBackground)),
            "Retained geometry redraws with the newest presentation");

    assertTrue(gl.bufferDataCalls.size() == uploadedBufferCount && gl.bufferSubDataCalls.size() == uploadedSubDataCount,
            "GPU identity suppresses uploads for presentation-only redraw");
    assertTrue(!gl.uniformMatrixCalls.empty() && std::all_of(gl.uniformMatrixCalls.begin(), gl.uniformMatrixCalls.end(), [&](const auto& call) {
        return call.value == expectedMatrix;
    }),
            "Every layer receives the immediate newest camera matrix");
    assertTrue(std::any_of(gl.uniform3fCalls.begin(), gl.uniform3fCalls.end(),
                       [&](const auto& call) {
                           return call.location == 15 && call.value == newestBackground;
                       }),
            "Background draw receives the newest presentation color");
    assertTrue(session.preparedFrame() == retainedFrame && retainedFrame->viewport.revision == retainedViewportRevision,
            "Presentation never mutates immutable capture-time viewport data");
}

void testRuntimeControllerRefreshesEverySwapWithoutRepeatUpload() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    NativeCellularityController controller(false, environment);
    controller.setViewportSurfaceSize(320, 240);
    controller.setViewportCamera(0.0f, 0.0f, 1.0f);
    controller.setMicroscopeTemperatureMode(
            cellularity::microscope::temperatureModeOrdinal(
                    cellularity::microscope::MicroscopeTemperatureMode::Freeze));
    {
        auto access = controller.writeWorld();
        const int cell = cellularity::tests::addAliveCell(access.world().mCellData,
                CellType::PHAGOCYTE);
        access.world().mCellData.mPositionX[cell] = 0.0;
        access.world().mCellData.mPositionY[cell] = 0.0;
        access.world().mCellData.mRadius[cell] = 0.02;
        access.world().updateGrids();
    }

    FakeGlApi gl;
    FakeRenderAssets assets;
    FakeTextureLoader textures;
    installGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textures);
    controller.startMicroscopeRuntime();
    assertTrue(
            waitUntil(
                    [&] {
                        return controller.microscopeRuntimeSnapshot()
                                       .rateTelemetry.publishedPreparedFrameCount
                                > 0;
                    },
                    std::chrono::seconds(2)),
            "Production owner publishes a complete frame before swap validation");
    assertTrue(waitUntil(
                       [&] {
                           (void)controller.drawMicroscope();
                           return !gl.drawEvents.empty();
                       },
                       std::chrono::seconds(2)),
            "Production controller presents its first owner-prepared frame");

    const std::size_t uploadedBufferCount = gl.bufferDataCalls.size();
    const std::size_t uploadedSubDataCount = gl.bufferSubDataCalls.size();
    const int clearCount = gl.clearCount;
    const std::size_t drawEventCount = gl.drawEvents.size();
    (void)controller.drawMicroscope();
    controller.stopMicroscopeRuntime();

    assertTrue(
            gl.clearCount == clearCount + 1 && gl.drawEvents.size() > drawEventCount && gl.bufferDataCalls.size() == uploadedBufferCount && gl.bufferSubDataCalls.size() == uploadedSubDataCount,
            "Production controller fully redraws an unchanged swap buffer without "
            "repeating dynamic uploads");

    controller.releaseRendererResources();
}

void testConcurrentProducerConsumerRaceContract() {
    auto world = createWorld();
    MicroscopeRenderFramePipeline pipeline(world->mEnvironment.maxCellCount);
    const cellularity::render::RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};
    constexpr std::uint64_t kFinalRevision = 1000;
    std::atomic<bool> producerDone{false};
    std::atomic<int> failureCode{0};
    const auto recordFailure = [&](int code) {
        int expected = 0;
        (void)failureCode.compare_exchange_strong(
                expected, code, std::memory_order_acq_rel, std::memory_order_acquire);
    };

    std::thread producer([&] {
        try {
            for (std::uint64_t revision = 1; revision < kFinalRevision; ++revision) {
                auto metadata = captureMetadata(81, view, view);
                metadata.completedUpdateId = revision;
                metadata.resolvedInput.lastResolvedInputSequence = revision;
                metadata.resolvedInput.appliedWorldRevision = revision;
                pipeline.capture(*world, view, metadata);
            }
            auto finalMetadata = captureMetadata(81, view, view);
            finalMetadata.completedUpdateId = kFinalRevision;
            finalMetadata.resolvedInput.lastResolvedInputSequence = kFinalRevision;
            finalMetadata.resolvedInput.appliedWorldRevision = kFinalRevision;
            pipeline.captureAndPrepare(*world, view, finalMetadata);
        } catch (...) {
            recordFailure(1);
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t lastFrameSequence = 0;
        std::uint64_t lastInputRevision = 0;
        std::uint64_t presentationRevision = 1;
        const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            try {
                const auto* frame = pipeline.presentLatestPreparedFrame(
                        presentation(presentationRevision++, 1.0f, {0.3f, 0.4f, 0.5f}));
                if (frame != nullptr) {
                    if (!frame->valid || !frame->geometryReady || frame->identity.worldEpoch != 81 || frame->identity.captureSequence < lastFrameSequence || frame->resolvedInput.lastResolvedInputSequence < lastInputRevision) {
                        recordFailure(2);
                        return;
                    }
                    lastFrameSequence = frame->identity.captureSequence;
                    lastInputRevision = frame->resolvedInput.lastResolvedInputSequence;
                }
                if (producerDone.load(std::memory_order_acquire) && lastInputRevision == kFinalRevision) {
                    return;
                }
            } catch (...) {
                recordFailure(3);
                return;
            }
            std::this_thread::yield();
        }
        recordFailure(4);
    });

    producer.join();
    consumer.join();
    const auto finalState = pipeline.snapshotWhenQuiescent();
    if (failureCode.load(std::memory_order_acquire) != 0) {
        std::cerr << "concurrent pipeline failure code="
                  << failureCode.load(std::memory_order_relaxed)
                  << " accepted=" << finalState.latestAcceptedSourceSequence
                  << " published=" << finalState.latestPublishedSourceSequence
                  << " sourceDrops="
                  << finalState.sourceCounters.droppedFrameCount()
                  << " preparedDrops="
                  << finalState.preparedCounters.droppedFrameCount() << '\n';
    }
    assertTrue(
            failureCode.load(std::memory_order_acquire) == 0 && finalState.sourceStorage.slots.size() == 3 && finalState.preparedStorage.slots.size() == 3 && !finalState.failure && finalState.preparedCounters.publishedFrameCount > 0 && finalState.supersededBuildCount == 0 && finalState.latestAcceptedSourceSequence == finalState.latestPublishedSourceSequence,
            "Concurrent producer/worker/consumer run stays complete, monotonic, and "
            "fixed-capacity without starving compatible publications");
}

void testReplacementRuntimeConfigurationIsAppliedBeforeOwnerStart() {
    NativeCellularityController controller(createWorld());
    cellularity::microscope::MicroscopeRuntimeStartConfiguration configuration;
    configuration.temperatureMode = cellularity::microscope::temperatureModeOrdinal(
            cellularity::microscope::MicroscopeTemperatureMode::Incubate);
    configuration.toolOrdinal = 4;
    configuration.slowMotionSpeed = 0.0F;
    configuration.incubateTargetUps = 2450.5;
    configuration.lifecyclePaused = true;
    configuration.performanceHudEnabled = true;

    controller.startMicroscopeRuntime(configuration);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto pausedStats = controller.inspectMicroscopeStats();
    const auto resolved = controller.microscopeRuntimeStartConfiguration();
    assertTrue(
            pausedStats.completedUpdateCount == 0,
            "A paused replacement runtime cannot complete an update before Resume");
    assertTrue(
            resolved.temperatureMode == configuration.temperatureMode
                    && resolved.toolOrdinal == configuration.toolOrdinal
                    && resolved.slowMotionSpeed == configuration.slowMotionSpeed
                    && resolved.incubateTargetUps
                            == configuration.incubateTargetUps
                    && resolved.lifecyclePaused
                    && resolved.performanceHudEnabled,
            "Replacement runtime preserves every independent requested-control channel");

    controller.setMicroscopeLifecyclePaused(false);
    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool resumed = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (controller.inspectMicroscopeStats().completedUpdateCount != 0) {
            resumed = true;
            break;
        }
        std::this_thread::yield();
    }
    controller.stopMicroscopeRuntime();
    assertTrue(resumed, "Replacement runtime resumes after its lifecycle barrier");
}

} // namespace

int main() {
    testWorkerOwnershipTransitions();
    testWorkerOutputMatchesSynchronousReference();
    testCompletedCompatibleBuildPublishesBeforeNewestSource();
    testBlockedWorkerCoalescesToNewestSource();
    testBlockedGlConsumerKeepsWorkerBounded();
    testEpochAndInputRollbackBarriers();
    testModeGenerationBarrierRejectsLateFrames();
    testControlledPreparationFailureRetainsOnlyCompleteCoverage();
    testAllFixedSlotsReportBoundedStorageAndStopReset();
    testFirstFrameClearAndSurfaceContextRecreation();
    testBlockedPanZoomUsesCoverageAndUrgentLatestWinsCapture();
    testControllerCoverageMissRecapturesActualGestureCamera();
    testProductionCaptureStagesRefreshFoodSpatialIndexAfterNoRender();
    testProductionOwnerAdvancesAndAppliesInputWhileGlIsBlocked();
    testProductionOwnerRateAndLifecycleAreIndependentFromGlCadence();
    testProductionOwnerExecutesRequestReplyOperations();
    testSessionDrawRefreshesEverySwapWithoutRepeatUpload();
    testRuntimeControllerRefreshesEverySwapWithoutRepeatUpload();
    testConcurrentProducerConsumerRaceContract();
    testReplacementRuntimeConfigurationIsAppliedBeforeOwnerStart();
    std::cout << "microscope preparation worker pipeline tests passed\n";
    return 0;
}
