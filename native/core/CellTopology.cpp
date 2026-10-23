#include "core/CellTopology.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "verification/SemanticTrace.h"

CellTopology::CellTopology(int maxCellCount)
        : mMaxCellCount(maxCellCount),
          mCellLinkRefs(maxCellCount),
          mCellLinkKinds(maxCellCount),
          mCellLinkCount(maxCellCount) {
    clearAll();
}

int CellTopology::edgeCount() const {
    return static_cast<int>(mEdgeCellA.size());
}

int CellTopology::boundaryCount() const {
    return static_cast<int>(mBoundaryCell.size());
}

std::uint64_t CellTopology::structuralRevision() const noexcept {
    return mStructuralRevision;
}

CellTopology::EdgeView CellTopology::edgeView() const {
    return {
            edgeCount(),
            mEdgeCellA.data(),
            mEdgeCellB.data(),
            mEdgeCreationReasons.data(),
            mEdgeAngleA.data(),
            mEdgeAngleB.data(),
            mEdgeAngleACos.data(),
            mEdgeAngleASin.data(),
            mEdgeAngleBCos.data(),
            mEdgeAngleBSin.data(),
            mEdgeTorsionalStiffness.data(),
            mEdgeRestLength.data()
    };
}

CellTopology::RenderEdgeView CellTopology::renderEdgeView() const {
    return {
            edgeCount(),
            mEdgeRuntimeIds.data(),
            mEdgeCellA.data(),
            mEdgeCellB.data(),
            mEdgeRenderStartOffsetAX.data(),
            mEdgeRenderStartOffsetAY.data(),
            mEdgeRenderEndOffsetAX.data(),
            mEdgeRenderEndOffsetAY.data()
    };
}

CellTopology::BoundaryView CellTopology::boundaryView() const {
    return {
            boundaryCount(),
            mBoundaryCell.data(),
            mBoundaryAngleA.data(),
            mBoundaryAngleACos.data(),
            mBoundaryAngleASin.data(),
            mBoundaryAnchorX.data(),
            mBoundaryAnchorY.data(),
            mBoundaryTorsionalStiffness.data(),
            mBoundaryRestLength.data()
    };
}

const int* CellTopology::cellLinkCounts() const {
    return mCellLinkCount.data();
}

int CellTopology::linkCount(int cellIndex) const {
    return isValidCellIndex(cellIndex) ? mCellLinkCount[cellIndex] : 0;
}

bool CellTopology::canMakeLink(int cellIndex) const {
    return isValidCellIndex(cellIndex) && mCellLinkCount[cellIndex] < MAX_NEIGHBORS;
}

bool CellTopology::isLinked(int firstCellIndex, int secondCellIndex) const {
    if (!isValidCellIndex(firstCellIndex) || !isValidCellIndex(secondCellIndex)) {
        return false;
    }
    const int count = mCellLinkCount[firstCellIndex];
    for (int slot = 0; slot < count; slot++) {
        const LinkKind kind = static_cast<LinkKind>(mCellLinkKinds[firstCellIndex][slot]);
        if (kind == BOUNDARY) {
            continue;
        }
        const int edgeIndex = mCellLinkRefs[firstCellIndex][slot];
        const int otherCell = kind == NORMAL_A ? mEdgeCellB[edgeIndex] : mEdgeCellA[edgeIndex];
        if (otherCell == secondCellIndex) {
            return true;
        }
    }
    return false;
}

bool CellTopology::hasBoundaryLink(int cellIndex) const {
    if (!isValidCellIndex(cellIndex)) {
        return false;
    }
    const int count = mCellLinkCount[cellIndex];
    for (int slot = 0; slot < count; slot++) {
        if (static_cast<LinkKind>(mCellLinkKinds[cellIndex][slot]) == BOUNDARY) {
            return true;
        }
    }
    return false;
}

