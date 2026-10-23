#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

class CellData;

// Incremental f64 broadphase ported from the independently validated
// PackedSource32/bucketed-row implementation. Spatial records stay grouped by
// Y row and sorted by X. Most row changes are routed directly between adjacent
// rows; births and long-distance moves use a cold fallback path.
class CellIncrementalBucketedF64Broadphase {
public:
    struct alignas(32) PackedSource32 {
        double x = 0.0;
        double y = 0.0;
        double radius = 0.0;
        double padding = 0.0;
    };

    struct alignas(8) RowRecord {
        double x = 0.0;
        double y = 0.0;
        double radius = 0.0;
        std::uint32_t cellId = 0;
    };

    struct FallbackRecord {
        std::int32_t targetRow = 0;
        RowRecord record;
    };

    static_assert(sizeof(PackedSource32) == 32);
    static_assert(alignof(PackedSource32) == 32);
    static_assert(sizeof(RowRecord) == 32);
    static_assert(alignof(RowRecord) == 8);
    static_assert(sizeof(FallbackRecord) == 40);
    static_assert(std::is_trivially_copyable_v<PackedSource32>);
    static_assert(std::is_trivially_copyable_v<RowRecord>);

    struct Stats {
        long long candidatePairs = 0;
        long long sameRowCandidatePairs = 0;
        long long nextRowCandidatePairs = 0;
        long long sameRowCandidateRanges = 0;
        long long nextRowCandidateRanges = 0;
        long long exactPairs = 0;
        long long transferredRecords = 0;
        long long adjacentTransfers = 0;
        long long fallbackTransfers = 0;
        long long fullRebuilds = 0;

        void clear() noexcept;
    };

    explicit CellIncrementalBucketedF64Broadphase(double maxDiameter);

    void setMaxDiameter(double maxDiameter);
    void clear();
    // A committed prepared source is consumed by one build only. Direct
    // callers that do not prepare it keep using the CellData staging fallback.
    [[nodiscard]] PackedSource32* beginPreparedSourceWrite(
            std::size_t cellCount);
    void commitPreparedSourceWrite() noexcept;
    [[nodiscard]] bool tryBuildPreparedSource(
            std::size_t expectedCellCount,
            bool collectStats = false);
    void build(const CellData& cellData, bool collectStats = false);

