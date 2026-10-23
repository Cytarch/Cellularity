#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <vector>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "render/frame/CellBoundaryMeshBuilder.h"
#include "render/frame/RenderContactChordBuffer.h"
#include "render/frame/RenderContactChordResolver.h"
#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/geometry/RenderContactChordNormalizer.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

namespace {

using cellularity::render::CellBoundaryMeshBuilder;
using cellularity::render::PreparedRenderFrame;
using cellularity::render::RenderCaptureStage;
using cellularity::render::RenderContactChordResolver;
using cellularity::render::RenderFrameBuilder;
using cellularity::render::RenderFrameCaptureMetadata;
using cellularity::render::RenderSourceCapture;
using cellularity::render::RenderSourceCell;
using cellularity::render::RenderSourceFrame;
using cellularity::render::RenderSourceLink;
using cellularity::render::RenderViewBounds;
using cellularity::tests::addAliveCell;
using cellularity::tests::addPairLink;
using cellularity::tests::assertTrue;

static_assert(!std::is_copy_constructible_v<RenderFrameBuilder>);
static_assert(!std::is_copy_assignable_v<RenderFrameBuilder>);
static_assert(!std::is_move_constructible_v<RenderFrameBuilder>);
static_assert(!std::is_move_assignable_v<RenderFrameBuilder>);
static_assert(!std::is_copy_constructible_v<RenderContactChordResolver>);
static_assert(!std::is_copy_assignable_v<RenderContactChordResolver>);

template <typename T>
void assertPrefixExact(const std::vector<T>& expected,
        const std::vector<T>& actual, std::size_t count,
        const char* message) {
    static_assert(std::is_trivially_copyable_v<T>);
    assertTrue(expected.size() >= count && actual.size() >= count, message);
    assertTrue(count == 0 || std::memcmp(expected.data(), actual.data(), count * sizeof(T)) == 0,
            message);
}

template <typename T>
std::vector<std::byte> objectBytes(const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::vector<std::byte> bytes(values.size() * sizeof(T));
    if (!bytes.empty()) {
        std::memcpy(bytes.data(), values.data(), bytes.size());
    }
    return bytes;
}

void assertChordExact(const RenderContactChord& expected,
        const RenderContactChord& actual, const char* message) {
    assertTrue(expected.x1 == actual.x1 && expected.y1 == actual.y1 && expected.x2 == actual.x2 && expected.y2 == actual.y2 && expected.neighborCellIndex == actual.neighborCellIndex && expected.isDiscarded == actual.isDiscarded && expected.joinsNextChord == actual.joinsNextChord,
            message);
}

RenderSourceFrame makeSingleCellDto(float radius, int contactChordCount) {
    RenderSourceFrame source;
    source.valid = true;
    source.sourceCellCount = 1;
    source.visibleCellCount = 1;
    RenderSourceCell cell;
    cell.runtimeId = 1;
    cell.sourceCellIndex = 0;
    cell.cellType = CellType::PHAGOCYTE;
    cell.positionX = 0.25f;
    cell.positionY = -0.5f;
    cell.radius = radius;
    cell.angle = 0.3f;
    cell.red = 0.1f;
    cell.green = 0.2f;
    cell.blue = 0.3f;
    cell.baseAux = 0.4f;
    cell.baseAux4 = 0.5f;
    cell.hasContactChordPayload = true;
    cell.contactChordCount = static_cast<std::uint8_t>(contactChordCount);
    source.cells.push_back(cell);
    source
            .orderedCellRecordIndices[static_cast<std::size_t>(
                    CellTypes::ordinal(CellType::PHAGOCYTE))]
            .push_back(0);
    return source;
}

void testRenderContactChordResolverCachesNormalizedDtoViews() {
    RenderSourceFrame source = makeSingleCellDto(0.02f, 2);
    RenderContactChord first;
    first.x1 = 0.01;
    first.y1 = -0.01732050807568877;
    first.x2 = 0.01;
    first.y2 = 0.01732050807568877;
    first.neighborCellIndex = 7;
    RenderContactChord second;
    second.x1 = -0.01;
    second.y1 = 0.01732050807568877;
    second.x2 = -0.01;
    second.y2 = -0.01732050807568877;
    second.neighborCellIndex = 8;
    source.activeContactChords = {first, second};
    source.activeContactChordNeighborRuntimeIds = {7, 8};

    std::array<RenderContactChord, 2> expected{first, second};
    cellularity::render::clipAndOrderRenderContactChords(expected.data(), 2);

    RenderContactChordResolver resolver;
    resolver.reset(1, 1, 1, CellDataConstants::MAX_NEIGHBORS);
    resolver.beginFrame();
    RenderContactChordResolver::Profile profile;
    const auto cellView = resolver.resolve(
            source, 0, RenderContactChordResolver::Consumer::CellGeometry, &profile);
    const auto linkView = resolver.resolve(
            source, 0, RenderContactChordResolver::Consumer::LinkLookup, &profile);

    assertTrue(cellView.count == 2 && linkView.count == 2 && cellView.data == linkView.data && resolver.normalizedContactChordCount() == 2,
            "RenderContactChordResolver reuses one normalized DTO view");
    assertChordExact(
            expected[0], cellView.data[0],
            "RenderContactChordResolver preserves first normalized chord");
    assertChordExact(
            expected[1], cellView.data[1],
            "RenderContactChordResolver preserves second normalized chord");
#ifdef CELLULARITY_RENDER_PROFILE
    assertTrue(profile.resolveRequests == 2 && profile.cacheMisses == 1 && profile.cacheHits == 1 && profile.uniqueNormalizedCells == 1,
            "RenderContactChordResolver profiles one miss and one shared hit");
#endif

    source.activeContactChords[0].neighborCellIndex = 9;
    resolver.beginFrame();
    const auto nextFrameView = resolver.resolve(
            source, 0, RenderContactChordResolver::Consumer::CellGeometry, nullptr);
    assertTrue(
            nextFrameView.data[0].neighborCellIndex == 9 || nextFrameView.data[1].neighborCellIndex == 9,
            "RenderContactChordResolver invalidates cache by frame generation");
}

void testRenderContactChordResolverReusesRecordSlotsAcrossGenerations() {
    RenderContactChordResolver resolver;
    resolver.reset(1, 1, 1, CellDataConstants::MAX_NEIGHBORS);
    constexpr std::array<int, 5> chordCounts{2, 0, 1, 3, 2};
    for (std::size_t generation = 0; generation < chordCounts.size(); ++generation) {
        const int count = chordCounts[generation];
        RenderSourceFrame source = makeSingleCellDto(0.02f, count);
        source.cells[0].runtimeId = generation + 10U;
        source.cells[0].sourceCellIndex = static_cast<int>(generation) + 10;
        for (int index = 0; index < count; ++index) {
            const double angle = 2.0 * std::acos(-1.0) * index / count + 0.1 * generation;
            RenderContactChord chord;
            chord.x1 = 0.02 * std::cos(angle - 0.4);
            chord.y1 = 0.02 * std::sin(angle - 0.4);
            chord.x2 = 0.02 * std::cos(angle + 0.4);
            chord.y2 = 0.02 * std::sin(angle + 0.4);
            chord.neighborCellIndex = 100 + static_cast<int>(generation) * 10 + index;
            source.activeContactChords.push_back(chord);
            source.activeContactChordNeighborRuntimeIds.push_back(chord.neighborCellIndex + 1U);
        }
        const auto rawBytes = objectBytes(source.activeContactChords);
        auto expected = source.activeContactChords;
        cellularity::render::clipAndOrderRenderContactChords(expected.data(), count);

        resolver.beginFrame();
        assertTrue(resolver.normalizedContactChordCount() == 0,
                "Each resolver generation starts with an empty active cache");
        RenderContactChordResolver::Profile profile;
        cellularity::render::RenderContactChordNormalizationProfile normalization;
        const auto linkView = resolver.resolve(source, 0, RenderContactChordResolver::Consumer::LinkLookup, &profile, &normalization);
        const auto cellView = resolver.resolve(source, 0, RenderContactChordResolver::Consumer::CellGeometry, &profile, &normalization);
        assertTrue(linkView.count == count && cellView.count == count && linkView.data == cellView.data,
                "A reused source record resolves only the current generation payload");
        for (int index = 0; index < count; ++index) {
            assertChordExact(expected[index], cellView.data[index],
                    "A reused cache record exposes the replacement Cell's normalized chords");
        }
        assertTrue(rawBytes == objectBytes(source.activeContactChords),
                "Resolving a reused record leaves every raw source byte unchanged");
        assertTrue(resolver.normalizedContactChordCount() == (count > 1 ? static_cast<std::size_t>(count) : 0U),
                "Direct views do not retain the previous generation's normalized range");
        if (count <= 1) {
            assertTrue(cellView.data == (count == 0 ? nullptr : source.activeContactChords.data()),
                    "Zero- and one-chord records are returned without copying source data");
        } else {
            assertTrue(cellView.data != source.activeContactChords.data(),
                    "Normalization writes owned cache storage, not the immutable source");
        }
#ifdef CELLULARITY_RENDER_PROFILE
        assertTrue(profile.resolveRequests == 2 && profile.directSourceViews == (count <= 1 ? 2 : 0)
                        && profile.cacheMisses == (count > 1 ? 1 : 0) && profile.cacheHits == (count > 1 ? 1 : 0)
                        && profile.linkOnlyCacheMisses == (count > 1 ? 1 : 0)
                        && normalization.calls == (count > 1 ? 1 : 0),
                "Generation reuse profiles one normalization or two direct views, never stale cache hits");
#endif
    }
}

void testCellBoundaryMeshBuilderBuildsDtoPerimeterAndFan() {
    RenderContactChordResolver resolver;
    resolver.reset(1, 1, 1, CellDataConstants::MAX_NEIGHBORS);
    CellBoundaryMeshBuilder meshBuilder;
    meshBuilder.reset(32, 96);

    RenderSourceFrame circleSource = makeSingleCellDto(0.02f, 0);
    PreparedRenderFrame circleFrame;
    resolver.beginFrame();
    meshBuilder.build(circleSource, circleFrame, resolver, nullptr, nullptr);
    const std::vector<float> expectedCircle{0.02f, 0.02f, 0.02f, -0.02f,
            -0.02f, -0.02f, -0.02f, 0.02f};
    const std::vector<unsigned short> expectedIndices{0, 1, 2, 0, 2, 3};
    assertTrue(circleFrame.cellDrawVertexCount == 4 && circleFrame.cellDrawIndexCount == 6 && circleFrame.cellCenterFloatCount == 8,
            "CellBoundaryMeshBuilder emits the DTO circle perimeter");
    assertPrefixExact(expectedCircle, circleFrame.cellTriangleCoords,
            expectedCircle.size(),
            "CellBoundaryMeshBuilder preserves circle vertex order");
    assertPrefixExact(expectedIndices, circleFrame.cellIndices,
            expectedIndices.size(),
            "CellBoundaryMeshBuilder preserves circle index order");

    RenderSourceFrame fanSource = makeSingleCellDto(0.02f, 1);
    RenderContactChord chord;
    chord.x1 = 0.01;
    chord.y1 = -0.01732050807568877;
    chord.x2 = 0.01;
    chord.y2 = 0.01732050807568877;
    chord.neighborCellIndex = 1;
    fanSource.activeContactChords.push_back(chord);
    fanSource.activeContactChordNeighborRuntimeIds.push_back(2);
    PreparedRenderFrame fanFrame;
    resolver.beginFrame();
    meshBuilder.build(fanSource, fanFrame, resolver, nullptr, nullptr);
    assertTrue(
            fanFrame.cellDrawVertexCount == 6 && fanFrame.cellDrawIndexCount == 15 && fanFrame.cellTriangleCoords[0] == 0.0f && fanFrame.cellTriangleCoords[1] == 0.0f && fanFrame.cellTriangleCoords[2] == static_cast<float>(chord.x1) && fanFrame.cellTriangleCoords[3] == static_cast<float>(chord.y1),
            "CellBoundaryMeshBuilder emits the normalized DTO fan");
}

void assertGeometryExact(const PreparedRenderFrame& expected,
        const PreparedRenderFrame& actual) {
    assertTrue(expected.geometryReady && actual.geometryReady,
            "RenderFrameBuilder completes both geometry slots");
    assertTrue(
            !expected.valid && !actual.valid,
            "RenderFrameBuilder leaves presentation publication to its caller");
    assertTrue(expected.cellVertexCountBefore == actual.cellVertexCountBefore && expected.cellVertexCount == actual.cellVertexCount && expected.cellIndexCountBefore == actual.cellIndexCountBefore && expected.cellIndexCount == actual.cellIndexCount && expected.bunchStart == actual.bunchStart && expected.bunchSize == actual.bunchSize && expected.bunchIndexStart == actual.bunchIndexStart && expected.bunchIndexSize == actual.bunchIndexSize && expected.bunches == actual.bunches,
            "RenderFrameBuilder reproduces exact bucket and bunch metadata");
    assertTrue(expected.cellDrawVertexCount == actual.cellDrawVertexCount && expected.cellDrawIndexCount == actual.cellDrawIndexCount && expected.cellCenterFloatCount == actual.cellCenterFloatCount && expected.foodInstanceCount == actual.foodInstanceCount && expected.deadCellInstanceCount == actual.deadCellInstanceCount && expected.drawLinksCount == actual.drawLinksCount,
            "RenderFrameBuilder reproduces exact active ranges");

    assertPrefixExact(expected.cellTriangleCoords, actual.cellTriangleCoords,
            static_cast<std::size_t>(expected.cellDrawVertexCount) * 2U,
            "RenderFrameBuilder reproduces Cell positions");
    assertPrefixExact(expected.cellTextureCoords, actual.cellTextureCoords,
            static_cast<std::size_t>(expected.cellDrawVertexCount) * 4U,
            "RenderFrameBuilder reproduces Cell texture attributes");
    assertPrefixExact(expected.cellCenterCoords, actual.cellCenterCoords,
            static_cast<std::size_t>(expected.cellCenterFloatCount),
            "RenderFrameBuilder reproduces Cell centers");
    assertPrefixExact(expected.cellColors, actual.cellColors,
            static_cast<std::size_t>(expected.cellDrawVertexCount) * 4U,
            "RenderFrameBuilder reproduces Cell colors");
    assertPrefixExact(expected.cellIndices, actual.cellIndices,
            static_cast<std::size_t>(expected.cellDrawIndexCount),
            "RenderFrameBuilder reproduces Cell indices");
    assertPrefixExact(expected.foodInstanceAttributes,
            actual.foodInstanceAttributes,
            static_cast<std::size_t>(expected.foodInstanceCount) * 4U,
            "RenderFrameBuilder reproduces Food instances");
    assertPrefixExact(expected.foodVertexAttributes, actual.foodVertexAttributes,
            static_cast<std::size_t>(expected.foodInstanceCount) * 4U,
            "RenderFrameBuilder reproduces Food attributes");
    assertPrefixExact(
            expected.deadCellInstanceAttributes, actual.deadCellInstanceAttributes,
            static_cast<std::size_t>(expected.deadCellInstanceCount) * 4U,
            "RenderFrameBuilder reproduces dead-splash instances");
    assertPrefixExact(
            expected.deadCellVertexAttributes, actual.deadCellVertexAttributes,
            static_cast<std::size_t>(expected.deadCellInstanceCount) * 4U,
            "RenderFrameBuilder reproduces dead-splash attributes");
    assertPrefixExact(expected.linkTriangleCoords, actual.linkTriangleCoords,
            static_cast<std::size_t>(expected.drawLinksCount) * 4U,
            "RenderFrameBuilder reproduces Link positions");
    assertPrefixExact(expected.linkTextureCoords, actual.linkTextureCoords,
            static_cast<std::size_t>(expected.drawLinksCount),
            "RenderFrameBuilder reproduces Link attributes");
}

void testRenderFrameBuilderWritesIndependentPassedSlotsWithoutGles() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    environment.mLightAngle = 0.25;
    environment.mLightRange = 0.5;
    environment.mLightIntensity = 0.75;
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;

