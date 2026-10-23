#include "PerfOverlap.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <vector>

CellOverlapScanAlgorithm parseOverlapScanAlgorithm(const std::string& name) {
    if (name == "baseline" || name == "baseline-sweep") {
        return CellOverlapScanAlgorithm::BASELINE_SWEEP;
    }
    if (name == "baseline-fast" || name == "baseline-fast-sweep") {
        return CellOverlapScanAlgorithm::BASELINE_FAST_SWEEP;
    }
    if (name == "tight-radius" || name == "tight-radius-sweep") {
        return CellOverlapScanAlgorithm::TIGHT_RADIUS_SWEEP;
    }
    if (name == "row-max" || name == "row-max-radius" || name == "row-max-radius-sweep") {
        return CellOverlapScanAlgorithm::ROW_MAX_RADIUS_SWEEP;
    }
    if (name == "simd-exact" || name == "simd-exact-sweep") {
        return CellOverlapScanAlgorithm::SIMD_EXACT_SWEEP;
    }
    if (name == "scalar-y-prune" || name == "y-prune" || name == "scalar-y-prune-sweep") {
        return CellOverlapScanAlgorithm::SCALAR_Y_PRUNE_SWEEP;
    }
    if (name == "scalar-tile" || name == "tile" || name == "scalar-tile-sweep") {
        return CellOverlapScanAlgorithm::SCALAR_TILE_SWEEP;
    }
    if (name == "simd-tile" || name == "simd-tile-sweep") {
        return CellOverlapScanAlgorithm::SIMD_TILE_SWEEP;
    }
    if (name == "compact-record" || name == "compact-record-sweep") {
        return CellOverlapScanAlgorithm::COMPACT_RECORD_SWEEP;
    }
    if (name == "compact-primary" || name == "compact-primary-sweep") {
        return CellOverlapScanAlgorithm::COMPACT_PRIMARY_SWEEP;
    }
    if (name == "incremental-bucketed-f64-source32"
            || name == "incremental-bucketed-f64-source32-sweep"
            || name == "f64-source32") {
        return CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP;
    }
    throw std::invalid_argument("Unknown overlap scan algorithm: " + name);
}

const char* overlapScanAlgorithmName(CellOverlapScanAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case CellOverlapScanAlgorithm::BASELINE_SWEEP:
            return "baseline-sweep";
        case CellOverlapScanAlgorithm::BASELINE_FAST_SWEEP:
            return "baseline-fast-sweep";
        case CellOverlapScanAlgorithm::TIGHT_RADIUS_SWEEP:
            return "tight-radius-sweep";
        case CellOverlapScanAlgorithm::ROW_MAX_RADIUS_SWEEP:
            return "row-max-radius-sweep";
        case CellOverlapScanAlgorithm::SIMD_EXACT_SWEEP:
            return "simd-exact-sweep";
        case CellOverlapScanAlgorithm::SCALAR_Y_PRUNE_SWEEP:
            return "scalar-y-prune-sweep";
        case CellOverlapScanAlgorithm::SCALAR_TILE_SWEEP:
            return "scalar-tile-sweep";
        case CellOverlapScanAlgorithm::SIMD_TILE_SWEEP:
            return "simd-tile-sweep";
        case CellOverlapScanAlgorithm::COMPACT_RECORD_SWEEP:
            return "compact-record-sweep";
        case CellOverlapScanAlgorithm::COMPACT_PRIMARY_SWEEP:
            return "compact-primary-sweep";
        case CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP:
            return "incremental-bucketed-f64-source32-sweep";
    }
    return "unknown";
}

