#pragma once

#include <cstdint>
#include <vector>

class CellTopology;

// Runtime-only, cell-owned view of normal topology edges. Each cell range is
// ordered by the current global edge index, so a cell kernel preserves the
// reference global-edge accumulation order without sorting in the hot path.
class CellSignalAdjacency {
public:
    using IncidentReference = std::uint32_t;

    struct View {
        int cellCount = 0;
        int referenceCount = 0;
        const std::uint32_t* offsets = nullptr;
        const IncidentReference* references = nullptr;
    };

    explicit CellSignalAdjacency(int maxCellCount);

    // Returns true only when the derived view had to be rebuilt.
    bool ensureCurrent(const CellTopology& topology, int activeCellCount);
    void invalidate() noexcept;

    [[nodiscard]] View view() const noexcept;
    [[nodiscard]] std::uint64_t topologyRevision() const noexcept;
    [[nodiscard]] int activeCellCount() const noexcept;

    [[nodiscard]] static int edgeIndex(IncidentReference reference) noexcept;
    [[nodiscard]] static bool ownerIsCellB(IncidentReference reference) noexcept;

private:
    static IncidentReference encodeReference(int edgeIndex, bool ownerIsCellB);

    int mMaxCellCount = 0;
    int mActiveCellCount = -1;
    int mReferenceCount = 0;
    std::uint64_t mTopologyRevision = 0;
    bool mIsValid = false;
    std::vector<std::uint32_t> mOffsets;
    std::vector<std::uint32_t> mScatterOffsets;
    std::vector<IncidentReference> mIncidentReferences;
};