    const int left = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int right = addAliveCell(cellData, CellType::NEUROCYTE);
    cellData.mPositionX[left] = 0.0;
    cellData.mPositionY[left] = 0.0;
    cellData.mRadius[left] = 0.02;
    cellData.mPositionX[right] = 0.03;
    cellData.mPositionY[right] = 0.0;
    cellData.mRadius[right] = 0.02;
    addPairLink(cellData, left, right, 0.0, std::acos(-1.0), 0.2f, 0.01f);
    cellData.mTopology.setEdgeRenderOffsets(0, -0.002f, 0.0f, 0.003f, 0.0f);
    constexpr double contactDistance = 0.03;
    assertTrue(cellData.addKnownCellCollisionContactFast(
                       left, right, -contactDistance, 0.0,
                       contactDistance * contactDistance),
            "RenderFrameBuilder fixture stores its source contact");
    assertTrue(cellData.addKnownCellCollisionContactFast(
                       right, left, contactDistance, 0.0,
                       contactDistance * contactDistance),
            "RenderFrameBuilder fixture stores its reciprocal contact");

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

    PreparedRenderFrame firstFrame;
    RenderSourceFrame sourceFrame;
    RenderSourceCapture sourceCapture(environment.maxCellCount, sourceFrame);
    RenderFrameBuilder builder(environment.maxCellCount, firstFrame);
    assertTrue(firstFrame.geometryReady && !firstFrame.valid,
            "RenderFrameBuilder initializes a complete empty geometry slot");

