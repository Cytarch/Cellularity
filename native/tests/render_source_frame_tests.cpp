#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/RendererMemoryModel.h"
#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderContactLimits.h"
#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/frame/RenderSourceFrame.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

namespace {

using cellularity::render::PreparedRenderFrame;
using cellularity::render::RenderCaptureStage;
using cellularity::render::RenderFrameBuilder;
using cellularity::render::RenderFrameCaptureMetadata;
using cellularity::render::RenderMarkerCaptureRequest;
using cellularity::render::RenderSourceCapture;
using cellularity::render::RenderSourceCell;
using cellularity::render::RenderSourceFrame;
using cellularity::render::RenderSourceLink;
using cellularity::render::RenderViewBounds;
using cellularity::tests::addAliveCell;
using cellularity::tests::assertTrue;

static_assert(!std::is_copy_constructible_v<RenderSourceFrame>);
static_assert(!std::is_copy_assignable_v<RenderSourceFrame>);
static_assert(std::is_nothrow_move_constructible_v<RenderSourceFrame>);
static_assert(std::is_nothrow_move_assignable_v<RenderSourceFrame>);
static_assert(!std::is_copy_constructible_v<RenderSourceCapture>);
static_assert(!std::is_move_constructible_v<RenderSourceCapture>);
static_assert(std::is_standard_layout_v<RenderSourceLink>);
static_assert(std::is_trivially_copyable_v<RenderSourceLink>);
static_assert(sizeof(RenderSourceLink) == cellularity::memory::kRenderSourceLinkBudgetBytes);

template <typename T>
bool activePrefixHasExactBytes(const std::vector<T>& left,
        const std::vector<T>& right, std::size_t count) {
    return left.size() >= count && right.size() >= count && (count == 0 || std::memcmp(left.data(), right.data(), count * sizeof(T)) == 0);
}

void assertPreparedGeometryExact(const PreparedRenderFrame& expected,
        const PreparedRenderFrame& actual) {
    assertTrue(expected.cellVertexCountBefore == actual.cellVertexCountBefore && expected.cellVertexCount == actual.cellVertexCount && expected.cellIndexCountBefore == actual.cellIndexCountBefore && expected.cellIndexCount == actual.cellIndexCount && expected.bunchStart == actual.bunchStart && expected.bunchSize == actual.bunchSize && expected.bunchIndexStart == actual.bunchIndexStart && expected.bunchIndexSize == actual.bunchIndexSize && expected.bunches == actual.bunches,
            "Owned source rebuild preserves bucket and bunch metadata");
    assertTrue(expected.cellDrawVertexCount == actual.cellDrawVertexCount && expected.cellDrawIndexCount == actual.cellDrawIndexCount && expected.cellCenterFloatCount == actual.cellCenterFloatCount && expected.foodInstanceCount == actual.foodInstanceCount && expected.deadCellInstanceCount == actual.deadCellInstanceCount && expected.drawLinksCount == actual.drawLinksCount,
            "Owned source rebuild preserves every active output range");
    assertTrue(
            activePrefixHasExactBytes(
                    expected.cellTriangleCoords, actual.cellTriangleCoords,
                    static_cast<std::size_t>(expected.cellDrawVertexCount) * 2U)
                    && activePrefixHasExactBytes(
                            expected.cellTextureCoords, actual.cellTextureCoords,
                            static_cast<std::size_t>(expected.cellDrawVertexCount) * 4U)
                    && activePrefixHasExactBytes(
                            expected.cellCenterCoords, actual.cellCenterCoords,
                            static_cast<std::size_t>(expected.cellCenterFloatCount))
                    && activePrefixHasExactBytes(
                            expected.cellColors, actual.cellColors,
                            static_cast<std::size_t>(expected.cellDrawVertexCount) * 4U)
                    && activePrefixHasExactBytes(
                            expected.cellIndices, actual.cellIndices,
                            static_cast<std::size_t>(expected.cellDrawIndexCount))
                    && activePrefixHasExactBytes(
                            expected.foodInstanceAttributes, actual.foodInstanceAttributes,
                            static_cast<std::size_t>(expected.foodInstanceCount) * 4U)
                    && activePrefixHasExactBytes(
                            expected.foodVertexAttributes, actual.foodVertexAttributes,
                            static_cast<std::size_t>(expected.foodInstanceCount) * 4U)
                    && activePrefixHasExactBytes(
                            expected.deadCellInstanceAttributes,
                            actual.deadCellInstanceAttributes,
                            static_cast<std::size_t>(expected.deadCellInstanceCount) * 4U)
                    && activePrefixHasExactBytes(
                            expected.deadCellVertexAttributes,
                            actual.deadCellVertexAttributes,
                            static_cast<std::size_t>(expected.deadCellInstanceCount) * 4U)
                    && activePrefixHasExactBytes(
                            expected.linkTriangleCoords, actual.linkTriangleCoords,
                            static_cast<std::size_t>(expected.drawLinksCount) * 4U)
                    && activePrefixHasExactBytes(
                            expected.linkTextureCoords, actual.linkTextureCoords,
                            static_cast<std::size_t>(expected.drawLinksCount)),
            "Owned source rebuild preserves exact active geometry bytes");
}

int addRenderEdge(CellData& cells, int sourceCell, int targetCell,
        float offsetMarker) {
    const int result =
            cells.addLink(
                    sourceCell,
                    targetCell,
                    CellLinkCreationReason::UNSPECIFIED,
                    0.0,
                    Environment::PI,
                    0.2f,
                    0.04f);
    assertTrue(result >= 0, "Render-source fixture adds topology edge");
    const int edgeIndex = cells.mTopology.edgeCount() - 1;
    cells.mTopology.setEdgeRenderOffsets(edgeIndex, offsetMarker, 0.0f,
            -offsetMarker, 0.0f);
    return edgeIndex;
}

int recordIndexForCell(const RenderSourceFrame& source, int sourceCellIndex) {
    const auto found =
            std::find_if(source.cells.begin(), source.cells.end(),
                    [&](const RenderSourceCell& cell) {
                        return cell.sourceCellIndex == sourceCellIndex;
                    });
    if (found == source.cells.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(source.cells.begin(), found));
}

void testLinkDtoUsesRuntimeBoundarySentinel() {
    constexpr RenderSourceLink empty;
    static_assert(!empty.isBoundary());
    static_assert(noexcept(empty.isBoundary()));
    assertTrue(empty.runtimeId == kInvalidTopologyEdgeRuntimeId
                    && empty.cellARuntimeId == kInvalidCellRuntimeId
                    && empty.cellBRuntimeId == kInvalidCellRuntimeId
                    && empty.sourceLinkIndex == -1 && empty.sourceCellBIndex == -1
                    && empty.sourceCellARecordIndex == -1
                    && empty.fallbackCenterRatio == 0.5f,
            "Link DTO defaults distinguish an empty record from a boundary Link");

    RenderSourceLink normal;
    normal.runtimeId = 41;
    normal.cellARuntimeId = 3;
    normal.cellBRuntimeId = 4;
    normal.sourceLinkIndex = 7;
    normal.sourceCellBIndex = 0;
    normal.sourceCellARecordIndex = 6;
    normal.fallbackCenterRatio = 4.25f;
    const RenderSourceLink normalCopy = normal;
    assertTrue(!normalCopy.isBoundary() && normalCopy.runtimeId == 41
                    && normalCopy.sourceLinkIndex == 7 && normalCopy.sourceCellBIndex == 0
                    && normalCopy.sourceCellARecordIndex == 6
                    && normalCopy.fallbackCenterRatio == 4.25f,
            "Normal Link DTO copies frame-local lookup and an unclamped ratio");

    RenderSourceLink boundary;
    boundary.cellARuntimeId = 3;
    boundary.cellBRuntimeId = kBoundaryCellRuntimeId;
    boundary.sourceLinkIndex = 2;
    boundary.sourceCellBIndex = CellData::BOUNDARY_CONNECTION_INDEX;
    boundary.sourceCellARecordIndex = 6;
    boundary.cellBPositionX = 0.75f;
    boundary.cellBPositionY = 0.5f;
    const RenderSourceLink boundaryCopy = boundary;
    assertTrue(boundaryCopy.isBoundary() && boundaryCopy.runtimeId == kInvalidTopologyEdgeRuntimeId
                    && boundaryCopy.sourceLinkIndex == 2
                    && boundaryCopy.sourceCellBIndex == CellData::BOUNDARY_CONNECTION_INDEX
                    && boundaryCopy.sourceCellARecordIndex == 6
                    && boundaryCopy.cellBPositionX == 0.75f && boundaryCopy.cellBPositionY == 0.5f
                    && boundaryCopy.fallbackCenterRatio == 0.5f
                    && boundaryCopy.renderEndOffsetX == 0.0f && boundaryCopy.renderEndOffsetY == 0.0f,
            "Boundary Link DTO uses a runtime sentinel and preserves its saved anchor");
}

void testLinkDtoStorageAccountsMixedRecords() {
    RenderSourceFrame source;
    const std::uint64_t inlineBytes = sizeof(RenderSourceFrame);
    assertTrue(source.activeStorageBytes() == inlineBytes && source.retainedStorageBytes() == inlineBytes,
            "Empty source storage counts only inline frame state");
    source.candidateLinks.reserve(7);
    const std::uint64_t retained = inlineBytes
            + source.candidateLinks.capacity() * sizeof(RenderSourceLink);
    assertTrue(source.activeStorageBytes() == inlineBytes && source.retainedStorageBytes() == retained,
            "Reserved Link storage counts capacity without inventing active records");
    RenderSourceLink normal;
    normal.runtimeId = 41;
    normal.cellARuntimeId = 3;
    normal.cellBRuntimeId = 4;
    RenderSourceLink boundary;
    boundary.cellARuntimeId = 3;
    boundary.cellBRuntimeId = kBoundaryCellRuntimeId;
    source.candidateLinks.push_back(normal);
    source.candidateLinks.push_back(boundary);
    assertTrue(source.activeStorageBytes() == inlineBytes + 144
                    && source.retainedStorageBytes() == retained,
            "One normal and one boundary Link share 144 active payload bytes");
    source.candidateLinks.clear();
    assertTrue(source.activeStorageBytes() == inlineBytes && source.retainedStorageBytes() == retained,
            "Clearing Link records releases their active range but keeps reusable storage");
}

void testCapturePopulatesLinkDtoLookupAndUnclampedRatio() {
    const Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cells = world.mCellData;
    const int target = addAliveCell(cells, CellType::GLUEOCYTE);
    const int owner = addAliveCell(cells, CellType::GLUEOCYTE);
    cells.mPositionX[target] = 0.002;
    cells.mPositionX[owner] = 0.0;
    cells.mPositionY[target] = cells.mPositionY[owner] = 0.0;
    cells.mRadius[target] = 0.015;
    cells.mRadius[owner] = 0.03;
    cells.refreshRadiusSq(target);
    cells.refreshRadiusSq(owner);
    const int edge = addRenderEdge(cells, owner, target, 0.003f);
    world.updateGrids();

    const RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    RenderFrameCaptureMetadata metadata;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    RenderSourceFrame source;
    RenderSourceCapture capture(environment.maxCellCount, source);
    capture.capture(world, view, metadata, source);
    assertTrue(source.candidateLinks.size() == 1, "DTO capture owns one normal Link");
    const RenderSourceLink& link = source.candidateLinks[0];
    const int ownerRecord = recordIndexForCell(source, owner);
    assertTrue(ownerRecord >= 0 && !link.isBoundary() && link.sourceLinkIndex == edge
                    && link.sourceCellBIndex == target && link.sourceCellARecordIndex == ownerRecord
                    && source.cells[static_cast<std::size_t>(ownerRecord)].hasContactChordPayload
                    && link.cellARuntimeId == cells.runtimeId(owner)
                    && link.cellBRuntimeId == cells.runtimeId(target)
                    && link.renderStartOffsetX == 0.003f && link.renderEndOffsetX == -0.003f,
            "DTO capture binds immutable contact payload, target index, identity and offsets");
    assertTrue(std::isfinite(link.fallbackCenterRatio), "Captured ratio is finite");
    cellularity::tests::assertNear(4.25, link.fallbackCenterRatio, 1e-7,
            "DTO capture computes the original double-precision ratio without clamp");

    cells.mPositionX[target] = 0.0;
    world.updateGrids();
    capture.capture(world, view, metadata, source);
    assertTrue(source.candidateLinks.size() == 1 && source.candidateLinks[0].fallbackCenterRatio == 0.5f,
            "Coincident centers keep a finite midpoint ratio for downstream axis handling");

    cells.mPositionX[target] = 1e-50;
    world.updateGrids();
    capture.capture(world, view, metadata, source);
    assertTrue(source.candidateLinks.size() == 1 && source.candidateLinks[0].fallbackCenterRatio == 0.5f,
            "An unrepresentable ratio keeps the finite undefined-axis default");

    cells.mIsDead[target] = true;
    world.updateGrids();
    capture.capture(world, view, metadata, source);
    assertTrue(source.candidateLinks.empty(), "DTO capture excludes a dead endpoint before publishing records");
}

void testNormalLinkCaptureUsesHigherIndexOwner() {
    std::array<float, 9> firstOrientation{};
    for (const bool reversed : {false, true}) {
        const Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
        SimulationWorld world(false, environment);
        CellData& cells = world.mCellData;
        const int target = addAliveCell(cells, CellType::NEUROCYTE);
        const int owner = addAliveCell(cells, CellType::GLUEOCYTE);
        cells.mPositionX[owner] = -0.012;
        cells.mPositionY[owner] = 0.008;
        cells.mPositionX[target] = -0.0108;
        cells.mPositionY[target] = 0.0096;
        cells.mRadius[owner] = 0.03;
        cells.mRadius[target] = 0.015;
        for (const int cell : {target, owner}) {
            cells.refreshRadiusSq(cell);
            cells.mMass[cell] = CellData::MASS_PER_AREA * cells.mRadiusSq[cell];
            cells.mVelocityX[cell] = 0.01 * (cell + 1);
            cells.mVelocityY[cell] = -0.02 * (cell + 1);
            cells.mForceX[cell] = 0.3 * (cell + 1);
            cells.mForceY[cell] = -0.4 * (cell + 1);
        }
        const int edge = addRenderEdge(cells, reversed ? target : owner, reversed ? owner : target, 0.0f);
        cells.mTopology.setEdgeRenderOffsets(edge,
                reversed ? -0.006f : 0.003f, reversed ? 0.009f : -0.005f,
                reversed ? 0.003f : -0.006f, reversed ? -0.005f : 0.009f);
        world.updateGrids();

        auto physicalState = [&]() {
            std::array<std::array<double, 10>, 2> result{};
            for (const int cell : {target, owner}) {
                result[cell] = {cells.mPositionX[cell], cells.mPositionY[cell],
                        cells.mVelocityX[cell], cells.mVelocityY[cell], cells.mForceX[cell], cells.mForceY[cell],
                        cells.mMass[cell], cells.mRadius[cell], cells.mAngle[cell], cells.mAge[cell]};
            }
            return result;
        };
        const auto physicalBefore = physicalState();
        const auto topologyRevision = cells.mTopology.structuralRevision();
        const auto edgeId = cells.mTopology.edgeRuntimeId(edge);
        const CellTopology::EdgeView topologyBefore = cells.mTopology.edgeView();
        const std::array<double, 4> linkPhysicsBefore{topologyBefore.angleA[edge], topologyBefore.angleB[edge],
                topologyBefore.torsionalStiffness[edge], topologyBefore.restLength[edge]};

        const RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
        RenderFrameCaptureMetadata metadata;
        metadata.viewport.visibleBounds = view;
        metadata.viewport.coverageBounds = view;
        RenderSourceFrame source;
        RenderSourceCapture capture(environment.maxCellCount, source);
        capture.capture(world, view, metadata, source);
        assertTrue(source.valid && source.candidateLinks.size() == 1,
                "Either topology endpoint order publishes one complete normal Link");
        const RenderSourceLink& link = source.candidateLinks[0];
        const int ownerRecord = recordIndexForCell(source, owner);
        assertTrue(ownerRecord >= 0 && link.sourceCellARecordIndex == ownerRecord
                        && source.cells[static_cast<std::size_t>(ownerRecord)].hasContactChordPayload
                        && link.sourceCellBIndex == target && link.sourceLinkIndex == edge
                        && link.runtimeId == edgeId && !link.isBoundary()
                        && link.cellARuntimeId == cells.runtimeId(owner) && link.cellBRuntimeId == cells.runtimeId(target),
                "Higher-index owner, contact lookup and edge identity do not depend on topology orientation");
        assertTrue(link.cellAPositionX == static_cast<float>(cells.mPositionX[owner])
                        && link.cellAPositionY == static_cast<float>(cells.mPositionY[owner])
                        && link.cellBPositionX == static_cast<float>(cells.mPositionX[target])
                        && link.cellBPositionY == static_cast<float>(cells.mPositionY[target])
                        && link.renderStartOffsetX == 0.003f && link.renderStartOffsetY == -0.005f
                        && link.renderEndOffsetX == -0.006f && link.renderEndOffsetY == 0.009f,
                "Positions and both local attachment offsets follow the render-owner");
        assertTrue(std::isfinite(link.fallbackCenterRatio), "Owner-oriented fallback ratio is finite");
        cellularity::tests::assertNear(4.25, link.fallbackCenterRatio, 1e-6,
                "The unclamped double-precision ratio uses the render-owner radius in either orientation");
        const std::array<float, 9> orientation{link.cellAPositionX, link.cellAPositionY,
                link.cellBPositionX, link.cellBPositionY, link.fallbackCenterRatio,
                link.renderStartOffsetX, link.renderStartOffsetY, link.renderEndOffsetX, link.renderEndOffsetY};
        if (!reversed) {
            firstOrientation = orientation;
        } else {
            assertTrue(orientation == firstOrientation, "Reversing topology endpoints preserves every oriented DTO float");
        }

        const CellTopology::EdgeView topologyAfter = cells.mTopology.edgeView();
        const std::array<double, 4> linkPhysicsAfter{topologyAfter.angleA[edge], topologyAfter.angleB[edge],
                topologyAfter.torsionalStiffness[edge], topologyAfter.restLength[edge]};
        assertTrue(physicalState() == physicalBefore && linkPhysicsAfter == linkPhysicsBefore
                        && topologyAfter.cellA[edge] == (reversed ? target : owner)
                        && topologyAfter.cellB[edge] == (reversed ? owner : target)
                        && cells.mTopology.edgeRuntimeId(edge) == edgeId
                        && cells.mTopology.structuralRevision() == topologyRevision && cells.mTopology.validate(),
                "Capture changes neither physical state nor topology endpoint order, Link parameters or revision");
    }
}

void testNormalLinkCaptureKeepsAscendingTopologyOrder() {
    constexpr int pairCount = 70;
    const Environment environment = Environment::constructDefaultEnvironment(pairCount * 2, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cells = world.mCellData;
    std::array<int, pairCount> targets{};
    std::array<int, pairCount> owners{};
    for (int pair = 0; pair < pairCount; ++pair) {
        targets[pair] = addAliveCell(cells, CellType::GLUEOCYTE);
        owners[pair] = addAliveCell(cells, CellType::GLUEOCYTE);
        for (const int cell : {targets[pair], owners[pair]}) {
            cells.mPositionX[cell] = -0.75 + 0.1 * (pair % 15) + (cell == owners[pair] ? 0.03 : 0.0);
            cells.mPositionY[cell] = -0.25 + 0.1 * (pair / 15);
            cells.mRadius[cell] = 0.02;
            cells.refreshRadiusSq(cell);
        }
    }
    for (int pair = pairCount - 1; pair >= 0; --pair) {
        addRenderEdge(cells, pair % 2 == 0 ? owners[pair] : targets[pair],
                pair % 2 == 0 ? targets[pair] : owners[pair], 0.001f);
    }
    const auto edges = cells.mTopology.renderEdgeView();
    cells.mIsDead[edges.cellA[5]] = true;
    cells.mIsDead[edges.cellB[64]] = true;
    const auto topologyRevision = cells.mTopology.structuralRevision();
    world.updateGrids();

    const RenderViewBounds view{-0.95f, 0.95f, -0.95f, 0.95f};
    RenderFrameCaptureMetadata metadata;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    RenderSourceFrame source;
    RenderSourceCapture capture(environment.maxCellCount, source);
    capture.capture(world, view, metadata, source);
    assertTrue(source.candidateLinks.size() == pairCount - 2,
            "Dead endpoints are excluded across multiple candidate words without deleting topology Links");
    std::size_t record = 0;
    for (int edge = 0; edge < pairCount; ++edge) {
        if (edge == 5 || edge == 64) {
            continue;
        }
        const int owner = std::max(edges.cellA[edge], edges.cellB[edge]);
        const int target = std::min(edges.cellA[edge], edges.cellB[edge]);
        const RenderSourceLink& link = source.candidateLinks[record++];
        assertTrue(link.sourceLinkIndex == edge && link.runtimeId == edges.runtimeId[edge]
                        && link.cellARuntimeId == cells.runtimeId(owner) && link.cellBRuntimeId == cells.runtimeId(target)
                        && link.sourceCellBIndex == target && link.sourceCellARecordIndex == recordIndexForCell(source, owner),
                "Owner selection preserves ascending topology-edge order and stable identities, not owner-index order");
        const RenderSourceCell& ownerRecord = source.cells[static_cast<std::size_t>(link.sourceCellARecordIndex)];
        const auto chordOffset = ownerRecord.contactChordOffset;
        assertTrue(ownerRecord.hasContactChordPayload && ownerRecord.contactChordCount == 1
                        && chordOffset < source.activeContactChords.size()
                        && source.activeContactChords[chordOffset].neighborCellIndex == target
                        && source.activeContactChordNeighborRuntimeIds[chordOffset] == cells.runtimeId(target),
                "Every contacted Link has the render-owner chord and its target lookup identity");
    }
    const std::vector<RenderSourceLink> firstCapture = source.candidateLinks;
    capture.capture(world, view, metadata, source);
    assertTrue(source.valid && activePrefixHasExactBytes(firstCapture, source.candidateLinks, firstCapture.size())
                    && cells.mTopology.edgeCount() == pairCount
                    && cells.mTopology.structuralRevision() == topologyRevision && cells.mTopology.validate(),
            "Repeated capture preserves exact Link payload bytes and leaves dead-endpoint topology intact");
}

void testNormalLinkCaptureRejectsInvalidEndpoints() {
    const Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cells = world.mCellData;
    const int target = addAliveCell(cells, CellType::GLUEOCYTE);
    const int owner = addAliveCell(cells, CellType::GLUEOCYTE);
    cells.mPositionX[target] = 0.03;
    cells.mPositionX[owner] = 0.0;
    cells.mPositionY[target] = cells.mPositionY[owner] = 0.0;
    const int edge = addRenderEdge(cells, owner, target, 0.001f);
    world.updateGrids();
    const RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    RenderFrameCaptureMetadata metadata;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    RenderSourceFrame source;
    RenderSourceCapture capture(environment.maxCellCount, source);
    const auto edges = cells.mTopology.renderEdgeView();
    for (const bool corruptFirst : {false, true}) {
        // Intentionally corrupt a non-const fixture through its view, then restore the endpoint.
        int* const endpoint = const_cast<int*>(corruptFirst ? edges.cellA : edges.cellB);
        const int saved = endpoint[edge];
        for (const int invalid : {-1, cells.mCellCount}) {
            endpoint[edge] = invalid;
            bool rejected = false;
            try {
                capture.capture(world, view, metadata, source);
            } catch (const std::logic_error& error) {
                rejected = std::string(error.what()) == "RenderSourceCapture received an invalid Link endpoint";
            }
            endpoint[edge] = saved;
            assertTrue(rejected && !source.valid && source.candidateLinks.empty(),
                    "Invalid normal endpoint is rejected before a Link record or endpoint payload is published");
        }
    }
    capture.capture(world, view, metadata, source);
    assertTrue(source.valid && source.candidateLinks.size() == 1 && cells.mTopology.validate(),
            "A failed capture does not poison the next valid capture");
}

void testCaptureIncludesLiveRenderOverflowCellsExactlyOnce() {
    constexpr int primaryCapacity = SimulationWorld::CELL_COLLISION_GRID_COUNT_MAX;
    constexpr int centeredCellCount = primaryCapacity + 3;
    Environment environment =
            Environment::constructDefaultEnvironment(centeredCellCount + 2, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cells = world.mCellData;

    for (int cellIndex = 0; cellIndex < centeredCellCount; ++cellIndex) {
        const int cell = addAliveCell(cells, CellType::PHAGOCYTE);
        cells.mPositionX[cell] = 0.0;
        cells.mPositionY[cell] = 0.0;
        cells.mRadius[cell] = 0.02;
        cells.mRadiusSq[cell] = 0.0004;
    }
    const int deadOverflowCell = centeredCellCount - 1;
    cells.markCellAsDead(deadOverflowCell);

    const int outsideView = addAliveCell(cells, CellType::PHAGOCYTE);
    cells.mPositionX[outsideView] = 0.75;
    cells.mPositionY[outsideView] = 0.0;
    cells.mRadius[outsideView] = 0.02;
    cells.mRadiusSq[outsideView] = 0.0004;

    const int nonFinite = addAliveCell(cells, CellType::PHAGOCYTE);
    cells.mPositionX[nonFinite] = std::numeric_limits<double>::quiet_NaN();
    cells.mPositionY[nonFinite] = 0.0;
    cells.mRadius[nonFinite] = 0.02;
    cells.mRadiusSq[nonFinite] = 0.0004;
    world.updateGrids();

    const RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    RenderFrameCaptureMetadata metadata;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    RenderSourceFrame source;
    RenderSourceCapture capture(environment.maxCellCount, source);

    const int visible = capture.capture(world, view, metadata, source);
    const int expectedVisible = primaryCapacity + 2;
    const auto& bucket =
            source.orderedCellRecordIndices[CellTypes::ordinal(CellType::PHAGOCYTE)];
    assertTrue(visible == expectedVisible && source.visibleCellCount == expectedVisible && source.cells.size() == static_cast<std::size_t>(expectedVisible) && bucket.size() == static_cast<std::size_t>(expectedVisible),
            "Capture includes every live same-bucket overflow Cell exactly "
            "once");

    std::vector<bool> seen(static_cast<std::size_t>(cells.mCellCount), false);
    for (std::size_t orderedIndex = 0; orderedIndex < bucket.size();
            ++orderedIndex) {
        const int recordIndex = bucket[orderedIndex];
        const int sourceCellIndex =
                source.cells[static_cast<std::size_t>(recordIndex)].sourceCellIndex;
        assertTrue(sourceCellIndex >= 0 && sourceCellIndex < cells.mCellCount && !seen[static_cast<std::size_t>(sourceCellIndex)],
                "Capture emits each visible overflow Cell at most once");
        seen[static_cast<std::size_t>(sourceCellIndex)] = true;
    }
    for (int cellIndex = 0; cellIndex < primaryCapacity; ++cellIndex) {
        const int recordIndex = bucket[static_cast<std::size_t>(cellIndex)];
        assertTrue(
                source.cells[static_cast<std::size_t>(recordIndex)].sourceCellIndex == cellIndex,
                "Capture preserves primary render-grid order");
    }
    assertTrue(
            source.cells[static_cast<std::size_t>(bucket[primaryCapacity])]
                                    .sourceCellIndex
                            == primaryCapacity + 1
                    && source.cells[static_cast<std::size_t>(bucket[primaryCapacity + 1])]
                                    .sourceCellIndex
                            == primaryCapacity,
            "Capture preserves render-overflow chain order after primary entries");
    assertTrue(!seen[static_cast<std::size_t>(deadOverflowCell)] && !seen[static_cast<std::size_t>(outsideView)] && !seen[static_cast<std::size_t>(nonFinite)],
            "Capture excludes dead, out-of-view, and non-finite Cells");

    const int movedOverflowCell = primaryCapacity + 1;
    cells.mPositionX[movedOverflowCell] = 0.75;
    world.updateGrids();
    const int visibleAfterMove = capture.capture(world, view, metadata, source);
    const auto& rebuiltBucket =
            source.orderedCellRecordIndices[CellTypes::ordinal(CellType::PHAGOCYTE)];
    assertTrue(visibleAfterMove == expectedVisible - 1 && rebuiltBucket.size() == static_cast<std::size_t>(expectedVisible - 1) && recordIndexForCell(source, movedOverflowCell) == -1 && recordIndexForCell(source, primaryCapacity) >= 0,
            "Capture rebuild removes a moved overflow Cell without losing "
            "the remaining overflow Cell");
}

void testCaptureOwnsOrderedCompactRenderInput() {
    Environment environment =
            Environment::constructDefaultEnvironment(128, 8, 1.0);
    environment.mLightAngle = 0.25;
    environment.mLightRange = 0.5;
    environment.mLightIntensity = 0.75;
    SimulationWorld world(false, environment);
    CellData& cells = world.mCellData;

    const int sourceCell = addAliveCell(cells, CellType::PHAGOCYTE);
    const int myocyte = addAliveCell(cells, CellType::MYOCYTE);
    const int deadTarget = addAliveCell(cells, CellType::CILIOCYTE);
    const int unrelated = addAliveCell(cells, CellType::STEMOCYTE);
    cells.mPositionX[sourceCell] = -0.02;
    cells.mPositionX[myocyte] = 0.01;
    cells.mPositionX[deadTarget] = 0.04;
    cells.mPositionX[unrelated] = 0.99;
    for (const int cell : {sourceCell, myocyte, deadTarget, unrelated}) {
        cells.mPositionY[cell] = 0.0;
        cells.mRadius[cell] = 0.02;
        cells.mRadiusSq[cell] = 0.0004;
    }
    cells.mMyocyteContraction[myocyte] = 0.35f;
    cells.mMyocyteBending[myocyte] = -0.45f;
    cells.mIsMirrored[myocyte] = true;
    cells.mIsDead[deadTarget] = true;

    const CellRuntimeId sourceCellRuntimeId = cells.runtimeId(sourceCell);
    const CellRuntimeId myocyteRuntimeId = cells.runtimeId(myocyte);

    const int firstEdge = addRenderEdge(cells, sourceCell, myocyte, 0.001f);
    addRenderEdge(cells, sourceCell, deadTarget, 0.002f);
    const TopologyEdgeRuntimeId firstEdgeRuntimeId =
            cells.mTopology.edgeRuntimeId(firstEdge);
    world.mFoodData.addFood(-0.02, 0.02, Environment::MAX_FOOD_SIZE, 0.0f,
            world.mEnvironment);
    world.mFoodData.addFood(0.02, 0.02, Environment::MAX_FOOD_SIZE, 1.0f,
            world.mEnvironment);
    world.mFoodData.addFood(0.8, 0.0, Environment::MAX_FOOD_SIZE, 0.5f,
            world.mEnvironment);
    const FoodRuntimeId firstFoodRuntimeId = world.mFoodData.runtimeId(0);
    const FoodRuntimeId secondFoodRuntimeId = world.mFoodData.runtimeId(1);
    world.mDeadSplashData.replaceActiveDeadSplashCount(2);
    for (int index = 0; index < 2; ++index) {
        world.mDeadSplashData.mPositionX[index] = index == 0 ? -0.02f : 0.02f;
        world.mDeadSplashData.mPositionY[index] = 0.0f;
        world.mDeadSplashData.mRadius[index] = 0.02f;
        world.mDeadSplashData.mAge[index] = 0.25f + index;
        world.mDeadSplashData.mColor[index] = {0.1f + index, 0.2f, 0.3f, 0.4f};
    }
    const DeadSplashRuntimeId firstDeadSplashRuntimeId =
            world.mDeadSplashData.runtimeId(0);
    const DeadSplashRuntimeId secondDeadSplashRuntimeId =
            world.mDeadSplashData.runtimeId(1);
    world.updateGrids();

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
    metadata.viewport.mvpMatrix = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f,
            7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f,
            13.0f, 14.0f, 15.0f, 16.0f};
    metadata.viewport.cameraX = 0.125f;
    metadata.viewport.cameraY = -0.25f;
    metadata.viewport.zoom = 3.5f;
    metadata.viewport.viewWidth = 0.75f;
    metadata.viewport.viewHeight = 0.5f;
    metadata.viewport.surfaceWidth = 2560;
    metadata.viewport.surfaceHeight = 1390;
    metadata.resolvedInput.lastResolvedInputSequence = 37;
    metadata.resolvedInput.appliedWorldRevision = 5;
    metadata.timeline.captureRequestedNanos = 101;

    RenderSourceFrame source;
    RenderSourceCapture capture(environment.maxCellCount, source);
    const RenderMarkerCaptureRequest markerRequest{myocyte, 0.01f, 0.3f};
    const int visible =
            capture.capture(world, view, metadata, source, &markerRequest, 1);

    assertTrue(source.valid && source.layoutVersion == cellularity::render::kRenderSourceFrameLayoutVersion && source.captureSequence == 1 && visible == 2 && source.visibleCellCount == 2,
            "Capture commits one valid versioned source frame last");
    assertTrue(source.capture.worldEpoch == 17 && source.capture.modeGeneration == 13 && source.capture.completedUpdateId == 29 && source.capture.stage == RenderCaptureStage::AfterCompletedUpdate && source.environment.revision == 3 && source.environment.lightAngle == 0.25 && source.environment.lightRange == 0.5 && source.environment.lightIntensity == 0.75 && source.environment.dishRadius == 1.0 && !source.environment.previewMode && source.environment.backgroundColor == std::array<float, 3>{},
            "Capture owns exact frame and environment metadata");
    assertTrue(source.capture.viewport.revision == 11 && source.capture.viewport.coverageRevision == 11 && source.capture.viewport.visibleBounds.minX == view.minX && source.capture.viewport.coverageBounds.maxY == view.maxY && source.capture.viewport.mvpMatrix[15] == 16.0f && source.capture.viewport.cameraX == 0.125f && source.capture.viewport.cameraY == -0.25f && source.capture.viewport.zoom == 3.5f && source.capture.viewport.viewWidth == 0.75f && source.capture.viewport.viewHeight == 0.5f && source.capture.viewport.surfaceWidth == 2560 && source.capture.viewport.surfaceHeight == 1390 && source.capture.resolvedInput.lastResolvedInputSequence == 37 && source.capture.resolvedInput.appliedWorldRevision == 5 && source.capture.timeline.captureRequestedNanos == 101,
            "Capture owns viewport, resolved-input, and timeline state");
#ifdef CELLULARITY_RENDER_TIMELINE
    assertTrue(source.capture.timeline.sourceCapturedNanos > 0,
            "Opt-in timeline stamps completed source capture");
#else
    assertTrue(source.capture.timeline.sourceCapturedNanos == 0,
            "Ordinary build performs no source-capture timeline clock read");
#endif

    const int sourceRecord = recordIndexForCell(source, sourceCell);
    const int myocyteRecord = recordIndexForCell(source, myocyte);
    const int deadRecord = recordIndexForCell(source, deadTarget);
    assertTrue(sourceRecord >= 0 && myocyteRecord >= 0 && deadRecord == -1 && recordIndexForCell(source, unrelated) == -1,
            "Capture retains visible and source-wall support Cells without "
            "cloning a hidden target");
    assertTrue(
            source.orderedCellRecordIndices[CellTypes::ordinal(
                    CellType::PHAGOCYTE)]
                            == std::vector<int>{sourceRecord}
                    && source.orderedCellRecordIndices[CellTypes::ordinal(
                               CellType::MYOCYTE)]
                            == std::vector<int>{myocyteRecord}
                    && source
                            .orderedCellRecordIndices[CellTypes::ordinal(CellType::CILIOCYTE)]
                            .empty(),
            "Capture records exact normal-mode visible order by bucket");
    assertTrue(source.candidateLinks.size() == 1,
            "Capture excludes the dead-target Link from its candidate range");
    const RenderSourceLink& firstLink = source.candidateLinks[0];
    assertTrue(
            firstLink.runtimeId == firstEdgeRuntimeId,
            "Capture preserves the stable live-edge identity");
    assertTrue(
            firstLink.cellARuntimeId == myocyteRuntimeId && firstLink.cellBRuntimeId == sourceCellRuntimeId,
            "Capture selects the higher-index render-owner with stable endpoint identities");
    assertTrue(firstLink.cellAPositionX == 0.01f && firstLink.cellAPositionY == 0.0f
                    && firstLink.cellBPositionX == -0.02f && firstLink.cellBPositionY == 0.0f
                    && firstLink.renderStartOffsetX == -0.001f && firstLink.renderEndOffsetX == 0.001f,
            "Capture orients positions and attachment offsets toward the render-owner");
    assertTrue(
            source.cells[static_cast<std::size_t>(sourceRecord)].contactChordCount == 1 && source.cells[static_cast<std::size_t>(sourceRecord)].hasContactChordPayload && source.cells[static_cast<std::size_t>(myocyteRecord)].contactChordCount == 1 && source.activeContactChords.size() == 2 && source.activeContactChordNeighborRuntimeIds.size() == 2,
            "Capture copies only active walls required by visible geometry and Link "
            "sources");
    assertTrue(
            source.cells[static_cast<std::size_t>(sourceRecord)].runtimeId == sourceCellRuntimeId && source.cells[static_cast<std::size_t>(myocyteRecord)].runtimeId == myocyteRuntimeId && source.activeContactChordNeighborRuntimeIds == std::vector<CellRuntimeId>{myocyteRuntimeId, sourceCellRuntimeId},
            "Capture owns stable Cell and raw-wall neighbor identities");
    const RenderSourceCell& capturedMyocyte =
            source.cells[static_cast<std::size_t>(myocyteRecord)];
    assertTrue(
            capturedMyocyte.baseAux == -0.35f && capturedMyocyte.baseAux4 == -0.45f && capturedMyocyte.mirrored && !capturedMyocyte.previewArrow,
            "Capture owns type-specific values and discrete Cell layout flags");
    assertTrue(
            source.foods.size() == 2 && source.foods[0].runtimeId == firstFoodRuntimeId && source.foods[1].runtimeId == secondFoodRuntimeId && source.foods[0].instance == std::array<float, 4>{-0.02f, 0.02f, FoodData::RADIUS, 0.0f} && source.foods[1].vertex == std::array<float, 4>{1.0f, 0.0f, 0.0f, 0.4f} && source.deadSplashes.size() == 2 && source.deadSplashes[0].runtimeId == firstDeadSplashRuntimeId && source.deadSplashes[1].runtimeId == secondDeadSplashRuntimeId,
            "Capture owns stable identities and exact ordered final Food/dead "
            "records");
    assertTrue(source.markers.size() == 1 && source.markers[0].selectedCellRuntimeId == myocyteRuntimeId && source.markers[0].x == 0.01f && source.markers[0].y == 0.0f && source.markers[0].radius == 0.03f && source.markers[0].alpha == 0.3f,
            "Capture resolves marker requests to stable Cell identity and "
            "owned values");

    PreparedRenderFrame markerFrame;
    RenderFrameBuilder markerBuilder(environment.maxCellCount, markerFrame);
    markerBuilder.build(source, markerFrame);
    assertTrue(markerFrame.markers.size() == 1 && markerFrame.markers[0].selectedCellRuntimeId == myocyteRuntimeId && markerFrame.viewport.cameraX == 0.125f && markerFrame.viewport.coverageRevision == 11 && markerFrame.layout.markerRevision == markerFrame.identity.captureSequence,
            "Builder preserves source-owned markers and viewport metadata");

    const std::uint64_t fullWorldCloneLowerBound =
            static_cast<std::uint64_t>(environment.maxCellCount) * (sizeof(RenderSourceCell) + static_cast<std::uint64_t>(cellularity::render::kMaximumRenderContactChordsPerCell) * sizeof(RenderContactChord));
    assertTrue(source.activeStorageBytes() < fullWorldCloneLowerBound && source.retainedStorageBytes() < fullWorldCloneLowerBound,
            "Sparse source storage remains smaller than cloning full Cell and "
            "wall capacity");

    world.mEnvironment.mIsPreviewMode = true;
    metadata.stage = RenderCaptureStage::GenomePreview;
    capture.capture(world, view, metadata, source);
    const auto& previewOrder =
            source.orderedCellRecordIndices
                    [cellularity::render::kPreviewExtraCellTypeIndex];
    assertTrue(source.captureSequence == 2 && previewOrder.size() == 4,
            "Preview capture owns its complete arrow bucket");
    for (int index = 0; index < 4; ++index) {
        const RenderSourceCell& previewCell = source.cells[static_cast<std::size_t>(
                previewOrder[static_cast<std::size_t>(index)])];
        assertTrue(
                previewCell.sourceCellIndex == index && previewCell.previewArrow,
                "Preview arrow bucket preserves linear order and explicit role flags");
    }
    const int unrelatedRecord = recordIndexForCell(source, unrelated);
    assertTrue(unrelatedRecord >= 0,
            "Preview capture includes the offscreen Cell in its arrow bucket");
    const RenderSourceCell& capturedUnrelated =
            source.cells[static_cast<std::size_t>(unrelatedRecord)];
    assertTrue(capturedUnrelated.contactChordCount == 1,
            "Preview capture owns the unrelated Cell boundary contact");
    for (std::size_t wallIndex = capturedUnrelated.contactChordOffset;
            wallIndex < capturedUnrelated.contactChordOffset + capturedUnrelated.contactChordCount;
            ++wallIndex) {
        assertTrue(source.activeContactChordNeighborRuntimeIds[wallIndex] == kBoundaryCellRuntimeId,
                "Boundary walls use the dedicated runtime identity sentinel");
    }

    cells.mCellCount = environment.maxCellCount + 1;
    bool rejected = false;
    try {
        capture.capture(world, view, metadata, source);
    } catch (const std::length_error&) {
        rejected = true;
    }
    cells.mCellCount = 4;
    assertTrue(
            rejected && !source.valid,
            "Failed capture invalidates the reusable source slot before validation");
}

void testBuilderUsesSourceOrderWithoutReselection() {
    RenderSourceFrame source;
    source.valid = true;
    source.sourceCellCount = 2;
    source.visibleCellCount = 2;
    source.capture.viewport.visibleBounds = {-1.0f, 1.0f, -1.0f, 1.0f};
    source.capture.viewport.coverageBounds =
            source.capture.viewport.visibleBounds;
    source.cells.resize(2);
    for (int index = 0; index < 2; ++index) {
        RenderSourceCell& cell = source.cells[static_cast<std::size_t>(index)];
        cell.runtimeId = static_cast<CellRuntimeId>(index + 1);
        cell.sourceCellIndex = index;
        cell.cellType = CellType::PHAGOCYTE;
        cell.positionX = index == 0 ? -0.25f : 0.5f;
        cell.positionY = 0.0f;
        cell.radius = 0.02f;
        cell.red = 0.1f + index;
        cell.green = 0.2f;
        cell.blue = 0.3f;
        cell.hasContactChordPayload = true;
    }
    source.orderedCellRecordIndices[CellTypes::ordinal(CellType::PHAGOCYTE)] = {
            1, 0
    };

    PreparedRenderFrame prepared;
    RenderFrameBuilder builder(2, prepared);
    builder.build(source, prepared);
    assertTrue(prepared.cellCenterCoords[0] == 0.5f && prepared.cellCenterCoords[8] == -0.25f,
            "Builder consumes source bucket order without selecting or "
            "sorting Cells");
}

void testHiddenRenderOwnerRetainsItsContactPayload() {
    for (const bool reversed : {false, true}) {
        const Environment environment =
                Environment::constructDefaultEnvironment(8, 0, 1.0);
        SimulationWorld world(false, environment);
        CellData& cells = world.mCellData;
        const int visibleTarget = addAliveCell(cells, CellType::NEUROCYTE);
        const int hiddenNeighbor = addAliveCell(cells, CellType::GLUEOCYTE);
        const int hiddenSource = addAliveCell(cells, CellType::GLUEOCYTE);
        cells.mPositionX[hiddenSource] = -0.20;
        cells.mPositionX[hiddenNeighbor] = -0.23;
        cells.mPositionX[visibleTarget] = 0.0;
        for (const int cell : {hiddenSource, visibleTarget, hiddenNeighbor}) {
            cells.mPositionY[cell] = 0.0;
            cells.mRadius[cell] = 0.02;
            cells.refreshRadiusSq(cell);
        }
        const int edge = addRenderEdge(cells, reversed ? visibleTarget : hiddenSource,
                reversed ? hiddenSource : visibleTarget, 0.003f);
        world.updateGrids();

        const RenderViewBounds view{-0.05f, 0.05f, -0.05f, 0.05f};
        RenderFrameCaptureMetadata metadata;
        metadata.worldEpoch = 53;
        metadata.viewport.visibleBounds = view;
        metadata.viewport.coverageBounds = view;
        RenderSourceFrame source;
        RenderSourceCapture capture(environment.maxCellCount, source);
        capture.capture(world, view, metadata, source);

        const int sourceRecord = recordIndexForCell(source, hiddenSource);
        const int targetRecord = recordIndexForCell(source, visibleTarget);
        assertTrue(
                source.visibleCellCount == 1 && sourceRecord >= 0 && targetRecord >= 0
                        && source.cells.size() == 2
                        && source.cells[static_cast<std::size_t>(sourceRecord)].hasContactChordPayload
                        && source.cells[static_cast<std::size_t>(sourceRecord)].contactChordCount == 1
                        && recordIndexForCell(source, hiddenNeighbor) == -1,
                "A hidden render-owner retains the contact payload needed by its Link");
        const auto chordOffset = source.cells[static_cast<std::size_t>(sourceRecord)].contactChordOffset;
        assertTrue(chordOffset < source.activeContactChords.size()
                        && source.activeContactChords[chordOffset].neighborCellIndex == hiddenNeighbor
                        && source.activeContactChordNeighborRuntimeIds[chordOffset] == cells.runtimeId(hiddenNeighbor),
                "Hidden-owner payload contains its real contact and stable neighbor identity");
        assertTrue(
                source.orderedCellRecordIndices[CellTypes::ordinal(CellType::GLUEOCYTE)]
                                .empty()
                        && source.orderedCellRecordIndices[CellTypes::ordinal(
                                   CellType::NEUROCYTE)]
                                == std::vector<int>{targetRecord},
                "Hidden Link sources do not enter Cell draw buckets");
        assertTrue(source.candidateLinks.size() == 1
                        && source.candidateLinks[0].runtimeId == cells.mTopology.edgeRuntimeId(edge)
                        && source.candidateLinks[0].cellARuntimeId == cells.runtimeId(hiddenSource)
                        && source.candidateLinks[0].cellBRuntimeId == cells.runtimeId(visibleTarget)
                        && source.candidateLinks[0].sourceCellARecordIndex == sourceRecord
                        && source.candidateLinks[0].sourceCellBIndex == visibleTarget
                        && source.candidateLinks[0].cellAPositionX == -0.20f
                        && source.candidateLinks[0].cellBPositionX == 0.0f
                        && source.candidateLinks[0].renderStartOffsetX == (reversed ? -0.003f : 0.003f)
                        && source.candidateLinks[0].renderEndOffsetX == (reversed ? 0.003f : -0.003f),
                "Hidden-owner Link record preserves identity and endpoint positions");

        PreparedRenderFrame prepared;
        RenderFrameBuilder builder(environment.maxCellCount, prepared);
        builder.build(source, prepared);
        assertTrue(prepared.drawLinksCount == 6,
                "Builder keeps a stretched hidden-source Link visible without "
                "requiring a collision contact");
    }
}

void testCaptureRebuildsStaleWallsFromCurrentGeometry() {
    const Environment environment =
            Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cells = world.mCellData;
    const int center = addAliveCell(cells, CellType::PHAGOCYTE);
    const int oldNeighbor = addAliveCell(cells, CellType::PHAGOCYTE);
    const int newNeighbor = addAliveCell(cells, CellType::PHAGOCYTE);
    cells.mPositionX[center] = 0.0;
    cells.mPositionX[oldNeighbor] = 0.03;
    cells.mPositionX[newNeighbor] = 0.2;
    for (const int cell : {center, oldNeighbor, newNeighbor}) {
        cells.mPositionY[cell] = 0.0;
        cells.mRadius[cell] = 0.02;
        cells.refreshRadiusSq(cell);
    }
    world.updateGrids();

    const RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    RenderFrameCaptureMetadata metadata;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    RenderSourceFrame source;
    RenderSourceCapture capture(environment.maxCellCount, source);
    capture.capture(world, view, metadata, source);

    const int firstCenterRecord = recordIndexForCell(source, center);
    assertTrue(firstCenterRecord >= 0, "Loaded Cell is present in source capture");
    const RenderSourceCell& firstCenter =
            source.cells[static_cast<std::size_t>(firstCenterRecord)];
    assertTrue(
            !world.cellContactCacheIsFresh() && firstCenter.contactChordCount == 1 && source.activeContactChordNeighborRuntimeIds[firstCenter.contactChordOffset] == cells.runtimeId(oldNeighbor),
            "Capture builds loaded walls from current geometry before a simulation "
            "tick");

    world.processCellOverlaps();
    assertTrue(world.cellContactCacheIsFresh(),
            "Regression fixture starts its edit with a fresh physics cache");
    cells.mPositionX[oldNeighbor] = 0.2;
    cells.mPositionX[newNeighbor] = 0.03;
    world.updateGrids();
    capture.capture(world, view, metadata, source);

    const int movedCenterRecord = recordIndexForCell(source, center);
    assertTrue(movedCenterRecord >= 0,
            "Moved Cell is present in the next source capture");
    const RenderSourceCell& movedCenter =
            source.cells[static_cast<std::size_t>(movedCenterRecord)];
    assertTrue(
            !world.cellContactCacheIsFresh() && movedCenter.contactChordCount == 1 && source.activeContactChordNeighborRuntimeIds[movedCenter.contactChordOffset] == cells.runtimeId(newNeighbor),
            "Freeze edit drops the old wall and builds the new wall from current "
            "positions");
}

void testRuntimeIdentitySurvivesCompactionAndSlotReuse() {
    const Environment environment =
            Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cells = world.mCellData;
    const int first = addAliveCell(cells, CellType::PHAGOCYTE);
    const int removed = addAliveCell(cells, CellType::MYOCYTE);
    const int moved = addAliveCell(cells, CellType::GLUEOCYTE);
    for (const int cell : {first, removed, moved}) {
        cells.mPositionX[cell] = static_cast<double>(cell) * 0.03;
        cells.mPositionY[cell] = 0.0;
        cells.mRadius[cell] = 0.02;
        cells.refreshRadiusSq(cell);
    }
    const CellRuntimeId firstId = cells.runtimeId(first);
    const CellRuntimeId removedId = cells.runtimeId(removed);
    const CellRuntimeId movedId = cells.runtimeId(moved);
    world.updateGrids();

    const RenderViewBounds view{-0.2f, 0.2f, -0.2f, 0.2f};
    RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 41;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    RenderSourceFrame source;
    RenderSourceCapture capture(environment.maxCellCount, source);
    capture.capture(world, view, metadata, source);
    assertTrue(
            source.cells[static_cast<std::size_t>(recordIndexForCell(source, first))]
                                    .runtimeId
                            == firstId
                    && source.cells[static_cast<std::size_t>(
                                            recordIndexForCell(source, moved))]
                                    .runtimeId
                            == movedId,
            "Initial source capture exposes allocated runtime identities");

    world.removeCellImmediately(removed);
    world.updateGrids();
    capture.capture(world, view, metadata, source);
    const int movedRecord = recordIndexForCell(source, removed);
    assertTrue(
            movedRecord >= 0 && source.cells[static_cast<std::size_t>(movedRecord)].runtimeId == movedId,
            "Compaction changes the frame-local slot but preserves stable identity");
    assertTrue(std::none_of(source.cells.begin(), source.cells.end(),
                       [&](const RenderSourceCell& cell) {
                           return cell.runtimeId == removedId;
                       }),
            "Removed Cell identity is absent from the next source frame");

    const int reusedSlot = addAliveCell(cells, CellType::NEUROCYTE);
    const CellRuntimeId replacementId = cells.runtimeId(reusedSlot);
    assertTrue(
            reusedSlot == moved && replacementId > removedId && replacementId > movedId && replacementId > firstId,
            "A reused source slot receives a fresh monotonic runtime identity");
}

void testRuntimeIdentityRangeTransitions() {
    CellData cells(4);
    cells.replaceActiveCellCount(2);
    const CellRuntimeId firstId = cells.runtimeId(0);
    const CellRuntimeId secondId = cells.runtimeId(1);

    cells.resizeActiveCellCount(3);
    const CellRuntimeId thirdId = cells.runtimeId(2);
    assertTrue(
            cells.runtimeId(0) == firstId && cells.runtimeId(1) == secondId && thirdId > firstId && thirdId > secondId,
            "Synthetic range growth preserves its prefix and identifies new Cells");

    cells.resizeActiveCellCount(1);
    cells.resizeActiveCellCount(3);
    assertTrue(cells.runtimeId(0) == firstId && cells.runtimeId(1) != secondId && cells.runtimeId(2) != thirdId,
            "Shrunk synthetic slots cannot resurrect retired identities");

    const CellRuntimeId replacementFirstId = cells.runtimeId(0);
    cells.replaceActiveCellCount(3);
    assertTrue(cells.runtimeId(0) != replacementFirstId && cells.runtimeId(0) != firstId,
            "Replacing a loaded active range assigns a fresh identity set");
}

void testAuxiliaryRuntimeIdentityLifecycle() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    FoodData foods(4);
    const int firstFood = foods.addFood(-0.1, 0.0, 0.1, 0.0f, environment);
    const int removedFood = foods.addFood(0.0, 0.0, 0.1, 0.0f, environment);
    const int movedFood = foods.addFood(0.1, 0.0, 0.1, 0.0f, environment);
    const FoodRuntimeId firstFoodId = foods.runtimeId(firstFood);
    const FoodRuntimeId removedFoodId = foods.runtimeId(removedFood);
    const FoodRuntimeId movedFoodId = foods.runtimeId(movedFood);
    foods.removeFood(removedFood);
    assertTrue(foods.runtimeId(0) == firstFoodId && foods.runtimeId(removedFood) == movedFoodId,
            "Food compaction preserves the surviving entity identity");
    const int reusedFood = foods.addFood(0.2, 0.0, 0.1, 0.0f, environment);
    const FoodRuntimeId replacementFoodId = foods.runtimeId(reusedFood);
    assertTrue(reusedFood == movedFood && replacementFoodId > firstFoodId && replacementFoodId > removedFoodId && replacementFoodId > movedFoodId,
            "A reused Food slot receives a fresh monotonic identity");
    foods.resizeActiveFoodCount(1);
    foods.resizeActiveFoodCount(3);
    assertTrue(foods.runtimeId(0) == firstFoodId && foods.runtimeId(1) != movedFoodId && foods.runtimeId(2) != replacementFoodId,
            "Food shrink/grow cannot resurrect retired identities");
    const FoodRuntimeId oldFirstFoodId = foods.runtimeId(0);
    foods.replaceActiveFoodCount(3);
    assertTrue(foods.runtimeId(0) != oldFirstFoodId,
            "Replacing a loaded Food range assigns a fresh identity set");
    const FoodRuntimeId beforeRejectedFoodId = foods.runtimeId(2);
    assertTrue(foods.addFood(2.0, 0.0, 0.1, 0.0f, environment) == -1,
            "Rejected Food creation does not create an entity");
    foods.resizeActiveFoodCount(2);
    const int acceptedFood = foods.addFood(0.25, 0.0, 0.1, 0.0f, environment);
    assertTrue(foods.runtimeId(acceptedFood) == beforeRejectedFoodId + 1,
            "Rejected Food creation does not consume runtime identity");

    DeadSplashData splashes(4);
    const std::array<float, 4> color{0.1f, 0.2f, 0.3f, 0.4f};
    const int firstSplash = splashes.addDeadSplash(-0.1f, 0.0f, 0.02f, color);
    const int removedSplash = splashes.addDeadSplash(0.0f, 0.0f, 0.02f, color);
    const int movedSplash = splashes.addDeadSplash(0.1f, 0.0f, 0.02f, color);
    const DeadSplashRuntimeId firstSplashId = splashes.runtimeId(firstSplash);
    const DeadSplashRuntimeId removedSplashId = splashes.runtimeId(removedSplash);
    const DeadSplashRuntimeId movedSplashId = splashes.runtimeId(movedSplash);
    splashes.removeDeadSplash(removedSplash);
    assertTrue(splashes.runtimeId(0) == firstSplashId && splashes.runtimeId(removedSplash) == movedSplashId,
            "Dead-splash compaction preserves the surviving entity identity");
    const int reusedSplash = splashes.addDeadSplash(0.2f, 0.0f, 0.02f, color);
    const DeadSplashRuntimeId replacementSplashId =
            splashes.runtimeId(reusedSplash);
    assertTrue(reusedSplash == movedSplash && replacementSplashId > firstSplashId && replacementSplashId > removedSplashId && replacementSplashId > movedSplashId,
            "A reused dead-splash slot receives a fresh monotonic identity");
    splashes.resizeActiveDeadSplashCount(1);
    splashes.resizeActiveDeadSplashCount(3);
    assertTrue(splashes.runtimeId(0) == firstSplashId && splashes.runtimeId(1) != movedSplashId && splashes.runtimeId(2) != replacementSplashId,
            "Dead-splash shrink/grow cannot resurrect retired identities");
    const DeadSplashRuntimeId oldFirstSplashId = splashes.runtimeId(0);
    splashes.replaceActiveDeadSplashCount(3);
    assertTrue(splashes.runtimeId(0) != oldFirstSplashId,
            "Replacing a loaded dead-splash range assigns fresh identities");
}

void testStaleFoodIndexCaptureMatchesFreshDeterministicOrder() {
    Environment environment =
            Environment::constructDefaultEnvironment(4, 8, 1.0);
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    for (int foodIndex = 0; foodIndex < 4; ++foodIndex) {
        world.mFoodData.addFood(0.010 + foodIndex * 0.001, 0.010,
                Environment::MAX_FOOD_SIZE,
                0.1f * static_cast<float>(foodIndex),
                world.mEnvironment);
    }
    world.ensureFoodSpatialIndex();

    world.mFoodData.mPositionX[0] = 0.4f;
    world.mFoodData.markFoodPositionChanged(0);
    world.removeFoodImmediately(1);
    world.mFoodData.addFood(0.014, 0.010, Environment::MAX_FOOD_SIZE, 0.8f,
            world.mEnvironment);
    assertTrue(!world.foodSpatialIndexIsFresh(),
            "Food mutation batch leaves the shared index stale");

    const RenderViewBounds view{-0.03f, 0.03f, -0.03f, 0.03f};
    RenderFrameCaptureMetadata metadata;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    RenderSourceFrame staleSource;
    RenderSourceCapture staleCapture(environment.maxCellCount, staleSource);
    staleCapture.capture(world, view, metadata, staleSource);

    world.ensureFoodSpatialIndexForCapture();
    RenderSourceFrame freshSource;
    RenderSourceCapture freshCapture(environment.maxCellCount, freshSource);
    freshCapture.capture(world, view, metadata, freshSource);

    assertTrue(staleSource.foods.size() == 3 && staleSource.foods.size() == freshSource.foods.size(),
            "Stale Food fallback captures the same visible set");
    for (std::size_t foodIndex = 0; foodIndex < staleSource.foods.size();
            ++foodIndex) {
        const auto& stale = staleSource.foods[foodIndex];
        const auto& fresh = freshSource.foods[foodIndex];
        assertTrue(stale.runtimeId == fresh.runtimeId && stale.instance == fresh.instance && stale.vertex == fresh.vertex,
                "Stale Food fallback preserves fresh grid traversal order");
    }
    assertTrue(staleSource.foods[0].runtimeId == world.mFoodData.runtimeId(3) && staleSource.foods[1].runtimeId == world.mFoodData.runtimeId(2) && staleSource.foods[2].runtimeId == world.mFoodData.runtimeId(1),
            "Narrow Food capture remains descending by active slot in a bucket");
}

void testSourceRemainsExactAfterWorldMutationAndDestruction() {
    const Environment environment =
            Environment::constructDefaultEnvironment(8, 2, 1.0);
    auto world = std::make_unique<SimulationWorld>(false, environment);
    CellData& cells = world->mCellData;
    const int left = addAliveCell(cells, CellType::PHAGOCYTE);
    const int right = addAliveCell(cells, CellType::NEUROCYTE);
    cells.mPositionX[left] = -0.01;
    cells.mPositionX[right] = 0.02;
    cells.mPositionY[left] = cells.mPositionY[right] = 0.0;
    cells.mRadius[left] = cells.mRadius[right] = 0.02;
    cells.mRadiusSq[left] = cells.mRadiusSq[right] = 0.0004;
    addRenderEdge(cells, left, right, 0.003f);
    world->mFoodData.addFood(0.0, 0.03, Environment::MAX_FOOD_SIZE, 0.25f,
            world->mEnvironment);
    world->mDeadSplashData.replaceActiveDeadSplashCount(1);
    world->mDeadSplashData.mPositionX[0] = 0.0f;
    world->mDeadSplashData.mPositionY[0] = -0.03f;
    world->mDeadSplashData.mRadius[0] = 0.02f;
    world->mDeadSplashData.mAge[0] = 0.25f;
    world->mDeadSplashData.mColor[0] = {0.2f, 0.3f, 0.4f, 0.5f};
    world->updateGrids();

    const RenderViewBounds view{-0.1f, 0.1f, -0.1f, 0.1f};
    RenderFrameCaptureMetadata metadata;
    metadata.viewport.visibleBounds = view;
    metadata.viewport.coverageBounds = view;
    RenderSourceFrame source;
    RenderSourceCapture capture(environment.maxCellCount, source);
    const RenderMarkerCaptureRequest markerRequest{right, 0.01f, 0.6f};
    capture.capture(*world, view, metadata, source, &markerRequest, 1);

    PreparedRenderFrame expected;
    RenderFrameBuilder builder(environment.maxCellCount, expected);
    builder.build(source, expected);
    assertTrue(expected.markers.size() == 1 && expected.markers[0].selectedCellRuntimeId == cells.runtimeId(right),
            "Ownership fixture captures a stable selected-Cell marker");
    const auto expectedMarker = expected.markers[0];
    const auto expectedEnvironment = source.environment;

    cells.mPositionX[left] = 123.0;
    cells.mPositionX[right] = -321.0;
    cells.mRadius[right] = 7.0;
    cells.mColor[left][0] = 99.0f;
    cells.clearCellCollisionContacts();
    world->mEnvironment.mLightAngle = 42.0;
    world.reset();

    PreparedRenderFrame rebuilt;
    builder.build(source, rebuilt);
    assertPreparedGeometryExact(expected, rebuilt);
    assertTrue(rebuilt.markers.size() == 1 && rebuilt.markers[0].selectedCellRuntimeId == expectedMarker.selectedCellRuntimeId && rebuilt.markers[0].x == expectedMarker.x && rebuilt.markers[0].y == expectedMarker.y && rebuilt.markers[0].radius == expectedMarker.radius && rebuilt.markers[0].alpha == expectedMarker.alpha,
            "Marker snapshot remains exact after source-world destruction");
    assertTrue(
            source.environment.lightAngle == expectedEnvironment.lightAngle && source.environment.lightRange == expectedEnvironment.lightRange && source.environment.lightIntensity == expectedEnvironment.lightIntensity && source.environment.dishRadius == expectedEnvironment.dishRadius,
            "Environment snapshot remains exact after source-world destruction");
}

} // namespace