namespace {

using Clock = std::chrono::steady_clock;

double nanosToMs(long long nanos) {
    return static_cast<double>(nanos) / 1'000'000.0;
}

void warmupSimulation(SimulationWorld& world, int warmupSteps) {
    for (int step = 0; step < warmupSteps; step++) {
        world.update1(false, 1.0);
        world.update2(false, 1.0);
    }
}

struct OverlapCollectResult {
    long long buildNanos = 0;
    long long scanNanos = 0;
    CellSweepBroadphase::Stats stats;
};

struct OverlapPositionTotals {
    long long buildNanos = 0;
    long long scanNanos = 0;
    int sampleCount = 0;
};

struct OverlapAlgorithmTotals {
    long long buildNanos = 0;
    long long scanNanos = 0;
    long long contacts = 0;
    OverlapPositionTotals firstPosition;
    OverlapPositionTotals secondPosition;
    CellSweepBroadphase::Stats stats;
};

struct OverlapCompareTotals {
    std::array<OverlapAlgorithmTotals, 2> algorithms;
    long long mismatchSteps = 0;
    long long orderedMismatchSteps = 0;
    long long missingContacts = 0;
    long long extraContacts = 0;
};

struct OverlapBalancedSample {
    bool algorithmAFirst = true;
    std::array<long long, 2> buildNanos{};
    std::array<long long, 2> scanNanos{};
};

void addStats(CellSweepBroadphase::Stats& totals, const CellSweepBroadphase::Stats& stats) {
    totals.candidatePairs += stats.candidatePairs;
    totals.sameRowCandidatePairs += stats.sameRowCandidatePairs;
    totals.nextRowCandidatePairs += stats.nextRowCandidatePairs;
    totals.sameRowCandidateRanges += stats.sameRowCandidateRanges;
    totals.nextRowCandidateRanges += stats.nextRowCandidateRanges;
    totals.exactPairs += stats.exactPairs;
    totals.scalarChecks += stats.scalarChecks;
    totals.simdChecks += stats.simdChecks;
    totals.rowsReused += stats.rowsReused;
    totals.rowRelayouts += stats.rowRelayouts;
    totals.rowChangedCells += stats.rowChangedCells;
    totals.layoutRebuilds += stats.layoutRebuilds;
    totals.insertionSortRows += stats.insertionSortRows;
    totals.fullSortRows += stats.fullSortRows;
    totals.insertionMoves += stats.insertionMoves;
    totals.compactProjectionNanos += stats.compactProjectionNanos;
    totals.compactClassifyNanos += stats.compactClassifyNanos;
    totals.compactLayoutNanos += stats.compactLayoutNanos;
    totals.compactScatterNanos += stats.compactScatterNanos;
    totals.compactMaterializeNanos += stats.compactMaterializeNanos;
    totals.compactSortNanos += stats.compactSortNanos;
    totals.compactFinalizeNanos += stats.compactFinalizeNanos;
    totals.compactPayloadRefreshes += stats.compactPayloadRefreshes;
    totals.compactScatterIdWrites += stats.compactScatterIdWrites;
    totals.compactRecordMaterializations += stats.compactRecordMaterializations;
    totals.compactRecordSortWrites += stats.compactRecordSortWrites;
    totals.compactFullSortInputRecords += stats.compactFullSortInputRecords;
}

PerfJson statsToJson(const CellSweepBroadphase::Stats& stats, int profileSteps) {
    return {{"candidatePairs", stats.candidatePairs},
            {"avgCandidatePairsPerStep", static_cast<double>(stats.candidatePairs) / profileSteps},
            {"sameRowCandidatePairs", stats.sameRowCandidatePairs},
            {"avgSameRowCandidatePairsPerStep", static_cast<double>(stats.sameRowCandidatePairs) / profileSteps},
            {"nextRowCandidatePairs", stats.nextRowCandidatePairs},
            {"avgNextRowCandidatePairsPerStep", static_cast<double>(stats.nextRowCandidatePairs) / profileSteps},
            {"sameRowCandidateRanges", stats.sameRowCandidateRanges},
            {"avgSameRowCandidateRangesPerStep", static_cast<double>(stats.sameRowCandidateRanges) / profileSteps},
            {"nextRowCandidateRanges", stats.nextRowCandidateRanges},
            {"avgNextRowCandidateRangesPerStep", static_cast<double>(stats.nextRowCandidateRanges) / profileSteps},
            {"exactPairs", stats.exactPairs},
            {"avgExactPairsPerStep", static_cast<double>(stats.exactPairs) / profileSteps},
            {"scalarChecks", stats.scalarChecks},
            {"avgScalarChecksPerStep", static_cast<double>(stats.scalarChecks) / profileSteps},
            {"simdChecks", stats.simdChecks},
            {"avgSimdChecksPerStep", static_cast<double>(stats.simdChecks) / profileSteps},
            {"rowsReused", stats.rowsReused},
            {"avgRowsReusedPerStep", static_cast<double>(stats.rowsReused) / profileSteps},
            {"rowRelayouts", stats.rowRelayouts},
            {"avgRowRelayoutsPerStep", static_cast<double>(stats.rowRelayouts) / profileSteps},
            {"rowChangedCells", stats.rowChangedCells},
            {"avgRowChangedCellsPerStep", static_cast<double>(stats.rowChangedCells) / profileSteps},
            {"layoutRebuilds", stats.layoutRebuilds},
            {"avgLayoutRebuildsPerStep", static_cast<double>(stats.layoutRebuilds) / profileSteps},
            {"insertionSortRows", stats.insertionSortRows},
            {"avgInsertionSortRowsPerStep", static_cast<double>(stats.insertionSortRows) / profileSteps},
            {"fullSortRows", stats.fullSortRows},
            {"avgFullSortRowsPerStep", static_cast<double>(stats.fullSortRows) / profileSteps},
            {"insertionMoves", stats.insertionMoves},
            {"avgInsertionMovesPerStep", static_cast<double>(stats.insertionMoves) / profileSteps},
            {"compactProjectionNanos", stats.compactProjectionNanos},
            {"avgCompactProjectionNanosPerStep",
                    static_cast<double>(stats.compactProjectionNanos) / profileSteps},
            {"compactClassifyNanos", stats.compactClassifyNanos},
            {"avgCompactClassifyNanosPerStep",
                    static_cast<double>(stats.compactClassifyNanos) / profileSteps},
            {"compactLayoutNanos", stats.compactLayoutNanos},
            {"avgCompactLayoutNanosPerStep",
                    static_cast<double>(stats.compactLayoutNanos) / profileSteps},
            {"compactScatterNanos", stats.compactScatterNanos},
            {"avgCompactScatterNanosPerStep",
                    static_cast<double>(stats.compactScatterNanos) / profileSteps},
            {"compactMaterializeNanos", stats.compactMaterializeNanos},
            {"avgCompactMaterializeNanosPerStep",
                    static_cast<double>(stats.compactMaterializeNanos) / profileSteps},
            {"compactSortNanos", stats.compactSortNanos},
            {"avgCompactSortNanosPerStep",
                    static_cast<double>(stats.compactSortNanos) / profileSteps},
            {"compactFinalizeNanos", stats.compactFinalizeNanos},
            {"avgCompactFinalizeNanosPerStep",
                    static_cast<double>(stats.compactFinalizeNanos) / profileSteps},
            {"compactPayloadRefreshes", stats.compactPayloadRefreshes},
            {"avgCompactPayloadRefreshesPerStep",
                    static_cast<double>(stats.compactPayloadRefreshes) / profileSteps},
            {"compactScatterIdWrites", stats.compactScatterIdWrites},
            {"avgCompactScatterIdWritesPerStep",
                    static_cast<double>(stats.compactScatterIdWrites) / profileSteps},
            {"compactRecordMaterializations", stats.compactRecordMaterializations},
            {"avgCompactRecordMaterializationsPerStep",
                    static_cast<double>(stats.compactRecordMaterializations) / profileSteps},
            {"compactRecordSortWrites", stats.compactRecordSortWrites},
            {"avgCompactRecordSortWritesPerStep",
                    static_cast<double>(stats.compactRecordSortWrites) / profileSteps},
            {"compactFullSortInputRecords", stats.compactFullSortInputRecords},
            {"avgCompactFullSortInputRecordsPerStep",
                    static_cast<double>(stats.compactFullSortInputRecords) / profileSteps}};
}

double averageNanos(long long totalNanos, int sampleCount) {
    return static_cast<double>(totalNanos) / std::max(1, sampleCount);
}

PerfJson positionTotalsToJson(const OverlapPositionTotals& totals) {
    return {{"sampleCount", totals.sampleCount},
            {"buildTotalNanos", totals.buildNanos},
            {"buildAvgNanos", averageNanos(totals.buildNanos, totals.sampleCount)},
            {"scanTotalNanos", totals.scanNanos},
            {"scanAvgNanos", averageNanos(totals.scanNanos, totals.sampleCount)},
            {"broadphaseTotalNanos", totals.buildNanos + totals.scanNanos},
            {"broadphaseAvgNanos", averageNanos(totals.buildNanos + totals.scanNanos, totals.sampleCount)}};
}

PerfJson algorithmTotalsToJson(const OverlapAlgorithmTotals& totals, int sampleCount) {
    const double firstBuildAverage = averageNanos(totals.firstPosition.buildNanos, totals.firstPosition.sampleCount);
    const double secondBuildAverage = averageNanos(totals.secondPosition.buildNanos, totals.secondPosition.sampleCount);
    const double firstScanAverage = averageNanos(totals.firstPosition.scanNanos, totals.firstPosition.sampleCount);
    const double secondScanAverage = averageNanos(totals.secondPosition.scanNanos, totals.secondPosition.sampleCount);
    const long long broadphaseNanos = totals.buildNanos + totals.scanNanos;
    return {{"sampleCount", sampleCount},
            {"buildTotalNanos", totals.buildNanos},
            {"buildAvgNanos", averageNanos(totals.buildNanos, sampleCount)},
            {"scanTotalNanos", totals.scanNanos},
            {"scanAvgNanos", averageNanos(totals.scanNanos, sampleCount)},
            {"broadphaseTotalNanos", broadphaseNanos},
            {"broadphaseAvgNanos", averageNanos(broadphaseNanos, sampleCount)},
            {"firstPosition", positionTotalsToJson(totals.firstPosition)},
            {"secondPosition", positionTotalsToJson(totals.secondPosition)},
            {"firstToSecondBuildRatio", firstBuildAverage / std::max(1.0, secondBuildAverage)},
            {"firstToSecondScanRatio", firstScanAverage / std::max(1.0, secondScanAverage)},
            {"firstToSecondBroadphaseRatio",
                    (firstBuildAverage + firstScanAverage) / std::max(1.0, secondBuildAverage + secondScanAverage)}};
}

void addCollectResult(OverlapAlgorithmTotals& totals, const OverlapCollectResult& result, std::size_t contactCount,
        bool firstPosition) {
    totals.buildNanos += result.buildNanos;
    totals.scanNanos += result.scanNanos;
    totals.contacts += static_cast<long long>(contactCount);
    OverlapPositionTotals& position = firstPosition ? totals.firstPosition : totals.secondPosition;
    position.buildNanos += result.buildNanos;
    position.scanNanos += result.scanNanos;
    position.sampleCount++;
    addStats(totals.stats, result.stats);
}

std::uint64_t canonicalPairKey(int firstCellIndex, int secondCellIndex) {
    const std::uint32_t a = static_cast<std::uint32_t>(std::min(firstCellIndex, secondCellIndex));
    const std::uint32_t b = static_cast<std::uint32_t>(std::max(firstCellIndex, secondCellIndex));
    return (static_cast<std::uint64_t>(a) << 32U) | b;
}

bool sameOrderedOverlapPairs(const ExactOverlapPairBuffer& baseline, const ExactOverlapPairBuffer& candidate) {
    return baseline.firstCellIds == candidate.firstCellIds && baseline.secondCellIds == candidate.secondCellIds && baseline.distanceX == candidate.distanceX && baseline.distanceY == candidate.distanceY;
}

void fillSortedPairKeys(const ExactOverlapPairBuffer& buffer, std::vector<std::uint64_t>& keys) {
    const std::size_t pairCount = buffer.size();
    keys.resize(pairCount);
    for (std::size_t pairIndex = 0; pairIndex < pairCount; pairIndex++) {
        keys[pairIndex] = canonicalPairKey(buffer.firstCellIds[pairIndex], buffer.secondCellIds[pairIndex]);
    }
    std::sort(keys.begin(), keys.end());
}

void countPairDiffs(const std::vector<std::uint64_t>& baselineKeys, const std::vector<std::uint64_t>& candidateKeys,
        long long& missingContacts, long long& extraContacts) {
    std::size_t baselineIndex = 0;
    std::size_t candidateIndex = 0;
    while (baselineIndex < baselineKeys.size() && candidateIndex < candidateKeys.size()) {
        const std::uint64_t baselineKey = baselineKeys[baselineIndex];
        const std::uint64_t candidateKey = candidateKeys[candidateIndex];
        if (baselineKey == candidateKey) {
            baselineIndex++;
            candidateIndex++;
        } else if (baselineKey < candidateKey) {
            missingContacts++;
            baselineIndex++;
        } else {
            extraContacts++;
            candidateIndex++;
        }
    }
    missingContacts += static_cast<long long>(baselineKeys.size() - baselineIndex);
    extraContacts += static_cast<long long>(candidateKeys.size() - candidateIndex);
}

#if defined(__clang__) || defined(__GNUC__)
#define CELLULARITY_PERF_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define CELLULARITY_PERF_NOINLINE __declspec(noinline)
#else
#define CELLULARITY_PERF_NOINLINE
#endif

CELLULARITY_PERF_NOINLINE
OverlapCollectResult collectOverlapPairs(CellSweepBroadphase& broadphase, const CellData& cellData,
        ExactOverlapPairBuffer& output) {
    output.clear();
    const auto buildStart = Clock::now();
    broadphase.buildSelectedLayout(cellData, true);
    const auto scanStart = Clock::now();
    if (broadphase.scanAlgorithm() == CellOverlapScanAlgorithm::BASELINE_FAST_SWEEP || broadphase.scanAlgorithm() == CellOverlapScanAlgorithm::SIMD_EXACT_SWEEP || broadphase.scanAlgorithm() == CellOverlapScanAlgorithm::SCALAR_Y_PRUNE_SWEEP || broadphase.scanAlgorithm() == CellOverlapScanAlgorithm::SCALAR_TILE_SWEEP || broadphase.scanAlgorithm() == CellOverlapScanAlgorithm::SIMD_TILE_SWEEP) {
        broadphase.collectOverlappingPairs(output, true);
    } else {
        broadphase.forEachOverlappingPairInline(
                [&output](int firstCellIndex, int secondCellIndex, double distanceX, double distanceY, double, double,
                        double) {
                    output.push(firstCellIndex, secondCellIndex, distanceX, distanceY);
                },
                true);
    }
    const auto scanEnd = Clock::now();

    OverlapCollectResult result;
    result.buildNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(scanStart - buildStart).count();
    result.scanNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(scanEnd - scanStart).count();
    result.stats = broadphase.stats();
    return result;
}

#undef CELLULARITY_PERF_NOINLINE

void validateBalancedSampleCount(int sampleCount, const char* label) {
    if (sampleCount <= 0 || (sampleCount % 4) != 0) {
        throw std::invalid_argument(std::string(label) + " must be a positive multiple of four");
    }
}

bool algorithmAFirstForSample(int sampleIndex) noexcept {
    const int abbaPosition = sampleIndex & 3;
    return abbaPosition == 0 || abbaPosition == 3;
}

void warmupBroadphasePair(std::array<CellSweepBroadphase*, 2>& broadphases, const CellData& cellData,
        std::array<ExactOverlapPairBuffer*, 2>& pairBuffers) {
    std::array<OverlapCollectResult, 2> ignored;
    for (int warmupPass = 0; warmupPass < 4; warmupPass++) {
        const int firstAlgorithm = algorithmAFirstForSample(warmupPass) ? 0 : 1;
        for (int position = 0; position < 2; position++) {
            const int algorithmIndex = position == 0 ? firstAlgorithm : 1 - firstAlgorithm;
            ignored[algorithmIndex] =
                    collectOverlapPairs(*broadphases[algorithmIndex], cellData, *pairBuffers[algorithmIndex]);
        }
    }
}

PerfJson samplesToJson(const std::vector<OverlapBalancedSample>& samples) {
    PerfJson output = PerfJson::array();
    for (std::size_t sampleIndex = 0; sampleIndex < samples.size(); sampleIndex++) {
        const OverlapBalancedSample& sample = samples[sampleIndex];
        output.push_back({{"sampleIndex", sampleIndex},
                {"order", sample.algorithmAFirst ? "AB" : "BA"},
                {"algorithmA",
                        {{"position", sample.algorithmAFirst ? "first" : "second"},
                                {"buildNanos", sample.buildNanos[0]},
                                {"scanNanos", sample.scanNanos[0]},
                                {"broadphaseNanos", sample.buildNanos[0] + sample.scanNanos[0]}}},
                {"algorithmB",
                        {{"position", sample.algorithmAFirst ? "second" : "first"},
                                {"buildNanos", sample.buildNanos[1]},
                                {"scanNanos", sample.scanNanos[1]},
                                {"broadphaseNanos", sample.buildNanos[1] + sample.scanNanos[1]}}}});
    }
    return output;
}

PerfJson runBalancedOverlapComparison(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int sampleCount, const std::string& algorithmAName,
        const std::string& algorithmBName, bool advanceSimulation, const char* scenario,
        const char* sampleCountField, const char* mismatchCountField,
        const char* orderedMismatchCountField) {
    validateBalancedSampleCount(sampleCount, sampleCountField);
    if (warmupSteps < 0) {
        throw std::invalid_argument("warmupSteps must not be negative");
    }

    warmupSimulation(world, warmupSteps);

    const CellOverlapScanAlgorithm algorithmA = parseOverlapScanAlgorithm(algorithmAName);
    const CellOverlapScanAlgorithm algorithmB = parseOverlapScanAlgorithm(algorithmBName);
    CellSweepBroadphase broadphaseA(2.0 * CellData::MAX_CELL_RADIUS);
    CellSweepBroadphase broadphaseB(2.0 * CellData::MAX_CELL_RADIUS);
    broadphaseA.setScanAlgorithm(algorithmA);
    broadphaseB.setScanAlgorithm(algorithmB);

    ExactOverlapPairBuffer pairsA;
    ExactOverlapPairBuffer pairsB;
    pairsA.reserve(static_cast<std::size_t>(world.mCellData.mCellCount) * 4U);
    pairsB.reserve(static_cast<std::size_t>(world.mCellData.mCellCount) * 4U);
    std::array<CellSweepBroadphase*, 2> broadphases = {&broadphaseA, &broadphaseB};
    std::array<ExactOverlapPairBuffer*, 2> pairBuffers = {&pairsA, &pairsB};
    warmupBroadphasePair(broadphases, world.mCellData, pairBuffers);

    std::vector<std::uint64_t> keysA;
    std::vector<std::uint64_t> keysB;
    std::vector<OverlapBalancedSample> samples(static_cast<std::size_t>(sampleCount));
    OverlapCompareTotals totals;

    const auto startedAt = Clock::now();
    for (int sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++) {
        if (advanceSimulation) {
            world.update1(false, 1.0);
            world.update2(false, 1.0);
        }

        const int firstAlgorithm = algorithmAFirstForSample(sampleIndex) ? 0 : 1;
        std::array<OverlapCollectResult, 2> results;
#if defined(__clang__)
#pragma clang loop unroll(disable)
#elif defined(__GNUC__)
#pragma GCC unroll 1
#endif
        for (int position = 0; position < 2; position++) {
            const int algorithmIndex = position == 0 ? firstAlgorithm : 1 - firstAlgorithm;
            results[algorithmIndex] =
                    collectOverlapPairs(*broadphases[algorithmIndex], world.mCellData, *pairBuffers[algorithmIndex]);
            addCollectResult(totals.algorithms[algorithmIndex], results[algorithmIndex],
                    pairBuffers[algorithmIndex]->size(), position == 0);
        }

        OverlapBalancedSample& sample = samples[static_cast<std::size_t>(sampleIndex)];
        sample.algorithmAFirst = firstAlgorithm == 0;
        sample.buildNanos[0] = results[0].buildNanos;
        sample.buildNanos[1] = results[1].buildNanos;
        sample.scanNanos[0] = results[0].scanNanos;
        sample.scanNanos[1] = results[1].scanNanos;

        if (!sameOrderedOverlapPairs(pairsA, pairsB)) {
            totals.orderedMismatchSteps++;
            fillSortedPairKeys(pairsA, keysA);
            fillSortedPairKeys(pairsB, keysB);
            if (keysA != keysB) {
                totals.mismatchSteps++;
                countPairDiffs(keysA, keysB, totals.missingContacts, totals.extraContacts);
            }
        }
    }
    const auto totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt);

