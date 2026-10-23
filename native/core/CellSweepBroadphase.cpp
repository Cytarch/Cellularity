#include "core/CellSweepBroadphase.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#ifdef CELLULARITY_PROFILE
#include <chrono>
#endif

#include "core/CellData.h"
#include "hwy/highway.h"

namespace hn = hwy::HWY_NAMESPACE;

namespace {

[[nodiscard]] int log2CeilInt(int value) noexcept {
    if (value <= 1) {
        return 0;
    }
    int result = 0;
    value--;
    while (value > 0) {
        value >>= 1;
        result++;
    }
    return result;
}

[[nodiscard]] int hybridFullSortThreshold(int rowSize) {
    static const int fixedThreshold = -1;
    if (fixedThreshold >= 0) {
        return fixedThreshold;
    }
    return log2CeilInt(rowSize);
}

void sortRowByInsertion(std::vector<int>& cellIds,
        std::vector<double>& x,
        std::vector<double>& y,
        std::vector<double>& radius,
        int rowStart,
        int rowEnd,
        CellSweepBroadphase::Stats& stats,
        bool collectStats) {
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats) && rowEnd - rowStart > 1) {
        stats.insertionSortRows++;
    }
    for (int slot = rowStart + 1; slot < rowEnd; slot++) {
        const double keyX = x[slot];
        const double keyY = y[slot];
        const double keyRadius = radius[slot];
        const int cellIndex = cellIds[slot];
        int write = slot;
        while (write > rowStart && keyX < x[write - 1]) {
            cellIds[write] = cellIds[write - 1];
            x[write] = x[write - 1];
            y[write] = y[write - 1];
            radius[write] = radius[write - 1];
            write--;
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                stats.insertionMoves++;
            }
        }
        cellIds[write] = cellIndex;
        x[write] = keyX;
        y[write] = keyY;
        radius[write] = keyRadius;
    }
}

void sortRowByStdSort(const CellData& cellData,
        std::vector<int>& cellIds,
        std::vector<double>& x,
        std::vector<double>& y,
        std::vector<double>& radius,
        int rowStart,
        int rowEnd,
        CellSweepBroadphase::Stats& stats,
        bool collectStats) {
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats) && rowEnd - rowStart > 1) {
        stats.fullSortRows++;
    }
    auto comparator = [&](int left, int right) {
        if (cellData.mPositionX[left] != cellData.mPositionX[right]) {
            return cellData.mPositionX[left] < cellData.mPositionX[right];
        }
        return left < right;
    };
    std::sort(cellIds.begin() + rowStart, cellIds.begin() + rowEnd, comparator);
    for (int slot = rowStart; slot < rowEnd; slot++) {
        const int cellIndex = cellIds[slot];
        x[slot] = cellData.mPositionX[cellIndex];
        y[slot] = cellData.mPositionY[cellIndex];
        radius[slot] = cellData.mRadius[cellIndex];
    }
}

void sortCompactRecordsByInsertion(
        std::vector<CellSweepBroadphase::CompactSpatialRecord>& records,
        int rowStart,
        int rowEnd,
        CellSweepBroadphase::Stats& stats,
        bool collectStats) {
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats) && rowEnd - rowStart > 1) {
        stats.insertionSortRows++;
    }
    for (int slot = rowStart + 1; slot < rowEnd; slot++) {
        const CellSweepBroadphase::CompactSpatialRecord key = records[slot];
        int write = slot;
        while (write > rowStart && key.x < records[write - 1].x) {
            records[write] = records[write - 1];
            write--;
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                stats.insertionMoves++;
                stats.compactRecordSortWrites++;
            }
        }
        if (write != slot) {
            records[write] = key;
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                stats.compactRecordSortWrites++;
            }
        }
    }
}

void sortCompactRecordsByFullSort(
        std::vector<CellSweepBroadphase::CompactSpatialRecord>& records,
        int rowStart,
        int rowEnd,
        CellSweepBroadphase::Stats& stats,
        bool collectStats) {
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats) && rowEnd - rowStart > 1) {
        stats.fullSortRows++;
    }
    if (rowEnd - rowStart <= 1) {
        return;
    }
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        stats.compactFullSortInputRecords += rowEnd - rowStart;
    }
    std::sort(records.begin() + rowStart,
            records.begin() + rowEnd,
            [](const CellSweepBroadphase::CompactSpatialRecord& left,
                    const CellSweepBroadphase::CompactSpatialRecord& right) {
                if (left.x != right.x) {
                    return left.x < right.x;
                }
                return left.cellId < right.cellId;
            });
}

} // namespace

void ExactOverlapPairBuffer::clear() {
    firstCellIds.clear();
    secondCellIds.clear();
    distanceX.clear();
    distanceY.clear();
}

void ExactOverlapPairBuffer::reserve(std::size_t capacity) {
    firstCellIds.reserve(capacity);
    secondCellIds.reserve(capacity);
    distanceX.reserve(capacity);
    distanceY.reserve(capacity);
}

void ExactOverlapPairBuffer::resize(std::size_t size) {
    firstCellIds.resize(size);
    secondCellIds.resize(size);
    distanceX.resize(size);
    distanceY.resize(size);
}

std::size_t ExactOverlapPairBuffer::size() const noexcept {
    return firstCellIds.size();
}

void ExactOverlapPairBuffer::push(int firstCellIndex,
        int secondCellIndex,
        double pairDistanceX,
        double pairDistanceY) {
    firstCellIds.push_back(firstCellIndex);
    secondCellIds.push_back(secondCellIndex);
    distanceX.push_back(pairDistanceX);
    distanceY.push_back(pairDistanceY);
}

void CellSweepBroadphase::Stats::clear() {
    candidatePairs = 0;
    sameRowCandidatePairs = 0;
    nextRowCandidatePairs = 0;
    sameRowCandidateRanges = 0;
    nextRowCandidateRanges = 0;
    exactPairs = 0;
    scalarChecks = 0;
    simdChecks = 0;
    rowsReused = 0;
    rowRelayouts = 0;
    rowChangedCells = 0;
    layoutRebuilds = 0;
    insertionSortRows = 0;
    fullSortRows = 0;
    insertionMoves = 0;
    compactProjectionNanos = 0;
    compactClassifyNanos = 0;
    compactLayoutNanos = 0;
    compactScatterNanos = 0;
    compactMaterializeNanos = 0;
    compactSortNanos = 0;
    compactFinalizeNanos = 0;
    compactPayloadRefreshes = 0;
    compactScatterIdWrites = 0;
    compactRecordMaterializations = 0;
    compactRecordSortWrites = 0;
    compactFullSortInputRecords = 0;
}

CellSweepBroadphase::CellSweepBroadphase(double maxDiameter)
        : mMaxDiameter(maxDiameter),
          mInvMaxDiameter(1.0 / maxDiameter),
          mMaxRadius(0.5 * maxDiameter),
          mIncrementalBucketedF64Broadphase(maxDiameter) {
    if (!(maxDiameter > 0.0)) {
        throw std::invalid_argument("CellSweepBroadphase requires positive maxDiameter");
    }
}

void CellSweepBroadphase::setMaxDiameter(double maxDiameter) {
    if (!(maxDiameter > 0.0)) {
        throw std::invalid_argument("CellSweepBroadphase requires positive maxDiameter");
    }
    mMaxDiameter = maxDiameter;
    mInvMaxDiameter = 1.0 / maxDiameter;
    mMaxRadius = 0.5 * maxDiameter;
    mIncrementalBucketedF64Broadphase.setMaxDiameter(maxDiameter);
    clear();
}

void CellSweepBroadphase::setScanAlgorithm(CellOverlapScanAlgorithm algorithm) noexcept {
    if (mScanAlgorithm != algorithm) {
        clear();
        mScanAlgorithm = algorithm;
    }
}

CellOverlapScanAlgorithm CellSweepBroadphase::scanAlgorithm() const noexcept {
    return mScanAlgorithm;
}

bool CellSweepBroadphase::compactRecordsReady() const noexcept {
    const bool usesCompactRecords =
            mScanAlgorithm == CellOverlapScanAlgorithm::COMPACT_RECORD_SWEEP
            || mScanAlgorithm == CellOverlapScanAlgorithm::COMPACT_PRIMARY_SWEEP;
    return usesCompactRecords
            && mCompactCellCount >= 0
            && mCompactRecords.size() == static_cast<std::size_t>(mCompactCellCount) + 1U;
}