int main(int argc, char** argv) {
    assertTrue(argc == 1 || (argc == 2 && (std::string(argv[1]) == "--dto-only" || std::string(argv[1]) == "--link-capture-only")),
            "Usage: cellularity_render_source_frame_tests [--dto-only|--link-capture-only]");
    testLinkDtoUsesRuntimeBoundarySentinel();
    testLinkDtoStorageAccountsMixedRecords();
    testCapturePopulatesLinkDtoLookupAndUnclampedRatio();
    if (argc == 2 && std::string(argv[1]) == "--dto-only") {
        std::cout << "render source Link DTO tests passed (72 bytes)\n";
        return 0;
    }
    testNormalLinkCaptureUsesHigherIndexOwner();
    testNormalLinkCaptureKeepsAscendingTopologyOrder();
    testNormalLinkCaptureRejectsInvalidEndpoints();
    testHiddenRenderOwnerRetainsItsContactPayload();
    if (argc == 2) {
        std::cout << "normal Link capture tests passed\n";
        return 0;
    }
    testCaptureIncludesLiveRenderOverflowCellsExactlyOnce();
    testCaptureOwnsOrderedCompactRenderInput();
    testBuilderUsesSourceOrderWithoutReselection();
    testCaptureRebuildsStaleWallsFromCurrentGeometry();
    testSourceRemainsExactAfterWorldMutationAndDestruction();
    testRuntimeIdentitySurvivesCompactionAndSlotReuse();
    testRuntimeIdentityRangeTransitions();
    testAuxiliaryRuntimeIdentityLifecycle();
    testStaleFoodIndexCaptureMatchesFreshDeterministicOrder();
    std::cout << "render source frame tests passed\n";
    return 0;
}