int CellTopology::addCellLink(int cellA,
        int cellB,
        CellLinkCreationReason creationReason,
        double angleA,
        double angleB,
        float torsionalStiffness,
        float restLength) {
    if (!isValidCellIndex(cellA)
            || !isValidCellIndex(cellB)
            || !isValidCellLinkCreationReason(creationReason)
            || cellA == cellB
            || mCellLinkCount[cellA] >= MAX_NEIGHBORS
            || mCellLinkCount[cellB] >= MAX_NEIGHBORS) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::TopologyCellLink,
                cellularity::verification::traceI32(cellA),
                cellularity::verification::traceI32(cellB),
                cellularity::verification::traceI32(-1),
                cellularity::verification::traceBool(false));
        return -1;
    }

    const int edgeIndex = edgeCount();
    mEdgeRuntimeIds.push_back(allocateEdgeRuntimeId());
    mEdgeCellA.push_back(cellA);
    mEdgeCellB.push_back(cellB);
    mEdgeCreationReasons.push_back(creationReason);
    mEdgeAngleA.push_back(angleA);
    mEdgeAngleB.push_back(angleB);
    mEdgeAngleACos.push_back(std::cos(angleA));
    mEdgeAngleASin.push_back(std::sin(angleA));
    mEdgeAngleBCos.push_back(std::cos(angleB));
    mEdgeAngleBSin.push_back(std::sin(angleB));
    mEdgeTorsionalStiffness.push_back(torsionalStiffness);
    mEdgeRestLength.push_back(restLength);
    mEdgeRenderStartOffsetAX.push_back(0.0f);
    mEdgeRenderStartOffsetAY.push_back(0.0f);
    mEdgeRenderEndOffsetAX.push_back(0.0f);
    mEdgeRenderEndOffsetAY.push_back(0.0f);

    addAdjacency(cellA, NORMAL_A, edgeIndex);
    addAdjacency(cellB, NORMAL_B, edgeIndex);
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::TopologyCellLink,
            cellularity::verification::traceI32(cellA),
            cellularity::verification::traceI32(cellB),
            cellularity::verification::traceI32(edgeIndex),
            cellularity::verification::traceBool(true));
    markStructureChanged();
    return edgeIndex;
}

int CellTopology::addBoundaryLink(int cellIndex,
        double angleA,
        double anchorX,
        double anchorY,
        float torsionalStiffness,
        float restLength) {
    if (!isValidCellIndex(cellIndex) || mCellLinkCount[cellIndex] >= MAX_NEIGHBORS) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::TopologyBoundaryLink,
                cellularity::verification::traceI32(cellIndex),
                cellularity::verification::traceI32(-1),
                cellularity::verification::traceBool(false));
        return -1;
    }

    const int boundaryIndex = boundaryCount();
    mBoundaryCell.push_back(cellIndex);
    mBoundaryAngleA.push_back(angleA);
    mBoundaryAngleACos.push_back(std::cos(angleA));
    mBoundaryAngleASin.push_back(std::sin(angleA));
    mBoundaryAnchorX.push_back(anchorX);
    mBoundaryAnchorY.push_back(anchorY);
    mBoundaryTorsionalStiffness.push_back(torsionalStiffness);
    mBoundaryRestLength.push_back(restLength);
    mBoundaryRenderStartOffsetX.push_back(0.0f);
    mBoundaryRenderStartOffsetY.push_back(0.0f);

    addAdjacency(cellIndex, BOUNDARY, boundaryIndex);
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::TopologyBoundaryLink,
            cellularity::verification::traceI32(cellIndex),
            cellularity::verification::traceI32(boundaryIndex),
            cellularity::verification::traceBool(true));
    markStructureChanged();
    return boundaryIndex;
}