    template <typename VisitorFn>
    void forEachOverlappingPairInline(VisitorFn&& visitor,
            bool collectStats = false) {
        prepareScanSentinels();
        ScanSentinelGuard sentinelGuard(*this);

#ifdef CELLULARITY_PROFILE
        long long sameRowCandidatePairs = 0;
        long long nextRowCandidatePairs = 0;
        long long sameRowCandidateRanges = 0;
        long long nextRowCandidateRanges = 0;
        long long exactPairs = 0;
#else
        (void)collectStats;
#endif

        for (const RowBucket& row : mRows) {
            const std::size_t recordCount = row.records.size() - 1U;
            const RowRecord* const records = row.records.data();
            for (std::size_t first = 0; first < recordCount; first++) {
                const RowRecord left = records[first];
                const double stopXRight = left.x + left.radius + mMaxRadius;
                std::size_t second = first + 1U;
                for (;;) {
                    const RowRecord right = records[second];
                    if (right.x >= stopXRight) {
                        break;
                    }
                    const double distanceX = left.x - right.x;
                    const double distanceY = left.y - right.y;
                    const double distanceSq =
                            distanceX * distanceX + distanceY * distanceY;
                    const double contactDistance = left.radius + right.radius;
                    if (distanceSq < contactDistance * contactDistance) {
#ifdef CELLULARITY_PROFILE
                        if (collectStats) {
                            exactPairs++;
                        }
#endif
                        visitor(static_cast<int>(left.cellId),
                                static_cast<int>(right.cellId),
                                distanceX,
                                distanceY,
                                distanceSq,
                                left.radius,
                                right.radius);
                    }
                    second++;
                }
#ifdef CELLULARITY_PROFILE
                if (collectStats) {
                    const long long candidateCount =
                            static_cast<long long>(second - (first + 1U));
                    sameRowCandidatePairs += candidateCount;
                    sameRowCandidateRanges += candidateCount > 0 ? 1 : 0;
                }
#endif
            }
        }

        for (std::size_t rowIndex = 0; rowIndex + 1U < mRows.size(); rowIndex++) {
            const std::vector<RowRecord>& lower = mRows[rowIndex].records;
            const RowRecord* const upperRecords = mRows[rowIndex + 1U].records.data();
            const std::size_t lowerCount = lower.size() - 1U;
            std::size_t upperCursor = 0;
            for (std::size_t first = 0; first < lowerCount; first++) {
                const RowRecord left = lower[first];
                const double stopXLeft = left.x - mMaxDiameter;
                const double stopXRight = left.x + left.radius + mMaxRadius;
                while (upperRecords[upperCursor].x < stopXLeft) {
                    upperCursor++;
                }
                std::size_t upperEnd = upperCursor;
                for (;;) {
                    const RowRecord right = upperRecords[upperEnd];
                    if (right.x >= stopXRight) {
                        break;
                    }
                    const double distanceX = left.x - right.x;
                    const double distanceY = left.y - right.y;
                    const double distanceSq =
                            distanceX * distanceX + distanceY * distanceY;
                    const double contactDistance = left.radius + right.radius;
                    if (distanceSq < contactDistance * contactDistance) {
#ifdef CELLULARITY_PROFILE
                        if (collectStats) {
                            exactPairs++;
                        }
#endif
                        visitor(static_cast<int>(left.cellId),
                                static_cast<int>(right.cellId),
                                distanceX,
                                distanceY,
                                distanceSq,
                                left.radius,
                                right.radius);
                    }
                    upperEnd++;
                }
#ifdef CELLULARITY_PROFILE
                if (collectStats) {
                    const long long candidateCount =
                            static_cast<long long>(upperEnd - upperCursor);
                    nextRowCandidatePairs += candidateCount;
                    nextRowCandidateRanges += candidateCount > 0 ? 1 : 0;
                }
#endif
            }
        }

#ifdef CELLULARITY_PROFILE
        if (collectStats) {
            mStats.sameRowCandidatePairs = sameRowCandidatePairs;
            mStats.nextRowCandidatePairs = nextRowCandidatePairs;
            mStats.candidatePairs = sameRowCandidatePairs + nextRowCandidatePairs;
            mStats.sameRowCandidateRanges = sameRowCandidateRanges;
            mStats.nextRowCandidateRanges = nextRowCandidateRanges;
            mStats.exactPairs = exactPairs;
        }
#endif
    }

    template <typename VisitorFn>
    void forEachCandidatePairInline(VisitorFn&& visitor) const {
        for (const RowBucket& row : mRows) {
            const std::vector<RowRecord>& records = row.records;
            for (std::size_t first = 0; first < records.size(); first++) {
                const RowRecord left = records[first];
                const double stopXRight = left.x + left.radius + mMaxRadius;
                std::size_t second = first + 1U;
                while (second < records.size() && records[second].x < stopXRight) {
                    visitor(static_cast<int>(left.cellId),
                            static_cast<int>(records[second].cellId));
                    second++;
                }
            }
        }

        for (std::size_t rowIndex = 0; rowIndex + 1U < mRows.size(); rowIndex++) {
            const std::vector<RowRecord>& lower = mRows[rowIndex].records;
            const std::vector<RowRecord>& upper = mRows[rowIndex + 1U].records;
            std::size_t upperCursor = 0;
            for (const RowRecord left : lower) {
                const double stopXLeft = left.x - mMaxDiameter;
                const double stopXRight = left.x + left.radius + mMaxRadius;
                while (upperCursor < upper.size() && upper[upperCursor].x < stopXLeft) {
                    upperCursor++;
                }
                std::size_t upperEnd = upperCursor;
                while (upperEnd < upper.size() && upper[upperEnd].x < stopXRight) {
                    visitor(static_cast<int>(left.cellId),
                            static_cast<int>(upper[upperEnd].cellId));
                    upperEnd++;
                }
            }
        }
    }

