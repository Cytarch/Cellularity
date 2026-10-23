#include "ThroughputWorkloads.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include "PerfOverlap.h"
#include "PerfScenarios.h"
#include "PerfSignal.h"
#include "PerfState.h"
#include "app/NativeWorldEditing.h"
#include "core/GeneAccess.h"
#include "core/SimulationRandom.h"
#include "tests/support/StableDigest.h"

namespace {

constexpr std::int64_t THROUGHPUT_WORKLOAD_SEED = 0x00A0'0206'0919LL;

Environment makeEnvironment(int maxCells, int maxFoods, double radius) {
    Environment environment = Environment::constructDefaultEnvironment(maxCells, maxFoods, radius);
    environment.mRandom.setSeed(THROUGHPUT_WORKLOAD_SEED);
    environment.mFoodSpeed = 0.0;
    environment.mLightAngle = 0.0;
    environment.mLightAngleCos = 1.0;
    environment.mLightAngleSin = 0.0;
    environment.mLightSpeed = 0.0;
    environment.mRadiation = 0.0;
    environment.mTestFoodDecay = 0.0;
    environment.mTestIdleMetabolismScale = 0.0;
    environment.aging = false;
    environment.addRandom = false;
    environment.mobileFood = false;
    return environment;
}

cellularity::GenomeData makeDynamicGenome(CellType type,
        std::size_t geneCount) {
    cellularity::GenomeData genome(geneCount);
    for (std::size_t geneIndex = 0; geneIndex < geneCount; geneIndex++) {
        GeneData& gene = genome[geneIndex];
        cellularity::GeneBuilder builder(gene);
        builder.setColor(0, 0.35f);
        builder.setColor(1, 0.65f);
        builder.setColor(2, 0.45f);
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
        builder.setVirusFromGeneIndex(self);
        builder.setVirusToGeneIndex(self);
        for (int outputIndex = 0; outputIndex < CellData::NEUROCYTE_OUTPUT_COUNT; outputIndex++) {
            builder.setIntegerValue(
                    Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + outputIndex,
                    outputIndex);
            const int valueIndex =
                    Gene::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + outputIndex;
            builder.setParametricFormulaType(valueIndex, 0);
            builder.setParametricVariableB(
                    valueIndex,
                    0.15f + 0.05f * static_cast<float>(outputIndex));
        }
        builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }
    return genome;
}

cellularity::GenomeData makeGenome(CellType type) {
    return makeDynamicGenome(type, cellularity::FIXED_GENOME_GENE_COUNT);
}

int addCell(SimulationWorld& world,
        CellType type,
        double x,
        double y,
        double radius = std::sqrt(CellData::INITIAL_MASS / CellData::MASS_PER_AREA)) {
    const auto genome = makeGenome(type);
    if (!addUserCellToWorld(world, x, y, genome)) {
        throw std::runtime_error("Could not add throughput workload cell");
    }
    const int cellIndex = world.mCellData.mCellCount - 1;
    CellData& cells = world.mCellData;
    cells.mAngle[cellIndex] = 0.031 * cellIndex;
    cells.mAngleCytoplasm[cellIndex] = -0.017 * cellIndex;
    cells.mSeldomCounter[cellIndex] = 0;
    cells.mMass[cellIndex] = CellData::MASS_PER_AREA * radius * radius;
    cells.mRadius[cellIndex] = radius;
    cells.refreshRadiusSq(cellIndex);
    return cellIndex;
}

void finishSetup(SimulationWorld& world) {
    for (int cellIndex = 0; cellIndex < world.mCellData.mCellCount; cellIndex++) {
        world.mCellData.mOldLinkCount[cellIndex] = world.mCellData.mTopology.linkCount(cellIndex);
    }
    world.updateGrids();
}

void addGrid(SimulationWorld& world,
        int columns,
        int rows,
        double spacing,
        CellType type,
        bool mixedRadii = false) {
    const double centerX = 0.5 * (columns - 1);
    const double centerY = 0.5 * (rows - 1);
    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            const int ordinal = row * columns + column;
            const double radius = mixedRadii
                    ? 0.012 + 0.004 * static_cast<double>(ordinal % 5)
                    : std::sqrt(CellData::INITIAL_MASS / CellData::MASS_PER_AREA);
            addCell(world,
                    type,
                    (column - centerX) * spacing,
                    (row - centerY) * spacing,
                    radius);
        }
    }
}