void CellTopology::removeCellLink(int edgeIndex) {
    if (edgeIndex < 0 || edgeIndex >= edgeCount()) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::TopologyCellRemove,
                cellularity::verification::traceI32(edgeIndex),
                cellularity::verification::traceI32(-1),
                cellularity::verification::traceBool(false));
        return;
    }

    const int cellA = mEdgeCellA[edgeIndex];
    const int cellB = mEdgeCellB[edgeIndex];
    removeAdjacency(cellA, NORMAL_A, edgeIndex);
    removeAdjacency(cellB, NORMAL_B, edgeIndex);

    const int lastIndex = edgeCount() - 1;
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::TopologyCellRemove,
            cellularity::verification::traceI32(edgeIndex),
            cellularity::verification::traceI32(lastIndex),
            cellularity::verification::traceI32(cellA),
            cellularity::verification::traceI32(cellB),
            cellularity::verification::traceI32(mEdgeCellA[lastIndex]),
            cellularity::verification::traceI32(mEdgeCellB[lastIndex]),
            cellularity::verification::traceBool(true));
    if (edgeIndex != lastIndex) {
        const int movedCellA = mEdgeCellA[lastIndex];
        const int movedCellB = mEdgeCellB[lastIndex];

        mEdgeRuntimeIds[edgeIndex] = mEdgeRuntimeIds[lastIndex];
        mEdgeCellA[edgeIndex] = movedCellA;
        mEdgeCellB[edgeIndex] = movedCellB;
        mEdgeCreationReasons[edgeIndex] = mEdgeCreationReasons[lastIndex];
        mEdgeAngleA[edgeIndex] = mEdgeAngleA[lastIndex];
        mEdgeAngleB[edgeIndex] = mEdgeAngleB[lastIndex];
        mEdgeAngleACos[edgeIndex] = mEdgeAngleACos[lastIndex];
        mEdgeAngleASin[edgeIndex] = mEdgeAngleASin[lastIndex];
        mEdgeAngleBCos[edgeIndex] = mEdgeAngleBCos[lastIndex];
        mEdgeAngleBSin[edgeIndex] = mEdgeAngleBSin[lastIndex];
        mEdgeTorsionalStiffness[edgeIndex] = mEdgeTorsionalStiffness[lastIndex];
        mEdgeRestLength[edgeIndex] = mEdgeRestLength[lastIndex];
        mEdgeRenderStartOffsetAX[edgeIndex] = mEdgeRenderStartOffsetAX[lastIndex];
        mEdgeRenderStartOffsetAY[edgeIndex] = mEdgeRenderStartOffsetAY[lastIndex];
        mEdgeRenderEndOffsetAX[edgeIndex] = mEdgeRenderEndOffsetAX[lastIndex];
        mEdgeRenderEndOffsetAY[edgeIndex] = mEdgeRenderEndOffsetAY[lastIndex];

        updateAdjacencyRef(movedCellA, NORMAL_A, lastIndex, edgeIndex);
        updateAdjacencyRef(movedCellB, NORMAL_B, lastIndex, edgeIndex);
    }

    mEdgeRuntimeIds.pop_back();
    mEdgeCellA.pop_back();
    mEdgeCellB.pop_back();
    mEdgeCreationReasons.pop_back();
    mEdgeAngleA.pop_back();
    mEdgeAngleB.pop_back();
    mEdgeAngleACos.pop_back();
    mEdgeAngleASin.pop_back();
    mEdgeAngleBCos.pop_back();
    mEdgeAngleBSin.pop_back();
    mEdgeTorsionalStiffness.pop_back();
    mEdgeRestLength.pop_back();
    mEdgeRenderStartOffsetAX.pop_back();
    mEdgeRenderStartOffsetAY.pop_back();
    mEdgeRenderEndOffsetAX.pop_back();
    mEdgeRenderEndOffsetAY.pop_back();
    markStructureChanged();
}

void CellTopology::removeBoundaryLink(int boundaryIndex) {
    if (boundaryIndex < 0 || boundaryIndex >= boundaryCount()) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::TopologyBoundaryRemove,
                cellularity::verification::traceI32(boundaryIndex),
                cellularity::verification::traceI32(-1),
                cellularity::verification::traceBool(false));
        return;
    }

    const int cellIndex = mBoundaryCell[boundaryIndex];
    removeAdjacency(cellIndex, BOUNDARY, boundaryIndex);

    const int lastIndex = boundaryCount() - 1;
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::TopologyBoundaryRemove,
            cellularity::verification::traceI32(boundaryIndex),
            cellularity::verification::traceI32(lastIndex),
            cellularity::verification::traceI32(cellIndex),
            cellularity::verification::traceI32(mBoundaryCell[lastIndex]),
            cellularity::verification::traceBool(true));
    if (boundaryIndex != lastIndex) {
        const int movedCell = mBoundaryCell[lastIndex];

        mBoundaryCell[boundaryIndex] = movedCell;
        mBoundaryAngleA[boundaryIndex] = mBoundaryAngleA[lastIndex];
        mBoundaryAngleACos[boundaryIndex] = mBoundaryAngleACos[lastIndex];
        mBoundaryAngleASin[boundaryIndex] = mBoundaryAngleASin[lastIndex];
        mBoundaryAnchorX[boundaryIndex] = mBoundaryAnchorX[lastIndex];
        mBoundaryAnchorY[boundaryIndex] = mBoundaryAnchorY[lastIndex];
        mBoundaryTorsionalStiffness[boundaryIndex] = mBoundaryTorsionalStiffness[lastIndex];
        mBoundaryRestLength[boundaryIndex] = mBoundaryRestLength[lastIndex];
        mBoundaryRenderStartOffsetX[boundaryIndex] = mBoundaryRenderStartOffsetX[lastIndex];
        mBoundaryRenderStartOffsetY[boundaryIndex] = mBoundaryRenderStartOffsetY[lastIndex];

        updateAdjacencyRef(movedCell, BOUNDARY, lastIndex, boundaryIndex);
    }

    mBoundaryCell.pop_back();
    mBoundaryAngleA.pop_back();
    mBoundaryAngleACos.pop_back();
    mBoundaryAngleASin.pop_back();
    mBoundaryAnchorX.pop_back();
    mBoundaryAnchorY.pop_back();
    mBoundaryTorsionalStiffness.pop_back();
    mBoundaryRestLength.pop_back();
    mBoundaryRenderStartOffsetX.pop_back();
    mBoundaryRenderStartOffsetY.pop_back();
    markStructureChanged();
}