    const OverlapAlgorithmTotals& totalsA = totals.algorithms[0];
    const OverlapAlgorithmTotals& totalsB = totals.algorithms[1];
    const long long broadphaseNanosA = totalsA.buildNanos + totalsA.scanNanos;
    const long long broadphaseNanosB = totalsB.buildNanos + totalsB.scanNanos;
    const char* canonicalA = overlapScanAlgorithmName(algorithmA);
    const char* canonicalB = overlapScanAlgorithmName(algorithmB);

    PerfJson report = {{"schemaVersion", 2},
            {"scenario", scenario},
            {"inputPath", inputPath},
            {"substrateFormatVersion", substrateFormatVersion},
            {"warmupSteps", warmupSteps},
            {sampleCountField, sampleCount},
            {"cells", world.mCellData.mCellCount},
            {"food", world.mFoodData.mFoodCount},
            {"time", world.mTime},
            {"algorithmA", canonicalA},
            {"algorithmB", canonicalB},
            {"sameContacts", totals.mismatchSteps == 0},
            {"sameOrderedContacts", totals.orderedMismatchSteps == 0},
            {mismatchCountField, totals.mismatchSteps},
            {orderedMismatchCountField, totals.orderedMismatchSteps},
            {"missingContacts", totals.missingContacts},
            {"extraContacts", totals.extraContacts},
            {"algorithmAContacts", totalsA.contacts},
            {"algorithmBContacts", totalsB.contacts},
            {"avgAlgorithmAContactsPerSample", static_cast<double>(totalsA.contacts) / sampleCount},
            {"avgAlgorithmBContactsPerSample", static_cast<double>(totalsB.contacts) / sampleCount},
            {"totalMs", nanosToMs(totalNanos.count())},
            {"algorithmATimings", algorithmTotalsToJson(totalsA, sampleCount)},
            {"algorithmBTimings", algorithmTotalsToJson(totalsB, sampleCount)},
            {"algorithmBScanSpeedup",
                    static_cast<double>(totalsA.scanNanos) / std::max<long long>(1, totalsB.scanNanos)},
            {"algorithmBBroadphaseSpeedup",
                    static_cast<double>(broadphaseNanosA) / std::max<long long>(1, broadphaseNanosB)},
            {"algorithmAStats", statsToJson(totalsA.stats, sampleCount)},
            {"algorithmBStats", statsToJson(totalsB.stats, sampleCount)},
            {"methodology",
                    {{"name", "alternating-abba-single-call-site"},
                            {"balanced", true},
                            {"algorithmAFirstSamples", sampleCount / 2},
                            {"algorithmBFirstSamples", sampleCount / 2},
                            {"broadphaseWarmupPassesPerAlgorithm", 4},
                            {"correctnessChecksInsideAlgorithmTimers", false},
                            {"rawSamplesIncluded", true}}},
            {"rawSamples", samplesToJson(samples)}};