void CellSweepBroadphase::writeCompactSentinel(int cellCount) noexcept {
    mCompactRecords[cellCount] = {
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            0.0,
            -1,
            std::numeric_limits<std::int32_t>::max()
    };
    mCompactCellCount = cellCount;
}

void CellSweepBroadphase::refreshProjectedCompactRecords(bool collectStats) {
    if (mScanAlgorithm != CellOverlapScanAlgorithm::COMPACT_RECORD_SWEEP) {
        return;
    }

#ifdef CELLULARITY_PROFILE
    std::chrono::steady_clock::time_point projectionStart;
    if (collectStats) {
        projectionStart = std::chrono::steady_clock::now();
    }
#else
    (void)collectStats;
#endif

    const int cellCount = static_cast<int>(mOrderedCellIds.size());
    mCompactRecords.resize(static_cast<std::size_t>(cellCount) + 1U);
    for (int slot = 0; slot < cellCount; slot++) {
        const int cellId = mOrderedCellIds[slot];
        mCompactRecords[slot] = {
                mOrderedX[slot],
                mOrderedY[slot],
                mOrderedRadius[slot],
                cellId,
                mCellRowsById[cellId]
        };
    }
    writeCompactSentinel(cellCount);

#ifdef CELLULARITY_PROFILE
    if (collectStats) {
        mStats.compactProjectionNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - projectionStart)
                                                 .count();
    }
#endif
}

void CellSweepBroadphase::clear() {
    mCellRowsById.clear();
    mCellCountsByRow.clear();
    mRowStartOffsets.clear();
    mRowWriteOffsets.clear();
    mChangedCellCountsByRow.clear();
    mOrderedCellIds.clear();
    mRelayoutCellIds.clear();
    mCompactRelayoutCellIds.clear();
    mCompactRecords.clear();
    mOrderedX.clear();
    mOrderedY.clear();
    mOrderedRadius.clear();
    mSuffixMaxRadius.clear();
    mMaxRadiusByRow.clear();
    mMinRow = 0;
    mUsesSuffixRadiusWindow = false;
    mUsesRowMaxRadiusWindow = false;
    mCompactPreviousBuildRelayout = false;
    mCompactCellCount = 0;
    mIncrementalBucketedF64Broadphase.clear();
    mStats.clear();
}

void CellSweepBroadphase::syncIncrementalBucketedF64Stats(bool collectStats) noexcept {
    if (!CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        return;
    }
    const CellIncrementalBucketedF64Broadphase::Stats& source =
            mIncrementalBucketedF64Broadphase.stats();
    mStats.candidatePairs = source.candidatePairs;
    mStats.sameRowCandidatePairs = source.sameRowCandidatePairs;
    mStats.nextRowCandidatePairs = source.nextRowCandidatePairs;
    mStats.sameRowCandidateRanges = source.sameRowCandidateRanges;
    mStats.nextRowCandidateRanges = source.nextRowCandidateRanges;
    mStats.exactPairs = source.exactPairs;
    mStats.scalarChecks = source.candidatePairs;
    mStats.rowChangedCells = source.transferredRecords;
    mStats.layoutRebuilds = source.fullRebuilds;
}

CellIncrementalBucketedF64Broadphase::PackedSource32*
CellSweepBroadphase::beginIncrementalPackedSourceWrite(std::size_t cellCount) {
    return mIncrementalBucketedF64Broadphase.beginPreparedSourceWrite(cellCount);
}

void CellSweepBroadphase::commitIncrementalPackedSourceWrite() noexcept {
    mIncrementalBucketedF64Broadphase.commitPreparedSourceWrite();
}

void CellSweepBroadphase::refreshRadiusWindowMetadata() {
    if (!mUsesSuffixRadiusWindow && !mUsesRowMaxRadiusWindow) {
        mSuffixMaxRadius.clear();
        mMaxRadiusByRow.clear();
        return;
    }

    const int cellCount = static_cast<int>(mOrderedRadius.size());
    const int rowSpan = static_cast<int>(mCellCountsByRow.size());
    mMaxRadiusByRow.assign(static_cast<std::size_t>(rowSpan), 0.0);
    if (mUsesSuffixRadiusWindow) {
        mSuffixMaxRadius.resize(cellCount);
    } else {
        mSuffixMaxRadius.clear();
    }
    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        double maxRadius = 0.0;
        for (int slot = rowEnd - 1; slot >= rowStart; slot--) {
            maxRadius = std::max(maxRadius, mOrderedRadius[slot]);
            if (mUsesSuffixRadiusWindow) {
                mSuffixMaxRadius[slot] = maxRadius;
            }
        }
        mMaxRadiusByRow[static_cast<std::size_t>(rowIndex)] = maxRadius;
    }
}

void CellSweepBroadphase::buildCompactPrimary(const CellData& cellData, bool collectStats) {
#ifdef CELLULARITY_PROFILE
    using ProfileClock = std::chrono::steady_clock;
    ProfileClock::time_point phaseStart;
    if (collectStats) {
        phaseStart = ProfileClock::now();
    }
    auto finishProfilePhase = [&](long long& phaseNanos) {
        if (!collectStats) {
            return;
        }
        const ProfileClock::time_point phaseEnd = ProfileClock::now();
        phaseNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                phaseEnd - phaseStart)
                              .count();
        phaseStart = phaseEnd;
    };
#endif

    mUsesSuffixRadiusWindow = false;
    mUsesRowMaxRadiusWindow = false;

    const int cellCount = cellData.mCellCount;
    if (cellCount == 0) {
        clear();
        return;
    }

    const int previousCount = mCompactCellCount;
    const bool hasReusableLayout =
            previousCount > 0
            && mCompactRecords.size() == static_cast<std::size_t>(previousCount) + 1U
            && !mCellCountsByRow.empty()
            && mRowStartOffsets.size() == mCellCountsByRow.size() + 1U;
    const bool sameCellCount = hasReusableLayout && previousCount == cellCount;

    mCellRowsById.resize(cellCount);
    std::int32_t minRow = std::numeric_limits<std::int32_t>::max();
    std::int32_t maxRow = std::numeric_limits<std::int32_t>::min();
    bool rowsStillMatch = sameCellCount;

    if (hasReusableLayout) {
        const bool refreshDuringClassification =
                sameCellCount && !mCompactPreviousBuildRelayout;
        for (int slot = 0; slot < previousCount; slot++) {
            CompactSpatialRecord& record = mCompactRecords[slot];
            const int cellId = record.cellId;
            if (cellId < 0 || cellId >= cellCount) {
                continue;
            }
            const double y = cellData.mPositionY[cellId];
            const std::int32_t newRow = rowOf(y);
            minRow = std::min(minRow, newRow);
            maxRow = std::max(maxRow, newRow);
            if (newRow != record.row) {
                mCellRowsById[cellId] = newRow;
                rowsStillMatch = false;
            }
            if (refreshDuringClassification && rowsStillMatch) {
                record.x = cellData.mPositionX[cellId];
                record.y = y;
                record.radius = cellData.mRadius[cellId];
                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                    mStats.compactPayloadRefreshes++;
                }
            }
        }
        if (rowsStillMatch && !refreshDuringClassification) {
            for (int slot = 0; slot < previousCount; slot++) {
                CompactSpatialRecord& record = mCompactRecords[slot];
                const int cellId = record.cellId;
                record.x = cellData.mPositionX[cellId];
                record.y = cellData.mPositionY[cellId];
                record.radius = cellData.mRadius[cellId];
            }
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.compactPayloadRefreshes += previousCount;
            }
        }
        for (int cellId = previousCount; cellId < cellCount; cellId++) {
            const std::int32_t newRow = rowOf(cellData.mPositionY[cellId]);
            mCellRowsById[cellId] = newRow;
            minRow = std::min(minRow, newRow);
            maxRow = std::max(maxRow, newRow);
        }
    } else {
        for (int cellId = 0; cellId < cellCount; cellId++) {
            const std::int32_t newRow = rowOf(cellData.mPositionY[cellId]);
            mCellRowsById[cellId] = newRow;
            minRow = std::min(minRow, newRow);
            maxRow = std::max(maxRow, newRow);
        }
    }

#ifdef CELLULARITY_PROFILE
    finishProfilePhase(mStats.compactClassifyNanos);
#endif

    if (rowsStillMatch) {
        const int rowSpan = static_cast<int>(mCellCountsByRow.size());
        if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
            mStats.rowsReused += rowSpan;
        }
        for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
            sortCompactRecordsByInsertion(mCompactRecords,
                    mRowStartOffsets[rowIndex],
                    mRowStartOffsets[rowIndex + 1],
                    mStats,
                    collectStats);
        }
        mCompactPreviousBuildRelayout = false;
