#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/CellDataConstants.h"
#include "core/CellRuntimeId.h"
#include "core/OrganismId.h"
#ifndef CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
#define CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL 1
#endif
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
#include "core/CellSignalAdjacency.h"
#endif
#include "core/CellTopology.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "core/GenomePool.h"

class Environment;
class SimulationWorld;
struct SimulationWorldProfile;

enum class CellSignalUpdateAlgorithm : std::uint8_t {
    GLOBAL_EDGE,
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
    CELL_OWNED_CSR,
#endif
};

class CellData : public CellDataConstants {
private:
    // Pools precede every handle-owning vector so they outlive all Cell slots.
    cellularity::GenePool mGenePool;
    cellularity::GenomePool mGenomePool;
    std::vector<cellularity::GenomeId> mGenomeIds;
    std::vector<cellularity::GeneIndex> mGeneIndices;
    std::vector<cellularity::GeneId> mActiveGeneIds;
    std::vector<const GeneData*> mActiveGeneData;
    std::vector<std::uint8_t> mActiveGeneInitialFlags;
    // CellData owns one reference so this immutable template cannot be
    // reclaimed, reused, or modified in place by a Cell mutation.
    cellularity::GenomeId mDefaultGenomeId = cellularity::INVALID_GENOME_ID;

public:
    static constexpr int CELL_AUX2_DRAG_START = 100;

    int mCellCount = 0;
    const int MAX_CELL_COUNT;
    const int RANDOM_CELLS_MAINTAIN_COUNT;

    std::vector<double> mPositionX;
    std::vector<double> mPositionY;
    std::vector<double> mPositionZ;
    std::vector<double> mRadius;
    std::vector<double> mRadiusSq;
    std::vector<double> mVelocityX;
    std::vector<double> mVelocityY;
    std::vector<double> mForceX;
    std::vector<double> mForceY;
    std::vector<double> mForceZ;
    std::vector<double> mTorque;
    std::vector<double> mAngle;
    std::vector<double> mCachedAngle;
    std::vector<double> mCachedAngleCos;
    std::vector<double> mCachedAngleSin;
    std::vector<double> mAngleCytoplasm;
    std::vector<double> mAngularVelocity;
    std::vector<double> mAngularVelocityCytoplasm;
    std::vector<double> mAge;
    std::vector<double> mNitrates;
    std::vector<float> mSwimAnimationTime;
    std::vector<float> mSwimAnimationTime2;
    std::vector<double> mHairRotation;
    std::vector<double> hairX;
    std::vector<double> hairY;
    std::vector<double> mMass;
    std::vector<uint8_t> mIsDead;
    std::vector<uint8_t> mIsMirrored;
    std::vector<uint8_t> mIsStuck;
    std::vector<int> mOriginTag;
    // Parent identity is repeated per Cell so compaction and save/load remain
    // pure SoA operations. All Cells sharing an ID must share the same parent.
    std::vector<OrganismId> mOrganismIds;
    std::vector<OrganismId> mParentOrganismIds;
    std::vector<int> mSelectTag;
    std::vector<int> mMutationCount;
    std::vector<int> mPendingMutationCount;
    CellTopology mTopology;
    std::vector<std::array<float, 4>> mColor;
    std::vector<int> mGridIndex1;
    std::vector<int> mSeldomCounter;
    std::vector<int> mOldLinkCount;
    std::vector<double> mNewMass;
    std::vector<double> mNewNitrates;
    std::vector<double> mMetabolismScaledMass;
    std::vector<double> mMetabolismMassFlowPriority;
    std::vector<float> mLipids;
    std::vector<int> mTelomeresCount;
    std::vector<CellType> mActiveCellType;
    std::vector<double> mActiveCellCollisionElasticity;
    std::vector<std::array<float, 2 * SIGNAL_SUBSTANCE_COUNT>> mSignalSubstance;
    std::vector<std::array<float, SIGNAL_SUBSTANCE_COUNT>> mNewSignalSubstance;
    std::vector<std::array<float, SIGNAL_SUBSTANCE_COUNT>> mSignalSubstanceOut;
    std::vector<float> mToxin;
    std::vector<float> mToxication;
    std::vector<float> mNewToxin;
    std::vector<float> mMyocyteContraction;
    std::vector<float> mMyocyteBending;
    std::vector<float> mMyocyteLifting;
    std::vector<int> mFertilizationTarget;
    int mCiliocyteInteractionCount = 0;
    std::vector<int> mCiliocyteInteractionCellIndex1;
    std::vector<int> mCiliocyteInteractionCellIndex2;

