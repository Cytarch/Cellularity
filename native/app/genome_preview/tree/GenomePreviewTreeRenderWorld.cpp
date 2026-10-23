#include "app/genome_preview/tree/GenomePreviewTreeRenderWorld.h"

#include <algorithm>
#include <stdexcept>

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/GenomeMetadata.h"
#include "render/NativeSimulationWorldRenderer.h"

GenomePreviewTreeRenderWorld::~GenomePreviewTreeRenderWorld() = default;

const SimulationWorld* GenomePreviewTreeRenderWorld::update(
        const SimulationWorld& sourceWorld,
        const std::vector<GenomePreviewTreeNode>& nodes,
        NativeSimulationWorldRenderer& renderer) {
    int nodeCount = static_cast<int>(nodes.size());
    int renderCapacity = std::max(sourceWorld.mEnvironment.maxCellCount, std::max(1, nodeCount));
    if (mWorld == nullptr || mWorld->mEnvironment.maxCellCount < renderCapacity) {
        Environment environment = sourceWorld.mEnvironment;
        environment.maxCellCount = renderCapacity;
        environment.mIsPreviewMode = true;
        mWorld = std::make_unique<SimulationWorld>(true, environment);
        renderer.reset(renderCapacity);
    }

    SimulationWorld& renderWorld = *mWorld;
    renderWorld.mEnvironment.mIsPreviewMode = true;
    renderWorld.mEnvironment.mLightAngle = sourceWorld.mEnvironment.mLightAngle;
    renderWorld.mEnvironment.mLightRange = sourceWorld.mEnvironment.mLightRange;
    renderWorld.mEnvironment.mLightIntensity = sourceWorld.mEnvironment.mLightIntensity;
    renderWorld.mEnvironment.mRadius = sourceWorld.mEnvironment.mRadius;
    CellData& cellData = renderWorld.mCellData;
    renderWorld.invalidateCellContactCache();
    cellData.resizeActiveCellCount(nodeCount);
    cellData.mTopology.clearAll();
    renderWorld.mFoodData.resizeActiveFoodCount(0);
    renderWorld.mDeadSplashData.resizeActiveDeadSplashCount(0);
    renderWorld.mTime = sourceWorld.mTime;

    for (int nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++) {
        const GenomePreviewTreeNode& node = nodes[nodeIndex];
        cellularity::GenomeData genome =
                cellData.materializeGenome(nodeIndex);
        cellularity::GeneBuilder(genome[0]).copyFrom(
                cellularity::GeneView(node.renderGene));
        if (!cellularity::applyGenomeMetadataToCompatibilityFlags(
                    genome.data(),
                    genome.size(),
                    cellularity::GenomeMetadata{cellularity::GeneIndex(0)})) {
            throw std::logic_error("Genome preview metadata is invalid");
        }
        if (!cellData.replaceGenome(
                    nodeIndex, genome, cellularity::GeneIndex(0))) {
            throw std::runtime_error("Genome preview pool import failed");
        }
        cellData.mPositionX[nodeIndex] = node.x;
        cellData.mPositionY[nodeIndex] = node.y;
        cellData.mPositionZ[nodeIndex] = 0.0;
        cellData.mVelocityX[nodeIndex] = 0.0;
        cellData.mVelocityY[nodeIndex] = 0.0;
        cellData.mForceX[nodeIndex] = 0.0;
        cellData.mForceY[nodeIndex] = 0.0;
        cellData.mForceZ[nodeIndex] = 0.0;
        cellData.mRadius[nodeIndex] = node.radius;
        cellData.refreshRadiusSq(nodeIndex);
        cellData.mMass[nodeIndex] = node.radius * node.radius * CellData::MASS_PER_AREA;
        cellData.mNitrates[nodeIndex] = CellData::MAX_NITRATES;
        cellData.mLipids[nodeIndex] = 0.0f;
        cellData.mToxin[nodeIndex] = 0.0f;
        cellData.mToxication[nodeIndex] = 0.0f;
        const cellularity::GeneView renderGene(node.renderGene);
        cellData.mColor[nodeIndex][0] = renderGene.color(0);
        cellData.mColor[nodeIndex][1] = renderGene.color(1);
        cellData.mColor[nodeIndex][2] = renderGene.color(2);
        cellData.mColor[nodeIndex][3] = 1.0f;
        cellData.mAngle[nodeIndex] = 0.0;
        cellData.mAngleCytoplasm[nodeIndex] = 0.0;
        cellData.mAngularVelocity[nodeIndex] = 0.0;
        cellData.mAngularVelocityCytoplasm[nodeIndex] = 0.0;
        cellData.mOldLinkCount[nodeIndex] = 0;
        cellData.clearCellCollisionContacts(nodeIndex);
        cellData.mAge[nodeIndex] = CellData::AGE_OF_CONSENT;
        cellData.mIsMirrored[nodeIndex] = false;
        cellData.mSelectTag[nodeIndex] = 0;
        cellData.mSwimAnimationTime[nodeIndex] = 0.0f;
        cellData.mSwimAnimationTime2[nodeIndex] = 0.0f;
        for (int signalIndex = 0; signalIndex < CellData::SIGNAL_SUBSTANCE_COUNT; signalIndex++) {
            cellData.mSignalSubstance[nodeIndex][signalIndex] = CellData::SIGNAL_SUBSTANCE_INITIAL;
            cellData.mNewSignalSubstance[nodeIndex][signalIndex] = CellData::SIGNAL_SUBSTANCE_INITIAL;
        }
        cellData.markCellAsAlive(nodeIndex);
    }

    return &renderWorld;
}