#ifdef CELLULARITY_PROFILE
        finishProfilePhase(mStats.compactSortNanos);
#endif
        writeCompactSentinel(cellCount);
#ifdef CELLULARITY_PROFILE
        finishProfilePhase(mStats.compactFinalizeNanos);
#endif
        return;
    }

    const int rowSpan = static_cast<int>(maxRow - minRow + 1);
    mCellCountsByRow.assign(rowSpan, 0);
    mChangedCellCountsByRow.assign(rowSpan, 0);
    int changedCells = 0;

    if (hasReusableLayout) {
        for (int slot = 0; slot < previousCount; slot++) {
            const CompactSpatialRecord& record = mCompactRecords[slot];
            const int cellId = record.cellId;
            if (cellId < 0 || cellId >= cellCount) {
                continue;
            }
            const std::int32_t newRow = mCellRowsById[cellId];
            const int rowIndex = static_cast<int>(newRow - minRow);
            mCellCountsByRow[rowIndex]++;
            if (newRow != record.row) {
                mChangedCellCountsByRow[rowIndex]++;
                changedCells++;
            }
        }
        for (int cellId = previousCount; cellId < cellCount; cellId++) {
            const int rowIndex = static_cast<int>(mCellRowsById[cellId] - minRow);
            mCellCountsByRow[rowIndex]++;
            mChangedCellCountsByRow[rowIndex]++;
            changedCells++;
        }
    } else {
        for (int cellId = 0; cellId < cellCount; cellId++) {
            mCellCountsByRow[static_cast<int>(mCellRowsById[cellId] - minRow)]++;
        }
    }

    mRowStartOffsets.resize(static_cast<std::size_t>(rowSpan) + 1U);
    int prefix = 0;
    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        mRowStartOffsets[rowIndex] = prefix;
        prefix += mCellCountsByRow[rowIndex];
    }
    mRowStartOffsets[rowSpan] = prefix;

#ifdef CELLULARITY_PROFILE
    finishProfilePhase(mStats.compactLayoutNanos);
#endif

    mRowWriteOffsets = mRowStartOffsets;
    mCompactRelayoutCellIds.resize(cellCount);
    if (hasReusableLayout) {
        for (int slot = 0; slot < previousCount; slot++) {
            const int cellId = mCompactRecords[slot].cellId;
            if (cellId < 0 || cellId >= cellCount) {
                continue;
            }
            const int rowIndex = static_cast<int>(mCellRowsById[cellId] - minRow);
            mCompactRelayoutCellIds[mRowWriteOffsets[rowIndex]++] = cellId;
        }
        for (int cellId = previousCount; cellId < cellCount; cellId++) {
            const int rowIndex = static_cast<int>(mCellRowsById[cellId] - minRow);
            mCompactRelayoutCellIds[mRowWriteOffsets[rowIndex]++] = cellId;
        }
    } else {
        for (int cellId = 0; cellId < cellCount; cellId++) {
            const int rowIndex = static_cast<int>(mCellRowsById[cellId] - minRow);
            mCompactRelayoutCellIds[mRowWriteOffsets[rowIndex]++] = cellId;
        }
    }
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        mStats.compactScatterIdWrites += cellCount;
    }

#ifdef CELLULARITY_PROFILE
    finishProfilePhase(mStats.compactScatterNanos);
#endif

    mCompactRecords.resize(static_cast<std::size_t>(cellCount) + 1U);
    for (int slot = 0; slot < cellCount; slot++) {
        const int cellId = mCompactRelayoutCellIds[slot];
        mCompactRecords[slot] = {
                cellData.mPositionX[cellId],
                cellData.mPositionY[cellId],
                cellData.mRadius[cellId],
                cellId,
                mCellRowsById[cellId]
        };
    }
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        mStats.compactRecordMaterializations += cellCount;
    }

#ifdef CELLULARITY_PROFILE
    finishProfilePhase(mStats.compactMaterializeNanos);
#endif

    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        const int rowSize = rowEnd - rowStart;
        const bool useFullSort = !hasReusableLayout
                || mChangedCellCountsByRow[rowIndex] > hybridFullSortThreshold(rowSize);
        if (useFullSort) {
            sortCompactRecordsByFullSort(mCompactRecords,
                    rowStart,
                    rowEnd,
                    mStats,
                    collectStats);
        } else {
            sortCompactRecordsByInsertion(mCompactRecords,
                    rowStart,
                    rowEnd,
                    mStats,
                    collectStats);
        }
    }

#ifdef CELLULARITY_PROFILE
    finishProfilePhase(mStats.compactSortNanos);
#endif

    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        mStats.rowRelayouts++;
        if (hasReusableLayout) {
            mStats.rowChangedCells += changedCells;
        } else {
            mStats.layoutRebuilds++;
        }
    }
    mMinRow = minRow;
    mCompactPreviousBuildRelayout = true;
    writeCompactSentinel(cellCount);
#ifdef CELLULARITY_PROFILE
    finishProfilePhase(mStats.compactFinalizeNanos);
#endif
}

void CellSweepBroadphase::buildSelectedLayout(const CellData& cellData, bool collectStats) {
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        mStats.clear();
    }
    if (mScanAlgorithm
            == CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP) {
        if (!mIncrementalBucketedF64Broadphase.tryBuildPreparedSource(
                    static_cast<std::size_t>(cellData.mCellCount),
                    collectStats)) {
            mIncrementalBucketedF64Broadphase.build(cellData, collectStats);
        }
#ifdef CELLULARITY_PROFILE
        syncIncrementalBucketedF64Stats(collectStats);
#endif
        return;
    }
    if (mScanAlgorithm == CellOverlapScanAlgorithm::COMPACT_PRIMARY_SWEEP) {
        buildCompactPrimary(cellData, collectStats);
        return;
    }
    if (mScanAlgorithm == CellOverlapScanAlgorithm::COMPACT_RECORD_SWEEP) {
        buildBaselineSpatialLayout<true>(cellData, collectStats);
    } else {
        buildBaselineSpatialLayout<false>(cellData, collectStats);
    }
}

void CellSweepBroadphase::buildBaseline(const CellData& cellData, bool collectStats) {
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        mStats.clear();
    }
    buildBaselineSpatialLayout<false>(cellData, collectStats);
}

void CellSweepBroadphase::buildConfigured(const CellData& cellData, bool collectStats) {
#if defined(CELLULARITY_RUNTIME_OVERLAP_SELECTION)
    buildSelectedLayout(cellData, collectStats);
#else
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        mStats.clear();
    }
#if defined(CELLULARITY_DEFAULT_COMPACT_PRIMARY_SWEEP)
    buildCompactPrimary(cellData, collectStats);
#elif defined(CELLULARITY_DEFAULT_INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP)
    if (!mIncrementalBucketedF64Broadphase.tryBuildPreparedSource(
                static_cast<std::size_t>(cellData.mCellCount),
                collectStats)) {
        mIncrementalBucketedF64Broadphase.build(cellData, collectStats);
    }
#ifdef CELLULARITY_PROFILE
    syncIncrementalBucketedF64Stats(collectStats);
#endif
#elif defined(CELLULARITY_DEFAULT_COMPACT_RECORD_SWEEP)
    buildBaselineSpatialLayout<true>(cellData, collectStats);
#else
    buildBaselineSpatialLayout<false>(cellData, collectStats);
#endif
#endif
}

