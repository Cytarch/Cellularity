#include "core/CellIncrementalBucketedF64Broadphase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "core/CellData.h"

void CellIncrementalBucketedF64Broadphase::Stats::clear() noexcept {
    candidatePairs = 0;
    sameRowCandidatePairs = 0;
    nextRowCandidatePairs = 0;
    sameRowCandidateRanges = 0;
    nextRowCandidateRanges = 0;
    exactPairs = 0;
    transferredRecords = 0;
    adjacentTransfers = 0;
    fallbackTransfers = 0;
    fullRebuilds = 0;
}

void CellIncrementalBucketedF64Broadphase::RowBucket::clear() {
    records.clear();
    clearIncoming();
}

void CellIncrementalBucketedF64Broadphase::RowBucket::clearIncoming() {
    incomingFromBelow.clear();
    incomingFromAbove.clear();
    fallbackIncoming.clear();
}

bool CellIncrementalBucketedF64Broadphase::RowBucket::hasIncoming() const noexcept {
    return !incomingFromBelow.empty()
            || !incomingFromAbove.empty()
            || !fallbackIncoming.empty();
}

CellIncrementalBucketedF64Broadphase::CellIncrementalBucketedF64Broadphase(double maxDiameter)
        : mMaxDiameter(maxDiameter),
          mInvMaxDiameter(1.0 / maxDiameter),
          mMaxRadius(0.5 * maxDiameter) {
    if (!std::isfinite(maxDiameter) || !(maxDiameter > 0.0)) {
        throw std::invalid_argument(
                "CellIncrementalBucketedF64Broadphase requires a finite positive maxDiameter");
    }
}

void CellIncrementalBucketedF64Broadphase::setMaxDiameter(double maxDiameter) {
    if (!std::isfinite(maxDiameter) || !(maxDiameter > 0.0)) {
        throw std::invalid_argument(
                "CellIncrementalBucketedF64Broadphase requires a finite positive maxDiameter");
    }
    if (mMaxDiameter == maxDiameter) {
        return;
    }
    mMaxDiameter = maxDiameter;
    mInvMaxDiameter = 1.0 / maxDiameter;
    mMaxRadius = 0.5 * maxDiameter;
    clear();
}

void CellIncrementalBucketedF64Broadphase::clear() {
    mPackedSource.clear();
    mRows.clear();
    mFallback.clear();
    mIncomingScratch.clear();
    mMergeScratch.clear();
    mMinRow = 0;
    mPreviousCount = 0;
    mInitialized = false;
    mPreparedSourceReady = false;
    mStats.clear();
}

CellIncrementalBucketedF64Broadphase::PackedSource32*
CellIncrementalBucketedF64Broadphase::beginPreparedSourceWrite(
        std::size_t cellCount) {
    mPreparedSourceReady = false;
    mPackedSource.resize(cellCount);
    return mPackedSource.data();
}

void CellIncrementalBucketedF64Broadphase::commitPreparedSourceWrite() noexcept {
    mPreparedSourceReady = true;
}

bool CellIncrementalBucketedF64Broadphase::tryBuildPreparedSource(
        std::size_t expectedCellCount,
        bool collectStats) {
    if (!mPreparedSourceReady || mPackedSource.size() != expectedCellCount) {
        return false;
    }
    mPreparedSourceReady = false;
    buildFromPackedSource(collectStats);
    return true;
}

void CellIncrementalBucketedF64Broadphase::build(const CellData& cellData,
        bool collectStats) {
    mPreparedSourceReady = false;
    const std::size_t cellCount = static_cast<std::size_t>(cellData.mCellCount);
    assert(cellCount <= std::numeric_limits<std::uint32_t>::max());
    mPackedSource.resize(cellCount);

    const double* const positionX = cellData.mPositionX.data();
    const double* const positionY = cellData.mPositionY.data();
    const double* const radius = cellData.mRadius.data();
    PackedSource32* const destination = mPackedSource.data();
    for (std::size_t cellId = 0; cellId < cellCount; cellId++) {
        destination[cellId] = {
                positionX[cellId],
                positionY[cellId],
                radius[cellId],
                0.0
        };
    }

    buildFromPackedSource(collectStats);
}

bool CellIncrementalBucketedF64Broadphase::recordLess(const RowRecord& left,
        const RowRecord& right) noexcept {
    if (left.x < right.x) {
        return true;
    }
    if (left.x > right.x) {
        return false;
    }
    // X is finite. The only numerically equal values with distinct total
    // ordering are -0.0 and +0.0; Rust's f64::total_cmp orders -0.0 first.
    if (std::signbit(left.x) != std::signbit(right.x)) {
        return std::signbit(left.x);
    }
    return left.cellId < right.cellId;
}

