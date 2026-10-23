#include "render/frame/RenderContactCollector.h"

#include <cassert>
#include <cmath>

#include "core/CellData.h"
#include "core/Environment.h"
#include "render/frame/RenderContactChordBuffer.h"
#include "render/frame/RenderContactLimits.h"

namespace {

bool isFertilizationPair(const CellData& cellData,
        int firstCellIndex,
        int secondCellIndex,
        double firstRadiusSq,
        double secondRadiusSq,
        double distanceSq) {
    if (!cellData.isCellReadyForFertilization(firstCellIndex)
            || !cellData.isCellReadyForFertilization(secondCellIndex)) {
        return false;
    }

    const double threshold =
            CellData::MIN_CELL_RADIUS * CellData::MIN_CELL_RADIUS * 0.01;
    return std::abs(firstRadiusSq - secondRadiusSq)
            > distanceSq - threshold;
}

} // namespace

RenderContactCollector::RenderContactCollector()
        : mCurrentGeometryBroadphase(2.0 * CellData::MAX_CELL_RADIUS) {}

void RenderContactCollector::collect(RenderContactInput input,
        const CellData& cellData,
        const Environment& environment,
        RenderContactChordBuffer& output) {
    output.beginCollection(cellData.mCellCount);
    switch (input) {
        case RenderContactInput::FreshPhysicsContacts:
            collectPhysicsContacts(cellData, output);
            break;
        case RenderContactInput::CurrentGeometry:
            collectCurrentGeometry(cellData, output);
            break;
    }
    collectBoundaryContacts(cellData, environment, output);
}

void RenderContactCollector::collectPhysicsContacts(
        const CellData& cellData,
        RenderContactChordBuffer& output) const {
    CellData::CellCollisionGeometry geometry;
    for (int currentCellIndex = 0;
            currentCellIndex < cellData.mCellCount;
            currentCellIndex++) {
        if (cellData.mIsDead[currentCellIndex]) {
            continue;
        }

        const int contactBegin =
                cellData.cellCollisionContactBegin(currentCellIndex);
        const int contactEnd =
                cellData.cellCollisionContactEnd(currentCellIndex);
        for (int contactIndex = contactBegin;
                contactIndex < contactEnd;
                contactIndex++) {
            const int collidedCellIndex =
                    cellData.cellCollisionContactTarget(contactIndex);
            if (!cellData.isValidCellCollisionTarget(
                        currentCellIndex, collidedCellIndex)) {
                continue;
            }

            cellData.getCellCollisionContactGeometry(contactIndex, geometry);
            appendCellPairChords(cellData,
                    output,
                    currentCellIndex,
                    collidedCellIndex,
                    geometry.contactChordLengthSquared,
                    geometry.distance,
                    geometry.normalX,
                    geometry.normalY);
        }
    }
}

void RenderContactCollector::collectCurrentGeometry(
        const CellData& cellData,
        RenderContactChordBuffer& output) {
    mCurrentGeometryBroadphase.buildBaseline(cellData);
    auto visitor = [&cellData, &output](int firstCellIndex,
                           int secondCellIndex,
                           double distanceX,
                           double distanceY,
                           double distanceSq,
                           double firstRadius,
                           double secondRadius) {
        if (cellData.mIsDead[firstCellIndex]
                || cellData.mIsDead[secondCellIndex]
                || distanceSq == 0.0) {
            return;
        }

        const double firstRadiusSq = firstRadius * firstRadius;
        const double secondRadiusSq = secondRadius * secondRadius;
        if (isFertilizationPair(cellData,
                    firstCellIndex,
                    secondCellIndex,
                    firstRadiusSq,
                    secondRadiusSq,
                    distanceSq)
                || secondRadiusSq - firstRadiusSq > distanceSq
                || firstRadiusSq - secondRadiusSq > distanceSq) {
            return;
        }

        CellData::CellCollisionGeometry geometry;
        if (!cellData.calculateCellCollisionGeometryFromDistance(
                    firstCellIndex,
                    secondCellIndex,
                    distanceX,
                    distanceY,
                    distanceSq,
                    geometry)) {
            return;
        }

        appendCellPairChords(cellData,
                output,
                firstCellIndex,
                secondCellIndex,
                geometry.contactChordLengthSquared,
                geometry.distance,
                geometry.normalX,
                geometry.normalY);
    };
    mCurrentGeometryBroadphase.forEachOverlappingPairInline<false>(visitor);
}

void RenderContactCollector::collectBoundaryContacts(
        const CellData& cellData,
        const Environment& environment,
        RenderContactChordBuffer& output) {
    const auto geometry = environment.geometry();
    for (int currentCellIndex = 0;
            currentCellIndex < cellData.mCellCount;
            currentCellIndex++) {
        if (cellData.mIsDead[currentCellIndex]) {
            continue;
        }

        const double positionX = cellData.mPositionX[currentCellIndex];
        const double positionY = cellData.mPositionY[currentCellIndex];
        const double cellRadius = cellData.mRadius[currentCellIndex];
        cellularity::SubstrateBoundaryContact contact;
        if (!geometry.cellBoundaryContact(positionX, positionY, cellRadius, contact)) {
            continue;
        }
        appendBoundaryChord(cellData,
                output,
                currentCellIndex,
                geometry,
                contact);
    }
}

