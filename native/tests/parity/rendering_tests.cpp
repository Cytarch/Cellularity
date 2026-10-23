#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if !defined(NDEBUG) && (defined(__unix__) || defined(__APPLE__))
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "app/NativeCellularityController.h"
#include "app/NativeRenderSession.h"
#include "app/NativeWorldEditing.h"
#include "app/genome_preview/GenomeFilePreviewSimulation.h"
#include "app/genome_preview/GenomePreviewEndpoint.h"
#include "app/genome_preview/GenomePreviewViewState.h"
#include "app/genome_preview/tree/GenomePreviewTreeRenderWorld.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/render/MicroscopeDebugHud.h"
#include "core/CellData.h"
#include "core/CellSweepBroadphase.h"
#include "core/CellTopology.h"
#include "core/SimulationWorld.h"
#include "core/CollisionGrid.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/RenderContactChord.h"
#include "formats/GenomeDraftCodec.h"
#include "render/SimulationWorldGpuRenderer.h"
#include "render/NativeCellRendererResources.h"
#include "render/NativeSimulationWorldRenderer.h"
#include "render/NativeRenderTextureLoader.h"
#include "render/backend/gles/GlesRenderSurfaceSession.h"
#include "render/frame/RenderContactChordBuffer.h"
#include "render/frame/RenderContactLimits.h"
#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/geometry/RenderContactChordNormalizer.h"
#include "render/shaders/NativeShaders.h"
#include "render/text/PixelTextRenderer.h"
#include "tests/support/RenderingTestDoubles.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

namespace cellularity::tests {

void ensureGlesBackend(NativeCellularityController& controller,
        NativeGlApi& gl) {
    if (!controller.hasRenderSurfaceSession()) {
        controller.installRenderSurfaceSession(
                std::make_unique<render::gles::GlesRenderSurfaceSession>(gl));
    }
}

struct NativeSimulationWorldRendererTestAccess {
    static void markPhysicsContactFixtureFresh(SimulationWorld& world) {
        world.mCellContactCacheFresh = true;
    }

    static std::size_t
    cacheIndexForSourceCell(const NativeSimulationWorldRenderer& renderer,
            int cellIndex) {
        const auto found = std::find_if(
                renderer.mSourceFrame.cells.begin(), renderer.mSourceFrame.cells.end(),
                [&](const cellularity::render::RenderSourceCell& cell) {
                    return cell.sourceCellIndex == cellIndex;
                });
        if (found == renderer.mSourceFrame.cells.end()) {
            return static_cast<std::size_t>(cellIndex);
        }
        return static_cast<std::size_t>(
                std::distance(renderer.mSourceFrame.cells.begin(), found));
    }

    static std::uint64_t wallGeneration(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mFrameBuilder.mContactChordResolver.mGeneration;
    }

    static std::size_t
    generationEntryCount(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mFrameBuilder.mContactChordResolver.mGenerationByCell
                .size();
    }

    static std::size_t offsetEntryCount(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mFrameBuilder.mContactChordResolver.mOffsetByCell.size();
    }

    static std::size_t countEntryCount(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mFrameBuilder.mContactChordResolver.mCountByCell.size();
    }

    static std::size_t
    normalizedContactChordCount(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mFrameBuilder.mContactChordResolver
                .mNormalizedContactChordCount;
    }

    static const RenderContactChordBuffer&
    capturedContactChords(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mSourceCapture.mRenderContactChords;
    }

    static std::size_t
    normalizedContactChordPhysicalSize(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mFrameBuilder.mContactChordResolver.mNormalizedContactChords
                .size();
    }

    static std::size_t
    normalizedContactChordCapacity(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mFrameBuilder.mContactChordResolver.mNormalizedContactChords
                .capacity();
    }

    static std::uint64_t generationStamp(const NativeSimulationWorldRenderer& renderer,
            int cellIndex) {
        return renderer.mFrameBuilder.mContactChordResolver.mGenerationByCell.at(
                cacheIndexForSourceCell(renderer, cellIndex));
    }

    static std::size_t contactChordOffset(const NativeSimulationWorldRenderer& renderer,
            int cellIndex) {
        return renderer.mFrameBuilder.mContactChordResolver.mOffsetByCell.at(
                cacheIndexForSourceCell(renderer, cellIndex));
    }

    static int cachedContactChordCount(const NativeSimulationWorldRenderer& renderer,
            int cellIndex) {
        return static_cast<int>(
                renderer.mFrameBuilder.mContactChordResolver.mCountByCell.at(
                        cacheIndexForSourceCell(renderer, cellIndex)));
    }

    static const RenderContactChord&
    normalizedContactChordAt(const NativeSimulationWorldRenderer& renderer,
            std::size_t wallIndex) {
        return renderer.mFrameBuilder.mContactChordResolver.mNormalizedContactChords
                .at(wallIndex);
    }

    static void fillNormalizedWallStorage(NativeSimulationWorldRenderer& renderer,
            const RenderContactChord& value) {
        auto& normalized =
                renderer.mFrameBuilder.mContactChordResolver.mNormalizedContactChords;
        std::fill(normalized.begin(), normalized.end(), value);
    }

    static void beginWallExtraction(NativeSimulationWorldRenderer& renderer) {
        renderer.mFrameBuilder.mContactChordResolver.beginFrame();
    }

    static std::vector<RenderContactChord> resolveCellContactChords(
            NativeSimulationWorldRenderer& renderer, const CellData& cellData,
            const RenderContactChordBuffer& contactChords, int cellIndex) {
        cellularity::render::RenderSourceFrame source;
        source.cells.resize(static_cast<std::size_t>(cellData.mCellCount));
        if (cellIndex >= 0 && cellIndex < cellData.mCellCount) {
            auto& cell = source.cells[static_cast<std::size_t>(cellIndex)];
            const auto activeContactChords = contactChords.activeChords(cellIndex);
            const int contactChordCount = activeContactChords.size();
            cell.sourceCellIndex = cellIndex;
            cell.contactChordCount = static_cast<std::uint8_t>(contactChordCount);
            cell.hasContactChordPayload = true;
            if (contactChordCount > 0 && contactChordCount <= cellularity::render::kMaximumRenderContactChordsPerCell) {
                source.activeContactChords.insert(source.activeContactChords.end(),
                        activeContactChords.begin(),
                        activeContactChords.end());
                for (int wallIndex = 0; wallIndex < contactChordCount; ++wallIndex) {
                    const int neighborCellIndex =
                            activeContactChords[wallIndex].neighborCellIndex;
                    source.activeContactChordNeighborRuntimeIds.push_back(
                            neighborCellIndex == CellData::BOUNDARY_CONNECTION_INDEX
                                    ? kBoundaryCellRuntimeId
                                    : neighborCellIndex >= 0 && neighborCellIndex < cellData.mCellCount
                                    ? cellData.runtimeId(neighborCellIndex)
                                    : kInvalidCellRuntimeId);
                }
            }
        }
        const cellularity::render::RenderContactChordResolver::View view =
                renderer.mFrameBuilder.mContactChordResolver.resolve(
                        source, cellIndex,
                        cellularity::render::RenderContactChordResolver::Consumer::
                                CellGeometry,
                        nullptr);
        if (view.count == 0) {
            return {};
        }
        return std::vector<RenderContactChord>(view.data, view.data + view.count);
    }

    static bool metadataIsZero(const NativeSimulationWorldRenderer& renderer) {
        const auto& resolver = renderer.mFrameBuilder.mContactChordResolver;
        return std::all_of(resolver.mGenerationByCell.begin(),
                       resolver.mGenerationByCell.end(),
                       [](std::uint64_t value) {
                           return value == 0;
                       })
                && std::all_of(resolver.mOffsetByCell.begin(),
                        resolver.mOffsetByCell.end(),
                        [](std::size_t value) {
                            return value == 0;
                        })
                && std::all_of(resolver.mCountByCell.begin(),
                        resolver.mCountByCell.end(),
                        [](std::uint8_t value) {
                            return value == 0;
                        });
    }

    static bool generationStampsAreZero(const NativeSimulationWorldRenderer& renderer) {
        const auto& generationByCell =
                renderer.mFrameBuilder.mContactChordResolver.mGenerationByCell;
        return std::all_of(generationByCell.begin(), generationByCell.end(),
                [](std::uint64_t value) {
                    return value == 0;
                });
    }

    static void seedCacheState(NativeSimulationWorldRenderer& renderer,
            std::uint64_t generation,
            std::size_t normalizedContactChordCount) {
        auto& resolver = renderer.mFrameBuilder.mContactChordResolver;
        resolver.mGeneration = generation;
        std::fill(resolver.mGenerationByCell.begin(),
                resolver.mGenerationByCell.end(), generation);
        std::fill(resolver.mOffsetByCell.begin(), resolver.mOffsetByCell.end(),
                normalizedContactChordCount);
        std::fill(resolver.mCountByCell.begin(), resolver.mCountByCell.end(),
                static_cast<std::uint8_t>(CellDataConstants::MAX_NEIGHBORS));
        resolver.mNormalizedContactChords.resize(normalizedContactChordCount);
        resolver.mNormalizedContactChordCount = normalizedContactChordCount;
    }

    static bool
    linkCandidateStorageIsClear(const NativeSimulationWorldRenderer& renderer) {
        return std::all_of(renderer.mSourceCapture.mLinkCandidateBits.begin(),
                renderer.mSourceCapture.mLinkCandidateBits.end(),
                [](std::uint64_t value) {
                    return value == 0;
                });
    }

    static std::size_t linkTriangleSize(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mPreparedFrame.linkTriangleCoords.size();
    }

    static std::size_t
    linkTriangleCapacity(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mPreparedFrame.linkTriangleCoords.capacity();
    }

    static std::size_t linkTextureSize(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mPreparedFrame.linkTextureCoords.size();
    }

    static std::size_t
    linkTextureCapacity(const NativeSimulationWorldRenderer& renderer) {
        return renderer.mPreparedFrame.linkTextureCoords.capacity();
    }

    static bool resetStorageIsCompact(const NativeSimulationWorldRenderer& renderer) {
        const auto compact = [](const auto& values) {
            return values.size() == values.capacity();
        };
        return std::all_of(renderer.mSourceFrame.orderedCellRecordIndices.begin(),
                       renderer.mSourceFrame.orderedCellRecordIndices.end(),
                       compact)
                && compact(renderer.mSourceFrame.cells) && compact(renderer.mSourceFrame.activeContactChords) && compact(renderer.mSourceFrame.activeContactChordNeighborRuntimeIds) && compact(renderer.mSourceFrame.candidateLinks) && compact(renderer.mSourceFrame.foods) && compact(renderer.mSourceFrame.deadSplashes) && compact(renderer.mSourceFrame.markers) && compact(renderer.mSourceCapture.mCellVisitMark) && compact(renderer.mSourceCapture.mSourceRecordIndexByCell) && compact(renderer.mSourceCapture.mSourceRecordGenerationByCell) && compact(renderer.mFrameBuilder.mContactChordResolver.mGenerationByCell) && compact(renderer.mFrameBuilder.mContactChordResolver.mOffsetByCell) && compact(renderer.mFrameBuilder.mContactChordResolver.mCountByCell) && renderer.mFrameBuilder.mContactChordResolver.mNormalizedContactChords.empty() && renderer.mFrameBuilder.mContactChordResolver.mNormalizedContactChords.capacity() == 0 && compact(renderer.mSourceCapture.mLinkCandidateBits) && compact(renderer.mPreparedFrame.markers) && compact(renderer.mPreparedFrame.cellTriangleCoords) && compact(renderer.mPreparedFrame.cellTextureCoords) && compact(renderer.mPreparedFrame.cellCenterCoords) && compact(renderer.mPreparedFrame.cellColors) && compact(renderer.mPreparedFrame.cellIndices) && compact(renderer.mPreparedFrame.foodInstanceAttributes) && compact(renderer.mPreparedFrame.foodVertexAttributes) && compact(renderer.mPreparedFrame.deadCellInstanceAttributes) && compact(renderer.mPreparedFrame.deadCellVertexAttributes) && compact(renderer.mPreparedFrame.linkTriangleCoords) && compact(renderer.mPreparedFrame.linkTextureCoords);
    }
};

} // namespace cellularity::tests

using namespace cellularity::tests;

namespace {

class OffscreenFakeGlApi final : public FakeGlApi {
public:
    int currentFramebuffer = 41;
    int currentPackAlignment = 4;
    int nextOffscreenHandle = 500;

    OffscreenFakeGlApi() {
        lastViewport = {7, 8, 9, 10};
    }

    int framebuffer() const override {
        return 201;
    }

    int renderbuffer() const override {
        return 202;
    }

    int colorAttachment0() const override {
        return 203;
    }

    int depthAttachment() const override {
        return 204;
    }

    int depthComponent16() const override {
        return 205;
    }

    int framebufferComplete() const override {
        return 206;
    }

    int framebufferBinding() const override {
        return 207;
    }

    int viewportState() const override {
        return 208;
    }

    int packAlignment() const override {
        return 209;
    }

    int rgba() const override {
        return 210;
    }

    int rgba8() const override {
        return 211;
    }

    int unsignedByte() const override {
        return 212;
    }

    int textureMinFilter() const override {
        return 213;
    }

    int textureMagFilter() const override {
        return 214;
    }

    int textureWrapS() const override {
        return 215;
    }

    int textureWrapT() const override {
        return 216;
    }

    int linear() const override {
        return 217;
    }

    int clampToEdge() const override {
        return 218;
    }

    void getIntegers(int parameterName, int* values) override {
        if (parameterName == framebufferBinding()) {
            values[0] = currentFramebuffer;
        } else if (parameterName == viewportState()) {
            std::copy(lastViewport.begin(), lastViewport.end(), values);
        } else if (parameterName == packAlignment()) {
            values[0] = currentPackAlignment;
        }
    }

    void pixelStore(int parameterName, int value) override {
        if (parameterName == packAlignment()) {
            currentPackAlignment = value;
        }
    }

    void genTextures(int count, int* textures) override {
        generateHandles(count, textures);
    }

    void textureParameter(int, int, int) override {}

    void textureImage2d(int, int, int, int, int, int, int, int,
            const void*) override {}

    void deleteTextures(int, const int*) override {}

    void genFramebuffers(int count, int* framebuffers) override {
        generateHandles(count, framebuffers);
    }

    void bindFramebuffer(int, int framebufferValue) override {
        currentFramebuffer = framebufferValue;
    }

    void framebufferTexture2d(int, int, int, int, int) override {}

    int checkFramebufferStatus(int) override {
        return framebufferComplete();
    }

    void deleteFramebuffers(int, const int*) override {}

    void genRenderbuffers(int count, int* renderbuffers) override {
        generateHandles(count, renderbuffers);
    }

    void bindRenderbuffer(int, int) override {}

    void renderbufferStorage(int, int, int, int) override {}

    void framebufferRenderbuffer(int, int, int, int) override {}

    void deleteRenderbuffers(int, const int*) override {}

    void readPixels(int, int, int width, int height, int, int,
            void* pixels) override {
        auto* bytes = static_cast<std::uint8_t*>(pixels);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t offset =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
                bytes[offset] = static_cast<std::uint8_t>(y & 0xff);
                bytes[offset + 1] = static_cast<std::uint8_t>(x & 0xff);
                bytes[offset + 2] = 17;
                bytes[offset + 3] = 255;
            }
        }
    }

private:
    void generateHandles(int count, int* handles) {
        for (int index = 0; index < count; ++index) {
            handles[index] = nextOffscreenHandle++;
        }
    }
};

int addRenderableTestEdge(CellData& cellData, int sourceCell, int targetCell,
        float marker);

template <typename T>
bool vectorsHaveExactBytes(const std::vector<T>& left,
        const std::vector<T>& right) {
    return left.size() == right.size() && (left.empty() || std::memcmp(left.data(), right.data(), left.size() * sizeof(T)) == 0);
}

void testPreparedRenderFrameOwnsCompleteSynchronousState() {
    Environment environment = Environment::constructDefaultEnvironment(4, 2, 1.0);
    environment.mLightAngle = 0.35;
    environment.mLightRange = 0.45;
    environment.mLightIntensity = 0.55;
    SimulationWorld world(false, environment);
    const int cellIndex = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[cellIndex] = 0.0;
    world.mCellData.mPositionY[cellIndex] = 0.0;
    world.mCellData.mRadius[cellIndex] = 0.02;
    world.updateGrids();

    NativeSimulationWorldRenderer renderer(4);
    const NativeSimulationWorldRenderer::ViewBounds visibleBounds{-0.5f, 0.5f, -0.4f,
            0.4f};
    const NativeSimulationWorldRenderer::ViewBounds coverageBounds{-0.6f, 0.6f, -0.5f,
            0.5f};
    NativeSimulationWorldRenderer::CaptureMetadata captureMetadata;
    captureMetadata.worldEpoch = 23;
    captureMetadata.modeGeneration = 9;
    captureMetadata.completedUpdateId = 47;
    captureMetadata.stage =
            cellularity::render::RenderCaptureStage::AfterCompletedUpdate;
    captureMetadata.environmentRevision = 5;
    captureMetadata.viewport.revision = 11;
    captureMetadata.viewport.coverageRevision = 11;
    captureMetadata.viewport.visibleBounds = visibleBounds;
    captureMetadata.viewport.coverageBounds = coverageBounds;
    captureMetadata.viewport.cameraX = -0.25f;
    captureMetadata.viewport.cameraY = 0.5f;
    captureMetadata.viewport.zoom = 2.0f;
    captureMetadata.viewport.viewWidth = 1.0f;
    captureMetadata.viewport.viewHeight = 0.8f;
    captureMetadata.viewport.surfaceWidth = 1280;
    captureMetadata.viewport.surfaceHeight = 720;

    const NativeSimulationWorldRenderer::MarkerCaptureRequest markerRequest{
            cellIndex, 0.01f, 0.8f
    };
    const int visibleCount = renderer.copyToDraw(
            world, coverageBounds, captureMetadata, &markerRequest, 1);
    const auto& built = renderer.preparedFrame();
    assertTrue(visibleCount == 1, "Prepared frame test has one visible Cell");
    assertTrue(!built.valid,
            "Prepared frame is not published before presentation state");
    assertTrue(built.geometryReady,
            "Prepared frame geometry is complete after synchronous build");
    assertTrue(built.identity.captureSequence != 0,
            "Prepared frame has a non-zero capture sequence");
    assertTrue(built.identity.worldEpoch == 23,
            "Prepared frame keeps world epoch");
    assertTrue(built.identity.modeGeneration == 9,
            "Prepared frame keeps mode generation");
    assertTrue(built.identity.completedUpdateId == 47,
            "Prepared frame keeps completed update id");
    assertTrue(built.identity.captureStage == cellularity::render::RenderCaptureStage::AfterCompletedUpdate,
            "Prepared frame keeps semantic capture stage");
    assertTrue(built.environment.revision == 5,
            "Prepared frame keeps environment revision");
    assertNear(environment.mLightIntensity, built.environment.lightIntensity, 0.0,
            "Prepared frame owns environment draw values");
    assertTrue(built.viewport.revision == 11,
            "Prepared frame keeps viewport revision");
    assertTrue(
            built.viewport.coverageRevision == 11,
            "Prepared frame ties extraction coverage to its viewport revision");
    assertNear(coverageBounds.maxX, built.viewport.coverageBounds.maxX, 0.0,
            "Prepared frame keeps extraction coverage");
    assertTrue(
            built.viewport.cameraX == -0.25f && built.viewport.cameraY == 0.5f && built.viewport.zoom == 2.0f && built.viewport.viewWidth == 1.0f && built.viewport.viewHeight == 0.8f && built.viewport.surfaceWidth == 1280 && built.viewport.surfaceHeight == 720,
            "Prepared frame owns the full source viewport snapshot");
    assertTrue(built.markers.size() == 1 && built.markers[0].selectedCellRuntimeId == world.mCellData.runtimeId(cellIndex) && built.markers[0].x == 0.0f && built.markers[0].radius == 0.03f && built.markers[0].alpha == 0.8f,
            "Prepared frame owns a stable selected-Cell marker record");
    assertTrue(built.layout.geometryRevision == built.identity.captureSequence,
            "Prepared frame geometry revision follows capture sequence");
    assertTrue(built.interpolation.mode == cellularity::render::RenderInterpolationMode::Disabled,
            "Synchronous prepared frame explicitly disables interpolation");
    assertTrue(
            built.interpolation.sourceCaptureSequence == built.identity.captureSequence && built.interpolation.targetCaptureSequence == built.identity.captureSequence,
            "Non-interpolated frame points both interpolation endpoints at itself");
    assertTrue(built.diagnostics.sourceCellCount == 1 && built.diagnostics.visibleCellCount == 1,
            "Prepared frame owns non-rendering diagnostics");
    assertTrue(&built.cellTriangleCoords == &renderer.cellTriangleCoords(),
            "Renderer has no parallel Cell triangle storage");
    assertTrue(&built.linkTriangleCoords == &renderer.linkTriangleCoords(),
            "Renderer has no parallel link storage");

    std::array<float, 16> matrix{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            0.25f, -0.5f, 0.0f, 1.0f};
    NativeSimulationWorldRenderer::PresentationState presentation;
    presentation.viewport = built.viewport;
    presentation.viewport.revision = 12;
    presentation.viewport.mvpMatrix = matrix;
    presentation.viewport.cameraX = 0.75f;
    presentation.viewport.cameraY = -0.625f;
    presentation.viewport.zoom = 4.0f;
    presentation.viewport.viewWidth = 0.5f;
    presentation.viewport.viewHeight = 0.4f;
    presentation.viewport.surfaceWidth = 2560;
    presentation.viewport.surfaceHeight = 1390;
    presentation.backgroundColor = {0.2f, 0.3f, 0.4f};
    world.mEnvironment.mLightIntensity = 99.0;
    world.mCellData.mPositionX[cellIndex] = 99.0;
    const auto& published = renderer.publishPreparedFrame(presentation);
    assertTrue(
            published.valid && published.geometryReady,
            "Complete prepared frame is published only after presentation state");
    assertTrue(published.viewport.mvpMatrix == matrix,
            "Published frame owns its MVP matrix");
    assertTrue(published.viewport.revision == 12 && published.viewport.coverageRevision == 11,
            "Published frame distinguishes presentation and coverage viewport "
            "revisions");
    assertTrue(published.viewport.surfaceWidth == 2560 && published.viewport.surfaceHeight == 1390,
            "Published frame owns surface dimensions");
    assertTrue(published.viewport.cameraX == 0.75f && published.viewport.cameraY == -0.625f && published.viewport.zoom == 4.0f && published.viewport.viewWidth == 0.5f && published.viewport.viewHeight == 0.4f,
            "Presentation can supply the newest immutable camera state");
    assertTrue(published.environment.backgroundColor == presentation.backgroundColor,
            "Published frame owns background color");
    assertTrue(
            published.environment.lightIntensity == 0.55 && published.markers.size() == 1 && published.markers[0].x == 0.0f,
            "Presentation does not reread or overwrite source-owned world values");
    assertTrue(
            published.layout.markerRevision == published.identity.captureSequence,
            "Marker revision follows its source capture rather than camera updates");

    const std::uint64_t firstSequence = published.identity.captureSequence;
    renderer.copyToDraw(world, coverageBounds, captureMetadata, &markerRequest,
            1);
    const auto& nextBuilt = renderer.preparedFrame();
    assertTrue(!nextBuilt.valid && nextBuilt.geometryReady,
            "Producer invalidates a returned synchronous slot before reuse");
    assertTrue(nextBuilt.identity.captureSequence != firstSequence,
            "Every prepared geometry capture has a distinct sequence");
}

