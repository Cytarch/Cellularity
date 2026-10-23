#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "core/CellIncrementalBucketedF64Broadphase.h"

#ifdef CELLULARITY_PROFILE
#define CELLULARITY_BROADPHASE_COLLECT_STATS(collect_stats) (collect_stats)
#else
#define CELLULARITY_BROADPHASE_COLLECT_STATS(collect_stats) ((void)(collect_stats), false)
#endif

class CellData;

struct ExactOverlapPairBuffer {
    std::vector<int> firstCellIds;
    std::vector<int> secondCellIds;
    std::vector<double> distanceX;
    std::vector<double> distanceY;

    void clear();
    void reserve(std::size_t capacity);
    void resize(std::size_t size);
    [[nodiscard]] std::size_t size() const noexcept;
    void push(int firstCellIndex,
            int secondCellIndex,
            double pairDistanceX,
            double pairDistanceY);
};

enum class CellOverlapScanAlgorithm {
    BASELINE_SWEEP,
    BASELINE_FAST_SWEEP,
    TIGHT_RADIUS_SWEEP,
    ROW_MAX_RADIUS_SWEEP,
    SIMD_EXACT_SWEEP,
    SCALAR_Y_PRUNE_SWEEP,
    SCALAR_TILE_SWEEP,
    SIMD_TILE_SWEEP,
    COMPACT_RECORD_SWEEP,
    COMPACT_PRIMARY_SWEEP,
    INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP
};

class CellSweepBroadphase {
public:
    struct CompactSpatialRecord {
        double x = 0.0;
        double y = 0.0;
        double radius = 0.0;
        int cellId = -1;
        std::int32_t row = 0;
    };

    static_assert(sizeof(CompactSpatialRecord) == 4 * sizeof(double));
    static_assert(alignof(CompactSpatialRecord) == alignof(double));

    struct Stats {
        long long candidatePairs = 0;
        long long sameRowCandidatePairs = 0;
        long long nextRowCandidatePairs = 0;
        long long sameRowCandidateRanges = 0;
        long long nextRowCandidateRanges = 0;
        long long exactPairs = 0;
        long long scalarChecks = 0;
        long long simdChecks = 0;
        long long rowsReused = 0;
        long long rowRelayouts = 0;
        long long rowChangedCells = 0;
        long long layoutRebuilds = 0;
        long long insertionSortRows = 0;
        long long fullSortRows = 0;
        long long insertionMoves = 0;
        long long compactProjectionNanos = 0;
        long long compactClassifyNanos = 0;
        long long compactLayoutNanos = 0;
        long long compactScatterNanos = 0;
        long long compactMaterializeNanos = 0;
        long long compactSortNanos = 0;
        long long compactFinalizeNanos = 0;
        long long compactPayloadRefreshes = 0;
        long long compactScatterIdWrites = 0;
        long long compactRecordMaterializations = 0;
        long long compactRecordSortWrites = 0;
        long long compactFullSortInputRecords = 0;

        void clear();
    };

    using Visitor = void (*)(void* context, int firstCellIndex, int secondCellIndex);
    using OverlapVisitor = void (*)(void* context,
            int firstCellIndex,
            int secondCellIndex,
            double distanceX,
            double distanceY,
            double distanceSq,
            double firstRadius,
            double secondRadius);

    explicit CellSweepBroadphase(double maxDiameter);

    void setMaxDiameter(double maxDiameter);
    void setScanAlgorithm(CellOverlapScanAlgorithm algorithm) noexcept;
    [[nodiscard]] CellOverlapScanAlgorithm scanAlgorithm() const noexcept;
    void clear();
    // Host tests and benchmarks use the runtime-selected algorithm. Production
    // code calls buildConfigured(), whose choice is fixed at compile time.
    void buildSelectedLayout(const CellData& cellData, bool collectStats = false);
    void buildBaseline(const CellData& cellData, bool collectStats = false);
    void buildConfigured(const CellData& cellData, bool collectStats = false);
    [[nodiscard]] CellIncrementalBucketedF64Broadphase::PackedSource32*
    beginIncrementalPackedSourceWrite(std::size_t cellCount);
    void commitIncrementalPackedSourceWrite() noexcept;
    void forEachCandidatePair(void* context, Visitor visitor) const;
    void forEachOverlappingPair(void* context, OverlapVisitor visitor, bool collectStats = false);

