#include "PerfScenarios.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "PerfOverlap.h"
#include "PerfSignal.h"
#include "PerfState.h"
#include "app/NativeWorldEditing.h"
#include "core/GeneAccess.h"
#include "core/SimulationRandom.h"
#include "render/NativeSimulationWorldRenderer.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array<const char*, SimulationWorldProfile::STAGE_COUNT> STAGE_NAMES{
        "time_light",
        "random_food_cells",
        "kinematics",
        "split",
        "reset_forces",
        "flagellocytes",
        "remove_dead",
        "update_grids",
        "eat_foods",
        "dead_splashes",
        "overlaps",
        "fertilization",
        "cell_cell_collisions",
        "link_interactions",
        "wall_collisions",
        "smell_interactions",
        "cell_forces",
        "velocities",
        "ciliocytes",
        "metabolism",
        "apply_metabolism",
        "signals",
};

constexpr std::array<const char*, 18> CELL_TYPE_NAMES{
        "phagocyte",
        "flagellocyte",
        "photocyte",
        "devorocyte",
        "lipocyte",
        "keratinocyte",
        "buoyocyte",
        "glueocyte",
        "virocyte",
        "nitrocyte",
        "stereocyte",
        "senseocyte",
        "myocyte",
        "neurocyte",
        "secrocyte",
        "stemocyte",
        "gamete",
        "ciliocyte",
};

double nanosToMs(long long nanos) {
    return static_cast<double>(nanos) / 1'000'000.0;
}

PerfJson makeCoarseAttribution(long long parentNanos, int profileSteps,
        std::initializer_list<std::pair<const char*, long long>> phases) {
    PerfJson phaseJson = PerfJson::object();
    long long measuredNanos = 0;
    for (const auto& [name, nanos] : phases) {
        measuredNanos += nanos;
        phaseJson[name] = {
                {"totalNanos", nanos},
                {"totalMs", nanosToMs(nanos)},
                {"avgMs", nanosToMs(nanos) / profileSteps},
                {"shareOfParentPercent", static_cast<double>(nanos) * 100.0 / std::max<long long>(1, parentNanos)}
        };
    }

    const long long residualNanos = parentNanos - measuredNanos;
    const long long clockAndBoundaryBudgetNanos = std::max<long long>(
            static_cast<long long>(profileSteps) * static_cast<long long>(phases.size() + 1U) * 500LL,
            parentNanos / 10LL);
    return {{"available", true},
            {"timerGranularity", "coarse-region"},
            {"parentTotalNanos", parentNanos},
            {"parentTotalMs", nanosToMs(parentNanos)},
            {"parentAvgMs", nanosToMs(parentNanos) / profileSteps},
            {"measuredChildTotalNanos", measuredNanos},
            {"accountedPercent", static_cast<double>(measuredNanos) * 100.0 / std::max<long long>(1, parentNanos)},
            {"residualNanos", residualNanos},
            {"residualTotalMs", nanosToMs(residualNanos)},
            {"residualAvgMs", nanosToMs(residualNanos) / profileSteps},
            {"residualPercent", static_cast<double>(residualNanos) * 100.0 / std::max<long long>(1, parentNanos)},
            {"acceptanceBudgetNanos", clockAndBoundaryBudgetNanos},
            {"withinAcceptance", std::abs(residualNanos) <= clockAndBoundaryBudgetNanos},
            {"phases", phaseJson}};
}

void warmupSimulation(SimulationWorld& world, int warmupSteps, bool forRendering) {
    for (int step = 0; step < warmupSteps; step++) {
        world.update1(forRendering, 1.0);
        world.update2(forRendering, 1.0);
    }
}

struct TwoCellSwimmerSearchResult {
    bool found = false;
    int flagellocyteCellIndex = -1;
    int phagocyteCellIndex = -1;
    int flagellocyteGeneIndex = -1;
    int phagocyteGeneIndex = -1;
};

TwoCellSwimmerSearchResult findTwoCellSwimmer(const SimulationWorld& world) {
    const CellData& cellData = world.mCellData;
    const CellTopology::EdgeView edges = cellData.mTopology.edgeView();
    for (int edgeIndex = 0; edgeIndex < edges.count; edgeIndex++) {
        const int cellA = edges.cellA[edgeIndex];
        const int cellB = edges.cellB[edgeIndex];
        if (cellA < 0
                || cellB < 0
                || cellA >= cellData.mCellCount
                || cellB >= cellData.mCellCount
                || cellData.mIsDead[cellA]
                || cellData.mIsDead[cellB]) {
            continue;
        }

        int flagellocyteCellIndex = -1;
        int phagocyteCellIndex = -1;
        if (cellData.mActiveCellType[cellA] == CellType::FLAGELLOCYTE
                && cellData.mActiveCellType[cellB] == CellType::PHAGOCYTE) {
            flagellocyteCellIndex = cellA;
            phagocyteCellIndex = cellB;
        } else if (cellData.mActiveCellType[cellB] == CellType::FLAGELLOCYTE
                && cellData.mActiveCellType[cellA] == CellType::PHAGOCYTE) {
            flagellocyteCellIndex = cellB;
            phagocyteCellIndex = cellA;
        } else {
            continue;
        }

        return {
                true,
                flagellocyteCellIndex,
                phagocyteCellIndex,
                cellData.activeGeneIndex(flagellocyteCellIndex).value(),
                cellData.activeGeneIndex(phagocyteCellIndex).value()
        };
    }
    return {};
}

struct TwoCellSwimmerCandidateTracker {
    bool active = false;
    int flagellocyteCellIndex = -1;
    int phagocyteCellIndex = -1;
    int flagellocyteGeneIndex = -1;
    int phagocyteGeneIndex = -1;
    int startStep = -1;
    int stableSteps = 0;
    double startCenterX = 0.0;
    double startCenterY = 0.0;
    double maxTravel = 0.0;
};

void pairCenter(const CellData& cellData, int firstCellIndex, int secondCellIndex, double& centerX, double& centerY) {
    centerX = 0.5 * (cellData.mPositionX[firstCellIndex] + cellData.mPositionX[secondCellIndex]);
    centerY = 0.5 * (cellData.mPositionY[firstCellIndex] + cellData.mPositionY[secondCellIndex]);
}

bool isTrackedCandidateStillPresent(const SimulationWorld& world, const TwoCellSwimmerCandidateTracker& tracker) {
    const CellData& cellData = world.mCellData;
    if (!tracker.active
            || tracker.flagellocyteCellIndex < 0
            || tracker.phagocyteCellIndex < 0
            || tracker.flagellocyteCellIndex >= cellData.mCellCount
            || tracker.phagocyteCellIndex >= cellData.mCellCount
            || cellData.mIsDead[tracker.flagellocyteCellIndex]
            || cellData.mIsDead[tracker.phagocyteCellIndex]
            || cellData.mActiveCellType[tracker.flagellocyteCellIndex] != CellType::FLAGELLOCYTE
            || cellData.mActiveCellType[tracker.phagocyteCellIndex] != CellType::PHAGOCYTE
            || cellData.activeGeneIndex(tracker.flagellocyteCellIndex).value()
                    != tracker.flagellocyteGeneIndex
            || cellData.activeGeneIndex(tracker.phagocyteCellIndex).value()
                    != tracker.phagocyteGeneIndex) {
        return false;
    }
    return cellData.mTopology.isLinked(tracker.flagellocyteCellIndex, tracker.phagocyteCellIndex);
}

void startTrackingCandidate(const SimulationWorld& world,
        const TwoCellSwimmerSearchResult& candidate,
        int step,
        TwoCellSwimmerCandidateTracker& tracker) {
    const CellData& cellData = world.mCellData;
    tracker.active = true;
    tracker.flagellocyteCellIndex = candidate.flagellocyteCellIndex;
    tracker.phagocyteCellIndex = candidate.phagocyteCellIndex;
    tracker.flagellocyteGeneIndex = candidate.flagellocyteGeneIndex;
    tracker.phagocyteGeneIndex = candidate.phagocyteGeneIndex;
    tracker.startStep = step;
    tracker.stableSteps = 1;
    pairCenter(cellData,
            tracker.flagellocyteCellIndex,
            tracker.phagocyteCellIndex,
            tracker.startCenterX,
            tracker.startCenterY);
    tracker.maxTravel = 0.0;
}

void updateTrackedCandidate(const SimulationWorld& world, TwoCellSwimmerCandidateTracker& tracker) {
    const CellData& cellData = world.mCellData;
    tracker.stableSteps++;
    double centerX;
    double centerY;
    pairCenter(cellData, tracker.flagellocyteCellIndex, tracker.phagocyteCellIndex, centerX, centerY);
    const double dx = centerX - tracker.startCenterX;
    const double dy = centerY - tracker.startCenterY;
    tracker.maxTravel = std::max(tracker.maxTravel, std::sqrt(dx * dx + dy * dy));
}

cellularity::GenomeData createDefaultEditorGenome(SimulationRandom& random) {
    cellularity::GenomeData genome(1);
    for (int geneIndex = 0; geneIndex < static_cast<int>(genome.size()); geneIndex++) {
        GeneData& gene = genome[geneIndex];
        cellularity::GeneBuilder builder(gene);
        builder.setSplitMass(static_cast<float>(CellData::MIN_MASS * 3.5));
        float red = static_cast<float>(random.nextUniform());
        float green = static_cast<float>(random.nextUniform());
        float blue = static_cast<float>(random.nextUniform());
        float colorLength = std::sqrt(
                red * red + green * green + blue * blue);
        if (colorLength > 0.0f) {
            red /= colorLength;
            green /= colorLength;
            blue /= colorLength;
        }
        builder.setColor(0, red);
        builder.setColor(1, green);
        builder.setColor(2, blue);
        builder.setSplitAngle(0.0f);
        builder.setSplitRatio(0.5f);
        builder.setChild1Angle(0.0f);
        builder.setChild2Angle(0.0f);
        builder.setMassFlowPriority(1.0f);
        builder.setCellType(CellType::PHAGOCYTE);
        const cellularity::GeneIndex self(
                static_cast<cellularity::GeneIndex::Value>(geneIndex));
        builder.setChild1GeneIndex(self);
        builder.setChild2GeneIndex(self);
        builder.setMakesLink(false);
        builder.setChild1KeepsLinks(true);
        builder.setChild2KeepsLinks(true);
        builder.setStaysAlive(true);
        builder.setInitialCompatibilityFlag(geneIndex == 0);

        for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
            builder.setParametricFormulaType(index, 0);
            builder.setParametricSignalChannel(index, 0);
            builder.setParametricVariableA(index, 0.0f);
            builder.setParametricVariableB(index, 0.0f);
            builder.setParametricVariableC(index, 0.0f);
        }
        for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
            builder.setIntegerValue(index, 0);
        }
        builder.setVirusFromGeneIndex(self);
        builder.setVirusToGeneIndex(self);
        builder.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                Gene::INTEGER_MAX[Gene::MAX_CONNECTION_COUNT_INDEX] - 1);
        builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }
    return genome;
}

struct ComplexityComponentStats {
    int size = 0;
    int typeCount = 0;
    int specializedTypeCount = 0;
    int phagocytes = 0;
    int flagellocytes = 0;
    int myocytes = 0;
    int stereocytes = 0;
    int senseocytes = 0;
    int neurocytes = 0;
    int ciliocytes = 0;
    int motors = 0;
    int sensorsAndControl = 0;
    double score = 0.0;
};

struct ComplexityComponentSnapshot {
    int id = -1;
    ComplexityComponentStats stats;
    std::vector<int> cells;
    uint64_t dominantGenomeSignature = 0;
};

struct PreviousMatureComponentSnapshot {
    uint64_t dominantGenomeSignature = 0;
    std::unordered_set<int> cells;
};

int findComponentRoot(std::vector<int>& parents, int cellIndex) {
    int root = cellIndex;
    while (parents[root] != root) {
        root = parents[root];
    }
    while (parents[cellIndex] != cellIndex) {
        const int parent = parents[cellIndex];
        parents[cellIndex] = root;
        cellIndex = parent;
    }
    return root;
}

void unionComponents(std::vector<int>& parents, std::vector<int>& ranks, int firstCellIndex, int secondCellIndex) {
    int firstRoot = findComponentRoot(parents, firstCellIndex);
    int secondRoot = findComponentRoot(parents, secondCellIndex);
    if (firstRoot == secondRoot) {
        return;
    }
    if (ranks[firstRoot] < ranks[secondRoot]) {
        std::swap(firstRoot, secondRoot);
    }
    parents[secondRoot] = firstRoot;
    if (ranks[firstRoot] == ranks[secondRoot]) {
        ranks[firstRoot]++;
    }
}

int popcountTypes(uint32_t mask) {
    int count = 0;
    while (mask != 0) {
        count += static_cast<int>(mask & 1U);
        mask >>= 1U;
    }
    return count;
}

double complexityScore(const ComplexityComponentStats& stats) {
    return static_cast<double>(std::min(stats.size, 50))
            + 15.0 * stats.typeCount
            + 20.0 * stats.specializedTypeCount
            + 3.0 * stats.motors
            + 5.0 * stats.sensorsAndControl
            + 5.0 * stats.myocytes
            + 5.0 * stats.stereocytes
            + 3.0 * stats.neurocytes;
}

uint64_t phenotypeLineageSignature(const ComplexityComponentStats& stats, uint32_t typeMask) {
    (void)stats;
    return typeMask;
}