void testRenderFrameBuilderMatchesRendererCompatibilityWrapper() {
    Environment environment = Environment::constructDefaultEnvironment(8, 2, 1.0);
    environment.mLightAngle = 0.2;
    environment.mLightRange = 0.4;
    environment.mLightIntensity = 0.6;
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int left = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int right = addAliveCell(cellData, CellType::NEUROCYTE);
    cellData.mPositionX[left] = -0.02;
    cellData.mPositionY[left] = 0.0;
    cellData.mRadius[left] = 0.02;
    cellData.mPositionX[right] = 0.02;
    cellData.mPositionY[right] = 0.0;
    cellData.mRadius[right] = 0.02;
    addRenderableTestEdge(cellData, left, right, 0.003f);
    world.mFoodData.addFood(0.0, 0.03, Environment::MAX_FOOD_SIZE, 0.25f,
            world.mEnvironment);
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    const cellularity::render::RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 31;
    metadata.modeGeneration = 11;
    metadata.completedUpdateId = 47;
    metadata.stage =
            cellularity::render::RenderCaptureStage::AfterCompletedUpdate;
    metadata.environmentRevision = 5;
    metadata.viewport.revision = 7;
    metadata.viewport.coverageRevision = 7;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;

    cellularity::render::PreparedRenderFrame directFrame;
    cellularity::render::RenderSourceFrame directSource;
    cellularity::render::RenderSourceCapture directCapture(
            environment.maxCellCount, directSource);
    cellularity::render::RenderFrameBuilder builder(environment.maxCellCount,
            directFrame);
    directCapture.capture(world, view, metadata, directSource);
    const int directVisible = builder.build(directSource, directFrame);

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    const int wrapperVisible = renderer.copyToDraw(world, view, metadata);
    const auto& wrapperFrame = renderer.preparedFrame();

    assertTrue(directVisible == wrapperVisible,
            "Direct RenderFrameBuilder and renderer wrapper return the same "
            "visible count");
    assertTrue(directFrame.valid == wrapperFrame.valid && directFrame.geometryReady == wrapperFrame.geometryReady && directFrame.identity.captureSequence == wrapperFrame.identity.captureSequence && directFrame.identity.worldEpoch == wrapperFrame.identity.worldEpoch && directFrame.identity.modeGeneration == wrapperFrame.identity.modeGeneration && directFrame.identity.completedUpdateId == wrapperFrame.identity.completedUpdateId && directFrame.identity.captureStage == wrapperFrame.identity.captureStage,
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "identity state");
    assertTrue(directFrame.environment.revision == wrapperFrame.environment.revision && directFrame.environment.previewMode == wrapperFrame.environment.previewMode && directFrame.environment.lightAngle == wrapperFrame.environment.lightAngle && directFrame.environment.lightRange == wrapperFrame.environment.lightRange && directFrame.environment.lightIntensity == wrapperFrame.environment.lightIntensity && directFrame.environment.dishRadius == wrapperFrame.environment.dishRadius && directFrame.environment.backgroundColor == wrapperFrame.environment.backgroundColor,
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "environment state");
    assertTrue(directFrame.viewport.revision == wrapperFrame.viewport.revision && directFrame.viewport.coverageRevision == wrapperFrame.viewport.coverageRevision && directFrame.viewport.visibleBounds.minX == wrapperFrame.viewport.visibleBounds.minX && directFrame.viewport.visibleBounds.maxX == wrapperFrame.viewport.visibleBounds.maxX && directFrame.viewport.visibleBounds.minY == wrapperFrame.viewport.visibleBounds.minY && directFrame.viewport.visibleBounds.maxY == wrapperFrame.viewport.visibleBounds.maxY && directFrame.viewport.coverageBounds.minX == wrapperFrame.viewport.coverageBounds.minX && directFrame.viewport.coverageBounds.maxX == wrapperFrame.viewport.coverageBounds.maxX && directFrame.viewport.coverageBounds.minY == wrapperFrame.viewport.coverageBounds.minY && directFrame.viewport.coverageBounds.maxY == wrapperFrame.viewport.coverageBounds.maxY,
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "coverage state");
    assertTrue(
            directFrame.layout.version == wrapperFrame.layout.version && directFrame.layout.geometryRevision == wrapperFrame.layout.geometryRevision && directFrame.layout.instanceRevision == wrapperFrame.layout.instanceRevision && directFrame.layout.markerRevision == wrapperFrame.layout.markerRevision && directFrame.interpolation.mode == wrapperFrame.interpolation.mode && directFrame.interpolation.sourceCaptureSequence == wrapperFrame.interpolation.sourceCaptureSequence && directFrame.interpolation.targetCaptureSequence == wrapperFrame.interpolation.targetCaptureSequence && directFrame.interpolation.alpha == wrapperFrame.interpolation.alpha,
            "Direct RenderFrameBuilder and renderer wrapper produce exact layout "
            "state");
    assertTrue(directFrame.diagnostics.buildNanoseconds == wrapperFrame.diagnostics.buildNanoseconds && directFrame.diagnostics.sourceCellCount == wrapperFrame.diagnostics.sourceCellCount && directFrame.diagnostics.sourceFoodCount == wrapperFrame.diagnostics.sourceFoodCount && directFrame.diagnostics.sourceDeadCellCount == wrapperFrame.diagnostics.sourceDeadCellCount && directFrame.diagnostics.sourceTopologyEdgeCount == wrapperFrame.diagnostics.sourceTopologyEdgeCount && directFrame.diagnostics.visibleCellCount == wrapperFrame.diagnostics.visibleCellCount && directFrame.diagnostics.normalizedContactChordCount == wrapperFrame.diagnostics.normalizedContactChordCount,
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "diagnostics");
    assertTrue(
            directFrame.markers.empty() && wrapperFrame.markers.empty(),
            "Direct RenderFrameBuilder and renderer wrapper produce exact markers");
    assertTrue(vectorsHaveExactBytes(directFrame.cellTriangleCoords,
                       wrapperFrame.cellTriangleCoords),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "Cell positions");
    assertTrue(vectorsHaveExactBytes(directFrame.cellTextureCoords,
                       wrapperFrame.cellTextureCoords),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "Cell attributes");
    assertTrue(vectorsHaveExactBytes(directFrame.cellCenterCoords,
                       wrapperFrame.cellCenterCoords),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "Cell centers");
    assertTrue(
            vectorsHaveExactBytes(directFrame.cellColors, wrapperFrame.cellColors),
            "Direct RenderFrameBuilder and renderer wrapper produce exact Cell "
            "colors");
    assertTrue(
            vectorsHaveExactBytes(directFrame.cellIndices, wrapperFrame.cellIndices),
            "Direct RenderFrameBuilder and renderer wrapper produce exact Cell "
            "indices");
    assertTrue(vectorsHaveExactBytes(directFrame.foodInstanceAttributes,
                       wrapperFrame.foodInstanceAttributes),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "Food instances");
    assertTrue(vectorsHaveExactBytes(directFrame.foodVertexAttributes,
                       wrapperFrame.foodVertexAttributes),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "Food attributes");
    assertTrue(vectorsHaveExactBytes(directFrame.deadCellInstanceAttributes,
                       wrapperFrame.deadCellInstanceAttributes),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "dead instances");
    assertTrue(vectorsHaveExactBytes(directFrame.deadCellVertexAttributes,
                       wrapperFrame.deadCellVertexAttributes),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "dead attributes");
    assertTrue(vectorsHaveExactBytes(directFrame.linkTriangleCoords,
                       wrapperFrame.linkTriangleCoords),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "Link positions");
    assertTrue(vectorsHaveExactBytes(directFrame.linkTextureCoords,
                       wrapperFrame.linkTextureCoords),
            "Direct RenderFrameBuilder and renderer wrapper produce exact "
            "Link attributes");
    assertTrue(
            directFrame.cellVertexCountBefore == wrapperFrame.cellVertexCountBefore && directFrame.cellVertexCount == wrapperFrame.cellVertexCount && directFrame.cellIndexCountBefore == wrapperFrame.cellIndexCountBefore && directFrame.cellIndexCount == wrapperFrame.cellIndexCount && directFrame.bunchStart == wrapperFrame.bunchStart && directFrame.bunchSize == wrapperFrame.bunchSize && directFrame.bunchIndexStart == wrapperFrame.bunchIndexStart && directFrame.bunchIndexSize == wrapperFrame.bunchIndexSize && directFrame.bunches == wrapperFrame.bunches && directFrame.cellDrawVertexCount == wrapperFrame.cellDrawVertexCount && directFrame.cellDrawIndexCount == wrapperFrame.cellDrawIndexCount && directFrame.cellCenterFloatCount == wrapperFrame.cellCenterFloatCount && directFrame.foodInstanceCount == wrapperFrame.foodInstanceCount && directFrame.deadCellInstanceCount == wrapperFrame.deadCellInstanceCount && directFrame.drawLinksCount == wrapperFrame.drawLinksCount,
            "Direct RenderFrameBuilder and renderer wrapper produce exact draw "
            "metadata");
}

void assertWallExact(const RenderContactChord& expected, const RenderContactChord& actual,
        const std::string& message) {
    assertNear(expected.x1, actual.x1, 0.0, message + " x1");
    assertNear(expected.y1, actual.y1, 0.0, message + " y1");
    assertNear(expected.x2, actual.x2, 0.0, message + " x2");
    assertNear(expected.y2, actual.y2, 0.0, message + " y2");
    assertTrue(expected.isDiscarded == actual.isDiscarded, message + " broken flag");
    assertTrue(expected.joinsNextChord == actual.joinsNextChord,
            message + " continues flag");
    assertTrue(expected.neighborCellIndex == actual.neighborCellIndex, message + " cell index");
}

void appendTestContactChord(RenderContactChordBuffer& contactChords,
        int cellIndex,
        const RenderContactChord& contactChord) {
    assertTrue(contactChords.tryAppend(cellIndex, contactChord),
            "Native renderer fixture stores a contact chord");
}

std::vector<RenderContactChord>
copyActiveContactChords(const RenderContactChordBuffer& contactChords,
        int cellIndex) {
    const auto activeContactChords = contactChords.activeChords(cellIndex);
    return {activeContactChords.begin(), activeContactChords.end()};
}

void setCircularTestWalls(RenderContactChordBuffer& contactChords,
        int cellIndex, int contactChordCount, double radius,
        int neighborBase) {
    assertTrue(contactChordCount >= 0 && contactChordCount <= cellularity::render::kMaximumRenderContactChordsPerCell,
            "Native renderer circular test wall count is valid");
    const double arc =
            2.0 * Environment::PI / static_cast<double>(std::max(1, contactChordCount));
    for (int wallIndex = 0; wallIndex < contactChordCount; wallIndex++) {
        const double center = -Environment::PI + (wallIndex + 0.5) * arc;
        const double start = center - arc * 0.31;
        const double end = center + arc * 0.31;
        RenderContactChord wall;
        wall.x1 = radius * std::cos(start);
        wall.y1 = radius * std::sin(start);
        wall.x2 = radius * std::cos(end);
        wall.y2 = radius * std::sin(end);
        wall.isDiscarded = false;
        wall.joinsNextChord = false;
        wall.neighborCellIndex = neighborBase + wallIndex;
        appendTestContactChord(contactChords, cellIndex, wall);
    }
}

int addRenderableTestEdge(CellData& cellData, int sourceCell, int targetCell,
        float marker) {
    addPairLink(cellData, sourceCell, targetCell, 0.0, std::acos(-1.0), 0.2f,
            0.01f);
    const int edgeIndex = cellData.mTopology.edgeCount() - 1;
    cellData.mTopology.setEdgeRenderOffsets(edgeIndex, marker, 0.0f, -marker,
            0.0f);
    constexpr double syntheticDistance = 0.03;
    double directionX = cellData.mPositionX[sourceCell] - cellData.mPositionX[targetCell];
    double directionY = cellData.mPositionY[sourceCell] - cellData.mPositionY[targetCell];
    double directionLength = std::hypot(directionX, directionY);
    if (directionLength == 0.0) {
        const double angle = static_cast<double>(sourceCell * 37 + targetCell * 17) * (Environment::PI / 97.0);
        directionX = std::cos(angle);
        directionY = std::sin(angle);
        directionLength = 1.0;
    }
    const double distanceX = directionX * syntheticDistance / directionLength;
    const double distanceY = directionY * syntheticDistance / directionLength;
    assertTrue(cellData.addKnownCellCollisionContactFast(
                       sourceCell, targetCell, distanceX, distanceY,
                       syntheticDistance * syntheticDistance),
            "Renderer edge fixture stores the source contact");
    assertTrue(cellData.addKnownCellCollisionContactFast(
                       targetCell, sourceCell, -distanceX, -distanceY,
                       syntheticDistance * syntheticDistance),
            "Renderer edge fixture stores the reciprocal contact");
    return edgeIndex;
}

int cellRenderOverflowGridEntryCount(const SimulationWorld& world, int gridIndex) {
    int count = 0;
    for (int cellIndex = world.mCellRenderOverflowGridHead[gridIndex];
            cellIndex != -1;
            cellIndex = world.mCellRenderOverflowGridNext[cellIndex]) {
        count++;
    }
    return count;
}

int cellRenderGridEntryCount(const SimulationWorld& world, int gridIndex) {
    return world.mCellCollisionGrid->countAt(gridIndex) + cellRenderOverflowGridEntryCount(world, gridIndex);
}

bool cellRenderGridBucketIsOccupied(const SimulationWorld& world, int gridIndex);

int cellRenderGridTotalEntryCount(const SimulationWorld& world) {
    int count = 0;
    for (int gridIndex = 0;
            gridIndex < static_cast<int>(world.mCellRenderOverflowGridHead.size());
            gridIndex++) {
        if (cellRenderGridBucketIsOccupied(world, gridIndex)) {
            count += cellRenderGridEntryCount(world, gridIndex);
        }
    }
    return count;
}

bool cellRenderGridBucketIsOccupied(const SimulationWorld& world, int gridIndex) {
    return (world.mCellRenderGridOccupied[static_cast<std::size_t>(gridIndex) / 64U] & (std::uint64_t{1} << (static_cast<unsigned int>(gridIndex) & 63U))) != 0;
}

const RenderContactChord& findWallForCell(
        const std::vector<RenderContactChord>& walls, int connectedCellIndex,
        const char* message) {
    for (const RenderContactChord& wall : walls) {
        if (wall.neighborCellIndex == connectedCellIndex) {
            return wall;
        }
    }
    fail(message);
    std::abort();
}

std::array<float, 4> expectedContactLinkCrossSection(
        const RenderContactChordBuffer& captured, const CellData& cells, int owner, int target) {
    auto normalized = copyActiveContactChords(captured, owner);
    cellularity::render::clipAndOrderRenderContactChords(normalized.data(), static_cast<int>(normalized.size()));
    const auto& chord = findWallForCell(normalized, target, "Expected Link contact is present");
    assertTrue(!chord.isDiscarded, "Expected Link contact remains drawable after normalization");
    std::array<float, 4> base{static_cast<float>(chord.x1 + static_cast<float>(cells.mPositionX[owner])),
            static_cast<float>(chord.y1 + static_cast<float>(cells.mPositionY[owner])),
            static_cast<float>(chord.x2 + static_cast<float>(cells.mPositionX[owner])),
            static_cast<float>(chord.y2 + static_cast<float>(cells.mPositionY[owner]))};
    const float width = std::hypot(base[2] - base[0], base[3] - base[1]);
    const float minimumWidth = static_cast<float>(CellDataConstants::MIN_CELL_RADIUS * 0.7);
    assertTrue(width > 0.0f, "Expected contact cross-section is non-degenerate");
    if (width < minimumWidth) {
        const float centerX = (base[0] + base[2]) * 0.5f;
        const float centerY = (base[1] + base[3]) * 0.5f;
        const float scale = minimumWidth / width;
        for (int endpoint = 0; endpoint < 4; endpoint += 2) {
            base[endpoint] = centerX + (base[endpoint] - centerX) * scale;
            base[endpoint + 1] = centerY + (base[endpoint + 1] - centerY) * scale;
        }
    }
    return base;
}

#ifdef CELLULARITY_RENDER_PROFILE
void assertWallCacheProfileInvariants(
        const NativeSimulationWorldRenderer::CopyToDrawProfile& profile,
        const std::string& message) {
    assertTrue(profile.contactChordResolveRequests == profile.contactChordDirectSourceViews + profile.contactChordCacheHits + profile.contactChordCacheMisses,
            message + " request accounting");
    assertTrue(profile.contactChordCacheMisses == profile.uniqueNormalizedCells,
            message + " unique normalized cells");
    assertTrue(profile.contactChordCacheMisses == profile.normalizerCalls,
            message + " normalizer calls");
    assertTrue(profile.normalizedContactChordCount == profile.normalizerContactChordCountSum,
            message + " normalized wall count");
    assertTrue(profile.linkOnlyCacheMisses == profile.linkPassNormalizerCalls,
            message + " each Link-only cache miss normalizes its owner once");
    assertTrue(profile.normalizerCalls == profile.cellPassNormalizerCalls + profile.linkPassNormalizerCalls,
            message + " normalizer phase calls");
    assertTrue(profile.normalizerContactChordCountSum == profile.cellPassNormalizerContactChordCountSum + profile.linkPassNormalizerContactChordCountSum,
            message + " normalizer phase walls");
    assertTrue(profile.contactChordResolutionNanos == profile.cellPassContactChordResolutionNanos + profile.linkPassContactChordResolutionNanos,
            message + " resolution phase timing");
    assertTrue(profile.contactChordNormalizationNanos == profile.cellPassContactChordNormalizationNanos + profile.linkPassContactChordNormalizationNanos,
            message + " normalization phase timing");
    assertTrue(profile.linkContactCrossSectionCount + profile.linkNormalFallbackCount
                            + profile.linkBoundaryFallbackCount + profile.linkDiscardedContactCount
                            + profile.degenerateLinkCount
                    == profile.sourceCandidateLinkCount,
            message + " every Link candidate has exactly one geometry outcome");
}
#endif

#if !defined(NDEBUG) && (defined(__unix__) || defined(__APPLE__))
template <typename Action>
void assertAborts(Action action, const std::string& message) {
    const pid_t child = fork();
    assertTrue(child >= 0, message + " fork");
    if (child == 0) {
        action();
        _exit(0);
    }

    int status = 0;
    assertTrue(waitpid(child, &status, 0) == child, message + " waitpid");
    assertTrue(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, message);
}
#endif

void testNativeSimulationWorldRendererGridCullingReadsWorldDirectly() {
    Environment environment = Environment::constructDefaultEnvironment(5, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    int phagocyte = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[phagocyte] = 0.0;
    cellData.mPositionY[phagocyte] = 0.0;
    cellData.mRadius[phagocyte] = 0.02;

    int neurocyte = addAliveCell(cellData, CellType::NEUROCYTE);
    cellData.mPositionX[neurocyte] = 0.05;
    cellData.mPositionY[neurocyte] = 0.0;
    cellData.mRadius[neurocyte] = 0.02;

    int outOfView = addAliveCell(cellData, CellType::LIPOCYTE);
    cellData.mPositionX[outOfView] = 0.5;
    cellData.mPositionY[outOfView] = 0.0;
    cellData.mRadius[outOfView] = 0.02;

    int dead = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[dead] = 0.0;
    cellData.mPositionY[dead] = 0.0;
    cellData.mRadius[dead] = 0.02;
    cellData.markCellAsDead(dead);

    world.updateGrids();

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.04f, 0.04f, -0.04f, 0.04f};

    int visibleCount = renderer.copyToDraw(world, view);

    assertTrue(visibleCount == 2, "Native renderer grid culling visible count");
    assertTrue(renderer.visibleCellCount() == 2,
            "Native renderer grid culling stores visible count");
    assertTrue(renderer.visibleCellCountForBucket(
                       CellTypes::ordinal(CellType::PHAGOCYTE))
                    == 1,
            "Native renderer phagocyte bucket count");
    assertTrue(renderer.visibleCellIndex(CellTypes::ordinal(CellType::PHAGOCYTE),
                       0)
                    == phagocyte,
            "Native renderer phagocyte bucket index");
    assertTrue(renderer.visibleCellCountForBucket(
                       CellTypes::ordinal(CellType::NEUROCYTE))
                    == 1,
            "Native renderer neurocyte bucket count");
    assertTrue(renderer.visibleCellIndex(CellTypes::ordinal(CellType::NEUROCYTE),
                       0)
                    == neurocyte,
            "Native renderer neurocyte bucket index");
    assertTrue(renderer.visibleCellCountForBucket(
                       CellTypes::ordinal(CellType::LIPOCYTE))
                    == 0,
            "Native renderer out-of-view skipped");
    assertTrue(renderer.visibleCellCountForBucket(
                       NativeSimulationWorldRenderer::PREVIEW_EXTRA_CELL_TYPE_INDEX)
                    == 0,
            "Native renderer no preview bucket for normal world");
    assertTrue(renderer.cellDrawVertexCount() == 8,
            "Native renderer cell quads vertex count");
    assertTrue(renderer.cellDrawIndexCount() == 12,
            "Native renderer cell quads index count");
    assertNear(0.02, renderer.cellTriangleCoords()[0], 1e-7,
            "Native renderer first cell local x0");
    assertNear(0.02, renderer.cellTriangleCoords()[1], 1e-7,
            "Native renderer first cell local y0");
    assertNear(0.0, renderer.cellCenterCoords()[0], 1e-7,
            "Native renderer first cell center x");
    assertNear(0.0, renderer.cellCenterCoords()[1], 1e-7,
            "Native renderer first cell center y");
    assertTrue(renderer.cellIndices()[0] == 0 && renderer.cellIndices()[5] == 3,
            "Native renderer first cell indices");
}