void RenderContactCollector::appendBoundaryChord(
        const CellData& cellData,
        RenderContactChordBuffer& output,
        int currentCellIndex,
        const cellularity::SubstrateGeometry& geometry,
        const cellularity::SubstrateBoundaryContact& contact) {
    if (output.activeChordCount(currentCellIndex) >= cellularity::render::kMaximumRenderContactChordsPerCell) {
        return;
    }

    const double cellRadius = cellData.mRadius[currentCellIndex];
    const double cellPositionX = cellData.mPositionX[currentCellIndex];
    const double cellPositionY = cellData.mPositionY[currentCellIndex];
    const auto chord = geometry.cellBoundaryChord(cellPositionX, cellPositionY, cellRadius, contact);

    RenderContactChord contactChord;
    contactChord.x1 = chord.x1;
    contactChord.y1 = chord.y1;
    contactChord.x2 = chord.x2;
    contactChord.y2 = chord.y2;
    contactChord.neighborCellIndex = CellData::BOUNDARY_CONNECTION_INDEX;
    contactChord.isDiscarded = false;
    contactChord.joinsNextChord = false;
    const bool appended = output.tryAppend(currentCellIndex, contactChord);
    assert(appended);
    (void)appended;
}

void RenderContactCollector::appendCellPairChords(
        const CellData& cellData,
        RenderContactChordBuffer& output,
        int currentCellIndex,
        int collidedCellIndex,
        double contactChordLengthSquared,
        double distance,
        double normalX,
        double normalY) {
    for (const RenderContactChord& contactChord : output.activeChords(currentCellIndex)) {
        if (contactChord.neighborCellIndex == collidedCellIndex) {
            return;
        }
    }
    for (const RenderContactChord& contactChord : output.activeChords(collidedCellIndex)) {
        if (contactChord.neighborCellIndex == currentCellIndex) {
            return;
        }
    }

    if (output.activeChordCount(currentCellIndex) >= cellularity::render::kMaximumCellContactChordsPerCell
            || output.activeChordCount(collidedCellIndex)
                    >= cellularity::render::kMaximumCellContactChordsPerCell) {
        return;
    }

    const double collidedCellRadius = cellData.mRadius[collidedCellIndex];
    const double currentCellRadius = cellData.mRadius[currentCellIndex];
    const double distanceToMidline =
            (distance * distance - collidedCellRadius * collidedCellRadius
                    + currentCellRadius * currentCellRadius)
            / (2.0 * distance);
    const double contactMidpointX = distanceToMidline * normalX;
    const double contactMidpointY = distanceToMidline * normalY;
    const double currentCellPositionX = cellData.mPositionX[currentCellIndex];
    const double currentCellPositionY = cellData.mPositionY[currentCellIndex];
    const double collidedCellPositionX = cellData.mPositionX[collidedCellIndex];
    const double collidedCellPositionY = cellData.mPositionY[collidedCellIndex];
    const double contactChordLength = std::sqrt(contactChordLengthSquared);
    const double perpendicularScale = contactChordLength / (2.0 * distance);

    RenderContactChord currentCellChord;
    currentCellChord.x1 = contactMidpointX
            + (collidedCellPositionY - currentCellPositionY)
                    * perpendicularScale;
    currentCellChord.y1 = contactMidpointY
            - (collidedCellPositionX - currentCellPositionX)
                    * perpendicularScale;
    currentCellChord.x2 = contactMidpointX
            - (collidedCellPositionY - currentCellPositionY)
                    * perpendicularScale;
    currentCellChord.y2 = contactMidpointY
            + (collidedCellPositionX - currentCellPositionX)
                    * perpendicularScale;
    currentCellChord.neighborCellIndex = collidedCellIndex;
    currentCellChord.isDiscarded = false;
    currentCellChord.joinsNextChord = false;

    RenderContactChord collidedCellChord;
    collidedCellChord.x1 = currentCellChord.x2
            + currentCellPositionX - collidedCellPositionX;
    collidedCellChord.y1 = currentCellChord.y2
            + currentCellPositionY - collidedCellPositionY;
    collidedCellChord.x2 = currentCellChord.x1
            + currentCellPositionX - collidedCellPositionX;
    collidedCellChord.y2 = currentCellChord.y1
            + currentCellPositionY - collidedCellPositionY;
    collidedCellChord.neighborCellIndex = currentCellIndex;
    collidedCellChord.isDiscarded = false;
    collidedCellChord.joinsNextChord = false;

    const bool appendedCurrent =
            output.tryAppend(currentCellIndex, currentCellChord);
    const bool appendedCollided =
            output.tryAppend(collidedCellIndex, collidedCellChord);
    assert(appendedCurrent && appendedCollided);
    (void)appendedCurrent;
    (void)appendedCollided;
}