    // Keep the original field names for existing local report readers. They
    // now contain balanced aggregates instead of fixed-position measurements.
    report["baselineAlgorithm"] = canonicalA;
    report["candidateAlgorithm"] = canonicalB;
    report["baselineContacts"] = totalsA.contacts;
    report["candidateContacts"] = totalsB.contacts;
    report["avgBaselineContactsPerStep"] = static_cast<double>(totalsA.contacts) / sampleCount;
    report["avgCandidateContactsPerStep"] = static_cast<double>(totalsB.contacts) / sampleCount;
    report["baselineBuildTotalMs"] = nanosToMs(totalsA.buildNanos);
    report["baselineBuildAvgMs"] = nanosToMs(totalsA.buildNanos) / sampleCount;
    report["baselineScanTotalMs"] = nanosToMs(totalsA.scanNanos);
    report["baselineScanAvgMs"] = nanosToMs(totalsA.scanNanos) / sampleCount;
    report["candidateBuildTotalMs"] = nanosToMs(totalsB.buildNanos);
    report["candidateBuildAvgMs"] = nanosToMs(totalsB.buildNanos) / sampleCount;
    report["candidateScanTotalMs"] = nanosToMs(totalsB.scanNanos);
    report["candidateScanAvgMs"] = nanosToMs(totalsB.scanNanos) / sampleCount;
    report["candidateScanSpeedup"] = report["algorithmBScanSpeedup"];
    report["candidateTotalBroadphaseSpeedup"] = report["algorithmBBroadphaseSpeedup"];
    report["baselineStats"] = report["algorithmAStats"];
    report["candidateStats"] = report["algorithmBStats"];
    return report;
}

} // namespace