void testSimulationWorldRenderGridIsExactAndRebuildsTouchedBuckets() {
    Environment environment =
            Environment::constructDefaultEnvironment(210, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    constexpr int centerCellCount = 205;
    for (int index = 0; index < centerCellCount; index++) {
        const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
        cellData.mPositionX[cell] = 0.0;
        cellData.mPositionY[cell] = 0.0;
        cellData.mRadius[cell] = 0.02;
    }
    cellData.markCellAsDead(centerCellCount - 1);

    const int negativeOutside = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[negativeOutside] = -1.25;
    cellData.mPositionY[negativeOutside] = 0.0;
    const int positiveOutside = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[positiveOutside] = 1.25;
    cellData.mPositionY[positiveOutside] = 0.0;
    const int nonFinite = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[nonFinite] = std::numeric_limits<double>::quiet_NaN();
    cellData.mPositionY[nonFinite] = 0.0;

    world.updateGrids();

    const CollisionGrid& grid = *world.mCellCollisionGrid;
    const int centerGridIndex = grid.indexAtWorldPosition(0.0, 0.0);
    const int negativeBorderIndex = grid.indexAtGridPosition(
            0, grid.clampGridPosition(grid.convertWorldToGridPosition(0.0)));
    const int positiveBorderIndex = grid.indexAtGridPosition(
            grid.gridSize() + 1,
            grid.clampGridPosition(grid.convertWorldToGridPosition(0.0)));
    assertTrue(grid.countAt(centerGridIndex) == SimulationWorld::CELL_COLLISION_GRID_COUNT_MAX,
            "Cell collision grid reaches its fixed bucket limit");
    assertTrue(cellRenderGridEntryCount(world, centerGridIndex) == centerCellCount,
            "Cell render grid retains every same-bucket cell beyond collision "
            "capacity");
    assertTrue(
            cellRenderOverflowGridEntryCount(world, centerGridIndex) == 2,
            "Cell render overflow stores the dead and over-capacity center cells");
    assertTrue(cellRenderGridBucketIsOccupied(world, centerGridIndex),
            "Cell render grid occupancy marks populated center bucket");
    assertTrue(cellRenderGridEntryCount(world, negativeBorderIndex) == 1,
            "Cell render grid retains finite negative off-world cell");
    assertTrue(cellRenderGridEntryCount(world, positiveBorderIndex) == 1,
            "Cell render grid retains finite positive off-world cell");
    assertTrue(cellRenderGridTotalEntryCount(world) == centerCellCount + 2,
            "Cell render grid includes dead finite cells and excludes "
            "non-finite cells");
    assertTrue(world.mCellRenderOverflowGridNext[nonFinite] == -1,
            "Cell render grid leaves non-finite cell unlinked");

    cellData.mPositionX[0] = 0.5;
    world.updateGrids();
    const int movedGridIndex = grid.indexAtWorldPosition(0.5, 0.0);
    assertTrue(cellRenderGridEntryCount(world, centerGridIndex) == centerCellCount - 1,
            "Cell render grid rebuild clears prior center membership");
    assertTrue(
            cellRenderGridBucketIsOccupied(world, centerGridIndex),
            "Cell render grid occupancy retains a still-populated center bucket");
    assertTrue(cellRenderGridEntryCount(world, movedGridIndex) == 1,
            "Cell render grid rebuild inserts moved cell once");
    assertTrue(cellRenderGridBucketIsOccupied(world, movedGridIndex),
            "Cell render grid occupancy marks a newly populated bucket");
    assertTrue(cellRenderGridTotalEntryCount(world) == centerCellCount + 2,
            "Cell render grid rebuild preserves exact finite entry count");

    cellData.mPositionX[positiveOutside] = 0.75;
    world.updateGrids();
    assertTrue(
            !cellRenderGridBucketIsOccupied(world, positiveBorderIndex),
            "Cell render grid occupancy clears a bucket after its last cell moves");
    assertTrue(cellRenderGridEntryCount(world, positiveBorderIndex) == 0,
            "Cell render grid head clears a bucket after its last cell moves");
}

void testNativeSimulationWorldRendererCandidatesPreserveGlobalOrderBeyondCollisionCapacity() {
    constexpr int originalEdgeCount = 130;
    Environment environment = Environment::constructDefaultEnvironment(
            originalEdgeCount * 2 + 4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    std::array<int, originalEdgeCount> sourceCells{};

    for (int edge = 0; edge < originalEdgeCount; edge++) {
        const int source = addAliveCell(cellData, CellType::PHAGOCYTE);
        const int target = addAliveCell(cellData, CellType::PHAGOCYTE);
        sourceCells[edge] = source;
        cellData.mPositionX[source] = 0.0;
        cellData.mPositionY[source] = 0.0;
        cellData.mPositionX[target] = 0.001;
        cellData.mPositionY[target] = 0.0;
        cellData.mRadius[source] = 0.02;
        cellData.mRadius[target] = 0.02;
        const int addedEdge = addRenderableTestEdge(
                cellData, source, target, static_cast<float>(edge) * 0.0001f);
        assertTrue(addedEdge == edge,
                "Renderer candidate fixture edge index is sequential");
    }
    cellData.mTopology.addBoundaryLink(sourceCells[1], 0.0, 0.0, 0.0, 0.0f, 0.0f);
    cellData.markCellAsDead(sourceCells[originalEdgeCount - 1]);
    cellData.mTopology.removeCellLink(0);
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    const int centerGridIndex =
            world.mCellCollisionGrid->indexAtWorldPosition(0.0, 0.0);
    assertTrue(
            world.mCellCollisionGrid->countAt(centerGridIndex) == SimulationWorld::CELL_COLLISION_GRID_COUNT_MAX,
            "Renderer candidate fixture overflows simulation collision bucket");
    assertTrue(
            cellRenderGridEntryCount(world, centerGridIndex) == originalEdgeCount * 2,
            "Renderer candidate fixture remains complete in exact render grid");

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    const NativeSimulationWorldRenderer::ViewBounds view{-0.02f, 0.02f, -0.02f, 0.02f};
    renderer.copyToDraw(world, view);

    const int expectedCandidateEdgeCount = originalEdgeCount - 1;
    const int expectedDrawableEdgeCount = expectedCandidateEdgeCount - 1;
    assertTrue(renderer.drawLinksCount() == expectedDrawableEdgeCount * 6,
            "Renderer candidate path emits every live drawable edge once");
    for (int outputEdge = 0; outputEdge < expectedDrawableEdgeCount;
            outputEdge++) {
        const int originalEdge = outputEdge + 1;
        const float expectedMarker = 0.001f - static_cast<float>(originalEdge) * 0.0001f;
        assertNear(expectedMarker, renderer.linkTriangleCoords()[outputEdge * 24],
                0.0,
                "Renderer candidate path preserves compacted global edge order");
    }
    assertTrue(
            NativeSimulationWorldRendererTestAccess::linkCandidateStorageIsClear(renderer),
            "Renderer candidate sparse storage is clear after extraction");

#ifdef CELLULARITY_RENDER_PROFILE
    const NativeSimulationWorldRenderer::CopyToDrawProfile profile =
            renderer.profileCopyToDraw(world, view);
    assertTrue(profile.linkTopologyEdgeCount == expectedCandidateEdgeCount,
            "Renderer candidate profile reports total topology edges");
    assertTrue(profile.linkGridCellVisits == originalEdgeCount * 2,
            "Renderer candidate profile visits every exact-grid cell");
    assertTrue(profile.linkEndpointCellCount == originalEdgeCount * 2,
            "Renderer candidate profile includes transient dead endpoint");
    assertTrue(
            profile.linkAdjacencyVisits == expectedCandidateEdgeCount * 2 + 1,
            "Renderer candidate profile visits normal and boundary adjacency slots");
    assertTrue(profile.linkCandidateEdgeCount == expectedDrawableEdgeCount,
            "Renderer candidate profile deduplicates edges discovered from "
            "both endpoints");
    assertTrue(profile.linkCandidateWordCount == 3,
            "Renderer candidate profile reports sparse words across 64-edge "
            "boundaries");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::linkCandidateStorageIsClear(renderer),
            "Renderer profiled candidate sparse storage is clear after extraction");
#endif
}

void testNativeSimulationWorldRendererCandidatesCoverOffWorldBordersAndClearBetweenViews() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    const int negativeSource = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int negativeTarget = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[negativeSource] = -1.25;
    cellData.mPositionY[negativeSource] = 0.0;
    cellData.mPositionX[negativeTarget] = -1.249;
    cellData.mPositionY[negativeTarget] = 0.0;
    addRenderableTestEdge(cellData, negativeSource, negativeTarget, 0.01f);

    const int positiveSource = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int positiveTarget = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[positiveSource] = 1.25;
    cellData.mPositionY[positiveSource] = 0.0;
    cellData.mPositionX[positiveTarget] = 1.251;
    cellData.mPositionY[positiveTarget] = 0.0;
    addRenderableTestEdge(cellData, positiveSource, positiveTarget, 0.02f);
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    assertTrue(
            world.mCellCollisionGrid->touchedCount() == 0,
            "Off-world candidate fixture is absent from simulation collision grid");

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    renderer.copyToDraw(world, {-1.27f, -1.23f, -0.02f, 0.02f});
    assertTrue(renderer.drawLinksCount() == 6,
            "Renderer candidate path finds negative off-world border edge");
    assertNear(-1.259, renderer.linkTriangleCoords()[0], 1e-7,
            "Renderer candidate negative border edge marker");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::linkCandidateStorageIsClear(renderer),
            "Renderer candidate storage clears after negative border view");

    renderer.copyToDraw(world, {-0.02f, 0.02f, -0.02f, 0.02f});
    assertTrue(
            renderer.drawLinksCount() == 0,
            "Renderer candidate path emits no stale edges in empty center view");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::linkCandidateStorageIsClear(renderer),
            "Renderer candidate storage remains clear after zero-candidate view");

    renderer.copyToDraw(world, {1.23f, 1.27f, -0.02f, 0.02f});
    assertTrue(renderer.drawLinksCount() == 6,
            "Renderer candidate path finds positive off-world border edge");
    assertNear(1.231, renderer.linkTriangleCoords()[0], 1e-7,
            "Renderer candidate positive border edge marker");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::linkCandidateStorageIsClear(renderer),
            "Renderer candidate storage clears after positive border view");
}

void testNativeSimulationWorldRendererCandidateCullIsInclusiveAndFinite() {
    Environment environment =
            Environment::constructDefaultEnvironment(24, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    constexpr float cullRadius =
            static_cast<float>(CellData::MAX_CELL_RADIUS * 2.0);

    auto addEdgeAt = [&](float sourceX, float sourceY, float targetX,
                             float targetY) {
        const int source = addAliveCell(cellData, CellType::PHAGOCYTE);
        const int target = addAliveCell(cellData, CellType::PHAGOCYTE);
        cellData.mPositionX[source] = sourceX;
        cellData.mPositionY[source] = sourceY;
        cellData.mPositionX[target] = targetX;
        cellData.mPositionY[target] = targetY;
        cellData.mRadius[source] = 0.02;
        cellData.mRadius[target] = 0.02;
        addRenderableTestEdge(cellData, source, target, 0.0f);
    };

    addEdgeAt(-cullRadius, 0.0f, -0.2f, 0.0f);
    addEdgeAt(
            std::nextafter(-cullRadius, -std::numeric_limits<float>::infinity()),
            0.0f, -0.2f, 0.0f);
    addEdgeAt(cullRadius, 0.0f, 0.2f, 0.0f);
    addEdgeAt(std::nextafter(cullRadius, std::numeric_limits<float>::infinity()),
            0.0f, 0.2f, 0.0f);
    addEdgeAt(0.0f, -cullRadius, 0.0f, -0.2f);
    addEdgeAt(
            0.0f,
            std::nextafter(-cullRadius, -std::numeric_limits<float>::infinity()),
            0.0f, -0.2f);
    addEdgeAt(0.0f, cullRadius, 0.0f, 0.2f);
    addEdgeAt(0.0f,
            std::nextafter(cullRadius, std::numeric_limits<float>::infinity()),
            0.0f, 0.2f);
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    const NativeSimulationWorldRenderer::ViewBounds pointView{0.0f, 0.0f, 0.0f, 0.0f};
    renderer.copyToDraw(world, pointView);
    assertTrue(renderer.drawLinksCount() == 4 * 6,
            "Renderer candidate cull includes exact boundaries and excludes "
            "adjacent floats");

    NativeSimulationWorldRenderer::ViewBounds nonFiniteView = pointView;
    nonFiniteView.minX = std::numeric_limits<float>::quiet_NaN();
    renderer.copyToDraw(world, nonFiniteView);
    assertTrue(
            renderer.drawLinksCount() == 0,
            "Renderer candidate cull does not accept a non-finite view interval");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::linkCandidateStorageIsClear(renderer),
            "Renderer candidate storage clears after a non-finite view interval");
}

void testNativeSimulationWorldRendererPreviewLinearCullingAndExtraBucket() {
    Environment environment = Environment::setupPreviewSimulationEnvironment(4);
    SimulationWorld world(true, environment);
    CellData& cellData = world.mCellData;

    int visible = addAliveCell(cellData, CellType::PHOTOCYTE);
    cellData.mPositionX[visible] = 0.0;
    cellData.mPositionY[visible] = 0.0;

    int outOfView = addAliveCell(cellData, CellType::DEVOROCYTE);
    cellData.mPositionX[outOfView] = 0.5;
    cellData.mPositionY[outOfView] = 0.0;

    int dead = addAliveCell(cellData, CellType::GLUEOCYTE);
    cellData.mPositionX[dead] = 0.0;
    cellData.mPositionY[dead] = 0.0;
    cellData.markCellAsDead(dead);

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.04f, 0.04f, -0.04f, 0.04f};

    int visibleCount = renderer.copyToDraw(world, view);

    assertTrue(
            visibleCount == 1,
            "Native renderer preview visible count excludes dead and out-of-view");
    assertTrue(renderer.visibleCellCountForBucket(
                       CellTypes::ordinal(CellType::PHOTOCYTE))
                    == 1,
            "Native renderer preview photocyte bucket count");
    assertTrue(renderer.visibleCellIndex(CellTypes::ordinal(CellType::PHOTOCYTE),
                       0)
                    == visible,
            "Native renderer preview photocyte index");
    assertTrue(renderer.visibleCellCountForBucket(
                       CellTypes::ordinal(CellType::DEVOROCYTE))
                    == 0,
            "Native renderer preview out-of-view skipped");
    assertTrue(renderer.visibleCellCountForBucket(
                       CellTypes::ordinal(CellType::GLUEOCYTE))
                    == 0,
            "Native renderer preview dead skipped from type bucket");
    assertTrue(renderer.visibleCellCountForBucket(
                       NativeSimulationWorldRenderer::PREVIEW_EXTRA_CELL_TYPE_INDEX)
                    == cellData.mCellCount,
            "Native renderer preview extra bucket keeps all cells");
    assertTrue(renderer.visibleCellIndex(
                       NativeSimulationWorldRenderer::PREVIEW_EXTRA_CELL_TYPE_INDEX, 0)
                    == 0,
            "Native renderer preview extra bucket first");
    assertTrue(renderer.visibleCellIndex(
                       NativeSimulationWorldRenderer::PREVIEW_EXTRA_CELL_TYPE_INDEX, 2)
                    == 2,
            "Native renderer preview extra bucket includes dead");
#ifdef CELLULARITY_RENDER_PROFILE
    const NativeSimulationWorldRenderer::CopyToDrawProfile profile =
            renderer.profileCopyToDraw(world, view);
    assertWallCacheProfileInvariants(profile,
            "Native renderer preview wall cache");
    assertTrue(
            profile.contactChordResolveRequests == 4,
            "Native renderer preview resolves regular and extra-bucket wall views");
    assertTrue(profile.contactChordDirectSourceViews == 4,
            "Native renderer preview directly resolves zero-wall views");
    assertTrue(profile.contactChordCacheMisses == 0 && profile.uniqueNormalizedCells == 0 && profile.contactChordCacheHits == 0,
            "Native renderer preview does not cache zero-wall views");
    assertTrue(profile.normalizedContactChordCount == 0,
            "Native renderer preview has no normalized wall payload");
    assertTrue(profile.linkOnlyCacheMisses == 0,
            "Native renderer preview has no link-only cache misses");
    assertTrue(profile.normalizerCalls == 0 && profile.cellPassNormalizerCalls == 0 && profile.linkPassNormalizerCalls == 0,
            "Native renderer preview skips unnecessary normalization");
#endif
}

void testNativeSimulationWorldRendererFoodDeadAndLinkBuffers() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    int left = addAliveCell(cellData, CellType::PHAGOCYTE);
    int right = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[left] = 0.0;
    cellData.mPositionY[left] = 0.0;
    cellData.mRadius[left] = 0.02;
    cellData.mPositionX[right] = 0.04;
    cellData.mPositionY[right] = 0.0;
    cellData.mRadius[right] = 0.02;
    addRenderableTestEdge(cellData, left, right, 0.0f);
    cellData.mTopology.setEdgeRenderOffsets(0, -0.002f, 0.0f, 0.003f, 0.0f);

    world.mFoodData.addFood(0.01, 0.0, Environment::MAX_FOOD_SIZE, 0.0f,
            world.mEnvironment);
    world.mFoodData.addFood(0.5, 0.0, Environment::MAX_FOOD_SIZE, 1.0f,
            world.mEnvironment);

    world.mDeadSplashData.replaceActiveDeadSplashCount(2);
    world.mDeadSplashData.mPositionX[0] = 0.02f;
    world.mDeadSplashData.mPositionY[0] = 0.0f;
    world.mDeadSplashData.mRadius[0] =
            static_cast<float>(CellData::MAX_CELL_RADIUS);
    world.mDeadSplashData.mAge[0] = 0.25f;
    world.mDeadSplashData.mColor[0] = {0.1f, 0.2f, 0.3f, 0.4f};
    world.mDeadSplashData.mPositionX[1] = 0.5f;
    world.mDeadSplashData.mPositionY[1] = 0.0f;
    world.mDeadSplashData.mRadius[1] =
            static_cast<float>(CellData::MAX_CELL_RADIUS);
    world.mDeadSplashData.mAge[1] = 0.25f;
    world.mDeadSplashData.mColor[1] = {0.9f, 0.8f, 0.7f, 0.6f};

    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);
    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    renderer.copyToDraw(world, view);

    assertTrue(renderer.foodInstanceCount() == 1,
            "Native renderer food culls out-of-view");
    assertNear(0.01, renderer.foodInstanceAttributes()[0], 1e-7,
            "Native renderer food x");
    assertNear(FoodData::RADIUS, renderer.foodInstanceAttributes()[2], 1e-7,
            "Native renderer food scaled radius");
    assertNear(0.6, renderer.foodVertexAttributes()[0], 1e-7,
            "Native renderer food color r");
    assertNear(0.4, renderer.foodVertexAttributes()[1], 1e-7,
            "Native renderer food color g");
    assertNear(0.2, renderer.foodVertexAttributes()[2], 1e-7,
            "Native renderer food color b");
    assertNear(0.4, renderer.foodVertexAttributes()[3], 1e-7,
            "Native renderer food alpha");

    assertTrue(renderer.deadCellInstanceCount() == 1,
            "Native renderer dead splash culls out-of-view");
    assertNear(0.02, renderer.deadCellInstanceAttributes()[0], 1e-7,
            "Native renderer dead x");
    assertNear(DeadSplashData::RADIUS, renderer.deadCellInstanceAttributes()[2],
            1e-7, "Native renderer dead scaled radius");
    assertNear(0.5, renderer.deadCellInstanceAttributes()[3], 1e-7,
            "Native renderer dead sqrt age");
    assertNear(0.1, renderer.deadCellVertexAttributes()[0], 1e-7,
            "Native renderer dead color r");
    assertNear(0.2, renderer.deadCellVertexAttributes()[1], 1e-7,
            "Native renderer dead color g");
    assertNear(0.3, renderer.deadCellVertexAttributes()[2], 1e-7,
            "Native renderer dead color b");
    assertNear(CellData::MAX_CELL_RADIUS, renderer.deadCellVertexAttributes()[3],
            1e-7, "Native renderer dead radius attribute");

    assertTrue(renderer.drawLinksCount() == 6,
            "Native renderer link emits two triangles");
    assertNear(0.043, renderer.linkTriangleCoords()[0], 1e-7,
            "Native renderer link start x");
    assertNear(0.0, renderer.linkTriangleCoords()[1], 1e-7,
            "Native renderer link start y");
    const std::array<float, 4> expectedCrossSection =
            expectedContactLinkCrossSection(NativeSimulationWorldRendererTestAccess::capturedContactChords(renderer), cellData, right, left);
    assertNear(expectedCrossSection[0], renderer.linkTriangleCoords()[4], 1e-7,
            "Native renderer link uses its normalized owner contact x1");
    assertNear(expectedCrossSection[1], renderer.linkTriangleCoords()[5], 1e-7,
            "Native renderer link uses its normalized owner contact y1");
    assertNear(-1.0, renderer.linkTriangleCoords()[6], 1e-7,
            "Native renderer link left edge marker");
    assertNear(expectedCrossSection[2], renderer.linkTriangleCoords()[8], 1e-7,
            "Native renderer link uses its normalized owner contact x2");
    assertNear(expectedCrossSection[3], renderer.linkTriangleCoords()[9], 1e-7,
            "Native renderer link uses its normalized owner contact y2");
    assertNear(1.0, renderer.linkTriangleCoords()[10], 1e-7,
            "Native renderer link right edge marker");
}

void testNativeSimulationWorldRendererDenseTopologyGrowsLinkBuffersOnce() {
    constexpr int cellCount = CellData::MAX_NEIGHBORS + 1;
    Environment environment =
            Environment::constructDefaultEnvironment(cellCount, 0, 2.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        const int cell = addAliveCell(cellData, CellType::GLUEOCYTE);
        assertTrue(cell == cellIndex,
                "Dense renderer topology allocates ordered Cells");
        const double angle = 2.0 * Environment::PI * cellIndex / cellCount;
        cellData.mPositionX[cell] = 0.15 * std::cos(angle);
        cellData.mPositionY[cell] = 0.15 * std::sin(angle);
        cellData.mRadius[cell] = 0.02;
        cellData.refreshRadiusSq(cell);
    }

    for (int first = 0; first < cellCount; first++) {
        for (int second = first + 1; second < cellCount; second++) {
            // This fixture tests topology capacity, not synthetic contact occlusion.
            addPairLink(cellData, first, second, 0.0, Environment::PI, 0.2f, 0.01f);
        }
    }
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    NativeSimulationWorldRenderer renderer(cellCount);
    const std::size_t initialTriangleCapacity =
            NativeSimulationWorldRendererTestAccess::linkTriangleCapacity(renderer);
    const std::size_t initialTextureCapacity =
            NativeSimulationWorldRendererTestAccess::linkTextureCapacity(renderer);
    const NativeSimulationWorldRenderer::ViewBounds view{-0.5f, 0.5f, -0.5f, 0.5f};

    renderer.copyToDraw(world, view);
    const int edgeCount = cellCount * (cellCount - 1) / 2;
    assertTrue(renderer.drawLinksCount() == edgeCount * 6,
            "Dense renderer topology emits every legal edge");
    assertTrue(NativeSimulationWorldRendererTestAccess::linkTriangleSize(renderer) >= static_cast<std::size_t>(edgeCount * 6 * 4),
            "Dense renderer topology grows link triangle storage safely");
    assertTrue(NativeSimulationWorldRendererTestAccess::linkTextureSize(renderer) >= static_cast<std::size_t>(edgeCount * 6),
            "Dense renderer topology grows link texture storage safely");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::linkTriangleCapacity(renderer) > initialTriangleCapacity && NativeSimulationWorldRendererTestAccess::linkTextureCapacity(renderer) > initialTextureCapacity,
            "Dense renderer topology crosses the legacy average-neighbor allocation");

    const std::size_t warmTriangleCapacity =
            NativeSimulationWorldRendererTestAccess::linkTriangleCapacity(renderer);
    const std::size_t warmTextureCapacity =
            NativeSimulationWorldRendererTestAccess::linkTextureCapacity(renderer);
    renderer.copyToDraw(world, view);
    assertTrue(NativeSimulationWorldRendererTestAccess::linkTriangleCapacity(
                       renderer)
                            == warmTriangleCapacity
                    && NativeSimulationWorldRendererTestAccess::linkTextureCapacity(
                               renderer)
                            == warmTextureCapacity,
            "Dense renderer topology reuses its link high-water capacity");

    renderer.reset(8);
    assertTrue(NativeSimulationWorldRendererTestAccess::resetStorageIsCompact(renderer),
            "Smaller renderer reset releases every prior CPU high-water");
    const std::size_t compactTriangleCapacity =
            NativeSimulationWorldRendererTestAccess::linkTriangleCapacity(renderer);
    assertThrows(
            [&] {
                renderer.reset(0);
            },
            "Renderer reset rejects an unsupported capacity before changing storage");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::linkTriangleCapacity(renderer) == compactTriangleCapacity && NativeSimulationWorldRendererTestAccess::resetStorageIsCompact(renderer),
            "Rejected renderer reset leaves existing storage intact");

    assertThrows(
            [] {
                NativeSimulationWorldRenderer invalidRenderer(0);
            },
            "Native renderer rejects unsupported capacity before allocation");
}

