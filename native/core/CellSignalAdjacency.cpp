#include "core/CellSignalAdjacency.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include "core/CellDataConstants.h"
#include "core/CellTopology.h"

namespace {

std::size_t checkedCellCapacity(int maxCellCount) {
    if (maxCellCount < 0) {
        throw std::length_error("Signal adjacency Cell capacity is negative");
    }
    return static_cast<std::size_t>(maxCellCount);
}

std::size_t maximumIncidentReferenceCount(int maxCellCount) {
    const std::size_t count = checkedCellCapacity(maxCellCount);
    if (count > std::numeric_limits<std::size_t>::max()
                            / CellDataConstants::MAX_NEIGHBORS
            || count > std::numeric_limits<std::uint32_t>::max()
                            / CellDataConstants::MAX_NEIGHBORS) {
        throw std::length_error("Signal adjacency capacity is too large");
    }
    return count * CellDataConstants::MAX_NEIGHBORS;
}

} // namespace

CellSignalAdjacency::CellSignalAdjacency(int maxCellCount)
        : mMaxCellCount(maxCellCount),
          mOffsets(checkedCellCapacity(maxCellCount) + 1U),
          mScatterOffsets(checkedCellCapacity(maxCellCount)) {
    mIncidentReferences.reserve(maximumIncidentReferenceCount(maxCellCount));
}

bool CellSignalAdjacency::ensureCurrent(const CellTopology& topology,
        int activeCellCount) {
    if (activeCellCount < 0 || activeCellCount > mMaxCellCount) {
        throw std::length_error("Signal adjacency active Cell count is out of range");
    }

    const std::uint64_t revision = topology.structuralRevision();
    if (mIsValid
            && mTopologyRevision == revision
            && mActiveCellCount == activeCellCount) {
        return false;
    }
    if (!topology.validateWithScratch(
                mScatterOffsets.data(), mMaxCellCount)) {
        throw std::logic_error("Signal adjacency cannot rebuild invalid topology");
    }

    std::fill(mOffsets.begin(),
            mOffsets.begin() + static_cast<std::size_t>(activeCellCount) + 1U,
            0U);
    const CellTopology::EdgeView edges = topology.edgeView();
    const std::uint64_t referenceCount =
            static_cast<std::uint64_t>(edges.count) * 2U;
    if (referenceCount > mIncidentReferences.capacity()
            || referenceCount > std::numeric_limits<std::uint32_t>::max()
            || referenceCount > static_cast<std::uint64_t>(
                       std::numeric_limits<int>::max())) {
        throw std::length_error("Signal adjacency topology exceeds bounded capacity");
    }

    for (int edgeIndex = 0; edgeIndex < edges.count; ++edgeIndex) {
        const int cellA = edges.cellA[edgeIndex];
        const int cellB = edges.cellB[edgeIndex];
        if (cellA < 0 || cellA >= activeCellCount
                || cellB < 0 || cellB >= activeCellCount) {
            throw std::logic_error(
                    "Signal adjacency edge endpoint is outside the active Cell range");
        }
        ++mOffsets[static_cast<std::size_t>(cellA) + 1U];
        ++mOffsets[static_cast<std::size_t>(cellB) + 1U];
    }
    for (int cellIndex = 0; cellIndex < activeCellCount; ++cellIndex) {
        mOffsets[static_cast<std::size_t>(cellIndex) + 1U] +=
                mOffsets[static_cast<std::size_t>(cellIndex)];
        mScatterOffsets[static_cast<std::size_t>(cellIndex)] =
                mOffsets[static_cast<std::size_t>(cellIndex)];
    }

    mIncidentReferences.resize(static_cast<std::size_t>(referenceCount));
    for (int edgeIndex = 0; edgeIndex < edges.count; ++edgeIndex) {
        const int cellA = edges.cellA[edgeIndex];
        const int cellB = edges.cellB[edgeIndex];
        mIncidentReferences[mScatterOffsets[static_cast<std::size_t>(cellA)]++] =
                encodeReference(edgeIndex, false);
        mIncidentReferences[mScatterOffsets[static_cast<std::size_t>(cellB)]++] =
                encodeReference(edgeIndex, true);
    }

    mActiveCellCount = activeCellCount;
    mReferenceCount = static_cast<int>(referenceCount);
    mTopologyRevision = revision;
    mIsValid = true;
    return true;
}

void CellSignalAdjacency::invalidate() noexcept {
    mIsValid = false;
    mActiveCellCount = -1;
    mReferenceCount = 0;
}

CellSignalAdjacency::View CellSignalAdjacency::view() const noexcept {
    if (!mIsValid) {
        return {};
    }
    return {
            mActiveCellCount,
            mReferenceCount,
            mOffsets.data(),
            mIncidentReferences.data()
    };
}

std::uint64_t CellSignalAdjacency::topologyRevision() const noexcept {
    return mIsValid ? mTopologyRevision : 0;
}

int CellSignalAdjacency::activeCellCount() const noexcept {
    return mIsValid ? mActiveCellCount : -1;
}

int CellSignalAdjacency::edgeIndex(IncidentReference reference) noexcept {
    return static_cast<int>(reference >> 1U);
}

bool CellSignalAdjacency::ownerIsCellB(IncidentReference reference) noexcept {
    return (reference & 1U) != 0U;
}

CellSignalAdjacency::IncidentReference CellSignalAdjacency::encodeReference(
        int edgeIndex,
        bool ownerIsCellBValue) {
    if (edgeIndex < 0
            || static_cast<std::uint64_t>(edgeIndex)
                    > (std::numeric_limits<std::uint32_t>::max() >> 1U)) {
        throw std::length_error("Signal adjacency edge index cannot be encoded");
    }
    return (static_cast<std::uint32_t>(edgeIndex) << 1U)
            | static_cast<std::uint32_t>(ownerIsCellBValue);
}