std::unique_ptr<SimulationWorld> makeGridWorkload(int columns,
        int rows,
        double spacing,
        double worldRadius,
        bool mixedRadii = false) {
    const int count = columns * rows;
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(count + 32, 32, worldRadius));
    addGrid(*world, columns, rows, spacing, CellType::KERATINOCYTE, mixedRadii);
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeGenomeLengthWorkload(std::size_t geneCount) {
    std::unique_ptr<SimulationWorld> world = makeGridWorkload(
            20, 20, 0.065, 2.0);
    const cellularity::GenomeData genome = makeDynamicGenome(
            CellType::KERATINOCYTE, geneCount);
    for (int cellIndex = 0;
            cellIndex < world->mCellData.mCellCount;
            ++cellIndex) {
        if (!world->mCellData.replaceGenome(
                    cellIndex, genome, cellularity::GeneIndex(0))) {
            throw std::runtime_error(
                    "Could not install dynamic Genome benchmark data");
        }
    }
    return world;
}

std::unique_ptr<SimulationWorld> makeAllOverlap() {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(192, 16, 1.0));
    constexpr int count = 128;
    for (int index = 0; index < count; index++) {
        const double angle = 2.0 * Environment::PI * index / count;
        const double ring = 0.00015 * (1 + index % 5);
        addCell(*world, CellType::KERATINOCYTE, ring * std::cos(angle), ring * std::sin(angle));
    }
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeTypedCollision(CellType actor) {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(128, 32, 2.0));
    constexpr int pairCount = 36;
    for (int pair = 0; pair < pairCount; pair++) {
        const int column = pair % 6;
        const int row = pair / 6;
        const double x = (column - 2.5) * 0.19;
        const double y = (row - 2.5) * 0.19;
        addCell(*world, actor, x - 0.018, y);
        addCell(*world, CellType::KERATINOCYTE, x + 0.018, y);
    }
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeCiliocyteCapacity() {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(96, 16, 1.0));
    addCell(*world, CellType::CILIOCYTE, 0.0, 0.0, 0.029);
    constexpr int neighborCount = 48;
    for (int index = 0; index < neighborCount; index++) {
        const double angle = 2.0 * Environment::PI * index / neighborCount;
        const double distance = 0.020 + 0.003 * (index % 3);
        addCell(*world,
                CellType::KERATINOCYTE,
                distance * std::cos(angle),
                distance * std::sin(angle),
                0.016);
    }
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeCiliocyteBoundary() {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(32, 16, 1.0));
    for (int index = 0; index < 16; index++) {
        const double angle = 2.0 * Environment::PI * index / 16.0;
        addCell(*world,
                CellType::CILIOCYTE,
                0.985 * std::cos(angle),
                0.985 * std::sin(angle),
                0.02);
    }
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeLifecycle(const std::string& name) {
    Environment environment = makeEnvironment(160, 32, 1.5);
    if (name == "lifecycle_refill") {
        environment.addRandom = true;
    }
    auto world = std::make_unique<SimulationWorld>(false, environment);
    addGrid(*world, 8, 8, 0.08, CellType::PHAGOCYTE);
    if (name == "lifecycle_grow") {
        for (int cellIndex = 0; cellIndex < world->mCellData.mCellCount; cellIndex++) {
            world->mCellData.editActiveGene(
                    cellIndex,
                    [](cellularity::GeneBuilder& gene) {
                        gene.setSplitMass(CellData::MIN_SPLIT_MASS);
                    });
        }
    } else {
        for (int cellIndex = 1; cellIndex < world->mCellData.mCellCount; cellIndex += 2) {
            world->mCellData.markCellAsDead(cellIndex);
        }
    }
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeFertilization() {
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(64, 16, 1.0));
    for (int pair = 0; pair < 12; pair++) {
        const double y = (pair - 5.5) * 0.09;
        const int first = addCell(*world, CellType::GAMETE, -0.014, y);
        const int second = addCell(*world, CellType::GAMETE, 0.014, y);
        world->mCellData.mAge[first] = CellData::AGE_OF_CONSENT + 1.0;
        world->mCellData.mAge[second] = CellData::AGE_OF_CONSENT + 1.0;
    }
    finishSetup(*world);
    return world;
}