bool CellTopology::removeCellLinkBetween(int firstCellIndex, int secondCellIndex) {
    if (!isValidCellIndex(firstCellIndex) || !isValidCellIndex(secondCellIndex)) {
        return false;
    }
    const int count = mCellLinkCount[firstCellIndex];
    for (int slot = 0; slot < count; slot++) {
        const LinkKind kind = static_cast<LinkKind>(mCellLinkKinds[firstCellIndex][slot]);
        if (kind == BOUNDARY) {
            continue;
        }
        const int edgeIndex = mCellLinkRefs[firstCellIndex][slot];
        const int otherCell = kind == NORMAL_A ? mEdgeCellB[edgeIndex] : mEdgeCellA[edgeIndex];
        if (otherCell != secondCellIndex) {
            continue;
        }
        removeCellLink(edgeIndex);
        return true;
    }
    return false;
}

bool CellTopology::removeBoundaryLinkOfCell(int cellIndex) {
    if (!isValidCellIndex(cellIndex)) {
        return false;
    }
    const int count = mCellLinkCount[cellIndex];
    for (int slot = 0; slot < count; slot++) {
        if (static_cast<LinkKind>(mCellLinkKinds[cellIndex][slot]) != BOUNDARY) {
            continue;
        }
        removeBoundaryLink(mCellLinkRefs[cellIndex][slot]);
        return true;
    }
    return false;
}

void CellTopology::removeAllLinksOfCell(int cellIndex) {
    if (!isValidCellIndex(cellIndex)) {
        return;
    }
    while (mCellLinkCount[cellIndex] > 0) {
        const LinkKind kind = static_cast<LinkKind>(mCellLinkKinds[cellIndex][0]);
        const int ref = mCellLinkRefs[cellIndex][0];
        if (kind == BOUNDARY) {
            removeBoundaryLink(ref);
        } else {
            removeCellLink(ref);
        }
    }
}

void CellTopology::moveCellIndex(int fromCellIndex, int toCellIndex) {
    if (!isValidCellIndex(fromCellIndex) || !isValidCellIndex(toCellIndex) || fromCellIndex == toCellIndex) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Apply,
                cellularity::verification::SemanticEventKind::TopologyMoveCell,
                cellularity::verification::traceI32(fromCellIndex),
                cellularity::verification::traceI32(toCellIndex),
                cellularity::verification::traceBool(false));
        return;
    }

    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::TopologyMoveCell,
            cellularity::verification::traceI32(fromCellIndex),
            cellularity::verification::traceI32(toCellIndex),
            cellularity::verification::traceBool(true));

    // Cell compaction removes every link from the destination slot before
    // moving the last active cell into it. In that common path the source
    // adjacency already contains every topology record that needs retargeting,
    // so avoid scanning all edges and rebuilding adjacency for every death.
    if (mCellLinkCount[toCellIndex] == 0) {
        const int movedLinkCount = mCellLinkCount[fromCellIndex];
        for (int slot = 0; slot < movedLinkCount; ++slot) {
            const LinkKind kind =
                    static_cast<LinkKind>(mCellLinkKinds[fromCellIndex][slot]);
            const int ref = mCellLinkRefs[fromCellIndex][slot];
            if (kind == NORMAL_A) {
                mEdgeCellA[ref] = toCellIndex;
            } else if (kind == NORMAL_B) {
                mEdgeCellB[ref] = toCellIndex;
            } else {
                mBoundaryCell[ref] = toCellIndex;
            }
        }

        mCellLinkRefs[toCellIndex] = mCellLinkRefs[fromCellIndex];
        mCellLinkKinds[toCellIndex] = mCellLinkKinds[fromCellIndex];
        mCellLinkCount[toCellIndex] = movedLinkCount;
        mCellLinkRefs[fromCellIndex].fill(INVALID_REF);
        mCellLinkKinds[fromCellIndex].fill(BOUNDARY);
        mCellLinkCount[fromCellIndex] = 0;
        markStructureChanged();
        return;
    }

    for (int edgeIndex = 0; edgeIndex < edgeCount(); edgeIndex++) {
        if (mEdgeCellA[edgeIndex] == fromCellIndex) {
            mEdgeCellA[edgeIndex] = toCellIndex;
        }
        if (mEdgeCellB[edgeIndex] == fromCellIndex) {
            mEdgeCellB[edgeIndex] = toCellIndex;
        }
    }
    for (int boundaryIndex = 0; boundaryIndex < boundaryCount(); boundaryIndex++) {
        if (mBoundaryCell[boundaryIndex] == fromCellIndex) {
            mBoundaryCell[boundaryIndex] = toCellIndex;
        }
    }
    rebuildAdjacency();
    markStructureChanged();
}