    const RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 17;
    metadata.modeGeneration = 13;
    metadata.completedUpdateId = 29;
    metadata.stage = RenderCaptureStage::AfterCompletedUpdate;
    metadata.environmentRevision = 3;
    metadata.viewport.revision = 11;
    metadata.viewport.coverageRevision = 11;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    metadata.resolvedInput.lastResolvedInputSequence = 37;
    metadata.resolvedInput.appliedWorldRevision = 5;
    metadata.timeline.captureRequestedNanos = 101;

    sourceCapture.capture(world, view, metadata, sourceFrame);
    const int firstVisible = builder.build(sourceFrame, firstFrame);
    assertTrue(firstVisible == 2 && sourceFrame.visibleCellCount == 2,
            "RenderFrameBuilder returns its source-frame visible count");
    assertTrue(firstFrame.foodInstanceCount == 1 && firstFrame.deadCellInstanceCount == 1 && firstFrame.drawLinksCount == 6,
            "RenderFrameBuilder fills every instance family");
    assertTrue(firstFrame.identity.worldEpoch == 17 && firstFrame.identity.modeGeneration == 13 && firstFrame.identity.completedUpdateId == 29 && firstFrame.identity.captureStage == RenderCaptureStage::AfterCompletedUpdate,
            "RenderFrameBuilder fills capture identity");
    assertTrue(firstFrame.environment.revision == 3 && firstFrame.viewport.coverageRevision == 11 && firstFrame.resolvedInput.lastResolvedInputSequence == 37 && firstFrame.resolvedInput.appliedWorldRevision == 5 && firstFrame.timeline.captureRequestedNanos == 101,
            "RenderFrameBuilder fills environment, coverage, input, and "
            "timeline metadata");
#ifdef CELLULARITY_RENDER_TIMELINE
    assertTrue(firstFrame.timeline.sourceCapturedNanos > 0 && firstFrame.timeline.framePreparedNanos >= firstFrame.timeline.sourceCapturedNanos,
            "Opt-in timeline stamps source and prepared-frame completion");
#else
    assertTrue(firstFrame.timeline.sourceCapturedNanos == 0 && firstFrame.timeline.framePreparedNanos == 0,
            "Ordinary builder performs no timeline clock reads");
#endif