    [[nodiscard]] const Stats& stats() const noexcept;

private:
    struct RowBucket {
        std::vector<RowRecord> records;
        std::vector<RowRecord> incomingFromBelow;
        std::vector<RowRecord> incomingFromAbove;
        std::vector<RowRecord> fallbackIncoming;

        void clear();
        void clearIncoming();
        [[nodiscard]] bool hasIncoming() const noexcept;
    };

    enum class RowDestinationKind : std::uint8_t {
        CURRENT,
        BELOW,
        ABOVE,
        FAR
    };

    struct RowDestination {
        RowDestinationKind kind = RowDestinationKind::CURRENT;
        std::int32_t targetRow = 0;
    };

    class ScanSentinelGuard {
    public:
        explicit ScanSentinelGuard(CellIncrementalBucketedF64Broadphase& owner) noexcept
                : mOwner(owner) {}

        ~ScanSentinelGuard() {
            mOwner.removeScanSentinels();
        }

        ScanSentinelGuard(const ScanSentinelGuard&) = delete;
        ScanSentinelGuard& operator=(const ScanSentinelGuard&) = delete;

    private:
        CellIncrementalBucketedF64Broadphase& mOwner;
    };

    inline static constexpr RowRecord ROW_SCAN_SENTINEL = {
            std::numeric_limits<double>::infinity(),
            0.0,
            0.0,
            0
    };

    [[nodiscard]] static bool recordLess(const RowRecord& left,
            const RowRecord& right) noexcept;
    static void insertionSortRecords(std::vector<RowRecord>& records) noexcept;
    static void mergeSortedRecords(const std::vector<RowRecord>& left,
            const std::vector<RowRecord>& right,
            std::vector<RowRecord>& output);
    void mergeIncomingIntoRow(RowBucket& row);
    [[nodiscard]] RowDestination classifyRow(double y,
            std::int32_t expectedRow) const;
    [[nodiscard]] std::int32_t rowOf(double y) const;
    [[nodiscard]] std::int32_t rowOfScaled(double scaledY) const;
    [[nodiscard]] RowRecord recordFromSource(std::size_t cellId) const noexcept;
    void buildFromPackedSource(bool collectStats);
    void rebuildAll();
    void ensureRowSpan(std::int32_t requestedMin, std::int32_t requestedMax);
#ifndef NDEBUG
    void validateLayout();
#endif
    void prepareScanSentinels() {
        std::size_t preparedRows = 0;
        try {
            for (RowBucket& row : mRows) {
                row.records.push_back(ROW_SCAN_SENTINEL);
                preparedRows++;
            }
        } catch (...) {
            for (std::size_t rowIndex = 0; rowIndex < preparedRows; rowIndex++) {
                mRows[rowIndex].records.pop_back();
            }
            throw;
        }
    }

    void removeScanSentinels() noexcept {
        for (RowBucket& row : mRows) {
            assert(!row.records.empty());
            assert(row.records.back().x
                    == std::numeric_limits<double>::infinity());
            row.records.pop_back();
        }
    }

    double mMaxDiameter;
    double mInvMaxDiameter;
    double mMaxRadius;
    std::vector<PackedSource32> mPackedSource;
    std::vector<RowBucket> mRows;
    std::int32_t mMinRow = 0;
    std::size_t mPreviousCount = 0;
    bool mInitialized = false;
    std::vector<FallbackRecord> mFallback;
    std::vector<RowRecord> mIncomingScratch;
    std::vector<RowRecord> mMergeScratch;
#ifndef NDEBUG
    std::vector<std::uint8_t> mValidationSeen;
#endif
    bool mPreparedSourceReady = false;
    Stats mStats;
};
