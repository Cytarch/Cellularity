#include "app/NativeWorldEditing.h"

#include <algorithm>
#include <cmath>

#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/GenomeMetadata.h"
#include "core/SimulationRandom.h"

void resetWorldContents(SimulationWorld& world) {
    world.invalidateCellContactCache();
    world.mCellData.resizeActiveCellCount(0);
    world.mCellData.mTopology.clearAll();
    world.mFoodData.resizeActiveFoodCount(0);
    world.mDeadSplashData.resizeActiveDeadSplashCount(0);
    world.mTime = 0.0;
    world.mEnvironment.mLightAngle = Environment::PI / 2.0;
    world.mEnvironment.mLightAngleCos = std::cos(world.mEnvironment.mLightAngle);
    world.mEnvironment.mLightAngleSin = std::sin(world.mEnvironment.mLightAngle);
}

void resetGenomePreviewWorld(SimulationWorld& world) {
    resetWorldContents(world);
}

bool addUserCellToWorld(SimulationWorld& world,
        double worldX,
        double worldY,
        const cellularity::GenomeData& genome) {
    if (world.mEnvironment.geometry().isOutsideOrOnBoundary(worldX, worldY)) {
        return false;
    }
    const auto genomeMetadata =
            cellularity::extractGenomeMetadataFromCompatibilityFlags(
                    genome.data(), genome.size());
    if (!genomeMetadata.has_value()) {
        return false;
    }

    CellData& cellData = world.mCellData;
    if (cellData.mCellCount == world.mEnvironment.maxCellCount && cellData.mCellCount > 0) {
        const int targetCellIndex = SimulationRandom::global.nextInt(cellData.mCellCount);
        const int lastCellIndex = cellData.mCellCount - 1;
        cellData.mTopology.removeAllLinksOfCell(targetCellIndex);
        if (targetCellIndex != lastCellIndex) {
            cellData.moveLastCellForCompaction(
                    lastCellIndex, targetCellIndex);
            cellData.mTopology.moveCellIndex(lastCellIndex, targetCellIndex);
        }
        cellData.releaseLastCellSlot();
    }

    int newCellIndex = cellData.allocateNewCell();
    if (newCellIndex == -1) {
        return false;
    }

    (void)cellData.assignNewOrganism(newCellIndex);

    cellData.mPositionX[newCellIndex] = worldX;
    cellData.mPositionY[newCellIndex] = worldY;
    cellData.mOriginTag[newCellIndex] = SimulationWorld::USER_CELL_ORIGIN;
    cellData.mSelectTag[newCellIndex] = 0;
    cellData.mNitrates[newCellIndex] = CellData::MAX_NITRATES;
    cellData.mTelomeresCount[newCellIndex] = -1;
    cellData.mIsMirrored[newCellIndex] = false;

    if (!cellData.replaceGenome(
                newCellIndex, genome, genomeMetadata->initialGeneIndex)) {
        cellData.releaseLastCellSlot();
        return false;
    }
    const cellularity::GeneView activeGene =
            cellData.getActiveGeneView(newCellIndex);
    if (activeGene.cellType() == CellType::LIPOCYTE) {
        cellData.mMass[newCellIndex] = CellData::INITIAL_MASS * CellData::MAX_LIPO_MASS / CellData::MAX_MASS;
        if (world.mEnvironment.mIsPreviewMode) {
            cellData.mLipids[newCellIndex] = CellData::MAX_LIPIDS;
        }
    } else {
        cellData.mMass[newCellIndex] = CellData::INITIAL_MASS;
    }
    cellData.mRadius[newCellIndex] = std::min(std::sqrt(cellData.mMass[newCellIndex] / CellData::MASS_PER_AREA),
            CellData::MAX_CELL_RADIUS * 0.99);
    cellData.refreshRadiusSq(newCellIndex);
    cellData.mColor[newCellIndex][0] = activeGene.color(0);
    cellData.mColor[newCellIndex][1] = activeGene.color(1);
    cellData.mColor[newCellIndex][2] = activeGene.color(2);
    cellData.mColor[newCellIndex][3] = 1.0f;
    cellData.mVelocityX[newCellIndex] = 0.0;
    cellData.mVelocityY[newCellIndex] = 0.0;
    cellData.markCellAsAlive(newCellIndex);
    cellData.mAngularVelocity[newCellIndex] = 0.0;
    cellData.mAngularVelocityCytoplasm[newCellIndex] = 0.0;
    cellData.mAngle[newCellIndex] = 0.0;
    cellData.mAngleCytoplasm[newCellIndex] = world.mEnvironment.mIsPreviewMode
            ? 0.0
            : SimulationRandom::global.nextUniformSymmetric() * Environment::PI;
    cellData.mAge[newCellIndex] = 0.0;
    cellData.mMutationCount[newCellIndex] = 0;
    cellData.mPendingMutationCount[newCellIndex] = 0;
    cellData.mSwimAnimationTime[newCellIndex] = 0.0f;
    for (int i = 0; i < CellData::SIGNAL_SUBSTANCE_COUNT; i++) {
        cellData.mSignalSubstance[newCellIndex][i] = CellData::SIGNAL_SUBSTANCE_INITIAL;
    }
    world.updateGrids();
    return true;
}