    const std::uint64_t firstSequence = firstFrame.identity.captureSequence;
    const float firstPosition = firstFrame.cellTriangleCoords.front();
    PreparedRenderFrame secondFrame;
    const int secondVisible = builder.build(sourceFrame, secondFrame);
    assertTrue(
            secondVisible == firstVisible,
            "RenderFrameBuilder can write a different uninitialized frame slot");
    assertTrue(secondFrame.identity.captureSequence == firstSequence + 1U,
            "RenderFrameBuilder assigns a distinct sequence to every slot");
    assertTrue(firstFrame.identity.captureSequence == firstSequence && firstFrame.cellTriangleCoords.front() == firstPosition,
            "Building another slot does not mutate the previous frame");
    assertGeometryExact(firstFrame, secondFrame);

    PreparedRenderFrame profiledFrame;
    const RenderFrameBuilder::Profile profile =
            builder.profileBuild(sourceFrame, profiledFrame);
    assertTrue(
            profile.visibleCellCount == firstVisible && profile.cellDrawVertexCount == profiledFrame.cellDrawVertexCount && profile.cellDrawIndexCount == profiledFrame.cellDrawIndexCount && profile.foodInstanceCount == profiledFrame.foodInstanceCount && profile.deadCellInstanceCount == profiledFrame.deadCellInstanceCount && profile.drawLinksCount == profiledFrame.drawLinksCount,
            "RenderFrameBuilder profile describes the passed output slot");
    assertGeometryExact(firstFrame, profiledFrame);
}

