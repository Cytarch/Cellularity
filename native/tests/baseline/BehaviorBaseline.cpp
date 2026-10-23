#include "tests/baseline/BehaviorBaseline.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "app/NativeWorldEditing.h"
#include "core/GeneAccess.h"
#include "core/SimulationRandom.h"
#include "formats/release/ReleaseSubstrateCodec.h"

namespace cellularity::behavior_baseline {
namespace {

constexpr std::array<ScenarioDefinition, 6> SCENARIOS{{
        {"empty", 10'001, 64,
                "9ad1594185913652728ed02bba8ea586b75ff224284f2c0a93e1191413048685",
                {0, 0, 0, 0, 0, 0, 14217}},
        {"small_colony", 20'002, 120,
                "3af4414f8c4617a07e7eee4404747b1ac88cd007b18bcdfcc59c0384eb672736",
                {4, 4, 0, 3, 0, 16, 25137}},
        {"dense_collision", 30'003, 80,
                "009ebe91c17e9cc4851ffb631812653181e85a2c76d44abdb0f126cf8ede6505",
                {49, 49, 0, 0, 0, 196, 26665}},
        {"signaling", 40'004, 120,
                "13c1bc5c0d7cdeee0d01d26ee7a9b9c9988f7c15171da3f65c4b1f1999f5625f",
                {3, 3, 0, 2, 0, 12, 25049}},
        {"mutation", 50'005, 90,
                "3a9516619b38ef827b36ea38f7de0c49910abbed784eb871221b4f8df60e074c",
                {1, 1, 0, 0, 18, 4, 24153}},
        {"food_heavy", 60'006, 120,
                "6b7d28c75946c18f4e85f11bc9884a471d1271468ea8885b0ce009731e19cb77",
                {8, 8, 302, 0, 0, 32, 25529}},
}};

Environment makeEnvironment(int maxCells, int maxFoods, double radius, int64_t seed) {
    Environment environment = Environment::constructDefaultEnvironment(maxCells, maxFoods, radius);
    environment.mRandom.setSeed(seed);
    environment.mFoodSpeed = 0.0;
    environment.mLightAngle = 0.0;
    environment.mLightAngleCos = 1.0;
    environment.mLightAngleSin = 0.0;
    environment.mLightSpeed = 0.0;
    environment.mRadiation = 0.0;
    environment.mTestFoodDecay = 0.0;
    environment.aging = false;
    environment.addRandom = false;
    environment.mobileFood = false;
    return environment;
}

cellularity::GenomeData makeGenome(CellType type, float colorOffset) {
    cellularity::GenomeData genome(CellData::MAX_GENES_COUNT);
    for (int geneIndex = 0; geneIndex < CellData::MAX_GENES_COUNT; geneIndex++) {
        Gene& gene = genome[geneIndex];
        cellularity::GeneBuilder builder(gene);
        builder.setColor(0, 0.25f + colorOffset);
        builder.setColor(1, 0.55f - colorOffset * 0.5f);
        builder.setColor(2, 0.35f + colorOffset * 0.25f);
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
        builder.setCellType(type);
        builder.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX, CellData::MAX_NEIGHBORS - 1);
        for (int outputIndex = 0; outputIndex < CellData::NEUROCYTE_OUTPUT_COUNT; outputIndex++) {
            builder.setIntegerValue(
                    Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + outputIndex,
                    outputIndex);
        }
        builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }
    return genome;
}

int addCell(SimulationWorld& world,
        CellType type,
        double x,
        double y,
        double angle,
        float colorOffset = 0.0f) {
    const auto genome = makeGenome(type, colorOffset);
    if (!addUserCellToWorld(world, x, y, genome)) {
        throw std::runtime_error("Could not create behavior baseline cell");
    }
    const int cellIndex = world.mCellData.mCellCount - 1;
    CellData& cells = world.mCellData;
    cells.mAngle[cellIndex] = angle;
    cells.mAngleCytoplasm[cellIndex] = -angle * 0.5;
    cells.mSeldomCounter[cellIndex] = 0;
    cells.mOldLinkCount[cellIndex] = 0;
    for (int signalIndex = 0; signalIndex < CellData::SIGNAL_SUBSTANCE_COUNT; signalIndex++) {
        cells.mSignalSubstance[cellIndex][signalIndex] = CellData::SIGNAL_SUBSTANCE_INITIAL;
        cells.mSignalSubstance[cellIndex][CellData::SIGNAL_SUBSTANCE_COUNT + signalIndex] = 0.0f;
        cells.mNewSignalSubstance[cellIndex][signalIndex] = 0.0f;
        cells.mSignalSubstanceOut[cellIndex][signalIndex] = 0.0f;
    }
    return cellIndex;
}

void finishSetup(SimulationWorld& world) {
    for (int cellIndex = 0; cellIndex < world.mCellData.mCellCount; cellIndex++) {
        world.mCellData.mOldLinkCount[cellIndex] = world.mCellData.mTopology.linkCount(cellIndex);
    }
    world.updateGrids();
}

std::unique_ptr<SimulationWorld> makeEmpty(const ScenarioDefinition& scenario) {
    return std::make_unique<SimulationWorld>(false, makeEnvironment(16, 16, 1.0, scenario.seed));
}

std::unique_ptr<SimulationWorld> makeSmallColony(const ScenarioDefinition& scenario) {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(32, 64, 1.0, scenario.seed));
    const int center = addCell(*world, CellType::PHAGOCYTE, 0.0, 0.0, 0.0, 0.00f);
    const int flagellocyte = addCell(*world, CellType::FLAGELLOCYTE, 0.045, 0.0, 0.0, 0.04f);
    const int photocyte = addCell(*world, CellType::PHOTOCYTE, -0.045, 0.0, 0.0, 0.08f);
    const int lipocyte = addCell(*world, CellType::LIPOCYTE, 0.0, 0.045, 0.0, 0.12f);
    world->mCellData.addLink(
            center, flagellocyte, CellLinkCreationReason::UNSPECIFIED,
            0.0, Environment::PI, 0.6f, 0.0f);
    world->mCellData.addLink(
            center, photocyte, CellLinkCreationReason::UNSPECIFIED,
            Environment::PI, 0.0, 0.6f, 0.0f);
    world->mCellData.addLink(
            center, lipocyte, CellLinkCreationReason::UNSPECIFIED,
            Environment::PI / 2.0, -Environment::PI / 2.0, 0.6f, 0.0f);
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeDenseCollision(const ScenarioDefinition& scenario) {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(96, 32, 2.0, scenario.seed));
    constexpr int side = 7;
    constexpr double spacing = 0.041;
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            const int ordinal = (x + y * side) % 3;
            const CellType type = ordinal == 0 ? CellType::PHAGOCYTE
                    : ordinal == 1             ? CellType::KERATINOCYTE
                                               : CellType::BUOYOCYTE;
            addCell(*world,
                    type,
                    (x - side / 2) * spacing,
                    (y - side / 2) * spacing,
                    (x - y) * 0.07,
                    static_cast<float>(ordinal) * 0.03f);
        }
    }
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeSignaling(const ScenarioDefinition& scenario) {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(16, 16, 1.0, scenario.seed));
    const int source = addCell(*world, CellType::NEUROCYTE, -0.05, 0.0, 0.0, 0.02f);
    const int relay = addCell(*world, CellType::PHAGOCYTE, 0.0, 0.0, 0.0, 0.06f);
    const int sink = addCell(*world, CellType::NEUROCYTE, 0.05, 0.0, 0.0, 0.10f);
    world->mCellData.addLink(
            source, relay, CellLinkCreationReason::UNSPECIFIED,
            0.0, Environment::PI, 0.5f, 0.0f);
    world->mCellData.addLink(
            relay, sink, CellLinkCreationReason::UNSPECIFIED,
            0.0, Environment::PI, 0.5f, 0.0f);
    for (int outputIndex = 0; outputIndex < CellData::NEUROCYTE_OUTPUT_COUNT; outputIndex++) {
        const int valueIndex =
                Gene::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + outputIndex;
        world->mCellData.editActiveGene(
                source,
                [=](cellularity::GeneBuilder& builder) {
                    builder.setParametricFormulaType(valueIndex, 0);
                    builder.setParametricVariableB(
                            valueIndex,
                            0.2f + static_cast<float>(outputIndex) * 0.1f);
                });
    }
    world->mCellData.mSignalSubstance[sink][0] = 0.8f;
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeMutation(const ScenarioDefinition& scenario) {
    Environment environment = makeEnvironment(16, 16, 1.0, scenario.seed);
    environment.mRadiation = 1.0;
    environment.mTestMutationCoef = 0.04;
    environment.mTestIdleMetabolismScale = 0.0;
    environment.mMutationWeights.fill(0.0);
    for (int colorWeight = 0; colorWeight < Environment::MUTATION_COLOR_WEIGHT_COUNT; colorWeight++) {
        environment.mMutationWeights[Environment::MUTATION_COLOR_WEIGHT_OFFSET + colorWeight] = 1.0;
    }
    auto world = std::make_unique<SimulationWorld>(false, environment);
    addCell(*world, CellType::PHAGOCYTE, 0.0, 0.0, 0.0, 0.05f);
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeFoodHeavy(const ScenarioDefinition& scenario) {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(48, 512, 1.2, scenario.seed));
    for (int cellIndex = 0; cellIndex < 8; cellIndex++) {
        const double angle = 2.0 * Environment::PI * cellIndex / 8.0;
        addCell(*world,
                CellType::PHAGOCYTE,
                0.18 * std::cos(angle),
                0.18 * std::sin(angle),
                angle,
                static_cast<float>(cellIndex) * 0.01f);
    }
    for (int foodIndex = 0; foodIndex < 320; foodIndex++) {
        const int x = foodIndex % 20;
        const int y = foodIndex / 20;
        const double foodX = (x - 9.5) * 0.035;
        const double foodY = (y - 7.5) * 0.035;
        const float lipidLayer = foodIndex % 5 == 0 ? 0.25f : 0.0f;
        world->mFoodData.addFood(foodX, foodY, 0.012 + (foodIndex % 3) * 0.002, lipidLayer, world->mEnvironment);
    }
    finishSetup(*world);
    return world;
}