void CellIncrementalBucketedF64Broadphase::insertionSortRecords(
        std::vector<RowRecord>& records) noexcept {
    for (std::size_t slot = 1; slot < records.size(); slot++) {
        const RowRecord key = records[slot];
        std::size_t write = slot;
        while (write > 0 && recordLess(key, records[write - 1U])) {
            records[write] = records[write - 1U];
            write--;
        }
        if (write != slot) {
            records[write] = key;
        }
    }
}

void CellIncrementalBucketedF64Broadphase::mergeSortedRecords(
        const std::vector<RowRecord>& left,
        const std::vector<RowRecord>& right,
        std::vector<RowRecord>& output) {
    output.clear();
    output.reserve(left.size() + right.size());
    std::size_t leftIndex = 0;
    std::size_t rightIndex = 0;
    while (leftIndex < left.size() && rightIndex < right.size()) {
        if (!recordLess(right[rightIndex], left[leftIndex])) {
            output.push_back(left[leftIndex]);
            leftIndex++;
        } else {
            output.push_back(right[rightIndex]);
            rightIndex++;
        }
    }
    output.insert(output.end(), left.begin() + static_cast<std::ptrdiff_t>(leftIndex), left.end());
    output.insert(output.end(),
            right.begin() + static_cast<std::ptrdiff_t>(rightIndex),
            right.end());
}

void CellIncrementalBucketedF64Broadphase::mergeIncomingIntoRow(RowBucket& row) {
    mergeSortedRecords(row.incomingFromBelow, row.incomingFromAbove, mIncomingScratch);
    if (!row.fallbackIncoming.empty()) {
        mergeSortedRecords(mIncomingScratch, row.fallbackIncoming, mMergeScratch);
        mIncomingScratch.swap(mMergeScratch);
    }
    mergeSortedRecords(row.records, mIncomingScratch, mMergeScratch);
    row.records.swap(mMergeScratch);
    row.clearIncoming();
}

CellIncrementalBucketedF64Broadphase::RowDestination
CellIncrementalBucketedF64Broadphase::classifyRow(double y,
        std::int32_t expectedRow) const {
    const double scaledY = y * mInvMaxDiameter;
    const double relativeRow = scaledY - static_cast<double>(expectedRow);
    if (relativeRow >= 0.0) {
        if (relativeRow < 1.0) {
            return {RowDestinationKind::CURRENT, expectedRow};
        }
        if (relativeRow < 2.0) {
            if (expectedRow == std::numeric_limits<std::int32_t>::max()) {
                return {RowDestinationKind::FAR, rowOfScaled(scaledY)};
            }
            return {RowDestinationKind::ABOVE,
                    static_cast<std::int32_t>(expectedRow + 1)};
        }
    } else if (relativeRow >= -1.0) {
        if (expectedRow == std::numeric_limits<std::int32_t>::min()) {
            return {RowDestinationKind::FAR, rowOfScaled(scaledY)};
        }
        return {RowDestinationKind::BELOW,
                static_cast<std::int32_t>(expectedRow - 1)};
    }

    const std::int32_t targetRow = rowOfScaled(scaledY);
    if (targetRow == expectedRow) {
        return {RowDestinationKind::CURRENT, targetRow};
    }
    if (expectedRow != std::numeric_limits<std::int32_t>::min()
            && targetRow == expectedRow - 1) {
        return {RowDestinationKind::BELOW, targetRow};
    }
    if (expectedRow != std::numeric_limits<std::int32_t>::max()
            && targetRow == expectedRow + 1) {
        return {RowDestinationKind::ABOVE, targetRow};
    }
    return {RowDestinationKind::FAR, targetRow};
}

std::int32_t CellIncrementalBucketedF64Broadphase::rowOf(double y) const {
    return rowOfScaled(y * mInvMaxDiameter);
}

std::int32_t CellIncrementalBucketedF64Broadphase::rowOfScaled(double scaledY) const {
    if (!std::isfinite(scaledY)) {
        throw std::overflow_error("Incremental broadphase row coordinate is not finite");
    }
    const double floored = std::floor(scaledY);
    if (floored < static_cast<double>(std::numeric_limits<std::int32_t>::min())
            || floored > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        throw std::overflow_error("Incremental broadphase row coordinate exceeds int32 range");
    }
    return static_cast<std::int32_t>(floored);
}