    struct CellCollisionGeometry {
        double distanceX = 0.0;
        double distanceY = 0.0;
        double distance = 0.0;
        double contactChordLengthSquared = 0.0;
        double normalX = 0.0;
        double normalY = 0.0;
    };

    struct CellContactBuffer {
        struct StoredGeometry {
            double distanceX = 0.0;
            double distanceY = 0.0;
            double distanceSq = 0.0;
        };

        std::vector<int> collidedCellIndex;
        std::vector<StoredGeometry> geometry;
        std::vector<int> cellContactCount;

        explicit CellContactBuffer(int maxCellCount);
        CellContactBuffer(CellContactBuffer&& other) noexcept;
        CellContactBuffer& operator=(CellContactBuffer&& other) noexcept;
        CellContactBuffer(const CellContactBuffer&) = delete;
        CellContactBuffer& operator=(const CellContactBuffer&) = delete;
        void clear(int cellCount);
        void clearCell(int cellIndex);
        int add(int currentCellIndex,
                int collidedCellIndex,
                double distanceX,
                double distanceY,
                double distanceSq);
        int begin(int cellIndex) const;
        int end(int cellIndex) const;
        int count(int cellIndex) const;
        void setCount(int cellIndex, int count);
        void copyCell(int fromCellIndex, int toCellIndex);
        void copyContact(int fromContactIndex, int toContactIndex, int currentCellIndex);
        void getGeometry(int contactIndex,
                double currentCellRadius,
                double collidedCellRadius,
                CellCollisionGeometry& geometry) const;
    };

    CellContactBuffer mCellContactBuffer;

    struct CellCollisionIntentBuffer {
        std::vector<int> currentCellIndex;
        std::vector<int> collidedCellIndex;
        std::vector<double> distanceX;
        std::vector<double> distanceY;
        std::vector<double> distance;
        std::vector<double> contactChordLengthSquared;
        int count = 0;

        explicit CellCollisionIntentBuffer(int maxCellCount);
        void clear();
        void add(int currentCellIndexValue,
                int collidedCellIndexValue,
                const CellCollisionGeometry& geometry);
        void getGeometry(int intentIndex, CellCollisionGeometry& geometry) const;
    };

    CellCollisionIntentBuffer mCellCollisionIntentBuffer;

    explicit CellData(int maxCellCount);
    ~CellData();
    /** Moves the last active Cell into a removed slot during swap compaction. */
    void moveLastCellForCompaction(int from, int to);
    [[nodiscard]] cellularity::GeneView getActiveGeneView(int cellIndex) const;
    [[nodiscard]] cellularity::GeneView getGeneView(
            int cellIndex,
            cellularity::GeneIndex geneIndex) const;
    [[nodiscard]] cellularity::GenomeView getGenomeView(int cellIndex) const;
    [[nodiscard]] cellularity::GenomeData materializeGenome(int cellIndex) const;
    [[nodiscard]] std::size_t geneCount(int cellIndex) const noexcept;
    [[nodiscard]] cellularity::GenomeId genomeId(int cellIndex) const noexcept;
    [[nodiscard]] cellularity::GeneIndex activeGeneIndex(
            int cellIndex) const noexcept;
    [[nodiscard]] cellularity::GeneId activeGeneId(
            int cellIndex) const noexcept;
    [[nodiscard]] std::uint32_t genomeReferenceCount(int cellIndex) const noexcept;

    [[nodiscard]] const cellularity::GenomePool& genomePool() const noexcept {
        return mGenomePool;
    }

    [[nodiscard]] const cellularity::GenePool& genePool() const noexcept {
        return mGenePool;
    }

    /** Replaces one Cell-owned record atomically after successful import. */
    bool replaceGenome(
            int cellIndex,
            const cellularity::GenomeData& genome,
            cellularity::GeneIndex activeGeneIndex);
    bool replaceGenome(
            int cellIndex,
            const cellularity::GenomeData& genome);
    bool replaceGenome(
            int cellIndex,
            const cellularity::FixedGenome& genome,
            cellularity::GeneIndex activeGeneIndex);
    bool replaceGenome(
            int cellIndex,
            const cellularity::FixedGenome& genome);