std::unique_ptr<SimulationWorld> makeHighFood(bool mobile) {
    Environment environment = makeEnvironment(256, 2400, 2.0);
    environment.mobileFood = mobile;
    auto world = std::make_unique<SimulationWorld>(false, environment);
    addGrid(*world, 14, 14, 0.07, CellType::KERATINOCYTE);
    for (int foodIndex = 0; foodIndex < 2000; foodIndex++) {
        const int column = foodIndex % 50;
        const int row = foodIndex / 50;
        const double x = (column - 24.5) * 0.055;
        const double y = (row - 19.5) * 0.055;
        const int added = world->mFoodData.addFood(
                x, y, 0.012 + 0.001 * (foodIndex % 4), foodIndex % 9 == 0 ? 0.2f : 0.0f,
                world->mEnvironment);
        if (mobile && added >= 0) {
            world->mFoodData.mVelocityX[added] = static_cast<float>(0.03 * std::sin(foodIndex * 0.17));
            world->mFoodData.mVelocityY[added] = static_cast<float>(0.03 * std::cos(foodIndex * 0.11));
        }
    }
    finishSetup(*world);
    return world;
}

enum class SignalCellMix {
    COMMON,
    NEURO,
    LIPOCYTE,
    MIXED
};

CellType signalCellType(SignalCellMix mix, int row, int column) {
    const int stripe = (row + column) % 3;
    switch (mix) {
        case SignalCellMix::COMMON:
            return CellType::PHAGOCYTE;
        case SignalCellMix::NEURO:
            return stripe == 0 ? CellType::NEUROCYTE : CellType::PHAGOCYTE;
        case SignalCellMix::LIPOCYTE:
            return stripe == 0 ? CellType::LIPOCYTE : CellType::PHAGOCYTE;
        case SignalCellMix::MIXED:
            if (stripe == 0) {
                return CellType::NEUROCYTE;
            }
            return stripe == 1 ? CellType::LIPOCYTE : CellType::PHAGOCYTE;
    }
    throw std::logic_error("Unknown signal workload Cell mix");
}

std::unique_ptr<SimulationWorld> makeHighTopologySignal(
        SignalCellMix mix,
        bool filterLipocyteEdges = false,
        bool replaceLipocytesWithCommonCells = false) {
    constexpr int side = 15;
    auto world = std::make_unique<SimulationWorld>(false, makeEnvironment(side * side + 32, 32, 1.5));
    for (int row = 0; row < side; row++) {
        for (int column = 0; column < side; column++) {
            addCell(*world,
                    signalCellType(mix, row, column),
                    (column - 0.5 * (side - 1)) * 0.052,
                    (row - 0.5 * (side - 1)) * 0.052);
        }
    }
    for (int row = 0; row < side; row++) {
        for (int column = 0; column < side; column++) {
            const int cell = row * side + column;
            if (column + 1 < side) {
                world->mCellData.addLink(
                        cell,
                        cell + 1,
                        CellLinkCreationReason::UNSPECIFIED,
                        0.0,
                        Environment::PI,
                        0.5f,
                        0.0f);
            }
            if (row + 1 < side) {
                world->mCellData.addLink(
                        cell,
                        cell + side,
                        CellLinkCreationReason::UNSPECIFIED,
                        Environment::PI / 2.0,
                        -Environment::PI / 2.0,
                        0.5f,
                        0.0f);
            }
            for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT; signal++) {
                world->mCellData.mSignalSubstance[cell][signal] =
                        0.1f + 0.01f * static_cast<float>((cell + signal) % 20);
            }
        }
    }
    if (filterLipocyteEdges) {
        for (int cellIndex = 0;
                cellIndex < world->mCellData.mCellCount;
                ++cellIndex) {
            if (world->mCellData.mActiveCellType[cellIndex]
                    == CellType::LIPOCYTE) {
                world->mCellData.mTopology.removeAllLinksOfCell(cellIndex);
            }
        }
    }
    if (replaceLipocytesWithCommonCells) {
        for (int cellIndex = 0;
                cellIndex < world->mCellData.mCellCount;
                ++cellIndex) {
            if (world->mCellData.mActiveCellType[cellIndex]
                    != CellType::LIPOCYTE) {
                continue;
            }
            cellularity::GenomeData genome =
                    world->mCellData.materializeGenome(cellIndex);
            for (GeneData& gene : genome) {
                cellularity::GeneBuilder(gene).setCellType(
                        CellType::PHAGOCYTE);
            }
            const bool replaced = world->mCellData.replaceGenome(
                    cellIndex,
                    genome,
                    world->mCellData.activeGeneIndex(cellIndex));
            if (!replaced) {
                throw std::runtime_error("Benchmark Genome import failed");
            }
        }
    }
    finishSetup(*world);
    return world;
}