    template <bool CheckCompactRecords = true, typename VisitorFn>
    void forEachOverlappingPairInline(VisitorFn&& visitor, bool collectStats = false) {
        if constexpr (CheckCompactRecords) {
            if (mScanAlgorithm
                    == CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP) {
                mIncrementalBucketedF64Broadphase.forEachOverlappingPairInline(
                        visitor, collectStats);
#ifdef CELLULARITY_PROFILE
                syncIncrementalBucketedF64Stats(collectStats);
#endif
                return;
            }
            if (compactRecordsReady()) {
                forEachOverlappingPairCompactRecordsInline(visitor, collectStats);
                return;
            }
        }
        const int rowSpan = mCellCountsByRow.empty() ? 0 : static_cast<int>(mCellCountsByRow.size());
        for (int rowIndex = 0; rowIndex < rowSpan; rowIndex++) {
            const int rowStart = mRowStartOffsets[rowIndex];
            const int rowEnd = mRowStartOffsets[rowIndex + 1];
            for (int first = rowStart; first < rowEnd; first++) {
                const double leftX = mOrderedX[first];
                const double leftY = mOrderedY[first];
                const double leftRadius = mOrderedRadius[first];
                int sameEnd = first + 1;
                if (mUsesSuffixRadiusWindow) {
                    while (sameEnd < rowEnd
                            && mOrderedX[sameEnd] < leftX + leftRadius + mSuffixMaxRadius[sameEnd]) {
                        sameEnd++;
                    }
                } else if (mUsesRowMaxRadiusWindow) {
                    const double stopXRight = leftX + leftRadius + mMaxRadiusByRow[rowIndex];
                    while (sameEnd < rowEnd && mOrderedX[sameEnd] < stopXRight) {
                        sameEnd++;
                    }
                } else {
                    const double stopXRight = leftX + leftRadius + mMaxRadius;
                    while (sameEnd < rowEnd && mOrderedX[sameEnd] < stopXRight) {
                        sameEnd++;
                    }
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
                    visitor(mOrderedCellIds[first],
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
                while (upperCursor < nextEnd && mOrderedX[upperCursor] < stopXLeft) {
                    upperCursor++;
                }
                int upperEnd = upperCursor;
                if (mUsesSuffixRadiusWindow) {
                    while (upperEnd < nextEnd
                            && mOrderedX[upperEnd] < leftX + leftRadius + mSuffixMaxRadius[upperEnd]) {
                        upperEnd++;
                    }
                } else if (mUsesRowMaxRadiusWindow) {
                    const double stopXRight = leftX + leftRadius + mMaxRadiusByRow[rowIndex + 1];
                    while (upperEnd < nextEnd && mOrderedX[upperEnd] < stopXRight) {
                        upperEnd++;
                    }
                } else {
                    const double stopXRight = leftX + leftRadius + mMaxRadius;
                    while (upperEnd < nextEnd && mOrderedX[upperEnd] < stopXRight) {
                        upperEnd++;
                    }
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
                    visitor(mOrderedCellIds[first],
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

    template <typename VisitorFn>
    void forEachOverlappingPairConfiguredInline(VisitorFn&& visitor,
            bool collectStats = false) {
#if defined(CELLULARITY_RUNTIME_OVERLAP_SELECTION)
        forEachOverlappingPairInline(visitor, collectStats);
#elif defined(CELLULARITY_DEFAULT_INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP)
        mIncrementalBucketedF64Broadphase.forEachOverlappingPairInline(
                visitor, collectStats);
#ifdef CELLULARITY_PROFILE
        syncIncrementalBucketedF64Stats(collectStats);
#endif
#elif defined(CELLULARITY_DEFAULT_COMPACT_RECORD_SWEEP) \
        || defined(CELLULARITY_DEFAULT_COMPACT_PRIMARY_SWEEP)
        forEachOverlappingPairCompactRecordsInline(visitor, collectStats);
#else
        forEachOverlappingPairInline<false>(visitor, collectStats);
#endif
    }

    void collectOverlappingPairs(ExactOverlapPairBuffer& output, bool collectStats = false);
    void collectOverlappingPairsConfigured(ExactOverlapPairBuffer& output,
            bool collectStats = false);
    [[nodiscard]] const Stats& stats() const noexcept;

private:
    [[nodiscard]] std::int32_t rowOf(double y) const noexcept;
    [[nodiscard]] bool compactRecordsReady() const noexcept;
    void syncIncrementalBucketedF64Stats(bool collectStats) noexcept;
    void refreshProjectedCompactRecords(bool collectStats);
    void buildCompactPrimary(const CellData& cellData, bool collectStats);
    template <bool BuildCompactProjection>
    void buildBaselineSpatialLayout(const CellData& cellData, bool collectStats);
    void writeCompactSentinel(int cellCount) noexcept;
    void refreshRadiusWindowMetadata();
    void collectOverlappingPairsBaselineFast(ExactOverlapPairBuffer& output, bool collectStats);
    void collectOverlappingPairsSimdExact(ExactOverlapPairBuffer& output, bool collectStats);
    void collectOverlappingPairsScalarYPrune(ExactOverlapPairBuffer& output, bool collectStats);
    void collectOverlappingPairsScalarTile(ExactOverlapPairBuffer& output, bool collectStats);
    void collectOverlappingPairsSimdTile(ExactOverlapPairBuffer& output, bool collectStats);

    template <typename VisitorFn>
    void forEachOverlappingPairCompactRecordsInline(VisitorFn& visitor, bool collectStats) {
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
                const CompactSpatialRecord right = records[second];
                const double distanceX = left.x - right.x;
                const double distanceY = left.y - right.y;
                const double distanceSq = distanceX * distanceX + distanceY * distanceY;
                const double contactDistance = left.radius + right.radius;
                if (distanceSq >= contactDistance * contactDistance) {
                    continue;
                }
                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                    mStats.exactPairs++;
                }
                visitor(left.cellId,
                        right.cellId,
                        distanceX,
                        distanceY,
                        distanceSq,
                        left.radius,
                        right.radius);
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
                const CompactSpatialRecord right = records[second];
                const double distanceX = left.x - right.x;
                const double distanceY = left.y - right.y;
                const double distanceSq = distanceX * distanceX + distanceY * distanceY;
                const double contactDistance = left.radius + right.radius;
                if (distanceSq >= contactDistance * contactDistance) {
                    continue;
                }
                if (CELLULARITY_BROADPHASE_COLLECT_STATS(collectStats)) {
                    mStats.exactPairs++;
                }
                visitor(left.cellId,
                        right.cellId,
                        distanceX,
                        distanceY,
                        distanceSq,
                        left.radius,
                        right.radius);
            }
        }
    }

    double mMaxDiameter;
    double mInvMaxDiameter;
    double mMaxRadius;

    // Metadata shared by the baseline SoA and compact-primary builders.
    std::vector<std::int32_t> mCellRowsById;
    std::vector<int> mCellCountsByRow;
    std::vector<int> mRowStartOffsets;
    std::vector<int> mRowWriteOffsets;
    std::vector<int> mChangedCellCountsByRow;

    // Baseline row-major ordering and its reusable relayout scratch.
    std::vector<int> mOrderedCellIds;
    std::vector<int> mRelayoutCellIds;

    // Compact-primary storage and ID-only relayout scratch. The final element
    // in mCompactRecords is a sentinel and is not included in mCompactCellCount.
    std::vector<int> mCompactRelayoutCellIds;
    std::vector<CompactSpatialRecord> mCompactRecords;

    // Numeric streams belonging to the baseline row-major SoA.
    std::vector<double> mOrderedX;
    std::vector<double> mOrderedY;
    std::vector<double> mOrderedRadius;
    std::vector<double> mSuffixMaxRadius;
    std::vector<double> mMaxRadiusByRow;

    std::int32_t mMinRow = 0;
    bool mUsesSuffixRadiusWindow = false;
    bool mUsesRowMaxRadiusWindow = false;
    bool mCompactPreviousBuildRelayout = false;
    int mCompactCellCount = 0;
    CellOverlapScanAlgorithm mScanAlgorithm = CellOverlapScanAlgorithm::BASELINE_SWEEP;
    CellIncrementalBucketedF64Broadphase mIncrementalBucketedF64Broadphase;
    Stats mStats;
};