void testNativeSimulationWorldRendererNeighborCellGeometry() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    int neighbor = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.01;
    cellData.mPositionY[cell] = -0.02;
    cellData.mRadius[cell] = 0.02;
    cellData.mPositionX[neighbor] = 0.04;
    cellData.mPositionY[neighbor] = -0.02;
    cellData.mRadius[neighbor] = 0.02;
    assertTrue(cellData.addKnownCellCollisionContactFast(
                       cell, neighbor, -0.03, 0.0, 0.0009),
            "Native renderer neighbor fixture stores source contact");
    assertTrue(cellData.addKnownCellCollisionContactFast(
                       neighbor, cell, 0.03, 0.0, 0.0009),
            "Native renderer neighbor fixture stores reciprocal contact");

    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    renderer.copyToDraw(world, view);

    assertTrue(renderer.cellDrawVertexCount() >= 6,
            "Native renderer neighbor cell emits first-commit fan vertices");
    assertTrue(renderer.cellDrawIndexCount() >= 12,
            "Native renderer neighbor cell emits first-commit fan indices");
    assertNear(0.0, renderer.cellTriangleCoords()[0], 1e-7,
            "Native renderer neighbor center local x");
    assertNear(0.0, renderer.cellTriangleCoords()[1], 1e-7,
            "Native renderer neighbor center local y");
    assertNear(0.01, renderer.cellCenterCoords()[0], 1e-7,
            "Native renderer neighbor center world x");
    assertNear(-0.02, renderer.cellCenterCoords()[1], 1e-7,
            "Native renderer neighbor center world y");
    bool hasContactWallDistance = false;
    for (int vertex = 1; vertex < renderer.cellDrawVertexCount(); vertex++) {
        float wallDistance = renderer.cellTextureCoords()[4 * vertex + 1];
        hasContactWallDistance =
                hasContactWallDistance || std::fabs(wallDistance - 0.02f) < 1e-7f;
    }
    assertTrue(hasContactWallDistance, "Native renderer neighbor mesh marks "
                                       "first-commit wall and edge vertices");
    assertTrue(renderer.cellIndices()[0] == 0 && renderer.cellIndices()[1] == 1 && renderer.cellIndices()[2] == 2,
            "Native renderer neighbor first fan triangle");
    for (int index = 0; index < renderer.cellDrawIndexCount(); index++) {
        assertTrue(renderer.cellIndices()[index] < renderer.cellDrawVertexCount(),
                "Native renderer neighbor index stays inside vertex range");
    }
}

void testNativeSimulationWorldRendererSkipsBrokenNeighborLikeFirstCommitFan() {
    cellularity::render::RenderSourceFrame source;
    source.valid = true;
    source.sourceCellCount = 1;
    source.visibleCellCount = 1;
    cellularity::render::RenderSourceCell cell;
    cell.runtimeId = 1;
    cell.sourceCellIndex = 0;
    cell.cellType = CellType::PHAGOCYTE;
    cell.radius = 0.02f;
    cell.hasContactChordPayload = true;
    cell.contactChordCount = 1;
    source.cells.push_back(cell);
    source
            .orderedCellRecordIndices[static_cast<std::size_t>(
                    CellTypes::ordinal(CellType::PHAGOCYTE))]
            .push_back(0);
    RenderContactChord contactChord;
    contactChord.x1 = 0.02f;
    contactChord.y1 = 0.0f;
    contactChord.x2 = 0.0f;
    contactChord.y2 = 0.02f;
    contactChord.isDiscarded = true;
    contactChord.joinsNextChord = false;
    source.activeContactChords.push_back(contactChord);
    source.activeContactChordNeighborRuntimeIds.push_back(
            kBoundaryCellRuntimeId);

    cellularity::render::PreparedRenderFrame frame;
    cellularity::render::RenderFrameBuilder builder(4, frame);
    builder.build(source, frame);

    assertTrue(frame.cellDrawVertexCount == 1,
            "Native renderer first-commit fan skips broken wall vertices");
    assertTrue(frame.cellDrawIndexCount == 3,
            "Native renderer first-commit fan keeps closing triangle for "
            "broken-only wall list");
}

void testNativeSimulationWorldRendererFlagellocyteOutlineBuffers() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    int cell = addAliveCell(cellData, CellType::FLAGELLOCYTE);
    cellData.mPositionX[cell] = 0.01;
    cellData.mPositionY[cell] = -0.03;
    cellData.mRadius[cell] = 0.02;
    cellData.mAngle[cell] = 0.0;
    cellData.mSwimAnimationTime[cell] = 0.7f;
    cellData.mColor[cell] = {0.2f, 0.3f, 0.4f, 1.0f};
    cellData.mToxin[cell] = 0.5f;
    cellData.mToxication[cell] = 0.25f;

    world.updateGrids();

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    renderer.copyToDraw(world, view);

    int dataIndex =
            CellTypes::length + CellTypes::ordinal(CellType::FLAGELLOCYTE);
    int start = renderer.cellVertexCountBefore(dataIndex);
    CellTypeInfo info = CellTypes::info(CellType::FLAGELLOCYTE);
    float x = static_cast<float>(CellData::MAX_CELL_RADIUS / info.outlineRadius);
    float y = static_cast<float>(std::sqrt(1.0f - x * x));

    assertTrue(
            renderer.cellVertexCount(CellTypes::ordinal(CellType::FLAGELLOCYTE)) == 4,
            "Native renderer flagellocyte base vertex count");
    assertTrue(renderer.cellVertexCount(dataIndex) == 3,
            "Native renderer flagellocyte outline vertex count");
    assertTrue(
            renderer.cellDrawVertexCount() == 7,
            "Native renderer flagellocyte total vertex count includes outline");
    assertTrue(
            renderer.cellCenterFloatCount() == 28,
            "Native renderer flagellocyte center float count uses outline stride");
    assertTrue(start == 4,
            "Native renderer flagellocyte outline starts after base vertices");
    assertNear(-0.02 * x, renderer.cellTriangleCoords()[2 * start], 1e-7,
            "Native renderer flagellocyte outline first local x");
    assertNear(0.02 * y, renderer.cellTriangleCoords()[2 * start + 1], 1e-7,
            "Native renderer flagellocyte outline first local y");
    assertNear(-info.outlineRadius * 0.02 / CellData::MAX_CELL_RADIUS,
            renderer.cellTriangleCoords()[2 * (start + 2)], 1e-7,
            "Native renderer flagellocyte outline tip x");
    assertNear(x, renderer.cellTextureCoords()[4 * start], 1e-7,
            "Native renderer flagellocyte outline texture x");
    assertNear(y, renderer.cellTextureCoords()[4 * start + 1], 1e-7,
            "Native renderer flagellocyte outline texture y");
    assertNear(0.7, renderer.cellTextureCoords()[4 * start + 2], 1e-7,
            "Native renderer flagellocyte outline animation");
    assertNear(0.25, renderer.cellColors()[4 * start], 1e-7,
            "Native renderer flagellocyte outline color toxin red");
    assertNear(0.55, renderer.cellColors()[4 * start + 1], 1e-7,
            "Native renderer flagellocyte outline color toxication green");
    assertNear(0.01, renderer.cellCenterCoords()[4 * start], 1e-7,
            "Native renderer flagellocyte outline center x");
    assertNear(-0.03, renderer.cellCenterCoords()[4 * start + 1], 1e-7,
            "Native renderer flagellocyte outline center y");
}

void testNativeSimulationWorldRendererGenericOutlineBuffers() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    int cell = addAliveCell(cellData, CellType::GLUEOCYTE);
    cellData.mPositionX[cell] = -0.01;
    cellData.mPositionY[cell] = 0.03;
    cellData.mRadius[cell] = 0.02;
    cellData.mAngle[cell] = 0.0;
    cellData.hairX[cell] = 0.4;
    cellData.hairY[cell] = -0.6;
    cellData.mHairRotation[cell] = 1.25;

    world.updateGrids();

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    renderer.copyToDraw(world, view);

    int dataIndex = CellTypes::length + CellTypes::ordinal(CellType::GLUEOCYTE);
    int start = renderer.cellVertexCountBefore(dataIndex);
    float outlineRadius = CellTypes::info(CellType::GLUEOCYTE).outlineRadius;
    float extent = 0.02f + outlineRadius;

    assertTrue(
            renderer.cellVertexCount(CellTypes::ordinal(CellType::GLUEOCYTE)) == 4,
            "Native renderer glueocyte base vertex count");
    assertTrue(renderer.cellVertexCount(dataIndex) == 6,
            "Native renderer glueocyte outline vertex count");
    assertTrue(renderer.cellDrawVertexCount() == 10,
            "Native renderer glueocyte total vertex count includes outline");
    assertTrue(start == 4,
            "Native renderer glueocyte outline starts after base vertices");
    assertNear(-extent, renderer.cellTriangleCoords()[2 * start], 1e-7,
            "Native renderer glueocyte outline first local x");
    assertNear(extent, renderer.cellTriangleCoords()[2 * start + 1], 1e-7,
            "Native renderer glueocyte outline first local y");
    assertNear(1.0, renderer.cellTextureCoords()[4 * (start + 1)], 1e-7,
            "Native renderer glueocyte outline texture second x");
    assertNear(-1.0, renderer.cellTextureCoords()[4 * (start + 1) + 1], 1e-7,
            "Native renderer glueocyte outline texture second y");
    assertNear(0.4, renderer.cellTextureCoords()[4 * start + 2], 1e-7,
            "Native renderer glueocyte outline hair x aux");
    assertNear(-0.6, renderer.cellTextureCoords()[4 * start + 3], 1e-7,
            "Native renderer glueocyte outline hair y aux");
    assertNear(-0.01, renderer.cellCenterCoords()[4 * start], 1e-7,
            "Native renderer glueocyte outline center x");
    assertNear(0.03, renderer.cellCenterCoords()[4 * start + 1], 1e-7,
            "Native renderer glueocyte outline center y");
    assertNear(1.25, renderer.cellCenterCoords()[4 * start + 2], 1e-7,
            "Native renderer glueocyte outline hair rotation");
    assertNear(0.0, renderer.cellCenterCoords()[4 * start + 3], 1e-7,
            "Native renderer glueocyte outline angle");
}

void testNativeCellShaderLoadsAssetsAndDrawsElementsVbo() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    CellShader shader(assets, gl, "phagocyte");

    assertTrue(assets.textPaths.size() == 2,
            "Native CellShader loads vertex and fragment shader");
    assertTrue(assets.textPaths[0] == "shaders/vertex/phagocyte.glsl",
            "Native CellShader vertex path");
    assertTrue(assets.textPaths[1] == "shaders/fragment/phagocyte.glsl",
            "Native CellShader fragment path");
    assertTrue(gl.shaderSources.size() == 2,
            "Native CellShader submits shader source twice");

    std::array<float, 16> matrix{};
    shader.begin(matrix, 77);
    shader.drawElementsVbo(10, 20, 11, 24, 12, 28, 13, 32, 14, 36, 42, 2);
    shader.end();

    assertTrue(gl.usedProgram == 200, "Native CellShader uses linked program");
    assertTrue(gl.uniformMatrixLocation == 11,
            "Native CellShader sets MVP uniform");
    assertTrue(gl.activeTextureValue == gl.texture0(),
            "Native CellShader activates texture0");
    assertTrue(gl.boundTexture == 77, "Native CellShader binds texture");
    assertTrue(gl.uniformTextureLocation == 12,
            "Native CellShader sets texture uniform");
    assertTrue(gl.enabledAttribs.size() == 4,
            "Native CellShader enables four attribs");
    assertTrue(gl.pointerCalls.size() == 4,
            "Native CellShader binds four VBO attrib pointers");
    assertTrue(gl.pointerCalls[0].buffer == 10 && gl.pointerCalls[0].index == 1 && gl.pointerCalls[0].size == 2 && gl.pointerCalls[0].stride == 8 && gl.pointerCalls[0].offsetBytes == 20,
            "Native CellShader position pointer");
    assertTrue(gl.pointerCalls[1].buffer == 12 && gl.pointerCalls[1].index == 3 && gl.pointerCalls[1].size == 2 && gl.pointerCalls[1].stride == 8 && gl.pointerCalls[1].offsetBytes == 28,
            "Native CellShader center pointer");
    assertTrue(gl.pointerCalls[2].buffer == 11 && gl.pointerCalls[2].index == 2 && gl.pointerCalls[2].size == 4 && gl.pointerCalls[2].stride == 16 && gl.pointerCalls[2].offsetBytes == 24,
            "Native CellShader texture pointer");
    assertTrue(gl.pointerCalls[3].buffer == 13 && gl.pointerCalls[3].index == 4 && gl.pointerCalls[3].size == 4 && gl.pointerCalls[3].stride == 16 && gl.pointerCalls[3].offsetBytes == 32,
            "Native CellShader color pointer");
    assertTrue(gl.currentElementArrayBuffer == 14,
            "Native CellShader binds index buffer");
    assertTrue(gl.drawElementsCount == 42 && gl.drawElementsOffsetBytes == 36,
            "Native CellShader indexed draw");
    assertTrue(gl.disabledAttribs.size() == 4,
            "Native CellShader disables attribs");
}

void testNativeFoodShaderDrawsInstancedVbo() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    FoodShader shader(assets, gl);

    std::array<float, 16> matrix{};
    shader.begin(matrix);
    shader.drawInstancedVbo(21, 22, 23, 9);
    shader.end();

    assertTrue(assets.textPaths[0] == "shaders/vertex/food.glsl",
            "Native FoodShader vertex path");
    assertTrue(assets.textPaths[1] == "shaders/fragment/food.glsl",
            "Native FoodShader fragment path");
    assertTrue(gl.pointerCalls.size() == 3,
            "Native FoodShader binds three VBO attrib pointers");
    assertTrue(gl.pointerCalls[0].buffer == 21 && gl.pointerCalls[0].index == 1 && gl.pointerCalls[0].size == 2 && gl.pointerCalls[0].stride == 8,
            "Native FoodShader position pointer");
    assertTrue(gl.pointerCalls[1].buffer == 22 && gl.pointerCalls[1].index == 6 && gl.pointerCalls[1].size == 4 && gl.pointerCalls[1].stride == 16,
            "Native FoodShader instance pointer");
    assertTrue(gl.pointerCalls[2].buffer == 23 && gl.pointerCalls[2].index == 7 && gl.pointerCalls[2].size == 4 && gl.pointerCalls[2].stride == 16,
            "Native FoodShader color pointer");
    assertTrue(gl.drawArraysInstancedCount == 6 && gl.drawArraysInstancedInstances == 9,
            "Native FoodShader instanced draw");
    assertTrue(gl.disabledAttribs.size() == 3,
            "Native FoodShader disables attribs");
}

void testNativeCellRendererResourcesLoadsShadersAndTextures() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;

    NativeCellRendererResources resources(assets, gl, textureLoader, false);
    std::array<CellShader*, CellTypes::length> basicShaders =
            resources.basicCellShaders();
    std::array<CellShader*, CellTypes::length> outlineShaders =
            resources.outlineCellShaders();

    assertTrue(basicShaders[CellTypes::ordinal(CellType::PHAGOCYTE)] != nullptr,
            "Native renderer resources phagocyte basic shader");
    assertTrue(basicShaders[CellTypes::ordinal(CellType::FLAGELLOCYTE)] != nullptr,
            "Native renderer resources flagellocyte basic shader");
    assertTrue(outlineShaders[CellTypes::ordinal(CellType::FLAGELLOCYTE)] != nullptr,
            "Native renderer resources flagellocyte outline shader");
    assertTrue(outlineShaders[CellTypes::ordinal(CellType::PHAGOCYTE)] == nullptr,
            "Native renderer resources phagocyte has no outline shader");
    assertTrue(resources.arrowShader() == nullptr,
            "Native renderer resources no arrow outside preview");
    assertTrue(resources.dotTextureHandle() == 300,
            "Native renderer resources dot texture handle");
    assertTrue(resources.besselTextureHandle() == 301,
            "Native renderer resources bessel texture handle");
    assertTrue(textureLoader.requests.size() == 2,
            "Native renderer resources loads two textures");
    assertTrue(textureLoader.requests[0].name == "small_dots" && !textureLoader.requests[0].clampToEdge,
            "Native renderer resources dot texture request");
    assertTrue(textureLoader.requests[1].name == "gauss_bessel_integral" && textureLoader.requests[1].clampToEdge,
            "Native renderer resources bessel texture request");

    bool sawPhagocyteVertex = false;
    bool sawFlagellocyteOutlineVertex = false;
    bool sawFoodVertex = false;
    bool sawDeadVertex = false;
    bool sawLinkVertex = false;
    bool sawBackgroundVertex = false;
    bool sawDensityFieldVertex = false;
    bool sawGenomeGridVertex = false;
    bool sawSolidColorVertex = false;
    bool sawPixelTextVertex = false;
    for (const std::string& path : assets.textPaths) {
        sawPhagocyteVertex =
                sawPhagocyteVertex || path == "shaders/vertex/phagocyte.glsl";
        sawFlagellocyteOutlineVertex =
                sawFlagellocyteOutlineVertex || path == "shaders/vertex/flagellocyte_outline.glsl";
        sawFoodVertex = sawFoodVertex || path == "shaders/vertex/food.glsl";
        sawDeadVertex = sawDeadVertex || path == "shaders/vertex/dead.glsl";
        sawLinkVertex = sawLinkVertex || path == "shaders/vertex/link.glsl";
        sawBackgroundVertex =
                sawBackgroundVertex || path == "shaders/vertex/background.glsl";
        sawDensityFieldVertex =
                sawDensityFieldVertex || path == "shaders/vertex/density_field.glsl";
        sawGenomeGridVertex =
                sawGenomeGridVertex || path == "shaders/vertex/genome_grid.glsl";
        sawSolidColorVertex =
                sawSolidColorVertex || path == "shaders/vertex/solid_color.glsl";
        sawPixelTextVertex =
                sawPixelTextVertex || path == "shaders/vertex/pixel_text.glsl";
    }
    assertTrue(sawPhagocyteVertex,
            "Native renderer resources loads phagocyte shader source");
    assertTrue(
            sawFlagellocyteOutlineVertex,
            "Native renderer resources loads flagellocyte outline shader source");
    assertTrue(sawFoodVertex && sawDeadVertex && sawLinkVertex && sawBackgroundVertex && sawDensityFieldVertex && sawGenomeGridVertex,
            "Native renderer resources loads shared shader sources");
    assertTrue(sawSolidColorVertex,
            "Native renderer resources loads solid-color shader source");
#ifdef CELLULARITY_PERFORMANCE_HUD
    (void)resources.pixelTextShader();
    assertTrue(sawPixelTextVertex,
            "Debug renderer resources load the pixel-text shader");
#else
    assertTrue(!sawPixelTextVertex,
            "Release renderer resources skip the debug pixel-text shader");
#endif
}

void testNativeCellRendererResourcesLoadsPreviewArrowShader() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;

    NativeCellRendererResources resources(assets, gl, textureLoader, true);

    assertTrue(resources.isPreviewMode(),
            "Native renderer resources preview flag");
    assertTrue(resources.arrowShader() != nullptr,
            "Native renderer resources preview arrow shader");
    (void)resources.pixelTextShader();
    bool sawArrowVertex = false;
    bool sawArrowFragment = false;
    bool sawPixelTextVertex = false;
    bool sawPixelTextFragment = false;
    for (const std::string& path : assets.textPaths) {
        sawArrowVertex = sawArrowVertex || path == "shaders/vertex/arrow.glsl";
        sawArrowFragment =
                sawArrowFragment || path == "shaders/fragment/arrow.glsl";
        sawPixelTextVertex =
                sawPixelTextVertex || path == "shaders/vertex/pixel_text.glsl";
        sawPixelTextFragment =
                sawPixelTextFragment || path == "shaders/fragment/pixel_text.glsl";
    }
    assertTrue(sawArrowVertex && sawArrowFragment,
            "Native renderer resources loads arrow shader sources in preview");
    assertTrue(sawPixelTextVertex && sawPixelTextFragment,
            "Native renderer resources loads pixel text for Preview labels");
}