bool finite(double value) {
    return std::isfinite(value);
}

} // namespace

const std::array<ScenarioDefinition, 6>& scenarios() {
    return SCENARIOS;
}

std::unique_ptr<SimulationWorld> createScenarioWorld(const ScenarioDefinition& scenario) {
    SimulationRandom::global.setSeed(scenario.seed ^ 0x5a5a5a5aLL);
    const std::string name(scenario.name);
    if (name == "empty")
        return makeEmpty(scenario);
    if (name == "small_colony")
        return makeSmallColony(scenario);
    if (name == "dense_collision")
        return makeDenseCollision(scenario);
    if (name == "signaling")
        return makeSignaling(scenario);
    if (name == "mutation")
        return makeMutation(scenario);
    if (name == "food_heavy")
        return makeFoodHeavy(scenario);
    throw std::invalid_argument("Unknown behavior baseline scenario: " + name);
}

ScenarioResult summarizeWorld(const SimulationWorld& world) {
    ScenarioResult result;
    result.state.cells = world.mCellData.mCellCount;
    result.state.foods = world.mFoodData.mFoodCount;
    result.state.links = world.mCellData.mTopology.edgeCount() + world.mCellData.mTopology.boundaryCount();
    for (int cellIndex = 0; cellIndex < world.mCellData.mCellCount; cellIndex++) {
        if (!world.mCellData.mIsDead[cellIndex]) {
            result.state.livingCells++;
        }
        result.state.mutations += world.mCellData.mMutationCount[cellIndex];
        for (int signalIndex = 0; signalIndex < CellData::SIGNAL_SUBSTANCE_COUNT; signalIndex++) {
            if (world.mCellData.mSignalSubstance[cellIndex][signalIndex] != 0.0f) {
                result.state.activeSignalValues++;
            }
        }
        result.finiteState = result.finiteState
                && finite(world.mCellData.mPositionX[cellIndex])
                && finite(world.mCellData.mPositionY[cellIndex])
                && finite(world.mCellData.mVelocityX[cellIndex])
                && finite(world.mCellData.mVelocityY[cellIndex])
                && finite(world.mCellData.mMass[cellIndex])
                && finite(world.mCellData.mNitrates[cellIndex]);
    }
    const auto snapshot =
            save_format::release::encodeReleaseSubstrate(world);
    result.state.snapshotBytes = snapshot.bytes.size();
    result.digest = save_format::release::substrateSemanticDigest(world);
    return result;
}