std::vector<ComplexityComponentSnapshot> measureComplexityComponents(const SimulationWorld& world) {
    const CellData& cellData = world.mCellData;
    const int cellCount = cellData.mCellCount;
    if (cellCount <= 0) {
        return {};
    }

    std::vector<int> parents(cellCount);
    std::vector<int> ranks(cellCount, 0);
    std::iota(parents.begin(), parents.end(), 0);

    const CellTopology::EdgeView edges = cellData.mTopology.edgeView();
    for (int edgeIndex = 0; edgeIndex < edges.count; edgeIndex++) {
        const int cellA = edges.cellA[edgeIndex];
        const int cellB = edges.cellB[edgeIndex];
        if (cellA < 0
                || cellB < 0
                || cellA >= cellCount
                || cellB >= cellCount
                || cellData.mIsDead[cellA]
                || cellData.mIsDead[cellB]) {
            continue;
        }
        unionComponents(parents, ranks, cellA, cellB);
    }

    std::vector<ComplexityComponentSnapshot> components(cellCount);
    std::vector<uint32_t> typeMasks(cellCount, 0);
    for (int root = 0; root < cellCount; root++) {
        components[root].id = root;
    }

    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        if (cellData.mIsDead[cellIndex]) {
            continue;
        }
        const int root = findComponentRoot(parents, cellIndex);
        ComplexityComponentSnapshot& component = components[root];
        ComplexityComponentStats& stats = component.stats;
        component.cells.push_back(cellIndex);
        stats.size++;
        const CellType cellType = cellData.mActiveCellType[cellIndex];
        const int typeOrdinal = CellTypes::ordinal(cellType);
        typeMasks[root] |= 1U << typeOrdinal;
        switch (cellType) {
            case CellType::PHAGOCYTE:
                stats.phagocytes++;
                break;
            case CellType::FLAGELLOCYTE:
                stats.flagellocytes++;
                stats.motors++;
                break;
            case CellType::MYOCYTE:
                stats.myocytes++;
                stats.motors++;
                break;
            case CellType::CILIOCYTE:
                stats.ciliocytes++;
                stats.motors++;
                break;
            case CellType::STEREOCYTE:
                stats.stereocytes++;
                stats.sensorsAndControl++;
                break;
            case CellType::SENSEOCYTE:
                stats.senseocytes++;
                stats.sensorsAndControl++;
                break;
            case CellType::NEUROCYTE:
                stats.neurocytes++;
                stats.sensorsAndControl++;
                break;
            default:
                break;
        }
    }

    std::vector<ComplexityComponentSnapshot> result;
    const uint32_t simpleSwimmerMask = (1U << CellTypes::ordinal(CellType::PHAGOCYTE))
            | (1U << CellTypes::ordinal(CellType::FLAGELLOCYTE));
    for (int root = 0; root < cellCount; root++) {
        ComplexityComponentSnapshot& component = components[root];
        if (component.stats.size == 0) {
            continue;
        }
        component.stats.typeCount = popcountTypes(typeMasks[root]);
        component.stats.specializedTypeCount = popcountTypes(typeMasks[root] & ~simpleSwimmerMask);
        component.stats.score = complexityScore(component.stats);
        component.dominantGenomeSignature = phenotypeLineageSignature(component.stats, typeMasks[root]);
        result.push_back(std::move(component));
    }
    return result;
}

ComplexityComponentStats measureBestComplexityComponent(const SimulationWorld& world) {
    ComplexityComponentStats best;
    double bestScore = 0.0;
    for (const ComplexityComponentSnapshot& component : measureComplexityComponents(world)) {
        if (component.stats.score > bestScore) {
            bestScore = component.stats.score;
            best = component.stats;
        }
    }
    return best;
}

bool isMatureOrganismComponent(const ComplexityComponentStats& stats, int minComponentSize) {
    return stats.size >= minComponentSize
            && stats.typeCount >= 3
            && stats.specializedTypeCount >= 1;
}

NativeSimulationWorldRenderer::ViewBounds renderProfileView(const SimulationWorld& world, double viewRadius) {
    double radius = viewRadius > 0.0 ? viewRadius : world.mEnvironment.mRadius;
    return {
            static_cast<float>(-radius),
            static_cast<float>(radius),
            static_cast<float>(-radius),
            static_cast<float>(radius)
    };
}

void addProfile(NativeSimulationWorldRenderer::CopyToDrawProfile& total,
        const NativeSimulationWorldRenderer::CopyToDrawProfile& sample) {
    total.sourceCaptureNanos += sample.sourceCaptureNanos;
    total.contactChordCollectionNanos += sample.contactChordCollectionNanos;
    total.cellCaptureNanos += sample.cellCaptureNanos;
    total.linkCaptureNanos += sample.linkCaptureNanos;
    total.foodCaptureNanos += sample.foodCaptureNanos;
    total.deadSplashCaptureNanos += sample.deadSplashCaptureNanos;
    total.markerCaptureNanos += sample.markerCaptureNanos;
    total.freshPhysicsContactCaptures += sample.freshPhysicsContactCaptures;
    total.staleGeometryContactFallbacks += sample.staleGeometryContactFallbacks;
    total.materializedContactChords += sample.materializedContactChords;
    total.foodGridCaptureCalls += sample.foodGridCaptureCalls;
    total.foodLinearCaptureCalls += sample.foodLinearCaptureCalls;
    total.foodLinearMissingIndexFallbacks += sample.foodLinearMissingIndexFallbacks;
    total.foodLinearCoverageFallbacks += sample.foodLinearCoverageFallbacks;
    total.foodCandidateVisits += sample.foodCandidateVisits;
    total.foodIndexGenerationHits += sample.foodIndexGenerationHits;
    total.foodIndexGenerationMisses += sample.foodIndexGenerationMisses;
    total.foodStaleIndexFallbacks += sample.foodStaleIndexFallbacks;
    total.foodStaleIndexMappingVisits += sample.foodStaleIndexMappingVisits;
    total.cullingNanos += sample.cullingNanos;
    total.clearNanos += sample.clearNanos;
    total.cellsNanos += sample.cellsNanos;
    total.linksNanos += sample.linksNanos;
    total.contactChordResolutionNanos += sample.contactChordResolutionNanos;
    total.cellPassContactChordResolutionNanos += sample.cellPassContactChordResolutionNanos;
    total.linkPassContactChordResolutionNanos += sample.linkPassContactChordResolutionNanos;
    total.contactChordNormalizationNanos += sample.contactChordNormalizationNanos;
    total.cellPassContactChordNormalizationNanos += sample.cellPassContactChordNormalizationNanos;
    total.linkPassContactChordNormalizationNanos += sample.linkPassContactChordNormalizationNanos;
    total.contactChordResolveRequests += sample.contactChordResolveRequests;
    total.contactChordDirectSourceViews += sample.contactChordDirectSourceViews;
    total.contactChordCacheHits += sample.contactChordCacheHits;
    total.contactChordCacheMisses += sample.contactChordCacheMisses;
    total.uniqueNormalizedCells += sample.uniqueNormalizedCells;
    total.normalizedContactChordCount += sample.normalizedContactChordCount;
    total.linkOnlyCacheMisses += sample.linkOnlyCacheMisses;
    total.normalizerFirstPassNanos += sample.normalizerFirstPassNanos;
    total.normalizerSecondPassNanos += sample.normalizerSecondPassNanos;
    total.normalizerSortNanos += sample.normalizerSortNanos;
    total.normalizerCalls += sample.normalizerCalls;
    total.cellPassNormalizerCalls += sample.cellPassNormalizerCalls;
    total.linkPassNormalizerCalls += sample.linkPassNormalizerCalls;
    total.normalizerSkippedCalls += sample.normalizerSkippedCalls;
    total.normalizerContactChordCountSum += sample.normalizerContactChordCountSum;
    total.cellPassNormalizerContactChordCountSum += sample.cellPassNormalizerContactChordCountSum;
    total.linkPassNormalizerContactChordCountSum += sample.linkPassNormalizerContactChordCountSum;
    total.normalizerFirstPassPairs += sample.normalizerFirstPassPairs;
    total.normalizerFirstPassDiscardedSkips += sample.normalizerFirstPassDiscardedSkips;
    total.normalizerFirstPassIntersections += sample.normalizerFirstPassIntersections;
    total.normalizerFirstPassJoinMarks += sample.normalizerFirstPassJoinMarks;
    total.normalizerFirstPassDiscardMarks += sample.normalizerFirstPassDiscardMarks;
    total.normalizerSecondPassPairs += sample.normalizerSecondPassPairs;
    total.normalizerSecondPassSelfSkips += sample.normalizerSecondPassSelfSkips;
    total.normalizerSecondPassEndpointSkips += sample.normalizerSecondPassEndpointSkips;
    total.normalizerSecondPassDiscardMarks += sample.normalizerSecondPassDiscardMarks;
    total.normalizerSortComparisons += sample.normalizerSortComparisons;
    total.normalizerSortMoves += sample.normalizerSortMoves;
    total.outlinesNanos += sample.outlinesNanos;
    total.foodsNanos += sample.foodsNanos;
    total.deadCellsNanos += sample.deadCellsNanos;
    total.totalNanos += sample.totalNanos;
    total.sourceActiveStorageBytes += sample.sourceActiveStorageBytes;
    total.sourceRetainedStorageBytes += sample.sourceRetainedStorageBytes;
    total.sourceCellRecordCount += sample.sourceCellRecordCount;
    total.sourceActiveContactChordCount += sample.sourceActiveContactChordCount;
    total.sourceCandidateLinkCount += sample.sourceCandidateLinkCount;
    total.sourceFoodRecordCount += sample.sourceFoodRecordCount;
    total.sourceDeadSplashRecordCount += sample.sourceDeadSplashRecordCount;
    total.visibleCellCount += sample.visibleCellCount;
    total.cellDrawVertexCount += sample.cellDrawVertexCount;
    total.cellDrawIndexCount += sample.cellDrawIndexCount;
    total.cellCenterFloatCount += sample.cellCenterFloatCount;
    total.foodInstanceCount += sample.foodInstanceCount;
    total.deadCellInstanceCount += sample.deadCellInstanceCount;
    total.drawLinksCount += sample.drawLinksCount;
}

} // namespace

PerfJson runFullUpdateProfileJson(SimulationWorld& world,
        const std::string& inputPath,
        int substrateFormatVersion,
        int warmupSteps,
        int profileSteps) {
    const PerfJson initialState = capturePerfWorldState(world);
    warmupSimulation(world, warmupSteps, false);
    const PerfJson measuredStartState = capturePerfWorldState(world);

    auto startedAt = Clock::now();
    for (int step = 0; step < profileSteps; step++) {
        world.update1(false, 1.0);
        world.update2(false, 1.0);
    }

    auto totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt);
    double totalMs = nanosToMs(totalNanos.count());
    const PerfJson finalState = capturePerfWorldState(world);

    return {
            {"schemaVersion", 2},
            {"scenario", "simulation/full_update"},
            {"inputPath", inputPath},
            {"substrateFormatVersion", substrateFormatVersion},
            {"overlapBroadphaseAlgorithm", overlapScanAlgorithmName(world.cellOverlapScanAlgorithm())},
            {"signalUpdateAlgorithm", signalUpdateAlgorithmName(world.mCellData.signalUpdateAlgorithm())},
            {"maxCells", world.mEnvironment.maxCellCount},
            {"maxFoods", world.mEnvironment.maxFoodCount},
            {"environmentRadius", world.mEnvironment.mRadius},
            {"warmupSteps", warmupSteps},
            {"profileSteps", profileSteps},
            {"cells", world.mCellData.mCellCount},
            {"food", world.mFoodData.mFoodCount},
            {"time", world.mTime},
            {"totalMs", totalMs},
            {"avgStepMs", totalMs / profileSteps},
            {"stepsPerSecond", profileSteps / (totalMs / 1000.0)},
            {"state", {{"initial", initialState}, {"measuredStart", measuredStartState}, {"final", finalState}}}
    };
}

PerfJson runSignalKernelBenchmarkJson(SimulationWorld& world,
        const std::string& inputPath,
        int substrateFormatVersion,
        int warmupSteps,
        int measuredSteps) {
    if (warmupSteps < 0 || measuredSteps < 1) {
        throw std::invalid_argument("Invalid signal kernel benchmark step count");
    }

    constexpr double SIGNAL_DELTA_TIME = SimulationWorld::DELTA_TIME;
    const PerfJson initialState = capturePerfWorldState(world);
    for (int step = 0; step < warmupSteps; ++step) {
        world.mCellData.updateSignals(SIGNAL_DELTA_TIME, nullptr);
    }
    const PerfJson measuredStartState = capturePerfWorldState(world);

    const auto startedAt = Clock::now();
    for (int step = 0; step < measuredSteps; ++step) {
        world.mCellData.updateSignals(SIGNAL_DELTA_TIME, nullptr);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - startedAt);
    const double totalMs = nanosToMs(elapsed.count());
    const PerfJson finalState = capturePerfWorldState(world);

    return {
            {"schemaVersion", 1},
            {"scenario", "simulation/signal_kernel"},
            {"inputPath", inputPath},
            {"substrateFormatVersion", substrateFormatVersion},
            {"signalUpdateAlgorithm",
                    signalUpdateAlgorithmName(world.mCellData.signalUpdateAlgorithm())},
            {"warmupSteps", warmupSteps},
            {"measuredSteps", measuredSteps},
            {"deltaTime", SIGNAL_DELTA_TIME},
            {"cells", world.mCellData.mCellCount},
            {"normalEdges", world.mCellData.mTopology.edgeCount()},
            {"totalMs", totalMs},
            {"avgUpdateMs", totalMs / measuredSteps},
            {"updatesPerSecond", measuredSteps / (totalMs / 1000.0)},
            {"state", {{"initial", initialState}, {"measuredStart", measuredStartState}, {"final", finalState}}}
    };
}

