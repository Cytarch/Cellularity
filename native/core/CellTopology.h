#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "core/CellDataConstants.h"
#include "core/CellLinkCreationReason.h"
#include "core/TopologyEdgeRuntimeId.h"

class CellTopology : public CellDataConstants {
public:
    struct CellLinkSnapshot {
        bool isBoundary = false;
        int connectedCellIndex = BOUNDARY_CONNECTION_INDEX;
        CellLinkCreationReason creationReason =
                CellLinkCreationReason::UNSPECIFIED;
        double angleA = 0.0;
        double angleB = 0.0;
        double boundaryAnchorX = 0.0;
        double boundaryAnchorY = 0.0;
        float torsionalStiffness = 0.0f;
        float restLength = 0.0f;
    };

    struct EdgeView {
        int count = 0;
        const int* cellA = nullptr;
        const int* cellB = nullptr;
        const CellLinkCreationReason* creationReason = nullptr;
        const double* angleA = nullptr;
        const double* angleB = nullptr;
        const double* angleACos = nullptr;
        const double* angleASin = nullptr;
        const double* angleBCos = nullptr;
        const double* angleBSin = nullptr;
        const float* torsionalStiffness = nullptr;
        const float* restLength = nullptr;
    };

    struct RenderEdgeView {
        int count = 0;
        const TopologyEdgeRuntimeId* runtimeId = nullptr;
        const int* cellA = nullptr;
        const int* cellB = nullptr;
        const float* renderStartOffsetAX = nullptr;
        const float* renderStartOffsetAY = nullptr;
        const float* renderEndOffsetAX = nullptr;
        const float* renderEndOffsetAY = nullptr;
    };

    struct BoundaryView {
        int count = 0;
        const int* cell = nullptr;
        const double* angleA = nullptr;
        const double* angleACos = nullptr;
        const double* angleASin = nullptr;
        const double* anchorX = nullptr;
        const double* anchorY = nullptr;
        const float* torsionalStiffness = nullptr;
        const float* restLength = nullptr;
    };

    explicit CellTopology(int maxCellCount);

    int edgeCount() const;
    int boundaryCount() const;
    [[nodiscard]] std::uint64_t structuralRevision() const noexcept;
    EdgeView edgeView() const;
    RenderEdgeView renderEdgeView() const;
    BoundaryView boundaryView() const;
    const int* cellLinkCounts() const;
    int linkCount(int cellIndex) const;
    bool canMakeLink(int cellIndex) const;
    bool isLinked(int firstCellIndex, int secondCellIndex) const;
    bool hasBoundaryLink(int cellIndex) const;

    int addCellLink(int cellA,
            int cellB,
            CellLinkCreationReason creationReason,
            double angleA,
            double angleB,
            float torsionalStiffness,
            float restLength);
    int addBoundaryLink(int cellIndex,
            double angleA,
            double anchorX,
            double anchorY,
            float torsionalStiffness,
            float restLength);

    void removeCellLink(int edgeIndex);
    void removeBoundaryLink(int boundaryIndex);
    bool removeCellLinkBetween(int firstCellIndex, int secondCellIndex);
    bool removeBoundaryLinkOfCell(int cellIndex);
    void removeAllLinksOfCell(int cellIndex);
    void moveCellIndex(int fromCellIndex, int toCellIndex);
    void clearAll();
    bool validate() const;
    bool validateWithScratch(std::uint32_t* expectedCounts,
            int expectedCountCapacity) const;

    int collectLinksOfCell(int cellIndex, std::array<CellLinkSnapshot, MAX_NEIGHBORS>& out) const;

    int adjacentCell(int cellIndex, int slotIndex) const {
        const LinkKind kind = static_cast<LinkKind>(mCellLinkKinds[cellIndex][slotIndex]);
        if (kind == BOUNDARY) {
            return BOUNDARY_CONNECTION_INDEX;
        }
        const int edgeIndex = mCellLinkRefs[cellIndex][slotIndex];
        return kind == NORMAL_A ? mEdgeCellB[edgeIndex] : mEdgeCellA[edgeIndex];
    }

    int incidentEdgeIndex(int cellIndex, int slotIndex) const {
        if (!isValidCellIndex(cellIndex)
                || slotIndex < 0
                || slotIndex >= mCellLinkCount[cellIndex]) {
            return INVALID_REF;
        }
        return static_cast<LinkKind>(mCellLinkKinds[cellIndex][slotIndex]) == BOUNDARY
                ? INVALID_REF
                : mCellLinkRefs[cellIndex][slotIndex];
    }