PerfJson runOverlapDetailProfileJson(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int profileSteps) {
    warmupSimulation(world, warmupSteps);

    SimulationWorldProfile profile;
    profile.clear();
    world.setProfile(&profile);

    auto startedAt = Clock::now();
    for (int step = 0; step < profileSteps; step++) {
        world.update1(false, 1.0);
        world.update2(false, 1.0);
    }
    world.setProfile(nullptr);

    const auto totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt);
    const long long overlapNanos = profile.stageNanos[SimulationWorldProfile::OVERLAPS];
    const long long attributedNanos = profile.overlapSetupNanos + profile.overlapBroadphaseBuildNanos + profile.overlapBroadphaseScanNanos + profile.overlapPairEffectsNanos + profile.overlapDeathCleanupNanos;
    const long long residualNanos = overlapNanos - attributedNanos;
    const long long residualBudgetNanos =
            std::max<long long>(static_cast<long long>(profileSteps) * 3'000LL, overlapNanos / 10LL);
    const long long compactBuildDetailNanos = profile.overlapCompactProjectionNanos + profile.overlapCompactClassifyNanos + profile.overlapCompactLayoutNanos + profile.overlapCompactScatterNanos + profile.overlapCompactMaterializeNanos + profile.overlapCompactSortNanos + profile.overlapCompactFinalizeNanos;

    auto phase = [profileSteps, overlapNanos](long long nanos) {
        return PerfJson{
                {"totalNanos", nanos},
                {"totalMs", nanosToMs(nanos)},
                {"avgMs", nanosToMs(nanos) / profileSteps},
                {"shareOfOverlapPercent", static_cast<double>(nanos) * 100.0 / std::max<long long>(1, overlapNanos)}
        };
    };

    return {{"schemaVersion", 3},
            {"scenario", "collision/overlap_detail"},
            {"inputPath", inputPath},
            {"substrateFormatVersion", substrateFormatVersion},
            {"overlapBroadphaseAlgorithm", overlapScanAlgorithmName(world.cellOverlapScanAlgorithm())},
            {"warmupSteps", warmupSteps},
            {"profileSteps", profileSteps},
            {"cells", world.mCellData.mCellCount},
            {"food", world.mFoodData.mFoodCount},
            {"time", world.mTime},
            {"totalMs", nanosToMs(totalNanos.count())},
            {"timerGranularity", "coarse-region"},
            {"innerPairTimers", false},
            {"overlap",
                    {{"parent", phase(overlapNanos)},
                            {"phases",
                                    {{"setup", phase(profile.overlapSetupNanos)},
                                            {"broadphaseBuild", phase(profile.overlapBroadphaseBuildNanos)},
                                            {"scanSemanticResolutionAndEndpointAdmission", phase(profile.overlapBroadphaseScanNanos)},
                                            {"materializedPairEffects", phase(profile.overlapPairEffectsNanos)},
                                            {"deathCleanup", phase(profile.overlapDeathCleanupNanos)}}},
                            {"accountedPercent", static_cast<double>(attributedNanos) * 100.0 / std::max<long long>(1, overlapNanos)},
                            {"residualNanos", residualNanos},
                            {"residualMs", nanosToMs(residualNanos)},
                            {"residualBudgetNanos", residualBudgetNanos},
                            {"withinAcceptance", std::abs(residualNanos) <= residualBudgetNanos}}},
            {"broadphaseBuildDetail",
                    {{"parentNanos", profile.overlapBroadphaseBuildNanos},
                            {"compactProjectionNanos", profile.overlapCompactProjectionNanos},
                            {"compactClassifyNanos", profile.overlapCompactClassifyNanos},
                            {"compactLayoutNanos", profile.overlapCompactLayoutNanos},
                            {"compactScatterNanos", profile.overlapCompactScatterNanos},
                            {"compactMaterializeNanos", profile.overlapCompactMaterializeNanos},
                            {"compactSortNanos", profile.overlapCompactSortNanos},
                            {"compactFinalizeNanos", profile.overlapCompactFinalizeNanos},
                            {"residualNanos", profile.overlapBroadphaseBuildNanos - compactBuildDetailNanos}}},
            {"pipelineSemantics",
                    {{"scanPhaseIsFused", true},
                            {"scanPhaseIncludes",
                                    {"candidate-range-scan", "exact-overlap-test", "semantic-resolution", "endpoint-admission"}},
                            {"pairStreamMode",
                                    profile.overlapPairBufferWrites == 0 ? "fused-no-materialized-stream" : "materialized"},
                            {"rawBudgetAvailable", false}}},
            {"counters",
                    {{"targetCells", profile.targetCells},
                            {"semanticPairs", profile.overlapSemanticPairs},
                            {"candidatePairs", profile.overlapCandidatePairs},
                            {"exactPairs", profile.overlapExactPairs},
                            {"endpointAdmissionAttempts", profile.overlapEndpointAdmissionAttempts},
                            {"directedAdmissions", profile.collisionWrites},
                            {"endpointCapacitySkips", profile.collisionWriteCapacitySkips},
                            {"pairStreamWrites", profile.overlapPairBufferWrites},
                            {"rawBudgetOverflows", profile.overlapRawBudgetOverflows},
                            {"fertilizationTargets", profile.fertilizationTargets},
                            {"overlapDeathIntents", profile.overlapDeathIntents},
                            {"deathsApplied", profile.deathsApplied},
                            {"cleanupCollisionReads", profile.cleanupCollisionReads},
                            {"cleanupCollisionWrites", profile.cleanupCollisionWrites},
                            {"sameRowCandidatePairs", profile.overlapSameRowCandidatePairs},
                            {"nextRowCandidatePairs", profile.overlapNextRowCandidatePairs},
                            {"scalarChecks", profile.overlapScalarChecks},
                            {"simdChecks", profile.overlapSimdChecks},
                            {"rowsReused", profile.overlapRowsReused},
                            {"rowsRelayouts", profile.overlapRowsRelayouts},
                            {"layoutRebuilds", profile.overlapLayoutRebuilds},
                            {"insertionSortRows", profile.overlapInsertionSortRows},
                            {"fullSortRows", profile.overlapFullSortRows},
                            {"insertionMoves", profile.overlapInsertionMoves}}}};
}