template <bool BuildCompactProjection>
void CellSweepBroadphase::buildBaselineSpatialLayout(const CellData& cellData,
        bool collectStats) {
    mUsesSuffixRadiusWindow = mScanAlgorithm == CellOverlapScanAlgorithm::TIGHT_RADIUS_SWEEP;
    mUsesRowMaxRadiusWindow = mScanAlgorithm == CellOverlapScanAlgorithm::ROW_MAX_RADIUS_SWEEP;
    const int cellCount = cellData.mCellCount;
    if (cellCount == 0) {
        clear();
        return;
    }

    const bool hasReusableLayout =
            !mOrderedCellIds.empty()
            && static_cast<int>(mOrderedX.size()) == static_cast<int>(mOrderedCellIds.size())
            && static_cast<int>(mOrderedY.size()) == static_cast<int>(mOrderedCellIds.size())
            && static_cast<int>(mOrderedRadius.size()) == static_cast<int>(mOrderedCellIds.size())
            && !mCellCountsByRow.empty();
    bool forceRelayout = false;
    if (hasReusableLayout && static_cast<int>(mOrderedCellIds.size()) != cellCount) {
        const int previousCount = static_cast<int>(mOrderedCellIds.size());
        if (cellCount < previousCount) {
            mOrderedCellIds.erase(std::remove_if(mOrderedCellIds.begin(),
                                          mOrderedCellIds.end(),
                                          [cellCount](int cellIndex) {
                                              return cellIndex >= cellCount;
                                          }),
                    mOrderedCellIds.end());
        } else {
            mOrderedCellIds.reserve(cellCount);
            for (int cellIndex = previousCount; cellIndex < cellCount; cellIndex++) {
                mOrderedCellIds.push_back(cellIndex);
            }
        }
        if (static_cast<int>(mOrderedCellIds.size()) == cellCount) {
            mOrderedX.resize(cellCount);
            mOrderedY.resize(cellCount);
            mOrderedRadius.resize(cellCount);
            forceRelayout = true;
        } else {
            clear();
        }
    }

    const bool hasPersistentLayout =
            static_cast<int>(mOrderedCellIds.size()) == cellCount
            && static_cast<int>(mOrderedX.size()) == cellCount
            && static_cast<int>(mOrderedY.size()) == cellCount
            && static_cast<int>(mOrderedRadius.size()) == cellCount
            && !mCellCountsByRow.empty();
    if (hasPersistentLayout) {
        bool rowsStillMatch = !forceRelayout;
        const int oldRowSpan = static_cast<int>(mCellCountsByRow.size());
        for (int rowIndex = 0; rowIndex < oldRowSpan && rowsStillMatch; rowIndex++) {
            const std::int32_t expectedRow = mMinRow + rowIndex;
            const int rowStart = mRowStartOffsets[rowIndex];
            const int rowEnd = mRowStartOffsets[rowIndex + 1];
            for (int slot = rowStart; slot < rowEnd; slot++) {
                const int cellIndex = mOrderedCellIds[slot];
                if (rowOf(cellData.mPositionY[cellIndex]) != expectedRow) {
                    rowsStillMatch = false;
                    break;
                }
                mOrderedX[slot] = cellData.mPositionX[cellIndex];
                mOrderedY[slot] = cellData.mPositionY[cellIndex];
                mOrderedRadius[slot] = cellData.mRadius[cellIndex];
            }
        }

        if (rowsStillMatch) {
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.rowsReused += oldRowSpan;
            }
            for (int rowIndex = 0; rowIndex < oldRowSpan; rowIndex++) {
                const int rowStart = mRowStartOffsets[rowIndex];
                const int rowEnd = mRowStartOffsets[rowIndex + 1];
                sortRowByInsertion(mOrderedCellIds,
                        mOrderedX,
                        mOrderedY,
                        mOrderedRadius,
                        rowStart,
                        rowEnd,
                        mStats,
                        collectStats);
            }
            refreshRadiusWindowMetadata();
            if constexpr (BuildCompactProjection) {
                refreshProjectedCompactRecords(collectStats);
            }
            return;
        }

        const int previousCellRowsSize = static_cast<int>(mCellRowsById.size());
        std::int32_t minRow = std::numeric_limits<std::int32_t>::max();
        std::int32_t maxRow = std::numeric_limits<std::int32_t>::min();
        mCellRowsById.resize(cellCount);
        for (int slot = 0; slot < cellCount; slot++) {
            const int cellIndex = mOrderedCellIds[slot];
            const std::int32_t row = rowOf(cellData.mPositionY[cellIndex]);
            minRow = std::min(minRow, row);
            maxRow = std::max(maxRow, row);
        }

        const int rowSpan = static_cast<int>(maxRow - minRow + 1);
        if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
            mStats.rowRelayouts++;
        }
        mCellCountsByRow.assign(rowSpan, 0);
        mChangedCellCountsByRow.assign(rowSpan, 0);
        for (int slot = 0; slot < cellCount; slot++) {
            const int cellIndex = mOrderedCellIds[slot];
            const std::int32_t oldRow =
                    cellIndex < previousCellRowsSize ? mCellRowsById[cellIndex] : std::numeric_limits<std::int32_t>::max();
            const std::int32_t newRow = rowOf(cellData.mPositionY[cellIndex]);
            mCellRowsById[cellIndex] = newRow;
            const int newRowIndex = static_cast<int>(newRow - minRow);
            mCellCountsByRow[newRowIndex]++;
            if (newRow != oldRow) {
                mChangedCellCountsByRow[newRowIndex]++;
                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                    mStats.rowChangedCells++;
                }
            }
        }

        mRowStartOffsets.resize(static_cast<size_t>(rowSpan) + 1);
        int prefix = 0;
        for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
            mRowStartOffsets[rowIndex] = prefix;
            prefix += mCellCountsByRow[rowIndex];
        }
        mRowStartOffsets[rowSpan] = prefix;

        mRowWriteOffsets = mRowStartOffsets;
        mRelayoutCellIds.resize(cellCount);
        for (int slot = 0; slot < cellCount; slot++) {
            const int cellIndex = mOrderedCellIds[slot];
            const int rowIndex = static_cast<int>(mCellRowsById[cellIndex] - minRow);
            mRelayoutCellIds[mRowWriteOffsets[rowIndex]++] = cellIndex;
        }
        mOrderedCellIds.swap(mRelayoutCellIds);
        for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
            const int rowStart = mRowStartOffsets[rowIndex];
            const int rowEnd = mRowStartOffsets[rowIndex + 1];
            for (int slot = rowStart; slot < rowEnd; slot++) {
                const int cellIndex = mOrderedCellIds[slot];
                mOrderedX[slot] = cellData.mPositionX[cellIndex];
                mOrderedY[slot] = cellData.mPositionY[cellIndex];
                mOrderedRadius[slot] = cellData.mRadius[cellIndex];
            }
            const int rowSize = rowEnd - rowStart;
            const bool useFullSort =
                    mChangedCellCountsByRow[rowIndex] > hybridFullSortThreshold(rowSize);
            if (useFullSort) {
                sortRowByStdSort(cellData,
                        mOrderedCellIds,
                        mOrderedX,
                        mOrderedY,
                        mOrderedRadius,
                        rowStart,
                        rowEnd,
                        mStats,
                        collectStats);
            } else {
                sortRowByInsertion(mOrderedCellIds,
                        mOrderedX,
                        mOrderedY,
                        mOrderedRadius,
                        rowStart,
                        rowEnd,
                        mStats,
                        collectStats);
            }
        }
        mMinRow = minRow;
        refreshRadiusWindowMetadata();
        if constexpr (BuildCompactProjection) {
            refreshProjectedCompactRecords(collectStats);
        }
        return;
    }

    mCellRowsById.resize(cellCount);
    std::int32_t minRow = std::numeric_limits<std::int32_t>::max();
    std::int32_t maxRow = std::numeric_limits<std::int32_t>::min();
    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        const std::int32_t row = rowOf(cellData.mPositionY[cellIndex]);
        mCellRowsById[cellIndex] = row;
        minRow = std::min(minRow, row);
        maxRow = std::max(maxRow, row);
    }

    const int rowSpan = static_cast<int>(maxRow - minRow + 1);
    if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
        mStats.rowRelayouts++;
        mStats.layoutRebuilds++;
    }
    mCellCountsByRow.assign(rowSpan, 0);
    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        mCellCountsByRow[static_cast<int>(mCellRowsById[cellIndex] - minRow)]++;
    }

    mRowStartOffsets.resize(static_cast<size_t>(rowSpan) + 1);
    int prefix = 0;
    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        mRowStartOffsets[rowIndex] = prefix;
        prefix += mCellCountsByRow[rowIndex];
    }
    mRowStartOffsets[rowSpan] = prefix;

    mRowWriteOffsets = mRowStartOffsets;
    mOrderedCellIds.resize(cellCount);
    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        const int rowIndex = static_cast<int>(mCellRowsById[cellIndex] - minRow);
        mOrderedCellIds[mRowWriteOffsets[rowIndex]++] = cellIndex;
    }

    mOrderedX.resize(cellCount);
    mOrderedY.resize(cellCount);
    mOrderedRadius.resize(cellCount);
    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        for (int slot = rowStart; slot < rowEnd; slot++) {
            const int cellIndex = mOrderedCellIds[slot];
            mOrderedX[slot] = cellData.mPositionX[cellIndex];
            mOrderedY[slot] = cellData.mPositionY[cellIndex];
            mOrderedRadius[slot] = cellData.mRadius[cellIndex];
        }
        sortRowByStdSort(cellData,
                mOrderedCellIds,
                mOrderedX,
                mOrderedY,
                mOrderedRadius,
                rowStart,
                rowEnd,
                mStats,
                collectStats);
    }
    mMinRow = minRow;
    refreshRadiusWindowMetadata();
    if constexpr (BuildCompactProjection) {
        refreshProjectedCompactRecords(collectStats);
    }
}