void CellTopology::clearAll() {
    mEdgeRuntimeIds.clear();
    mEdgeCellA.clear();
    mEdgeCellB.clear();
    mEdgeCreationReasons.clear();
    mEdgeAngleA.clear();
    mEdgeAngleB.clear();
    mEdgeAngleACos.clear();
    mEdgeAngleASin.clear();
    mEdgeAngleBCos.clear();
    mEdgeAngleBSin.clear();
    mEdgeTorsionalStiffness.clear();
    mEdgeRestLength.clear();
    mEdgeRenderStartOffsetAX.clear();
    mEdgeRenderStartOffsetAY.clear();
    mEdgeRenderEndOffsetAX.clear();
    mEdgeRenderEndOffsetAY.clear();

    mBoundaryCell.clear();
    mBoundaryAngleA.clear();
    mBoundaryAngleACos.clear();
    mBoundaryAngleASin.clear();
    mBoundaryAnchorX.clear();
    mBoundaryAnchorY.clear();
    mBoundaryTorsionalStiffness.clear();
    mBoundaryRestLength.clear();
    mBoundaryRenderStartOffsetX.clear();
    mBoundaryRenderStartOffsetY.clear();

    for (int cellIndex = 0; cellIndex < mMaxCellCount; cellIndex++) {
        mCellLinkCount[cellIndex] = 0;
        mCellLinkRefs[cellIndex].fill(INVALID_REF);
        mCellLinkKinds[cellIndex].fill(BOUNDARY);
    }
    markStructureChanged();
}

void CellTopology::rebuildAdjacency() {
    for (int cellIndex = 0; cellIndex < mMaxCellCount; cellIndex++) {
        mCellLinkCount[cellIndex] = 0;
        mCellLinkRefs[cellIndex].fill(INVALID_REF);
        mCellLinkKinds[cellIndex].fill(BOUNDARY);
    }

    for (int edgeIndex = 0; edgeIndex < edgeCount(); edgeIndex++) {
        if (!isValidCellIndex(mEdgeCellA[edgeIndex]) || !isValidCellIndex(mEdgeCellB[edgeIndex])) {
            continue;
        }
        addAdjacency(mEdgeCellA[edgeIndex], NORMAL_A, edgeIndex);
        addAdjacency(mEdgeCellB[edgeIndex], NORMAL_B, edgeIndex);
    }
    for (int boundaryIndex = 0; boundaryIndex < boundaryCount(); boundaryIndex++) {
        if (!isValidCellIndex(mBoundaryCell[boundaryIndex])) {
            continue;
        }
        addAdjacency(mBoundaryCell[boundaryIndex], BOUNDARY, boundaryIndex);
    }
}

bool CellTopology::validate() const {
    std::vector<std::uint32_t> expectedCounts(
            static_cast<std::size_t>(mMaxCellCount));
    return validateWithScratch(expectedCounts.data(), mMaxCellCount);
}

