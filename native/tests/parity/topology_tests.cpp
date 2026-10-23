#include "core/CellData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
#include "core/CellSignalAdjacency.h"
#endif
#include "core/CellSweepBroadphase.h"
#include "core/CellTopology.h"
#include "core/SimulationWorld.h"
#include "core/CollisionGrid.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/RenderContactChord.h"
#include "render/frame/RenderContactChordBuffer.h"
#include "render/frame/RenderContactCollector.h"
#include "render/frame/RenderContactLimits.h"
#include "render/geometry/RenderContactChordNormalizer.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity::tests;

namespace {

void testLinkPhysicsConstants() {
    assertNear(30.0, CellData::LINK_SPRING_COEFFICIENT, 0.0, "CellData LINK_SPRING_COEFFICIENT");
    assertNear(50.0, CellData::LINK_SUBSTRATE_DAMPING, 0.0, "CellData LINK_SUBSTRATE_DAMPING");
    assertNear(25.0, CellData::LINK_INTERCELL_DAMPING, 0.0, "CellData LINK_INTERCELL_DAMPING");
}

void testRenderContactChordCopy() {
    RenderContactChord source;
    source.x1 = 1.0f;
    source.y1 = 2.0f;
    source.x2 = 3.0f;
    source.y2 = 4.0f;
    source.neighborCellIndex = 9;
    source.isDiscarded = true;
    source.joinsNextChord = true;

    const RenderContactChord copy = source;
    assertNear(source.x1, copy.x1, 0.0, "RenderContactChord copy x1");
    assertNear(source.y2, copy.y2, 0.0, "RenderContactChord copy y2");
    assertTrue(copy.neighborCellIndex == 9,
            "RenderContactChord copy neighbor cell index");
    assertTrue(copy.isDiscarded, "RenderContactChord copy discarded state");
    assertTrue(copy.joinsNextChord, "RenderContactChord copy join state");
}

void testCollisionGridCoordinatesAndBounds() {
    CollisionGrid grid(1.0, 4, 2, false);

    assertTrue(grid.stride() == 6, "CollisionGrid stride");
    assertTrue(grid.convertWorldToGridPosition(-1.0) == 1, "CollisionGrid world left edge");
    assertTrue(grid.convertWorldToGridPosition(0.0) == 3, "CollisionGrid world center");
    assertTrue(grid.convertWorldToGridPosition(1.0) == 5, "CollisionGrid world right edge");
    assertTrue(grid.convertWorldToGridPosition(-2.0) == -1, "CollisionGrid world outside negative truncation");
    assertTrue(grid.indexAtGridPosition(2, 3) == 20, "CollisionGrid index at grid position");
    assertTrue(grid.indexAtWorldPosition(0.0, 0.5) == grid.indexAtGridPosition(3, 4), "CollisionGrid index at world position");
    assertTrue(grid.convertWorldToGridPosition(std::numeric_limits<double>::max()) == grid.stride(),
            "CollisionGrid saturates extreme positive world position");
    assertTrue(grid.convertWorldToGridPosition(-std::numeric_limits<double>::max()) == -1,
            "CollisionGrid saturates extreme negative world position");
    assertTrue(grid.convertWorldToGridPosition(std::numeric_limits<double>::quiet_NaN()) == -1,
            "CollisionGrid rejects non-finite world position");
    assertTrue(grid.indexAtWorldPosition(std::numeric_limits<double>::max(), 0.0) == -1,
            "CollisionGrid rejects extreme world index");
    assertTrue(grid.indexAtGridPosition(std::numeric_limits<int>::max(),
                       std::numeric_limits<int>::max())
                    == -1,
            "CollisionGrid rejects overflowing grid coordinates");
    assertTrue(grid.clampGridPosition(-3) == 0, "CollisionGrid clamp low");
    assertTrue(grid.clampGridPosition(99) == 5, "CollisionGrid clamp high");
    assertTrue(grid.containsGridPosition(0, 0), "CollisionGrid contains origin bucket");
    assertTrue(grid.containsGridPosition(5, 5), "CollisionGrid contains last bucket");
    assertTrue(!grid.containsGridPosition(6, 0), "CollisionGrid rejects high x");
    assertTrue(!grid.containsGridPosition(0, -1), "CollisionGrid rejects low y");
    assertTrue(grid.containsIndex(35), "CollisionGrid contains last index");
    assertTrue(!grid.containsIndex(36), "CollisionGrid rejects high index");
    assertTrue(!grid.add(-1, 1),
            "CollisionGrid rejects invalid untyped add");
}

void testCollisionGridUntypedAddOverflowAndClear() {
    CollisionGrid grid(1.0, 4, 2, false);
    int gridIndex = grid.indexAtGridPosition(3, 3);
    int offset = grid.entryOffsetAt(gridIndex);

    assertTrue(grid.add(gridIndex, 11), "CollisionGrid first untyped add");
    assertTrue(grid.add(gridIndex, 22), "CollisionGrid second untyped add");
    assertTrue(!grid.add(gridIndex, 33), "CollisionGrid untyped add overflow");
    assertTrue(grid.countAt(gridIndex) == 2, "CollisionGrid untyped count");
    assertTrue(grid.indexAtOffset(offset) == 11, "CollisionGrid untyped first value");
    assertTrue(grid.indexAtOffset(offset + 1) == 22, "CollisionGrid untyped second value");

    bool threw = false;
    try {
        (void)grid.typeAtOffset(offset);
    } catch (const std::logic_error&) {
        threw = true;
    }
    assertTrue(threw, "CollisionGrid untyped typeAtOffset throws");

    grid.clear();
    assertTrue(grid.countAt(gridIndex) == 0, "CollisionGrid clear resets count");
    assertTrue(grid.indexAtOffset(offset) == 11, "CollisionGrid clear leaves indexes like Java arrays");
}

void testCollisionGridTypedAddStoresTypesAndThrowsForWrongAdd() {
    CollisionGrid typedGrid(2.0, 6, 2, true);
    int gridIndex = typedGrid.indexAtGridPosition(4, 5);
    int offset = typedGrid.entryOffsetAt(gridIndex);

    assertTrue(typedGrid.add(gridIndex, 7, static_cast<int16_t>(3)), "CollisionGrid typed first add");
    assertTrue(typedGrid.add(gridIndex, -8, static_cast<int16_t>(5)), "CollisionGrid typed second add");
    assertTrue(!typedGrid.add(gridIndex, 9, static_cast<int16_t>(6)), "CollisionGrid typed overflow");
    assertTrue(typedGrid.countAt(gridIndex) == 2, "CollisionGrid typed count");
    assertTrue(typedGrid.indexAtOffset(offset) == 7, "CollisionGrid typed first index");
    assertTrue(typedGrid.typeAtOffset(offset) == 3, "CollisionGrid typed first type");
    assertTrue(typedGrid.indexAtOffset(offset + 1) == -8, "CollisionGrid typed second index");
    assertTrue(typedGrid.typeAtOffset(offset + 1) == 5, "CollisionGrid typed second type");

    CollisionGrid untypedGrid(1.0, 4, 1, false);
    bool threw = false;
    try {
        (void)untypedGrid.add(0, 1, static_cast<int16_t>(2));
    } catch (const std::logic_error&) {
        threw = true;
    }
    assertTrue(threw, "CollisionGrid typed add on untyped grid throws");
}

void testCellTopologyAddsNormalAndBoundaryLinks() {
    CellTopology topology(4);

    int edge = topology.addCellLink(
            0, 1, CellLinkCreationReason::SPLIT_SIBLING,
            0.25, 1.25, 0.5f, 0.01f);
    int boundary = topology.addBoundaryLink(0, 2.0, 0.75, -0.5, 0.25f, 0.0f);

    assertTrue(edge == 0, "CellTopology first edge index");
    assertTrue(boundary == 0, "CellTopology first boundary index");
    assertTrue(topology.edgeCount() == 1, "CellTopology edge count");
    assertTrue(isValidTopologyEdgeRuntimeId(topology.edgeRuntimeId(edge)),
            "CellTopology assigns a valid runtime edge identity");
    assertTrue(topology.boundaryCount() == 1, "CellTopology boundary count");
    assertTrue(topology.linkCount(0) == 2, "CellTopology source link count");
    assertTrue(topology.linkCount(1) == 1, "CellTopology target link count");
    assertTrue(topology.isLinked(0, 1), "CellTopology isLinked forward");
    assertTrue(topology.isLinked(1, 0), "CellTopology isLinked reverse");
    assertTrue(!topology.isLinked(0, 2), "CellTopology isLinked false for missing edge");
    assertTrue(topology.hasBoundaryLink(0), "CellTopology boundary present");
    assertTrue(!topology.hasBoundaryLink(1), "CellTopology boundary absent");
    assertTrue(topology.incidentEdgeIndex(0, 0) == edge,
            "CellTopology NORMAL_A exposes incident edge index");
    assertTrue(topology.incidentEdgeIndex(1, 0) == edge,
            "CellTopology NORMAL_B exposes incident edge index");
    assertTrue(topology.incidentEdgeIndex(0, 1) == -1,
            "CellTopology boundary adjacency has no normal edge index");
    assertTrue(topology.incidentEdgeIndex(-1, 0) == -1,
            "CellTopology incident edge rejects invalid cell");
    assertTrue(topology.incidentEdgeIndex(0, -1) == -1,
            "CellTopology incident edge rejects negative slot");
    assertTrue(topology.incidentEdgeIndex(0, topology.linkCount(0)) == -1,
            "CellTopology incident edge rejects past-end slot");
    assertTrue(topology.edgeCellA(edge) == 0, "CellTopology edge cellA");
    assertTrue(topology.edgeCellB(edge) == 1, "CellTopology edge cellB");
    assertTrue(
            topology.edgeCreationReason(edge)
                    == CellLinkCreationReason::SPLIT_SIBLING,
            "CellTopology edge creation reason");
    assertTrue(
            topology.edgeView().creationReason[edge]
                    == CellLinkCreationReason::SPLIT_SIBLING,
            "CellTopology edge view creation reason");
    assertNear(0.25, topology.edgeAngleA(edge), 0.0, "CellTopology edge angleA");
    assertNear(1.25, topology.edgeAngleB(edge), 0.0, "CellTopology edge angleB");
    assertNear(0.5f, topology.edgeTorsionalStiffness(edge), 0.0, "CellTopology edge stiffness");
    assertNear(0.01f, topology.edgeRestLength(edge), 0.0, "CellTopology edge rest length");
    assertTrue(topology.boundaryCell(boundary) == 0, "CellTopology boundary cell");
    assertNear(2.0, topology.boundaryAngleA(boundary), 0.0, "CellTopology boundary angle");
    assertNear(0.75, topology.boundaryAnchorX(boundary), 0.0, "CellTopology boundary anchor x");
    assertNear(-0.5, topology.boundaryAnchorY(boundary), 0.0, "CellTopology boundary anchor y");
    assertTrue(topology.validate(), "CellTopology add validate");
    std::array<std::uint32_t, 4> validationScratch{};
    assertTrue(
            topology.validateWithScratch(
                    validationScratch.data(),
                    static_cast<int>(validationScratch.size())),
            "CellTopology scratch validate");
    assertTrue(
            !topology.validateWithScratch(validationScratch.data(), 3),
            "CellTopology scratch validate rejects undersized storage");
    assertTrue(
            !topology.validateWithScratch(nullptr, 4),
            "CellTopology scratch validate rejects null storage");
}

void testCellTopologyMaxLinkCountAndInvalidAdds() {
    CellTopology topology(CellData::MAX_NEIGHBORS + 2);

    for (int linkIndex = 0; linkIndex < CellData::MAX_NEIGHBORS; linkIndex++) {
        int edge = topology.addCellLink(
                0, linkIndex + 1, CellLinkCreationReason::UNSPECIFIED,
                0.0, 0.0, 0.0f, 0.0f);
        assertTrue(edge == linkIndex, "CellTopology max fill edge index");
    }

    assertTrue(!topology.canMakeLink(0), "CellTopology full cell cannot make link");
    assertTrue(topology.addCellLink(
                       0,
                       CellData::MAX_NEIGHBORS + 1,
                       CellLinkCreationReason::UNSPECIFIED,
                       0.0,
                       0.0,
                       0.0f,
                       0.0f)
                    == -1,
            "CellTopology rejects overfull normal link");
    assertTrue(topology.addBoundaryLink(0, 0.0, 0.0, 0.0, 0.0f, 0.0f) == -1,
            "CellTopology rejects overfull boundary link");
    assertTrue(topology.addCellLink(
                       -1, 1, CellLinkCreationReason::UNSPECIFIED,
                       0.0, 0.0, 0.0f, 0.0f)
                    == -1,
            "CellTopology rejects invalid first cell");
    assertTrue(topology.addCellLink(
                       1, 1, CellLinkCreationReason::UNSPECIFIED,
                       0.0, 0.0, 0.0f, 0.0f)
                    == -1,
            "CellTopology rejects self link");
    assertTrue(topology.addCellLink(
                       1,
                       2,
                       static_cast<CellLinkCreationReason>(255),
                       0.0,
                       0.0,
                       0.0f,
                       0.0f)
                    == -1,
            "CellTopology rejects unknown creation reason");
    assertTrue(topology.validate(), "CellTopology max validate");
}

void testCellTopologyRemoveNormalEdgeUpdatesAdjacencyRefs() {
    CellTopology topology(5);
    int first = topology.addCellLink(
            0, 1, CellLinkCreationReason::SPLIT_SIBLING,
            0.1, 1.1, 0.2f, 0.01f);
    int second = topology.addCellLink(
            2, 3, CellLinkCreationReason::GLUEOCYTE_COLLISION,
            0.2, 1.2, 0.3f, 0.02f);
    const TopologyEdgeRuntimeId removedRuntimeId =
            topology.edgeRuntimeId(first);
    const TopologyEdgeRuntimeId movedRuntimeId =
            topology.edgeRuntimeId(second);
    topology.setEdgeRenderOffsets(second, 1.0f, 2.0f, 3.0f, 4.0f);

    topology.removeCellLink(first);

    assertTrue(topology.edgeCount() == 1, "CellTopology remove normal edge count");
    assertTrue(topology.edgeCellA(0) == 2, "CellTopology remove moved cellA");
    assertTrue(topology.edgeCellB(0) == 3, "CellTopology remove moved cellB");
    assertTrue(
            topology.edgeCreationReason(0)
                    == CellLinkCreationReason::GLUEOCYTE_COLLISION,
            "CellTopology compaction moves the creation reason");
    assertTrue(topology.edgeRuntimeId(0) == movedRuntimeId
                    && topology.edgeRuntimeId(0) != removedRuntimeId,
            "CellTopology compaction moves the surviving edge identity");
    assertNear(0.2, topology.edgeAngleA(0), 0.0, "CellTopology remove moved angleA");
    assertNear(1.2, topology.edgeAngleB(0), 0.0, "CellTopology remove moved angleB");
    assertNear(1.0f, topology.edgeRenderStartOffsetAX(0), 0.0, "CellTopology remove moved render start x");
    assertTrue(topology.linkCount(0) == 0, "CellTopology remove old cellA link count");
    assertTrue(topology.linkCount(1) == 0, "CellTopology remove old cellB link count");
    assertTrue(topology.linkCount(2) == 1, "CellTopology remove moved cellA link count");
    assertTrue(topology.linkCount(3) == 1, "CellTopology remove moved cellB link count");
    assertTrue(topology.incidentEdgeIndex(2, 0) == 0,
            "CellTopology remove updates moved NORMAL_A incident edge index");
    assertTrue(topology.incidentEdgeIndex(3, 0) == 0,
            "CellTopology remove updates moved NORMAL_B incident edge index");
    assertTrue(topology.isLinked(2, 3), "CellTopology remove keeps moved link");
    assertTrue(!topology.isLinked(0, 1), "CellTopology remove drops old link");
    assertTrue(topology.validate(), "CellTopology remove normal validate");
}

void testCellTopologyRuntimeIdentityLifecycle() {
    CellTopology topology(6);
    assertTrue(topology.addCellLink(
                       -1, 0, CellLinkCreationReason::UNSPECIFIED,
                       0.0, 0.0, 0.0f, 0.0f)
                    == -1,
            "Rejected topology edge does not enter the identity lifecycle");
    topology.addBoundaryLink(5, 0.0, 0.0, 0.0, 0.0f, 0.0f);

    const int first = topology.addCellLink(
            0, 1, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    const int second = topology.addCellLink(
            2, 3, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    const TopologyEdgeRuntimeId firstId = topology.edgeRuntimeId(first);
    const TopologyEdgeRuntimeId secondId = topology.edgeRuntimeId(second);
    assertTrue(firstId == 1 && secondId == 2,
            "Normal edges receive distinct monotonic IDs without RNG or boundary gaps");

    topology.moveCellIndex(3, 4);
    assertTrue(topology.edgeRuntimeId(second) == secondId,
            "Retargeting a compact Cell slot preserves edge identity");
    topology.removeCellLink(first);
    assertTrue(topology.edgeRuntimeId(0) == secondId,
            "Removing another edge preserves the compacted survivor identity");

    const int recreated = topology.addCellLink(
            0, 1, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    const TopologyEdgeRuntimeId recreatedId =
            topology.edgeRuntimeId(recreated);
    assertTrue(recreatedId > secondId && recreatedId != firstId,
            "Recreating the same endpoint pair receives a fresh edge identity");

    topology.clearAll();
    const int afterReset = topology.addCellLink(
            0, 1, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    assertTrue(topology.edgeRuntimeId(afterReset) > recreatedId,
            "Clearing a world cannot resurrect a retired edge identity");
    assertTrue(topology.validate(),
            "CellTopology runtime identity lifecycle remains valid");
}

void testCellTopologyRemoveBoundaryUpdatesAdjacencyRefs() {
    CellTopology topology(4);
    int first = topology.addBoundaryLink(0, 0.1, 1.0, 2.0, 0.3f, 0.0f);
    int second = topology.addBoundaryLink(2, 0.2, 3.0, 4.0, 0.4f, 0.01f);
    topology.setBoundaryRenderStartOffset(second, 5.0f, 6.0f);

    topology.removeBoundaryLink(first);

    assertTrue(topology.boundaryCount() == 1, "CellTopology remove boundary count");
    assertTrue(topology.boundaryCell(0) == 2, "CellTopology remove moved boundary cell");
    assertNear(0.2, topology.boundaryAngleA(0), 0.0, "CellTopology remove moved boundary angle");
    assertNear(3.0, topology.boundaryAnchorX(0), 0.0, "CellTopology remove moved boundary anchor x");
    assertNear(4.0, topology.boundaryAnchorY(0), 0.0, "CellTopology remove moved boundary anchor y");
    assertNear(5.0f, topology.boundaryRenderStartOffsetX(0), 0.0, "CellTopology remove moved boundary render x");
    assertTrue(topology.linkCount(0) == 0, "CellTopology remove boundary old count");
    assertTrue(topology.linkCount(2) == 1, "CellTopology remove boundary moved count");
    assertTrue(topology.hasBoundaryLink(2), "CellTopology remove boundary keeps moved link");
    assertTrue(topology.validate(), "CellTopology remove boundary validate");
}

void testCellTopologyRemoveBetweenAndRemoveAll() {
    CellTopology topology(5);
    topology.addCellLink(
            0, 1, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    topology.addCellLink(
            0, 2, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    topology.addBoundaryLink(0, 0.0, 1.0, 0.0, 0.0f, 0.0f);

    assertTrue(topology.removeCellLinkBetween(0, 2), "CellTopology remove between reports success");

    assertTrue(!topology.isLinked(0, 2), "CellTopology remove between drops selected link");
    assertTrue(topology.isLinked(0, 1), "CellTopology remove between keeps other normal link");
    assertTrue(topology.hasBoundaryLink(0), "CellTopology remove between keeps boundary");
    assertTrue(topology.validate(), "CellTopology remove between validate");

    topology.removeAllLinksOfCell(0);

    assertTrue(topology.edgeCount() == 0, "CellTopology remove all normal count");
    assertTrue(topology.boundaryCount() == 0, "CellTopology remove all boundary count");
    assertTrue(topology.linkCount(0) == 0, "CellTopology remove all source count");
    assertTrue(topology.linkCount(1) == 0, "CellTopology remove all peer count");
    assertTrue(topology.validate(), "CellTopology remove all validate");
}

void testCellTopologyMoveCellIndexAndRebuildAdjacency() {
    CellTopology topology(6);
    topology.addCellLink(
            4, 1, CellLinkCreationReason::UNSPECIFIED,
            0.4, 1.4, 0.2f, 0.01f);
    topology.addCellLink(
            2, 4, CellLinkCreationReason::UNSPECIFIED,
            0.2, 1.2, 0.3f, 0.02f);
    topology.addBoundaryLink(4, 0.7, 0.8, 0.9, 0.4f, 0.03f);

    topology.moveCellIndex(4, 0);

    assertTrue(topology.linkCount(4) == 0, "CellTopology move old cell count");
    assertTrue(topology.linkCount(0) == 3, "CellTopology move new cell count");
    assertTrue(topology.isLinked(0, 1), "CellTopology move first link");
    assertTrue(topology.isLinked(2, 0), "CellTopology move second link");
    assertTrue(topology.hasBoundaryLink(0), "CellTopology move boundary");
    assertTrue(topology.edgeCellA(0) == 0, "CellTopology move edge A retarget");
    assertTrue(topology.edgeCellB(1) == 0, "CellTopology move edge B retarget");
    assertTrue(topology.boundaryCell(0) == 0, "CellTopology move boundary retarget");
    assertTrue(topology.validate(), "CellTopology move validate");

    topology.clearAll();
    assertTrue(topology.edgeCount() == 0, "CellTopology clear all edge count");
    assertTrue(topology.boundaryCount() == 0, "CellTopology clear all boundary count");
    assertTrue(topology.linkCount(0) == 0, "CellTopology clear all cell count");
    assertTrue(topology.validate(), "CellTopology clear all validate");
}

void testCellTopologyCollectLinksUsesCellLocalOrientation() {
    CellTopology topology(5);
    topology.addCellLink(
            0, 1, CellLinkCreationReason::SPLIT_SIBLING,
            0.1, 1.1, 0.2f, 0.01f);
    topology.addCellLink(
            2, 1, CellLinkCreationReason::GLUEOCYTE_COLLISION,
            0.2, 1.2, 0.3f, 0.02f);
    topology.addBoundaryLink(1, 0.7, 0.8, 0.9, 0.4f, 0.03f);

    std::array<CellTopology::CellLinkSnapshot, CellData::MAX_NEIGHBORS> snapshots{};
    int count = topology.collectLinksOfCell(1, snapshots);

    assertTrue(count == 3, "CellTopology collect link count");
    assertTrue(!snapshots[0].isBoundary, "CellTopology collect first normal");
    assertTrue(
            snapshots[0].creationReason
                    == CellLinkCreationReason::SPLIT_SIBLING,
            "CellTopology collect first creation reason");
    assertTrue(snapshots[0].connectedCellIndex == 0, "CellTopology collect NORMAL_B connected cell");
    assertNear(1.1, snapshots[0].angleA, 0.0, "CellTopology collect NORMAL_B local angle");
    assertNear(0.1, snapshots[0].angleB, 0.0, "CellTopology collect NORMAL_B peer angle");
    assertTrue(!snapshots[1].isBoundary, "CellTopology collect second normal");
    assertTrue(
            snapshots[1].creationReason
                    == CellLinkCreationReason::GLUEOCYTE_COLLISION,
            "CellTopology collect second creation reason");
    assertTrue(snapshots[1].connectedCellIndex == 2, "CellTopology collect second connected cell");
    assertNear(1.2, snapshots[1].angleA, 0.0, "CellTopology collect second local angle");
    assertNear(0.2, snapshots[1].angleB, 0.0, "CellTopology collect second peer angle");
    assertTrue(snapshots[2].isBoundary, "CellTopology collect boundary");
    assertTrue(
            snapshots[2].creationReason
                    == CellLinkCreationReason::UNSPECIFIED,
            "CellTopology collect boundary has no creation reason");
    assertTrue(snapshots[2].connectedCellIndex == CellData::BOUNDARY_CONNECTION_INDEX, "CellTopology collect boundary sentinel");
    assertNear(0.7, snapshots[2].angleA, 0.0, "CellTopology collect boundary angle");
    assertNear(0.8, snapshots[2].boundaryAnchorX, 0.0, "CellTopology collect boundary anchor x");
    assertNear(0.9, snapshots[2].boundaryAnchorY, 0.0, "CellTopology collect boundary anchor y");
}

void testCellTopologyStructuralRevisionTracksTopologyButNotRenderOffsets() {
    CellTopology topology(6);
    const std::uint64_t initialRevision = topology.structuralRevision();
    assertTrue(initialRevision != 0,
            "CellTopology constructor establishes structural revision");

    assertTrue(topology.addCellLink(
                       -1, 0, CellLinkCreationReason::UNSPECIFIED,
                       0.0, 0.0, 0.0f, 0.0f)
                    == -1,
            "CellTopology rejects invalid edge before revision check");
    assertTrue(topology.structuralRevision() == initialRevision,
            "Rejected topology mutation preserves structural revision");

    const int firstEdge = topology.addCellLink(
            0, 2, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    const std::uint64_t afterEdge = topology.structuralRevision();
    assertTrue(afterEdge > initialRevision,
            "Normal edge addition advances structural revision");
    topology.setEdgeRenderOffsets(firstEdge, 1.0f, 2.0f, 3.0f, 4.0f);
    assertTrue(topology.structuralRevision() == afterEdge,
            "Normal edge render offsets preserve structural revision");

    const int boundary = topology.addBoundaryLink(
            0, 0.0, 1.0, 2.0, 0.0f, 0.0f);
    const std::uint64_t afterBoundary = topology.structuralRevision();
    assertTrue(afterBoundary > afterEdge,
            "Boundary addition advances structural revision");
    topology.setBoundaryRenderStartOffset(boundary, 5.0f, 6.0f);
    assertTrue(topology.structuralRevision() == afterBoundary,
            "Boundary render offsets preserve structural revision");

    topology.moveCellIndex(2, 3);
    const std::uint64_t afterMove = topology.structuralRevision();
    assertTrue(afterMove > afterBoundary,
            "Cell index move advances structural revision");
    topology.removeCellLink(firstEdge);
    const std::uint64_t afterRemove = topology.structuralRevision();
    assertTrue(afterRemove > afterMove,
            "Normal edge removal advances structural revision");
    topology.removeBoundaryLink(boundary);
    const std::uint64_t afterBoundaryRemove = topology.structuralRevision();
    assertTrue(afterBoundaryRemove > afterRemove,
            "Boundary removal advances structural revision");
    topology.clearAll();
    assertTrue(topology.structuralRevision() > afterBoundaryRemove,
            "Topology reset advances structural revision");
}

#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
void testCellSignalAdjacencyBuildsGlobalEdgeOrderedCellRanges() {
    CellTopology topology(6);
    topology.addCellLink(
            0, 2, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f); // Removed below.
    topology.addCellLink(
            1, 0, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    topology.addBoundaryLink(0, 0.0, 1.0, 2.0, 0.0f, 0.0f);
    topology.addCellLink(
            0, 3, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    topology.removeCellLink(0); // Swap-removes edge 2 into global index 0.

    CellSignalAdjacency adjacency(6);
    assertTrue(adjacency.ensureCurrent(topology, 4),
            "Signal adjacency builds its first derived view");
    assertTrue(!adjacency.ensureCurrent(topology, 4),
            "Signal adjacency reuses matching topology revision");

    const CellSignalAdjacency::View view = adjacency.view();
    assertTrue(view.cellCount == 4 && view.referenceCount == 4,
            "Signal adjacency contains two references per normal edge only");
    assertTrue(view.offsets[0] == 0 && view.offsets[1] == 2,
            "Signal adjacency Cell zero owns two references");
    assertTrue(view.offsets[2] == 3 && view.offsets[3] == 3
                    && view.offsets[4] == 4,
            "Signal adjacency prefix offsets include empty Cell ranges");

    const auto cellZeroFirst = view.references[view.offsets[0]];
    const auto cellZeroSecond = view.references[view.offsets[0] + 1U];
    assertTrue(CellSignalAdjacency::edgeIndex(cellZeroFirst) == 0
                    && !CellSignalAdjacency::ownerIsCellB(cellZeroFirst),
            "Signal adjacency preserves moved global edge zero as endpoint A");
    assertTrue(CellSignalAdjacency::edgeIndex(cellZeroSecond) == 1
                    && CellSignalAdjacency::ownerIsCellB(cellZeroSecond),
            "Signal adjacency preserves ascending edge order and B orientation");

    const std::uint64_t revision = topology.structuralRevision();
    topology.setEdgeRenderOffsets(0, 1.0f, 2.0f, 3.0f, 4.0f);
    assertTrue(topology.structuralRevision() == revision,
            "Signal adjacency source ignores render-only mutation");
    assertTrue(!adjacency.ensureCurrent(topology, 4),
            "Render-only mutation does not rebuild signal adjacency");

    topology.addCellLink(
            2, 3, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    assertTrue(adjacency.ensureCurrent(topology, 4),
            "Structural mutation rebuilds signal adjacency");
    const CellSignalAdjacency::View rebuilt = adjacency.view();
    assertTrue(rebuilt.referenceCount == 6,
            "Rebuilt signal adjacency includes the new normal edge");
}
#endif

struct SweepPairCollector {
    std::vector<std::pair<int, int>> pairs;
};

struct OrderedSweepPairCollector {
    std::vector<std::pair<int, int>> pairs;
};

void collectSweepPair(void* context, int firstCellIndex, int secondCellIndex) {
    auto* collector = static_cast<SweepPairCollector*>(context);
    if (firstCellIndex > secondCellIndex) {
        std::swap(firstCellIndex, secondCellIndex);
    }
    collector->pairs.emplace_back(firstCellIndex, secondCellIndex);
}

void collectSweepOverlapPair(void* context,
        int firstCellIndex,
        int secondCellIndex,
        double,
        double,
        double,
        double,
        double) {
    collectSweepPair(context, firstCellIndex, secondCellIndex);
}

void collectOrderedSweepPair(void* context, int firstCellIndex, int secondCellIndex) {
    auto* collector = static_cast<OrderedSweepPairCollector*>(context);
    collector->pairs.emplace_back(firstCellIndex, secondCellIndex);
}

void collectOrderedOverlaps(CellSweepBroadphase& broadphase, ExactOverlapPairBuffer& output) {
    output.clear();
    broadphase.forEachOverlappingPairInline(
            [&output](int firstCellIndex,
                    int secondCellIndex,
                    double distanceX,
                    double distanceY,
                    double,
                    double,
                    double) {
                output.push(firstCellIndex, secondCellIndex, distanceX, distanceY);
            });
}

void assertOrderedOverlapBuffersEqual(const ExactOverlapPairBuffer& expected,
        const ExactOverlapPairBuffer& actual,
        const std::string& context) {
    assertTrue(expected.firstCellIds == actual.firstCellIds,
            context + " first-cell order");
    assertTrue(expected.secondCellIds == actual.secondCellIds,
            context + " second-cell order");
    assertTrue(expected.distanceX == actual.distanceX,
            context + " distanceX values");
    assertTrue(expected.distanceY == actual.distanceY,
            context + " distanceY values");
}

std::vector<std::pair<int, int>> normalizedOverlapPairs(
        const ExactOverlapPairBuffer& overlaps) {
    std::vector<std::pair<int, int>> result;
    result.reserve(overlaps.size());
    for (std::size_t pairIndex = 0; pairIndex < overlaps.size(); pairIndex++) {
        int first = overlaps.firstCellIds[pairIndex];
        int second = overlaps.secondCellIds[pairIndex];
        if (first > second) {
            std::swap(first, second);
        }
        result.emplace_back(first, second);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::pair<int, int>> bruteForceOverlapPairs(const CellData& cellData) {
    std::vector<std::pair<int, int>> result;
    for (int first = 0; first < cellData.mCellCount; first++) {
        for (int second = first + 1; second < cellData.mCellCount; second++) {
            const double distanceX =
                    cellData.mPositionX[first] - cellData.mPositionX[second];
            const double distanceY =
                    cellData.mPositionY[first] - cellData.mPositionY[second];
            const double contactDistance =
                    cellData.mRadius[first] + cellData.mRadius[second];
            if (distanceX * distanceX + distanceY * distanceY
                    < contactDistance * contactDistance) {
                result.emplace_back(first, second);
            }
        }
    }
    return result;
}

void testIncrementalBucketedF64Source32SweepMatchesBruteForceDuringChurn() {
    CellData cellData(192);
    std::uint32_t randomState = 0xa511e9b3U;
    auto randomUnit = [&randomState]() {
        randomState = randomState * 747796405U + 2891336453U;
        return static_cast<double>(randomState)
                / static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    };
    auto appendRandomCell = [&]() {
        const int cellIndex = addAliveCell(cellData, CellType::PHAGOCYTE);
        assertTrue(cellIndex >= 0, "Incremental bucketed test cell allocation");
        cellData.mPositionX[cellIndex] = randomUnit() * 2.0 - 1.0;
        cellData.mPositionY[cellIndex] = randomUnit() * 1.2 - 0.6;
        cellData.mRadius[cellIndex] = 0.004 + randomUnit() * 0.026;
        return cellIndex;
    };
    for (int cellIndex = 0; cellIndex < 96; cellIndex++) {
        appendRandomCell();
    }

    CellSweepBroadphase broadphase(2.0 * CellData::MAX_CELL_RADIUS);
    broadphase.setScanAlgorithm(
            CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP);
    ExactOverlapPairBuffer inlineOverlaps;
    ExactOverlapPairBuffer bufferedOverlaps;

    for (int frame = 0; frame < 48; frame++) {
        for (int cellIndex = 0; cellIndex < cellData.mCellCount; cellIndex++) {
            const int phase = (cellIndex * 19 + frame * 11) % 23 - 11;
            cellData.mPositionX[cellIndex] += static_cast<double>(phase) * 0.00006;
            cellData.mPositionY[cellIndex] -= static_cast<double>(phase) * 0.00009;
        }

        if (frame == 5) {
            cellData.mPositionY[3] += 0.061;
        } else if (frame == 9) {
            cellData.mPositionY[4] += 0.31;
        } else if (frame == 13) {
            cellData.resizeActiveCellCount(80);
        } else if (frame == 14) {
            for (int added = 0; added < 12; added++) {
                appendRandomCell();
            }
        } else if (frame == 18) {
            const int replacementIndex = cellData.mCellCount - 1;
            cellData.resizeActiveCellCount(replacementIndex);
            const int replacement = appendRandomCell();
            assertTrue(replacement == replacementIndex,
                    "Incremental bucketed same-count replacement keeps dense ID");
            cellData.mPositionY[replacement] += 0.45;
        } else if (frame == 23) {
            cellData.resizeActiveCellCount(0);
        } else if (frame == 24) {
            for (int added = 0; added < 32; added++) {
                appendRandomCell();
            }
        } else if (frame == 30) {
            cellData.mPositionX[0] = -0.0;
            cellData.mPositionX[1] = 0.0;
            cellData.mPositionX[2] = 0.0;
            cellData.mPositionY[0] = 0.0;
            cellData.mPositionY[1] = 0.0;
            cellData.mPositionY[2] = 0.0;
            cellData.mRadius[0] = 0.03;
            cellData.mRadius[1] = 0.03;
            cellData.mRadius[2] = 0.03;
        } else if (frame == 35) {
            broadphase.setMaxDiameter(2.0 * CellData::MAX_CELL_RADIUS);
        }

        broadphase.buildSelectedLayout(cellData);
        inlineOverlaps.clear();
        broadphase.forEachOverlappingPairInline(
                [&cellData, &inlineOverlaps](int first,
                        int second,
                        double distanceX,
                        double distanceY,
                        double distanceSq,
                        double firstRadius,
                        double secondRadius) {
                    assertTrue(distanceX
                                    == cellData.mPositionX[first]
                                            - cellData.mPositionX[second],
                            "Incremental bucketed distanceX geometry");
                    assertTrue(distanceY
                                    == cellData.mPositionY[first]
                                            - cellData.mPositionY[second],
                            "Incremental bucketed distanceY geometry");
                    assertTrue(distanceSq
                                    == distanceX * distanceX
                                            + distanceY * distanceY,
                            "Incremental bucketed distance-squared geometry");
                    assertTrue(firstRadius == cellData.mRadius[first],
                            "Incremental bucketed first radius geometry");
                    assertTrue(secondRadius == cellData.mRadius[second],
                            "Incremental bucketed second radius geometry");
                    inlineOverlaps.push(first, second, distanceX, distanceY);
                });
        broadphase.collectOverlappingPairs(bufferedOverlaps);

        const std::vector<std::pair<int, int>> expected =
                bruteForceOverlapPairs(cellData);
        const std::vector<std::pair<int, int>> actualInline =
                normalizedOverlapPairs(inlineOverlaps);
        const std::vector<std::pair<int, int>> actualBuffered =
                normalizedOverlapPairs(bufferedOverlaps);
        const std::string frameContext = "Incremental bucketed frame "
                + std::to_string(frame);
        assertTrue(actualInline == expected, frameContext + " inline overlap set");
        assertTrue(actualBuffered == expected, frameContext + " buffered overlap set");
        assertTrue(std::adjacent_find(actualInline.begin(), actualInline.end())
                        == actualInline.end(),
                frameContext + " emits no duplicate pairs");

        if (frame == 31 && !expected.empty()) {
            bool visitorThrew = false;
            try {
                broadphase.forEachOverlappingPairInline(
                        [](int, int, double, double, double, double, double) {
                            throw std::runtime_error("intentional visitor failure");
                        });
            } catch (const std::runtime_error&) {
                visitorThrew = true;
            }
            assertTrue(visitorThrew,
                    "Incremental bucketed scan propagates visitor exceptions");
            bufferedOverlaps.clear();
            broadphase.collectOverlappingPairs(bufferedOverlaps);
            assertTrue(normalizedOverlapPairs(bufferedOverlaps) == expected,
                    "Incremental bucketed sentinel cleanup after exception");
        }
    }
}

void testIncrementalBucketedF64Source32StrictTangencyAndRowRange() {
    CellData cellData(3);
    for (int cellIndex = 0; cellIndex < 3; cellIndex++) {
        addAliveCell(cellData, CellType::PHAGOCYTE);
        cellData.mPositionY[cellIndex] = -0.0;
        cellData.mRadius[cellIndex] = 0.01;
    }
    cellData.mPositionX[0] = 0.0;
    cellData.mPositionX[1] = 0.02;
    cellData.mPositionX[2] = std::nextafter(0.02, 0.0);

    CellSweepBroadphase broadphase(2.0 * CellData::MAX_CELL_RADIUS);
    broadphase.setScanAlgorithm(
            CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP);
    broadphase.buildSelectedLayout(cellData);
    ExactOverlapPairBuffer overlaps;
    broadphase.collectOverlappingPairs(overlaps);
    const std::vector<std::pair<int, int>> pairs = normalizedOverlapPairs(overlaps);
    assertTrue(std::find(pairs.begin(), pairs.end(), std::make_pair(0, 1))
                    == pairs.end(),
            "Incremental bucketed strict predicate excludes exact tangency");
    assertTrue(std::find(pairs.begin(), pairs.end(), std::make_pair(0, 2))
                    != pairs.end(),
            "Incremental bucketed strict predicate includes sub-tangent overlap");

    CellData overflowData(1);
    const int overflowCell = addAliveCell(overflowData, CellType::PHAGOCYTE);
    overflowData.mPositionX[overflowCell] = 0.0;
    overflowData.mPositionY[overflowCell] =
            (static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 1024.0)
            * (2.0 * CellData::MAX_CELL_RADIUS);
    overflowData.mRadius[overflowCell] = 0.01;
    bool rejectedOverflow = false;
    try {
        broadphase.clear();
        broadphase.buildSelectedLayout(overflowData);
    } catch (const std::overflow_error&) {
        rejectedOverflow = true;
    }
    assertTrue(rejectedOverflow,
            "Incremental bucketed rejects row IDs outside int32 range");

    CellData transitionData(1);
    const int transitionCell = addAliveCell(transitionData, CellType::PHAGOCYTE);
    transitionData.mPositionX[transitionCell] = 0.0;
    transitionData.mPositionY[transitionCell] =
            (static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 0.25)
            * (2.0 * CellData::MAX_CELL_RADIUS);
    transitionData.mRadius[transitionCell] = 0.01;
    broadphase.clear();
    broadphase.buildSelectedLayout(transitionData);
    transitionData.mPositionY[transitionCell] =
            (static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 1.25)
            * (2.0 * CellData::MAX_CELL_RADIUS);
    rejectedOverflow = false;
    try {
        broadphase.buildSelectedLayout(transitionData);
    } catch (const std::overflow_error&) {
        rejectedOverflow = true;
    }
    assertTrue(rejectedOverflow,
            "Incremental bucketed rejects overflow during adjacent-row classification");
}

void testIncrementalBucketedPreparedSourceIsConsumedOnce() {
    CellData cellData(2);
    for (int cellIndex = 0; cellIndex < 2; cellIndex++) {
        addAliveCell(cellData, CellType::PHAGOCYTE);
        cellData.mPositionY[cellIndex] = 0.0;
        cellData.mRadius[cellIndex] = 0.02;
    }
    cellData.mPositionX[0] = 0.0;
    cellData.mPositionX[1] = 0.01;

    CellSweepBroadphase broadphase(2.0 * CellData::MAX_CELL_RADIUS);
    broadphase.setScanAlgorithm(
            CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP);
    CellIncrementalBucketedF64Broadphase::PackedSource32* const prepared =
            broadphase.beginIncrementalPackedSourceWrite(2);
    prepared[0] = {0.0, 0.0, 0.02, 0.0};
    prepared[1] = {1.0, 0.0, 0.02, 0.0};
    broadphase.commitIncrementalPackedSourceWrite();

    ExactOverlapPairBuffer overlaps;
    broadphase.buildSelectedLayout(cellData);
    broadphase.collectOverlappingPairs(overlaps);
    assertTrue(overlaps.size() == 0,
            "Incremental bucketed consumes the prepared source");

    broadphase.buildSelectedLayout(cellData);
    broadphase.collectOverlappingPairs(overlaps);
    assertTrue(normalizedOverlapPairs(overlaps)
                    == std::vector<std::pair<int, int>>{{0, 1}},
            "Incremental bucketed falls back to current SoA after consuming prepared source");
}

void testCellSweepBroadphaseBuildsSameAndAdjacentRowPairs() {
    CellData cellData(5);
    int a = addAliveCell(cellData, CellType::PHAGOCYTE);
    int b = addAliveCell(cellData, CellType::PHAGOCYTE);
    int c = addAliveCell(cellData, CellType::PHAGOCYTE);
    int far = addAliveCell(cellData, CellType::PHAGOCYTE);
    int dead = addAliveCell(cellData, CellType::PHAGOCYTE);

    cellData.mPositionX[a] = 0.0;
    cellData.mPositionY[a] = 0.0;
    cellData.mRadius[a] = 0.02;
    cellData.mPositionX[b] = 0.03;
    cellData.mPositionY[b] = 0.0;
    cellData.mRadius[b] = 0.02;
    cellData.mPositionX[c] = -0.05;
    cellData.mPositionY[c] = 0.065;
    cellData.mRadius[c] = 0.02;
    cellData.mPositionX[far] = 0.2;
    cellData.mPositionY[far] = 0.0;
    cellData.mRadius[far] = 0.02;
    cellData.mPositionX[dead] = 0.015;
    cellData.mPositionY[dead] = 0.0;
    cellData.mRadius[dead] = 0.02;
    cellData.markCellAsDead(dead);

    CellSweepBroadphase sweep(2.0 * CellData::MAX_CELL_RADIUS);
    sweep.buildSelectedLayout(cellData);
    SweepPairCollector collector;
    sweep.forEachOverlappingPair(&collector, collectSweepOverlapPair);
    std::sort(collector.pairs.begin(), collector.pairs.end());

    assertTrue(collector.pairs.size() == 3, "CellSweepBroadphase overlap pair count");
    assertTrue(collector.pairs[0] == std::make_pair(a, b), "CellSweepBroadphase same-row overlap pair");
    assertTrue(collector.pairs[1] == std::make_pair(a, dead), "CellSweepBroadphase dense dead overlap pair");
    assertTrue(collector.pairs[2] == std::make_pair(b, dead), "CellSweepBroadphase dense second dead overlap pair");
}

void verifyCompactSweepMatchesBaselineOrderAcrossLayoutChanges(
        CellOverlapScanAlgorithm algorithm,
        const std::string& algorithmName) {
    CellData cellData(16);
    for (int cellIndex = 0; cellIndex < 12; cellIndex++) {
        addAliveCell(cellData, CellType::PHAGOCYTE);
    }

    const std::array<double, 12> initialX = {
            -0.060, -0.025, 0.010, 0.045,
            -0.055, -0.015, 0.020, 0.060,
            -0.040, 0.000, 0.035, 0.070
    };
    const std::array<double, 12> initialY = {
            -0.101, -0.099, -0.004, 0.001,
            0.047, 0.099, 0.101, 0.149,
            0.199, 0.201, 0.305, 0.311
    };
    const std::array<double, 12> initialRadius = {
            0.050, 0.020, 0.035, 0.015,
            0.045, 0.025, 0.050, 0.010,
            0.040, 0.030, 0.050, 0.020
    };
    for (int cellIndex = 0; cellIndex < 12; cellIndex++) {
        cellData.mPositionX[cellIndex] = initialX[cellIndex];
        cellData.mPositionY[cellIndex] = initialY[cellIndex];
        cellData.mRadius[cellIndex] = initialRadius[cellIndex];
    }

    CellSweepBroadphase baseline(2.0 * CellData::MAX_CELL_RADIUS);
    CellSweepBroadphase compact(2.0 * CellData::MAX_CELL_RADIUS);
    compact.setScanAlgorithm(algorithm);

    ExactOverlapPairBuffer baselineOverlaps;
    ExactOverlapPairBuffer compactOverlaps;
    ExactOverlapPairBuffer compactBufferedOverlaps;
    for (int frame = 0; frame < 20; frame++) {
        if (frame == 3) {
            baseline.setMaxDiameter(2.0 * CellData::MAX_CELL_RADIUS);
            compact.setMaxDiameter(2.0 * CellData::MAX_CELL_RADIUS);
        }
        if (frame == 4) {
            cellData.mPositionY[0] = 0.002;
        } else if (frame == 8) {
            cellData.mPositionY[7] = 0.205;
        } else if (frame == 12) {
            cellData.mPositionY[10] = 0.095;
        } else if (frame == 15) {
            const int added = addAliveCell(cellData, CellType::PHAGOCYTE);
            cellData.mPositionX[added] = 0.018;
            cellData.mPositionY[added] = 0.1005;
            cellData.mRadius[added] = 0.032;
        } else if (frame == 17) {
            cellData.resizeActiveCellCount(9);
        } else if (frame == 18) {
            cellData.resizeActiveCellCount(0);
        } else if (frame == 19) {
            const int first = addAliveCell(cellData, CellType::PHAGOCYTE);
            const int second = addAliveCell(cellData, CellType::PHAGOCYTE);
            cellData.mPositionX[first] = -0.01;
            cellData.mPositionY[first] = 0.049;
            cellData.mRadius[first] = 0.04;
            cellData.mPositionX[second] = 0.01;
            cellData.mPositionY[second] = 0.051;
            cellData.mRadius[second] = 0.03;
        }

        for (int cellIndex = 0; cellIndex < cellData.mCellCount; cellIndex++) {
            const int offset = (cellIndex * 7 + frame * 3) % 5 - 2;
            cellData.mPositionX[cellIndex] += static_cast<double>(offset) * 0.0002;
        }

        baseline.buildSelectedLayout(cellData);
        compact.buildSelectedLayout(cellData);

        OrderedSweepPairCollector baselineCandidates;
        OrderedSweepPairCollector compactCandidates;
        baseline.forEachCandidatePair(&baselineCandidates, collectOrderedSweepPair);
        compact.forEachCandidatePair(&compactCandidates, collectOrderedSweepPair);
        assertTrue(baselineCandidates.pairs == compactCandidates.pairs,
                algorithmName + " candidate order at frame " + std::to_string(frame));

        collectOrderedOverlaps(baseline, baselineOverlaps);
        collectOrderedOverlaps(compact, compactOverlaps);
        assertOrderedOverlapBuffersEqual(
                baselineOverlaps,
                compactOverlaps,
                algorithmName + " fused sweep at frame " + std::to_string(frame));

        compact.collectOverlappingPairs(compactBufferedOverlaps);
        assertOrderedOverlapBuffersEqual(
                baselineOverlaps,
                compactBufferedOverlaps,
                algorithmName + " buffered sweep at frame " + std::to_string(frame));

        if (frame == 13) {
            compact.setScanAlgorithm(CellOverlapScanAlgorithm::BASELINE_SWEEP);
            ExactOverlapPairBuffer invalidatedOverlaps;
            collectOrderedOverlaps(compact, invalidatedOverlaps);
            assertTrue(invalidatedOverlaps.size() == 0,
                    algorithmName + " invalidates storage when switching away");
            compact.setScanAlgorithm(algorithm);
            collectOrderedOverlaps(compact, invalidatedOverlaps);
            assertTrue(invalidatedOverlaps.size() == 0,
                    algorithmName + " does not reuse stale storage when switching back");
        }
    }
}

void testCellDataBasics() {
    CellData cellData(2);
    assertTrue(cellData.MAX_CELL_COUNT == 2, "CellData max count");
    assertTrue(cellData.RANDOM_CELLS_MAINTAIN_COUNT == 0, "CellData random maintain count");
    assertTrue(cellData.allocateNewCell() == 0, "CellData allocate first index");
    const CellRuntimeId firstRuntimeId = cellData.runtimeId(0);
    assertTrue(cellData.allocateNewCell() == 1, "CellData allocate second index");
    const CellRuntimeId secondRuntimeId = cellData.runtimeId(1);
    assertTrue(cellData.allocateNewCell() == -1, "CellData allocate over capacity");
    assertTrue(cellData.mCellCount == 2, "CellData cell count after failed allocate");
    assertTrue(isEntityCellRuntimeId(firstRuntimeId)
                    && isEntityCellRuntimeId(secondRuntimeId)
                    && secondRuntimeId > firstRuntimeId,
            "CellData allocates monotonic non-reserved runtime identities");

    cellData.markCellAsDead(0);
    assertTrue(cellData.mIsDead[0], "CellData mark dead");
    cellData.editActiveGene(0, [](cellularity::GeneBuilder& gene) {
        gene.setCellType(CellType::MYOCYTE);
    });
    cellData.markCellAsAlive(0);
    assertTrue(!cellData.mIsDead[0], "CellData mark alive");
    assertTrue(cellData.mActiveCellType[0] == CellType::MYOCYTE, "CellData mark alive refreshes active cell type");
    assertTrue(cellData.runtimeId(0) == firstRuntimeId,
            "CellData alive/dead state does not change entity identity");
}

void testCellDataKeepsDefaultGenomeTemplateImmutable() {
    CellData cellData(2);
    assertTrue(cellData.allocateNewCell() == 0,
            "Default template fixture allocates its first Cell");

    const cellularity::GenomeId defaultGenomeId = cellData.genomeId(0);
    const CellType defaultCellType = cellData.getActiveGeneView(0).cellType();
    assertTrue(cellData.genomeReferenceCount(0) == 2,
            "Default Genome is owned by CellData and the first Cell");

    const CellType mutatedCellType = defaultCellType == CellType::MYOCYTE
            ? CellType::PHAGOCYTE
            : CellType::MYOCYTE;
    assertTrue(cellData.editActiveGene(
                       0,
                       [=](cellularity::GeneBuilder& gene) {
                           gene.setCellType(mutatedCellType);
                       }),
            "First Cell Genome mutation succeeds");
    assertTrue(cellData.genomeId(0) != defaultGenomeId,
            "First Cell mutation detaches from the immutable default template");
    assertTrue(cellData.getActiveGeneView(0).cellType() == mutatedCellType,
            "Detached first Cell observes its mutation");

    assertTrue(cellData.allocateNewCell() == 1,
            "Default template fixture allocates a second Cell");
    assertTrue(cellData.genomeId(1) == defaultGenomeId,
            "A later Cell reuses the original default Genome record");
    assertTrue(cellData.getActiveGeneView(1).cellType() == defaultCellType,
            "A first-Cell mutation cannot poison later default Cells");
    assertTrue(cellData.genomeReferenceCount(1) == 2,
            "Default Genome retains exactly its template and Cell owners");
    assertTrue(cellData.genomePool().validateInvariants().isValid(),
            "Default template ownership preserves GenomePool invariants");
}

void testCellDataLargeClonePopulationSharesOneGenomeRecord() {
    constexpr int cloneCount = 2048;
    CellData cellData(cloneCount);
    cellData.resizeActiveCellCount(cloneCount);

    const cellularity::GenomeId sharedGenome = cellData.genomeId(0);
    for (int cellIndex = 1; cellIndex < cloneCount; ++cellIndex) {
        assertTrue(cellData.genomeId(cellIndex) == sharedGenome,
                "Default clone population shares one GenomeRecord");
    }
    assertTrue(cellData.genomeReferenceCount(0)
                    == static_cast<std::uint32_t>(cloneCount + 1),
            "Shared Genome reference count includes all clones and its template owner");
    assertTrue(cellData.genomePool().diagnostics().liveSlots == 1,
            "A large clone population stores one live GenomeRecord");

    cellData.resizeActiveCellCount(0);
    assertTrue(cellData.genomePool().diagnostics().liveSlots == 1
                    && cellData.genePool().diagnostics().defaultGeneReferences
                            == cellularity::FIXED_GENOME_GENE_COUNT,
            "Removing every clone leaves only the owned default template");
    assertTrue(cellData.genomePool().validateInvariants().isValid()
                    && cellData.genePool().validateInvariants().isValid(),
            "Clone population teardown balances both pools");
}

void testCellDataMoveLastCellForCompaction() {
    CellData cellData(2);
    int to = addAliveCell(cellData, CellType::PHAGOCYTE);
    int from = addAliveCell(cellData, CellType::CILIOCYTE);
    const CellRuntimeId fromRuntimeId = cellData.runtimeId(from);

    cellData.mPositionX[from] = 1.25;
    cellData.mPositionY[from] = -2.5;
    cellData.mPositionZ[from] = 0.125;
    cellData.mVelocityX[from] = 3.0;
    cellData.mVelocityY[from] = -4.0;
    cellData.mForceX[from] = 0.1;
    cellData.mForceY[from] = 0.2;
    cellData.mForceZ[from] = 0.3;
    cellData.mAngle[from] = 0.75;
    cellData.mAngleCytoplasm[from] = -0.25;
    cellData.mAngularVelocity[from] = 1.5;
    cellData.mAngularVelocityCytoplasm[from] = -1.5;
    cellData.mAge[from] = 7.0;
    cellData.mMass[from] = CellData::MAX_MASS;
    cellData.mLipids[from] = 0.25f;
    cellData.mColor[from] = {0.1f, 0.2f, 0.3f, 0.4f};
    cellData.mSignalSubstance[from][0] = 0.33f;
    cellData.mSignalSubstanceOut[from][0] = 0.44f;
    cellData.editActiveGene(from, [](cellularity::GeneBuilder& sourceGene) {
        sourceGene.setCellType(CellType::STEREOCYTE);
        sourceGene.setColor(0, 0.9f);
    });
    cellData.mPositionX[to] = 1.27;
    cellData.mPositionY[to] = -2.5;
    cellData.mRadius[from] = 0.02;
    cellData.mRadius[to] = 0.02;
    addCollisionContact(cellData, from, to);
    cellData.mFertilizationTarget[from] = 5;

    cellData.moveLastCellForCompaction(from, to);
    cellData.releaseLastCellSlot();

    assertNear(1.25, cellData.mPositionX[to], 0.0, "CellData copy position x");
    assertNear(-2.5, cellData.mPositionY[to], 0.0, "CellData copy position y");
    assertNear(0.125, cellData.mPositionZ[to], 0.0, "CellData copy position z");
    assertNear(3.0, cellData.mVelocityX[to], 0.0, "CellData copy velocity x");
    assertNear(0.3, cellData.mForceZ[to], 0.0, "CellData copy force z");
    assertNear(0.75, cellData.mAngle[to], 0.0, "CellData copy angle");
    assertNear(-1.5, cellData.mAngularVelocityCytoplasm[to], 0.0, "CellData copy cytoplasm angular velocity");
    assertNear(CellData::MAX_MASS, cellData.mMass[to], 0.0, "CellData copy mass");
    assertNear(0.25f, cellData.mLipids[to], 0.0, "CellData copy lipids");
    assertNear(0.2f, cellData.mColor[to][1], 0.0, "CellData copy color");
    assertNear(0.33f, cellData.mSignalSubstance[to][0], 0.0, "CellData copy signal");
    assertNear(0.44f, cellData.mSignalSubstanceOut[to][0], 0.0, "CellData copy signal out");
    const cellularity::GeneView copiedGene = cellData.getActiveGeneView(to);
    assertTrue(copiedGene.cellType() == CellType::STEREOCYTE,
            "CellData copy genome");
    assertNear(0.9f, copiedGene.color(0), 0.0,
            "CellData copy genome color");
    assertTrue(cellData.mActiveCellType[to] == CellType::STEREOCYTE, "CellData copy active cell type");
    assertTrue(cellData.runtimeId(to) == fromRuntimeId,
            "CellData copy moves runtime identity with compacted entity state");
    assertTrue(cellData.cellCollisionContactCount(to) == 1, "CellData copy collision count");
    assertTrue(cellData.mCellCount == 1
                    && cellData.genomePool().validateInvariants().isValid(),
            "CellData compaction leaves one valid owned Genome handle");
    assertTrue(collisionContactTargetAt(cellData, to, 0) == to, "CellData copy collision item");
    assertTrue(cellData.mFertilizationTarget[to] == 5, "CellData copy fertilization target");
}

void testCellDataValuesAndLinks() {
    CellData cellData(3);
    int lipocyte = addAliveCell(cellData, CellType::LIPOCYTE);
    int phagocyte = addAliveCell(cellData, CellType::PHAGOCYTE);
    int keratinocyte = addAliveCell(cellData, CellType::KERATINOCYTE);

    cellData.mMass[lipocyte] = CellData::MAX_MASS;
    cellData.mLipids[lipocyte] = CellData::MAX_LIPIDS;
    cellData.mMass[phagocyte] = CellData::MAX_MASS;
    cellData.mLipids[phagocyte] = CellData::MAX_LIPIDS;
    assertNear(CellData::MAX_MASS, cellData.getScaledMass(lipocyte), 1e-6, "CellData scaled lipocyte full");
    assertNear(CellData::MAX_MASS, cellData.getScaledMass(phagocyte), 1e-6, "CellData scaled phagocyte ignores lipids");
    cellData.mLipids[lipocyte] = 0.0f;
    assertTrue(cellData.getScaledMass(lipocyte) < CellData::MAX_MASS, "CellData scaled lipocyte without lipids");

    cellData.editActiveGene(phagocyte, [](cellularity::GeneBuilder& gene) {
        gene.setParametricFormulaType(Gene::SWIM_FORCE_VALUE_INDEX, 0);
        gene.setParametricVariableB(Gene::SWIM_FORCE_VALUE_INDEX, 0.0f);
    });
    assertNear(CellData::MAX_SWIM_FORCE * 0.5f, cellData.getValue(phagocyte, Gene::SWIM_FORCE_VALUE_INDEX, 0), 1e-6, "CellData getValue constant middle");
    cellData.editActiveGene(phagocyte, [](cellularity::GeneBuilder& gene) {
        gene.setParametricVariableB(Gene::SWIM_FORCE_VALUE_INDEX, 1.0f);
    });
    assertNear(CellData::MAX_SWIM_FORCE, cellData.getValue(phagocyte, Gene::SWIM_FORCE_VALUE_INDEX, 0), 1e-6, "CellData getValue constant max");
    cellData.editActiveGene(phagocyte, [](cellularity::GeneBuilder& gene) {
        gene.setParametricFormulaType(Gene::SWIM_FORCE_VALUE_INDEX, 1);
        gene.setParametricSignalChannel(
                Gene::SWIM_FORCE_VALUE_INDEX,
                CellData::SIGNAL_SUBSTANCE_COUNT);
        gene.setParametricVariableA(Gene::SWIM_FORCE_VALUE_INDEX, 8.0f);
        gene.setParametricVariableB(Gene::SWIM_FORCE_VALUE_INDEX, -1.0f);
    });
    cellData.mMass[phagocyte] = CellData::MAX_MASS * 0.25;
    assertNear(CellData::MAX_SWIM_FORCE, cellData.getValue(phagocyte, Gene::SWIM_FORCE_VALUE_INDEX, 0), 1e-6, "CellData getValue mass channel");

    assertTrue(cellData.isCellSafeByKeratinocyte(keratinocyte), "CellData keratinocyte protects itself");
    assertTrue(!cellData.isCellSafeByKeratinocyte(phagocyte), "CellData phagocyte is not safe by default");
    cellData.addLink(
            phagocyte,
            keratinocyte,
            CellLinkCreationReason::UNSPECIFIED,
            0.25,
            1.25,
            0.5f,
            0.01f);
    assertTrue(cellData.mTopology.edgeCount() == 1, "CellData addLink creates topology edge");
    assertTrue(cellData.mTopology.isLinked(phagocyte, keratinocyte), "CellData addLink topology target");
    assertNear(0.25, cellData.mTopology.edgeAngleA(0), 0.0, "CellData addLink topology angleA");
    assertNear(1.25, cellData.mTopology.edgeAngleB(0), 0.0, "CellData addLink topology angleB");
    assertNear(0.5f, cellData.mTopology.edgeTorsionalStiffness(0), 0.0, "CellData addLink topology stiffness");
    assertNear(0.01f, cellData.mTopology.edgeRestLength(0), 0.0, "CellData addLink topology length");
    assertTrue(cellData.isCellSafeByKeratinocyte(phagocyte), "CellData linked keratinocyte protects neighbor");
    assertTrue(!cellData.isNotLinked(phagocyte, keratinocyte), "CellData isNotLinked false for existing link");
    assertTrue(!cellData.isNotLinked(keratinocyte, phagocyte), "CellData isNotLinked follows topology edge");

    for (int linkIndex = 0; linkIndex < CellData::MAX_NEIGHBORS; linkIndex++) {
        cellData.mTopology.addCellLink(
                lipocyte,
                linkIndex % 2 == 0 ? phagocyte : keratinocyte,
                CellLinkCreationReason::UNSPECIFIED,
                0.0,
                0.0,
                0.0f,
                0.0f);
    }
    assertTrue(!cellData.isCellCanMakeLink(lipocyte), "CellData max link count prevents links");
}

void testCellDataLinkCrudSyncsTopology() {
    CellData cellData(3);
    int first = addAliveCell(cellData, CellType::PHAGOCYTE);
    int second = addAliveCell(cellData, CellType::PHAGOCYTE);

    int firstSlot = cellData.addLink(
            first,
            second,
            CellLinkCreationReason::UNSPECIFIED,
            0.25,
            1.25,
            0.5f,
            0.01f);
    assertTrue(firstSlot == 0, "CellData topology sync first add succeeds");
    assertTrue(cellData.mTopology.edgeCount() == 1, "CellData topology sync first edge count");
    assertTrue(cellData.mTopology.isLinked(first, second), "CellData topology sync first edge");
    assertTrue(cellData.mTopology.linkCount(first) == 1, "CellData topology sync first link count");
    assertTrue(cellData.mTopology.linkCount(second) == 1, "CellData topology sync second link count");

    int secondSlot = cellData.addLink(
            second,
            first,
            CellLinkCreationReason::UNSPECIFIED,
            1.25,
            0.25,
            0.5f,
            0.01f);
    assertTrue(secondSlot == 0, "CellData topology sync reciprocal add succeeds");
    assertTrue(cellData.mTopology.edgeCount() == 1, "CellData topology sync reciprocal does not duplicate edge");
    assertTrue(cellData.mTopology.validate(), "CellData topology sync add validate");

    cellData.addLink(
            first,
            CellData::BOUNDARY_CONNECTION_INDEX,
            CellLinkCreationReason::UNSPECIFIED,
            0.75,
            0.0,
            0.25f,
            0.0f);
    assertTrue(cellData.mTopology.boundaryCount() == 1, "CellData topology sync boundary add count");
    assertTrue(cellData.mTopology.hasBoundaryLink(first), "CellData topology sync boundary present");
    assertTrue(cellData.mTopology.validate(), "CellData topology sync boundary validate");

    cellData.mTopology.removeCellLink(0);
    assertTrue(cellData.mTopology.edgeCount() == 0, "CellData topology sync direct remove edge count");
    assertTrue(cellData.mTopology.validate(), "CellData topology sync direct remove validate");
}

void testRenderContactCollectorCollectsCellPairChords() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    CellData cellData(2);
    RenderContactChordBuffer renderContactChords(2);
    RenderContactCollector collector;
    int a = addAliveCell(cellData, CellType::PHAGOCYTE);
    int b = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[a] = 0.0;
    cellData.mPositionY[a] = 0.0;
    cellData.mPositionX[b] = 0.05;
    cellData.mPositionY[b] = 0.0;
    cellData.mRadius[a] = 0.03;
    cellData.mRadius[b] = 0.03;

    collector.collect(RenderContactInput::CurrentGeometry,
            cellData,
            environment,
            renderContactChords);

    const auto aChords = renderContactChords.activeChords(a);
    const auto bChords = renderContactChords.activeChords(b);
    const double halfChordLength = std::sqrt(4.0 * 0.03 * 0.03 - 0.05 * 0.05) / 2.0;
    assertTrue(aChords.size() == 1, "RenderContactCollector emits one chord for the first Cell");
    assertTrue(aChords[0].neighborCellIndex == b, "RenderContactCollector stores the first neighbor index");
    assertTrue(!aChords[0].isDiscarded, "RenderContactCollector initializes the first discarded flag");
    assertTrue(!aChords[0].joinsNextChord, "RenderContactCollector initializes the first continuation flag");
    assertNear(0.025f, aChords[0].x1, 1e-6, "RenderContactCollector first x1");
    assertNear(-halfChordLength, aChords[0].y1, 1e-6, "RenderContactCollector first y1");
    assertNear(0.025f, aChords[0].x2, 1e-6, "RenderContactCollector first x2");
    assertNear(halfChordLength, aChords[0].y2, 1e-6, "RenderContactCollector first y2");
    assertTrue(bChords.size() == 1, "RenderContactCollector mirrors one chord to the second Cell");
    assertTrue(bChords[0].neighborCellIndex == a, "RenderContactCollector stores the mirrored neighbor index");
    assertTrue(!bChords[0].isDiscarded, "RenderContactCollector initializes the mirrored discarded flag");
    assertTrue(!bChords[0].joinsNextChord, "RenderContactCollector initializes the mirrored continuation flag");
    assertNear(-0.025f, bChords[0].x1, 1e-6, "RenderContactCollector mirrored x1");
    assertNear(halfChordLength, bChords[0].y1, 1e-6, "RenderContactCollector mirrored y1");
    assertNear(-0.025f, bChords[0].x2, 1e-6, "RenderContactCollector mirrored x2");
    assertNear(-halfChordLength, bChords[0].y2, 1e-6, "RenderContactCollector mirrored y2");

    cellData.clearCellCollisionContacts();
    assertTrue(cellData.addCellCollisionContact(a, b, -0.05, 0.0, 0.0025),
            "RenderContactCollector fresh fixture adds first directed contact");
    assertTrue(cellData.addCellCollisionContact(b, a, 0.05, 0.0, 0.0025),
            "RenderContactCollector fresh fixture adds reciprocal contact");
    collector.collect(RenderContactInput::FreshPhysicsContacts,
            cellData,
            environment,
            renderContactChords);
    assertTrue(renderContactChords.activeChordCount(a) == 1,
            "RenderContactCollector deduplicates reciprocal cached contact for first Cell");
    assertTrue(renderContactChords.activeChordCount(b) == 1,
            "RenderContactCollector deduplicates reciprocal cached contact for second Cell");

    renderContactChords.beginCollection(2);
    assertTrue(renderContactChords.activeChords(a).empty()
                    && renderContactChords.activeChords(b).empty(),
            "RenderContactChordBuffer begins a fresh sparse collection");

    RenderContactChord capacityChord;
    for (int chordIndex = 0;
            chordIndex < cellularity::render::kMaximumRenderContactChordsPerCell;
            chordIndex++) {
        capacityChord.neighborCellIndex = 100 + chordIndex;
        assertTrue(renderContactChords.tryAppend(a, capacityChord),
                "RenderContactChordBuffer accepts its declared capacity");
    }
    assertTrue(!renderContactChords.tryAppend(a, capacityChord),
            "RenderContactChordBuffer rejects overflow without mutation");
    assertTrue(renderContactChords.activeChordCount() == static_cast<std::size_t>(cellularity::render::kMaximumRenderContactChordsPerCell),
            "RenderContactChordBuffer stores only active sparse payload");
}

void testRenderContactCollectorCollectsBoundaryChord() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    CellData cellData(1);
    RenderContactChordBuffer renderContactChords(1);
    RenderContactCollector collector;
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.9;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.2;

    collector.collect(RenderContactInput::CurrentGeometry,
            cellData,
            environment,
            renderContactChords);

    float x = (0.9f * 0.9f - 1.0f * 1.0f + 0.2f * 0.2f) / (2.0f * 0.9f);
    float mx = x * (0.0f - 0.9f) / 0.9f;
    const float contactChordLength = std::sqrt(
                                             (-0.9f + 0.2f - 1.0f)
                                             * (-0.9f - 0.2f + 1.0f)
                                             * (-0.9f + 0.2f + 1.0f)
                                             * (0.9f + 0.2f + 1.0f))
            / 0.9f;
    float expectedY1 = contactChordLength * (0.0f - 0.9f) / (2.0f * 0.9f);
    float expectedY2 = -expectedY1;

    const auto contactChords = renderContactChords.activeChords(cell);
    assertTrue(contactChords.size() == 1, "RenderContactCollector emits one boundary chord");
    assertTrue(contactChords[0].neighborCellIndex == CellData::BOUNDARY_CONNECTION_INDEX, "RenderContactCollector boundary index");
    assertNear(mx, contactChords[0].x1, 1e-6, "RenderContactCollector boundary x1");
    assertNear(expectedY1, contactChords[0].y1, 1e-6, "RenderContactCollector boundary y1");
    assertNear(mx, contactChords[0].x2, 1e-6, "RenderContactCollector boundary x2");
    assertNear(expectedY2, contactChords[0].y2, 1e-6, "RenderContactCollector boundary y2");
    assertTrue(!contactChords[0].isDiscarded, "RenderContactCollector initializes boundary discarded flag");
    assertTrue(!contactChords[0].joinsNextChord, "RenderContactCollector initializes boundary continuation flag");

    constexpr int cellContactCount =
            cellularity::render::kMaximumCellContactChordsPerCell;
    static_assert(CellData::MAX_NEIGHBORS == 20);
    static_assert(cellContactCount == CellData::MAX_COLLISIONS_PER_CELL);
    static_assert(cellularity::render::kMaximumRenderContactChordsPerCell
            == cellContactCount + 1);
    Environment denseEnvironment =
            Environment::constructDefaultEnvironment(cellContactCount + 1,
                    0,
                    1.0);
    CellData denseCells(cellContactCount + 1);
    RenderContactChordBuffer denseChords(cellContactCount + 1);
    RenderContactCollector denseCollector;
    const int center = addAliveCell(denseCells, CellType::PHAGOCYTE);
    denseCells.mPositionX[center] = 0.99;
    denseCells.mPositionY[center] = 0.0;
    denseCells.mRadius[center] = 0.02;
    constexpr double contactDistance = 0.03;
    for (int contactIndex = 0; contactIndex < cellContactCount;
            ++contactIndex) {
        const int neighbor = addAliveCell(denseCells, CellType::PHAGOCYTE);
        const double angle =
                2.0 * Environment::PI * contactIndex / cellContactCount;
        const double distanceX = -contactDistance * std::cos(angle);
        const double distanceY = -contactDistance * std::sin(angle);
        denseCells.mPositionX[neighbor] =
                denseCells.mPositionX[center] - distanceX;
        denseCells.mPositionY[neighbor] =
                denseCells.mPositionY[center] - distanceY;
        denseCells.mRadius[neighbor] = 0.02;
        assertTrue(denseCells.addKnownCellCollisionContactFast(
                           center,
                           neighbor,
                           distanceX,
                           distanceY,
                           contactDistance * contactDistance),
                "Dense render-contact fixture stores a center contact");
        assertTrue(denseCells.addKnownCellCollisionContactFast(
                           neighbor,
                           center,
                           -distanceX,
                           -distanceY,
                           contactDistance * contactDistance),
                "Dense render-contact fixture stores a reciprocal contact");
    }

    denseCollector.collect(RenderContactInput::FreshPhysicsContacts,
            denseCells,
            denseEnvironment,
            denseChords);
    const auto centerChords = denseChords.activeChords(center);
    assertTrue(centerChords.size()
                    == cellularity::render::
                            kMaximumRenderContactChordsPerCell,
            "RenderContactCollector retains 32 Cell contacts plus the dish boundary");
    int boundaryContactCount = 0;
    int cellChordCount = 0;
    for (const RenderContactChord& centerChord : centerChords) {
        if (centerChord.neighborCellIndex
                == CellData::BOUNDARY_CONNECTION_INDEX) {
            ++boundaryContactCount;
        } else {
            ++cellChordCount;
        }
    }
    assertTrue(cellChordCount == cellContactCount
                    && boundaryContactCount == 1,
            "Render capacity is independent of the unchanged topology limit");
}

void testCellDataFixNeighborsForDrawing() {
    std::array<RenderContactChord, CellData::MAX_NEIGHBORS> contactChords{};
    constexpr int contactChordCount = 2;

    contactChords[0].x1 = -1.0f;
    contactChords[0].y1 = 1.0f;
    contactChords[0].x2 = -1.0f;
    contactChords[0].y2 = 2.0f;
    contactChords[0].neighborCellIndex = 10;

    contactChords[1].x1 = 1.0f;
    contactChords[1].y1 = 1.0f;
    contactChords[1].x2 = 1.0f;
    contactChords[1].y2 = 2.0f;
    contactChords[1].neighborCellIndex = 11;

    cellularity::render::clipAndOrderRenderContactChords(
            contactChords,
            contactChordCount);

    assertTrue(contactChords[0].neighborCellIndex == 11, "Render contact normalizer sorts first");
    assertTrue(contactChords[1].neighborCellIndex == 10, "Render contact normalizer sorts second");

    std::array<RenderContactChord, CellData::MAX_NEIGHBORS> crossingChords{};
    crossingChords[0].x1 = -1.0f;
    crossingChords[0].y1 = 0.0f;
    crossingChords[0].x2 = 1.0f;
    crossingChords[0].y2 = 0.0f;
    crossingChords[1].x1 = 0.0f;
    crossingChords[1].y1 = -1.0f;
    crossingChords[1].x2 = 0.0f;
    crossingChords[1].y2 = 1.0f;

    cellularity::render::clipAndOrderRenderContactChords(
            crossingChords,
            contactChordCount);

    assertTrue(crossingChords[0].joinsNextChord || crossingChords[1].joinsNextChord, "Render contact normalizer marks continuation on crossing");
    bool hasIntersectionEndpoint = false;
    for (int i = 0; i < contactChordCount; i++) {
        const RenderContactChord& wall = crossingChords[i];
        hasIntersectionEndpoint = hasIntersectionEndpoint
                || (std::abs(wall.x1) < 1e-6f && std::abs(wall.y1) < 1e-6f)
                || (std::abs(wall.x2) < 1e-6f && std::abs(wall.y2) < 1e-6f);
    }
    assertTrue(hasIntersectionEndpoint, "Render contact normalizer persists intersection endpoint");
}

void testCellDataBoundaryLinkAppliesSpringForceAndTorque() {
    CellData cellData(1);
    int cell = addCiliocyteTestCell(cellData, CellType::GLUEOCYTE, 0.0, 0.0, 1.0, 0.02);

    cellData.mTopology.addBoundaryLink(cell, 0.0, 0.03, 0.01, 0.0f, 0.0f);

    cellData.updateAllCellLinkInteractions(0.0);

    double attachOffset = 0.02 * (1.0 - CellData::LINK_ATTACH_DEPTH);
    double distanceToAnchorX = 0.03 - attachOffset;
    double distanceToAnchorY = 0.01;
    double expectedForceX = distanceToAnchorX * CellData::LINK_SPRING_COEFFICIENT * 3.0;
    double expectedForceY = distanceToAnchorY * CellData::LINK_SPRING_COEFFICIENT * 3.0;
    assertNear(expectedForceX, cellData.mForceX[cell], 1e-12, "CellData boundary link forceX");
    assertNear(expectedForceY, cellData.mForceY[cell], 1e-12, "CellData boundary link forceY");
    assertNear(expectedForceY * attachOffset, cellData.mTorque[cell], 1e-12, "CellData boundary link torque");
    assertNear(static_cast<float>((0.02 - CellData::MIN_CELL_RADIUS) * 0.8),
            cellData.mTopology.boundaryRenderStartOffsetX(0),
            1e-7,
            "CellData boundary link render start X");
    assertNear(0.0f, cellData.mTopology.boundaryRenderStartOffsetY(0), 0.0, "CellData boundary link render start Y");
}

void testCellDataBoundaryLinkTearsWhenAnchorIsTooFar() {
    CellData cellData(1);
    int cell = addCiliocyteTestCell(cellData, CellType::GLUEOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);

    cellData.mTopology.addBoundaryLink(cell, 0.0, 1.0, 0.0, 0.0f, 0.0f);

    cellData.updateAllCellLinkInteractions(SimulationWorld::DELTA_TIME);

    assertTrue(cellData.mTopology.boundaryCount() == 0, "CellData boundary link tears");
    assertNear(0.0, cellData.mForceX[cell], 0.0, "CellData torn boundary link forceX");
    assertNear(0.0, cellData.mForceY[cell], 0.0, "CellData torn boundary link forceY");
}

void testCellDataCellLinkAppliesSpringForceRenderOffsetsAndNoTorqueForAxialLink() {
    CellData cellData(2);
    int left = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 1.0, 0.02);
    int right = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.05, 0.0, 1.0, 0.02);
    cellData.addLink(
            left, right, CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);

    cellData.updateAllCellLinkInteractions(0.0);

    double penetration = 0.02 * CellData::LINK_ATTACH_DEPTH;
    double attachDistance = 0.02 - penetration;
    double distanceX = 0.05 - attachDistance - attachDistance;
    double sqrDistance = distanceX * distanceX;
    double strain = 1.0 + 2.0 * sqrDistance / (CellData::MAX_CELL_RADIUS * CellData::MAX_CELL_RADIUS * CellData::LINK_MARGIN * CellData::LINK_MARGIN);
    double expectedForce = distanceX * strain * CellData::LINK_SPRING_COEFFICIENT;

    assertNear(expectedForce, cellData.mForceX[left], 1e-12, "CellData cell link axial forceX");
    assertNear(-expectedForce, cellData.mForceX[right], 1e-12, "CellData cell link axial reverse forceX");
    assertNear(0.0, cellData.mForceY[left], 1e-12, "CellData cell link axial forceY");
    assertNear(0.0, cellData.mForceZ[left], 1e-12, "CellData cell link axial forceZ");
    assertNear(0.0, cellData.mTorque[left], 1e-12, "CellData cell link axial torque");
    assertNear(static_cast<float>((0.02 - CellData::MIN_CELL_RADIUS) * 0.8),
            cellData.mTopology.edgeRenderStartOffsetAX(0),
            1e-7,
            "CellData cell link render start X");
    assertNear(static_cast<float>(-(0.02 - CellData::MIN_CELL_RADIUS) * 0.8),
            cellData.mTopology.edgeRenderEndOffsetAX(0),
            1e-7,
            "CellData cell link render end X");
}

void testCellDataCellLinkIncludesPointVelocityDamping() {
    CellData cellData(2);
    int left = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 1.0, 0.02);
    int right = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.05, 0.0, 3.0, 0.02);
    cellData.mVelocityX[left] = 0.1;
    cellData.mVelocityX[right] = -0.2;
    cellData.addLink(
            left, right, CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);

    cellData.updateAllCellLinkInteractions(0.0);

    double penetration = 0.02 * CellData::LINK_ATTACH_DEPTH;
    double attachDistance = 0.02 - penetration;
    double distanceX = 0.05 - attachDistance - attachDistance;
    double sqrDistance = distanceX * distanceX;
    double strain = 1.0 + 2.0 * sqrDistance / (CellData::MAX_CELL_RADIUS * CellData::MAX_CELL_RADIUS * CellData::LINK_MARGIN * CellData::LINK_MARGIN);
    double spring = distanceX * strain * CellData::LINK_SPRING_COEFFICIENT;
    double damping = CellData::LINK_INTERCELL_DAMPING * 1.0 * 3.0 / (1.0 + 3.0);
    double expectedForce = spring + (-0.2 - 0.1) * damping;

    assertNear(expectedForce, cellData.mForceX[left], 1e-12, "CellData cell link damping forceX");
}

void testCellDataCellLinkRestLengthChangesAttachmentDistanceAndBaseStiffness() {
    CellData cellData(2);
    int left = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    int right = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.06, 0.0, CellData::INITIAL_MASS, 0.02);
    cellData.addLink(
            left, right, CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.005f);

    cellData.updateAllCellLinkInteractions(0.0);

    double penetration = 0.02 * CellData::LINK_ATTACH_DEPTH;
    double restLength = static_cast<float>(0.005);
    double attachDistance = 0.02 - penetration + restLength;
    double distanceX = 0.06 - attachDistance - attachDistance;
    double sqrDistance = distanceX * distanceX;
    double strain = 1.0 + 2.0 * sqrDistance / (CellData::MAX_CELL_RADIUS * CellData::MAX_CELL_RADIUS * CellData::LINK_MARGIN * CellData::LINK_MARGIN);
    double baseStiffness = CellData::LINK_SPRING_COEFFICIENT * CellData::MIN_CELL_RADIUS / (CellData::MIN_CELL_RADIUS + 2.0 * restLength);

    assertNear(distanceX * strain * baseStiffness, cellData.mForceX[left], 1e-8, "CellData cell link rest length forceX");
}

void testCellDataCellLinkAddsTorsionTorqueWhenAttachmentDirectionsAreSkewed() {
    CellData cellData(2);
    int left = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    int right = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.02, CellData::INITIAL_MASS, 0.02);
    cellData.addLink(
            left, right, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.5 * std::acos(-1.0), 0.0f, 0.0f);

    cellData.updateAllCellLinkInteractions(0.0);
    double torqueWithoutTorsion = cellData.mTorque[left];

    CellData torsionData(2);
    int torsionLeft = addCiliocyteTestCell(torsionData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    int torsionRight = addCiliocyteTestCell(torsionData, CellType::PHAGOCYTE, 0.0, 0.02, CellData::INITIAL_MASS, 0.02);
    torsionData.addLink(
            torsionLeft, torsionRight, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.5 * std::acos(-1.0), 4.0f, 0.0f);
    torsionData.updateAllCellLinkInteractions(0.0);

    double attachmentLength = 0.02 * (1.0 - CellData::LINK_ATTACH_DEPTH);
    double expectedTorsion = -attachmentLength * 0.02 * 4.0;
    assertNear(expectedTorsion, torsionData.mTorque[torsionLeft] - torqueWithoutTorsion, 1e-12, "CellData cell link torsion torque");
}

void testCellDataCellLinkTearsReciprocalPairWhenTooFar() {
    CellData cellData(2);
    int left = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    int right = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 1.0, 0.0, CellData::INITIAL_MASS, 0.02);
    cellData.addLink(
            left, right, CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);

    cellData.updateAllCellLinkInteractions(SimulationWorld::DELTA_TIME);

    assertTrue(cellData.mTopology.edgeCount() == 0, "CellData far cell link tears topology edge");
}

void testCellDataMyocyteLinkContractionBendingAndLiftingChangeForceAndTorque() {
    CellData cellData(2);
    int myocyte = addCiliocyteTestCell(cellData, CellType::MYOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    int other = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.05, 0.01, CellData::INITIAL_MASS, 0.02);
    cellData.mMyocyteContraction[myocyte] = 0.25f;
    cellData.mMyocyteBending[myocyte] = 0.5f;
    cellData.mMyocyteLifting[myocyte] = 0.75f;
    cellData.addLink(
            myocyte, other, CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);

    cellData.updateAllCellLinkInteractions(0.0);

    assertNear(-0.75, cellData.mForceZ[myocyte], 1e-12, "CellData myocyte link lifting forceZ");
    assertTrue(cellData.mForceX[myocyte] > 0.0, "CellData myocyte link contraction changes forceX");
    assertTrue(std::abs(cellData.mTorque[myocyte]) > 0.0, "CellData myocyte link bending changes torque");
}

void testCellDataAllLinkInteractionsProcessesEveryCellIncludingDeadCells() {
    CellData cellData(2);
    int left = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 1.0, 0.02);
    int right = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.05, 0.0, 1.0, 0.02);
    addPairLink(cellData, left, right, 0.0, std::acos(-1.0), 0.0f, 0.0f);
    cellData.markCellAsDead(right);

    cellData.updateAllCellLinkInteractions(0.0);

    double penetration = 0.02 * CellData::LINK_ATTACH_DEPTH;
    double attachDistance = 0.02 - penetration;
    double distanceX = 0.05 - attachDistance - attachDistance;
    double sqrDistance = distanceX * distanceX;
    double strain = 1.0 + 2.0 * sqrDistance / (CellData::MAX_CELL_RADIUS * CellData::MAX_CELL_RADIUS * CellData::LINK_MARGIN * CellData::LINK_MARGIN);
    double expectedForce = distanceX * strain * CellData::LINK_SPRING_COEFFICIENT;

    assertNear(expectedForce, cellData.mForceX[left], 1e-12, "CellData all link interactions live force");
    assertNear(-expectedForce, cellData.mForceX[right], 1e-12, "CellData all link interactions dead cell still processed");
}

} // namespace

int main() {
    testLinkPhysicsConstants();
    testRenderContactChordCopy();
    testCollisionGridCoordinatesAndBounds();
    testCollisionGridUntypedAddOverflowAndClear();
    testCollisionGridTypedAddStoresTypesAndThrowsForWrongAdd();
    testCellTopologyAddsNormalAndBoundaryLinks();
    testCellTopologyMaxLinkCountAndInvalidAdds();
    testCellTopologyRemoveNormalEdgeUpdatesAdjacencyRefs();
    testCellTopologyRuntimeIdentityLifecycle();
    testCellTopologyRemoveBoundaryUpdatesAdjacencyRefs();
    testCellTopologyRemoveBetweenAndRemoveAll();
    testCellTopologyMoveCellIndexAndRebuildAdjacency();
    testCellTopologyCollectLinksUsesCellLocalOrientation();
    testCellTopologyStructuralRevisionTracksTopologyButNotRenderOffsets();
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
    testCellSignalAdjacencyBuildsGlobalEdgeOrderedCellRanges();
#endif
    testIncrementalBucketedF64Source32SweepMatchesBruteForceDuringChurn();
    testIncrementalBucketedF64Source32StrictTangencyAndRowRange();
    testIncrementalBucketedPreparedSourceIsConsumedOnce();
    testCellSweepBroadphaseBuildsSameAndAdjacentRowPairs();
    verifyCompactSweepMatchesBaselineOrderAcrossLayoutChanges(
            CellOverlapScanAlgorithm::COMPACT_RECORD_SWEEP,
            "Compact record sweep");
    verifyCompactSweepMatchesBaselineOrderAcrossLayoutChanges(
            CellOverlapScanAlgorithm::COMPACT_PRIMARY_SWEEP,
            "Compact primary sweep");
    testCellDataBasics();
    testCellDataKeepsDefaultGenomeTemplateImmutable();
    testCellDataLargeClonePopulationSharesOneGenomeRecord();
    testCellDataMoveLastCellForCompaction();
    testCellDataValuesAndLinks();
    testCellDataLinkCrudSyncsTopology();
    testRenderContactCollectorCollectsCellPairChords();
    testRenderContactCollectorCollectsBoundaryChord();
    testCellDataFixNeighborsForDrawing();
    testCellDataBoundaryLinkAppliesSpringForceAndTorque();
    testCellDataBoundaryLinkTearsWhenAnchorIsTooFar();
    testCellDataCellLinkAppliesSpringForceRenderOffsetsAndNoTorqueForAxialLink();
    testCellDataCellLinkIncludesPointVelocityDamping();
    testCellDataCellLinkRestLengthChangesAttachmentDistanceAndBaseStiffness();
    testCellDataCellLinkAddsTorsionTorqueWhenAttachmentDirectionsAreSkewed();
    testCellDataCellLinkTearsReciprocalPairWhenTooFar();
    testCellDataMyocyteLinkContractionBendingAndLiftingChangeForceAndTorque();
    testCellDataAllLinkInteractionsProcessesEveryCellIncludingDeadCells();
    std::cout << "native topology parity tests passed\n";
    return 0;
}