    /**
     * Installs one file-local canonical Gene/Genome table into fresh active
     * Cell slots while preserving shared Genome records between Cells.
     */
    bool importSharedGenomes(
            const std::vector<cellularity::CanonicalGene>& uniqueGenes,
            const std::vector<std::uint64_t>& geneReferenceOffsets,
            const std::vector<std::uint32_t>& fileGeneReferences,
            const std::vector<cellularity::GeneIndex>& initialGeneIndices,
            const std::vector<std::uint32_t>& cellGenomeReferences,
            const std::vector<cellularity::GeneIndex>& activeGeneIndices);

    /** Replaces one immutable pooled Gene and performs whole-genome CoW. */
    bool replaceGene(
            int cellIndex,
            cellularity::GeneIndex geneIndex,
            const GeneData& candidate,
            bool* changed = nullptr);

    template <typename Editor>
    bool editGene(
            int cellIndex,
            cellularity::GeneIndex geneIndex,
            Editor&& editor) {
        auto candidate = mGenomePool.copyGeneForMutation(
                mGenomeIds[static_cast<std::size_t>(cellIndex)], geneIndex);
        if (!candidate.has_value()) {
            return false;
        }
        cellularity::GeneBuilder builder(*candidate);
        std::forward<Editor>(editor)(builder);
        return replaceGene(cellIndex, geneIndex, *candidate);
    }

    template <typename Editor>
    bool editActiveGene(int cellIndex, Editor&& editor) {
        return editGene(
                cellIndex,
                mGeneIndices[static_cast<std::size_t>(cellIndex)],
                std::forward<Editor>(editor));
    }

    bool setActiveGeneIndex(
            int cellIndex,
            cellularity::GeneIndex geneIndex);
    void refreshActiveCellType(int cellIndex);
    void refreshRadiusSq(int cellIndex);
    void refreshAllRadiusSq();
    float getValue(int cellIndex, int valueIndex, int readOffset) const;
    double getScaledMass(int index) const;
    int getCellIndex(double positionX, double positionY) const;
    void deselectAll();
    int getIdFromSelectTag(int tag) const;
    bool isCellCanMakeLink(int cellIndex) const;
    bool isCellSafeByKeratinocyte(int currentCellIndex) const;
    bool isNotLinked(int currentCellIndex, int anotherCellIndex) const;
    bool isCellReadyForFertilization(int cellIndex) const;
    bool isValidCellCollisionTarget(int currentCellIndex, int collidedCellIndex) const;
    bool calculateCellCollisionGeometryFromDistance(int currentCellIndex,
            int collidedCellIndex,
            double distanceX,
            double distanceY,
            double distanceSq,
            CellCollisionGeometry& geometry) const;
    void clearCellCollisionContacts();
    void clearCellCollisionContacts(int cellIndex);
    void removeCollisionContactsTargetingDeadCells(
            SimulationWorldProfile* profile = nullptr);
    bool addCellCollisionContact(int currentCellIndex,
            int collidedCellIndex,
            double distanceX,
            double distanceY,
            double distanceSq);
    bool addKnownCellCollisionContact(int currentCellIndex,
            int collidedCellIndex,
            double distanceX,
            double distanceY,
            double distanceSq,
            double currentCellRadius,
            double collidedCellRadius);

    bool addKnownCellCollisionContactFast(int currentCellIndex,
            int collidedCellIndex,
            double distanceX,
            double distanceY,
            double distanceSq) {
        int currentCount = mCellContactBuffer.cellContactCount[currentCellIndex];
        if (currentCount >= MAX_COLLISIONS_PER_CELL) {
            return false;
        }

        const int contactIndex = currentCellIndex * MAX_COLLISIONS_PER_CELL + currentCount;
        mCellContactBuffer.collidedCellIndex[contactIndex] = collidedCellIndex;
        CellContactBuffer::StoredGeometry& storedGeometry = mCellContactBuffer.geometry[contactIndex];
        storedGeometry.distanceX = distanceX;
        storedGeometry.distanceY = distanceY;
        storedGeometry.distanceSq = distanceSq;
        mCellContactBuffer.cellContactCount[currentCellIndex] = currentCount + 1;
        return true;
    }