PerfJson runOverlapAlgorithmCompareJson(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int profileSteps, const std::string& candidateAlgorithm) {
    return runBalancedOverlapAlgorithmCompareJson(world, inputPath, substrateFormatVersion, warmupSteps, profileSteps,
            "baseline", candidateAlgorithm);
}

PerfJson runBalancedOverlapAlgorithmCompareJson(SimulationWorld& world, const std::string& inputPath,
        int substrateFormatVersion, int warmupSteps, int profileSteps,
        const std::string& algorithmA, const std::string& algorithmB) {
    return runBalancedOverlapComparison(world, inputPath, substrateFormatVersion, warmupSteps, profileSteps, algorithmA,
            algorithmB, true, "collision/overlap_algorithm_compare", "profileSteps",
            "mismatchSteps", "orderedMismatchSteps");
}

PerfJson runOverlapScanRepeatJson(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int repeatCount, const std::string& candidateAlgorithm) {
    return runBalancedOverlapScanRepeatJson(world, inputPath, substrateFormatVersion, warmupSteps, repeatCount,
            "baseline", candidateAlgorithm);
}

PerfJson runBalancedOverlapScanRepeatJson(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int repeatCount, const std::string& algorithmA,
        const std::string& algorithmB) {
    PerfJson report = runBalancedOverlapComparison(world, inputPath, substrateFormatVersion, warmupSteps, repeatCount,
            algorithmA, algorithmB, false, "collision/overlap_scan_repeat",
            "repeatCount", "mismatchRepeats", "orderedMismatchRepeats");
    report["avgBaselineContactsPerRepeat"] = report["avgAlgorithmAContactsPerSample"];
    report["avgCandidateContactsPerRepeat"] = report["avgAlgorithmBContactsPerSample"];
    return report;
}