PerfJson runStageBreakdownProfileJson(SimulationWorld& world,
        const std::string& inputPath,
        int substrateFormatVersion,
        int warmupSteps,
        int profileSteps) {
    const PerfJson initialState = capturePerfWorldState(world);
    warmupSimulation(world, warmupSteps, false);
    const PerfJson measuredStartState = capturePerfWorldState(world);

    SimulationWorldProfile profile;
    profile.clear();
    world.setProfile(&profile);

    auto startedAt = Clock::now();
    for (int step = 0; step < profileSteps; step++) {
        world.update1(false, 1.0);
        world.update2(false, 1.0);
    }
    world.setProfile(nullptr);

    auto totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt);
    double totalMs = nanosToMs(totalNanos.count());
    const PerfJson finalState = capturePerfWorldState(world);
    PerfJson stages = PerfJson::object();
    for (size_t i = 0; i < profile.stageNanos.size(); i++) {
        double stageMs = nanosToMs(profile.stageNanos[i]);
        stages[STAGE_NAMES[i]] = {
                {"totalMs", stageMs},
                {"avgMs", stageMs / profileSteps},
                {"share", profile.stageNanos[i] * 100.0 / std::max<long long>(1, totalNanos.count())}
        };
    }
    double linkInteractionsMs = nanosToMs(profile.stageNanos[SimulationWorldProfile::LINK_INTERACTIONS]);
    double linkAngleCacheMs = nanosToMs(profile.linkAngleCacheNanos);
    double linkTopologyCountScanMs = nanosToMs(profile.linkTopologyCountScanNanos);
    double linkBoundaryLoopMs = nanosToMs(profile.linkBoundaryLoopNanos);
    double linkNormalLoopMs = nanosToMs(profile.linkNormalLoopNanos);
    PerfJson linkCounters = {
            {"angleCacheTotalMs", linkAngleCacheMs},
            {"angleCacheAvgMs", linkAngleCacheMs / profileSteps},
            {"angleCacheShareOfLinkInteractions",
                    profile.linkAngleCacheNanos * 100.0 / std::max<long long>(1, profile.stageNanos[SimulationWorldProfile::LINK_INTERACTIONS])},
            {"topologyCountScanTotalMs", linkTopologyCountScanMs},
            {"topologyCountScanAvgMs", linkTopologyCountScanMs / profileSteps},
            {"topologyCountScanShareOfLinkInteractions",
                    profile.linkTopologyCountScanNanos * 100.0 / std::max<long long>(1, profile.stageNanos[SimulationWorldProfile::LINK_INTERACTIONS])},
            {"boundaryLoopTotalMs", linkBoundaryLoopMs},
            {"boundaryLoopAvgMs", linkBoundaryLoopMs / profileSteps},
            {"boundaryLoopShareOfLinkInteractions",
                    profile.linkBoundaryLoopNanos * 100.0 / std::max<long long>(1, profile.stageNanos[SimulationWorldProfile::LINK_INTERACTIONS])},
            {"normalLoopTotalMs", linkNormalLoopMs},
            {"normalLoopAvgMs", linkNormalLoopMs / profileSteps},
            {"normalLoopShareOfLinkInteractions",
                    profile.linkNormalLoopNanos * 100.0 / std::max<long long>(1, profile.stageNanos[SimulationWorldProfile::LINK_INTERACTIONS])},
            {"removalTiming",
                    {{"available", false}, {"reason", "removal-is-fused-into-link-loops-to-avoid-inner-link-clocks"}}},
            {"targetCells", profile.linkTargetCells},
            {"avgTargetCellsPerStep", static_cast<double>(profile.linkTargetCells) / profileSteps},
            {"cellsWithLinks", profile.linkCellsWithLinks},
            {"avgCellsWithLinksPerStep", static_cast<double>(profile.linkCellsWithLinks) / profileSteps},
            {"totalLinks", profile.linkTotalLinks},
            {"avgLinksPerStep", static_cast<double>(profile.linkTotalLinks) / profileSteps},
            {"topologyLinkCountQueries", profile.linkTopologyLinkCountQueries},
            {"avgTopologyLinkCountQueriesPerStep",
                    static_cast<double>(profile.linkTopologyLinkCountQueries) / profileSteps},
            {"avgLinksPerTargetCell",
                    static_cast<double>(profile.linkTotalLinks) / std::max<long long>(1, profile.linkTargetCells)},
            {"avgLinksPerLinkedCell",
                    static_cast<double>(profile.linkTotalLinks) / std::max<long long>(1, profile.linkCellsWithLinks)},
            {"boundaryLinks", profile.linkBoundaryLinks},
            {"avgBoundaryLinksPerStep", static_cast<double>(profile.linkBoundaryLinks) / profileSteps},
            {"normalLinks", profile.linkNormalLinks},
            {"avgNormalLinksPerStep", static_cast<double>(profile.linkNormalLinks) / profileSteps},
            {"renderOffsetWrites", profile.linkRenderOffsetWrites},
            {"avgRenderOffsetWritesPerStep", static_cast<double>(profile.linkRenderOffsetWrites) / profileSteps},
            {"targetMyocyteBranches", profile.linkTargetMyocyteBranches},
            {"avgTargetMyocyteBranchesPerStep", static_cast<double>(profile.linkTargetMyocyteBranches) / profileSteps},
            {"linkedMyocyteBranches", profile.linkLinkedMyocyteBranches},
            {"avgLinkedMyocyteBranchesPerStep", static_cast<double>(profile.linkLinkedMyocyteBranches) / profileSteps},
            {"removedBoundaryLinks", profile.linkRemovedBoundaryLinks},
            {"removedNormalLinks", profile.linkRemovedNormalLinks},
            {"linkInteractionsTotalMs", linkInteractionsMs},
            {"linkInteractionsAvgMs", linkInteractionsMs / profileSteps}
    };
    PerfJson spatialIndexCounters = {{"cellTouchedBucketsCleared", profile.gridCellTouchedBucketsCleared},
            {"smellTouchedBucketsCleared", profile.gridSmellTouchedBucketsCleared},
            {"renderOverflowHeadsCleared", profile.gridRenderOverflowHeadsCleared},
            {"renderOccupancyWordsCleared", profile.gridRenderOccupancyWordsCleared},
            {"cellPositionConversions", profile.gridCellPositionConversions},
            {"cellIndexAttempts", profile.gridCellIndexAttempts},
            {"cellIndexAdmissions", profile.gridCellIndexAdmissions},
            {"cellIndexCapacitySkips", profile.gridCellIndexCapacitySkips},
            {"cellRenderOccupancyMarks", profile.gridCellRenderOccupancyMarks},
            {"cellRenderOverflowAdmissions", profile.gridCellRenderOverflowAdmissions},
            {"consumerCellsVisited", profile.gridConsumerCellsVisited},
            {"anySmellConsumerMatches", profile.gridAnySmellConsumerMatches},
            {"smellGridConsumerMatches", profile.gridSmellGridConsumerMatches},
            {"foodSmellConsumerMatches", profile.gridFoodSmellConsumerMatches},
            {"foodPhysicsConsumerMatches", profile.gridFoodPhysicsConsumerMatches},
            {"foodSmellMaskSlotsCleared", profile.gridFoodSmellMaskSlotsCleared},
            {"foodSmellMaskCellsVisited", profile.gridFoodSmellMaskCellsVisited},
            {"foodSmellMaskConsumerCells", profile.gridFoodSmellMaskConsumerCells},
            {"foodSmellMaskWrites", profile.gridFoodSmellMaskWrites},
            {"smellCellAttempts", profile.gridSmellCellAttempts},
            {"smellCellAdmissions", profile.gridSmellCellAdmissions},
            {"smellCellOutOfBoundsSkips", profile.gridSmellCellOutOfBoundsSkips},
            {"smellCellCapacitySkips", profile.gridSmellCellCapacitySkips},
            {"smellSecretionAdmissions", profile.gridSmellSecretionAdmissions},
            {"foodCollisionRefreshCalls", profile.gridFoodCollisionRefreshCalls},
            {"foodCollisionRefreshVisits", profile.gridFoodCollisionRefreshVisits},
            {"foodMappingVisits", profile.foodIndexMappingVisits},
            {"foodMappingCacheHits", profile.foodIndexMappingCacheHits},
            {"foodMappingCacheMisses", profile.foodIndexMappingCacheMisses},
            {"foodMappingWrites", profile.foodIndexMappingWrites},
            {"foodRenderIndexBuildCalls", profile.foodRenderIndexBuildCalls},
            {"foodRenderHeadSlotsCleared", profile.foodRenderHeadSlotsCleared},
            {"foodRenderMappingVisits", profile.foodRenderMappingVisits},
            {"foodRenderMappingCacheHits", profile.foodRenderMappingCacheHits},
            {"foodRenderMappingCacheMisses", profile.foodRenderMappingCacheMisses},
            {"foodRenderMappingWrites", profile.foodRenderMappingWrites},
            {"foodRenderInsertions", profile.foodRenderInsertions},
            {"foodRenderOutOfBoundsSkips", profile.foodRenderOutOfBoundsSkips},
            {"foodSpatialGenerationHits", profile.foodSpatialGenerationHits},
            {"foodSpatialGenerationMisses", profile.foodSpatialGenerationMisses},
            {"foodSpatialCaptureEnsureCalls", profile.foodSpatialCaptureEnsureCalls}};
    PerfJson eatFoodCounters = {
            {"foodVisits", profile.eatFoodVisits},
            {"avgFoodVisitsPerStep", static_cast<double>(profile.eatFoodVisits) / profileSteps},
            {"decayRemovals", profile.eatFoodDecayRemovals},
            {"avgDecayRemovalsPerStep", static_cast<double>(profile.eatFoodDecayRemovals) / profileSteps},
            {"gridIndexCacheHits", profile.eatFoodGridIndexCacheHits},
            {"avgGridIndexCacheHitsPerStep", static_cast<double>(profile.eatFoodGridIndexCacheHits) / profileSteps},
            {"gridIndexCacheMisses", profile.eatFoodGridIndexCacheMisses},
            {"avgGridIndexCacheMissesPerStep", static_cast<double>(profile.eatFoodGridIndexCacheMisses) / profileSteps},
            {"neighborGridVisits", profile.eatFoodNeighborGridVisits},
            {"avgNeighborGridVisitsPerStep", static_cast<double>(profile.eatFoodNeighborGridVisits) / profileSteps},
            {"neighborGridEntries", profile.eatFoodNeighborGridEntries},
            {"avgNeighborGridEntriesPerStep", static_cast<double>(profile.eatFoodNeighborGridEntries) / profileSteps},
            {"candidateChecks", profile.eatFoodCandidateChecks},
            {"avgCandidateChecksPerStep", static_cast<double>(profile.eatFoodCandidateChecks) / profileSteps},
            {"contactChecks", profile.eatFoodContactChecks},
            {"avgContactChecksPerStep", static_cast<double>(profile.eatFoodContactChecks) / profileSteps},
            {"mobileInnerPushes", profile.eatFoodMobileInnerPushes},
            {"avgMobileInnerPushesPerStep", static_cast<double>(profile.eatFoodMobileInnerPushes) / profileSteps},
            {"ciliocyteContacts", profile.eatFoodCiliocyteContacts},
            {"avgCiliocyteContactsPerStep", static_cast<double>(profile.eatFoodCiliocyteContacts) / profileSteps},
            {"secrocyteContacts", profile.eatFoodSecrocyteContacts},
            {"avgSecrocyteContactsPerStep", static_cast<double>(profile.eatFoodSecrocyteContacts) / profileSteps},
            {"phagocyteContacts", profile.eatFoodPhagocyteContacts},
            {"avgPhagocyteContactsPerStep", static_cast<double>(profile.eatFoodPhagocyteContacts) / profileSteps},
            {"phagocyteEatEvents", profile.eatFoodPhagocyteEatEvents},
            {"avgPhagocyteEatEventsPerStep", static_cast<double>(profile.eatFoodPhagocyteEatEvents) / profileSteps},
            {"consumedRemovals", profile.eatFoodConsumedRemovals},
            {"avgConsumedRemovalsPerStep", static_cast<double>(profile.eatFoodConsumedRemovals) / profileSteps},
            {"movedFoods", profile.eatFoodMovedFoods},
            {"avgMovedFoodsPerStep", static_cast<double>(profile.eatFoodMovedFoods) / profileSteps},
            {"boundaryRemovals", profile.eatFoodBoundaryRemovals},
            {"avgBoundaryRemovalsPerStep", static_cast<double>(profile.eatFoodBoundaryRemovals) / profileSteps},
            {"boundaryReflections", profile.eatFoodBoundaryReflections},
            {"avgBoundaryReflectionsPerStep", static_cast<double>(profile.eatFoodBoundaryReflections) / profileSteps},
            {"smellGridAttempts", profile.eatFoodSmellGridAttempts},
            {"avgSmellGridAttemptsPerStep", static_cast<double>(profile.eatFoodSmellGridAttempts) / profileSteps},
            {"smellGridMaskSkips", profile.eatFoodSmellGridMaskSkips},
            {"avgSmellGridMaskSkipsPerStep", static_cast<double>(profile.eatFoodSmellGridMaskSkips) / profileSteps},
            {"smellGridCapacitySkips", profile.eatFoodSmellGridCapacitySkips},
            {"avgSmellGridCapacitySkipsPerStep",
                    static_cast<double>(profile.eatFoodSmellGridCapacitySkips) / profileSteps},
            {"smellGridAdds", profile.eatFoodSmellGridAdds},
            {"avgSmellGridAddsPerStep", static_cast<double>(profile.eatFoodSmellGridAdds) / profileSteps},
            {"decayTotalMs", nanosToMs(profile.eatFoodDecayNanos)},
            {"preQueryIndexBuildTotalMs", nanosToMs(profile.eatFoodPreQueryIndexBuildNanos)},
            {"smellIndexBuildTotalMs", nanosToMs(profile.eatFoodSmellIndexBuildNanos)},
            {"queryAndUpdateTotalMs", nanosToMs(profile.eatFoodQueryAndUpdateNanos)},
            {"removalTotalMs", nanosToMs(profile.eatFoodRemovalNanos)},
            {"motionTotalMs", nanosToMs(profile.eatFoodMotionNanos)},
            {"postUpdateIndexBuildTotalMs", nanosToMs(profile.eatFoodPostUpdateIndexBuildNanos)}
    };
    PerfJson overlapCounters = {
            {"setupTotalMs", nanosToMs(profile.overlapSetupNanos)},
            {"setupAvgMs", nanosToMs(profile.overlapSetupNanos) / profileSteps},
            {"broadphaseBuildTotalMs", nanosToMs(profile.overlapBroadphaseBuildNanos)},
            {"broadphaseBuildAvgMs", nanosToMs(profile.overlapBroadphaseBuildNanos) / profileSteps},
            {"scanResolveAdmissionTotalMs", nanosToMs(profile.overlapBroadphaseScanNanos)},
            {"scanResolveAdmissionAvgMs", nanosToMs(profile.overlapBroadphaseScanNanos) / profileSteps},
            {"scanResolveAdmissionIsFused", true},
            {"pairEffectsTotalMs", nanosToMs(profile.overlapPairEffectsNanos)},
            {"pairEffectsAvgMs", nanosToMs(profile.overlapPairEffectsNanos) / profileSteps},
            {"deathCleanupTotalMs", nanosToMs(profile.overlapDeathCleanupNanos)},
            {"deathCleanupAvgMs", nanosToMs(profile.overlapDeathCleanupNanos) / profileSteps},
            {"avgOverlapPairBufferWritesPerStep", static_cast<double>(profile.overlapPairBufferWrites) / profileSteps},
            {"avgSemanticPairsPerStep", static_cast<double>(profile.overlapSemanticPairs) / profileSteps},
            {"avgEndpointAdmissionAttemptsPerStep",
                    static_cast<double>(profile.overlapEndpointAdmissionAttempts) / profileSteps},
            {"avgDirectedAdmissionsPerStep", static_cast<double>(profile.collisionWrites) / profileSteps},
            {"avgEndpointCapacitySkipsPerStep",
                    static_cast<double>(profile.collisionWriteCapacitySkips) / profileSteps},
            {"rawBudgetOverflows", profile.overlapRawBudgetOverflows},
            {"avgOverlapCandidatePairsPerStep", static_cast<double>(profile.overlapCandidatePairs) / profileSteps},
            {"avgOverlapSameRowCandidatePairsPerStep",
                    static_cast<double>(profile.overlapSameRowCandidatePairs) / profileSteps},
            {"avgOverlapNextRowCandidatePairsPerStep",
                    static_cast<double>(profile.overlapNextRowCandidatePairs) / profileSteps},
            {"avgOverlapSameRowCandidateRangesPerStep",
                    static_cast<double>(profile.overlapSameRowCandidateRanges) / profileSteps},
            {"avgOverlapNextRowCandidateRangesPerStep",
                    static_cast<double>(profile.overlapNextRowCandidateRanges) / profileSteps},
            {"avgOverlapExactPairsPerStep", static_cast<double>(profile.overlapExactPairs) / profileSteps},
            {"avgOverlapScalarChecksPerStep", static_cast<double>(profile.overlapScalarChecks) / profileSteps},
            {"avgOverlapSimdChecksPerStep", static_cast<double>(profile.overlapSimdChecks) / profileSteps},
            {"avgOverlapRowsReusedPerStep", static_cast<double>(profile.overlapRowsReused) / profileSteps},
            {"avgOverlapRowsRelayoutsPerStep", static_cast<double>(profile.overlapRowsRelayouts) / profileSteps},
            {"avgOverlapRowChangedCellsPerStep", static_cast<double>(profile.overlapRowChangedCells) / profileSteps},
            {"avgOverlapLayoutRebuildsPerStep", static_cast<double>(profile.overlapLayoutRebuilds) / profileSteps},
            {"avgOverlapInsertionSortRowsPerStep",
                    static_cast<double>(profile.overlapInsertionSortRows) / profileSteps},
            {"avgOverlapFullSortRowsPerStep", static_cast<double>(profile.overlapFullSortRows) / profileSteps},
            {"avgOverlapInsertionMovesPerStep", static_cast<double>(profile.overlapInsertionMoves) / profileSteps}
    };
    double cellForcesMs = nanosToMs(profile.stageNanos[SimulationWorldProfile::CELL_FORCES]);
    PerfJson cellForcesCounters = {
            {"visitedCells", profile.cellForcesVisitedCells},
            {"avgVisitedCellsPerStep", static_cast<double>(profile.cellForcesVisitedCells) / profileSteps},
            {"liveCells", profile.cellForcesLiveCells},
            {"avgLiveCellsPerStep", static_cast<double>(profile.cellForcesLiveCells) / profileSteps},
            {"deadSkips", profile.cellForcesDeadSkips},
            {"avgDeadSkipsPerStep", static_cast<double>(profile.cellForcesDeadSkips) / profileSteps},
            {"buoyocytes", profile.cellForcesBuoyocytes},
            {"avgBuoyocytesPerStep", static_cast<double>(profile.cellForcesBuoyocytes) / profileSteps},
            {"previewSkips", profile.cellForcesPreviewSkips},
            {"avgPreviewSkipsPerStep", static_cast<double>(profile.cellForcesPreviewSkips) / profileSteps},
            {"cellForcesTotalMs", cellForcesMs},
            {"cellForcesAvgMs", cellForcesMs / profileSteps}
    };
    double cellCellCollisionsMs = nanosToMs(profile.stageNanos[SimulationWorldProfile::CELL_CELL_COLLISIONS]);
    double collisionIntentCollectMs = nanosToMs(profile.collisionIntentCollectNanos);
    double collisionIntentApplyMs = nanosToMs(profile.collisionIntentApplyNanos);
    PerfJson typedActorPresence = PerfJson::object();
    for (size_t typeIndex = 0; typeIndex < CELL_TYPE_NAMES.size(); typeIndex++) {
        const long long collected = profile.collisionTypedIntentCollectedByActorType[typeIndex];
        const long long committed = profile.collisionTypedIntentCommittedByActorType[typeIndex];
        typedActorPresence[CELL_TYPE_NAMES[typeIndex]] = {
                {"present", collected != 0 || committed != 0}, {"collected", collected}, {"committed", committed}
        };
    }
    PerfJson collisionIntentCounters = {
            {"intentsCollected", profile.collisionIntentCollected},
            {"avgIntentsCollectedPerStep", static_cast<double>(profile.collisionIntentCollected) / profileSteps},
            {"intentsApplied", profile.collisionIntentApplied},
            {"avgIntentsAppliedPerStep", static_cast<double>(profile.collisionIntentApplied) / profileSteps},
            {"geometryEvaluations", profile.collisionGeometryEvaluations},
            {"avgGeometryEvaluationsPerStep", static_cast<double>(profile.collisionGeometryEvaluations) / profileSteps},
            {"ordinaryForceApplications", profile.collisionOrdinaryForceApplications},
            {"avgOrdinaryForceApplicationsPerStep",
                    static_cast<double>(profile.collisionOrdinaryForceApplications) / profileSteps},
            {"typedIntentsCollected", profile.collisionTypedIntentCollected},
            {"typedIntentsCommitted", profile.collisionTypedIntentCommitted},
            {"typedActorPresence", typedActorPresence},
            {"biologicalIntentsDispatched", profile.collisionBiologicalIntentDispatched},
            {"avgBiologicalIntentsDispatchedPerStep",
                    static_cast<double>(profile.collisionBiologicalIntentDispatched) / profileSteps},
            {"ciliocyteIntentsDispatched", profile.collisionCiliocyteIntentDispatched},
            {"avgCiliocyteIntentsDispatchedPerStep",
                    static_cast<double>(profile.collisionCiliocyteIntentDispatched) / profileSteps},
            {"collectTotalMs", collisionIntentCollectMs},
            {"collectAvgMs", collisionIntentCollectMs / profileSteps},
            {"collectShareOfCellCellCollisions",
                    profile.collisionIntentCollectNanos * 100.0 / std::max<long long>(1, profile.stageNanos[SimulationWorldProfile::CELL_CELL_COLLISIONS])},
            {"applyTotalMs", collisionIntentApplyMs},
            {"applyAvgMs", collisionIntentApplyMs / profileSteps},
            {"applyPhaseIsOrderedForceAndTypedCommit", true},
            {"applyShareOfCellCellCollisions",
                    profile.collisionIntentApplyNanos * 100.0 / std::max<long long>(1, profile.stageNanos[SimulationWorldProfile::CELL_CELL_COLLISIONS])},
            {"cellCellCollisionsTotalMs", cellCellCollisionsMs},
            {"cellCellCollisionsAvgMs", cellCellCollisionsMs / profileSteps}
    };
    PerfJson metabolismTypeVisits = PerfJson::object();
    for (size_t typeIndex = 0; typeIndex < CELL_TYPE_NAMES.size(); typeIndex++) {
        metabolismTypeVisits[CELL_TYPE_NAMES[typeIndex]] = {
                {"visits", profile.metabolismTypeVisits[typeIndex]},
                {"avgVisitsPerStep", static_cast<double>(profile.metabolismTypeVisits[typeIndex]) / profileSteps}
        };
    }
    double metabolismMs = nanosToMs(profile.stageNanos[SimulationWorldProfile::METABOLISM]);
    PerfJson metabolismCounters = {
            {"visitedCells", profile.metabolismVisitedCells},
            {"avgVisitedCellsPerStep", static_cast<double>(profile.metabolismVisitedCells) / profileSteps},
            {"typeVisits", metabolismTypeVisits},
            {"nitrateUptakeCells", profile.metabolismNitrateUptakeCells},
            {"avgNitrateUptakeCellsPerStep", static_cast<double>(profile.metabolismNitrateUptakeCells) / profileSteps},
            {"previewGrowthCells", profile.metabolismPreviewGrowthCells},
            {"flagellocytes", profile.metabolismFlagellocytes},
            {"stemocytes", profile.metabolismStemocytes},
            {"stemSwitches", profile.metabolismStemSwitches},
            {"myocytes", profile.metabolismMyocytes},
            {"nitrocytes", profile.metabolismNitrocytes},
            {"photocytes", profile.metabolismPhotocytes},
            {"cytoplasmViscosityCells", profile.metabolismCytoplasmViscosityCells},
            {"glueocytes", profile.metabolismGlueocytes},
            {"ciliocytes", profile.metabolismCiliocytes},
            {"ciliocyteAnimationWraps", profile.metabolismCiliocyteAnimationWraps},
            {"secrocytes", profile.metabolismSecrocytes},
            {"secrocyteNitrateDrainCells", profile.metabolismSecrocyteNitrateDrainCells},
            {"salinitySamples", profile.metabolismSalinitySamples},
            {"avgSalinitySamplesPerStep", static_cast<double>(profile.metabolismSalinitySamples) / profileSteps},
            {"agingDeaths", profile.metabolismAgingDeaths},
            {"mutationPeriodHits", profile.metabolismMutationPeriodHits},
            {"avgMutationPeriodHitsPerStep", static_cast<double>(profile.metabolismMutationPeriodHits) / profileSteps},
            {"mutationRollHits", profile.metabolismMutationRollHits},
            {"mutationAttempts", profile.metabolismMutationAttempts},
            {"mutationsRecorded", profile.metabolismMutationsRecorded},
            {"linkIterations", profile.metabolismLinkIterations},
            {"avgLinkIterationsPerStep", static_cast<double>(profile.metabolismLinkIterations) / profileSteps},
            {"linkPairsProcessed", profile.metabolismLinkPairsProcessed},
            {"avgLinkPairsProcessedPerStep", static_cast<double>(profile.metabolismLinkPairsProcessed) / profileSteps},
            {"boundaryLinkSkips", profile.metabolismBoundaryLinkSkips},
            {"topologyEdgeLoopTotalMs", nanosToMs(profile.metabolismTopologyEdgeLoopNanos)},
            {"topologyEdgeLoopAvgMs", nanosToMs(profile.metabolismTopologyEdgeLoopNanos) / profileSteps},
            {"topologyEdgeLoopShareOfMetabolism",
                    profile.metabolismTopologyEdgeLoopNanos * 100.0
                            / std::max<long long>(1, profile.stageNanos[SimulationWorldProfile::METABOLISM])},
            {"topologyEdgeReads", profile.metabolismTopologyEdgeReads},
            {"avgTopologyEdgeReadsPerStep", static_cast<double>(profile.metabolismTopologyEdgeReads) / profileSteps},
            {"lipidCells", profile.metabolismLipidCells},
            {"lipidStoreEvents", profile.metabolismLipidStoreEvents},
            {"lipidMoveEvents", profile.metabolismLipidMoveEvents},
            {"massClampEvents", profile.metabolismMassClampEvents},
            {"nitrateClampEvents", profile.metabolismNitrateClampEvents},
            {"nitrateDeaths", profile.metabolismNitrateDeaths},
            {"metabolismTotalMs", metabolismMs},
            {"metabolismAvgMs", metabolismMs / profileSteps}
    };
    double signalsMs = nanosToMs(profile.stageNanos[SimulationWorldProfile::SIGNALS]);
    PerfJson signalCounters = {
            {"topologyLinkCountQueries", profile.signalTopologyLinkCountQueries},
            {"avgTopologyLinkCountQueriesPerStep",
                    static_cast<double>(profile.signalTopologyLinkCountQueries) / profileSteps},
            {"topologyAdjacentCellQueries", profile.signalTopologyAdjacentCellQueries},
            {"avgTopologyAdjacentCellQueriesPerStep",
                    static_cast<double>(profile.signalTopologyAdjacentCellQueries) / profileSteps},
            {"topologyEdgeIterations", profile.signalTopologyEdgeIterations},
            {"avgTopologyEdgeIterationsPerStep",
                    static_cast<double>(profile.signalTopologyEdgeIterations) / profileSteps},
            {"topologyEndpointUpdates", profile.signalTopologyEndpointUpdates},
            {"avgTopologyEndpointUpdatesPerStep",
                    static_cast<double>(profile.signalTopologyEndpointUpdates) / profileSteps},
            {"linkIterations", profile.signalLinkIterations},
            {"avgLinkIterationsPerStep", static_cast<double>(profile.signalLinkIterations) / profileSteps},
            {"boundaryLinkSkips", profile.signalBoundaryLinkSkips},
            {"lipocyteLinkSkips", profile.signalLipocyteLinkSkips},
            {"featureScanTotalMs", nanosToMs(profile.signalFeatureScanNanos)},
            {"cellPhaseTotalMs", nanosToMs(profile.signalCellPhaseNanos)},
            {"edgePhaseTotalMs", nanosToMs(profile.signalEdgePhaseNanos)},
            {"clampPhaseTotalMs", nanosToMs(profile.signalClampPhaseNanos)},
            {"adjacencyBuildTotalMs", nanosToMs(profile.signalAdjacencyBuildNanos)},
            {"fusedCellPhaseTotalMs", nanosToMs(profile.signalFusedCellPhaseNanos)},
            {"cellPhaseVisits", profile.signalCellPhaseVisits},
            {"neurocyteEvaluations", profile.signalNeurocyteEvaluations},
            {"neurocyteTiming", "fused-into-cell-phase"},
            {"clampEvaluations", profile.signalClampEvaluations},
            {"clampLowWrites", profile.signalClampLowWrites},
            {"clampHighWrites", profile.signalClampHighWrites},
            {"adjacencyCacheHits", profile.signalAdjacencyCacheHits},
            {"adjacencyCacheRebuilds", profile.signalAdjacencyCacheRebuilds},
            {"csrReferenceVisits", profile.signalCsrReferenceVisits},
            {"signalsTotalMs", signalsMs},
            {"signalsAvgMs", signalsMs / profileSteps}
    };

    PerfJson spatialAttribution = {
            {"gridClear",
                    makeCoarseAttribution(profile.gridClearNanos, profileSteps,
                            {{"cellTouchedBucketClear", profile.gridCellCollisionClearNanos},
                                    {"smellTouchedBucketClear", profile.gridSmellCollisionClearNanos},
                                    {"renderOverflowTouchedHeadClear", profile.gridRenderOverflowClearNanos},
                                    {"renderOccupiedBitsetClear", profile.gridRenderOccupancyClearNanos}})},
            {"cellSpatialBuild",
                    makeCoarseAttribution(profile.gridCellAndSmellBuildNanos, profileSteps,
                            {{"positionToPhysicalGrid", profile.gridCellPositionConversionNanos},
                                    {"smellCellInsertion", profile.gridSmellCellInsertionNanos},
                                    {"physicalGridInsertion", profile.gridPhysicalCellInsertionNanos},
                                    {"renderOccupancy", profile.gridRenderOccupancyNanos},
                                    {"renderOverflow", profile.gridRenderOverflowNanos}})},
            {"foodRenderIndex",
                    makeCoarseAttribution(profile.foodRenderIndexBuildNanos, profileSteps,
                            {{"headClearAndStoragePreparation", profile.foodRenderHeadClearNanos},
                                    {"mappingResolve", profile.foodRenderMappingNanos},
                                    {"orderedHeadInsertion", profile.foodRenderInsertionNanos}})},
            {"measurementModel",
                    {{"productionCellSpatialPath", "single-fused-cell-loop"},
                            {"profiledCellSpatialPath", "order-preserving-profile-only-phase-split"},
                            {"profileOffPathChanged", true},
                            {"foodIndexLifecycle", "generation-aware-demand-driven-shared-index"},
                            {"foodHeadClear", "touched-only"},
                            {"perCellClocks", false}}}
    };

    PerfJson attribution = {
            {"updateGrids", makeCoarseAttribution(profile.stageNanos[SimulationWorldProfile::UPDATE_GRIDS], profileSteps, {{"contactCacheInvalidation", profile.gridCacheInvalidationNanos}, {"gridClear", profile.gridClearNanos}, {"consumerDiscovery", profile.gridConsumerDiscoveryNanos}, {"foodSmellConsumerMask", profile.gridFoodSmellMaskNanos}, {"cellAndSmellIndexBuild", profile.gridCellAndSmellBuildNanos}, {"foodMappingRefresh", profile.gridFoodMappingRefreshNanos}, {"foodRenderIndexBuild", profile.gridFoodRenderBuildNanos}})},
            {"eatFoods", makeCoarseAttribution(profile.stageNanos[SimulationWorldProfile::EAT_FOODS], profileSteps, {{"decay", profile.eatFoodDecayNanos}, {"preQueryIndexBuild", profile.eatFoodPreQueryIndexBuildNanos}, {"smellIndexBuild", profile.eatFoodSmellIndexBuildNanos}, {"queryAndUpdate", profile.eatFoodQueryAndUpdateNanos}, {"removal", profile.eatFoodRemovalNanos}, {"motion", profile.eatFoodMotionNanos}, {"postUpdateIndexBuild", profile.eatFoodPostUpdateIndexBuildNanos}})},
            {"overlaps",
                    makeCoarseAttribution(profile.stageNanos[SimulationWorldProfile::OVERLAPS], profileSteps,
                            {{"setup", profile.overlapSetupNanos},
                                    {"broadphaseBuild", profile.overlapBroadphaseBuildNanos},
                                    {"scanSemanticResolutionAndEndpointAdmission", profile.overlapBroadphaseScanNanos},
                                    {"materializedPairEffects", profile.overlapPairEffectsNanos},
                                    {"deathCleanup", profile.overlapDeathCleanupNanos}})},
            {"cellCellCollisions",
                    makeCoarseAttribution(profile.stageNanos[SimulationWorldProfile::CELL_CELL_COLLISIONS], profileSteps,
                            {{"geometryEvaluationAndIntentCollection", profile.collisionIntentCollectNanos},
                                    {"orderedForceAndTypedCommit", profile.collisionIntentApplyNanos}})},
            {"signals", makeCoarseAttribution(profile.stageNanos[SimulationWorldProfile::SIGNALS], profileSteps, {{"featureScan", profile.signalFeatureScanNanos}, {"adjacencyBuild", profile.signalAdjacencyBuildNanos}, {"cellPhaseIncludingNeurocyteEvaluation", profile.signalCellPhaseNanos}, {"edgePhase", profile.signalEdgePhaseNanos}, {"clampPhase", profile.signalClampPhaseNanos}, {"fusedCellOwnedPhase", profile.signalFusedCellPhaseNanos}})},
            {"workerExecution",
                    {{"available", false},
                            {"reason", "no-production-simulation-worker-pool"},
                            {"warmupNanos", profile.workerWarmupNanos},
                            {"autotuneNanos", profile.workerAutotuneNanos},
                            {"dispatchNanos", profile.workerDispatchNanos},
                            {"usefulWorkNanos", profile.workerUsefulWorkNanos},
                            {"barrierNanos", profile.workerBarrierNanos},
                            {"reductionNanos", profile.workerReductionNanos},
                            {"warmupRuns", profile.workerWarmupRuns},
                            {"autotuneRuns", profile.workerAutotuneRuns},
                            {"dispatches", profile.workerDispatches}}}
    };
    PerfJson capabilityStatus = {
            {"pairStream",
                    {{"mode", profile.overlapPairBufferWrites == 0 ? "fused-no-materialized-stream" : "materialized"},
                            {"writes", profile.overlapPairBufferWrites},
                            {"rawBudgetAvailable", false},
                            {"rawBudgetOverflowCount", profile.overlapRawBudgetOverflows}}},
            {"foodIndexGeneration",
                    {{"available", true},
                            {"generationHits", profile.foodSpatialGenerationHits},
                            {"generationMisses", profile.foodSpatialGenerationMisses}}},
            {"foodPostUpdateRefresh",
                    {{"available", false},
                            {"reason", "replaced-by-demand-driven-owner-capture-ensure"},
                            {"totalNanos", profile.eatFoodPostUpdateIndexBuildNanos}}},
            {"foodPostCaptureRefresh",
                    {{"available", true},
                            {"ownerEnsureCalls", profile.foodSpatialCaptureEnsureCalls},
                            {"captureMutatesWorld", false}}},
            {"foodStaleLinearCaptureFallback",
                    {{"available", true},
                            {"preservesGridOrder", true},
                            {"captureMutatesWorld", false}}}
    };

    return {{"schemaVersion", 5},
            {"scenario", "simulation/stage_breakdown"},
            {"inputPath", inputPath},
            {"substrateFormatVersion", substrateFormatVersion},
            {"overlapBroadphaseAlgorithm", overlapScanAlgorithmName(world.cellOverlapScanAlgorithm())},
            {"signalUpdateAlgorithm", signalUpdateAlgorithmName(world.mCellData.signalUpdateAlgorithm())},
            {"warmupSteps", warmupSteps},
            {"profileSteps", profileSteps},
            {"cells", world.mCellData.mCellCount},
            {"food", world.mFoodData.mFoodCount},
            {"time", world.mTime},
            {"totalMs", totalMs},
            {"avgStepMs", totalMs / profileSteps},
            {"stepsPerSecond", profileSteps / (totalMs / 1000.0)},
            {"state", {{"initial", initialState}, {"measuredStart", measuredStartState}, {"final", finalState}}},
            {"stages", stages},
            {"attribution", attribution},
            {"spatialAttribution", spatialAttribution},
            {"capabilityStatus", capabilityStatus},
            {"spatialIndexCounters", spatialIndexCounters},
            {"linkCounters", linkCounters},
            {"collisionIntentCounters", collisionIntentCounters},
            {"cellForcesCounters", cellForcesCounters},
            {"metabolismCounters", metabolismCounters},
            {"signalCounters", signalCounters},
            {"eatFoodCounters", eatFoodCounters},
            {"overlapCounters", overlapCounters}};
}