template <typename Integer>
void appendInteger(std::vector<std::uint8_t>& bytes, Integer value) {
    using Unsigned = std::make_unsigned_t<Integer>;
    Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t byte = 0; byte < sizeof(Integer); byte++) {
        bytes.push_back(static_cast<std::uint8_t>(bits >> (byte * 8U)));
    }
}

void appendDouble(std::vector<std::uint8_t>& bytes, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    appendInteger(bytes, bits);
}

void appendPairTrace(SimulationWorld& world,
        int step,
        std::vector<std::uint8_t>& bytes,
        std::uint64_t& pairCount) {
    appendInteger(bytes, static_cast<std::int32_t>(step));
    world.mCellSweepBroadphase.forEachOverlappingPairConfiguredInline(
            [&](int first,
                    int second,
                    double distanceX,
                    double distanceY,
                    double distanceSq,
                    double firstRadius,
                    double secondRadius) {
                appendInteger(bytes, static_cast<std::int32_t>(first));
                appendInteger(bytes, static_cast<std::int32_t>(second));
                appendDouble(bytes, distanceX);
                appendDouble(bytes, distanceY);
                appendDouble(bytes, distanceSq);
                appendDouble(bytes, firstRadius);
                appendDouble(bytes, secondRadius);
                pairCount++;
            });
    appendInteger(bytes, std::numeric_limits<std::uint32_t>::max());
}

void appendEndpointTrace(const CellData& cells,
        int step,
        std::vector<std::uint8_t>& bytes,
        std::uint64_t& contactCount) {
    appendInteger(bytes, static_cast<std::int32_t>(step));
    appendInteger(bytes, static_cast<std::int32_t>(cells.mCellCount));
    for (int cell = 0; cell < cells.mCellCount; cell++) {
        const int begin = cells.mCellContactBuffer.begin(cell);
        const int end = cells.mCellContactBuffer.end(cell);
        appendInteger(bytes, static_cast<std::int32_t>(cell));
        appendInteger(bytes, static_cast<std::int32_t>(end - begin));
        for (int contact = begin; contact < end; contact++) {
            appendInteger(bytes, static_cast<std::int32_t>(cells.mCellContactBuffer.collidedCellIndex[contact]));
            const CellData::CellContactBuffer::StoredGeometry& geometry =
                    cells.mCellContactBuffer.geometry[contact];
            appendDouble(bytes, geometry.distanceX);
            appendDouble(bytes, geometry.distanceY);
            appendDouble(bytes, geometry.distanceSq);
            contactCount++;
        }
    }
}

void appendIntentTrace(const CellData& cells,
        int step,
        std::vector<std::uint8_t>& bytes,
        std::uint64_t& intentCount) {
    const CellData::CellCollisionIntentBuffer& intents = cells.mCellCollisionIntentBuffer;
    appendInteger(bytes, static_cast<std::int32_t>(step));
    appendInteger(bytes, static_cast<std::int32_t>(intents.count));
    for (int intent = 0; intent < intents.count; intent++) {
        appendInteger(bytes, static_cast<std::int32_t>(intents.currentCellIndex[intent]));
        appendInteger(bytes, static_cast<std::int32_t>(intents.collidedCellIndex[intent]));
        appendDouble(bytes, intents.distanceX[intent]);
        appendDouble(bytes, intents.distanceY[intent]);
        appendDouble(bytes, intents.distance[intent]);
        appendDouble(bytes, intents.contactChordLengthSquared[intent]);
        intentCount++;
    }
}

} // namespace

std::vector<std::string> throughputWorkloadNames() {
    return {
            "empty",
            "one_cell",
            "two_cells",
            "tiny_16",
            "tiny_32",
            "sparse",
            "medium",
            "dense",
            "mixed_radii",
            "all_overlap",
            "genome_length_1",
            "genome_length_2",
            "genome_length_40",
            "genome_length_150",
            "genome_length_256",
            "lifecycle_grow",
            "lifecycle_shrink_compact",
            "lifecycle_refill",
            "fertilization",
            "pure_force",
            "typed_virocyte",
            "typed_glueocyte",
            "typed_devorocyte",
            "typed_secrocyte",
            "typed_ciliocyte",
            "ciliocyte_capacity",
            "ciliocyte_boundary",
            "high_food_stationary",
            "high_food_mobile",
            "high_topology_signal",
            "high_topology_signal_common",
            "high_topology_signal_lipocyte",
            "high_topology_signal_mixed",
            "high_topology_signal_lipocyte_prefiltered",
            "high_topology_signal_lipocyte_cache_upper_bound",
    };
}