bool CellTopology::validateWithScratch(
        std::uint32_t* expectedCounts,
        int expectedCountCapacity) const {
    if (expectedCountCapacity < mMaxCellCount
            || (mMaxCellCount > 0 && expectedCounts == nullptr)) {
        return false;
    }
    if (static_cast<int>(mEdgeRuntimeIds.size()) != edgeCount()
            || static_cast<int>(mEdgeCellB.size()) != edgeCount()
            || static_cast<int>(mEdgeCreationReasons.size()) != edgeCount()
            || static_cast<int>(mEdgeAngleA.size()) != edgeCount()
            || static_cast<int>(mEdgeAngleB.size()) != edgeCount()
            || static_cast<int>(mEdgeAngleACos.size()) != edgeCount()
            || static_cast<int>(mEdgeAngleASin.size()) != edgeCount()
            || static_cast<int>(mEdgeAngleBCos.size()) != edgeCount()
            || static_cast<int>(mEdgeAngleBSin.size()) != edgeCount()
            || static_cast<int>(mEdgeTorsionalStiffness.size()) != edgeCount()
            || static_cast<int>(mEdgeRestLength.size()) != edgeCount()
            || static_cast<int>(mEdgeRenderStartOffsetAX.size()) != edgeCount()
            || static_cast<int>(mEdgeRenderStartOffsetAY.size()) != edgeCount()
            || static_cast<int>(mEdgeRenderEndOffsetAX.size()) != edgeCount()
            || static_cast<int>(mEdgeRenderEndOffsetAY.size()) != edgeCount()) {
        return false;
    }
    if (static_cast<int>(mBoundaryAngleA.size()) != boundaryCount()
            || static_cast<int>(mBoundaryAngleACos.size()) != boundaryCount()
            || static_cast<int>(mBoundaryAngleASin.size()) != boundaryCount()
            || static_cast<int>(mBoundaryAnchorX.size()) != boundaryCount()
            || static_cast<int>(mBoundaryAnchorY.size()) != boundaryCount()
            || static_cast<int>(mBoundaryTorsionalStiffness.size()) != boundaryCount()
            || static_cast<int>(mBoundaryRestLength.size()) != boundaryCount()
            || static_cast<int>(mBoundaryRenderStartOffsetX.size()) != boundaryCount()
            || static_cast<int>(mBoundaryRenderStartOffsetY.size()) != boundaryCount()) {
        return false;
    }

    if (mMaxCellCount > 0) {
        std::fill(expectedCounts,
                expectedCounts + static_cast<std::size_t>(mMaxCellCount),
                0U);
    }
    for (int edgeIndex = 0; edgeIndex < edgeCount(); edgeIndex++) {
        const TopologyEdgeRuntimeId runtimeId = mEdgeRuntimeIds[edgeIndex];
        const int cellA = mEdgeCellA[edgeIndex];
        const int cellB = mEdgeCellB[edgeIndex];
        if (!isValidTopologyEdgeRuntimeId(runtimeId)
                || runtimeId >= mNextEdgeRuntimeId
                || !isValidCellIndex(cellA)
                || !isValidCellIndex(cellB)
                || !isValidCellLinkCreationReason(
                        mEdgeCreationReasons[edgeIndex])
                || cellA == cellB) {
            return false;
        }
        expectedCounts[cellA]++;
        expectedCounts[cellB]++;
    }
    for (int boundaryIndex = 0; boundaryIndex < boundaryCount(); boundaryIndex++) {
        const int cellIndex = mBoundaryCell[boundaryIndex];
        if (!isValidCellIndex(cellIndex)) {
            return false;
        }
        expectedCounts[cellIndex]++;
    }
    for (int cellIndex = 0; cellIndex < mMaxCellCount; cellIndex++) {
        if (static_cast<std::uint32_t>(mCellLinkCount[cellIndex])
                        != expectedCounts[cellIndex]
                || mCellLinkCount[cellIndex] < 0
                || mCellLinkCount[cellIndex] > MAX_NEIGHBORS) {
            return false;
        }
        for (int slot = 0; slot < mCellLinkCount[cellIndex]; slot++) {
            const LinkKind kind = static_cast<LinkKind>(mCellLinkKinds[cellIndex][slot]);
            const int ref = mCellLinkRefs[cellIndex][slot];
            if (kind == NORMAL_A) {
                if (ref < 0 || ref >= edgeCount() || mEdgeCellA[ref] != cellIndex) {
                    return false;
                }
            } else if (kind == NORMAL_B) {
                if (ref < 0 || ref >= edgeCount() || mEdgeCellB[ref] != cellIndex) {
                    return false;
                }
            } else if (kind == BOUNDARY) {
                if (ref < 0 || ref >= boundaryCount() || mBoundaryCell[ref] != cellIndex) {
                    return false;
                }
            } else {
                return false;
            }
        }
    }
    return true;
}