RenderSourceFrame makeSharedResolverSource(double chordX, bool preview, bool reordered) {
    RenderSourceFrame source = makeSingleCellDto(0.02f, 2);
    source.environment.previewMode = preview;
    source.sourceCellCount = 4;
    source.visibleCellCount = 2;
    source.sourceTopologyEdgeCount = 2;
    source.sourceBoundaryLinkCount = 1;
    source.cells[0].positionX = 0.0f;
    source.cells[0].positionY = 0.0f;
    source.cells[0].previewArrow = preview;
    for (int index = 1; index < 4; ++index) {
        RenderSourceCell cell = source.cells[0];
        cell.runtimeId = index + 1U;
        cell.sourceCellIndex = index;
        cell.positionX = index == 1 ? 0.5f : index == 2 ? 0.03f
                                                        : 0.47f;
        cell.contactChordOffset = index == 1 ? 2 : index == 2 ? 4
                                                              : 5;
        cell.contactChordCount = index == 1 ? 2 : index == 2 ? 1
                                                             : 0;
        cell.previewArrow = false;
        if (index == 1) {
            cell.cellType = CellType::GLUEOCYTE;
        }
        source.cells.push_back(cell);
    }
    source.orderedCellRecordIndices[CellTypes::ordinal(CellType::PHAGOCYTE)].push_back(2);
    if (preview) {
        source.orderedCellRecordIndices[RenderFrameBuilder::PREVIEW_EXTRA_CELL_TYPE_INDEX].push_back(0);
    }

    const double chordY = std::sqrt(0.02 * 0.02 - chordX * chordX);
    auto chord = [&](bool right, int neighbor) {
        RenderContactChord result;
        result.x1 = result.x2 = right ? chordX : -chordX;
        result.y1 = right ? -chordY : chordY;
        result.y2 = -result.y1;
        result.neighborCellIndex = neighbor;
        return result;
    };
    source.activeContactChords = {chord(true, 2), chord(false, 3),
            chord(true, CellDataConstants::BOUNDARY_CONNECTION_INDEX), chord(false, 3), chord(false, 0)};
    source.activeContactChordNeighborRuntimeIds = {3, 4, kBoundaryCellRuntimeId, 4, 1};

    RenderSourceLink normal;
    normal.runtimeId = 11;
    normal.cellARuntimeId = 1;
    normal.cellBRuntimeId = 3;
    normal.sourceLinkIndex = 0;
    normal.sourceCellARecordIndex = 0;
    normal.sourceCellBIndex = 2;
    normal.cellBPositionX = 0.03f;
    normal.renderStartOffsetX = 0.009f;
    normal.renderEndOffsetX = -0.009f;
    source.candidateLinks.push_back(normal);

    RenderSourceLink hidden = normal;
    hidden.runtimeId = 12;
    hidden.cellARuntimeId = 2;
    hidden.sourceLinkIndex = 1;
    hidden.sourceCellARecordIndex = 1;
    hidden.cellAPositionX = 0.5f;
    source.candidateLinks.push_back(hidden);

    RenderSourceLink boundary = hidden;
    boundary.runtimeId = kInvalidTopologyEdgeRuntimeId;
    boundary.cellBRuntimeId = kBoundaryCellRuntimeId;
    boundary.sourceLinkIndex = 0;
    boundary.sourceCellBIndex = CellDataConstants::BOUNDARY_CONNECTION_INDEX;
    boundary.cellBPositionX = 0.9f;
    boundary.renderEndOffsetX = 0.0f;
    source.candidateLinks.push_back(boundary);

    if (reordered) {
        std::swap(source.cells[0], source.cells[1]);
        for (auto& bucket : source.orderedCellRecordIndices) {
            for (int& recordIndex : bucket) {
                if (recordIndex <= 1) {
                    recordIndex = 1 - recordIndex;
                }
            }
        }
        for (auto& cell : source.cells) {
            cell.runtimeId += 100;
        }
        for (auto& runtimeId : source.activeContactChordNeighborRuntimeIds) {
            if (runtimeId != kBoundaryCellRuntimeId) {
                runtimeId += 100;
            }
        }
        for (auto& link : source.candidateLinks) {
            link.sourceCellARecordIndex = 1 - link.sourceCellARecordIndex;
            link.cellARuntimeId += 100;
            if (!link.isBoundary()) {
                link.cellBRuntimeId += 100;
            }
        }
    }
    return source;
}