std::unique_ptr<SimulationWorld> createThroughputWorkload(const std::string& name) {
    SimulationRandom::global.setSeed(THROUGHPUT_WORKLOAD_SEED ^ 0x5a5a5a5aLL);
    if (name == "empty")
        return std::make_unique<SimulationWorld>(false, makeEnvironment(16, 16, 1.0));
    if (name == "one_cell")
        return makeGridWorkload(1, 1, 0.05, 1.0);
    if (name == "two_cells")
        return makeGridWorkload(2, 1, 0.035, 1.0);
    if (name == "tiny_16")
        return makeGridWorkload(4, 4, 0.05, 1.0);
    if (name == "tiny_32")
        return makeGridWorkload(8, 4, 0.05, 1.0);
    if (name == "sparse")
        return makeGridWorkload(16, 16, 0.16, 3.0);
    if (name == "medium")
        return makeGridWorkload(20, 20, 0.065, 2.0);
    if (name == "dense")
        return makeGridWorkload(20, 20, 0.040, 1.5);
    if (name == "mixed_radii")
        return makeGridWorkload(20, 20, 0.050, 1.5, true);
    if (name == "all_overlap")
        return makeAllOverlap();
    if (name == "genome_length_1")
        return makeGenomeLengthWorkload(1);
    if (name == "genome_length_2")
        return makeGenomeLengthWorkload(2);
    if (name == "genome_length_40")
        return makeGenomeLengthWorkload(40);
    if (name == "genome_length_150")
        return makeGenomeLengthWorkload(150);
    if (name == "genome_length_256")
        return makeGenomeLengthWorkload(256);
    if (name == "lifecycle_grow" || name == "lifecycle_shrink_compact" || name == "lifecycle_refill") {
        return makeLifecycle(name);
    }
    if (name == "fertilization")
        return makeFertilization();
    if (name == "pure_force")
        return makeTypedCollision(CellType::KERATINOCYTE);
    if (name == "typed_virocyte")
        return makeTypedCollision(CellType::VIROCYTE);
    if (name == "typed_glueocyte")
        return makeTypedCollision(CellType::GLUEOCYTE);
    if (name == "typed_devorocyte")
        return makeTypedCollision(CellType::DEVOROCYTE);
    if (name == "typed_secrocyte")
        return makeTypedCollision(CellType::SECROCYTE);
    if (name == "typed_ciliocyte")
        return makeTypedCollision(CellType::CILIOCYTE);
    if (name == "ciliocyte_capacity")
        return makeCiliocyteCapacity();
    if (name == "ciliocyte_boundary")
        return makeCiliocyteBoundary();
    if (name == "high_food_stationary")
        return makeHighFood(false);
    if (name == "high_food_mobile")
        return makeHighFood(true);
    if (name == "high_topology_signal") {
        return makeHighTopologySignal(SignalCellMix::NEURO);
    }
    if (name == "high_topology_signal_common") {
        return makeHighTopologySignal(SignalCellMix::COMMON);
    }
    if (name == "high_topology_signal_lipocyte") {
        return makeHighTopologySignal(SignalCellMix::LIPOCYTE);
    }
    if (name == "high_topology_signal_mixed") {
        return makeHighTopologySignal(SignalCellMix::MIXED);
    }
    if (name == "high_topology_signal_lipocyte_prefiltered") {
        return makeHighTopologySignal(SignalCellMix::LIPOCYTE, true, false);
    }
    if (name == "high_topology_signal_lipocyte_cache_upper_bound") {
        return makeHighTopologySignal(SignalCellMix::LIPOCYTE, true, true);
    }
    throw std::invalid_argument("Unknown throughput workload: " + name);
}

PerfJson runThroughputSyntheticProfileJson(const std::string& name,
        int warmupSteps,
        int profileSteps,
        bool detailed) {
    std::unique_ptr<SimulationWorld> world = createThroughputWorkload(name);
    PerfJson report = detailed
            ? runStageBreakdownProfileJson(*world, name, 0, warmupSteps, profileSteps)
            : runFullUpdateProfileJson(*world, name, 0, warmupSteps, profileSteps);
    report["scenario"] = detailed ? "simulation/synthetic_stage_breakdown"
                                  : "simulation/synthetic_full_update";
    report["workload"] = name;
    report["seed"] = THROUGHPUT_WORKLOAD_SEED;
    return report;
}