ScenarioResult runScenario(const ScenarioDefinition& scenario) {
    std::unique_ptr<SimulationWorld> world = createScenarioWorld(scenario);
    for (int update = 0; update < scenario.updateCount; update++) {
        world->update(false, 1.0);
    }
    return summarizeWorld(*world);
}

std::unique_ptr<SimulationWorld> createRepresentativePerformanceWorld() {
    Environment environment = makeEnvironment(256, 1600, 1.5, REPRESENTATIVE_PERFORMANCE_SEED);
    environment.mTestIdleMetabolismScale = 0.25;
    auto world = std::make_unique<SimulationWorld>(false, environment);
    constexpr int side = 14;
    constexpr double spacing = 0.047;
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            const CellType type = (x + y) % 4 == 0 ? CellType::PHAGOCYTE
                    : (x + y) % 4 == 1             ? CellType::FLAGELLOCYTE
                    : (x + y) % 4 == 2             ? CellType::PHOTOCYTE
                                                   : CellType::KERATINOCYTE;
            addCell(*world,
                    type,
                    (x - 6.5) * spacing,
                    (y - 6.5) * spacing,
                    (x * 3 - y * 2) * 0.03,
                    static_cast<float>((x + y) % 4) * 0.025f);
        }
    }
    for (int foodIndex = 0; foodIndex < 900; foodIndex++) {
        const int x = foodIndex % 30;
        const int y = foodIndex / 30;
        world->mFoodData.addFood((x - 14.5) * 0.042,
                (y - 14.5) * 0.042,
                0.01 + (foodIndex % 4) * 0.002,
                foodIndex % 7 == 0 ? 0.2f : 0.0f,
                world->mEnvironment);
    }
    finishSetup(*world);
    return world;
}

} // namespace cellularity::behavior_baseline