void CellSweepBroadphase::forEachCandidatePair(void* context, Visitor visitor) const {
    if (mScanAlgorithm
            == CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP) {
        mIncrementalBucketedF64Broadphase.forEachCandidatePairInline(
                [context, visitor](int firstCellIndex, int secondCellIndex) {
                    visitor(context, firstCellIndex, secondCellIndex);
                });
        return;
    }
    if (compactRecordsReady()) {
        const int recordCount = mCompactCellCount;
        const CompactSpatialRecord* const records = mCompactRecords.data();
        for (int first = 0; first < recordCount; first++) {
            const CompactSpatialRecord left = records[first];
            const double stopXRight = left.x + left.radius + mMaxRadius;
            int sameEnd = first + 1;
            while (records[sameEnd].row == left.row
                    && records[sameEnd].x < stopXRight) {
                sameEnd++;
            }
            for (int second = first + 1; second < sameEnd; second++) {
                visitor(context, left.cellId, records[second].cellId);
            }
        }

        int upperCursor = 0;
        for (int first = 0; first < recordCount; first++) {
            const CompactSpatialRecord left = records[first];
            if (left.row == std::numeric_limits<std::int32_t>::max()) {
                continue;
            }
            const std::int32_t upperRow = left.row + 1;
            const double stopXLeft = left.x - mMaxDiameter;
            const double stopXRight = left.x + left.radius + mMaxRadius;
            while (records[upperCursor].row < upperRow
                    || (records[upperCursor].row == upperRow
                            && records[upperCursor].x < stopXLeft)) {
                upperCursor++;
            }
            int upperEnd = upperCursor;
            while (records[upperEnd].row == upperRow
                    && records[upperEnd].x < stopXRight) {
                upperEnd++;
            }
            for (int second = upperCursor; second < upperEnd; second++) {
                visitor(context, left.cellId, records[second].cellId);
            }
        }
        return;
    }

    const int rowSpan = mCellCountsByRow.empty() ? 0 : static_cast<int>(mCellCountsByRow.size());
    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = mOrderedX[first];
            const double leftRadius = mOrderedRadius[first];
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            int sameEnd = first + 1;
            while (sameEnd < rowEnd && mOrderedX[sameEnd] < stopXRight) {
                sameEnd++;
            }
            for (int second = first + 1; second < sameEnd; second++) {
                visitor(context, mOrderedCellIds[first], mOrderedCellIds[second]);
            }
        }
    }

    for (int rowIndex = 0; rowIndex + 1 < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        const int nextStart = mRowStartOffsets[rowIndex + 1];
        const int nextEnd = mRowStartOffsets[rowIndex + 2];
        int upperCursor = nextStart;
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = mOrderedX[first];
            const double leftRadius = mOrderedRadius[first];
            const double stopXLeft = leftX - mMaxDiameter;
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            while (upperCursor < nextEnd && mOrderedX[upperCursor] < stopXLeft) {
                upperCursor++;
            }
            int upperEnd = upperCursor;
            while (upperEnd < nextEnd && mOrderedX[upperEnd] < stopXRight) {
                upperEnd++;
            }
            for (int second = upperCursor; second < upperEnd; second++) {
                visitor(context, mOrderedCellIds[first], mOrderedCellIds[second]);
            }
        }
    }
}

void CellSweepBroadphase::forEachOverlappingPair(void* context, OverlapVisitor visitor, bool collectStats) {
    if (mScanAlgorithm
            == CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP) {
        auto adapter = [context, visitor](int firstCellIndex,
                               int secondCellIndex,
                               double distanceX,
                               double distanceY,
                               double distanceSq,
                               double firstRadius,
                               double secondRadius) {
            visitor(context,
                    firstCellIndex,
                    secondCellIndex,
                    distanceX,
                    distanceY,
                    distanceSq,
                    firstRadius,
                    secondRadius);
        };
        mIncrementalBucketedF64Broadphase.forEachOverlappingPairInline(
                adapter, collectStats);
#ifdef CELLULARITY_PROFILE
        syncIncrementalBucketedF64Stats(collectStats);
#endif
        return;
    }
    if (compactRecordsReady()) {
        auto adapter = [context, visitor](int firstCellIndex,
                               int secondCellIndex,
                               double distanceX,
                               double distanceY,
                               double distanceSq,
                               double firstRadius,
                               double secondRadius) {
            visitor(context,
                    firstCellIndex,
                    secondCellIndex,
                    distanceX,
                    distanceY,
                    distanceSq,
                    firstRadius,
                    secondRadius);
        };
        forEachOverlappingPairCompactRecordsInline(adapter, collectStats);
        return;
    }

    const int rowSpan = mCellCountsByRow.empty() ? 0 : static_cast<int>(mCellCountsByRow.size());
    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = mOrderedX[first];
            const double leftY = mOrderedY[first];
            const double leftRadius = mOrderedRadius[first];
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            int sameEnd = first + 1;
            while (sameEnd < rowEnd && mOrderedX[sameEnd] < stopXRight) {
                sameEnd++;
            }
            const int sameCandidateCount = sameEnd - (first + 1);
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.candidatePairs += sameCandidateCount;
                mStats.sameRowCandidatePairs += sameCandidateCount;
                mStats.scalarChecks += sameCandidateCount;
                if (sameCandidateCount > 0) {
                    mStats.sameRowCandidateRanges++;
                }
            }
            for (int second = first + 1; second < sameEnd; second++) {
                const double distanceX = leftX - mOrderedX[second];
                const double distanceY = leftY - mOrderedY[second];
                const double distanceSq = distanceX * distanceX + distanceY * distanceY;
                const double rightRadius = mOrderedRadius[second];
                const double contactDistance = leftRadius + rightRadius;
                if (distanceSq >= contactDistance * contactDistance) {
                    continue;
                }
                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                    mStats.exactPairs++;
                }
                visitor(context,
                        mOrderedCellIds[first],
                        mOrderedCellIds[second],
                        distanceX,
                        distanceY,
                        distanceSq,
                        leftRadius,
                        rightRadius);
            }
        }
    }

    for (int rowIndex = 0; rowIndex + 1 < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        const int nextStart = mRowStartOffsets[rowIndex + 1];
        const int nextEnd = mRowStartOffsets[rowIndex + 2];
        int upperCursor = nextStart;
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = mOrderedX[first];
            const double leftY = mOrderedY[first];
            const double leftRadius = mOrderedRadius[first];
            const double stopXLeft = leftX - mMaxDiameter;
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            while (upperCursor < nextEnd && mOrderedX[upperCursor] < stopXLeft) {
                upperCursor++;
            }
            int upperEnd = upperCursor;
            while (upperEnd < nextEnd && mOrderedX[upperEnd] < stopXRight) {
                upperEnd++;
            }
            const int nextCandidateCount = upperEnd - upperCursor;
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.candidatePairs += nextCandidateCount;
                mStats.nextRowCandidatePairs += nextCandidateCount;
                mStats.scalarChecks += nextCandidateCount;
                if (nextCandidateCount > 0) {
                    mStats.nextRowCandidateRanges++;
                }
            }
            for (int second = upperCursor; second < upperEnd; second++) {
                const double distanceX = leftX - mOrderedX[second];
                const double distanceY = leftY - mOrderedY[second];
                const double distanceSq = distanceX * distanceX + distanceY * distanceY;
                const double rightRadius = mOrderedRadius[second];
                const double contactDistance = leftRadius + rightRadius;
                if (distanceSq >= contactDistance * contactDistance) {
                    continue;
                }
                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                    mStats.exactPairs++;
                }
                visitor(context,
                        mOrderedCellIds[first],
                        mOrderedCellIds[second],
                        distanceX,
                        distanceY,
                        distanceSq,
                        leftRadius,
                        rightRadius);
            }
        }
    }
}