void testPixelTextRendererOwnsReusableCachedGeometry() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    PixelTextShader shader(assets, gl);
    cellularity::render::PixelTextRenderer renderer;
    constexpr std::array<float, 16> identity = {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };

    renderer.beginGeometry(1280, 720, 2);
    renderer.configureLayer(0, {0.0f, 0.0f, 0.0f, 0.8f}, 1);
    renderer.configureLayer(1, {1.0f, 1.0f, 1.0f, 1.0f}, 2);
    renderer.appendRectangle(0, 4.0f, 4.0f, 80.0f, 24.0f);
    renderer.appendText(1, "10", 8.0f, 8.0f, 2.0f);
    renderer.commitGeometry();
    renderer.draw(gl, shader, identity);

    assertTrue(gl.bufferDataCalls.size() == 1 && gl.bufferDataCalls[0].hasData && gl.bufferDataCalls[0].sizeBytes == 3 * 9 * 4,
            "Pixel text renderer uploads three compact instances in one VBO");
    assertTrue(gl.drawEvents.size() == 1 && gl.drawEvents[0].type == "arrays_instanced" && gl.drawEvents[0].count == 6 && gl.drawArraysInstancedInstances == 3 && gl.drawArraysInstancedVertexArray != 0 && gl.currentVertexArray == 0,
            "Pixel text renderer draws every layer in one instanced call");
    assertTrue(gl.pointerCalls.size() == 3 && gl.pointerCalls[0].size == 4 && gl.pointerCalls[0].stride == 9 * 4 && gl.pointerCalls[0].offsetBytes == 0 && gl.pointerCalls[1].size == 1 && gl.pointerCalls[1].offsetBytes == 4 * 4 && gl.pointerCalls[2].size == 4 && gl.pointerCalls[2].offsetBytes == 5 * 4 && gl.pointerCalls[0].vertexArray == gl.drawArraysInstancedVertexArray && gl.pointerCalls[1].vertexArray == gl.drawArraysInstancedVertexArray && gl.pointerCalls[2].vertexArray == gl.drawArraysInstancedVertexArray,
            "Pixel text VAO owns rect, glyph, and color instance layout");
    assertTrue(gl.vertexAttribDivisorCalls.size() == 3 && gl.vertexAttribDivisorCalls[0].divisor == 1 && gl.vertexAttribDivisorCalls[1].divisor == 1 && gl.vertexAttribDivisorCalls[2].divisor == 1,
            "Pixel text VAO retains every per-instance divisor");
    assertTrue(gl.textureImageUploadCount == 1 && gl.textureImageWidth == 40 && gl.textureImageHeight == 42 && gl.textureImageInternalFormat == gl.r8() && gl.textureImageFormat == gl.red() && gl.textureImageBytesPerPixel == 1 && gl.textureImageBytes.size() == 40U * 42U,
            "Pixel text renderer creates one single-channel 5x7 glyph atlas");
    assertTrue(gl.textureParameterCalls.size() == 4 && gl.textureParameterCalls[0].value == gl.nearest() && gl.textureParameterCalls[1].value == gl.nearest() && gl.textureParameterCalls[2].value == gl.clampToEdge() && gl.textureParameterCalls[3].value == gl.clampToEdge(),
            "Pixel text atlas uses crisp sampling without edge bleed");
    const auto atlasCoverage = [&gl](int x, int y) {
        return gl.textureImageBytes[static_cast<std::size_t>(y) * 40U + static_cast<std::size_t>(x)];
    };
    assertTrue(atlasCoverage(12, 0) == 255 && atlasCoverage(10, 0) == 0,
            "Pixel text atlas preserves the exact top row of glyph 1");
    assertTrue(atlasCoverage(12, 36) == 255 && atlasCoverage(10, 38) == 255 && atlasCoverage(10, 36) == 0,
            "Pixel text atlas contains a real plus glyph");
    assertTrue(atlasCoverage(10, 35) == 0 && atlasCoverage(14, 41) == 0,
            "Solid rectangles no longer occupy or sample an atlas glyph");
    const int firstBuffer = gl.bufferDataCalls[0].buffer;
    const int firstTexture = gl.boundTexture;
    const int firstVertexArray = gl.drawArraysInstancedVertexArray;

    renderer.draw(gl, shader, identity);
    assertTrue(gl.bufferDataCalls.size() == 1,
            "Pixel text renderer reuses unchanged GPU geometry");
    assertTrue(gl.textureImageUploadCount == 1 && gl.drawEvents.size() == 2,
            "Cached pixel text geometry and atlas remain drawable every frame");
    assertTrue(gl.generatedVertexArrays.size() == 1 && gl.pointerCalls.size() == 3 && gl.vertexAttribDivisorCalls.size() == 3 && gl.drawArraysInstancedVertexArray == firstVertexArray,
            "Cached pixel text draw reuses its configured VAO without state setup");

    renderer.abandonGlResources();
    renderer.draw(gl, shader, identity);
    assertTrue(gl.bufferDataCalls.size() == 2 && gl.bufferDataCalls[1].buffer != firstBuffer,
            "Pixel text renderer recreates its VBO after context loss");
    assertTrue(gl.textureImageUploadCount == 2 && gl.boundTexture != firstTexture,
            "Pixel text renderer recreates its atlas after context loss");
    assertTrue(gl.generatedVertexArrays.size() == 2 && gl.drawArraysInstancedVertexArray != firstVertexArray && gl.pointerCalls.size() == 6,
            "Pixel text renderer recreates and configures its VAO after context loss");
    const int recreatedBuffer = gl.bufferDataCalls[1].buffer;
    const int recreatedTexture = gl.boundTexture;
    const int recreatedVertexArray = gl.drawArraysInstancedVertexArray;
    renderer.releaseGlResources(gl);
    assertTrue(!gl.deletedBuffers.empty() && gl.deletedBuffers.back() == recreatedBuffer,
            "Pixel text renderer releases its live VBO");
    assertTrue(!gl.deletedTextures.empty() && gl.deletedTextures.back() == recreatedTexture,
            "Pixel text renderer releases its live atlas");
    assertTrue(!gl.deletedVertexArrays.empty() && gl.deletedVertexArrays.back() == recreatedVertexArray,
            "Pixel text renderer releases its live VAO");
}

#ifdef CELLULARITY_PERFORMANCE_HUD
void testMicroscopeDebugHudCachesPixelGeometryInOneVbo() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    PixelTextShader shader(assets, gl);
    cellularity::microscope::MicroscopeDebugHud hud;
    cellularity::microscope::MicroscopeDebugHudTelemetry telemetry;
    telemetry.rateSampleSequence = 1;
    telemetry.actualRateValid = true;
    telemetry.actualCompletedUps = 2'345.0;
    telemetry.stageTimings.sequence = 1;
    telemetry.stageTimings.totalNanos = 220'000.0;
    telemetry.stageTimings.stageNanos.fill(10'000.0);

    hud.draw(gl, shader, 1280, 720, telemetry);
    assertTrue(gl.bufferDataCalls.size() == 1,
            "Debug HUD uploads all pixel geometry through one VBO");
    assertTrue(gl.bufferDataCalls[0].hasData && gl.bufferDataCalls[0].sizeBytes == 507 * 9 * 4 && gl.drawArraysInstancedInstances == 507,
            "Debug HUD VBO owns one compact instance per visible primitive");
    assertTrue(gl.textureImageUploadCount == 1 && gl.drawEvents.size() == 1 && gl.drawEvents[0].type == "arrays_instanced",
            "Debug HUD draws every color layer through one atlas call");
    const int firstBuffer = gl.bufferDataCalls[0].buffer;
    const int firstVertexArray = gl.drawArraysInstancedVertexArray;

    hud.draw(gl, shader, 1280, 720, telemetry);
    assertTrue(gl.bufferDataCalls.size() == 1,
            "Unchanged debug HUD values reuse cached GPU geometry");
    assertTrue(gl.textureImageUploadCount == 1 && gl.drawEvents.size() == 2,
            "Cached debug HUD geometry remains drawable every frame");
    assertTrue(gl.generatedVertexArrays.size() == 1 && gl.pointerCalls.size() == 3 && gl.drawArraysInstancedVertexArray == firstVertexArray,
            "Cached debug HUD draw reuses its configured VAO");

    hud.abandonGlResources();
    hud.draw(gl, shader, 1280, 720, telemetry);
    assertTrue(gl.bufferDataCalls.size() == 2 && gl.bufferDataCalls[1].buffer != firstBuffer,
            "Debug HUD recreates its VBO after context loss");
    assertTrue(gl.textureImageUploadCount == 2,
            "Debug HUD recreates its glyph atlas after context loss");
    assertTrue(gl.generatedVertexArrays.size() == 2 && gl.drawArraysInstancedVertexArray != firstVertexArray,
            "Debug HUD recreates its VAO after context loss");
    const int recreatedBuffer = gl.bufferDataCalls[1].buffer;
    const int recreatedVertexArray = gl.drawArraysInstancedVertexArray;
    hud.releaseGlResources(gl);
    assertTrue(!gl.deletedBuffers.empty() && gl.deletedBuffers.back() == recreatedBuffer,
            "Debug HUD releases its live VBO with the GL context");
    assertTrue(!gl.deletedVertexArrays.empty() && gl.deletedVertexArrays.back() == recreatedVertexArray,
            "Debug HUD releases its live VAO with the GL context");
}
#endif

void testNativeSimulationWorldRendererUploadDynamicBuffers() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;
    world.updateGrids();

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    renderer.copyToDraw(world, view);

    FakeGlApi gl;
    cellularity::render::SimulationWorldGpuRenderer gpuRenderer;
    gpuRenderer.uploadDynamicBuffers(gl, renderer.preparedFrame());

    assertTrue(gpuRenderer.hasGlBuffers(),
            "Separated GPU renderer creates GL buffers lazily");
    assertTrue(gl.bufferDataCalls.size() >= 12,
            "Native renderer upload writes static and dynamic buffers");
    assertTrue(gl.bufferDataCalls[0].buffer == 6 && gl.bufferDataCalls[0].sizeBytes == 12 * 4 && gl.bufferDataCalls[0].hasData,
            "Native renderer static food quad upload");
    assertTrue(gl.bufferDataCalls[1].buffer == 9 && gl.bufferDataCalls[1].sizeBytes == 12 * 4 && gl.bufferDataCalls[1].hasData,
            "Native renderer static dead quad upload");
    assertTrue(gl.bufferDataCalls[2].buffer == 1 && gl.bufferDataCalls[2].sizeBytes == renderer.cellDrawVertexCount() * 2 * 4 && !gl.bufferDataCalls[2].hasData,
            "Native renderer cell coords bufferData");
    assertTrue(gl.bufferDataCalls[3].buffer == 2 && gl.bufferDataCalls[3].sizeBytes == renderer.cellCenterFloatCount() * 4,
            "Native renderer cell centers bufferData");
    assertTrue(gl.bufferDataCalls[6].target == gl.elementArrayBuffer() && gl.bufferDataCalls[6].buffer == 5 && gl.bufferDataCalls[6].sizeBytes == renderer.cellDrawIndexCount() * 2,
            "Native renderer cell index bufferData");
    assertTrue(!gl.bufferSubDataCalls.empty(),
            "Native renderer upload fills non-empty buffers");
    assertTrue(gl.currentArrayBuffer == 0 && gl.currentElementArrayBuffer == 0,
            "Native renderer upload unbinds buffers");
}

void testSimulationWorldGpuRendererSkipsOnlyUnchangedUploads() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;
    world.updateGrids();

    cellularity::render::PreparedRenderFrame frame;
    cellularity::render::RenderSourceFrame source;
    cellularity::render::RenderSourceCapture sourceCapture(
            environment.maxCellCount, source);
    cellularity::render::RenderFrameBuilder builder(environment.maxCellCount,
            frame);
    const cellularity::render::RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    cellularity::render::RenderFrameCaptureMetadata captureMetadata;
    captureMetadata.worldEpoch = 17;
    captureMetadata.stage = cellularity::render::RenderCaptureStage::StableWorld;
    captureMetadata.viewport.visibleBounds = view;
    captureMetadata.viewport.coverageBounds = view;
    sourceCapture.capture(world, view, captureMetadata, source);
    builder.build(source, frame);

    cellularity::render::SimulationWorldGpuRenderer gpuRenderer;
    FakeGlApi gl;
    assertTrue(gpuRenderer.uploadDynamicBuffers(gl, frame),
            "GPU renderer uploads a prepared frame on first consumption");
    assertTrue(gpuRenderer.hasGlBuffers(),
            "GPU renderer owns the lazily created GL buffers");
    const std::size_t firstBufferDataCount = gl.bufferDataCalls.size();
    const std::size_t firstBufferSubDataCount = gl.bufferSubDataCalls.size();

    assertTrue(!gpuRenderer.uploadDynamicBuffers(gl, frame),
            "GPU renderer skips an unchanged frame identity");
    assertTrue(gl.bufferDataCalls.size() == firstBufferDataCount && gl.bufferSubDataCalls.size() == firstBufferSubDataCount,
            "Skipped upload emits no buffer write calls");

    frame.viewport.revision++;
    frame.viewport.mvpMatrix[0] = 2.0f;
    frame.layout.markerRevision++;
    frame.markers.push_back({0.0f, 0.0f, 0.01f, 1.0f});
    assertTrue(!gpuRenderer.uploadDynamicBuffers(gl, frame),
            "Presentation-only state does not invalidate geometry buffers");

    frame.layout.instanceRevision++;
    assertTrue(gpuRenderer.uploadDynamicBuffers(gl, frame),
            "Instance revision invalidates uploaded buffers");
    const std::size_t revisedBufferDataCount = gl.bufferDataCalls.size();
    assertTrue(!gpuRenderer.uploadDynamicBuffers(gl, frame) && gl.bufferDataCalls.size() == revisedBufferDataCount,
            "Revised frame is skipped after its successful upload");

    frame.identity.worldEpoch++;
    assertTrue(gpuRenderer.uploadDynamicBuffers(gl, frame),
            "World epoch prevents cross-world upload reuse");

    frame.identity.modeGeneration++;
    assertTrue(gpuRenderer.uploadDynamicBuffers(gl, frame),
            "Mode generation prevents cross-mode upload reuse");

    cellularity::render::PreparedRenderFrame collidingFrame;
    cellularity::render::RenderFrameBuilder collidingBuilder(
            environment.maxCellCount, collidingFrame);
    collidingBuilder.build(source, collidingFrame);
    collidingFrame.identity = frame.identity;
    collidingFrame.layout = frame.layout;
    assertTrue(gpuRenderer.uploadDynamicBuffers(gl, collidingFrame),
            "A different frame slot cannot alias an uploaded identity");
    const std::size_t beforeContextLossBufferDataCount =
            gl.bufferDataCalls.size();
    gpuRenderer.abandonGlBuffers();
    assertTrue(!gpuRenderer.hasGlBuffers(),
            "GPU renderer abandons stale context handles");
    assertTrue(gpuRenderer.uploadDynamicBuffers(gl, collidingFrame) && gl.bufferDataCalls.size() > beforeContextLossBufferDataCount,
            "Context recreation invalidates the remembered upload");

    gpuRenderer.deleteGlBuffers(gl);
    assertTrue(!gpuRenderer.hasGlBuffers() && gl.deletedBuffers.size() == 13,
            "GPU renderer deletes every owned buffer explicitly");
}

void testNativeSimulationWorldRendererConsumesAnExternalPreparedFrame() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;
    world.updateGrids();

    NativeSimulationWorldRenderer producer(environment.maxCellCount);
    const NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    producer.copyToDraw(world, view);
    const std::array<float, 16> matrix{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
    NativeSimulationWorldRenderer::PresentationState presentation;
    presentation.viewport = producer.preparedFrame().viewport;
    presentation.viewport.mvpMatrix = matrix;
    presentation.viewport.revision = 0;
    presentation.viewport.surfaceWidth = 100;
    presentation.viewport.surfaceHeight = 100;
    presentation.backgroundColor =
            producer.preparedFrame().environment.backgroundColor;
    const auto& frame = producer.publishPreparedFrame(presentation);

    cellularity::render::SimulationWorldGpuRenderer consumer;
    assertTrue(!consumer.hasGlBuffers(),
            "Prepared-frame consumer starts without GPU resources");
    FakeGlApi gl;
    consumer.uploadDynamicBuffers(gl, frame);

    assertTrue(gl.bufferDataCalls.size() >= 12,
            "Prepared-frame consumer uploads every dynamic buffer");
    assertTrue(gl.bufferDataCalls[2].sizeBytes == frame.cellDrawVertexCount * 2 * 4,
            "Prepared-frame consumer uploads the external geometry range");
    assertTrue(gl.bufferDataCalls[6].sizeBytes == frame.cellDrawIndexCount * 2,
            "Prepared-frame consumer uploads the external index range");
    assertTrue(producer.cellDrawVertexCount() == frame.cellDrawVertexCount,
            "GPU consumption does not mutate producer-owned CPU vectors");
}

void testSimulationWorldGpuRendererDrawCellsUsesPerTypeRelativeIndices() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    int phagocyte = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[phagocyte] = 0.0;
    cellData.mPositionY[phagocyte] = 0.0;
    cellData.mRadius[phagocyte] = 0.02;

    int neurocyte = addAliveCell(cellData, CellType::NEUROCYTE);
    cellData.mPositionX[neurocyte] = 0.04;
    cellData.mPositionY[neurocyte] = 0.0;
    cellData.mRadius[neurocyte] = 0.02;
    world.updateGrids();

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    renderer.copyToDraw(world, view);

    int phagocyteDataIndex = CellTypes::ordinal(CellType::PHAGOCYTE);
    int neurocyteDataIndex = CellTypes::ordinal(CellType::NEUROCYTE);
    assertTrue(renderer.cellIndexCountBefore(phagocyteDataIndex) == 0,
            "Native renderer phagocyte index range starts at zero");
    assertTrue(renderer.cellIndexCountBefore(neurocyteDataIndex) == 6,
            "Native renderer neurocyte index range starts after phagocyte");
    assertTrue(renderer.cellIndices()[6] == 0 && renderer.cellIndices()[11] == 3,
            "Native renderer second type indices are batch-relative");

    FakeRenderAssets assets;
    FakeGlApi gl;
    CellShader phagocyteShader(assets, gl, "phagocyte");
    CellShader neurocyteShader(assets, gl, "neurocyte");
    std::array<CellShader*, CellTypes::length> basicShaders{};
    basicShaders[phagocyteDataIndex] = &phagocyteShader;
    basicShaders[neurocyteDataIndex] = &neurocyteShader;
    cellularity::render::SimulationWorldGpuRenderer gpuRenderer;
    gpuRenderer.createGlBuffers(gl);
    gpuRenderer.drawCells(gl, basicShaders, nullptr, 7, false,
            renderer.preparedFrame());

    assertTrue(gl.drawElementsCalls.size() == 2,
            "Native renderer drawCells draws two type batches");
    assertTrue(gl.drawElementsCalls[0].count == 6 && gl.drawElementsCalls[0].offsetBytes == 0,
            "Native renderer first type draw range");
    assertTrue(gl.drawElementsCalls[1].count == 6 && gl.drawElementsCalls[1].offsetBytes == 12,
            "Native renderer second type draw range");
}

void testNativeSimulationWorldRendererCopyToDrawDoesNotMutateContactChords() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int connectedCell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;
    cellData.mPositionX[connectedCell] = 0.03;
    cellData.mPositionY[connectedCell] = 0.0;
    cellData.mRadius[connectedCell] = 0.02;
    addRenderableTestEdge(cellData, cell, connectedCell, 0.01f);
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    renderer.copyToDraw(world, view);
    const RenderContactChordBuffer& captured =
            NativeSimulationWorldRendererTestAccess::capturedContactChords(renderer);
    const std::vector<RenderContactChord> originalNeighbors =
            copyActiveContactChords(captured, cell);
    assertTrue(originalNeighbors.size() == 1,
            "Native renderer capture owns one raw source contact chord");
    const std::uint64_t firstGeneration =
            NativeSimulationWorldRendererTestAccess::wallGeneration(renderer);
    assertTrue(firstGeneration == 1,
            "Native renderer first wall extraction starts generation one");
    std::vector<float> firstCoords = renderer.cellTriangleCoords();
    assertTrue(renderer.drawLinksCount() == 6,
            "Native renderer extracts the Link from the owner contact");
    const std::array<float, 4> expectedCrossSection =
            expectedContactLinkCrossSection(captured, cellData, connectedCell, cell);
    assertNear(expectedCrossSection[0], renderer.linkTriangleCoords()[4], 1e-7,
            "Native renderer Link cross-section matches the owner contact x1");
    assertNear(expectedCrossSection[1], renderer.linkTriangleCoords()[5], 1e-7,
            "Native renderer Link cross-section matches the owner contact y1");
    assertNear(expectedCrossSection[2], renderer.linkTriangleCoords()[8], 1e-7,
            "Native renderer Link cross-section matches the owner contact x2");
    assertNear(expectedCrossSection[3], renderer.linkTriangleCoords()[9], 1e-7,
            "Native renderer Link cross-section matches the owner contact y2");
#ifdef CELLULARITY_RENDER_PROFILE
    const NativeSimulationWorldRenderer::CopyToDrawProfile profile =
            renderer.profileCopyToDraw(world, view);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == firstGeneration + 1,
            "Native renderer repeated profiled extraction starts a new wall "
            "generation");
    assertWallCacheProfileInvariants(profile, "Native renderer cell wall cache");
    assertTrue(profile.normalizerCalls == 0,
            "Native renderer bypasses normalization for one-wall sources");
    assertTrue(profile.cellPassNormalizerCalls == 0,
            "Native renderer performs no cell-pass normalization");
    assertTrue(profile.linkPassNormalizerCalls == 0,
            "Native renderer Link pass performs no wall normalization");
    assertTrue(profile.contactChordResolveRequests == 3,
            "Native renderer resolves two Cell views and the Link owner view");
    assertTrue(profile.contactChordDirectSourceViews == 3,
            "Native renderer returns one-wall Cell and Link requests directly");
    assertTrue(profile.contactChordCacheMisses == 0 && profile.uniqueNormalizedCells == 0,
            "Native renderer does not cache direct one-wall sources");
    assertTrue(profile.contactChordCacheHits == 0 && profile.linkOnlyCacheMisses == 0,
            "Native renderer direct one-wall Link lookup needs no cache copy");
    assertTrue(profile.linkPassContactChordNormalizationNanos == 0,
            "Native renderer direct Link lookup needs no normalization");
    assertTrue(profile.normalizedContactChordCount == 0,
            "Native renderer stores no normalized one-wall copies");
    assertTrue(profile.normalizerCalls == profile.contactChordCacheMisses,
            "Native renderer normalizer calls equal cache misses");
    assertTrue(profile.normalizerContactChordCountSum == 0,
            "Native renderer profile records no skipped one-wall work");
    assertTrue(
            profile.contactChordResolutionNanos == profile.cellPassContactChordResolutionNanos + profile.linkPassContactChordResolutionNanos,
            "Native renderer wall resolution timing has exact phase accounting");
    assertTrue(
            profile.contactChordNormalizationNanos == profile.cellPassContactChordNormalizationNanos + profile.linkPassContactChordNormalizationNanos,
            "Native renderer wall normalization timing has exact phase accounting");
#else
    renderer.copyToDraw(world, view);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == firstGeneration + 1,
            "Native renderer repeated ordinary extraction starts a new wall "
            "generation");
#endif

    const std::vector<RenderContactChord> recaptured = copyActiveContactChords(
            NativeSimulationWorldRendererTestAccess::capturedContactChords(renderer), cell);
    assertTrue(recaptured.size() == originalNeighbors.size(),
            "Native renderer keeps capture-owned source wall count stable");
    for (std::size_t wallIndex = 0; wallIndex < originalNeighbors.size();
            ++wallIndex) {
        assertWallExact(originalNeighbors[wallIndex], recaptured[wallIndex],
                "Native renderer keeps capture-owned raw wall immutable");
    }
    for (std::size_t coordinateIndex = 0; coordinateIndex < firstCoords.size();
            ++coordinateIndex) {
        assertNear(firstCoords[coordinateIndex],
                renderer.cellTriangleCoords()[coordinateIndex], 0.0,
                "Native renderer repeated copyToDraw is stable");
    }
}