int CellTopology::collectLinksOfCell(int cellIndex, std::array<CellLinkSnapshot, MAX_NEIGHBORS>& out) const {
    if (!isValidCellIndex(cellIndex)) {
        return 0;
    }
    const int count = mCellLinkCount[cellIndex];
    for (int slot = 0; slot < count; slot++) {
        const LinkKind kind = static_cast<LinkKind>(mCellLinkKinds[cellIndex][slot]);
        const int ref = mCellLinkRefs[cellIndex][slot];
        CellLinkSnapshot& snapshot = out[slot];
        if (kind == BOUNDARY) {
            snapshot.isBoundary = true;
            snapshot.connectedCellIndex = BOUNDARY_CONNECTION_INDEX;
            snapshot.creationReason = CellLinkCreationReason::UNSPECIFIED;
            snapshot.angleA = mBoundaryAngleA[ref];
            snapshot.angleB = 0.0;
            snapshot.boundaryAnchorX = mBoundaryAnchorX[ref];
            snapshot.boundaryAnchorY = mBoundaryAnchorY[ref];
            snapshot.torsionalStiffness = mBoundaryTorsionalStiffness[ref];
            snapshot.restLength = mBoundaryRestLength[ref];
            continue;
        }
        snapshot.isBoundary = false;
        snapshot.creationReason = mEdgeCreationReasons[ref];
        snapshot.boundaryAnchorX = 0.0;
        snapshot.boundaryAnchorY = 0.0;
        snapshot.torsionalStiffness = mEdgeTorsionalStiffness[ref];
        snapshot.restLength = mEdgeRestLength[ref];
        if (kind == NORMAL_A) {
            snapshot.connectedCellIndex = mEdgeCellB[ref];
            snapshot.angleA = mEdgeAngleA[ref];
            snapshot.angleB = mEdgeAngleB[ref];
        } else {
            snapshot.connectedCellIndex = mEdgeCellA[ref];
            snapshot.angleA = mEdgeAngleB[ref];
            snapshot.angleB = mEdgeAngleA[ref];
        }
    }
    return count;
}

int CellTopology::edgeCellA(int edgeIndex) const {
    return mEdgeCellA[edgeIndex];
}

int CellTopology::edgeCellB(int edgeIndex) const {
    return mEdgeCellB[edgeIndex];
}

CellLinkCreationReason CellTopology::edgeCreationReason(int edgeIndex) const {
    return mEdgeCreationReasons[edgeIndex];
}

double CellTopology::edgeAngleA(int edgeIndex) const {
    return mEdgeAngleA[edgeIndex];
}

double CellTopology::edgeAngleB(int edgeIndex) const {
    return mEdgeAngleB[edgeIndex];
}

double CellTopology::edgeAngleACos(int edgeIndex) const {
    return mEdgeAngleACos[edgeIndex];
}

double CellTopology::edgeAngleASin(int edgeIndex) const {
    return mEdgeAngleASin[edgeIndex];
}

double CellTopology::edgeAngleBCos(int edgeIndex) const {
    return mEdgeAngleBCos[edgeIndex];
}

double CellTopology::edgeAngleBSin(int edgeIndex) const {
    return mEdgeAngleBSin[edgeIndex];
}

float CellTopology::edgeTorsionalStiffness(int edgeIndex) const {
    return mEdgeTorsionalStiffness[edgeIndex];
}

float CellTopology::edgeRestLength(int edgeIndex) const {
    return mEdgeRestLength[edgeIndex];
}

TopologyEdgeRuntimeId CellTopology::edgeRuntimeId(int edgeIndex) const noexcept {
    assert(edgeIndex >= 0 && edgeIndex < edgeCount());
    return mEdgeRuntimeIds[static_cast<std::size_t>(edgeIndex)];
}

TopologyEdgeRuntimeId CellTopology::allocateEdgeRuntimeId() {
    if (mNextEdgeRuntimeId > kMaximumTopologyEdgeRuntimeId) {
        throw std::overflow_error(
                "CellTopology runtime edge identity space is exhausted");
    }
    return mNextEdgeRuntimeId++;
}

void CellTopology::markStructureChanged() {
    if (mStructuralRevision == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
                "CellTopology structural revision space is exhausted");
    }
    ++mStructuralRevision;
}

int CellTopology::boundaryCell(int boundaryIndex) const {
    return mBoundaryCell[boundaryIndex];
}

double CellTopology::boundaryAngleA(int boundaryIndex) const {
    return mBoundaryAngleA[boundaryIndex];
}

double CellTopology::boundaryAngleACos(int boundaryIndex) const {
    return mBoundaryAngleACos[boundaryIndex];
}

double CellTopology::boundaryAngleASin(int boundaryIndex) const {
    return mBoundaryAngleASin[boundaryIndex];
}