void CellSweepBroadphase::collectOverlappingPairs(ExactOverlapPairBuffer& output, bool collectStats) {
    if (mScanAlgorithm
            == CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP) {
        output.clear();
        forEachOverlappingPairInline(
                [&output](int firstCellIndex,
                        int secondCellIndex,
                        double distanceX,
                        double distanceY,
                        double,
                        double,
                        double) {
                    output.push(firstCellIndex, secondCellIndex, distanceX, distanceY);
                },
                collectStats);
        return;
    }
    if (compactRecordsReady()) {
        output.clear();
        forEachOverlappingPairInline(
                [&output](int firstCellIndex,
                        int secondCellIndex,
                        double distanceX,
                        double distanceY,
                        double,
                        double,
                        double) {
                    output.push(firstCellIndex, secondCellIndex, distanceX, distanceY);
                },
                collectStats);
        return;
    }

    if (mScanAlgorithm == CellOverlapScanAlgorithm::BASELINE_FAST_SWEEP) {
        collectOverlappingPairsBaselineFast(output, collectStats);
        return;
    }
    if (mScanAlgorithm == CellOverlapScanAlgorithm::SIMD_EXACT_SWEEP) {
        collectOverlappingPairsSimdExact(output, collectStats);
        return;
    }
    if (mScanAlgorithm == CellOverlapScanAlgorithm::SCALAR_Y_PRUNE_SWEEP) {
        collectOverlappingPairsScalarYPrune(output, collectStats);
        return;
    }
    if (mScanAlgorithm == CellOverlapScanAlgorithm::SCALAR_TILE_SWEEP) {
        collectOverlappingPairsScalarTile(output, collectStats);
        return;
    }
    if (mScanAlgorithm == CellOverlapScanAlgorithm::SIMD_TILE_SWEEP) {
        collectOverlappingPairsSimdTile(output, collectStats);
        return;
    }

    collectOverlappingPairsBaselineFast(output, collectStats);
}

void CellSweepBroadphase::collectOverlappingPairsConfigured(ExactOverlapPairBuffer& output,
        bool collectStats) {
    output.clear();
    forEachOverlappingPairConfiguredInline(
            [&output](int firstCellIndex,
                    int secondCellIndex,
                    double distanceX,
                    double distanceY,
                    double,
                    double,
                    double) {
                output.push(firstCellIndex, secondCellIndex, distanceX, distanceY);
            },
            collectStats);
}

void CellSweepBroadphase::collectOverlappingPairsBaselineFast(ExactOverlapPairBuffer& output, bool collectStats) {
    output.clear();
    const int rowSpan = mCellCountsByRow.empty() ? 0 : static_cast<int>(mCellCountsByRow.size());
    const int* const cellIds = mOrderedCellIds.data();
    const double* const x = mOrderedX.data();
    const double* const y = mOrderedY.data();
    const double* const radius = mOrderedRadius.data();
    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = x[first];
            const double leftY = y[first];
            const double leftRadius = radius[first];
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            int sameEnd = first + 1;
            while (sameEnd < rowEnd && x[sameEnd] < stopXRight) {
                sameEnd++;
            }
            const int sameCandidateCount = sameEnd - (first + 1);
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.candidatePairs += sameCandidateCount;
                mStats.sameRowCandidatePairs += sameCandidateCount;
                mStats.scalarChecks += sameCandidateCount;
                if (sameCandidateCount > 0) {
                    mStats.sameRowCandidateRanges++;
                }
            }
            for (int second = first + 1; second < sameEnd; second++) {
                const double distanceX = leftX - x[second];
                const double distanceY = leftY - y[second];
                const double distanceSq = distanceX * distanceX + distanceY * distanceY;
                const double rightRadius = radius[second];
                const double contactDistance = leftRadius + rightRadius;
                if (distanceSq >= contactDistance * contactDistance) {
                    continue;
                }
                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                    mStats.exactPairs++;
                }
                output.push(cellIds[first],
                        cellIds[second],
                        distanceX,
                        distanceY);
            }
        }
    }

    for (int rowIndex = 0; rowIndex + 1 < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        const int nextStart = mRowStartOffsets[rowIndex + 1];
        const int nextEnd = mRowStartOffsets[rowIndex + 2];
        int upperCursor = nextStart;
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = x[first];
            const double leftY = y[first];
            const double leftRadius = radius[first];
            const double stopXLeft = leftX - mMaxDiameter;
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            while (upperCursor < nextEnd && x[upperCursor] < stopXLeft) {
                upperCursor++;
            }
            int upperEnd = upperCursor;
            while (upperEnd < nextEnd && x[upperEnd] < stopXRight) {
                upperEnd++;
            }
            const int nextCandidateCount = upperEnd - upperCursor;
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.candidatePairs += nextCandidateCount;
                mStats.nextRowCandidatePairs += nextCandidateCount;
                mStats.scalarChecks += nextCandidateCount;
                if (nextCandidateCount > 0) {
                    mStats.nextRowCandidateRanges++;
                }
            }
            for (int second = upperCursor; second < upperEnd; second++) {
                const double distanceX = leftX - x[second];
                const double distanceY = leftY - y[second];
                const double distanceSq = distanceX * distanceX + distanceY * distanceY;
                const double rightRadius = radius[second];
                const double contactDistance = leftRadius + rightRadius;
                if (distanceSq >= contactDistance * contactDistance) {
                    continue;
                }
                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                    mStats.exactPairs++;
                }
                output.push(cellIds[first],
                        cellIds[second],
                        distanceX,
                        distanceY);
            }
        }
    }
}

void CellSweepBroadphase::collectOverlappingPairsScalarTile(ExactOverlapPairBuffer& output, bool collectStats) {
    output.clear();
    constexpr int TILE_SIZE = 4;
    const int rowSpan = mCellCountsByRow.empty() ? 0 : static_cast<int>(mCellCountsByRow.size());
    const int* const cellIds = mOrderedCellIds.data();
    const double* const x = mOrderedX.data();
    const double* const y = mOrderedY.data();
    const double* const radius = mOrderedRadius.data();

    auto testPair = [&](int first, int second) {
        const double distanceX = x[first] - x[second];
        const double distanceY = y[first] - y[second];
        const double distanceSq = distanceX * distanceX + distanceY * distanceY;
        const double contactDistance = radius[first] + radius[second];
        if (distanceSq >= contactDistance * contactDistance) {
            return;
        }
        if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
            mStats.exactPairs++;
        }
        output.push(cellIds[first], cellIds[second], distanceX, distanceY);
    };

    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        for (int leftBlockStart = rowStart; leftBlockStart < rowEnd; leftBlockStart += TILE_SIZE) {
            const int leftBlockEnd = std::min(leftBlockStart + TILE_SIZE, rowEnd);
            for (int rightBlockStart = leftBlockStart; rightBlockStart < rowEnd; rightBlockStart += TILE_SIZE) {
                const int rightBlockEnd = std::min(rightBlockStart + TILE_SIZE, rowEnd);

                int checkedPairs = 0;
                if (rightBlockStart == leftBlockStart) {
                    for (int first = leftBlockStart; first < leftBlockEnd; first++) {
                        const double stopXRight = x[first] + radius[first] + mMaxRadius;
                        for (int second = first + 1;
                                second < rightBlockEnd && x[second] < stopXRight;
                                second++) {
                            checkedPairs++;
                            testPair(first, second);
                        }
                    }
                } else {
                    if (x[rightBlockStart] >= x[leftBlockEnd - 1] + mMaxDiameter) {
                        break;
                    }
                    for (int first = leftBlockStart; first < leftBlockEnd; first++) {
                        const double stopXRight = x[first] + radius[first] + mMaxRadius;
                        if (x[rightBlockStart] >= stopXRight) {
                            continue;
                        }
                        for (int second = rightBlockStart;
                                second < rightBlockEnd && x[second] < stopXRight;
                                second++) {
                            checkedPairs++;
                            testPair(first, second);
                        }
                    }
                }

                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats) && checkedPairs > 0) {
                    mStats.candidatePairs += checkedPairs;
                    mStats.sameRowCandidatePairs += checkedPairs;
                    mStats.scalarChecks += checkedPairs;
                    mStats.sameRowCandidateRanges++;
                }
            }
        }
    }

    for (int rowIndex = 0; rowIndex + 1 < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        const int nextStart = mRowStartOffsets[rowIndex + 1];
        const int nextEnd = mRowStartOffsets[rowIndex + 2];
        if (rowStart == rowEnd || nextStart == nextEnd) {
            continue;
        }

        int firstCandidateRightBlock = nextStart;
        for (int leftBlockStart = rowStart; leftBlockStart < rowEnd; leftBlockStart += TILE_SIZE) {
            const int leftBlockEnd = std::min(leftBlockStart + TILE_SIZE, rowEnd);
            const double leftBlockMinX = x[leftBlockStart];
            const double leftBlockMaxX = x[leftBlockEnd - 1];

            while (firstCandidateRightBlock < nextEnd
                    && x[std::min(firstCandidateRightBlock + TILE_SIZE, nextEnd) - 1]
                            < leftBlockMinX - mMaxDiameter) {
                firstCandidateRightBlock += TILE_SIZE;
            }

            for (int rightBlockStart = firstCandidateRightBlock;
                    rightBlockStart < nextEnd;
                    rightBlockStart += TILE_SIZE) {
                const int rightBlockEnd = std::min(rightBlockStart + TILE_SIZE, nextEnd);
                if (x[rightBlockStart] > leftBlockMaxX + mMaxDiameter) {
                    break;
                }

                int checkedPairs = 0;
                for (int first = leftBlockStart; first < leftBlockEnd; first++) {
                    const double stopXLeft = x[first] - mMaxDiameter;
                    const double stopXRight = x[first] + radius[first] + mMaxRadius;
                    if (x[rightBlockEnd - 1] < stopXLeft || x[rightBlockStart] >= stopXRight) {
                        continue;
                    }
                    for (int second = rightBlockStart; second < rightBlockEnd; second++) {
                        if (x[second] < stopXLeft) {
                            continue;
                        }
                        if (x[second] >= stopXRight) {
                            break;
                        }
                        checkedPairs++;
                        testPair(first, second);
                    }
                }

                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats) && checkedPairs > 0) {
                    mStats.candidatePairs += checkedPairs;
                    mStats.nextRowCandidatePairs += checkedPairs;
                    mStats.scalarChecks += checkedPairs;
                    mStats.nextRowCandidateRanges++;
                }
            }
        }
    }
}