void testNativeSimulationWorldRendererLinksResolveOffscreenOwnerWalls() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int firstEndpoint = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int secondEndpoint = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int leftNeighbor = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int rightNeighbor = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int source = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[source] = 0.5;
    cellData.mPositionY[source] = 0.0;
    cellData.mRadius[source] = 0.02;
    cellData.mPositionX[firstEndpoint] = 0.0;
    cellData.mPositionY[firstEndpoint] = 0.0;
    cellData.mRadius[firstEndpoint] = 0.02;
    cellData.mPositionX[secondEndpoint] = 0.04;
    cellData.mPositionY[secondEndpoint] = 0.04;
    cellData.mRadius[secondEndpoint] = 0.02;
    for (const auto [target, marker] : {std::pair{firstEndpoint, 0.01f}, std::pair{secondEndpoint, 0.012f}}) {
        addPairLink(cellData, source, target, 0.0, Environment::PI, 0.2f, 0.03f);
        cellData.mTopology.setEdgeRenderOffsets(cellData.mTopology.edgeCount() - 1, marker, 0.0f, -marker, 0.0f);
    }
    // A hidden owner has real contacts with other hidden Cells; its stretched
    // Links to visible endpoints still share the normalized owner cache.
    for (const auto [neighbor, positionX] : {std::pair{leftNeighbor, 0.47}, std::pair{rightNeighbor, 0.53}}) {
        cellData.mPositionX[neighbor] = positionX;
        cellData.mPositionY[neighbor] = 0.0;
        cellData.mRadius[neighbor] = 0.02;
        const double distanceX = 0.5 - positionX;
        assertTrue(cellData.addCellCollisionContact(source, neighbor, distanceX, 0.0, distanceX * distanceX)
                        && cellData.addCellCollisionContact(neighbor, source, -distanceX, 0.0, distanceX * distanceX),
                "Hidden-owner fixture stores its real reciprocal contacts");
    }
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    const int visibleCount = renderer.copyToDraw(world, view);
    const RenderContactChordBuffer& captured =
            NativeSimulationWorldRendererTestAccess::capturedContactChords(renderer);
    const std::vector<RenderContactChord> originalSourceWalls =
            copyActiveContactChords(captured, source);
    assertTrue(originalSourceWalls.size() == 2,
            "Native renderer captures both offscreen-source contacts");
    assertTrue(
            visibleCount == 2,
            "Native renderer excludes the offscreen Link source from Cell geometry");
    assertTrue(renderer.drawLinksCount() == 12,
            "Native renderer preserves both Links from the offscreen source: " + std::to_string(renderer.drawLinksCount()));
    assertNear(0.25f, renderer.linkTriangleCoords()[4],
            1e-7,
            "Native renderer preserves first offscreen-source Link order");
    const float secondBaseX = 0.27f - 0.04f * static_cast<float>(CellData::MIN_CELL_RADIUS * 0.7 * 0.5) / std::hypot(0.46f, 0.04f);
    assertNear(secondBaseX, renderer.linkTriangleCoords()[28],
            1e-7,
            "Native renderer preserves second offscreen-source Link order");

#ifdef CELLULARITY_RENDER_PROFILE
    const NativeSimulationWorldRenderer::CopyToDrawProfile profile =
            renderer.profileCopyToDraw(world, view);
    assertWallCacheProfileInvariants(
            profile, "Native renderer offscreen-source Cell wall cache");
    assertTrue(
            profile.contactChordResolveRequests == 4,
            "Native renderer resolves visible Cells and both hidden-owner Links");
    assertTrue(profile.contactChordDirectSourceViews == 2,
            "Native renderer directly resolves reciprocal endpoint contacts");
    assertTrue(profile.contactChordCacheMisses == 1
                    && profile.uniqueNormalizedCells == 1,
            "Native renderer normalizes its offscreen two-wall Link owner once");
    assertTrue(profile.contactChordCacheHits == 1
                    && profile.linkOnlyCacheMisses == 1,
            "The second hidden-owner Link reuses the same normalized cache entry");
    assertTrue(
            profile.normalizerCalls == 1
                    && profile.cellPassNormalizerCalls == 0
                    && profile.linkPassNormalizerCalls == 1,
            "Native renderer records offscreen normalization in the Link pass");
    assertTrue(profile.normalizedContactChordCount == 2
                    && profile.normalizerContactChordCountSum == 2,
            "Native renderer retains one normalized offscreen-owner range");
    assertTrue(profile.linkNormalFallbackCount == 2 && profile.linkContactCrossSectionCount == 0,
            "Stretched hidden-owner Links use fallback after resolving the owner's other contacts");
#endif

    const auto activeSourceContactChords =
            NativeSimulationWorldRendererTestAccess::capturedContactChords(renderer)
                    .activeChords(source);
    for (int wall = 0; wall < activeSourceContactChords.size(); wall++) {
        assertNear(originalSourceWalls[wall].x1,
                activeSourceContactChords[wall].x1, 0.0,
                "Native renderer keeps lazy source wall x1 immutable");
        assertNear(originalSourceWalls[wall].y1,
                activeSourceContactChords[wall].y1, 0.0,
                "Native renderer keeps lazy source wall y1 immutable");
        assertNear(originalSourceWalls[wall].x2,
                activeSourceContactChords[wall].x2, 0.0,
                "Native renderer keeps lazy source wall x2 immutable");
        assertNear(originalSourceWalls[wall].y2,
                activeSourceContactChords[wall].y2, 0.0,
                "Native renderer keeps lazy source wall y2 immutable");
    }
}

void testNativeSimulationWorldRendererRefreshesLinkBasesWithChangedContactChords() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int source = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int endpoint = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[source] = 0.0;
    cellData.mPositionY[source] = 0.0;
    cellData.mRadius[source] = 0.02;
    cellData.mPositionX[endpoint] = 0.03;
    cellData.mPositionY[endpoint] = 0.0;
    cellData.mRadius[endpoint] = 0.02;
    addPairLink(cellData, source, endpoint, 0.0, Environment::PI, 0.2f, 0.03f);
    auto replacePhysicsContact = [&](double distanceX, double distanceY) {
        const double distanceSquared =
                distanceX * distanceX + distanceY * distanceY;
        cellData.clearCellCollisionContacts();
        assertTrue(cellData.addKnownCellCollisionContactFast(
                           source, endpoint, distanceX, distanceY, distanceSquared),
                "Changed-wall fixture stores the source contact");
        assertTrue(cellData.addKnownCellCollisionContactFast(
                           endpoint, source, -distanceX, -distanceY,
                           distanceSquared),
                "Changed-wall fixture stores the reciprocal contact");
    };
    replacePhysicsContact(-0.03, 0.0);
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    const NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};

    struct ExtractionResult {
        std::pair<float, float> linkCrossSection;
        std::pair<float, float> cellBoundary;
    };

    auto extractAndCheck =
            [&](std::uint64_t expectedGeneration, const std::string& message) {
#ifdef CELLULARITY_RENDER_PROFILE
                const NativeSimulationWorldRenderer::CopyToDrawProfile profile =
                        renderer.profileCopyToDraw(world, view);
                assertWallCacheProfileInvariants(profile, message + " profile");
                assertTrue(profile.contactChordResolveRequests == 3 && profile.contactChordDirectSourceViews == 3 && profile.contactChordCacheMisses == 0 && profile.contactChordCacheHits == 0,
                        message + " request classes");
                assertTrue(profile.normalizerCalls == 0 && profile.cellPassNormalizerCalls == 0 && profile.linkPassNormalizerCalls == 0 && profile.normalizedContactChordCount == 0,
                        message + " normalization counts");
#else
                renderer.copyToDraw(world, view);
#endif
                assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(
                                   renderer)
                                == expectedGeneration,
                        message + " generation");
                assertTrue(renderer.drawLinksCount() == 6, message + " link output");
                const RenderContactChordBuffer& captured =
                        NativeSimulationWorldRendererTestAccess::capturedContactChords(renderer);
                const std::vector<RenderContactChord> sourceBefore =
                        copyActiveContactChords(captured, source);
                assertTrue(sourceBefore.size() == 1,
                        message + " captures the replacement contact");
                std::vector<RenderContactChord> normalized = sourceBefore;
                cellularity::render::clipAndOrderRenderContactChords(
                        normalized.data(), static_cast<int>(normalized.size()));
                const RenderContactChord& expected = findWallForCell(
                        normalized, endpoint,
                        "Native renderer changed-wall fixture lost its Cell wall");
                assertTrue(!expected.isDiscarded,
                        message + " keeps the Cell wall drawable");
                const std::array<float, 4> expectedCrossSection =
                        expectedContactLinkCrossSection(captured, cellData, endpoint, source);
                assertNear(expectedCrossSection[0],
                        renderer.linkTriangleCoords()[4], 1e-7,
                        message + " normalized owner Link cross-section x");
                assertNear(expectedCrossSection[1],
                        renderer.linkTriangleCoords()[5], 1e-7,
                        message + " normalized owner Link cross-section y");
                const auto activeContactChords = captured.activeChords(source);
                for (int wallIndex = 0; wallIndex < activeContactChords.size();
                        wallIndex++) {
                    assertWallExact(sourceBefore[static_cast<std::size_t>(wallIndex)],
                            activeContactChords[wallIndex],
                            message + " raw capture wall " + std::to_string(wallIndex));
                }
                return ExtractionResult{
                        {renderer.linkTriangleCoords()[4],
                                renderer.linkTriangleCoords()[5]},
                        {renderer.cellTriangleCoords()[2],
                                renderer.cellTriangleCoords()[3]}
                };
            };

    const ExtractionResult firstExtraction =
            extractAndCheck(1, "First extraction");

    replacePhysicsContact(-0.018, -0.024);
    const ExtractionResult secondExtraction =
            extractAndCheck(2, "Second extraction");
    assertTrue(firstExtraction.cellBoundary != secondExtraction.cellBoundary,
            "Native renderer refreshes changed Cell-wall geometry");
    assertTrue(
            firstExtraction.linkCrossSection != secondExtraction.linkCrossSection,
            "Native renderer Link geometry follows the changed owner contact");

    renderer.reset(2);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 0,
            "Native renderer reset invalidates changed-wall generation");
    replacePhysicsContact(0.0, -0.03);
    const ExtractionResult thirdExtraction =
            extractAndCheck(1, "Extraction after reset");
    assertTrue(secondExtraction.cellBoundary != thirdExtraction.cellBoundary,
            "Native renderer reset cannot expose the previous Cell-wall cache entry");
    assertTrue(
            secondExtraction.linkCrossSection != thirdExtraction.linkCrossSection,
            "Native renderer reset rebuilds the Link from the new owner contact");
}

void testNativeSimulationWorldRendererMixedLinksShareVisibleAndHiddenOwnerCache() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int visibleEndpointA = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int visibleEndpointB = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int visibleEndpointC = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int visibleEndpointD = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int visibleSource = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int leftNeighbor = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int rightNeighbor = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int offscreenSource = addAliveCell(cellData, CellType::PHAGOCYTE);

    const std::array<double, 8> positionsX{{0.03, 0.08, 0.08, 0.0, 0.0, 0.47, 0.53, 0.5}};
    const std::array<double, 8> positionsY{{0.0, 0.08, -0.08, 0.03, 0.0, 0.0, 0.0, 0.0}};
    for (int cellIndex = 0; cellIndex < cellData.mCellCount; cellIndex++) {
        cellData.mPositionX[cellIndex] =
                positionsX[static_cast<std::size_t>(cellIndex)];
        cellData.mPositionY[cellIndex] =
                positionsY[static_cast<std::size_t>(cellIndex)];
        cellData.mRadius[cellIndex] = 0.02;
        cellData.refreshRadiusSq(cellIndex);
    }

    auto addDirectedEdge = [&](int source, int target, float marker) {
        addPairLink(cellData, source, target, 0.0, Environment::PI, 0.2f, 0.01f);
        const int edgeIndex = cellData.mTopology.edgeCount() - 1;
        cellData.mTopology.setEdgeRenderOffsets(edgeIndex, marker, 0.0f, -marker,
                0.0f);
    };
    addDirectedEdge(visibleSource, visibleEndpointA, 0.010f);
    addDirectedEdge(offscreenSource, visibleEndpointB, 0.012f);
    addDirectedEdge(offscreenSource, visibleEndpointC, 0.014f);
    addDirectedEdge(visibleSource, visibleEndpointD, 0.016f);
    for (const auto [source, target] : {std::pair{visibleSource, visibleEndpointA}, std::pair{visibleSource, visibleEndpointD},
                 std::pair{offscreenSource, leftNeighbor}, std::pair{offscreenSource, rightNeighbor}}) {
        const double distanceX = cellData.mPositionX[source] - cellData.mPositionX[target];
        const double distanceY = cellData.mPositionY[source] - cellData.mPositionY[target];
        const double distanceSq = distanceX * distanceX + distanceY * distanceY;
        assertTrue(cellData.addCellCollisionContact(source, target, distanceX, distanceY, distanceSq)
                        && cellData.addCellCollisionContact(target, source, -distanceX, -distanceY, distanceSq),
                "Mixed-owner fixture stores real reciprocal contacts");
    }
    world.updateGrids();
    NativeSimulationWorldRendererTestAccess::markPhysicsContactFixtureFresh(world);

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    const NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
#ifdef CELLULARITY_RENDER_PROFILE
    const NativeSimulationWorldRenderer::CopyToDrawProfile profile =
            renderer.profileCopyToDraw(world, view);
    assertWallCacheProfileInvariants(profile,
            "Native renderer mixed-source Cell wall cache");
    assertTrue(profile.contactChordResolveRequests == 9 && profile.contactChordDirectSourceViews == 4 && profile.contactChordCacheMisses == 2 && profile.contactChordCacheHits == 3,
            "Native renderer resolves five Cell views and four shared-owner Links");
    assertTrue(
            profile.uniqueNormalizedCells == 2
                    && profile.normalizedContactChordCount == 4
                    && profile.linkOnlyCacheMisses == 1,
            "Native renderer normalizes visible and hidden owners once each");
    assertTrue(profile.normalizerCalls == 2
                    && profile.cellPassNormalizerCalls == 1
                    && profile.linkPassNormalizerCalls == 1,
            "Native renderer accounts for the hidden owner in the Link pass");
#else
    renderer.copyToDraw(world, view);
#endif

    const RenderContactChordBuffer& captured =
            NativeSimulationWorldRendererTestAccess::capturedContactChords(renderer);
    const std::vector<RenderContactChord> visibleSourceBefore =
            copyActiveContactChords(captured, visibleSource);
    const std::vector<RenderContactChord> offscreenSourceBefore =
            copyActiveContactChords(captured, offscreenSource);
    assertTrue(visibleSourceBefore.size() == 2 && offscreenSourceBefore.size() == 2,
            "Native renderer captures both visible and link-only sources");
    assertTrue(renderer.visibleCellCount() == 5,
            "Native renderer mixed-source fixture has five visible cells");
    assertTrue(
            renderer.drawLinksCount() == 24,
            "Native renderer mixed-source fixture preserves four ordered links");
    const std::array<float, 4> expectedFirstCrossSection =
            expectedContactLinkCrossSection(captured, cellData, visibleSource, visibleEndpointA);
    const std::array<float, 4> expectedLastCrossSection =
            expectedContactLinkCrossSection(captured, cellData, visibleSource, visibleEndpointD);
    assertNear(expectedFirstCrossSection[0], renderer.linkTriangleCoords()[4],
            1e-7,
            "Native renderer mixed-source first edge keeps source order");
    assertNear(expectedLastCrossSection[0], renderer.linkTriangleCoords()[76],
            1e-7,
            "Native renderer mixed-source final edge keeps source order");
    assertTrue(NativeSimulationWorldRendererTestAccess::cachedContactChordCount(
                       renderer, visibleSource)
                    == 2,
            "Native renderer caches the visible source active wall count");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::generationStamp(renderer,
                    visibleSource)
                    == NativeSimulationWorldRendererTestAccess::wallGeneration(renderer),
            "Native renderer visible-source entry belongs to the current extraction");
    for (int wallIndex = 0; wallIndex < 2; wallIndex++) {
        const auto visibleContactChords = captured.activeChords(visibleSource);
        const auto offscreenContactChords = captured.activeChords(offscreenSource);
        assertWallExact(visibleSourceBefore[static_cast<std::size_t>(wallIndex)],
                visibleContactChords[wallIndex],
                "Native renderer mixed visible source wall " + std::to_string(wallIndex));
        assertWallExact(offscreenSourceBefore[static_cast<std::size_t>(wallIndex)],
                offscreenContactChords[wallIndex],
                "Native renderer mixed offscreen source wall " + std::to_string(wallIndex));
    }
}

void testNativeSimulationWorldRendererCacheOffsetsSurviveCompactStorageGrowth() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int firstCell = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int secondCell = addAliveCell(cellData, CellType::PHAGOCYTE);
    RenderContactChordBuffer contactChords(environment.maxCellCount);
    contactChords.beginCollection(cellData.mCellCount);
    setCircularTestWalls(contactChords, firstCell, 2, 0.02, 100);
    setCircularTestWalls(
            contactChords, secondCell,
            cellularity::render::kMaximumRenderContactChordsPerCell, 0.02, 200);

    auto expectedFirst = copyActiveContactChords(contactChords, firstCell);
    auto expectedSecond = copyActiveContactChords(contactChords, secondCell);
    cellularity::render::clipAndOrderRenderContactChords(
            expectedFirst.data(), static_cast<int>(expectedFirst.size()));
    cellularity::render::clipAndOrderRenderContactChords(
            expectedSecond.data(), static_cast<int>(expectedSecond.size()));

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRendererTestAccess::beginWallExtraction(renderer);
    const std::vector<RenderContactChord> firstResolution =
            NativeSimulationWorldRendererTestAccess::resolveCellContactChords(
                    renderer, cellData, contactChords, firstCell);
    const std::size_t firstCapacity =
            NativeSimulationWorldRendererTestAccess::normalizedContactChordCapacity(renderer);
    const std::vector<RenderContactChord> secondResolution =
            NativeSimulationWorldRendererTestAccess::resolveCellContactChords(
                    renderer, cellData, contactChords, secondCell);
    const std::vector<RenderContactChord> firstHit =
            NativeSimulationWorldRendererTestAccess::resolveCellContactChords(
                    renderer, cellData, contactChords, firstCell);

    assertTrue(
            firstCapacity < NativeSimulationWorldRendererTestAccess::normalizedContactChordCapacity(renderer),
            "Native renderer compact wall storage grows after a larger second miss");
    assertTrue(firstResolution.size() == 2 && firstHit.size() == 2 && secondResolution.size() == static_cast<std::size_t>(cellularity::render::kMaximumRenderContactChordsPerCell),
            "Native renderer compact wall views expose only active ranges");
    for (int wallIndex = 0; wallIndex < 2; wallIndex++) {
        assertWallExact(expectedFirst[wallIndex],
                firstResolution[static_cast<std::size_t>(wallIndex)],
                "Native renderer first compact resolution wall " + std::to_string(wallIndex));
        assertWallExact(expectedFirst[wallIndex],
                firstHit[static_cast<std::size_t>(wallIndex)],
                "Native renderer compact hit after storage growth wall " + std::to_string(wallIndex));
    }
    for (int wallIndex = 0;
            wallIndex < cellularity::render::kMaximumRenderContactChordsPerCell;
            wallIndex++) {
        assertWallExact(expectedSecond[wallIndex],
                secondResolution[static_cast<std::size_t>(wallIndex)],
                "Native renderer second compact resolution wall " + std::to_string(wallIndex));
    }
    assertTrue(
            NativeSimulationWorldRendererTestAccess::contactChordOffset(renderer, firstCell) == 0 && NativeSimulationWorldRendererTestAccess::contactChordOffset(renderer, secondCell) == 2,
            "Native renderer compact growth preserves cache offsets");
}

void testCellDataActiveRangeNormalizerBounds() {
    cellularity::render::clipAndOrderRenderContactChords(nullptr, 0);
    constexpr int maximumRenderContactChordCount =
            cellularity::render::kMaximumRenderContactChordsPerCell;
    for (int contactChordCount = 0;
            contactChordCount <= maximumRenderContactChordCount;
            contactChordCount++) {
        std::array<RenderContactChord, maximumRenderContactChordCount> source{};
        for (int wallIndex = 0; wallIndex < maximumRenderContactChordCount;
                wallIndex++) {
            RenderContactChord& wall = source[static_cast<std::size_t>(wallIndex)];
            wall.x1 = (wallIndex + 1) * 0.0078125;
            wall.y1 = (wallIndex % 3 - 1) * 0.01171875;
            wall.x2 = -(wallIndex + 2) * 0.005859375;
            wall.y2 = (wallIndex % 5 - 2) * 0.009765625;
            wall.neighborCellIndex = wallIndex % 4 == 0 ? CellData::BOUNDARY_CONNECTION_INDEX
                                                        : 100 + wallIndex;
            wall.isDiscarded = wallIndex % 7 == 0;
            wall.joinsNextChord = wallIndex % 5 == 0;
        }

        auto wrapped = source;
        cellularity::render::clipAndOrderRenderContactChords(
                wrapped.data(), contactChordCount);
        for (int wallIndex = contactChordCount;
                wallIndex < maximumRenderContactChordCount;
                wallIndex++) {
            assertWallExact(source[static_cast<std::size_t>(wallIndex)],
                    wrapped[static_cast<std::size_t>(wallIndex)],
                    "CellData array normalizer inactive wall " + std::to_string(contactChordCount) + ":" + std::to_string(wallIndex));
        }

        std::unique_ptr<RenderContactChord[]> exactRange;
        if (contactChordCount > 0) {
            exactRange =
                    std::make_unique<RenderContactChord[]>(static_cast<std::size_t>(contactChordCount));
            std::copy_n(source.begin(), contactChordCount, exactRange.get());
        }
        cellularity::render::clipAndOrderRenderContactChords(exactRange.get(), contactChordCount);
        for (int wallIndex = 0; wallIndex < contactChordCount; wallIndex++) {
            assertWallExact(wrapped[static_cast<std::size_t>(wallIndex)],
                    exactRange[static_cast<std::size_t>(wallIndex)],
                    "CellData exact active-range wall " + std::to_string(contactChordCount) + ":" + std::to_string(wallIndex));
        }
    }
}