PerfJson runThroughputSyntheticSignalProfileJson(
        const std::string& name,
        int warmupSteps,
        int profileSteps,
        bool detailed,
        CellSignalUpdateAlgorithm algorithm) {
    std::unique_ptr<SimulationWorld> world = createThroughputWorkload(name);
    world->mCellData.setSignalUpdateAlgorithm(algorithm);
    PerfJson report = detailed
            ? runStageBreakdownProfileJson(
                      *world, name, 0, warmupSteps, profileSteps)
            : runFullUpdateProfileJson(
                      *world, name, 0, warmupSteps, profileSteps);
    report["scenario"] = detailed
            ? "simulation/synthetic_stage_breakdown"
            : "simulation/synthetic_full_update";
    report["workload"] = name;
    report["seed"] = THROUGHPUT_WORKLOAD_SEED;
    report["signalUpdateAlgorithm"] = signalUpdateAlgorithmName(algorithm);
    return report;
}

PerfJson runThroughputOrderedTraceJson(SimulationWorld& world,
        const std::string& workload,
        int steps) {
    if (steps < 1) {
        throw std::invalid_argument("Throughput trace steps must be positive");
    }
    const PerfJson initial = capturePerfWorldState(world);
    std::vector<std::uint8_t> pairs;
    std::vector<std::uint8_t> endpoints;
    std::vector<std::uint8_t> intents;
    std::uint64_t pairCount = 0;
    std::uint64_t contactCount = 0;
    std::uint64_t intentCount = 0;
    for (int step = 0; step < steps; step++) {
        world.update1(false, 1.0);
        appendPairTrace(world, step, pairs, pairCount);
        appendEndpointTrace(world.mCellData, step, endpoints, contactCount);
        appendIntentTrace(world.mCellData, step, intents, intentCount);
        world.update2(false, 1.0);
    }
    const PerfJson final = capturePerfWorldState(world);
    return {
            {"schemaVersion", 1},
            {"scenario", "simulation/ordered_trace"},
            {"workload", workload},
            {"steps", steps},
            {"overlapBroadphaseAlgorithm", overlapScanAlgorithmName(world.cellOverlapScanAlgorithm())},
            {"encoding", "little-endian fixed-width integers and IEEE-754 bit patterns"},
            {"pairs", {{"count", pairCount}, {"digest", cellularity::test_support::stableDigest(pairs)}}},
            {"endpoints", {{"count", contactCount}, {"digest", cellularity::test_support::stableDigest(endpoints)}}},
            {"applyIntents", {{"count", intentCount}, {"digest", cellularity::test_support::stableDigest(intents)}}},
            {"state", {{"initial", initial}, {"final", final}}},
    };
}

PerfJson runThroughputStationaryOverlapJson(SimulationWorld& world,
        const std::string& workload,
        int warmupRepeats,
        int measuredRepeats) {
    if (warmupRepeats < 0 || measuredRepeats < 1) {
        throw std::invalid_argument("Invalid stationary overlap repeat count");
    }
    std::uint64_t checksum = 0;
    std::uint64_t pairVisits = 0;
    auto scan = [&] {
        world.mCellSweepBroadphase.buildConfigured(world.mCellData, false);
        world.mCellSweepBroadphase.forEachOverlappingPairConfiguredInline(
                [&](int first,
                        int second,
                        double,
                        double,
                        double,
                        double,
                        double) {
                    checksum = checksum * 1'099'511'628'211ULL
                            + static_cast<std::uint64_t>(first + 1) * 65'537ULL
                            + static_cast<std::uint64_t>(second + 1);
                    pairVisits++;
                });
    };
    for (int repeat = 0; repeat < warmupRepeats; repeat++) {
        scan();
    }
    pairVisits = 0;
    const auto startedAt = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < measuredRepeats; repeat++) {
        scan();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - startedAt);
    const double totalMs = static_cast<double>(elapsed.count()) / 1'000'000.0;
    return {
            {"schemaVersion", 1},
            {"scenario", "simulation/stationary_overlap"},
            {"workload", workload},
            {"warmupRepeats", warmupRepeats},
            {"measuredRepeats", measuredRepeats},
            {"overlapBroadphaseAlgorithm", overlapScanAlgorithmName(world.cellOverlapScanAlgorithm())},
            {"cells", world.mCellData.mCellCount},
            {"pairVisits", pairVisits},
            {"checksum", checksum},
            {"totalMs", totalMs},
            {"avgRepeatMs", totalMs / measuredRepeats},
            {"repeatsPerSecond", measuredRepeats / (totalMs / 1000.0)},
            {"state", capturePerfWorldState(world)},
    };
}