PerfJson runRenderCopyToDrawProfileJson(SimulationWorld& world,
        const std::string& inputPath,
        int substrateFormatVersion,
        int warmupSteps,
        int profileSteps,
        double viewRadius) {
    NativeSimulationWorldRenderer renderer(world.mEnvironment.maxCellCount);
    NativeSimulationWorldRenderer::ViewBounds view = renderProfileView(world, viewRadius);

    for (int step = 0; step < warmupSteps; step++) {
        world.update1(true, 1.0);
        renderer.copyToDraw(world, view);
        world.update2(true, 1.0);
    }

    NativeSimulationWorldRenderer::CopyToDrawProfile total;
    NativeSimulationWorldRenderer::CopyToDrawProfile minSample;
    NativeSimulationWorldRenderer::CopyToDrawProfile maxSample;
    bool hasSample = false;
    int startCellCount = world.mCellData.mCellCount;
    int startFoodCount = world.mFoodData.mFoodCount;

    for (int step = 0; step < profileSteps; step++) {
        world.update1(true, 1.0);
        NativeSimulationWorldRenderer::CopyToDrawProfile sample = renderer.profileCopyToDraw(world, view);
        world.update2(true, 1.0);
        addProfile(total, sample);
        if (!hasSample || sample.totalNanos < minSample.totalNanos) {
            minSample = sample;
        }
        if (!hasSample || sample.totalNanos > maxSample.totalNanos) {
            maxSample = sample;
        }
        hasSample = true;
    }

    auto avgMs = [profileSteps](long long nanos) {
        return nanosToMs(nanos) / profileSteps;
    };
    auto share = [&total](long long nanos) {
        return total.totalNanos == 0 ? 0.0 : static_cast<double>(nanos) * 100.0 / total.totalNanos;
    };
    auto avgInt = [profileSteps](long long value) {
        return static_cast<double>(value) / profileSteps;
    };
    auto stage = [&](long long nanos) {
        return PerfJson{{"avgMs", avgMs(nanos)}, {"share", share(nanos)}};
    };

    if (total.contactChordResolveRequests
            != total.contactChordDirectSourceViews + total.contactChordCacheHits + total.contactChordCacheMisses) {
        throw std::runtime_error("Cell-pass wall resolver request accounting mismatch");
    }
    if (total.contactChordCacheMisses != total.uniqueNormalizedCells
            || total.contactChordCacheMisses != total.normalizerCalls) {
        throw std::runtime_error("Wall cache miss accounting mismatch");
    }
    if (total.normalizedContactChordCount != total.normalizerContactChordCountSum) {
        throw std::runtime_error("Normalized wall accounting mismatch");
    }
    if (total.linkOnlyCacheMisses > total.contactChordCacheMisses
            || total.normalizerCalls
                    != total.cellPassNormalizerCalls + total.linkPassNormalizerCalls) {
        throw std::runtime_error("Wall cache phase accounting mismatch");
    }
#ifdef CELLULARITY_RENDER_PROFILE
    if (total.freshPhysicsContactCaptures + total.staleGeometryContactFallbacks != profileSteps) {
        throw std::runtime_error("Render contact source accounting mismatch");
    }
    if (total.foodGridCaptureCalls + total.foodLinearCaptureCalls
                    + total.foodStaleIndexFallbacks
            != profileSteps) {
        throw std::runtime_error("Render Food capture-path accounting mismatch");
    }
    if (total.foodLinearCaptureCalls != total.foodLinearMissingIndexFallbacks + total.foodLinearCoverageFallbacks) {
        throw std::runtime_error("Render Food linear-fallback accounting mismatch");
    }
#endif

    PerfJson renderAttribution = {
            {"copyToDraw", makeCoarseAttribution(total.totalNanos, profileSteps, {{"sourceCapture", total.sourceCaptureNanos}, {"frameBuild", total.totalNanos - total.sourceCaptureNanos}})},
            {"sourceCapture", makeCoarseAttribution(total.sourceCaptureNanos, profileSteps, {{"contactMaterialization", total.contactChordCollectionNanos}, {"cellCapture", total.cellCaptureNanos}, {"linkCapture", total.linkCaptureNanos}, {"foodCapture", total.foodCaptureNanos}, {"deadSplashCapture", total.deadSplashCaptureNanos}, {"markerCapture", total.markerCaptureNanos}})}
    };

    return {{"schemaVersion", 6},
            {"scenario", "render/copy_to_draw"},
            {"inputPath", inputPath},
            {"substrateFormatVersion", substrateFormatVersion},
            {"overlapBroadphaseAlgorithm", overlapScanAlgorithmName(world.cellOverlapScanAlgorithm())},
            {"wallProfilingScope", "cell-and-link-passes"},
            {"wallCacheScope", "cell-and-link-passes"},
            {"wallResolutionDefinition",
                    "resolver-lookup-plus-cache-miss-copy-"
                    "normalization-and-active-range-append"},
            {"contactChordNormalizationDefinition", "clipAndOrderRenderContactChords-only"},
            {"timingComposition",
                    "sourceCapture-plus-frameBuild-equals-total; legacy object-stage "
                    "timings include their corresponding capture substages"},
            {"warmupSteps", warmupSteps},
            {"profileSteps", profileSteps},
            {"viewRadius", viewRadius > 0.0 ? viewRadius : world.mEnvironment.mRadius},
            {"startCellCount", startCellCount},
            {"endCellCount", world.mCellData.mCellCount},
            {"startFoodCount", startFoodCount},
            {"endFoodCount", world.mFoodData.mFoodCount},
            {"avgMs", avgMs(total.totalNanos)},
            {"minMs", nanosToMs(minSample.totalNanos)},
            {"maxMs", nanosToMs(maxSample.totalNanos)},
            {"attribution", renderAttribution},
            {"capabilityStatus",
                    {{"contactGenerationFreshness",
                             {{"available", true},
                                     {"freshPhysicsCaptures", total.freshPhysicsContactCaptures},
                                     {"staleGeometryFallbacks", total.staleGeometryContactFallbacks}}},
                            {"foodIndexGeneration",
                                    {{"available", true},
                                            {"generationHits", total.foodIndexGenerationHits},
                                            {"generationMisses", total.foodIndexGenerationMisses}}},
                            {"foodPostCaptureRefresh",
                                    {{"available", true},
                                            {"ownerEnsureRequired", true},
                                            {"captureMutatesWorld", false}}},
                            {"foodStaleLinearFallback",
                                    {{"available", true},
                                            {"fallbackCalls", total.foodStaleIndexFallbacks},
                                            {"mappingVisits", total.foodStaleIndexMappingVisits},
                                            {"preservesGridOrder", true}}}}},
            {"stages",
                    {{"sourceCapture", stage(total.sourceCaptureNanos)},
                            {"contactChordCollection", stage(total.contactChordCollectionNanos)},
                            {"cellCapture", stage(total.cellCaptureNanos)},
                            {"linkCapture", stage(total.linkCaptureNanos)},
                            {"foodCapture", stage(total.foodCaptureNanos)},
                            {"deadSplashCapture", stage(total.deadSplashCaptureNanos)},
                            {"frameBuild", stage(total.totalNanos - total.sourceCaptureNanos)},
                            {"culling", stage(total.cullingNanos)},
                            {"clear", stage(total.clearNanos)},
                            {"cells", stage(total.cellsNanos)},
                            {"links", stage(total.linksNanos)},
                            {"wallResolution", stage(total.contactChordResolutionNanos)},
                            {"cellPassWallResolution", stage(total.cellPassContactChordResolutionNanos)},
                            {"linkPassWallResolution", stage(total.linkPassContactChordResolutionNanos)},
                            {"contactChordNormalization", stage(total.contactChordNormalizationNanos)},
                            {"cellPassWallNormalization", stage(total.cellPassContactChordNormalizationNanos)},
                            {"linkPassWallNormalization", stage(total.linkPassContactChordNormalizationNanos)},
                            {"normalizerFirstPass", stage(total.normalizerFirstPassNanos)},
                            {"normalizerSecondPass", stage(total.normalizerSecondPassNanos)},
                            {"normalizerSort", stage(total.normalizerSortNanos)},
                            {"cellGeometryAndAttributes", stage(total.cellsNanos - total.cellPassContactChordResolutionNanos)},
                            {"linkGeometryAndAttributes", stage(total.linksNanos - total.linkPassContactChordResolutionNanos)},
                            {"outlines", stage(total.outlinesNanos)},
                            {"foods", stage(total.foodsNanos)},
                            {"deadCells", stage(total.deadCellsNanos)}}},
            {"counters",
                    {{"avgSourceActiveStorageBytes", avgInt(static_cast<long long>(total.sourceActiveStorageBytes))},
                            {"avgSourceRetainedStorageBytes", avgInt(static_cast<long long>(total.sourceRetainedStorageBytes))},
                            {"avgSourceCellRecords", avgInt(total.sourceCellRecordCount)},
                            {"avgSourceActiveContactChords", avgInt(total.sourceActiveContactChordCount)},
                            {"avgMaterializedContactChords", avgInt(total.materializedContactChords)},
                            {"freshPhysicsContactCaptures", total.freshPhysicsContactCaptures},
                            {"staleGeometryContactFallbacks", total.staleGeometryContactFallbacks},
                            {"avgSourceCandidateLinks", avgInt(total.sourceCandidateLinkCount)},
                            {"avgSourceFoodRecords", avgInt(total.sourceFoodRecordCount)},
                            {"foodGridCaptureCalls", total.foodGridCaptureCalls},
                            {"foodLinearCaptureCalls", total.foodLinearCaptureCalls},
                            {"foodLinearMissingIndexFallbacks", total.foodLinearMissingIndexFallbacks},
                            {"foodLinearCoverageFallbacks", total.foodLinearCoverageFallbacks},
                            {"avgFoodCandidateVisits", avgInt(total.foodCandidateVisits)},
                            {"foodIndexGenerationHits", total.foodIndexGenerationHits},
                            {"foodIndexGenerationMisses", total.foodIndexGenerationMisses},
                            {"foodStaleIndexFallbacks", total.foodStaleIndexFallbacks},
                            {"avgFoodStaleIndexMappingVisits",
                                    avgInt(total.foodStaleIndexMappingVisits)},
                            {"avgSourceDeadSplashRecords", avgInt(total.sourceDeadSplashRecordCount)},
                            {"avgVisibleCells", avgInt(total.visibleCellCount)},
                            {"avgCellVertices", avgInt(total.cellDrawVertexCount)},
                            {"avgCellIndices", avgInt(total.cellDrawIndexCount)},
                            {"avgFoodInstances", avgInt(total.foodInstanceCount)},
                            {"avgDeadCellInstances", avgInt(total.deadCellInstanceCount)},
                            {"avgLinkVertices", avgInt(total.drawLinksCount)},
                            {"contactChordResolveRequestsAvg", avgInt(total.contactChordResolveRequests)},
                            {"wallDirectSourceViewsAvg", avgInt(total.contactChordDirectSourceViews)},
                            {"wallCacheHitsAvg", avgInt(total.contactChordCacheHits)},
                            {"wallCacheMissesAvg", avgInt(total.contactChordCacheMisses)},
                            {"uniqueNormalizedCellsAvg", avgInt(total.uniqueNormalizedCells)},
                            {"normalizedContactChordCountAvg", avgInt(total.normalizedContactChordCount)},
                            {"linkOnlyCacheMissesAvg", avgInt(total.linkOnlyCacheMisses)},
                            {"normalizerCallsAvg", avgInt(total.normalizerCalls)},
                            {"cellPassNormalizerCallsAvg", avgInt(total.cellPassNormalizerCalls)},
                            {"linkPassNormalizerCallsAvg", avgInt(total.linkPassNormalizerCalls)},
                            {"normalizerSkippedCallsAvg", avgInt(total.normalizerSkippedCalls)},
                            {"normalizerAvgWallsPerCall",
                                    total.normalizerCalls == 0
                                            ? 0.0
                                            : static_cast<double>(total.normalizerContactChordCountSum) / total.normalizerCalls},
                            {"cellPassNormalizerWallsAvg", avgInt(total.cellPassNormalizerContactChordCountSum)},
                            {"linkPassNormalizerWallsAvg", avgInt(total.linkPassNormalizerContactChordCountSum)},
                            {"normalizerFirstPassPairsAvg", avgInt(total.normalizerFirstPassPairs)},
                            {"normalizerFirstPassDiscardedSkipsAvg", avgInt(total.normalizerFirstPassDiscardedSkips)},
                            {"normalizerFirstPassIntersectionsAvg", avgInt(total.normalizerFirstPassIntersections)},
                            {"normalizerFirstPassJoinMarksAvg", avgInt(total.normalizerFirstPassJoinMarks)},
                            {"normalizerFirstPassDiscardMarksAvg", avgInt(total.normalizerFirstPassDiscardMarks)},
                            {"normalizerSecondPassPairsAvg", avgInt(total.normalizerSecondPassPairs)},
                            {"normalizerSecondPassSelfSkipsAvg", avgInt(total.normalizerSecondPassSelfSkips)},
                            {"normalizerSecondPassEndpointSkipsAvg", avgInt(total.normalizerSecondPassEndpointSkips)},
                            {"normalizerSecondPassDiscardMarksAvg", avgInt(total.normalizerSecondPassDiscardMarks)},
                            {"normalizerSortComparisonsAvg", avgInt(total.normalizerSortComparisons)},
                            {"normalizerSortMovesAvg", avgInt(total.normalizerSortMoves)}}},
            {"finalTime", world.mTime}};
}