    int cellCollisionContactBegin(int cellIndex) const;
    int cellCollisionContactEnd(int cellIndex) const;
    int cellCollisionContactCount(int cellIndex) const;
    int cellCollisionContactTarget(int contactIndex) const;
    void getCellCollisionContactGeometry(int contactIndex, CellCollisionGeometry& geometry) const;
    double calculateGameteForceModifier(int currentCellIndex, int collidedCellIndex) const;
    void applyCellCollisionForce(int currentCellIndex, int collidedCellIndex, const CellCollisionGeometry& geometry);
    void handleSingleCellCollisionPartial(int currentCellIndex,
            int collidedCellIndex,
            const CellCollisionGeometry& geometry,
            double deltaTime,
            Environment& environment);
    void collectCellToCellCollisionIntentsPartial(int currentCellIndex);
    void collectAllCellToCellCollisionIntentsPartial();
    void applyCellToCellCollisionIntentPartial(int intentIndex,
            double deltaTime,
            Environment& environment,
            SimulationWorldProfile* profile = nullptr);
    void applyCellToCellCollisionIntentsPartial(double deltaTime,
            Environment& environment,
            SimulationWorldProfile* profile = nullptr);
    void applyCellTypedCollisionIntentPartial(int intentIndex,
            double deltaTime,
            Environment& environment,
            SimulationWorldProfile* profile = nullptr);
    void handleCellToCellCollisionsPartial(int currentCellIndex,
            double deltaTime,
            Environment& environment);
    void handleAllCellToCellCollisionsPartial(double deltaTime,
            Environment& environment,
            SimulationWorldProfile* profile = nullptr);
    void handleCellToBoundaryCollisionPartial(
            int currentCellIndex,
            const Environment& environment);
    void handleAllCellToBoundaryCollisionsPartial(
            const Environment& environment);
    void updateAllCellInteractionsPartial(double deltaTime,
            Environment& environment);
    void collectAllCollisionInteractionsPartial(
            double deltaTime,
            Environment& environment);
    void applyCollectedCiliocyteInteractionsPartial(const Environment& environment);
    void updateCellForces(int currentCellIndex,
            double deltaTime,
            const Environment& environment,
            SimulationWorldProfile* profile = nullptr);
    void updateAllCellForces(double deltaTime,
            const Environment& environment,
            SimulationWorldProfile* profile = nullptr);
    void updateCellsMetabolism(double deltaTime,
            Environment& environment,
            SimulationWorldProfile* profile = nullptr);
    void setSignalUpdateAlgorithm(CellSignalUpdateAlgorithm algorithm) noexcept;
    [[nodiscard]] CellSignalUpdateAlgorithm signalUpdateAlgorithm() const noexcept;
    void updateSignals(double deltaTime, SimulationWorldProfile* profile = nullptr);
    void handleCellSplit(SimulationWorld& simulationWorld);
    bool applyCellFertilization(int spermIndex, int eggIndex, Environment& environment);
    bool resolveFertilizationIntents(
            Environment& environment,
            SimulationWorldProfile* profile = nullptr);
    void updateCellAngleDirectionCache(int cellIndex);
    void updateAllCellAngleDirectionCaches();
    void updateAllCellLinkInteractions(double deltaTime,
            SimulationWorldProfile* profile = nullptr,
            bool updateRenderOffsets = true);
    void updateCellVelocities(const Environment& environment, double deltaTime);
    void updateCellsKinematics(int targetCellIndex, double deltaTime);
    void updateCellsKinematics(double deltaTime);
    void resetCellForces();
    void updateFlagellocytesForce(double deltaTime, const Environment& environment);
    void handleCellCollisionEffects(int currentCellIndex);
    void handleAllCellCollisionEffects();
    void handleCellCollisionBiologicalSideEffects(int currentCellIndex, double deltaTime, Environment& environment);
    void handleAllCellCollisionBiologicalSideEffects(double deltaTime, Environment& environment);
    void addCiliocyteInteraction(int currentCellIndex, int collidedCellIndex);
    void handleCiliocyteCollision(int currentCellIndex, int collidedCellIndex, double normalX, double normalY);
    void handleCiliocyteToBoundaryCollision(int currentCellIndex);
    void updateCiliocyteInteractions(const Environment& environment);
    void handleGlueocyteCollision(int currentCellIndex, int victimCellIndex);
    void handleDevorocyteCollision(int currentCellIndex, int victimCellIndex, double deltaTime);
    void handleVirocyteCollision(int currentCellIndex, int victimCellIndex, double deltaTime, Environment& environment);
    void handleSecrocyteCollision(int currentCellIndex, int victimCellIndex, double deltaTime, Environment& environment);
    int allocateNewCell();
    // These count transitions own the non-serialized runtime-ID lifecycle.
    // resize preserves the active prefix; replace assigns a fresh identity to
    // every active slot. Neither function initializes simulation fields.
    void resizeActiveCellCount(int newCellCount);
    void replaceActiveCellCount(int newCellCount);
    void releaseLastCellSlot();
    [[nodiscard]] CellRuntimeId runtimeId(int cellIndex) const noexcept;

