#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "core/CellData.h"
#include "core/CellType.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "tests/semantic/RelaxedSemanticContract.h"

namespace {

constexpr std::int64_t kWorldSeed = 0x00A0'0206'0920LL;

void initializeGene(Gene& gene, CellType type, int geneIndex) {
    cellularity::GeneBuilder builder(gene);
    builder.setCellType(type);
    builder.setColor(
            0, 0.25f + 0.01f * static_cast<float>(geneIndex % 3));
    builder.setColor(1, 0.45f);
    builder.setColor(2, 0.35f);
    builder.setSplitMass(CellData::MAX_SPLIT_MASS);
    builder.setSplitRatio(0.5f);
    builder.setMassFlowPriority(1.0f);
    builder.setLinkStrength(0.5f);
    const cellularity::GeneIndex self(
            static_cast<cellularity::GeneIndex::Value>(geneIndex));
    builder.setChild1GeneIndex(self);
    builder.setChild2GeneIndex(self);
    builder.setStaysAlive(true);
    builder.setInitialCompatibilityFlag(geneIndex == 0);
    builder.setIntegerValue(
            Gene::MAX_CONNECTION_COUNT_INDEX, CellData::MAX_NEIGHBORS - 1);
    builder.setVirusFromGeneIndex(self);
    builder.setVirusToGeneIndex(self);
    for (int output = 0; output < CellData::NEUROCYTE_OUTPUT_COUNT; output++) {
        builder.setIntegerValue(
                Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + output,
                output);
    }
    builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
}

int addCell(SimulationWorld& world, CellType type, double x, double y) {
    CellData& cells = world.mCellData;
    const int index = cells.allocateNewCell();
    if (index < 0) {
        throw std::runtime_error("Level 2 fixture exceeded Cell capacity");
    }
    cellularity::FixedGenome genome;
    for (int geneIndex = 0; geneIndex < CellData::MAX_GENES_COUNT;
            geneIndex++) {
        initializeGene(genome[geneIndex], type, geneIndex);
    }
    if (!cells.replaceGenome(index, genome, cellularity::GeneIndex(0))) {
        throw std::runtime_error("Level 2 fixture Genome import failed");
    }
    cells.mPositionX[index] = x;
    cells.mPositionY[index] = y;
    cells.mPositionZ[index] = 0.0;
    cells.mRadius[index] = 0.02;
    cells.refreshRadiusSq(index);
    cells.mMass[index] = CellData::MASS_PER_AREA * 0.02 * 0.02;
    cells.mNitrates[index] = CellData::MAX_NITRATES;
    cells.mAge[index] = 0.1;
    cells.mOriginTag[index] = SimulationWorld::USER_CELL_ORIGIN;
    cells.mTelomeresCount[index] = -1;
    cells.mFertilizationTarget[index] = -1;
    cells.mAngle[index] = 0.0;
    cells.mCachedAngle[index] = 0.0;
    cells.mCachedAngleCos[index] = 1.0;
    cells.mCachedAngleSin[index] = 0.0;
    const cellularity::GeneView activeGene = cells.getActiveGeneView(index);
    cells.mColor[index][0] = activeGene.color(0);
    cells.mColor[index][1] = activeGene.color(1);
    cells.mColor[index][2] = activeGene.color(2);
    cells.mColor[index][3] = 1.0f;
    for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT; signal++) {
        cells.mSignalSubstance[index][signal] =
                0.03125f * static_cast<float>(index + signal + 1);
        cells.mSignalSubstance[index][CellData::SIGNAL_SUBSTANCE_COUNT + signal] = -0.015625f * static_cast<float>(index + signal + 1);
        cells.mNewSignalSubstance[index][signal] = 0.0f;
        cells.mSignalSubstanceOut[index][signal] =
                0.0078125f * static_cast<float>(signal - 1);
    }
    cells.markCellAsAlive(index);
    return index;
}

