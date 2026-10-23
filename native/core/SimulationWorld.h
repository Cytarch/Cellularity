#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/CellData.h"
#include "core/CellSweepBroadphase.h"
#include "core/SimulationWorldProfile.h"
#include "core/SimulationWorldStageTiming.h"
#include "core/CollisionGrid.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"

namespace cellularity::tests {
struct NativeSimulationWorldRendererTestAccess;
} // namespace cellularity::tests

namespace cellularity::execution {
class SimulationExecutionContext;
} // namespace cellularity::execution

class SimulationWorld {
public:
    struct SplitEvent {
        int parentCellIndex = -1;
        int child1CellIndex = -1;
        int child2CellIndex = -1;
        int parentGeneIndex = -1;
        int child1GeneIndex = -1;
        int child2GeneIndex = -1;
        double splitTime = 0.0;
    };

    static constexpr int LEVEL_CELL_ORIGIN = 0;
    static constexpr int USER_CELL_ORIGIN = 1;
    static constexpr int RANDOM_CELL_ORIGIN = 2;
    static constexpr int INFECTED_LEVEL_CELL_ORIGIN = 3;
    static constexpr int INFECTED_USER_CELL_ORIGIN = 4;
    static constexpr int INFECTED_RANDOM_CELL_ORIGIN = 5;
    static constexpr int HYBRID_CELL_ORIGIN = 6;
    static constexpr int UPDATE_STAGE_CAN_TOUCH = 0;
    static constexpr double DELTA_TIME = 0.02;
    static constexpr int SIGNAL_PASSES = 2 * 2;
    static constexpr float SIGNAL_SUBSTANCE_SPLIT_NUDGE = 0.0001f;
    static constexpr double SPLIT_SEAM_TOLERANCE = 0.2;
    static constexpr double MAX_TAX_SPEED = 0.075;
    static constexpr int CELL_COLLISION_GRID_COUNT_MAX = 203;
    static constexpr double SMELL_RADIUS = CellData::MAX_CELL_RADIUS * 3.0;
    static constexpr double SMELL_RADIUS_CUTOFF = SMELL_RADIUS * 4.0;
    static constexpr int16_t SMELL_TYPE_CELL = 0;
    static constexpr int16_t SMELL_TYPE_FOOD = 1;
    static constexpr int16_t SMELL_TYPE_FAT = 2;
    static constexpr int16_t SMELL_TYPE_LIGHT = 3;
    static constexpr int16_t SMELL_TYPE_VELOCITY = 4;
    static constexpr int16_t SMELL_TYPE_WALL = 5;
    static constexpr int SMELL_COLLISION_GRID_COUNT_MAX = 404;

    Environment mEnvironment;
    CellData mCellData;
    FoodData mFoodData;
    DeadSplashData mDeadSplashData;
    CellSweepBroadphase mCellSweepBroadphase;
    std::unique_ptr<CollisionGrid> mCellCollisionGrid;
    std::unique_ptr<CollisionGrid> mSmellCollisionGrid;
    std::vector<int> mCellRenderOverflowGridHead;
    std::vector<int> mCellRenderOverflowGridNext;
    std::vector<int> mCellRenderOverflowGridTouched;
    std::vector<std::uint64_t> mCellRenderGridOccupied;
    std::vector<int> mFoodSpatialGridHead;
    std::vector<int> mFoodSpatialGridNext;
    std::vector<int> mFoodSpatialGridTouched;
    std::vector<int> mFoodSmellVisitStamp;
    int mFoodSmellVisitGeneration = 0;
    std::vector<uint8_t> mOverlapDeathIntent;
    std::vector<char> mFoodSmellGridConsumerMask;
    bool mHasAnySmellConsumers = false;
    bool mHasSmellGridConsumers = false;
    bool mHasFoodSmellConsumers = false;
    bool mHasFoodPhysicsQueryConsumers = false;
    bool mFoodSmellGridConsumerMaskActive = false;
    bool mHasFertilizationIntents = false;
    std::int64_t mCreatedAtUnixMillis = 0;
    double mTime = 0.0;
    int updateStage = UPDATE_STAGE_CAN_TOUCH;

    SimulationWorld(bool isPreviewMode, const Environment& environment);

    void setCellOverlapScanAlgorithm(CellOverlapScanAlgorithm algorithm) noexcept;
    [[nodiscard]] CellOverlapScanAlgorithm cellOverlapScanAlgorithm() const noexcept;