PerfJson runDefaultRandomCellsMaxUpsJson(int maxCells,
        int maxFoods,
        double radius,
        int warmupSteps,
        int profileSteps) {
    Environment environment = Environment::constructDefaultEnvironment(maxCells, maxFoods, radius);
    environment.addRandom = true;
    SimulationWorld world(false, environment);

    warmupSimulation(world, warmupSteps, false);

    int startCellCount = world.mCellData.mCellCount;
    int startFoodCount = world.mFoodData.mFoodCount;
    double startTime = world.mTime;

    auto startedAt = Clock::now();
    for (int step = 0; step < profileSteps; step++) {
        world.update(false, 1.0);
    }
    auto totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt);
    double totalMs = nanosToMs(totalNanos.count());

    return {
            {"schemaVersion", 1},
            {"scenario", "simulation/default_random_cells_max_ups"},
            {"maxCells", maxCells},
            {"maxFoods", maxFoods},
            {"radius", radius},
            {"addRandom", true},
            {"warmupSteps", warmupSteps},
            {"profileSteps", profileSteps},
            {"startCellCount", startCellCount},
            {"endCellCount", world.mCellData.mCellCount},
            {"startFoodCount", startFoodCount},
            {"endFoodCount", world.mFoodData.mFoodCount},
            {"startTime", startTime},
            {"finalTime", world.mTime},
            {"totalMs", totalMs},
            {"avgStepMs", totalMs / profileSteps},
            {"stepsPerSecond", profileSteps / (totalMs / 1000.0)}
    };
}