std::unique_ptr<SimulationWorld> makeLevel2World() {
    Environment environment =
            Environment::constructDefaultEnvironment(16, 8, 1.0);
    environment.mRandom.setSeed(kWorldSeed);
    environment.addRandom = false;
    environment.aging = false;
    environment.mobileFood = false;
    environment.dieEdge = false;
    environment.mFoodSpeed = 0.0;
    environment.mLightSpeed = 0.0;
    environment.mRadiation = 0.0;
    environment.mTestFoodDecay = 0.0;
    environment.mTestIdleMetabolismScale = 0.0;
    environment.mTestFlagellocyteCostScale = 0.0;
    environment.mTestPhagocyteEatSpeedScale = 0.0;
    environment.mViscosity = 1.0;
    environment.mDensity = 0.0;
    environment.mDensityGradient = 0.0;
    environment.syncDensityFieldFromLegacyGradient();
    environment.mGravity = 0.0;
    environment.syncGravityFieldFromLegacyGravity();
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;

    auto world = std::make_unique<SimulationWorld>(false, environment);
    const int source = addCell(*world, CellType::NEUROCYTE, -0.0675, 0.0);
    const int relay = addCell(*world, CellType::PHAGOCYTE, -0.0225, 0.0);
    const int sink = addCell(*world, CellType::NEUROCYTE, 0.0225, 0.0);
    const int lipocyte = addCell(*world, CellType::LIPOCYTE, 0.0675, 0.0);
    addCell(*world, CellType::PHAGOCYTE, -0.015, 0.20);
    addCell(*world, CellType::MYOCYTE, 0.015, 0.20);
    addCell(*world, CellType::CILIOCYTE, -0.015, -0.20);
    addCell(*world, CellType::PHAGOCYTE, 0.015, -0.20);
    addCell(*world, CellType::GLUEOCYTE, 0.99, 0.0);

    world->mCellData.addLink(
            source, relay, CellLinkCreationReason::UNSPECIFIED,
            0.0, Environment::PI, 0.5f, 0.0f);
    world->mCellData.addLink(
            relay, sink, CellLinkCreationReason::UNSPECIFIED,
            0.0, Environment::PI, 0.5f, 0.0f);
    world->mCellData.addLink(
            sink, lipocyte, CellLinkCreationReason::UNSPECIFIED,
            0.0, Environment::PI, 0.5f, 0.0f);
    world->mCellData.editActiveGene(
            source,
            [](cellularity::GeneBuilder& sourceGene) {
                for (int output = 0;
                        output < CellData::NEUROCYTE_OUTPUT_COUNT;
                        output++) {
                    const int valueIndex =
                            Gene::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + output;
                    sourceGene.setParametricFormulaType(valueIndex, 0);
                    sourceGene.setParametricVariableB(
                            valueIndex,
                            0.125f * static_cast<float>(output + 1));
                }
            });
    world->mCellData.mSignalSubstance[sink][0] = 0.75f;
    for (int cell = 0; cell < world->mCellData.mCellCount; cell++) {
        world->mCellData.mOldLinkCount[cell] =
                world->mCellData.mTopology.linkCount(cell);
    }
    world->mFoodData.addFood(0.55, 0.45, 0.02, 0.0f,
            world->mEnvironment);
    world->mFoodData.addFood(-0.50, 0.40, 0.018, 0.25f,
            world->mEnvironment);
    world->updateGrids();
    return world;
}

} // namespace

int main() {
    try {
        using namespace cellularity::semantic_tests::level2;
        verifyDiscreteTraceProjection();
        verifyRenderSourceLinkComparison();

        ContractConfiguration configuration;
        configuration.shortHorizonUpdates = 32;
        configuration.longRunUpdates = 1024;
        configuration.visualCheckpointInterval = 128;

        const WorldConfigurator reference = [](SimulationWorld& world) {
            world.mCellData.setSignalUpdateAlgorithm(
                    CellSignalUpdateAlgorithm::GLOBAL_EDGE);
        };
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
        const WorldConfigurator candidate = [](SimulationWorld& world) {
            world.mCellData.setSignalUpdateAlgorithm(
                    CellSignalUpdateAlgorithm::CELL_OWNED_CSR);
        };
        constexpr const char* candidateName = "cell-owned-signal-csr";
#else
        const WorldConfigurator candidate = reference;
        constexpr const char* candidateName = "level2-reference-self-check";
#endif
        verifyCandidate(candidateName, makeLevel2World, reference, candidate,
                configuration);

        std::cout << "simulation semantic contract Level 2 passed: exact "
                     "discrete traces/state, bounded numerical comparison, "
                     "long-run invariants, and Incubate render-source baseline\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "simulation semantic contract Level 2 failed: "
                  << error.what() << '\n';
        return 1;
    }
}