void testRenderFrameBuilderPreservesImmutableGeometryAcrossReusedSlots() {
    for (const bool preview : {false, true}) {
        const RenderSourceFrame source = makeSharedResolverSource(0.01, preview, false);
        const RenderSourceFrame replacement = makeSharedResolverSource(0.012, preview, true);
        RenderSourceFrame empty;
        empty.valid = true;
        const auto cellBytes = objectBytes(source.cells);
        const auto chordBytes = objectBytes(source.activeContactChords);
        const auto linkBytes = objectBytes(source.candidateLinks);
        const auto runtimeIds = source.activeContactChordNeighborRuntimeIds;
        const auto buckets = source.orderedCellRecordIndices;
        const auto* rawChordData = source.activeContactChords.data();
        const auto replacementChordBytes = objectBytes(replacement.activeContactChords);

        PreparedRenderFrame expected;
        PreparedRenderFrame expectedReplacement;
        RenderFrameBuilder referenceBuilder(4, expected);
        RenderFrameBuilder replacementReferenceBuilder(4, expectedReplacement);
        referenceBuilder.build(source, expected);
        replacementReferenceBuilder.build(replacement, expectedReplacement);
        assertTrue(expected.drawLinksCount == 18 && expected.diagnostics.normalizedContactChordCount == 4,
                "The shared-resolver fixture exercises contact, fallback and boundary Links with two normalized owners");
        assertTrue(expected.linkTriangleCoords[4] != expectedReplacement.linkTriangleCoords[4],
                "The replacement snapshot has different geometry, not just different cache identities");

        PreparedRenderFrame firstSlot;
        PreparedRenderFrame secondSlot;
        RenderFrameBuilder builder(4, firstSlot);
        for (int reuse = 0; reuse < 3; ++reuse) {
            builder.build(source, firstSlot);
            assertGeometryExact(expected, firstSlot);
            builder.build(replacement, secondSlot);
            assertGeometryExact(expectedReplacement, secondSlot);
            assertGeometryExact(expected, firstSlot);
            const auto profile = builder.profileBuild(source, secondSlot);
            assertGeometryExact(expected, secondSlot);
            assertGeometryExact(expected, firstSlot);
#ifdef CELLULARITY_RENDER_PROFILE
            assertTrue(profile.contactChordResolveRequests == (preview ? 6 : 5)
                            && profile.contactChordDirectSourceViews == 1 && profile.contactChordCacheMisses == 2
                            && profile.contactChordCacheHits == (preview ? 3 : 2) && profile.linkOnlyCacheMisses == 1,
                    "Cell, preview-arrow and Link passes share one frame-local resolver cache");
            assertTrue(profile.normalizerCalls == 2 && profile.cellPassNormalizerCalls == 1 && profile.linkPassNormalizerCalls == 1
                            && profile.normalizedContactChordCount == 4 && profile.normalizerContactChordCountSum == 4
                            && profile.cellPassNormalizerContactChordCountSum == 2 && profile.linkPassNormalizerContactChordCountSum == 2,
                    "The visible owner is normalized once before Links and the hidden owner only once in the Link pass");
            assertTrue(profile.contactChordResolutionNanos == profile.cellPassContactChordResolutionNanos + profile.linkPassContactChordResolutionNanos
                            && profile.contactChordNormalizationNanos == profile.cellPassContactChordNormalizationNanos + profile.linkPassContactChordNormalizationNanos,
                    "Resolver and normalizer timings account for both geometry passes exactly");
            assertTrue(profile.linkContactCrossSectionCount == 2 && profile.linkNormalFallbackCount == 1 && profile.linkBoundaryFallbackCount == 0,
                    "Shared-cache lookup preserves normal and boundary contact bases and stretched fallback");
#else
            assertTrue(profile.contactChordResolveRequests == 0 && profile.normalizerCalls == 0,
                    "The non-profile build leaves optional resolver instrumentation disabled");
#endif
            builder.build(empty, firstSlot);
            assertTrue(firstSlot.cellDrawVertexCount == 0 && firstSlot.drawLinksCount == 0 && firstSlot.diagnostics.normalizedContactChordCount == 0,
                    "An empty snapshot invalidates active geometry and normalized cache ranges");
            builder.build(replacement, firstSlot);
            assertGeometryExact(expectedReplacement, firstSlot);
            builder.profileBuild(source, firstSlot);
            assertGeometryExact(expected, firstSlot);
            assertTrue(cellBytes == objectBytes(source.cells) && chordBytes == objectBytes(source.activeContactChords)
                            && linkBytes == objectBytes(source.candidateLinks) && runtimeIds == source.activeContactChordNeighborRuntimeIds
                            && buckets == source.orderedCellRecordIndices && rawChordData == source.activeContactChords.data()
                            && replacementChordBytes == objectBytes(replacement.activeContactChords),
                    "Ordinary and profiled builds leave immutable source records, raw chords, identities and ordering unchanged");
        }
    }
}