PerfJson runDefaultRandomCellsDetailJson(int maxCells,
        int maxFoods,
        double radius,
        int warmupSteps,
        int profileSteps) {
    Environment environment = Environment::constructDefaultEnvironment(maxCells, maxFoods, radius);
    environment.addRandom = true;
    SimulationWorld world(false, environment);

    PerfJson report = runStageBreakdownProfileJson(world,
            "default-random-cells",
            0,
            warmupSteps,
            profileSteps);
    report["scenario"] = "simulation/default_random_cells_stage_breakdown";
    report["maxCells"] = maxCells;
    report["maxFoods"] = maxFoods;
    report["radius"] = radius;
    report["addRandom"] = true;
    return report;
}

PerfJson runEvolutionTwoCellSwimmerJsonInternal(const char* scenario,
        const char* initialPopulation,
        bool addRandomCells,
        bool seedDefaultEditorCell,
        int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        int stableWindowSteps,
        double minStableTravel,
        double mutationCoef,
        double foodSpeed,
        double foodSize,
        int seedCellCount,
        double idleMetabolismScale,
        double flagellocyteCostScale,
        double phagocyteEatSpeedScale) {
    if (stableWindowSteps < 1) {
        stableWindowSteps = 1;
    }
    if (minStableTravel < 0.0) {
        minStableTravel = 0.0;
    }

    Environment environment = Environment::constructDefaultEnvironment(maxCells, maxFoods, radius);
    environment.addRandom = addRandomCells;
    environment.mRadiation = radiation;
    environment.mTestMutationCoef = mutationCoef;
    environment.mFoodSpeed = foodSpeed;
    environment.mFoodSize = foodSize;
    environment.mTestIdleMetabolismScale = idleMetabolismScale;
    environment.mTestFlagellocyteCostScale = flagellocyteCostScale;
    environment.mTestPhagocyteEatSpeedScale = phagocyteEatSpeedScale;
    environment.mRandom.setSeed(seed);
    SimulationRandom::global.setSeed(seed);

    SimulationWorld world(false, environment);
    int seededCellsAdded = 0;
    if (seedDefaultEditorCell) {
        cellularity::GenomeData genome = createDefaultEditorGenome(environment.mRandom);
        int targetSeedCellCount = std::max(1, seedCellCount);
        for (int seedCellIndex = 0; seedCellIndex < targetSeedCellCount; seedCellIndex++) {
            double positionX;
            double positionY;
            do {
                positionX = environment.mRandom.nextUniformSymmetric() * environment.mRadius;
                positionY = environment.mRandom.nextUniformSymmetric() * environment.mRadius;
            } while (positionX * positionX + positionY * positionY >= environment.mRadius * environment.mRadius);
            if (addUserCellToWorld(world, positionX, positionY, genome)) {
                seededCellsAdded++;
            }
        }
    }
    SimulationWorldProfile profile;
    profile.clear();
    world.setProfile(&profile);

    TwoCellSwimmerSearchResult firstCandidate;
    TwoCellSwimmerSearchResult firstMatch;
    TwoCellSwimmerCandidateTracker tracker;
    int firstCandidateStep = -1;
    double firstCandidateTime = -1.0;
    int stableMatchStep = -1;
    double stableMatchTime = -1.0;
    int stableStartStep = -1;
    double stableStartTime = -1.0;
    double stableTravel = 0.0;
    int peakCellCount = world.mCellData.mCellCount;
    int extinctionStep = -1;
    double extinctionTime = -1.0;

    auto startedAt = Clock::now();
    for (int step = 0; step < maxSteps; step++) {
        world.update(false, 1.0);
        const int currentStep = step + 1;
        peakCellCount = std::max(peakCellCount, world.mCellData.mCellCount);
        if (world.mCellData.mCellCount == 0 && extinctionStep < 0) {
            extinctionStep = currentStep;
            extinctionTime = world.mTime;
            if (!addRandomCells) {
                break;
            }
        }

        if (tracker.active) {
            if (isTrackedCandidateStillPresent(world, tracker)) {
                updateTrackedCandidate(world, tracker);
                if (tracker.stableSteps >= stableWindowSteps && tracker.maxTravel >= minStableTravel) {
                    firstMatch = {
                            true,
                            tracker.flagellocyteCellIndex,
                            tracker.phagocyteCellIndex,
                            tracker.flagellocyteGeneIndex,
                            tracker.phagocyteGeneIndex
                    };
                    stableMatchStep = currentStep;
                    stableMatchTime = world.mTime;
                    stableStartStep = tracker.startStep;
                    stableStartTime = tracker.startStep * SimulationWorld::DELTA_TIME;
                    stableTravel = tracker.maxTravel;
                    break;
                }
                continue;
            }
            tracker = {};
        }

        TwoCellSwimmerSearchResult candidate = findTwoCellSwimmer(world);
        if (candidate.found) {
            if (!firstCandidate.found) {
                firstCandidate = candidate;
                firstCandidateStep = currentStep;
                firstCandidateTime = world.mTime;
            }
            startTrackingCandidate(world, candidate, currentStep, tracker);
            if (tracker.stableSteps >= stableWindowSteps && tracker.maxTravel >= minStableTravel) {
                firstMatch = candidate;
                stableMatchStep = currentStep;
                stableMatchTime = world.mTime;
                stableStartStep = tracker.startStep;
                stableStartTime = tracker.startStep * SimulationWorld::DELTA_TIME;
                stableTravel = tracker.maxTravel;
                break;
            }
        }
    }
    world.setProfile(nullptr);
    if (!firstMatch.found && tracker.active) {
        stableStartStep = tracker.startStep;
        stableStartTime = tracker.startStep * SimulationWorld::DELTA_TIME;
        stableTravel = tracker.maxTravel;
    }
    if (!firstCandidate.found) {
        firstCandidate = firstMatch;
    }
    auto totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt);
    const double totalMs = nanosToMs(totalNanos.count());
    const int stepsRun = firstMatch.found ? stableMatchStep : maxSteps;
    const double finalFoodMass = world.mFoodData.getFoodMass();
    const double foodUtilizationRatio = profile.foodMassSpawned > 0.0
            ? profile.foodMassConsumedByCells / profile.foodMassSpawned
            : 0.0;
    const double foodDecayRatio = profile.foodMassSpawned > 0.0
            ? profile.foodMassDecayed / profile.foodMassSpawned
            : 0.0;
    const double foodBoundaryLossRatio = profile.foodMassSpawned > 0.0
            ? profile.foodMassRemovedAtBoundary / profile.foodMassSpawned
            : 0.0;
    const double foodRemainingRatio = profile.foodMassSpawned > 0.0
            ? finalFoodMass / profile.foodMassSpawned
            : 0.0;

    return {
            {"schemaVersion", 1},
            {"scenario", scenario},
            {"matcher", "stable_linked_flagellocyte_phagocyte_motion"},
            {"candidateMatcher", "linked_flagellocyte_phagocyte"},
            {"initialPopulation", initialPopulation},
            {"maxSteps", maxSteps},
            {"stepsRun", stepsRun},
            {"found", firstMatch.found},
            {"firstMatchStep", stableMatchStep},
            {"firstMatchTime", stableMatchTime},
            {"firstCandidateStep", firstCandidateStep},
            {"firstCandidateTime", firstCandidateTime},
            {"stableStartStep", stableStartStep},
            {"stableStartTime", stableStartTime},
            {"stableWindowSteps", stableWindowSteps},
            {"minStableTravel", minStableTravel},
            {"stableTravel", stableTravel},
            {"maxCells", maxCells},
            {"maxFoods", maxFoods},
            {"radius", radius},
            {"radiation", radiation},
            {"mutationCoef", mutationCoef},
            {"foodSpeed", environment.mFoodSpeed},
            {"foodSize", environment.mFoodSize},
            {"idleMetabolismScale", environment.mTestIdleMetabolismScale},
            {"flagellocyteCostScale", environment.mTestFlagellocyteCostScale},
            {"phagocyteEatSpeedScale", environment.mTestPhagocyteEatSpeedScale},
            {"seed", seed},
            {"addRandom", addRandomCells},
            {"seedDefaultEditorCell", seedDefaultEditorCell},
            {"seedCellCount", seedCellCount},
            {"seededCellsAdded", seededCellsAdded},
            {"finalTime", world.mTime},
            {"finalCellCount", world.mCellData.mCellCount},
            {"finalFoodCount", world.mFoodData.mFoodCount},
            {"finalFoodMass", finalFoodMass},
            {"peakCellCount", peakCellCount},
            {"extinctionStep", extinctionStep},
            {"extinctionTime", extinctionTime},
            {"foodUtilization", {{"spawnedFoodMass", profile.foodMassSpawned}, {"corpseFoodMass", profile.foodMassSpawnedFromCorpses}, {"consumedFoodMass", profile.foodMassConsumedByCells}, {"decayedFoodMass", profile.foodMassDecayed}, {"boundaryRemovedFoodMass", profile.foodMassRemovedAtBoundary}, {"finalFoodMass", finalFoodMass}, {"consumedPerSpawned", foodUtilizationRatio}, {"decayedPerSpawned", foodDecayRatio}, {"boundaryRemovedPerSpawned", foodBoundaryLossRatio}, {"remainingPerSpawned", foodRemainingRatio}}},
            {"totalMs", totalMs},
            {"avgStepMs", totalMs / std::max(1, stepsRun)},
            {"stepsPerSecond", stepsRun / (totalMs / 1000.0)},
            {"match", {{"flagellocyteCellIndex", firstMatch.flagellocyteCellIndex}, {"phagocyteCellIndex", firstMatch.phagocyteCellIndex}, {"flagellocyteGeneIndex", firstMatch.flagellocyteGeneIndex}, {"phagocyteGeneIndex", firstMatch.phagocyteGeneIndex}}},
            {"candidate", {{"flagellocyteCellIndex", firstCandidate.flagellocyteCellIndex}, {"phagocyteCellIndex", firstCandidate.phagocyteCellIndex}, {"flagellocyteGeneIndex", firstCandidate.flagellocyteGeneIndex}, {"phagocyteGeneIndex", firstCandidate.phagocyteGeneIndex}}}
    };
}