    void initializeCellCollisionGrid();
    void initializeSmellCollisionGrid();
    void clearGrids();
    void rebuildFoodSpatialIndex();
    void ensureFoodSpatialIndex();
    void ensureFoodSpatialIndexForCapture();
    [[nodiscard]] bool foodSpatialIndexIsFresh() const noexcept;
    void addCellToRenderOverflowGrid(int cellIndex,
            int gridPositionX,
            int gridPositionY);
    void markCellRenderGridOccupied(int gridIndex);
    void updatePhysicalGrid(int cellIndex);
    bool tryAddCellToCollisionGrid(int cellIndex, int gridIndex);
    void tryAddSmellToSmellGrid(int cellIndex);
    bool addFoodSmellToGrid(int foodIndex);
    void addFoodSmellsToGridForConsumers();
    bool isAnySmellConsumer(int cellIndex) const;
    bool isSmellGridConsumer(int cellIndex) const;
    bool isFoodSmellConsumer(int cellIndex) const;
    void updateSpatialConsumerFlags();
    void updateFoodSmellGridConsumerMask();
    void updateGrids(bool buildFoodSpatialIndex = true);
    void processKnownCellOverlapPair(int firstCellIndex,
            int secondCellIndex,
            double distanceX,
            double distanceY,
            double distanceSq,
            double firstRadius,
            double secondRadius);
    void processSweepKnownCellOverlapPairFast(int firstCellIndex,
            int secondCellIndex,
            double distanceX,
            double distanceY,
            double distanceSq,
            double firstRadius,
            double secondRadius);
    void processCellOverlapsSweepPairs();
    void applyCollisionOverlapDeathIntents();
    void processCellOverlaps();
    void addCorpseFood(int targetCellIndex);
    void addDeadSplash(int cellIndex);
    void removeCellImmediately(int targetCellIndex);
    void removeDeadCells();
    void removeFoodImmediately(int foodIndex);
    void eatFoods(double deltaTime);
    void updateCellSmellInteraction(int targetCellIndex);
    void updateAllCellSmellInteractions();
    void updateAllCellInteractionsPartial(bool isForRendering, double deltaTime);
    void applyNewCellMetabolism();
    void update(bool isForRendering, double speed);
    void update(bool isForRendering, double speed,
            cellularity::execution::SimulationExecutionContext* executionContext);
    void update1(bool isForRendering, double speed);
    void update1(bool isForRendering, double speed,
            cellularity::execution::SimulationExecutionContext* executionContext);
    void update2(bool forView, double speed);
    void update2(bool forView, double speed,
            cellularity::execution::SimulationExecutionContext* executionContext);
    void invalidateCellContactCache() noexcept;
    [[nodiscard]] bool cellContactCacheIsFresh() const noexcept;
    int getUpdateStage() const;
    void update1Partial(bool isForRendering, double speed);
    void update1Partial(bool isForRendering, double speed,
            cellularity::execution::SimulationExecutionContext* executionContext);
    void update2Partial(double speed);
    void update2Partial(double speed,
            cellularity::execution::SimulationExecutionContext* executionContext);
    void setProfile(SimulationWorldProfile* profile);
    SimulationWorldProfile* profile() const;
    void requestStageTimingSample() noexcept;
    [[nodiscard]] const SimulationWorldStageTimingSnapshot& stageTimingSnapshot() const noexcept;
    void beginSplitEventRecording(SplitEvent* events, int capacity);
    void endSplitEventRecording();
    void recordSplitEvent(int parentCellIndex,
            int child1CellIndex,
            int child2CellIndex,
            int parentGeneIndex,
            int child1GeneIndex,
            int child2GeneIndex);
    int splitEventCount() const;

private:
    friend struct cellularity::tests::NativeSimulationWorldRendererTestAccess;

    struct AllocationValidatedTag {};

    SimulationWorld(AllocationValidatedTag,
            bool isPreviewMode,
            const Environment& environment);

#ifdef CELLULARITY_PROFILE
    void updateCellSpatialIndexesProfiled();
    void rebuildFoodSpatialIndexProfiled();

    std::vector<int> mProfileCellGridPositionX;
    std::vector<int> mProfileCellGridPositionY;
    std::vector<std::uint8_t> mProfileCellGridAdmission;
#endif

    SimulationWorldProfile* mProfile = nullptr;
    std::array<long long, SimulationWorldProfile::STAGE_COUNT> mStageTimingWorking{};
    SimulationWorldStageTimingSnapshot mStageTimingSnapshot{};
    bool mStageTimingSampleRequested = false;
    bool mStageTimingSampleActive = false;
    SplitEvent* mSplitEventBuffer = nullptr;
    int mSplitEventCapacity = 0;
    int mSplitEventCount = 0;
    bool mCellContactCacheFresh = false;
    std::uint64_t mCellCollisionGridMappingGeneration = 0;
    std::uint64_t mFoodSpatialIndexFoodGeneration = 0;
    std::uint64_t mFoodSpatialIndexGridGeneration = 0;

    void beginStageTimingSample() noexcept;
    void finishStageTimingSample() noexcept;
};