    struct OrganismIdentitySummary final {
        std::uint64_t knownOrganismCount = 0;
        bool complete = true;
    };

    [[nodiscard]] OrganismId organismId(int cellIndex) const noexcept;
    [[nodiscard]] OrganismId parentOrganismId(int cellIndex) const noexcept;
    [[nodiscard]] OrganismId nextOrganismId() const noexcept;
    [[nodiscard]] OrganismId assignNewOrganism(
            int cellIndex,
            OrganismId parentOrganismId = kUnknownOrganismId);
    void restoreNextOrganismId(OrganismId nextOrganismId);
    [[nodiscard]] bool validateOrganismIdentities() const;
    [[nodiscard]] OrganismIdentitySummary organismIdentitySummary() const;
    void addRandomCell(double positionX, double positionY, Environment& environment);
    void addRandomCells(Environment& environment);
    int addLink(int currentCellIndex,
            int victimCellIndex,
            CellLinkCreationReason creationReason,
            double angleA,
            double angleB,
            float stiffness,
            float length);
    void markCellAsAlive(int newCellIndex);
    void markCellAsDead(int clickedCellIndex);
    void applySupportTool(int clickedCellIndex);

private:
    std::vector<CellRuntimeId> mRuntimeIds;
    // Reused only when a split can disconnect organism-forming branches.
    // Keeping the scratch storage at world capacity avoids allocations in the
    // simulation update path.
    std::vector<std::uint32_t> mOrganismTraversalMarks;
    std::vector<int> mOrganismTraversalQueue;
    std::uint32_t mOrganismTraversalGeneration = 0;
    CellRuntimeId mNextRuntimeId = 1;
    OrganismId mNextOrganismId = 1;
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
    CellSignalAdjacency mSignalAdjacency;
#endif
    CellSignalUpdateAlgorithm mSignalUpdateAlgorithm =
            CellSignalUpdateAlgorithm::GLOBAL_EDGE;

    [[nodiscard]] CellRuntimeId allocateRuntimeId();
    void ensureRuntimeIdsAvailable(std::size_t count) const;
    [[nodiscard]] OrganismId allocateOrganismId();
    [[nodiscard]] bool hasInheritedOrganismConnection(
            int cellIndex,
            int siblingCellIndex,
            OrganismId inheritedOrganismId) const;
    [[nodiscard]] bool hasSiblingOrganismConnection(
            int firstCellIndex,
            int secondCellIndex) const;
    [[nodiscard]] bool hasOrganismFormingConnection(
            int firstCellIndex,
            int secondCellIndex) const;
    int collectOrganismComponent(
            int startCellIndex,
            OrganismId organismId);
    void assignNewOrganismToCollectedComponent(
            int componentSize,
            OrganismId parentOrganismId);
    void resolveSplitOrganismIdentity(
            int child1Index,
            int child2Index,
            OrganismId inheritedOrganismId,
            const std::array<CellTopology::CellLinkSnapshot, MAX_NEIGHBORS>&
                    previousLinks,
            int previousLinkCount);
    bool initializeDefaultGenomeHandle(int cellIndex);
    void releaseGenomeHandle(int cellIndex) noexcept;
    bool copyGenomeHandle(int fromCellIndex, int toCellIndex) noexcept;
    void moveGenomeHandle(int fromCellIndex, int toCellIndex) noexcept;
    void installOwnedGenomeHandle(
            int cellIndex,
            cellularity::GenomeId genomeId,
            cellularity::GeneIndex activeGeneIndex) noexcept;
    void refreshActiveGeneCache(int cellIndex) noexcept;
    bool mutatePooledGenomeDuringMetabolism(
            int cellIndex,
            Environment& environment);
};