void testNativeSimulationWorldRendererNormalizesDirectlyIntoActiveStorage() {
    constexpr std::array<int, 7> wallCounts{{2, 3, 5, 6, 10, 20,
            cellularity::render::
                    kMaximumRenderContactChordsPerCell}};
    Environment environment = Environment::constructDefaultEnvironment(
            static_cast<int>(wallCounts.size()), 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    std::vector<int> cells;
    std::vector<std::vector<RenderContactChord>> sourceContactChords;
    std::vector<std::vector<RenderContactChord>> expectedWalls;
    RenderContactChordBuffer contactChords(environment.maxCellCount);
    contactChords.beginCollection(static_cast<int>(wallCounts.size()));
    for (std::size_t index = 0; index < wallCounts.size(); index++) {
        const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
        cells.push_back(cell);
        setCircularTestWalls(contactChords, cell, wallCounts[index],
                0.02 + static_cast<double>(index) * 0.001,
                100 + static_cast<int>(index) * 32);
        sourceContactChords.push_back(
                copyActiveContactChords(contactChords, cell));
        expectedWalls.push_back(sourceContactChords.back());
        cellularity::render::clipAndOrderRenderContactChords(
                expectedWalls.back().data(), wallCounts[index]);
    }

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRendererTestAccess::beginWallExtraction(renderer);
    std::size_t expectedOffset = 0;
    for (std::size_t index = 0; index < wallCounts.size(); index++) {
        const std::vector<RenderContactChord> resolved =
                NativeSimulationWorldRendererTestAccess::resolveCellContactChords(
                        renderer, cellData, contactChords, cells[index]);
        assertTrue(NativeSimulationWorldRendererTestAccess::contactChordOffset(
                           renderer, cells[index])
                        == expectedOffset,
                "Native renderer direct active-range cache offset " + std::to_string(index));
        assertTrue(resolved.size() == static_cast<std::size_t>(wallCounts[index]),
                "Native renderer direct active-range view count " + std::to_string(index));
        for (int wallIndex = 0; wallIndex < wallCounts[index]; wallIndex++) {
            assertWallExact(expectedWalls[index][static_cast<std::size_t>(wallIndex)],
                    resolved[static_cast<std::size_t>(wallIndex)],
                    "Native renderer direct active-range result " + std::to_string(index) + ":" + std::to_string(wallIndex));
        }
        expectedOffset += static_cast<std::size_t>(wallCounts[index]);
    }
    assertTrue(NativeSimulationWorldRendererTestAccess::normalizedContactChordCount(renderer) == expectedOffset && NativeSimulationWorldRendererTestAccess::normalizedContactChordPhysicalSize(renderer) == expectedOffset,
            "Native renderer direct active ranges exactly fill "
            "first-extraction storage");

    RenderContactChord inactiveMarker;
    inactiveMarker.x1 = 12345.125;
    inactiveMarker.y1 = -23456.25;
    inactiveMarker.x2 = 34567.5;
    inactiveMarker.y2 = -45678.75;
    inactiveMarker.neighborCellIndex = std::numeric_limits<int>::min() + 81;
    inactiveMarker.isDiscarded = true;
    inactiveMarker.joinsNextChord = true;
    NativeSimulationWorldRendererTestAccess::fillNormalizedWallStorage(renderer,
            inactiveMarker);
    NativeSimulationWorldRendererTestAccess::beginWallExtraction(renderer);
    const std::vector<RenderContactChord> reused =
            NativeSimulationWorldRendererTestAccess::resolveCellContactChords(
                    renderer, cellData, contactChords, cells.front());
    assertTrue(NativeSimulationWorldRendererTestAccess::normalizedContactChordCount(renderer) == 2 && NativeSimulationWorldRendererTestAccess::normalizedContactChordPhysicalSize(renderer) == expectedOffset,
            "Native renderer reuses physical high-water with a two-wall "
            "logical range");
    for (int wallIndex = 0; wallIndex < 2; wallIndex++) {
        assertWallExact(
                expectedWalls.front()[static_cast<std::size_t>(wallIndex)],
                reused[static_cast<std::size_t>(wallIndex)],
                "Native renderer rewrites only the new active storage range " + std::to_string(wallIndex));
    }
    for (std::size_t wallIndex = 2; wallIndex < expectedOffset; wallIndex++) {
        assertWallExact(
                inactiveMarker,
                NativeSimulationWorldRendererTestAccess::normalizedContactChordAt(renderer,
                        wallIndex),
                "Native renderer leaves physical inactive storage untouched " + std::to_string(wallIndex));
    }
    for (std::size_t index = 0; index < cells.size(); index++) {
        const auto activeContactChords = contactChords.activeChords(cells[index]);
        for (int wallIndex = 0; wallIndex < wallCounts[index]; wallIndex++) {
            assertWallExact(
                    sourceContactChords[index][static_cast<std::size_t>(wallIndex)],
                    activeContactChords[wallIndex],
                    "Native renderer direct active-range source wall " + std::to_string(index) + ":" + std::to_string(wallIndex));
        }
    }
}

void testNativeSimulationWorldRendererDebugChecksMalformedContactChordRequests() {
#if !defined(NDEBUG) && (defined(__unix__) || defined(__APPLE__))
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    RenderContactChordBuffer contactChords(environment.maxCellCount);
    contactChords.beginCollection(cellData.mCellCount);
    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRendererTestAccess::beginWallExtraction(renderer);

    assertAborts(
            [&]() {
                NativeSimulationWorldRendererTestAccess::resolveCellContactChords(
                        renderer, cellData, contactChords, -1);
            },
            "Native renderer debug checks reject a negative wall source index");
    assertAborts(
            [&]() {
                NativeSimulationWorldRendererTestAccess::resolveCellContactChords(
                        renderer, cellData, contactChords, cellData.mCellCount);
            },
            "Native renderer debug checks reject an out-of-range wall source index");

    RenderContactChord contactChord;
    for (int contactChordIndex = 0;
            contactChordIndex < cellularity::render::kMaximumRenderContactChordsPerCell;
            ++contactChordIndex) {
        assertTrue(contactChords.tryAppend(cell, contactChord),
                "Render contact chord buffer accepts its bounded capacity");
    }
    assertTrue(!contactChords.tryAppend(cell, contactChord) && contactChords.activeChordCount(cell) == cellularity::render::kMaximumRenderContactChordsPerCell,
            "Render contact chord buffer rejects overflow without changing "
            "its active range");

    std::array<RenderContactChord,
            cellularity::render::kMaximumRenderContactChordsPerCell>
            walls{};
    assertAborts([&]() {
        cellularity::render::clipAndOrderRenderContactChords(nullptr, 1);
    },
            "CellData active-range normalizer rejects null storage for a "
            "positive count");
    assertAborts([&]() {
        cellularity::render::clipAndOrderRenderContactChords(walls.data(), -1);
    },
            "CellData active-range normalizer rejects a negative count");
    assertAborts(
            [&]() {
                cellularity::render::clipAndOrderRenderContactChords(walls.data(),
                        cellularity::render::kMaximumRenderContactChordsPerCell + 1);
            },
            "CellData active-range normalizer rejects an excessive count");
#endif
}

void testNativeSimulationWorldRendererContactChordCacheLifecycle() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;
    world.updateGrids();

    NativeSimulationWorldRenderer renderer(environment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 0,
            "Native renderer wall cache starts before the first generation");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::generationEntryCount(renderer) == 4,
            "Native renderer wall cache generation metadata matches capacity");
    assertTrue(NativeSimulationWorldRendererTestAccess::offsetEntryCount(renderer) == 4,
            "Native renderer wall cache offset metadata matches capacity");
    assertTrue(NativeSimulationWorldRendererTestAccess::countEntryCount(renderer) == 4,
            "Native renderer wall cache count metadata matches capacity");
    assertTrue(NativeSimulationWorldRendererTestAccess::metadataIsZero(renderer),
            "Native renderer wall cache metadata starts cleared");

    NativeSimulationWorldRendererTestAccess::seedCacheState(renderer, 7, 3);
    const std::size_t warmStorageCapacity =
            NativeSimulationWorldRendererTestAccess::normalizedContactChordCapacity(renderer);
    renderer.copyToDraw(world, view);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 8,
            "Native renderer ordinary extraction advances wall cache "
            "generation once");
    assertTrue(NativeSimulationWorldRendererTestAccess::normalizedContactChordCount(renderer) == 0,
            "Native renderer ordinary extraction clears compact wall storage");
    assertTrue(NativeSimulationWorldRendererTestAccess::normalizedContactChordCapacity(
                       renderer)
                    == warmStorageCapacity,
            "Native renderer ordinary extraction preserves compact wall "
            "storage capacity");

    renderer.profileCopyToDraw(world, view);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 9,
            "Native renderer profiled extraction advances wall cache "
            "generation once");

    NativeSimulationWorldRendererTestAccess::seedCacheState(
            renderer, std::numeric_limits<std::uint64_t>::max(), 5);
    const std::size_t wrapStorageCapacity =
            NativeSimulationWorldRendererTestAccess::normalizedContactChordCapacity(renderer);
    renderer.copyToDraw(world, view);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 1,
            "Native renderer wall cache generation wraps to one");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::generationStampsAreZero(renderer),
            "Native renderer wall cache wrap invalidates every generation stamp");
    assertTrue(NativeSimulationWorldRendererTestAccess::normalizedContactChordCount(renderer) == 0,
            "Native renderer wall cache wrap clears compact wall storage");
    assertTrue(NativeSimulationWorldRendererTestAccess::normalizedContactChordCapacity(
                       renderer)
                    == wrapStorageCapacity,
            "Native renderer wall cache wrap preserves compact wall storage "
            "capacity");

    NativeSimulationWorldRendererTestAccess::seedCacheState(renderer, 3, 6);
    renderer.reset(2);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 0,
            "Native renderer reset returns wall cache generation to zero");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::generationEntryCount(renderer) == 2 && NativeSimulationWorldRendererTestAccess::offsetEntryCount(renderer) == 2 && NativeSimulationWorldRendererTestAccess::countEntryCount(renderer) == 2,
            "Native renderer reset shrinks wall cache metadata to renderer capacity");
    assertTrue(NativeSimulationWorldRendererTestAccess::metadataIsZero(renderer),
            "Native renderer reset clears shrunken wall cache metadata");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::normalizedContactChordCount(renderer) == 0 && NativeSimulationWorldRendererTestAccess::normalizedContactChordCapacity(renderer) == 0,
            "Native renderer reset releases compact wall storage high-water");
    const int visibleAfterShrinkingReset = renderer.copyToDraw(world, view);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 1,
            "Native renderer extraction after shrinking reset starts at "
            "generation one");
    assertTrue(
            visibleAfterShrinkingReset == 1 && renderer.cellDrawVertexCount() == 4,
            "Native renderer remains usable after shrinking wall cache metadata");

    renderer.reset(8);
    assertTrue(
            NativeSimulationWorldRendererTestAccess::generationEntryCount(renderer) == 8 && NativeSimulationWorldRendererTestAccess::offsetEntryCount(renderer) == 8 && NativeSimulationWorldRendererTestAccess::countEntryCount(renderer) == 8,
            "Native renderer reset grows wall cache metadata to renderer capacity");
    assertTrue(
            NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 0 && NativeSimulationWorldRendererTestAccess::metadataIsZero(renderer),
            "Native renderer growing reset fully invalidates wall cache metadata");
    const int visibleAfterGrowingReset = renderer.copyToDraw(world, view);
    assertTrue(NativeSimulationWorldRendererTestAccess::wallGeneration(renderer) == 1,
            "Native renderer extraction after growing reset starts at "
            "generation one");
    assertTrue(
            visibleAfterGrowingReset == 1 && renderer.cellDrawVertexCount() == 4,
            "Native renderer remains usable after growing wall cache metadata");
}

void testNativeCellularityControllerSeparatesWorldAndRenderLifetimes() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    NativeCellularityController controller(false, environment);
    SimulationWorld* originalWorld;
    {
        auto access = controller.writeWorld();
        originalWorld = &access.world();
        access.world().mTime = 37.5;
    }

    assertTrue(!controller.isPreviewMode(), "Native controller non-preview mode");
    assertTrue(!controller.hasRendererResources(),
            "Native controller starts without renderer resources");
    {
        auto access = controller.readWorld();
        assertTrue(access.world().mEnvironment.maxCellCount == environment.maxCellCount,
                "Native controller owns world");
    }
    assertTrue(!controller.hasRendererGpuBuffers(),
            "Native controller renderer starts without GL buffers");

    FakeGlApi gl;
    FakeRenderAssets assets;
    FakeTextureLoader textureLoader;
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);

    assertTrue(controller.hasRendererResources(),
            "Native controller loads renderer resources");
    assertTrue(controller.hasRendererGpuBuffers(),
            "Native controller creates renderer GL buffers");
    assertTrue(textureLoader.requests.size() == 2,
            "Native controller resource load requests textures");

    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);

    {
        auto access = controller.readWorld();
        assertTrue(
                &access.world() == originalWorld,
                "Native controller surface recreation preserves world ownership");
        assertNear(access.world().mTime, 37.5, 0.0,
                "Native controller surface recreation preserves world state");
    }
    assertTrue(controller.hasRendererResources(),
            "Native controller surface recreation reloads renderer resources");
    assertTrue(textureLoader.requests.size() == 4,
            "Native controller surface recreation reloads both textures");
    assertTrue(gl.deletedBuffers.size() == 13,
            "Native controller surface recreation releases stale GL buffers");

    controller.abandonRendererResources();

    assertTrue(!controller.hasRendererResources(),
            "Surface loss abandons renderer resources");
    assertTrue(!controller.hasRendererGpuBuffers(),
            "Surface loss forgets stale renderer handles");
    assertTrue(gl.deletedBuffers.size() == 13,
            "Surface loss does not issue GL calls after the context is gone");

    controller.update(false, 1.0);
    {
        auto access = controller.readWorld();
        assertTrue(&access.world() == originalWorld,
                "Native controller keeps world ownership after surface loss");
        assertTrue(access.world().mTime > 37.5,
                "Native controller simulation continues after surface loss");
    }

    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    assertTrue(controller.hasRendererResources(),
            "Recreated surface builds a fresh render session");
    assertTrue(controller.hasRendererGpuBuffers(),
            "Recreated surface creates fresh GL buffers");
    assertTrue(textureLoader.requests.size() == 6,
            "Recreated surface reloads textures after abandoned context");

    controller.releaseRendererResources();
    assertTrue(!controller.hasRendererResources(),
            "Explicit release clears renderer resources");
    assertTrue(!controller.hasRendererGpuBuffers(),
            "Explicit release deletes renderer GL buffers");
    assertTrue(gl.deletedBuffers.size() == 26,
            "Explicit release deletes all live renderer VBOs");
}

void testNativeCellularityControllerGenomePreviewOwnsDedicatedRenderer() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(100, 100);
    {
        auto access = controller.writeWorld();
        SimulationWorld& world = access.world();
        CellData& cellData = world.mCellData;
        const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
        cellData.mPositionX[cell] = 0.0;
        cellData.mPositionY[cell] = 0.0;
        cellData.mRadius[cell] = 0.02;
        cellData.setActiveGeneIndex(cell, cellularity::GeneIndex(0));
        world.updateGrids();
    }

    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    controller.setGenomePreviewSelectedGeneIndex(0);

    assertTrue(gl.nextBuffer == 27,
            "Genome Preview and microscope own separate GL buffers");
    assertTrue(controller.rendererVisibleCellCount() == 0,
            "Microscope renderer starts without Genome Preview geometry");

    gl.bufferSubDataCalls.clear();
    const int previewVisibleCount =
            controller.drawGenomePreview().semanticValue;
    assertTrue(previewVisibleCount == 1,
            "Dedicated Genome Preview renderer sees the preview Cell");
    assertTrue(controller.rendererVisibleCellCount() == 0,
            "Genome Preview draw does not replace the microscope frame");
    assertTrue(!gl.bufferSubDataCalls.empty() && std::all_of(gl.bufferSubDataCalls.begin(), gl.bufferSubDataCalls.end(), [](const FakeGlApi::BufferSubDataCall& call) {
        return call.buffer >= 1 && call.buffer <= 13;
    }),
            "Genome Preview uploads only through its dedicated GL buffers");

    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    assertTrue(gl.deletedBuffers.size() == 26 && gl.nextBuffer == 53,
            "Genome Preview reload replaces both live GL buffer sets");
    assertTrue(
            textureLoader.requests.size() == 4,
            "Genome Preview reload refreshes shared shader texture resources once");
    assertTrue(
            controller.drawGenomePreview().semanticValue == 1,
            "Dedicated Genome Preview renderer remains usable after resource reload");

    controller.releaseRendererResources();
    assertTrue(gl.deletedBuffers.size() == 52,
            "Genome Preview release deletes both live GL buffer sets");
}

void testNativeCellularityControllerGenomePreviewGhostSurvivesSurfaceLoss() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(100, 100);
    {
        auto access = controller.writeWorld();
        SimulationWorld& world = access.world();
        CellData& cellData = world.mCellData;
        const int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
        cellData.mPositionX[cell] = -0.02;
        cellData.mPositionY[cell] = 0.0;
        cellData.mRadius[cell] = 0.02;
        cellData.setActiveGeneIndex(cell, cellularity::GeneIndex(0));
        world.updateGrids();
    }

    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    controller.setGenomePreviewSelectedGeneIndex(0);
    controller.captureGenomePreviewGhost();
    {
        auto access = controller.writeWorld();
        access.world().mCellData.mPositionX[0] = 0.02;
        access.world().updateGrids();
    }

    gl.drawElementsCalls.clear();
    const int firstVisibleCount =
            controller.drawGenomePreview().semanticValue;
    assertTrue(firstVisibleCount == 1 && gl.drawElementsCalls.size() >= 3,
            "Genome Preview draws the synchronous Ghost snapshot before "
            "current Cells");
    assertTrue(controller.rendererVisibleCellCount() == 0,
            "Ghost restore never mutates the general prepared frame");

    controller.abandonRendererResources();
    assertTrue(gl.deletedBuffers.empty(), "Genome Preview surface loss abandons "
                                          "both buffer sets without GL deletion");
    assertTrue(!controller.hasRendererResources() && !controller.hasRendererGpuBuffers(),
            "Genome Preview surface loss forgets every stale render handle");

    controller.setViewportSurfaceSize(100, 100);
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    assertTrue(
            gl.nextBuffer == 53,
            "Genome Preview context recreation allocates two fresh buffer sets");
    gl.drawElementsCalls.clear();
    const int recreatedVisibleCount =
            controller.drawGenomePreview().semanticValue;
    assertTrue(recreatedVisibleCount == 1 && gl.drawElementsCalls.size() >= 3,
            "CPU-owned Ghost snapshot survives GL context recreation");

    controller.releaseRendererResources();
    assertTrue(
            gl.deletedBuffers.size() == 26,
            "Genome Preview deletes only buffers from the live recreated context");
}

void testNativeCellularityControllerGenomePreviewDrawsGridAndMarkerBeforeCells() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(100, 100);
    auto worldAccess = controller.writeWorld();
    SimulationWorld& world = worldAccess.world();
    CellData& cellData = world.mCellData;
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;
    cellData.setActiveGeneIndex(cell, cellularity::GeneIndex(0));
    world.updateGrids();

    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    controller.setGenomePreviewSelectedGeneIndex(0);

    int visibleCount = controller.drawGenomePreview().semanticValue;

    int arraysBeforeCells = 0;
    int firstCellEvent = -1;
    for (int i = 0; i < static_cast<int>(gl.drawEvents.size()); i++) {
        if (firstCellEvent == -1 && gl.drawEvents[i].type == "elements") {
            firstCellEvent = i;
        }
        if (firstCellEvent == -1 && gl.drawEvents[i].type == "arrays" && gl.drawEvents[i].count == 6) {
            arraysBeforeCells++;
        }
    }

    assertTrue(visibleCount == 1,
            "Native controller genome preview visible count");
    assertTrue(firstCellEvent != -1,
            "Native controller genome preview draws cells");
    assertTrue(
            arraysBeforeCells >= 2,
            "Native controller genome preview draws grid and marker below cells");

    controller.releaseRendererResources();
}

void testNativeCellularityControllerGenomePreviewCanDisableGrid() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(100, 100);
    auto worldAccess = controller.writeWorld();
    SimulationWorld& world = worldAccess.world();
    CellData& cellData = world.mCellData;
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.0;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;
    cellData.setActiveGeneIndex(cell, cellularity::GeneIndex(0));
    world.updateGrids();

    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    controller.setGenomePreviewSelectedGeneIndex(0);
    controller.setGenomePreviewGridEnabled(false);

    int visibleCount = controller.drawGenomePreview().semanticValue;

    int arraysBeforeCells = 0;
    bool sawCellEvent = false;
    for (const FakeGlApi::DrawEvent& event : gl.drawEvents) {
        if (event.type == "elements") {
            sawCellEvent = true;
            break;
        }
        if (event.type == "arrays" && event.count == 6) {
            arraysBeforeCells++;
        }
    }

    assertTrue(visibleCount == 1,
            "Native controller genome preview disabled grid visible count");
    assertTrue(sawCellEvent,
            "Native controller genome preview disabled grid draws cells");
    assertTrue(arraysBeforeCells == 1,
            "Native controller genome preview disabled grid only draws marker "
            "before cells");

    controller.releaseRendererResources();
}

void testNativeCellularityControllerGenomePreviewTreeDrawsTimelineNodes() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(100, 100);
    auto worldAccess = controller.writeWorld();
    SimulationWorld& world = worldAccess.world();
    CellData& cellData = world.mCellData;
    int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.editActiveGene(parent, [](cellularity::GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(cellularity::GeneIndex(0));
        parentGene.setChild2GeneIndex(cellularity::GeneIndex(0));
        parentGene.setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
        parentGene.setColor(0, 0.2f);
        parentGene.setColor(1, 0.3f);
        parentGene.setColor(2, 0.4f);
    });
    cellData.mMass[parent] = 0.2;
    cellData.mRadius[parent] = 0.02;
    cellData.mAge[parent] = CellData::AGE_OF_CONSENT + 0.1;
    cellData.mNitrates[parent] = 0.8;

    controller.resetGenomePreviewTreeState();
    controller.beginGenomePreviewTreeStep();
    cellData.handleCellSplit(world);
    controller.recordGenomePreviewTreeSplits();

    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    controller.setGenomePreviewSelectedGeneIndex(0);
    controller.setGenomePreviewTreeEnabled(true);
    controller.setViewportCamera(0.0F, 0.0F, 1.0F);

    int visibleCount = controller.drawGenomePreview().semanticValue;

    assertTrue(visibleCount == 3, "Native controller genome tree preview returns "
                                  "self-split unfolded node count");
    assertTrue(
            controller.genomePreviewTreeNodeCount() == 3,
            "Native controller genome tree records self-split parent and children");
    assertTrue(!gl.drawElementsCalls.empty(),
            "Native controller genome tree draws nodes through cell renderer");

    controller.releaseRendererResources();
}

cellularity::GenomeData makeDynamicPreviewGenome(std::size_t geneCount) {
    cellularity::GenomeData genome(geneCount);
    const std::size_t activeIndex = geneCount - 1;
    for (std::size_t index = 0; index < geneCount; ++index) {
        const cellularity::GeneIndex owner(
                static_cast<cellularity::GeneIndex::Value>(index));
        const auto gene = cellularity::materializeCanonicalGene(
                cellularity::CanonicalGeneDefaults::value(), owner, geneCount,
                index == activeIndex);
        assertTrue(gene.has_value(), "Dynamic preview Gene materializes");
        genome[index] = *gene;
        cellularity::GeneBuilder builder(genome[index]);
        builder.setCellType(CellType::PHAGOCYTE);
        builder.setColor(0, 0.15f);
        builder.setColor(1, 0.35f);
        builder.setColor(2, 0.75f);
    }
    cellularity::GeneBuilder active(genome[activeIndex]);
    active.setSplitMass(static_cast<float>(CellData::MIN_MASS));
    active.setSplitRatio(0.5f);
    active.setChild1GeneIndex(cellularity::SELF_GENE_INDEX);
    active.setChild2GeneIndex(cellularity::SELF_GENE_INDEX);
    active.setIntegerValue(Gene::MAX_CONNECTION_COUNT_INDEX,
            CellData::MAX_NEIGHBORS - 1);
    active.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    return genome;
}