void CellSweepBroadphase::collectOverlappingPairsSimdTile(ExactOverlapPairBuffer& output, bool collectStats) {
    output.clear();
    constexpr int TILE_SIZE = 4;
    const int rowSpan = mCellCountsByRow.empty() ? 0 : static_cast<int>(mCellCountsByRow.size());
    const int* const cellIds = mOrderedCellIds.data();
    const double* const x = mOrderedX.data();
    const double* const y = mOrderedY.data();
    const double* const radius = mOrderedRadius.data();

    const hn::CappedTag<double, TILE_SIZE> d;
    const std::size_t laneCount = hn::Lanes(d);
    HWY_ALIGN double distanceXLanes[TILE_SIZE];
    HWY_ALIGN double distanceYLanes[TILE_SIZE];
    HWY_ALIGN uint8_t overlapBits[8];

    auto testPairScalar = [&](int first, int second) {
        const double distanceX = x[first] - x[second];
        const double distanceY = y[first] - y[second];
        const double distanceSq = distanceX * distanceX + distanceY * distanceY;
        const double contactDistance = radius[first] + radius[second];
        if (distanceSq >= contactDistance * contactDistance) {
            return;
        }
        if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
            mStats.exactPairs++;
        }
        output.push(cellIds[first], cellIds[second], distanceX, distanceY);
    };

    auto testRange = [&](int first, int secondBegin, int secondEnd, int& checkedPairs) {
        const double leftX = x[first];
        const double leftY = y[first];
        const double leftRadius = radius[first];
        const auto leftXV = hn::Set(d, leftX);
        const auto leftYV = hn::Set(d, leftY);
        const auto leftRadiusV = hn::Set(d, leftRadius);

        int second = secondBegin;
        const int simdWidth = static_cast<int>(laneCount);
        const int simdEnd = secondBegin + ((secondEnd - secondBegin) / simdWidth) * simdWidth;
        for (; second < simdEnd; second += simdWidth) {
            const auto rightX = hn::LoadU(d, x + second);
            const auto rightY = hn::LoadU(d, y + second);
            const auto rightRadius = hn::LoadU(d, radius + second);
            const auto distanceX = hn::Sub(leftXV, rightX);
            const auto distanceY = hn::Sub(leftYV, rightY);
            const auto contactDistance = hn::Add(leftRadiusV, rightRadius);
            const auto distanceSq = hn::MulAdd(distanceX, distanceX, hn::Mul(distanceY, distanceY));
            const auto contactDistanceSq = hn::Mul(contactDistance, contactDistance);
            const auto overlapMask = hn::Lt(distanceSq, contactDistanceSq);
            checkedPairs += simdWidth;

            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.simdChecks += simdWidth;
                mStats.exactPairs += static_cast<long long>(hn::CountTrue(d, overlapMask));
            }
            if (hn::AllFalse(d, overlapMask)) {
                continue;
            }

            hn::Store(distanceX, d, distanceXLanes);
            hn::Store(distanceY, d, distanceYLanes);
            hn::StoreMaskBits(d, overlapMask, overlapBits);
            for (int lane = 0; lane < simdWidth; lane++) {
                const uint8_t bit = static_cast<uint8_t>(1U << (lane & 7U));
                if ((overlapBits[static_cast<std::size_t>(lane) >> 3U] & bit) == 0) {
                    continue;
                }
                output.push(cellIds[first],
                        cellIds[second + lane],
                        distanceXLanes[lane],
                        distanceYLanes[lane]);
            }
        }

        if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
            mStats.scalarChecks += secondEnd - second;
        }
        for (; second < secondEnd; second++) {
            checkedPairs++;
            testPairScalar(first, second);
        }
    };

    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        for (int leftBlockStart = rowStart; leftBlockStart < rowEnd; leftBlockStart += TILE_SIZE) {
            const int leftBlockEnd = std::min(leftBlockStart + TILE_SIZE, rowEnd);
            const double leftBlockMaxX = x[leftBlockEnd - 1];
            for (int rightBlockStart = leftBlockStart; rightBlockStart < rowEnd; rightBlockStart += TILE_SIZE) {
                const int rightBlockEnd = std::min(rightBlockStart + TILE_SIZE, rowEnd);
                if (rightBlockStart != leftBlockStart
                        && x[rightBlockStart] - leftBlockMaxX >= mMaxDiameter) {
                    break;
                }

                int checkedPairs = 0;
                if (rightBlockStart == leftBlockStart) {
                    for (int first = leftBlockStart; first < leftBlockEnd; first++) {
                        testRange(first, first + 1, rightBlockEnd, checkedPairs);
                    }
                } else {
                    for (int first = leftBlockStart; first < leftBlockEnd; first++) {
                        testRange(first, rightBlockStart, rightBlockEnd, checkedPairs);
                    }
                }

                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats) && checkedPairs > 0) {
                    mStats.candidatePairs += checkedPairs;
                    mStats.sameRowCandidatePairs += checkedPairs;
                    mStats.sameRowCandidateRanges++;
                }
            }
        }
    }

    for (int rowIndex = 0; rowIndex + 1 < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        const int nextStart = mRowStartOffsets[rowIndex + 1];
        const int nextEnd = mRowStartOffsets[rowIndex + 2];
        if (rowStart == rowEnd || nextStart == nextEnd) {
            continue;
        }

        int firstCandidateRightBlock = nextStart;
        for (int leftBlockStart = rowStart; leftBlockStart < rowEnd; leftBlockStart += TILE_SIZE) {
            const int leftBlockEnd = std::min(leftBlockStart + TILE_SIZE, rowEnd);
            const double leftBlockMinX = x[leftBlockStart];
            const double leftBlockMaxX = x[leftBlockEnd - 1];

            while (firstCandidateRightBlock < nextEnd
                    && x[std::min(firstCandidateRightBlock + TILE_SIZE, nextEnd) - 1]
                            < leftBlockMinX - mMaxDiameter) {
                firstCandidateRightBlock += TILE_SIZE;
            }

            for (int rightBlockStart = firstCandidateRightBlock;
                    rightBlockStart < nextEnd;
                    rightBlockStart += TILE_SIZE) {
                const int rightBlockEnd = std::min(rightBlockStart + TILE_SIZE, nextEnd);
                if (x[rightBlockStart] > leftBlockMaxX + mMaxDiameter) {
                    break;
                }

                int checkedPairs = 0;
                for (int first = leftBlockStart; first < leftBlockEnd; first++) {
                    testRange(first, rightBlockStart, rightBlockEnd, checkedPairs);
                }

                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats) && checkedPairs > 0) {
                    mStats.candidatePairs += checkedPairs;
                    mStats.nextRowCandidatePairs += checkedPairs;
                    mStats.nextRowCandidateRanges++;
                }
            }
        }
    }
}