CellIncrementalBucketedF64Broadphase::RowRecord
CellIncrementalBucketedF64Broadphase::recordFromSource(std::size_t cellId) const noexcept {
    const PackedSource32 source = mPackedSource[cellId];
    assert(std::isfinite(source.x));
    assert(std::isfinite(source.y));
    assert(std::isfinite(source.radius));
    assert(source.radius >= 0.0 && source.radius <= mMaxRadius);
    return {
            source.x,
            source.y,
            source.radius,
            static_cast<std::uint32_t>(cellId)
    };
}

void CellIncrementalBucketedF64Broadphase::buildFromPackedSource(bool collectStats) {
#ifdef CELLULARITY_PROFILE
    if (collectStats) {
        mStats.clear();
    }
#else
    (void)collectStats;
#endif
    const std::size_t cellCount = mPackedSource.size();
    if (cellCount == 0) {
        for (RowBucket& row : mRows) {
            row.clear();
        }
        mFallback.clear();
        mPreviousCount = 0;
        mInitialized = true;
#ifndef NDEBUG
        validateLayout();
#endif
        return;
    }

    if (!mInitialized || mRows.empty()) {
        rebuildAll();
        return;
    }

    for (RowBucket& row : mRows) {
        assert(!row.hasIncoming());
        row.clearIncoming();
    }
    mFallback.clear();

    const std::int32_t oldMinRow = mMinRow;
#ifdef CELLULARITY_PROFILE
    long long adjacentTransfers = 0;
#endif
    for (std::size_t rowIndex = 0; rowIndex < mRows.size(); rowIndex++) {
        const std::int32_t expectedRow =
                static_cast<std::int32_t>(oldMinRow + static_cast<std::int32_t>(rowIndex));
        RowBucket& current = mRows[rowIndex];

        std::size_t write = 0;
        for (std::size_t read = 0; read < current.records.size(); read++) {
            const std::size_t cellId = current.records[read].cellId;
            if (cellId >= cellCount) {
                continue;
            }
            current.records[write] = recordFromSource(cellId);
            write++;
        }
        current.records.resize(write);
        insertionSortRecords(current.records);

        write = 0;
        for (std::size_t read = 0; read < current.records.size(); read++) {
            const RowRecord record = current.records[read];
            const RowDestination destination = classifyRow(record.y, expectedRow);
            switch (destination.kind) {
                case RowDestinationKind::CURRENT:
                    current.records[write] = record;
                    write++;
                    break;
                case RowDestinationKind::BELOW:
                    if (rowIndex > 0) {
                        mRows[rowIndex - 1U].incomingFromAbove.push_back(record);
#ifdef CELLULARITY_PROFILE
                        if (collectStats) {
                            adjacentTransfers++;
                        }
#endif
                    } else {
                        mFallback.push_back({destination.targetRow, record});
                    }
                    break;
                case RowDestinationKind::ABOVE:
                    if (rowIndex + 1U < mRows.size()) {
                        mRows[rowIndex + 1U].incomingFromBelow.push_back(record);
#ifdef CELLULARITY_PROFILE
                        if (collectStats) {
                            adjacentTransfers++;
                        }
#endif
                    } else {
                        mFallback.push_back({destination.targetRow, record});
                    }
                    break;
                case RowDestinationKind::FAR:
                    mFallback.push_back({destination.targetRow, record});
                    break;
            }
        }
        current.records.resize(write);
    }

    for (std::size_t cellId = mPreviousCount; cellId < cellCount; cellId++) {
        const RowRecord record = recordFromSource(cellId);
        mFallback.push_back({rowOf(record.y), record});
    }
    mPreviousCount = cellCount;

#ifdef CELLULARITY_PROFILE
    if (collectStats) {
        const long long fallbackTransfers = static_cast<long long>(mFallback.size());
        mStats.adjacentTransfers = adjacentTransfers;
        mStats.fallbackTransfers = fallbackTransfers;
        mStats.transferredRecords = adjacentTransfers + fallbackTransfers;
    }
#endif

    if (!mFallback.empty()) {
        std::int32_t fallbackMinRow = std::numeric_limits<std::int32_t>::max();
        std::int32_t fallbackMaxRow = std::numeric_limits<std::int32_t>::min();
        for (const FallbackRecord& pending : mFallback) {
            fallbackMinRow = std::min(fallbackMinRow, pending.targetRow);
            fallbackMaxRow = std::max(fallbackMaxRow, pending.targetRow);
        }
        ensureRowSpan(fallbackMinRow, fallbackMaxRow);

        for (const FallbackRecord& pending : mFallback) {
            const std::size_t rowIndex = static_cast<std::size_t>(
                    static_cast<std::int64_t>(pending.targetRow)
                    - static_cast<std::int64_t>(mMinRow));
            mRows[rowIndex].fallbackIncoming.push_back(pending.record);
        }
        mFallback.clear();
    }

    for (RowBucket& row : mRows) {
        if (!row.hasIncoming()) {
            continue;
        }
        std::sort(row.fallbackIncoming.begin(),
                row.fallbackIncoming.end(),
                recordLess);
        mergeIncomingIntoRow(row);
    }
#ifndef NDEBUG
    validateLayout();
#endif
}