void testRingFramesCarryBothWallsAndFrozenEdits() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mInnerRadius = 0.5;
    SimulationWorld world(false, environment);
    const int inner = addAliveCell(world.mCellData, CellType::GLUEOCYTE);
    const int outer = addAliveCell(world.mCellData, CellType::GLUEOCYTE);
    world.mCellData.mPositionX[inner] = 0.505;
    world.mCellData.mPositionX[outer] = 0.995;
    world.mCellData.handleAllCellToBoundaryCollisionsPartial(world.mEnvironment);
    world.updateGrids();
    RenderSourceFrame source;
    PreparedRenderFrame frame;
    RenderSourceCapture capture(environment.maxCellCount, source);
    RenderFrameBuilder builder(environment.maxCellCount, frame);
    const RenderViewBounds view{-1.1f, 1.1f, -1.1f, 1.1f};
    RenderFrameCaptureMetadata metadata;
    metadata.stage = RenderCaptureStage::StableWorld;
    capture.capture(world, view, metadata, source);
    builder.build(source, frame);
    assertTrue(frame.environment.dishRadius == 1.0 && frame.environment.dishInnerRadius == 0.5, "Both radii reach the prepared render frame");
    assertTrue(source.activeContactChords.size() == 2 && frame.drawLinksCount == 12, "Both wall chords and adhesion links reach render geometry");
    for (const auto& chord : source.activeContactChords) {
        assertTrue(chord.x1 * chord.y2 - chord.y1 * chord.x2 > 0.0, "Both ring walls preserve visible chord orientation");
    }
    // Move an inner-wall attachment to the outer wall without advancing physics.
    // Its old anchor must not borrow the new wall's contact cross-section.
    world.mCellData.mPositionX[inner] = -0.995;
    metadata.stage = RenderCaptureStage::StableWorld;
    capture.capture(world, view, metadata, source);
    builder.build(source, frame);
    assertTrue(source.activeContactChords.size() == 2 && frame.drawLinksCount == 12, "Frozen edits rebuild ring walls while retaining detached boundary-link geometry");
    for (int index = 0; index < frame.drawLinksCount * 4; ++index) {
        assertTrue(std::isfinite(frame.linkTriangleCoords[static_cast<std::size_t>(index)]), "Ring link coordinates stay finite after frozen editing");
    }
}

} // namespace

int main() {
    testRenderContactChordResolverCachesNormalizedDtoViews();
    testRenderContactChordResolverReusesRecordSlotsAcrossGenerations();
    testCellBoundaryMeshBuilderBuildsDtoPerimeterAndFan();
    testRenderFrameBuilderWritesIndependentPassedSlotsWithoutGles();
    testRenderFrameBuilderPreservesImmutableGeometryAcrossReusedSlots();
    testRingFramesCarryBothWallsAndFrozenEdits();
    std::cout << "render frame builder CPU-only tests passed\n";
    return 0;
}