PerfJson runEvolutionTwoCellSwimmerJson(int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        int stableWindowSteps,
        double minStableTravel,
        double mutationCoef,
        double foodSpeed,
        double foodSize) {
    return runEvolutionTwoCellSwimmerJsonInternal("evolution/two_cell_swimmer",
            "random_cells",
            true,
            false,
            maxSteps,
            maxCells,
            maxFoods,
            radius,
            radiation,
            seed,
            stableWindowSteps,
            minStableTravel,
            mutationCoef,
            foodSpeed,
            foodSize,
            0,
            1.0,
            1.0,
            1.0);
}

PerfJson runEvolutionSeededCellTwoCellSwimmerJson(int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        int stableWindowSteps,
        double minStableTravel,
        double mutationCoef,
        double foodSpeed,
        double foodSize,
        int seedCellCount) {
    return runEvolutionTwoCellSwimmerJsonInternal("evolution/seeded_cell_two_cell_swimmer",
            "default_editor_cells",
            false,
            true,
            maxSteps,
            maxCells,
            maxFoods,
            radius,
            radiation,
            seed,
            stableWindowSteps,
            minStableTravel,
            mutationCoef,
            foodSpeed,
            foodSize,
            seedCellCount,
            1.0,
            1.0,
            1.0);
}

PerfJson runEvolutionSeededCellBalanceJson(int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        int stableWindowSteps,
        double minStableTravel,
        double mutationCoef,
        double foodSpeed,
        double foodSize,
        int seedCellCount,
        double idleMetabolismScale,
        double flagellocyteCostScale,
        double phagocyteEatSpeedScale) {
    return runEvolutionTwoCellSwimmerJsonInternal("evolution/seeded_cell_balance",
            "default_editor_cells",
            false,
            true,
            maxSteps,
            maxCells,
            maxFoods,
            radius,
            radiation,
            seed,
            stableWindowSteps,
            minStableTravel,
            mutationCoef,
            foodSpeed,
            foodSize,
            seedCellCount,
            idleMetabolismScale,
            flagellocyteCostScale,
            phagocyteEatSpeedScale);
}