void CellIncrementalBucketedF64Broadphase::rebuildAll() {
    mRows.clear();
    std::int32_t minRow = std::numeric_limits<std::int32_t>::max();
    std::int32_t maxRow = std::numeric_limits<std::int32_t>::min();
    for (std::size_t cellId = 0; cellId < mPackedSource.size(); cellId++) {
        const RowRecord record = recordFromSource(cellId);
        const std::int32_t row = rowOf(record.y);
        minRow = std::min(minRow, row);
        maxRow = std::max(maxRow, row);
    }

    const std::int64_t rowSpan =
            static_cast<std::int64_t>(maxRow) - static_cast<std::int64_t>(minRow) + 1;
    assert(rowSpan > 0);
    mRows.resize(static_cast<std::size_t>(rowSpan));
    mMinRow = minRow;
    for (std::size_t cellId = 0; cellId < mPackedSource.size(); cellId++) {
        const RowRecord record = recordFromSource(cellId);
        const std::size_t rowIndex = static_cast<std::size_t>(
                static_cast<std::int64_t>(rowOf(record.y))
                - static_cast<std::int64_t>(minRow));
        mRows[rowIndex].records.push_back(record);
    }
    for (RowBucket& row : mRows) {
        std::sort(row.records.begin(), row.records.end(), recordLess);
    }

    mPreviousCount = mPackedSource.size();
    mInitialized = true;
#ifdef CELLULARITY_PROFILE
    mStats.fullRebuilds = 1;
#endif
#ifndef NDEBUG
    validateLayout();
#endif
}

void CellIncrementalBucketedF64Broadphase::ensureRowSpan(std::int32_t requestedMin,
        std::int32_t requestedMax) {
    const std::int32_t oldMin = mMinRow;
    const std::int64_t oldMax64 = static_cast<std::int64_t>(oldMin)
            + static_cast<std::int64_t>(mRows.size()) - 1;
    assert(oldMax64 <= std::numeric_limits<std::int32_t>::max());
    const std::int32_t oldMax = static_cast<std::int32_t>(oldMax64);
    const std::int32_t newMin = std::min(oldMin, requestedMin);
    const std::int32_t newMax = std::max(oldMax, requestedMax);
    if (newMin == oldMin && newMax == oldMax) {
        return;
    }

    const std::int64_t newSpan =
            static_cast<std::int64_t>(newMax) - static_cast<std::int64_t>(newMin) + 1;
    std::vector<RowBucket> expanded(static_cast<std::size_t>(newSpan));
    const std::size_t offset = static_cast<std::size_t>(
            static_cast<std::int64_t>(oldMin) - static_cast<std::int64_t>(newMin));
    for (std::size_t oldIndex = 0; oldIndex < mRows.size(); oldIndex++) {
        expanded[offset + oldIndex] = std::move(mRows[oldIndex]);
    }
    mRows = std::move(expanded);
    mMinRow = newMin;
}

#ifndef NDEBUG
void CellIncrementalBucketedF64Broadphase::validateLayout() {
    mValidationSeen.resize(mPackedSource.size());
    std::fill(mValidationSeen.begin(), mValidationSeen.end(), std::uint8_t{0});
    std::size_t recordCount = 0;
    for (std::size_t rowIndex = 0; rowIndex < mRows.size(); rowIndex++) {
        const RowBucket& row = mRows[rowIndex];
        assert(!row.hasIncoming());
        for (std::size_t recordIndex = 1; recordIndex < row.records.size(); recordIndex++) {
            assert(!recordLess(row.records[recordIndex],
                    row.records[recordIndex - 1U]));
        }
        const std::int32_t expectedRow = static_cast<std::int32_t>(
                static_cast<std::int64_t>(mMinRow)
                + static_cast<std::int64_t>(rowIndex));
        for (const RowRecord& record : row.records) {
            assert(record.cellId < mPackedSource.size());
            assert(mValidationSeen[record.cellId] == 0);
            mValidationSeen[record.cellId] = 1;
            assert(rowOf(record.y) == expectedRow);
            recordCount++;
        }
    }
    assert(recordCount == mPackedSource.size());
}
#endif

const CellIncrementalBucketedF64Broadphase::Stats&
CellIncrementalBucketedF64Broadphase::stats() const noexcept {
    return mStats;
}