    int incidentEdgeIndexAtValidSlot(int cellIndex, int slotIndex) const {
        assert(cellIndex >= 0 && cellIndex < mMaxCellCount);
        assert(slotIndex >= 0 && slotIndex < mCellLinkCount[cellIndex]);
        return static_cast<LinkKind>(mCellLinkKinds[cellIndex][slotIndex]) == BOUNDARY
                ? INVALID_REF
                : mCellLinkRefs[cellIndex][slotIndex];
    }

    int edgeCellA(int edgeIndex) const;
    int edgeCellB(int edgeIndex) const;
    CellLinkCreationReason edgeCreationReason(int edgeIndex) const;
    double edgeAngleA(int edgeIndex) const;
    double edgeAngleB(int edgeIndex) const;
    double edgeAngleACos(int edgeIndex) const;
    double edgeAngleASin(int edgeIndex) const;
    double edgeAngleBCos(int edgeIndex) const;
    double edgeAngleBSin(int edgeIndex) const;
    float edgeTorsionalStiffness(int edgeIndex) const;
    float edgeRestLength(int edgeIndex) const;
    [[nodiscard]] TopologyEdgeRuntimeId edgeRuntimeId(int edgeIndex) const noexcept;

    int boundaryCell(int boundaryIndex) const;
    double boundaryAngleA(int boundaryIndex) const;
    double boundaryAngleACos(int boundaryIndex) const;
    double boundaryAngleASin(int boundaryIndex) const;
    double boundaryAnchorX(int boundaryIndex) const;
    double boundaryAnchorY(int boundaryIndex) const;
    float boundaryTorsionalStiffness(int boundaryIndex) const;
    float boundaryRestLength(int boundaryIndex) const;

    float edgeRenderStartOffsetAX(int edgeIndex) const;
    float edgeRenderStartOffsetAY(int edgeIndex) const;
    float edgeRenderEndOffsetAX(int edgeIndex) const;
    float edgeRenderEndOffsetAY(int edgeIndex) const;
    void setEdgeRenderOffsets(int edgeIndex,
            float startOffsetAX,
            float startOffsetAY,
            float endOffsetAX,
            float endOffsetAY);

    float boundaryRenderStartOffsetX(int boundaryIndex) const;
    float boundaryRenderStartOffsetY(int boundaryIndex) const;
    void setBoundaryRenderStartOffset(int boundaryIndex, float startOffsetX, float startOffsetY);

private:
    enum LinkKind : uint8_t {
        NORMAL_A = 0,
        NORMAL_B = 1,
        BOUNDARY = 2
    };

    static constexpr int INVALID_REF = -1;

    bool isValidCellIndex(int cellIndex) const;
    void rebuildAdjacency();
    void addAdjacency(int cellIndex, LinkKind kind, int ref);
    bool removeAdjacency(int cellIndex, LinkKind kind, int ref);
    bool updateAdjacencyRef(int cellIndex, LinkKind kind, int oldRef, int newRef);
    [[nodiscard]] TopologyEdgeRuntimeId allocateEdgeRuntimeId();
    void markStructureChanged();

    int mMaxCellCount;
    std::vector<TopologyEdgeRuntimeId> mEdgeRuntimeIds;
    std::vector<int> mEdgeCellA;
    std::vector<int> mEdgeCellB;
    std::vector<CellLinkCreationReason> mEdgeCreationReasons;
    std::vector<double> mEdgeAngleA;
    std::vector<double> mEdgeAngleB;
    std::vector<double> mEdgeAngleACos;
    std::vector<double> mEdgeAngleASin;
    std::vector<double> mEdgeAngleBCos;
    std::vector<double> mEdgeAngleBSin;
    std::vector<float> mEdgeTorsionalStiffness;
    std::vector<float> mEdgeRestLength;
    std::vector<float> mEdgeRenderStartOffsetAX;
    std::vector<float> mEdgeRenderStartOffsetAY;
    std::vector<float> mEdgeRenderEndOffsetAX;
    std::vector<float> mEdgeRenderEndOffsetAY;

    std::vector<int> mBoundaryCell;
    std::vector<double> mBoundaryAngleA;
    std::vector<double> mBoundaryAngleACos;
    std::vector<double> mBoundaryAngleASin;
    std::vector<double> mBoundaryAnchorX;
    std::vector<double> mBoundaryAnchorY;
    std::vector<float> mBoundaryTorsionalStiffness;
    std::vector<float> mBoundaryRestLength;
    std::vector<float> mBoundaryRenderStartOffsetX;
    std::vector<float> mBoundaryRenderStartOffsetY;

    std::vector<std::array<int, MAX_NEIGHBORS>> mCellLinkRefs;
    std::vector<std::array<uint8_t, MAX_NEIGHBORS>> mCellLinkKinds;
    std::vector<int> mCellLinkCount;
    TopologyEdgeRuntimeId mNextEdgeRuntimeId = 1;
    std::uint64_t mStructuralRevision = 0;
};