double CellTopology::boundaryAnchorX(int boundaryIndex) const {
    return mBoundaryAnchorX[boundaryIndex];
}

double CellTopology::boundaryAnchorY(int boundaryIndex) const {
    return mBoundaryAnchorY[boundaryIndex];
}

float CellTopology::boundaryTorsionalStiffness(int boundaryIndex) const {
    return mBoundaryTorsionalStiffness[boundaryIndex];
}

float CellTopology::boundaryRestLength(int boundaryIndex) const {
    return mBoundaryRestLength[boundaryIndex];
}

float CellTopology::edgeRenderStartOffsetAX(int edgeIndex) const {
    return mEdgeRenderStartOffsetAX[edgeIndex];
}

float CellTopology::edgeRenderStartOffsetAY(int edgeIndex) const {
    return mEdgeRenderStartOffsetAY[edgeIndex];
}

float CellTopology::edgeRenderEndOffsetAX(int edgeIndex) const {
    return mEdgeRenderEndOffsetAX[edgeIndex];
}

float CellTopology::edgeRenderEndOffsetAY(int edgeIndex) const {
    return mEdgeRenderEndOffsetAY[edgeIndex];
}

void CellTopology::setEdgeRenderOffsets(int edgeIndex,
        float startOffsetAX,
        float startOffsetAY,
        float endOffsetAX,
        float endOffsetAY) {
    mEdgeRenderStartOffsetAX[edgeIndex] = startOffsetAX;
    mEdgeRenderStartOffsetAY[edgeIndex] = startOffsetAY;
    mEdgeRenderEndOffsetAX[edgeIndex] = endOffsetAX;
    mEdgeRenderEndOffsetAY[edgeIndex] = endOffsetAY;
}

float CellTopology::boundaryRenderStartOffsetX(int boundaryIndex) const {
    return mBoundaryRenderStartOffsetX[boundaryIndex];
}

float CellTopology::boundaryRenderStartOffsetY(int boundaryIndex) const {
    return mBoundaryRenderStartOffsetY[boundaryIndex];
}

void CellTopology::setBoundaryRenderStartOffset(int boundaryIndex, float startOffsetX, float startOffsetY) {
    mBoundaryRenderStartOffsetX[boundaryIndex] = startOffsetX;
    mBoundaryRenderStartOffsetY[boundaryIndex] = startOffsetY;
}

bool CellTopology::isValidCellIndex(int cellIndex) const {
    return cellIndex >= 0 && cellIndex < mMaxCellCount;
}

void CellTopology::addAdjacency(int cellIndex, LinkKind kind, int ref) {
    const int slot = mCellLinkCount[cellIndex]++;
    mCellLinkRefs[cellIndex][slot] = ref;
    mCellLinkKinds[cellIndex][slot] = static_cast<uint8_t>(kind);
}

bool CellTopology::removeAdjacency(int cellIndex, LinkKind kind, int ref) {
    if (!isValidCellIndex(cellIndex)) {
        return false;
    }
    const int count = mCellLinkCount[cellIndex];
    for (int slot = 0; slot < count; slot++) {
        if (mCellLinkRefs[cellIndex][slot] != ref
                || static_cast<LinkKind>(mCellLinkKinds[cellIndex][slot]) != kind) {
            continue;
        }
        const int lastSlot = count - 1;
        if (slot != lastSlot) {
            mCellLinkRefs[cellIndex][slot] = mCellLinkRefs[cellIndex][lastSlot];
            mCellLinkKinds[cellIndex][slot] = mCellLinkKinds[cellIndex][lastSlot];
        }
        mCellLinkRefs[cellIndex][lastSlot] = INVALID_REF;
        mCellLinkKinds[cellIndex][lastSlot] = static_cast<uint8_t>(BOUNDARY);
        mCellLinkCount[cellIndex] = lastSlot;
        return true;
    }
    return false;
}

bool CellTopology::updateAdjacencyRef(int cellIndex, LinkKind kind, int oldRef, int newRef) {
    if (!isValidCellIndex(cellIndex)) {
        return false;
    }
    const int count = mCellLinkCount[cellIndex];
    for (int slot = 0; slot < count; slot++) {
        if (mCellLinkRefs[cellIndex][slot] != oldRef
                || static_cast<LinkKind>(mCellLinkKinds[cellIndex][slot]) != kind) {
            continue;
        }
        mCellLinkRefs[cellIndex][slot] = newRef;
        return true;
    }
    return false;
}