PerfJson runEvolutionComplexityJson(int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        double mutationCoef,
        double cellTypeMutationWeightScale,
        double foodSpeed,
        double foodSize,
        double foodLumpiness,
        double foodLumpSize,
        int seedCellCount,
        double idleMetabolismScale,
        double flagellocyteCostScale,
        double phagocyteEatSpeedScale,
        int sampleIntervalSteps,
        int stableWindowSteps,
        int stableMinComponentSize) {
    sampleIntervalSteps = std::max(1, sampleIntervalSteps);
    stableWindowSteps = std::max(sampleIntervalSteps, stableWindowSteps);
    stableMinComponentSize = std::max(1, stableMinComponentSize);
    const int stableRequiredSamples = std::max(1, (stableWindowSteps + sampleIntervalSteps - 1) / sampleIntervalSteps);

    Environment environment = Environment::constructDefaultEnvironment(maxCells, maxFoods, radius);
    environment.addRandom = false;
    environment.mRadiation = radiation;
    environment.mTestMutationCoef = mutationCoef;
    environment.mFoodSpeed = foodSpeed;
    environment.mFoodSize = foodSize;
    environment.mFoodLumpiness = foodLumpiness;
    environment.mFoodLumpSize = foodLumpSize;
    environment.mTestIdleMetabolismScale = idleMetabolismScale;
    environment.mTestFlagellocyteCostScale = flagellocyteCostScale;
    environment.mTestPhagocyteEatSpeedScale = phagocyteEatSpeedScale;
    environment.mGenePool.fill(false);
    environment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::FLAGELLOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::LIPOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::GLUEOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::MYOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::NEUROCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::SENSEOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::STEREOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::STEMOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::CILIOCYTE)] = true;
    environment.mMutationWeights[Environment::MUTATION_CELL_TYPE_WEIGHT_OFFSET] *= cellTypeMutationWeightScale;
    environment.mRandom.setSeed(seed);
    SimulationRandom::global.setSeed(seed);

    SimulationWorld world(false, environment);
    cellularity::GenomeData genome = createDefaultEditorGenome(environment.mRandom);
    int seededCellsAdded = 0;
    int targetSeedCellCount = std::max(1, seedCellCount);
    for (int seedCellIndex = 0; seedCellIndex < targetSeedCellCount; seedCellIndex++) {
        double positionX;
        double positionY;
        do {
            positionX = environment.mRandom.nextUniformSymmetric() * environment.mRadius;
            positionY = environment.mRandom.nextUniformSymmetric() * environment.mRadius;
        } while (positionX * positionX + positionY * positionY >= environment.mRadius * environment.mRadius);
        if (addUserCellToWorld(world, positionX, positionY, genome)) {
            seededCellsAdded++;
        }
    }

    SimulationWorldProfile profile;
    profile.clear();
    world.setProfile(&profile);

    ComplexityComponentStats maxStats;
    ComplexityComponentStats finalStats;
    double componentSizeAuc = 0.0;
    double typeCountAuc = 0.0;
    double specializedTypeCountAuc = 0.0;
    double complexityScoreAuc = 0.0;
    int samples = 0;
    int stableStreakSamples = 0;
    int maxStableComponentSize = 0;
    int firstStableComplexityStep = -1;
    double firstStableComplexityTime = -1.0;
    int matureComponentObservations = 0;
    int samplesWithMatureComponents = 0;
    int maxMatureComponentsInSample = 0;
    int maxMatureComponentsSameLineage = 0;
    int matureSiblingComponents = 0;
    int splitReproductionEvents = 0;
    int stableMatureLineageSignatures = 0;
    std::unordered_set<uint64_t> observedMatureLineageSignatures;
    std::unordered_set<uint64_t> reproducedLineageSignatures;
    std::unordered_set<uint64_t> stableMatureLineageSignatureSet;
    std::unordered_map<uint64_t, int> matureLineageStreakSamples;
    std::vector<PreviousMatureComponentSnapshot> previousMatureComponents;
    int extinctionStep = -1;
    double extinctionTime = -1.0;
    int peakCellCount = world.mCellData.mCellCount;

    auto startedAt = Clock::now();
    int stepsRun = 0;
    for (int step = 0; step < maxSteps; step++) {
        world.update(false, 1.0);
        stepsRun = step + 1;
        peakCellCount = std::max(peakCellCount, world.mCellData.mCellCount);
        if (world.mCellData.mCellCount == 0) {
            extinctionStep = stepsRun;
            extinctionTime = world.mTime;
            finalStats = {};
            break;
        }

        if (stepsRun % sampleIntervalSteps != 0 && stepsRun != maxSteps) {
            continue;
        }

        std::vector<ComplexityComponentSnapshot> components = measureComplexityComponents(world);
        finalStats = {};
        for (const ComplexityComponentSnapshot& component : components) {
            if (component.stats.score > finalStats.score) {
                finalStats = component.stats;
            }
        }
        samples++;
        componentSizeAuc += finalStats.size * sampleIntervalSteps;
        typeCountAuc += finalStats.typeCount * sampleIntervalSteps;
        specializedTypeCountAuc += finalStats.specializedTypeCount * sampleIntervalSteps;
        complexityScoreAuc += finalStats.score * sampleIntervalSteps;
        if (finalStats.score > maxStats.score) {
            maxStats = finalStats;
        }

        std::vector<ComplexityComponentSnapshot> matureComponents;
        std::unordered_map<uint64_t, int> currentMatureByLineage;
        std::unordered_set<uint64_t> currentMatureLineages;
        for (ComplexityComponentSnapshot& component : components) {
            if (!isMatureOrganismComponent(component.stats, stableMinComponentSize)) {
                continue;
            }
            currentMatureByLineage[component.dominantGenomeSignature]++;
            currentMatureLineages.insert(component.dominantGenomeSignature);
            observedMatureLineageSignatures.insert(component.dominantGenomeSignature);
            matureComponents.push_back(std::move(component));
        }
        if (!matureComponents.empty()) {
            samplesWithMatureComponents++;
        }
        matureComponentObservations += static_cast<int>(matureComponents.size());
        maxMatureComponentsInSample = std::max(maxMatureComponentsInSample, static_cast<int>(matureComponents.size()));
        for (const auto& entry : currentMatureByLineage) {
            maxMatureComponentsSameLineage = std::max(maxMatureComponentsSameLineage, entry.second);
            if (entry.second >= 2) {
                reproducedLineageSignatures.insert(entry.first);
                matureSiblingComponents += entry.second - 1;
            }
        }

        std::vector<uint64_t> staleLineages;
        for (auto& entry : matureLineageStreakSamples) {
            if (currentMatureLineages.find(entry.first) == currentMatureLineages.end()) {
                staleLineages.push_back(entry.first);
            }
        }
        for (uint64_t lineageSignature : staleLineages) {
            matureLineageStreakSamples.erase(lineageSignature);
        }
        for (uint64_t lineageSignature : currentMatureLineages) {
            int streak = ++matureLineageStreakSamples[lineageSignature];
            if (streak >= stableRequiredSamples) {
                stableMatureLineageSignatureSet.insert(lineageSignature);
            }
        }
        stableMatureLineageSignatures = static_cast<int>(stableMatureLineageSignatureSet.size());

        for (const PreviousMatureComponentSnapshot& previous : previousMatureComponents) {
            int overlappedCurrentMatureComponents = 0;
            for (const ComplexityComponentSnapshot& current : matureComponents) {
                bool hasOverlap = false;
                for (int cellIndex : current.cells) {
                    if (previous.cells.find(cellIndex) != previous.cells.end()) {
                        hasOverlap = true;
                        break;
                    }
                }
                if (hasOverlap) {
                    overlappedCurrentMatureComponents++;
                }
            }
            if (overlappedCurrentMatureComponents >= 2) {
                splitReproductionEvents++;
            }
        }
        previousMatureComponents.clear();
        previousMatureComponents.reserve(matureComponents.size());
        for (const ComplexityComponentSnapshot& matureComponent : matureComponents) {
            PreviousMatureComponentSnapshot previous;
            previous.dominantGenomeSignature = matureComponent.dominantGenomeSignature;
            previous.cells.insert(matureComponent.cells.begin(), matureComponent.cells.end());
            previousMatureComponents.push_back(std::move(previous));
        }

        if (finalStats.size >= stableMinComponentSize) {
            stableStreakSamples++;
            if (stableStreakSamples >= stableRequiredSamples) {
                maxStableComponentSize = std::max(maxStableComponentSize, finalStats.size);
                if (firstStableComplexityStep < 0) {
                    firstStableComplexityStep = stepsRun;
                    firstStableComplexityTime = world.mTime;
                }
            }
        } else {
            stableStreakSamples = 0;
        }
    }

    if (samples == 0 && world.mCellData.mCellCount > 0) {
        finalStats = measureBestComplexityComponent(world);
        samples = 1;
        componentSizeAuc = finalStats.size;
        typeCountAuc = finalStats.typeCount;
        specializedTypeCountAuc = finalStats.specializedTypeCount;
        complexityScoreAuc = finalStats.score;
        if (finalStats.score > maxStats.score) {
            maxStats = finalStats;
        }
    }

    world.setProfile(nullptr);
    auto totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt);
    const double totalMs = nanosToMs(totalNanos.count());
    const double finalFoodMass = world.mFoodData.getFoodMass();
    const double foodUtilizationRatio = profile.foodMassSpawned > 0.0
            ? profile.foodMassConsumedByCells / profile.foodMassSpawned
            : 0.0;
    const double foodRemainingRatio = profile.foodMassSpawned > 0.0
            ? finalFoodMass / profile.foodMassSpawned
            : 0.0;

    auto statsJson = [](const ComplexityComponentStats& stats) {
        return PerfJson{
                {"size", stats.size},
                {"typeCount", stats.typeCount},
                {"specializedTypeCount", stats.specializedTypeCount},
                {"phagocytes", stats.phagocytes},
                {"flagellocytes", stats.flagellocytes},
                {"myocytes", stats.myocytes},
                {"stereocytes", stats.stereocytes},
                {"senseocytes", stats.senseocytes},
                {"neurocytes", stats.neurocytes},
                {"ciliocytes", stats.ciliocytes},
                {"motors", stats.motors},
                {"sensorsAndControl", stats.sensorsAndControl},
                {"score", stats.score}
        };
    };

    return {
            {"schemaVersion", 1},
            {"scenario", "evolution/complexity"},
            {"initialPopulation", "default_editor_cells"},
            {"complexityMatcher", "connected_component_size_type_diversity"},
            {"maxSteps", maxSteps},
            {"stepsRun", stepsRun},
            {"sampleIntervalSteps", sampleIntervalSteps},
            {"samples", samples},
            {"stableWindowSteps", stableWindowSteps},
            {"stableRequiredSamples", stableRequiredSamples},
            {"stableMinComponentSize", stableMinComponentSize},
            {"firstStableComplexityStep", firstStableComplexityStep},
            {"firstStableComplexityTime", firstStableComplexityTime},
            {"maxStableComponentSize", maxStableComponentSize},
            {"organismReproduction", {{"matureMinComponentSize", stableMinComponentSize}, {"matureMinTypeCount", 3}, {"matureMinSpecializedTypeCount", 1}, {"matureComponentObservations", matureComponentObservations}, {"samplesWithMatureComponents", samplesWithMatureComponents}, {"observedMatureLineageSignatures", static_cast<int>(observedMatureLineageSignatures.size())}, {"stableMatureLineageSignatures", stableMatureLineageSignatures}, {"reproducedLineageSignatures", static_cast<int>(reproducedLineageSignatures.size())}, {"maxMatureComponentsInSample", maxMatureComponentsInSample}, {"maxMatureComponentsSameLineage", maxMatureComponentsSameLineage}, {"matureSiblingComponents", matureSiblingComponents}, {"splitReproductionEvents", splitReproductionEvents}}},
            {"maxCells", maxCells},
            {"maxFoods", maxFoods},
            {"radius", radius},
            {"radiation", radiation},
            {"mutationCoef", mutationCoef},
            {"cellTypeMutationWeightScale", cellTypeMutationWeightScale},
            {"foodSpeed", environment.mFoodSpeed},
            {"foodSize", environment.mFoodSize},
            {"foodLumpiness", environment.mFoodLumpiness},
            {"foodLumpSize", environment.mFoodLumpSize},
            {"idleMetabolismScale", environment.mTestIdleMetabolismScale},
            {"flagellocyteCostScale", environment.mTestFlagellocyteCostScale},
            {"phagocyteEatSpeedScale", environment.mTestPhagocyteEatSpeedScale},
            {"seed", seed},
            {"seedCellCount", seedCellCount},
            {"seededCellsAdded", seededCellsAdded},
            {"finalTime", world.mTime},
            {"finalCellCount", world.mCellData.mCellCount},
            {"finalFoodCount", world.mFoodData.mFoodCount},
            {"finalFoodMass", finalFoodMass},
            {"peakCellCount", peakCellCount},
            {"extinctionStep", extinctionStep},
            {"extinctionTime", extinctionTime},
            {"maxComponent", statsJson(maxStats)},
            {"finalBestComponent", statsJson(finalStats)},
            {"complexityAuc", {{"componentSize", componentSizeAuc}, {"typeCount", typeCountAuc}, {"specializedTypeCount", specializedTypeCountAuc}, {"score", complexityScoreAuc}, {"avgComponentSize", componentSizeAuc / std::max(1, stepsRun)}, {"avgTypeCount", typeCountAuc / std::max(1, stepsRun)}, {"avgSpecializedTypeCount", specializedTypeCountAuc / std::max(1, stepsRun)}, {"avgScore", complexityScoreAuc / std::max(1, stepsRun)}}},
            {"foodUtilization", {{"spawnedFoodMass", profile.foodMassSpawned}, {"corpseFoodMass", profile.foodMassSpawnedFromCorpses}, {"consumedFoodMass", profile.foodMassConsumedByCells}, {"decayedFoodMass", profile.foodMassDecayed}, {"boundaryRemovedFoodMass", profile.foodMassRemovedAtBoundary}, {"finalFoodMass", finalFoodMass}, {"consumedPerSpawned", foodUtilizationRatio}, {"remainingPerSpawned", foodRemainingRatio}}},
            {"totalMs", totalMs},
            {"avgStepMs", totalMs / std::max(1, stepsRun)},
            {"stepsPerSecond", stepsRun / (totalMs / 1000.0)}
    };
}