void testGenomePreviewGhostAndTreeSupportDynamicGeneIndices() {
    constexpr std::size_t geneCount = 150;
    constexpr std::size_t activeIndex = geneCount - 1;
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(100, 100);
    {
        auto access = controller.writeWorld();
        SimulationWorld& world = access.world();
        CellData& cells = world.mCellData;
        const int parent = addAliveCell(cells, CellType::PHAGOCYTE);
        assertTrue(cells.replaceGenome(
                           parent, makeDynamicPreviewGenome(geneCount),
                           cellularity::GeneIndex(
                                   static_cast<cellularity::GeneIndex::Value>(activeIndex))),
                "Genome Preview installs a dynamic Genome");
        cells.mPositionX[parent] = 0.0;
        cells.mPositionY[parent] = 0.0;
        cells.mMass[parent] = 0.2;
        cells.mRadius[parent] = 0.02;
        cells.refreshRadiusSq(parent);
        cells.mAge[parent] = CellData::AGE_OF_CONSENT + 0.1;
        cells.mNitrates[parent] = 0.8;
        world.updateGrids();
    }

    assertTrue(controller.selectGenomePreviewGeneAt(0.0, 0.0) == static_cast<int>(activeIndex),
            "Genome Preview cell selection returns a dynamic Gene index");

    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    controller.setGenomePreviewSelectedGeneIndex(
            static_cast<int>(activeIndex));
    controller.captureGenomePreviewGhost();
    assertTrue(controller.drawGenomePreview()
                            .semanticValue
                    == 1,
            "Genome Preview Ghost renders a dynamic active Gene");

    controller.resetGenomePreviewTreeState();
    controller.beginGenomePreviewTreeStep();
    {
        auto access = controller.writeWorld();
        access.world().mCellData.handleCellSplit(access.world());
    }
    controller.recordGenomePreviewTreeSplits();
    controller.setGenomePreviewTreeEnabled(true);
    controller.setViewportCamera(0.0F, 0.0F, 1.0F);

    assertTrue(controller.drawGenomePreview()
                            .semanticValue
                    == 3,
            "Genome Preview tree renders dynamic-index split nodes");
    assertTrue(controller.genomePreviewTreeNodeCount() == 3,
            "Genome Preview tree records dynamic-index descendants");

    controller.releaseRendererResources();
}

void testGenomePreviewTreeRenderWorldClearsStaleTopology() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld sourceWorld(true, environment);
    NativeSimulationWorldRenderer renderer(4);
    GenomePreviewTreeRenderWorld renderWorld;

    std::vector<GenomePreviewTreeNode> nodes(2);
    const auto defaultGenome = canonicalDefaultGenome();
    nodes[0].renderGene = defaultGenome[0];
    cellularity::GeneBuilder(nodes[0].renderGene)
            .setCellType(CellType::PHAGOCYTE);
    nodes[0].radius = 0.02f;
    nodes[1].renderGene = defaultGenome[0];
    cellularity::GeneBuilder(nodes[1].renderGene)
            .setCellType(CellType::GLUEOCYTE);
    nodes[1].radius = 0.02f;

    const SimulationWorld* firstWorld =
            renderWorld.update(sourceWorld, nodes, renderer);
    assertTrue(firstWorld != nullptr,
            "Genome preview tree render world first update creates world");
    // White-box corruption setup: production consumers receive this buffer as
    // const.
    SimulationWorld& mutableTestWorld = const_cast<SimulationWorld&>(*firstWorld);
    mutableTestWorld.mCellData.mTopology.addCellLink(
            0, 1, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.5f, 0.0f);
    assertTrue(firstWorld->mCellData.mTopology.edgeCount() == 1,
            "Genome preview tree render world test seeds stale topology");

    sourceWorld.mEnvironment.mLightAngle = 0.23;
    sourceWorld.mEnvironment.mLightRange = 0.47;
    sourceWorld.mEnvironment.mLightIntensity = 1.31;
    sourceWorld.mEnvironment.mRadius = 2.75;
    nodes.resize(1);
    const SimulationWorld* secondWorld =
            renderWorld.update(sourceWorld, nodes, renderer);

    assertTrue(
            secondWorld == firstWorld,
            "Genome preview tree render world reuses world when capacity is enough");
    assertTrue(secondWorld->mCellData.mTopology.edgeCount() == 0,
            "Genome preview tree render world clears stale cell links");
    assertTrue(secondWorld->mCellData.mTopology.boundaryCount() == 0,
            "Genome preview tree render world clears stale boundary links");
    assertTrue(secondWorld->mCellData.mTopology.linkCount(0) == 0,
            "Genome preview tree render world clears stale adjacency");
    assertTrue(secondWorld->mEnvironment.mIsPreviewMode,
            "Genome preview tree render world remains in preview mode");
    assertNear(secondWorld->mEnvironment.mLightAngle, 0.23, 0.000001,
            "Genome preview tree render world refreshes light angle");
    assertNear(secondWorld->mEnvironment.mLightRange, 0.47, 0.000001,
            "Genome preview tree render world refreshes light range");
    assertNear(secondWorld->mEnvironment.mLightIntensity, 1.31, 0.000001,
            "Genome preview tree render world refreshes light intensity");
    assertNear(secondWorld->mEnvironment.mRadius, 2.75, 0.000001,
            "Genome preview tree render world refreshes dish radius");
}

void testNativeCellularityControllerGenomePreviewTreeAllowsUnboundedCamera() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    NativeCellularityController controller(true, environment);
    controller.setViewportSurfaceSize(100, 100);

    controller.moveViewportByScreenDelta(-1000.0f, 1000.0f);
    assertTrue(std::abs(controller.viewportCameraX())
                    <= static_cast<float>(environment.mRadius * 1.1),
            "Native controller genome preview camera is bounded by default");
    assertTrue(std::abs(controller.viewportCameraY())
                    <= static_cast<float>(environment.mRadius * 1.1),
            "Native controller genome preview camera y is bounded by default");

    controller.setGenomePreviewTreeEnabled(true);
    controller.moveViewportByScreenDelta(-1000.0f, 1000.0f);
    assertTrue(
            controller.viewportCameraX()
                    > static_cast<float>(environment.mRadius * 1.1),
            "Native controller genome tree camera can move past world x bounds");
    assertTrue(
            controller.viewportCameraY()
                    > static_cast<float>(environment.mRadius * 1.1),
            "Native controller genome tree camera can move past world y bounds");

    controller.setGenomePreviewTreeEnabled(false);
    assertTrue(
            std::abs(controller.viewportCameraX())
                    <= static_cast<float>(environment.mRadius * 1.1),
            "Native controller genome preview camera clamps again after tree mode");
    assertTrue(
            std::abs(controller.viewportCameraY())
                    <= static_cast<float>(environment.mRadius * 1.1),
            "Native controller genome preview camera y clamps again after tree mode");
}

void testSubstratePreviewUsesProductionRendererOffscreen() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 2.0);
    environment.mLightAngle = 0.31;
    environment.mLightRange = 0.42;
    environment.mLightIntensity = 0.73;
    SimulationWorld world(false, environment);
    const int cell = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[cell] = 0.25;
    world.mCellData.mPositionY[cell] = -0.15;
    world.mCellData.mRadius[cell] = 0.04;
    world.mCellData.refreshRadiusSq(cell);
    world.updateGrids();

    FakeRenderAssets assets;
    OffscreenFakeGlApi gl;
    FakeTextureLoader textureLoader;
    NativeRenderSession session(environment.maxCellCount,
            std::make_unique<cellularity::render::gles::GlesRenderSurfaceSession>(
                    gl));
    session.loadResources(world, assets, textureLoader);

    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 7;
    metadata.completedUpdateId = 11;
    metadata.environmentRevision = 13;
    const std::uint64_t token =
            session.prepareSubstratePreviewFrame(world, metadata);
    const std::vector<std::uint8_t> rgba = session.captureSubstratePreviewRgba(
            token, {0.73f, 0.70f, 0.95f});

    constexpr std::size_t kExpectedBytes =
            static_cast<std::size_t>(NativeRenderSession::SUBSTRATE_PREVIEW_PIXELS) * NativeRenderSession::SUBSTRATE_PREVIEW_PIXELS * 4U;
    assertTrue(rgba.size() == kExpectedBytes,
            "Off-screen substrate preview returns one RGBA8 frame");
    assertTrue(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 17 && rgba[3] == 255,
            "Off-screen substrate preview converts GLES rows to top-left order");
    assertTrue(rgba[rgba.size() - static_cast<std::size_t>(NativeRenderSession::SUBSTRATE_PREVIEW_PIXELS) * 4U] == 0,
            "Off-screen substrate preview keeps the bottom source row last");
    assertTrue(!gl.drawEvents.empty(),
            "Off-screen substrate preview executes production draw calls");
    assertTrue(!gl.uniformMatrixCalls.empty(),
            "Off-screen substrate preview supplies the production shaders");
    const float expectedWorldScale = 1.0f / (2.0f * 1.04f);
    assertNear(expectedWorldScale, gl.uniformMatrixCalls.front().value[0], 1e-6,
            "Substrate preview frames the complete dish width with margin");
    assertNear(expectedWorldScale, gl.uniformMatrixCalls.front().value[5], 1e-6,
            "Substrate preview frames the complete dish height with margin");
    assertNear(0.0, gl.uniformMatrixCalls.front().value[12], 1e-7,
            "Substrate preview camera is centered horizontally");
    assertNear(0.0, gl.uniformMatrixCalls.front().value[13], 1e-7,
            "Substrate preview camera is centered vertically");
    assertTrue(gl.currentFramebuffer == 41 && gl.lastViewport == std::array<int, 4>{7, 8, 9, 10} && gl.currentPackAlignment == 4,
            "Off-screen substrate preview restores caller GLES state");
    assertTrue(session.captureSubstratePreviewRgba(
                              token, {0.73f, 0.70f, 0.95f})
                       .empty(),
            "A staged substrate preview frame is consumed exactly once");
    const std::size_t uploadCallsAfterFirstPreview = gl.bufferDataCalls.size();

    const std::uint64_t discardedToken =
            session.prepareSubstratePreviewFrame(world, metadata);
    session.discardSubstratePreviewFrame(discardedToken);
    const std::uint64_t replacementToken =
            session.prepareSubstratePreviewFrame(world, metadata);
    assertTrue(replacementToken != discardedToken,
            "Substrate preview tokens are never reused after discard");
    assertTrue(session.captureSubstratePreviewRgba(
                              discardedToken, {0.73f, 0.70f, 0.95f})
                       .empty(),
            "A delayed preview request cannot consume a later save frame");
    const std::vector<std::uint8_t> replacementRgba =
            session.captureSubstratePreviewRgba(
                    replacementToken, {0.73f, 0.70f, 0.95f});
    assertTrue(!replacementRgba.empty(),
            "The current preview request remains valid after a stale request");
    assertTrue(gl.bufferDataCalls.size() > uploadCallsAfterFirstPreview,
            "Each save preview owns a unique GPU upload revision");

    world.mEnvironment.mRadius = 0.0;
    assertTrue(session.tryPrepareSubstratePreviewFrame(world, metadata) == 0,
            "Optional preview preparation failure produces no-preview state");
    world.mEnvironment.mRadius = 2.0;
    const std::uint64_t recoveredToken =
            session.tryPrepareSubstratePreviewFrame(world, metadata);
    assertTrue(recoveredToken != 0,
            "Preview preparation recovers after an optional failure");
    session.discardSubstratePreviewFrame(recoveredToken);

    session.releaseResources();
}

void testGenomeFilePreviewStopsAtTimeLimitAndFramesFounder() {
    const auto genome = canonicalDefaultGenomeData();
    auto developed =
            cellularity::genome_preview::developGenomeForFilePreview(genome, 8, 0.0);

    assertTrue(
            developed.stopReason == cellularity::genome_preview::DevelopmentStopReason::TimeLimitReached && developed.completedUpdates == 0,
            "Genome file preview obeys a zero simulation-time limit");
    assertTrue(developed.world != nullptr && developed.world->mCellData.mCellCount == 1 && isKnownOrganismId(developed.organismId) && developed.world->mCellData.organismId(0) == developed.organismId,
            "Time-limited Genome preview retains exactly its living founder");

    developed.world->mCellData.mPositionX[0] = 0.37;
    developed.world->mCellData.mPositionY[0] = -0.21;
    const auto framing =
            cellularity::genome_preview::frameGenomeFilePreview(*developed.world);
    assertTrue(framing.visibleCellCount == 1 && std::isfinite(framing.halfExtent) && framing.halfExtent > 0.0f,
            "Genome file preview framing is finite and includes the founder");
    assertNear(0.37, framing.centerX, 1e-6,
            "Genome file preview centers its living organism horizontally");
    assertNear(-0.21, framing.centerY, 1e-6,
            "Genome file preview centers its living organism vertically");
}

void testGenomeFilePreviewStopsAtCellLimit() {
    auto genome = canonicalDefaultGenomeData();
    cellularity::GeneBuilder(genome[0]).setMakesLink(true);
    auto developed = cellularity::genome_preview::developGenomeForFilePreview(
            genome, 2, cellularity::genome_preview::kGenomeFilePreviewMaximumTime);

    assertTrue(
            developed.stopReason == cellularity::genome_preview::DevelopmentStopReason::CellLimitReached && developed.completedUpdates > 0 && developed.world->mCellData.mCellCount == 2 && developed.world->mCellData.mTopology.edgeCount() == 1,
            "Genome file preview stops at its Cell limit with the complete linked organism");
    assertTrue(
            developed.world->mCellData.organismId(0) == developed.organismId && developed.world->mCellData.organismId(1) == developed.organismId,
            "Cell-limited Genome preview does not discard members of one organism");
}

void testGenomeFilePreviewStopsWhenDetachedOffspringAppears() {
    const auto genome = canonicalDefaultGenomeData();
    auto developed = cellularity::genome_preview::developGenomeForFilePreview(
            genome, 8, cellularity::genome_preview::kGenomeFilePreviewMaximumTime);

    assertTrue(
            developed.stopReason == cellularity::genome_preview::DevelopmentStopReason::OffspringProduced,
            "Genome file preview recognizes the first detached offspring");
    assertTrue(developed.completedUpdates > 0 && developed.world->mCellData.mCellCount == 1,
            "Genome file preview develops before retaining one organism");
    for (int cellIndex = 0;
            cellIndex < developed.world->mCellData.mCellCount;
            ++cellIndex) {
        assertTrue(
                !developed.world->mCellData.mIsDead[cellIndex] && developed.world->mCellData.organismId(cellIndex) == developed.organismId,
                "Genome file preview removes detached descendants from the render world");
    }
}

void testGenomeFilePreviewCaptureUsesTransparentProductionRendering() {
    Environment environment = Environment::setupPreviewSimulationEnvironment(8);
    NativeCellularityController controller(true, environment);
    FakeRenderAssets assets;
    OffscreenFakeGlApi gl;
    FakeTextureLoader textureLoader;
    ensureGlesBackend(controller, gl);
    controller.loadRendererResources(assets, textureLoader);
    controller.setViewportSurfaceSize(200, 100);

    const auto endpoint = controller.genomePreviewEndpoint();
    const std::uint64_t attachment = endpoint->surfaceCreated();
    const std::vector<std::uint8_t> visibleGenome =
            writeGenomeDraft(canonicalDefaultGenomeData(1));
    const std::vector<std::uint8_t> capturedGenome =
            writeGenomeDraft(canonicalDefaultGenomeData(2));
    const auto content = endpoint->publishContent(visibleGenome.data(),
            visibleGenome.size(), 0.0, 1, endpoint->generation());
    const auto view = endpoint->publishViewState(
            {0, true, false, 320, 240}, 1, endpoint->generation());
    endpoint->applyPendingOnRenderOwner(controller);
    const auto beforeCapture = endpoint->readAcknowledgment();
    auto admission = endpoint->enqueueFileCapture(1,
            capturedGenome.data(), capturedGenome.size(),
            cellularity::genome_preview::kGenomeFilePreviewMaximumTime,
            endpoint->generation(), attachment);
    assertTrue(admission.accepted() && admission.request != nullptr,
            "Typed Genome file capture admission succeeds");
    endpoint->applyPendingOnRenderOwner(controller);
    auto& request = static_cast<
            cellularity::app::preview::PreviewCaptureRequestState&>(
            *admission.request);
    auto result = request.await(std::chrono::milliseconds(10));

    constexpr std::size_t kExpectedBytes =
            static_cast<std::size_t>(
                    cellularity::genome_preview::kGenomeFilePreviewPixels)
            * cellularity::genome_preview::kGenomeFilePreviewPixels * 4U;
    assertTrue(attachment != 0 && content.accepted() && view.accepted()
                    && admission.accepted()
                    && result.header.status
                            == cellularity::app::preview::
                                    PreviewCaptureStatus::Completed
                    && result.payload != nullptr
                    && result.payload->bytes.size() == kExpectedBytes,
            "Genome file preview returns one RGBA8 frame");
    assertTrue(gl.lastClearColor == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f},
            "Genome file preview clears to fully transparent black");
    assertTrue(!gl.drawEvents.empty() && !gl.uniformMatrixCalls.empty(),
            "Genome file preview uses production Cell shaders and geometry");
    assertTrue(gl.currentFramebuffer == 41 && gl.lastViewport == std::array<int, 4>{7, 8, 9, 10} && gl.currentPackAlignment == 4,
            "Genome file preview restores caller GLES state");

    const auto afterCapture = endpoint->readAcknowledgment();
    const auto ordinaryFrame = controller.drawGenomePreview();
    const auto visibleWorld = controller.readWorld();
    assertTrue(afterCapture.appliedContentRevision
                            == beforeCapture.appliedContentRevision
                    && afterCapture.appliedViewRevision
                            == beforeCapture.appliedViewRevision,
            "Independent file capture preserves visible editor revisions");
    assertTrue(visibleWorld.world().mCellData.mCellCount == 1,
            "Independent file capture preserves the visible Cell world");
    assertTrue(visibleWorld.world().mCellData.geneCount(0) == 1,
            "Independent file capture preserves the visible Genome");
    assertTrue(ordinaryFrame.semanticValue == 1,
            "The first ordinary frame after file capture renders visible editor state: outcome="
                    + std::to_string(static_cast<int>(ordinaryFrame.outcome))
                    + ", semantic="
                    + std::to_string(ordinaryFrame.semanticValue));
    assertTrue(request.consumeDelivery(result.deliveryToken),
            "Endpoint capture payload is consumed exactly once");

    endpoint->surfaceDestroyed(attachment);
    controller.releaseRendererResources();
}

} // namespace

int main() {
    testPreparedRenderFrameOwnsCompleteSynchronousState();
    testRenderFrameBuilderMatchesRendererCompatibilityWrapper();
    testNativeSimulationWorldRendererGridCullingReadsWorldDirectly();
    testSimulationWorldRenderGridIsExactAndRebuildsTouchedBuckets();
    testNativeSimulationWorldRendererCandidatesPreserveGlobalOrderBeyondCollisionCapacity();
    testNativeSimulationWorldRendererCandidatesCoverOffWorldBordersAndClearBetweenViews();
    testNativeSimulationWorldRendererCandidateCullIsInclusiveAndFinite();
    testNativeSimulationWorldRendererPreviewLinearCullingAndExtraBucket();
    testNativeSimulationWorldRendererFoodDeadAndLinkBuffers();
    testNativeSimulationWorldRendererDenseTopologyGrowsLinkBuffersOnce();
    testNativeSimulationWorldRendererNeighborCellGeometry();
    testNativeSimulationWorldRendererSkipsBrokenNeighborLikeFirstCommitFan();
    testNativeSimulationWorldRendererFlagellocyteOutlineBuffers();
    testNativeSimulationWorldRendererGenericOutlineBuffers();
    testNativeCellShaderLoadsAssetsAndDrawsElementsVbo();
    testNativeFoodShaderDrawsInstancedVbo();
    testNativeCellRendererResourcesLoadsShadersAndTextures();
    testNativeCellRendererResourcesLoadsPreviewArrowShader();
    testPixelTextRendererOwnsReusableCachedGeometry();
#ifdef CELLULARITY_PERFORMANCE_HUD
    testMicroscopeDebugHudCachesPixelGeometryInOneVbo();
#endif
    testNativeSimulationWorldRendererUploadDynamicBuffers();
    testSimulationWorldGpuRendererSkipsOnlyUnchangedUploads();
    testNativeSimulationWorldRendererConsumesAnExternalPreparedFrame();
    testSimulationWorldGpuRendererDrawCellsUsesPerTypeRelativeIndices();
    testNativeSimulationWorldRendererCopyToDrawDoesNotMutateContactChords();
    testNativeSimulationWorldRendererLinksResolveOffscreenOwnerWalls();
    testNativeSimulationWorldRendererRefreshesLinkBasesWithChangedContactChords();
    testNativeSimulationWorldRendererMixedLinksShareVisibleAndHiddenOwnerCache();
    testNativeSimulationWorldRendererCacheOffsetsSurviveCompactStorageGrowth();
    testCellDataActiveRangeNormalizerBounds();
    testNativeSimulationWorldRendererNormalizesDirectlyIntoActiveStorage();
    testNativeSimulationWorldRendererDebugChecksMalformedContactChordRequests();
    testNativeSimulationWorldRendererContactChordCacheLifecycle();
    testNativeCellularityControllerSeparatesWorldAndRenderLifetimes();
    testNativeCellularityControllerGenomePreviewOwnsDedicatedRenderer();
    testNativeCellularityControllerGenomePreviewGhostSurvivesSurfaceLoss();
    testNativeCellularityControllerGenomePreviewDrawsGridAndMarkerBeforeCells();
    testNativeCellularityControllerGenomePreviewCanDisableGrid();
    testNativeCellularityControllerGenomePreviewTreeDrawsTimelineNodes();
    testGenomePreviewGhostAndTreeSupportDynamicGeneIndices();
    testGenomePreviewTreeRenderWorldClearsStaleTopology();
    testNativeCellularityControllerGenomePreviewTreeAllowsUnboundedCamera();
    testSubstratePreviewUsesProductionRendererOffscreen();
    testGenomeFilePreviewStopsAtTimeLimitAndFramesFounder();
    testGenomeFilePreviewStopsAtCellLimit();
    testGenomeFilePreviewStopsWhenDetachedOffspringAppears();
    testGenomeFilePreviewCaptureUsesTransparentProductionRendering();
    std::cout << "native rendering parity tests passed\n";
    return 0;
}