void CellSweepBroadphase::collectOverlappingPairsScalarYPrune(ExactOverlapPairBuffer& output, bool collectStats) {
    output.clear();
    const int rowSpan = mCellCountsByRow.empty() ? 0 : static_cast<int>(mCellCountsByRow.size());
    const int* const cellIds = mOrderedCellIds.data();
    const double* const x = mOrderedX.data();
    const double* const y = mOrderedY.data();
    const double* const radius = mOrderedRadius.data();

    auto scanRange = [&](int first,
                             int secondBegin,
                             int secondEnd,
                             long long& candidateCounter,
                             long long& rangeCounter) {
        const int candidateCount = secondEnd - secondBegin;
        if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
            mStats.candidatePairs += candidateCount;
            candidateCounter += candidateCount;
            mStats.scalarChecks += candidateCount;
            if (candidateCount > 0) {
                rangeCounter++;
            }
        }
        const double leftX = x[first];
        const double leftY = y[first];
        const double leftRadius = radius[first];
        for (int second = secondBegin; second < secondEnd; second++) {
            const double rightRadius = radius[second];
            const double contactDistance = leftRadius + rightRadius;
            const double distanceY = leftY - y[second];
            const double distanceYSq = distanceY * distanceY;
            const double contactDistanceSq = contactDistance * contactDistance;
            if (distanceYSq >= contactDistanceSq) {
                continue;
            }
            const double distanceX = leftX - x[second];
            const double distanceSq = distanceX * distanceX + distanceYSq;
            if (distanceSq >= contactDistanceSq) {
                continue;
            }
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.exactPairs++;
            }
            output.push(cellIds[first], cellIds[second], distanceX, distanceY);
        }
    };

    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = x[first];
            const double leftRadius = radius[first];
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            int sameEnd = first + 1;
            while (sameEnd < rowEnd && x[sameEnd] < stopXRight) {
                sameEnd++;
            }
            scanRange(first,
                    first + 1,
                    sameEnd,
                    mStats.sameRowCandidatePairs,
                    mStats.sameRowCandidateRanges);
        }
    }

    for (int rowIndex = 0; rowIndex + 1 < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        const int nextStart = mRowStartOffsets[rowIndex + 1];
        const int nextEnd = mRowStartOffsets[rowIndex + 2];
        int upperCursor = nextStart;
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = x[first];
            const double leftRadius = radius[first];
            const double stopXLeft = leftX - mMaxDiameter;
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            while (upperCursor < nextEnd && x[upperCursor] < stopXLeft) {
                upperCursor++;
            }
            int upperEnd = upperCursor;
            while (upperEnd < nextEnd && x[upperEnd] < stopXRight) {
                upperEnd++;
            }
            scanRange(first,
                    upperCursor,
                    upperEnd,
                    mStats.nextRowCandidatePairs,
                    mStats.nextRowCandidateRanges);
        }
    }
}

void CellSweepBroadphase::collectOverlappingPairsSimdExact(ExactOverlapPairBuffer& output, bool collectStats) {
    output.clear();
    const int rowSpan = mCellCountsByRow.empty() ? 0 : static_cast<int>(mCellCountsByRow.size());
    const int* const cellIds = mOrderedCellIds.data();
    const double* const x = mOrderedX.data();
    const double* const y = mOrderedY.data();
    const double* const radius = mOrderedRadius.data();

    const hn::CappedTag<double, 4> d;
    const std::size_t laneCount = hn::Lanes(d);
    HWY_ALIGN double distanceXLanes[4];
    HWY_ALIGN double distanceYLanes[4];
    HWY_ALIGN uint8_t overlapBits[8];

    auto scanRange = [&](int first,
                             int secondBegin,
                             int secondEnd,
                             long long& candidateCounter,
                             long long& rangeCounter) {
        const int candidateCount = secondEnd - secondBegin;
        if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
            mStats.candidatePairs += candidateCount;
            candidateCounter += candidateCount;
            if (candidateCount > 0) {
                rangeCounter++;
            }
        }
        if (candidateCount <= 0) {
            return;
        }

        const double leftX = x[first];
        const double leftY = y[first];
        const double leftRadius = radius[first];
        const auto leftXV = hn::Set(d, leftX);
        const auto leftYV = hn::Set(d, leftY);
        const auto leftRadiusV = hn::Set(d, leftRadius);

        int second = secondBegin;
        const int simdEnd = secondBegin + static_cast<int>(candidateCount / static_cast<int>(laneCount)) * static_cast<int>(laneCount);
        for (; second < simdEnd; second += static_cast<int>(laneCount)) {
            const auto rightX = hn::LoadU(d, x + second);
            const auto rightY = hn::LoadU(d, y + second);
            const auto rightRadius = hn::LoadU(d, radius + second);
            const auto distanceX = hn::Sub(leftXV, rightX);
            const auto distanceY = hn::Sub(leftYV, rightY);
            const auto contactDistance = hn::Add(leftRadiusV, rightRadius);
            const auto distanceSq = hn::MulAdd(distanceX, distanceX, hn::Mul(distanceY, distanceY));
            const auto contactDistanceSq = hn::Mul(contactDistance, contactDistance);
            const auto overlapMask = hn::Lt(distanceSq, contactDistanceSq);

            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.simdChecks += static_cast<long long>(laneCount);
                mStats.exactPairs += static_cast<long long>(hn::CountTrue(d, overlapMask));
            }
            if (hn::AllFalse(d, overlapMask)) {
                continue;
            }

            hn::Store(distanceX, d, distanceXLanes);
            hn::Store(distanceY, d, distanceYLanes);
            hn::StoreMaskBits(d, overlapMask, overlapBits);
            for (std::size_t lane = 0; lane < laneCount; lane++) {
                const uint8_t bit = static_cast<uint8_t>(1U << (lane & 7U));
                if ((overlapBits[lane >> 3U] & bit) == 0) {
                    continue;
                }
                output.push(cellIds[first],
                        cellIds[second + static_cast<int>(lane)],
                        distanceXLanes[lane],
                        distanceYLanes[lane]);
            }
        }

        if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
            mStats.scalarChecks += secondEnd - second;
        }
        for (; second < secondEnd; second++) {
            const double distanceX = leftX - x[second];
            const double distanceY = leftY - y[second];
            const double distanceSq = distanceX * distanceX + distanceY * distanceY;
            const double rightRadius = radius[second];
            const double contactDistance = leftRadius + rightRadius;
            if (distanceSq >= contactDistance * contactDistance) {
                continue;
            }
            if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                mStats.exactPairs++;
            }
            output.push(cellIds[first], cellIds[second], distanceX, distanceY);
        }
    };

    for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = x[first];
            const double leftRadius = radius[first];
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            int sameEnd = first + 1;
            while (sameEnd < rowEnd && x[sameEnd] < stopXRight) {
                sameEnd++;
            }
            scanRange(first,
                    first + 1,
                    sameEnd,
                    mStats.sameRowCandidatePairs,
                    mStats.sameRowCandidateRanges);
        }
    }

    for (int rowIndex = 0; rowIndex + 1 < rowSpan; rowIndex++) {
        const int rowStart = mRowStartOffsets[rowIndex];
        const int rowEnd = mRowStartOffsets[rowIndex + 1];
        const int nextStart = mRowStartOffsets[rowIndex + 1];
        const int nextEnd = mRowStartOffsets[rowIndex + 2];
        int upperCursor = nextStart;
        for (int first = rowStart; first < rowEnd; first++) {
            const double leftX = x[first];
            const double leftRadius = radius[first];
            const double stopXLeft = leftX - mMaxDiameter;
            const double stopXRight = leftX + leftRadius + mMaxRadius;
            while (upperCursor < nextEnd && x[upperCursor] < stopXLeft) {
                upperCursor++;
            }
            int upperEnd = upperCursor;
            while (upperEnd < nextEnd && x[upperEnd] < stopXRight) {
                upperEnd++;
            }
            scanRange(first,
                    upperCursor,
                    upperEnd,
                    mStats.nextRowCandidatePairs,
                    mStats.nextRowCandidateRanges);
        }
    }
}

const CellSweepBroadphase::Stats& CellSweepBroadphase::stats() const noexcept {
    return mStats;
}

std::int32_t CellSweepBroadphase::rowOf(double y) const noexcept {
    return static_cast<std::int32_t>(std::floor(y * mInvMaxDiameter));
}
