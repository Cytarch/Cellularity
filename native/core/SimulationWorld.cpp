#include "core/SimulationWorld.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string_view>

#if defined(CELLULARITY_PROFILE) || defined(CELLULARITY_PERFORMANCE_HUD)
#include <chrono>
#endif

#include "core/BesselLookup.h"
#include "core/Gene.h"
#include "core/ProfileInstrumentation.h"
#include "core/SimulationMemoryPolicy.h"
#include "hwy/highway.h"
#include "verification/SemanticTrace.h"

namespace hn = hwy::HWY_NAMESPACE;

namespace {

#if defined(CELLULARITY_PROFILE) || defined(CELLULARITY_PERFORMANCE_HUD)
class ProfileStageTimer {
public:
    ProfileStageTimer(SimulationWorldProfile* profile,
            SimulationWorldProfile::Stage stage,
            long long* sampledStageNanos)
            : mProfile(profile), mStage(stage), mSampledStageNanos(sampledStageNanos) {
        if (mProfile != nullptr || mSampledStageNanos != nullptr) {
            mStart = std::chrono::steady_clock::now();
            mEnabled = true;
        }
    }

    ~ProfileStageTimer() {
        if (!mEnabled) {
            return;
        }
        const long long elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - mStart)
                                          .count();
        if (mProfile != nullptr) {
            mProfile->stageNanos[mStage] += elapsed;
        }
        if (mSampledStageNanos != nullptr) {
            *mSampledStageNanos += elapsed;
        }
    }

private:
    SimulationWorldProfile* mProfile;
    SimulationWorldProfile::Stage mStage;
    long long* mSampledStageNanos;
    std::chrono::steady_clock::time_point mStart{};
    bool mEnabled = false;
};
#endif

#ifdef CELLULARITY_PROFILE
#define CELLULARITY_STAGE_PROFILE_POINTER mProfile
#define CELLULARITY_PROFILE_ENABLED_VALUE true
#define CELLULARITY_WORLD_PROFILE_IF if (mProfile != nullptr)
#define CELLULARITY_PROFILE_ARG mProfile
#else
#define CELLULARITY_STAGE_PROFILE_POINTER nullptr
#define CELLULARITY_PROFILE_ENABLED_VALUE false
#define CELLULARITY_WORLD_PROFILE_IF if constexpr (false)
#define CELLULARITY_PROFILE_ARG nullptr
#endif

#ifdef CELLULARITY_PERFORMANCE_HUD
#define CELLULARITY_STAGE_SAMPLE_POINTER(stage_name)                                               \
    (mStageTimingSampleActive                                                                      \
                    ? &mStageTimingWorking[static_cast<std::size_t>(SimulationWorldProfile::stage_name)] \
                    : nullptr)
#else
#define CELLULARITY_STAGE_SAMPLE_POINTER(stage_name) nullptr
#endif

#if defined(CELLULARITY_PROFILE) || defined(CELLULARITY_PERFORMANCE_HUD)
#define CELLULARITY_PROFILE_STAGE(stage_name)                  \
    ProfileStageTimer timer(CELLULARITY_STAGE_PROFILE_POINTER, \
            SimulationWorldProfile::stage_name,                      \
            CELLULARITY_STAGE_SAMPLE_POINTER(stage_name))
#else
#define CELLULARITY_PROFILE_STAGE(stage_name) ((void)0)
#endif

const Environment& validatedAllocationEnvironment(const Environment& environment) {
    cellularity::memory::validateWorldAllocation(environment);
    return environment;
}

} // namespace

SimulationWorld::SimulationWorld(bool isPreviewMode, const Environment& environment)
        : SimulationWorld(AllocationValidatedTag{},
                  isPreviewMode,
                  validatedAllocationEnvironment(environment)) {}

SimulationWorld::SimulationWorld(AllocationValidatedTag,
        bool isPreviewMode,
        const Environment& environment)
        : mEnvironment(),
          mCellData(environment.maxCellCount),
          mFoodData(environment.maxFoodCount),
          mDeadSplashData((environment.maxCellCount + 3) / 4),
          mCellSweepBroadphase(2.0 * CellData::MAX_CELL_RADIUS) {
#if defined(CELLULARITY_DEFAULT_INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP)
    mCellSweepBroadphase.setScanAlgorithm(
            CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP);
#elif defined(CELLULARITY_DEFAULT_COMPACT_PRIMARY_SWEEP)
    mCellSweepBroadphase.setScanAlgorithm(CellOverlapScanAlgorithm::COMPACT_PRIMARY_SWEEP);
#elif defined(CELLULARITY_DEFAULT_COMPACT_RECORD_SWEEP)
    mCellSweepBroadphase.setScanAlgorithm(CellOverlapScanAlgorithm::COMPACT_RECORD_SWEEP);
#endif
    mEnvironment.set(environment);
    mEnvironment.mIsPreviewMode = isPreviewMode;
    mEnvironment.maxDeadSplashCount = (mEnvironment.maxCellCount + 3) / 4;
    mOverlapDeathIntent.assign(mEnvironment.maxCellCount, false);
    initializeCellCollisionGrid();
    initializeSmellCollisionGrid();
}

void SimulationWorld::setCellOverlapScanAlgorithm(CellOverlapScanAlgorithm algorithm) noexcept {
    mCellSweepBroadphase.setScanAlgorithm(algorithm);
}

CellOverlapScanAlgorithm SimulationWorld::cellOverlapScanAlgorithm() const noexcept {
    return mCellSweepBroadphase.scanAlgorithm();
}

void SimulationWorld::initializeCellCollisionGrid() {
    const auto geometry = mEnvironment.geometry();
    int cellCollisionGridSize = std::min(206, static_cast<int>(geometry.radius() / CellData::MAX_CELL_RADIUS - 0.1)) / 2 * 2;
    mCellCollisionGrid = std::make_unique<CollisionGrid>(geometry.radius(),
            cellCollisionGridSize,
            CELL_COLLISION_GRID_COUNT_MAX,
            false);
    ++mCellCollisionGridMappingGeneration;
    if (mCellCollisionGridMappingGeneration == 0) {
        mCellCollisionGridMappingGeneration = 1;
    }
    int stride = mCellCollisionGrid->stride();
    const int slotCount = stride * stride;
    mCellRenderOverflowGridHead.assign(slotCount, -1);
    mCellRenderOverflowGridNext.assign(mEnvironment.maxCellCount, -1);
    mCellRenderOverflowGridTouched.clear();
    mCellRenderOverflowGridTouched.reserve(std::min(
            mEnvironment.maxCellCount,
            slotCount));
    mCellRenderGridOccupied.assign(
            (static_cast<std::size_t>(slotCount) + 63U) / 64U,
            0);
    mFoodSpatialGridHead.assign(slotCount, -1);
    mFoodSpatialGridNext.assign(mEnvironment.maxFoodCount, -1);
    mFoodSpatialGridTouched.clear();
    mFoodSpatialGridTouched.reserve(std::min(
            mEnvironment.maxFoodCount,
            slotCount));
    mFoodSpatialIndexFoodGeneration = 0;
    mFoodSpatialIndexGridGeneration = 0;
    mFoodSmellVisitStamp.assign(mEnvironment.maxFoodCount, 0);
}

void SimulationWorld::initializeSmellCollisionGrid() {
    const auto geometry = mEnvironment.geometry();
    int smellCollisionGridSize = std::min(207, static_cast<int>(2.0 * geometry.radius() / SMELL_RADIUS_CUTOFF));
    mSmellCollisionGrid = std::make_unique<CollisionGrid>(geometry.radius(),
            smellCollisionGridSize,
            SMELL_COLLISION_GRID_COUNT_MAX,
            true);
}

void SimulationWorld::clearGrids() {
    {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridCellTouchedBucketsCleared +=
                    mCellCollisionGrid->touchedCount();
        }
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridCellCollisionClearNanos);
        mCellCollisionGrid->clear();
    }
    {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridSmellTouchedBucketsCleared +=
                    mSmellCollisionGrid->touchedCount();
        }
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridSmellCollisionClearNanos);
        mSmellCollisionGrid->clear();
    }
    {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridRenderOverflowHeadsCleared +=
                    static_cast<long long>(mCellRenderOverflowGridTouched.size());
        }
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridRenderOverflowClearNanos);
        for (int gridIndex : mCellRenderOverflowGridTouched) {
            mCellRenderOverflowGridHead[gridIndex] = -1;
        }
        mCellRenderOverflowGridTouched.clear();
    }
    {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridRenderOccupancyWordsCleared +=
                    static_cast<long long>(mCellRenderGridOccupied.size());
        }
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridRenderOccupancyClearNanos);
        std::fill(mCellRenderGridOccupied.begin(), mCellRenderGridOccupied.end(), 0);
    }
}

bool SimulationWorld::foodSpatialIndexIsFresh() const noexcept {
    const int stride = mCellCollisionGrid->stride();
    return mFoodSpatialIndexFoodGeneration == mFoodData.spatialGeneration()
            && mFoodSpatialIndexGridGeneration
            == mCellCollisionGridMappingGeneration
            && static_cast<int>(mFoodSpatialGridHead.size()) == stride * stride
            && static_cast<int>(mFoodSpatialGridNext.size())
            >= mFoodData.MAX_FOOD_COUNT;
}

void SimulationWorld::ensureFoodSpatialIndex() {
    if (foodSpatialIndexIsFresh()) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->foodSpatialGenerationHits++;
        }
        return;
    }
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->foodSpatialGenerationMisses++;
    }
    rebuildFoodSpatialIndex();
}

void SimulationWorld::ensureFoodSpatialIndexForCapture() {
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->foodSpatialCaptureEnsureCalls++;
    }
    ensureFoodSpatialIndex();
}

void SimulationWorld::rebuildFoodSpatialIndex() {
#ifdef CELLULARITY_PROFILE
    if (mProfile != nullptr) {
        rebuildFoodSpatialIndexProfiled();
        return;
    }
#endif
    const int stride = mCellCollisionGrid->stride();
    const int slotCount = stride * stride;
    if (static_cast<int>(mFoodSpatialGridHead.size()) != slotCount) {
        mFoodSpatialGridHead.assign(slotCount, -1);
        mFoodSpatialGridTouched.clear();
        mFoodSpatialGridTouched.reserve(std::min(
                mFoodData.MAX_FOOD_COUNT,
                slotCount));
    } else {
        for (int gridIndex : mFoodSpatialGridTouched) {
            mFoodSpatialGridHead[gridIndex] = -1;
        }
        mFoodSpatialGridTouched.clear();
    }
    if (static_cast<int>(mFoodSpatialGridNext.size()) < mFoodData.MAX_FOOD_COUNT) {
        mFoodSpatialGridNext.assign(mFoodData.MAX_FOOD_COUNT, -1);
    }

    for (int foodIndex = 0; foodIndex < mFoodData.mFoodCount; foodIndex++) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->foodIndexMappingVisits++;
        }
        int gridIndex;
        if (mFoodData.mCellCollisionGridMappingGeneration[foodIndex]
                == mCellCollisionGridMappingGeneration) {
            gridIndex = mFoodData.mCellCollisionGridIndex[foodIndex];
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->foodIndexMappingCacheHits++;
            }
            if (!mFoodData.mCellCollisionGridIndexValid[foodIndex]) {
                continue;
            }
        } else {
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->foodIndexMappingCacheMisses++;
            }
            gridIndex = mCellCollisionGrid->indexAtWorldPosition(mFoodData.mPositionX[foodIndex],
                    mFoodData.mPositionY[foodIndex]);
            mFoodData.mCellCollisionGridMappingGeneration[foodIndex] =
                    mCellCollisionGridMappingGeneration;
            if (!mCellCollisionGrid->containsIndex(gridIndex)) {
                mFoodData.mCellCollisionGridIndex[foodIndex] = -1;
                mFoodData.mCellCollisionGridIndexValid[foodIndex] = false;
                CELLULARITY_WORLD_PROFILE_IF {
                    mProfile->foodIndexMappingWrites++;
                }
                continue;
            }
            mFoodData.mCellCollisionGridIndex[foodIndex] = gridIndex;
            mFoodData.mCellCollisionGridIndexValid[foodIndex] = true;
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->foodIndexMappingWrites++;
            }
        }
        if (mFoodSpatialGridHead[gridIndex] == -1) {
            mFoodSpatialGridTouched.push_back(gridIndex);
        }
        mFoodSpatialGridNext[foodIndex] = mFoodSpatialGridHead[gridIndex];
        mFoodSpatialGridHead[gridIndex] = foodIndex;
    }
    mFoodSpatialIndexFoodGeneration = mFoodData.spatialGeneration();
    mFoodSpatialIndexGridGeneration = mCellCollisionGridMappingGeneration;
}

#ifdef CELLULARITY_PROFILE
void SimulationWorld::rebuildFoodSpatialIndexProfiled() {
    CELLULARITY_PROFILE_COARSE_REGION(mProfile, foodRenderIndexBuildNanos);
    mProfile->foodRenderIndexBuildCalls++;

    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, foodRenderHeadClearNanos);
        const int stride = mCellCollisionGrid->stride();
        const int slotCount = stride * stride;
        if (static_cast<int>(mFoodSpatialGridHead.size()) != slotCount) {
            mFoodSpatialGridHead.assign(slotCount, -1);
            mFoodSpatialGridTouched.clear();
            mFoodSpatialGridTouched.reserve(std::min(
                    mFoodData.MAX_FOOD_COUNT,
                    slotCount));
            mProfile->foodRenderHeadSlotsCleared += slotCount;
        } else {
            mProfile->foodRenderHeadSlotsCleared +=
                    static_cast<long long>(mFoodSpatialGridTouched.size());
            for (int gridIndex : mFoodSpatialGridTouched) {
                mFoodSpatialGridHead[gridIndex] = -1;
            }
            mFoodSpatialGridTouched.clear();
        }
        if (static_cast<int>(mFoodSpatialGridNext.size()) < mFoodData.MAX_FOOD_COUNT) {
            mFoodSpatialGridNext.assign(mFoodData.MAX_FOOD_COUNT, -1);
        }
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, foodRenderMappingNanos);
        for (int foodIndex = 0; foodIndex < mFoodData.mFoodCount; foodIndex++) {
            mProfile->foodIndexMappingVisits++;
            mProfile->foodRenderMappingVisits++;
            if (mFoodData.mCellCollisionGridMappingGeneration[foodIndex]
                    == mCellCollisionGridMappingGeneration) {
                mProfile->foodIndexMappingCacheHits++;
                mProfile->foodRenderMappingCacheHits++;
                if (!mFoodData.mCellCollisionGridIndexValid[foodIndex]) {
                    mProfile->foodRenderOutOfBoundsSkips++;
                }
                continue;
            }

            mProfile->foodIndexMappingCacheMisses++;
            mProfile->foodRenderMappingCacheMisses++;
            const int gridIndex = mCellCollisionGrid->indexAtWorldPosition(
                    mFoodData.mPositionX[foodIndex],
                    mFoodData.mPositionY[foodIndex]);
            mFoodData.mCellCollisionGridMappingGeneration[foodIndex] =
                    mCellCollisionGridMappingGeneration;
            if (!mCellCollisionGrid->containsIndex(gridIndex)) {
                mFoodData.mCellCollisionGridIndex[foodIndex] = -1;
                mFoodData.mCellCollisionGridIndexValid[foodIndex] = false;
                mProfile->foodIndexMappingWrites++;
                mProfile->foodRenderMappingWrites++;
                mProfile->foodRenderOutOfBoundsSkips++;
                continue;
            }
            mFoodData.mCellCollisionGridIndex[foodIndex] = gridIndex;
            mFoodData.mCellCollisionGridIndexValid[foodIndex] = true;
            mProfile->foodIndexMappingWrites++;
            mProfile->foodRenderMappingWrites++;
        }
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, foodRenderInsertionNanos);
        for (int foodIndex = 0; foodIndex < mFoodData.mFoodCount; foodIndex++) {
            if (mFoodData.mCellCollisionGridMappingGeneration[foodIndex]
                            != mCellCollisionGridMappingGeneration
                    || !mFoodData.mCellCollisionGridIndexValid[foodIndex]) {
                continue;
            }
            const int gridIndex = mFoodData.mCellCollisionGridIndex[foodIndex];
            if (mFoodSpatialGridHead[gridIndex] == -1) {
                mFoodSpatialGridTouched.push_back(gridIndex);
            }
            mFoodSpatialGridNext[foodIndex] = mFoodSpatialGridHead[gridIndex];
            mFoodSpatialGridHead[gridIndex] = foodIndex;
            mProfile->foodRenderInsertions++;
        }
    }
    mFoodSpatialIndexFoodGeneration = mFoodData.spatialGeneration();
    mFoodSpatialIndexGridGeneration = mCellCollisionGridMappingGeneration;
}
#endif

void SimulationWorld::updatePhysicalGrid(int cellIndex) {
    double cellPositionX = mCellData.mPositionX[cellIndex];
    double cellPositionY = mCellData.mPositionY[cellIndex];

    mCellData.mGridIndex1[cellIndex] = mCellCollisionGrid->indexAtWorldPosition(cellPositionX, cellPositionY);
    tryAddCellToCollisionGrid(cellIndex, mCellData.mGridIndex1[cellIndex]);
}

void SimulationWorld::addCellToRenderOverflowGrid(
        int cellIndex,
        int gridPositionX,
        int gridPositionY) {
    const float cellPositionX = static_cast<float>(mCellData.mPositionX[cellIndex]);
    const float cellPositionY = static_cast<float>(mCellData.mPositionY[cellIndex]);
    if (!std::isfinite(cellPositionX) || !std::isfinite(cellPositionY)) {
        mCellRenderOverflowGridNext[cellIndex] = -1;
        return;
    }

    gridPositionX = mCellCollisionGrid->clampGridPosition(gridPositionX);
    gridPositionY = mCellCollisionGrid->clampGridPosition(gridPositionY);
    const int gridIndex = mCellCollisionGrid->indexAtGridPosition(
            gridPositionX,
            gridPositionY);
    if (mCellRenderOverflowGridHead[gridIndex] == -1) {
        mCellRenderOverflowGridTouched.push_back(gridIndex);
    }
    markCellRenderGridOccupied(gridIndex);
    mCellRenderOverflowGridNext[cellIndex] =
            mCellRenderOverflowGridHead[gridIndex];
    mCellRenderOverflowGridHead[gridIndex] = cellIndex;
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->gridCellRenderOverflowAdmissions++;
    }
}

void SimulationWorld::markCellRenderGridOccupied(int gridIndex) {
    mCellRenderGridOccupied[static_cast<std::size_t>(gridIndex) / 64U] |= std::uint64_t{1}
            << (static_cast<unsigned int>(gridIndex) & 63U);
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->gridCellRenderOccupancyMarks++;
    }
}

bool SimulationWorld::tryAddCellToCollisionGrid(int cellIndex, int gridIndex) {
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->gridCellIndexAttempts++;
    }
    const bool added = mCellCollisionGrid->add(gridIndex, cellIndex);
    CELLULARITY_WORLD_PROFILE_IF {
        if (added) {
            mProfile->gridCellIndexAdmissions++;
        } else {
            mProfile->gridCellIndexCapacitySkips++;
        }
    }
    return added;
}

void SimulationWorld::tryAddSmellToSmellGrid(int cellIndex) {
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->gridSmellCellAttempts++;
    }
    double cellPositionX = mCellData.mPositionX[cellIndex];
    double cellPositionY = mCellData.mPositionY[cellIndex];

    int gridPositionX = mSmellCollisionGrid->convertWorldToGridPosition(cellPositionX);
    int gridPositionY = mSmellCollisionGrid->convertWorldToGridPosition(cellPositionY);

    int gridIndex = mSmellCollisionGrid->indexAtGridPosition(gridPositionX, gridPositionY);
    if (!mSmellCollisionGrid->containsIndex(gridIndex)) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridSmellCellOutOfBoundsSkips++;
        }
        return;
    }
    int count = mSmellCollisionGrid->countAt(gridIndex);
    if (count < SMELL_COLLISION_GRID_COUNT_MAX) {
        mSmellCollisionGrid->add(gridIndex, cellIndex, SMELL_TYPE_CELL);
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridSmellCellAdmissions++;
        }

        if (mCellData.mActiveCellType[cellIndex] == CellType::SECROCYTE
                && mCellData.mNitrates[cellIndex] >= CellData::MIN_SECRETE_NITRATES) {
            const cellularity::GeneView gene =
                    mCellData.getActiveGeneView(cellIndex);
            int secreteType =
                    gene.integerValue(Gene::SECRETE_TYPE_INTEGER_INDEX);

            int smellType = -1;
            if (secreteType == Gene::SECRETE_TYPE_FOOD_SMELL) {
                smellType = SMELL_TYPE_FOOD;
            }
            if (secreteType == Gene::SECRETE_TYPE_COATED_FOOD_SMELL) {
                smellType = SMELL_TYPE_FAT;
            }
            if (secreteType == Gene::SECRETE_TYPE_LIGHT_SMELL) {
                smellType = SMELL_TYPE_LIGHT;
            }
            if (secreteType == Gene::SECRETE_TYPE_WALL_SMELL) {
                smellType = SMELL_TYPE_WALL;
            }

            if (smellType != -1) {
                mSmellCollisionGrid->add(gridIndex,
                        -1 - cellIndex,
                        static_cast<int16_t>(smellType));
                CELLULARITY_WORLD_PROFILE_IF {
                    mProfile->gridSmellSecretionAdmissions++;
                }
            }
        }
    } else {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridSmellCellCapacitySkips++;
        }
    }
}

bool SimulationWorld::addFoodSmellToGrid(int foodIndex) {
    int gridPositionX = mSmellCollisionGrid->convertWorldToGridPosition(mFoodData.mPositionX[foodIndex]);
    int gridPositionY = mSmellCollisionGrid->convertWorldToGridPosition(mFoodData.mPositionY[foodIndex]);

    int smellGridIndex = mSmellCollisionGrid->indexAtGridPosition(gridPositionX, gridPositionY);
    if (!mSmellCollisionGrid->containsIndex(smellGridIndex)) {
        return false;
    }
    if (mFoodSmellGridConsumerMaskActive
            && !mFoodSmellGridConsumerMask.empty()
            && !mFoodSmellGridConsumerMask[smellGridIndex]) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->eatFoodSmellGridMaskSkips++;
        }
        return false;
    }

    int count = mSmellCollisionGrid->countAt(smellGridIndex);
    if (count >= SMELL_COLLISION_GRID_COUNT_MAX) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->eatFoodSmellGridCapacitySkips++;
        }
        return false;
    }

    mSmellCollisionGrid->add(smellGridIndex,
            foodIndex,
            mFoodData.mLipidLayer[foodIndex] <= 0.0f ? SMELL_TYPE_FOOD : SMELL_TYPE_FAT);
    return true;
}

void SimulationWorld::addFoodSmellsToGridForConsumers() {
    if (!mHasFoodSmellConsumers || mFoodData.mFoodCount <= 0 || mFoodSmellGridConsumerMask.empty()) {
        return;
    }

    if (mFoodSmellVisitGeneration == std::numeric_limits<int>::max()) {
        std::fill(mFoodSmellVisitStamp.begin(), mFoodSmellVisitStamp.end(), 0);
        mFoodSmellVisitGeneration = 1;
    } else {
        mFoodSmellVisitGeneration++;
    }
    if (static_cast<int>(mFoodSmellVisitStamp.size()) < mFoodData.MAX_FOOD_COUNT) {
        mFoodSmellVisitStamp.assign(mFoodData.MAX_FOOD_COUNT, 0);
    }

    const int smellStride = mSmellCollisionGrid->stride();
    const int smellGridSize = mSmellCollisionGrid->gridSize();
    const int cellStride = mCellCollisionGrid->stride();
    const double worldRadius = mSmellCollisionGrid->worldRadius();
    const int visitGeneration = mFoodSmellVisitGeneration;

    for (int smellGridIndex = 0; smellGridIndex < static_cast<int>(mFoodSmellGridConsumerMask.size()); smellGridIndex++) {
        if (!mFoodSmellGridConsumerMask[smellGridIndex]) {
            continue;
        }

        const int smellGridX = smellGridIndex % smellStride;
        const int smellGridY = smellGridIndex / smellStride;
        const double minWorldX = ((static_cast<double>(smellGridX - 1) / smellGridSize) * 2.0 - 1.0) * worldRadius;
        const double maxWorldX = ((static_cast<double>(smellGridX) / smellGridSize) * 2.0 - 1.0) * worldRadius;
        const double minWorldY = ((static_cast<double>(smellGridY - 1) / smellGridSize) * 2.0 - 1.0) * worldRadius;
        const double maxWorldY = ((static_cast<double>(smellGridY) / smellGridSize) * 2.0 - 1.0) * worldRadius;
        const int minCellGridX = mCellCollisionGrid->clampGridPosition(
                mCellCollisionGrid->convertWorldToGridPosition(minWorldX) - 1);
        const int maxCellGridX = mCellCollisionGrid->clampGridPosition(
                mCellCollisionGrid->convertWorldToGridPosition(maxWorldX) + 1);
        const int minCellGridY = mCellCollisionGrid->clampGridPosition(
                mCellCollisionGrid->convertWorldToGridPosition(minWorldY) - 1);
        const int maxCellGridY = mCellCollisionGrid->clampGridPosition(
                mCellCollisionGrid->convertWorldToGridPosition(maxWorldY) + 1);

        for (int cellGridY = minCellGridY; cellGridY <= maxCellGridY; cellGridY++) {
            for (int cellGridX = minCellGridX; cellGridX <= maxCellGridX; cellGridX++) {
                const int cellGridIndex = cellGridX + cellGridY * cellStride;
                for (int foodIndex = mFoodSpatialGridHead[cellGridIndex];
                        foodIndex != -1;
                        foodIndex = mFoodSpatialGridNext[foodIndex]) {
                    if (mFoodSmellVisitStamp[foodIndex] == visitGeneration) {
                        continue;
                    }
                    if (mFoodData.mMass[foodIndex] <= 0.0f) {
                        continue;
                    }

                    const int candidateSmellGridIndex = mSmellCollisionGrid->indexAtWorldPosition(
                            mFoodData.mPositionX[foodIndex],
                            mFoodData.mPositionY[foodIndex]);
                    if (candidateSmellGridIndex != smellGridIndex) {
                        continue;
                    }

                    mFoodSmellVisitStamp[foodIndex] = visitGeneration;
                    CELLULARITY_WORLD_PROFILE_IF {
                        mProfile->eatFoodSmellGridAttempts++;
                    }
                    const bool addedToSmellGrid = addFoodSmellToGrid(foodIndex);
                    CELLULARITY_WORLD_PROFILE_IF {
                        if (!addedToSmellGrid) {
                            continue;
                        }
                        mProfile->eatFoodSmellGridAdds++;
                    }
                }
            }
        }
    }
}

bool SimulationWorld::isAnySmellConsumer(int cellIndex) const {
    CellType cellType = mCellData.mActiveCellType[cellIndex];
    return cellType == CellType::SENSEOCYTE || cellType == CellType::STEREOCYTE;
}

bool SimulationWorld::isSmellGridConsumer(int cellIndex) const {
    if (!isAnySmellConsumer(cellIndex)) {
        return false;
    }

    const cellularity::GeneView gene =
            mCellData.getActiveGeneView(cellIndex);
    int smellType = gene.integerValue(Gene::SMELL_TYPE_INTEGER_INDEX);
    return smellType == SMELL_TYPE_CELL
            || smellType == SMELL_TYPE_FOOD
            || smellType == SMELL_TYPE_FAT
            || smellType == SMELL_TYPE_LIGHT
            || smellType == SMELL_TYPE_WALL;
}

bool SimulationWorld::isFoodSmellConsumer(int cellIndex) const {
    if (!isAnySmellConsumer(cellIndex)) {
        return false;
    }

    const cellularity::GeneView gene =
            mCellData.getActiveGeneView(cellIndex);
    int smellType = gene.integerValue(Gene::SMELL_TYPE_INTEGER_INDEX);
    return smellType == SMELL_TYPE_FOOD || smellType == SMELL_TYPE_FAT;
}

void SimulationWorld::updateSpatialConsumerFlags() {
    mHasAnySmellConsumers = false;
    mHasSmellGridConsumers = false;
    mHasFoodSmellConsumers = false;
    mHasFoodPhysicsQueryConsumers = false;

    for (int cellIndex = 0; cellIndex < mCellData.mCellCount; cellIndex++) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridConsumerCellsVisited++;
        }
        if (mCellData.mIsDead[cellIndex]) {
            continue;
        }

        const CellType cellType = mCellData.mActiveCellType[cellIndex];
        if (cellType == CellType::PHAGOCYTE
                || cellType == CellType::SECROCYTE
                || (mEnvironment.mobileFood
                        && cellType == CellType::CILIOCYTE)) {
            mHasFoodPhysicsQueryConsumers = true;
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->gridFoodPhysicsConsumerMatches++;
            }
        }

        if (isAnySmellConsumer(cellIndex)) {
            mHasAnySmellConsumers = true;
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->gridAnySmellConsumerMatches++;
            }
            const cellularity::GeneView gene =
                    mCellData.getActiveGeneView(cellIndex);
            int smellType = gene.integerValue(Gene::SMELL_TYPE_INTEGER_INDEX);
            if (smellType == SMELL_TYPE_CELL
                    || smellType == SMELL_TYPE_FOOD
                    || smellType == SMELL_TYPE_FAT
                    || smellType == SMELL_TYPE_LIGHT
                    || smellType == SMELL_TYPE_WALL) {
                mHasSmellGridConsumers = true;
                CELLULARITY_WORLD_PROFILE_IF {
                    mProfile->gridSmellGridConsumerMatches++;
                }
            }
            if (smellType == SMELL_TYPE_FOOD || smellType == SMELL_TYPE_FAT) {
                mHasFoodSmellConsumers = true;
                CELLULARITY_WORLD_PROFILE_IF {
                    mProfile->gridFoodSmellConsumerMatches++;
                }
            }
        }

        if (mHasSmellGridConsumers && mHasFoodSmellConsumers
                && mHasFoodPhysicsQueryConsumers) {
            break;
        }
    }
}

void SimulationWorld::updateFoodSmellGridConsumerMask() {
    if (!mHasFoodSmellConsumers) {
        mFoodSmellGridConsumerMask.clear();
        return;
    }

    int smellGridEntryCount = mSmellCollisionGrid->stride() * mSmellCollisionGrid->stride();
    mFoodSmellGridConsumerMask.assign(smellGridEntryCount, 0);
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->gridFoodSmellMaskSlotsCleared += smellGridEntryCount;
    }

    for (int cellIndex = 0; cellIndex < mCellData.mCellCount; cellIndex++) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridFoodSmellMaskCellsVisited++;
        }
        if (mCellData.mIsDead[cellIndex] || !isFoodSmellConsumer(cellIndex)) {
            continue;
        }
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->gridFoodSmellMaskConsumerCells++;
        }

        int gridPositionX = mSmellCollisionGrid->convertWorldToGridPosition(mCellData.mPositionX[cellIndex]);
        int gridPositionY = mSmellCollisionGrid->convertWorldToGridPosition(mCellData.mPositionY[cellIndex]);
        for (int offsetIndexX = -1; offsetIndexX < 2; offsetIndexX++) {
            for (int offsetIndexY = -1; offsetIndexY < 2; offsetIndexY++) {
                int localGridPositionX = gridPositionX + offsetIndexX;
                int localGridPositionY = gridPositionY + offsetIndexY;

                if (!mSmellCollisionGrid->containsGridPosition(localGridPositionX, localGridPositionY)) {
                    continue;
                }

                int smellGridIndex = mSmellCollisionGrid->indexAtGridPosition(localGridPositionX, localGridPositionY);
                mFoodSmellGridConsumerMask[smellGridIndex] = 1;
                CELLULARITY_WORLD_PROFILE_IF {
                    mProfile->gridFoodSmellMaskWrites++;
                }
            }
        }
    }
}

#ifdef CELLULARITY_PROFILE
void SimulationWorld::updateCellSpatialIndexesProfiled() {
    constexpr std::uint8_t RENDER_OVERFLOW = 0;
    constexpr std::uint8_t PHYSICAL_GRID_ADMITTED = 1;

    const int cellCount = mCellData.mCellCount;
    if (static_cast<int>(mProfileCellGridPositionX.size()) < cellCount) {
        mProfileCellGridPositionX.resize(mCellData.MAX_CELL_COUNT);
        mProfileCellGridPositionY.resize(mCellData.MAX_CELL_COUNT);
        mProfileCellGridAdmission.resize(mCellData.MAX_CELL_COUNT);
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridCellPositionConversionNanos);
        for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
            mProfileCellGridPositionX[cellIndex] =
                    mCellCollisionGrid->convertWorldToGridPosition(
                            mCellData.mPositionX[cellIndex]);
            mProfileCellGridPositionY[cellIndex] =
                    mCellCollisionGrid->convertWorldToGridPosition(
                            mCellData.mPositionY[cellIndex]);
        }
        mProfile->gridCellPositionConversions += cellCount;
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridSmellCellInsertionNanos);
        if (mHasSmellGridConsumers) {
            for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
                if (!mCellData.mIsDead[cellIndex]) {
                    tryAddSmellToSmellGrid(cellIndex);
                }
            }
        }
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridPhysicalCellInsertionNanos);
        for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
            if (mCellData.mIsDead[cellIndex]) {
                mProfileCellGridAdmission[cellIndex] = RENDER_OVERFLOW;
                continue;
            }
            const int gridIndex = mCellCollisionGrid->indexAtGridPosition(
                    mProfileCellGridPositionX[cellIndex],
                    mProfileCellGridPositionY[cellIndex]);
            mCellData.mGridIndex1[cellIndex] = gridIndex;
            mProfileCellGridAdmission[cellIndex] =
                    tryAddCellToCollisionGrid(cellIndex, gridIndex)
                    ? PHYSICAL_GRID_ADMITTED
                    : RENDER_OVERFLOW;
        }
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridRenderOccupancyNanos);
        for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
            if (mProfileCellGridAdmission[cellIndex]
                    == PHYSICAL_GRID_ADMITTED) {
                markCellRenderGridOccupied(mCellData.mGridIndex1[cellIndex]);
            }
        }
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(
                mProfile, gridRenderOverflowNanos);
        for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
            if (mProfileCellGridAdmission[cellIndex]
                    == PHYSICAL_GRID_ADMITTED) {
                continue;
            }
            addCellToRenderOverflowGrid(
                    cellIndex,
                    mProfileCellGridPositionX[cellIndex],
                    mProfileCellGridPositionY[cellIndex]);
        }
    }
}
#endif

void SimulationWorld::updateGrids(bool buildFoodSpatialIndex) {
    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, gridCacheInvalidationNanos);
        invalidateCellContactCache();
    }
    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, gridClearNanos);
        clearGrids();
    }
    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, gridConsumerDiscoveryNanos);
        updateSpatialConsumerFlags();
    }
    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, gridFoodSmellMaskNanos);
        updateFoodSmellGridConsumerMask();
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, gridCellAndSmellBuildNanos);
#ifdef CELLULARITY_PROFILE
        if (mProfile != nullptr) {
            updateCellSpatialIndexesProfiled();
        } else
#endif
        {
            for (int cellIndex = 0; cellIndex < mCellData.mCellCount; cellIndex++) {
                const int gridPositionX = mCellCollisionGrid->convertWorldToGridPosition(mCellData.mPositionX[cellIndex]);
                const int gridPositionY = mCellCollisionGrid->convertWorldToGridPosition(mCellData.mPositionY[cellIndex]);
                if (mCellData.mIsDead[cellIndex]) {
                    addCellToRenderOverflowGrid(cellIndex, gridPositionX, gridPositionY);
                    continue;
                }
                if (mHasSmellGridConsumers) {
                    tryAddSmellToSmellGrid(cellIndex);
                }
                mCellData.mGridIndex1[cellIndex] = mCellCollisionGrid->indexAtGridPosition(gridPositionX, gridPositionY);
                if (tryAddCellToCollisionGrid(cellIndex, mCellData.mGridIndex1[cellIndex])) {
                    markCellRenderGridOccupied(mCellData.mGridIndex1[cellIndex]);
                } else {
                    addCellToRenderOverflowGrid(cellIndex, gridPositionX, gridPositionY);
                }
            }
        }
    }

    if (buildFoodSpatialIndex || mHasFoodSmellConsumers
            || mHasFoodPhysicsQueryConsumers) {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, gridFoodRenderBuildNanos);
        ensureFoodSpatialIndex();
    }
}

void SimulationWorld::processKnownCellOverlapPair(int firstCellIndex,
        int secondCellIndex,
        double distanceX,
        double distanceY,
        double distanceSq,
        double firstRadius,
        double secondRadius) {
#if defined(CELLULARITY_SEMANTIC_TRACE)
    const auto traceRow = [this](int cellIndex) -> std::int32_t {
        if (cellIndex < 0 || cellIndex >= mCellData.mCellCount) {
            return std::numeric_limits<std::int32_t>::min();
        }
        return static_cast<std::int32_t>(std::floor(
                mCellData.mPositionY[cellIndex]
                / (2.0 * CellData::MAX_CELL_RADIUS)));
    };
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Sweep,
            cellularity::verification::SemanticEventKind::SweepPair,
            cellularity::verification::traceI32(firstCellIndex),
            cellularity::verification::traceI32(secondCellIndex),
            cellularity::verification::traceI32(traceRow(firstCellIndex)),
            cellularity::verification::traceI32(traceRow(secondCellIndex)),
            cellularity::verification::traceF64(distanceX),
            cellularity::verification::traceF64(distanceY),
            cellularity::verification::traceF64(distanceSq),
            cellularity::verification::traceF64(firstRadius),
            cellularity::verification::traceF64(secondRadius));
#endif
    if (firstCellIndex == secondCellIndex
            || firstCellIndex < 0
            || secondCellIndex < 0
            || firstCellIndex >= mCellData.mCellCount
            || secondCellIndex >= mCellData.mCellCount
            || mCellData.mIsDead[firstCellIndex]
            || mCellData.mIsDead[secondCellIndex]
            || mOverlapDeathIntent[firstCellIndex]
            || mOverlapDeathIntent[secondCellIndex]) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Endpoint,
                cellularity::verification::SemanticEventKind::EndpointPairSkipped,
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceI32(1));
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->skippedSelfOrDead++;
        }
        return;
    }

    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->potentialChecks++;
    }

    if (distanceSq == 0.0) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Endpoint,
                cellularity::verification::SemanticEventKind::EndpointPairSkipped,
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceI32(2));
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->skippedNoContact++;
        }
        return;
    }

    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->overlapSemanticPairs++;
    }

    const double firstRadiusSq = mCellData.mRadiusSq[firstCellIndex];
    const double secondRadiusSq = mCellData.mRadiusSq[secondCellIndex];
    if (mCellData.mActiveCellType[firstCellIndex] == CellType::GAMETE
            && mCellData.mActiveCellType[secondCellIndex] == CellType::GAMETE
            && mCellData.mAge[firstCellIndex] > CellData::AGE_OF_CONSENT
            && mCellData.mAge[secondCellIndex] > CellData::AGE_OF_CONSENT) {
        const double fertilizationThreshold = CellData::MIN_CELL_RADIUS * CellData::MIN_CELL_RADIUS * 0.01;
        if (std::abs(firstRadiusSq - secondRadiusSq) > distanceSq - fertilizationThreshold) {
            CELLULARITY_SEMANTIC_EVENT(
                    cellularity::verification::TraceChannel::Endpoint,
                    cellularity::verification::SemanticEventKind::EndpointFertilizationTarget,
                    cellularity::verification::traceI32(firstCellIndex),
                    cellularity::verification::traceI32(secondCellIndex),
                    cellularity::verification::traceI32(mCellData.mFertilizationTarget[firstCellIndex]),
                    cellularity::verification::traceI32(mCellData.mFertilizationTarget[secondCellIndex]));
            mCellData.mFertilizationTarget[firstCellIndex] = secondCellIndex;
            mCellData.mFertilizationTarget[secondCellIndex] = firstCellIndex;
            mHasFertilizationIntents = true;
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->fertilizationTargets += 2;
            }
            return;
        }
    }

    bool firstDies = secondRadiusSq - firstRadiusSq > distanceSq;
    bool secondDies = firstRadiusSq - secondRadiusSq > distanceSq;

    if (firstDies) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Endpoint,
                cellularity::verification::SemanticEventKind::EndpointContainmentDeath,
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceI32(mCellData.cellCollisionContactCount(firstCellIndex)));
        mOverlapDeathIntent[firstCellIndex] = true;
        mCellData.clearCellCollisionContacts(firstCellIndex);
        mCellData.mFertilizationTarget[firstCellIndex] = -1;
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->overlapDeathIntents++;
        }
    }
    if (secondDies) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Endpoint,
                cellularity::verification::SemanticEventKind::EndpointContainmentDeath,
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(mCellData.cellCollisionContactCount(secondCellIndex)));
        mOverlapDeathIntent[secondCellIndex] = true;
        mCellData.clearCellCollisionContacts(secondCellIndex);
        mCellData.mFertilizationTarget[secondCellIndex] = -1;
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->overlapDeathIntents++;
        }
    }
    if (firstDies || secondDies) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->earlyStops++;
        }
        return;
    }

    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->overlapEndpointAdmissionAttempts += 2;
    }
    bool firstWritten = mCellData.addKnownCellCollisionContact(firstCellIndex,
            secondCellIndex,
            distanceX,
            distanceY,
            distanceSq,
            firstRadius,
            secondRadius);
    bool secondWritten = mCellData.addKnownCellCollisionContact(secondCellIndex,
            firstCellIndex,
            -distanceX,
            -distanceY,
            distanceSq,
            secondRadius,
            firstRadius);
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Endpoint,
            cellularity::verification::SemanticEventKind::EndpointAdmission,
            cellularity::verification::traceI32(firstCellIndex),
            cellularity::verification::traceI32(secondCellIndex),
            cellularity::verification::traceBool(firstWritten),
            cellularity::verification::traceI32(mCellData.cellCollisionContactCount(firstCellIndex)),
            cellularity::verification::traceF64(distanceX),
            cellularity::verification::traceF64(distanceY));
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Endpoint,
            cellularity::verification::SemanticEventKind::EndpointAdmission,
            cellularity::verification::traceI32(secondCellIndex),
            cellularity::verification::traceI32(firstCellIndex),
            cellularity::verification::traceBool(secondWritten),
            cellularity::verification::traceI32(mCellData.cellCollisionContactCount(secondCellIndex)),
            cellularity::verification::traceF64(-distanceX),
            cellularity::verification::traceF64(-distanceY));
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->collisionWrites += firstWritten ? 1 : 0;
        mProfile->collisionWrites += secondWritten ? 1 : 0;
        mProfile->collisionWriteCapacitySkips += firstWritten ? 0 : 1;
        mProfile->collisionWriteCapacitySkips += secondWritten ? 0 : 1;
    }
}

void SimulationWorld::processSweepKnownCellOverlapPairFast(int firstCellIndex,
        int secondCellIndex,
        double distanceX,
        double distanceY,
        double distanceSq,
        [[maybe_unused]] double firstRadius,
        [[maybe_unused]] double secondRadius) {
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Sweep,
            cellularity::verification::SemanticEventKind::SweepPair,
            cellularity::verification::traceI32(firstCellIndex),
            cellularity::verification::traceI32(secondCellIndex),
            cellularity::verification::traceI32(static_cast<std::int32_t>(std::floor(
                    mCellData.mPositionY[firstCellIndex]
                    / (2.0 * CellData::MAX_CELL_RADIUS)))),
            cellularity::verification::traceI32(static_cast<std::int32_t>(std::floor(
                    mCellData.mPositionY[secondCellIndex]
                    / (2.0 * CellData::MAX_CELL_RADIUS)))),
            cellularity::verification::traceF64(distanceX),
            cellularity::verification::traceF64(distanceY),
            cellularity::verification::traceF64(distanceSq),
            cellularity::verification::traceF64(firstRadius),
            cellularity::verification::traceF64(secondRadius));
    if (mCellData.mIsDead[firstCellIndex]
            || mCellData.mIsDead[secondCellIndex]
            || mOverlapDeathIntent[firstCellIndex]
            || mOverlapDeathIntent[secondCellIndex]) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Endpoint,
                cellularity::verification::SemanticEventKind::EndpointPairSkipped,
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceI32(1));
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->skippedSelfOrDead++;
        }
        return;
    }

    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->potentialChecks++;
    }

    if (distanceSq == 0.0) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Endpoint,
                cellularity::verification::SemanticEventKind::EndpointPairSkipped,
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceI32(2));
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->skippedNoContact++;
        }
        return;
    }

    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->overlapSemanticPairs++;
    }

    const double firstRadiusSq = mCellData.mRadiusSq[firstCellIndex];
    const double secondRadiusSq = mCellData.mRadiusSq[secondCellIndex];
    if (mCellData.mActiveCellType[firstCellIndex] == CellType::GAMETE
            && mCellData.mActiveCellType[secondCellIndex] == CellType::GAMETE
            && mCellData.mAge[firstCellIndex] > CellData::AGE_OF_CONSENT
            && mCellData.mAge[secondCellIndex] > CellData::AGE_OF_CONSENT) {
        const double fertilizationThreshold = CellData::MIN_CELL_RADIUS * CellData::MIN_CELL_RADIUS * 0.01;
        if (std::abs(firstRadiusSq - secondRadiusSq) > distanceSq - fertilizationThreshold) {
            CELLULARITY_SEMANTIC_EVENT(
                    cellularity::verification::TraceChannel::Endpoint,
                    cellularity::verification::SemanticEventKind::EndpointFertilizationTarget,
                    cellularity::verification::traceI32(firstCellIndex),
                    cellularity::verification::traceI32(secondCellIndex),
                    cellularity::verification::traceI32(mCellData.mFertilizationTarget[firstCellIndex]),
                    cellularity::verification::traceI32(mCellData.mFertilizationTarget[secondCellIndex]));
            mCellData.mFertilizationTarget[firstCellIndex] = secondCellIndex;
            mCellData.mFertilizationTarget[secondCellIndex] = firstCellIndex;
            mHasFertilizationIntents = true;
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->fertilizationTargets += 2;
            }
            return;
        }
    }

    const bool firstDies = secondRadiusSq - firstRadiusSq > distanceSq;
    const bool secondDies = firstRadiusSq - secondRadiusSq > distanceSq;

    if (firstDies) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Endpoint,
                cellularity::verification::SemanticEventKind::EndpointContainmentDeath,
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceI32(mCellData.cellCollisionContactCount(firstCellIndex)));
        mOverlapDeathIntent[firstCellIndex] = true;
        mCellData.clearCellCollisionContacts(firstCellIndex);
        mCellData.mFertilizationTarget[firstCellIndex] = -1;
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->overlapDeathIntents++;
        }
    }
    if (secondDies) {
        CELLULARITY_SEMANTIC_EVENT(
                cellularity::verification::TraceChannel::Endpoint,
                cellularity::verification::SemanticEventKind::EndpointContainmentDeath,
                cellularity::verification::traceI32(secondCellIndex),
                cellularity::verification::traceI32(firstCellIndex),
                cellularity::verification::traceI32(mCellData.cellCollisionContactCount(secondCellIndex)));
        mOverlapDeathIntent[secondCellIndex] = true;
        mCellData.clearCellCollisionContacts(secondCellIndex);
        mCellData.mFertilizationTarget[secondCellIndex] = -1;
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->overlapDeathIntents++;
        }
    }
    if (firstDies || secondDies) {
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->earlyStops++;
        }
        return;
    }

    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->overlapEndpointAdmissionAttempts += 2;
    }
    const bool firstWritten = mCellData.addKnownCellCollisionContactFast(firstCellIndex,
            secondCellIndex,
            distanceX,
            distanceY,
            distanceSq);
    const bool secondWritten = mCellData.addKnownCellCollisionContactFast(secondCellIndex,
            firstCellIndex,
            -distanceX,
            -distanceY,
            distanceSq);
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Endpoint,
            cellularity::verification::SemanticEventKind::EndpointAdmission,
            cellularity::verification::traceI32(firstCellIndex),
            cellularity::verification::traceI32(secondCellIndex),
            cellularity::verification::traceBool(firstWritten),
            cellularity::verification::traceI32(mCellData.cellCollisionContactCount(firstCellIndex)),
            cellularity::verification::traceF64(distanceX),
            cellularity::verification::traceF64(distanceY));
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Endpoint,
            cellularity::verification::SemanticEventKind::EndpointAdmission,
            cellularity::verification::traceI32(secondCellIndex),
            cellularity::verification::traceI32(firstCellIndex),
            cellularity::verification::traceBool(secondWritten),
            cellularity::verification::traceI32(mCellData.cellCollisionContactCount(secondCellIndex)),
            cellularity::verification::traceF64(-distanceX),
            cellularity::verification::traceF64(-distanceY));
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->collisionWrites += firstWritten ? 1 : 0;
        mProfile->collisionWrites += secondWritten ? 1 : 0;
        mProfile->collisionWriteCapacitySkips += firstWritten ? 0 : 1;
        mProfile->collisionWriteCapacitySkips += secondWritten ? 0 : 1;
    }
}

void SimulationWorld::processCellOverlapsSweepPairs() {
#ifdef CELLULARITY_PROFILE
    const bool collectProfile = mProfile != nullptr;
    std::chrono::steady_clock::time_point buildStart;
    if (collectProfile) {
        buildStart = std::chrono::steady_clock::now();
    }
#else
    constexpr bool collectProfile = false;
#endif

    mCellSweepBroadphase.buildConfigured(mCellData, collectProfile);

#ifdef CELLULARITY_PROFILE
    std::chrono::steady_clock::time_point scanStart;
    if (collectProfile) {
        scanStart = std::chrono::steady_clock::now();
        mProfile->overlapBroadphaseBuildNanos +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                        scanStart - buildStart)
                        .count();
    }
#endif

    auto visitor = [this](int firstCellIndex,
                           int secondCellIndex,
                           double distanceX,
                           double distanceY,
                           double distanceSq,
                           double firstRadius,
                           double secondRadius) {
        processSweepKnownCellOverlapPairFast(firstCellIndex,
                secondCellIndex,
                distanceX,
                distanceY,
                distanceSq,
                firstRadius,
                secondRadius);
    };
    mCellSweepBroadphase.forEachOverlappingPairConfiguredInline(
            visitor, collectProfile);

#ifdef CELLULARITY_PROFILE
    if (collectProfile) {
        mProfile->overlapBroadphaseScanNanos +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - scanStart)
                        .count();
        const CellSweepBroadphase::Stats& stats = mCellSweepBroadphase.stats();
        mProfile->overlapCandidatePairs += stats.candidatePairs;
        mProfile->overlapSameRowCandidatePairs += stats.sameRowCandidatePairs;
        mProfile->overlapNextRowCandidatePairs += stats.nextRowCandidatePairs;
        mProfile->overlapSameRowCandidateRanges += stats.sameRowCandidateRanges;
        mProfile->overlapNextRowCandidateRanges += stats.nextRowCandidateRanges;
        mProfile->overlapExactPairs += stats.exactPairs;
        mProfile->overlapScalarChecks += stats.scalarChecks;
        mProfile->overlapSimdChecks += stats.simdChecks;
        mProfile->overlapRowsReused += stats.rowsReused;
        mProfile->overlapRowsRelayouts += stats.rowRelayouts;
        mProfile->overlapRowChangedCells += stats.rowChangedCells;
        mProfile->overlapLayoutRebuilds += stats.layoutRebuilds;
        mProfile->overlapInsertionSortRows += stats.insertionSortRows;
        mProfile->overlapFullSortRows += stats.fullSortRows;
        mProfile->overlapInsertionMoves += stats.insertionMoves;
        mProfile->overlapCompactProjectionNanos += stats.compactProjectionNanos;
        mProfile->overlapCompactClassifyNanos += stats.compactClassifyNanos;
        mProfile->overlapCompactLayoutNanos += stats.compactLayoutNanos;
        mProfile->overlapCompactScatterNanos += stats.compactScatterNanos;
        mProfile->overlapCompactMaterializeNanos += stats.compactMaterializeNanos;
        mProfile->overlapCompactSortNanos += stats.compactSortNanos;
        mProfile->overlapCompactFinalizeNanos += stats.compactFinalizeNanos;
        mProfile->overlapCompactPayloadRefreshes += stats.compactPayloadRefreshes;
        mProfile->overlapCompactScatterIdWrites += stats.compactScatterIdWrites;
        mProfile->overlapCompactRecordMaterializations +=
                stats.compactRecordMaterializations;
        mProfile->overlapCompactRecordSortWrites += stats.compactRecordSortWrites;
        mProfile->overlapCompactFullSortInputRecords +=
                stats.compactFullSortInputRecords;
    }
#endif
}

void SimulationWorld::applyCollisionOverlapDeathIntents() {
    int cellCount = mCellData.mCellCount;
    bool hasDeaths = false;
    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        if (!mOverlapDeathIntent[cellIndex]) {
            continue;
        }

        mCellData.markCellAsDead(cellIndex);
        mCellData.clearCellCollisionContacts(cellIndex);
        mCellData.mFertilizationTarget[cellIndex] = -1;
        hasDeaths = true;
        CELLULARITY_WORLD_PROFILE_IF {
            mProfile->deathsApplied++;
        }
    }

    if (!hasDeaths) {
        return;
    }

    for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
        if (mCellData.mIsDead[cellIndex]) {
            continue;
        }

        int fertilizationTarget = mCellData.mFertilizationTarget[cellIndex];
        if (fertilizationTarget >= 0 && mCellData.mIsDead[fertilizationTarget]) {
            mCellData.mFertilizationTarget[cellIndex] = -1;
        }
    }
    mCellData.removeCollisionContactsTargetingDeadCells(
            CELLULARITY_PROFILE_ARG);
}

void SimulationWorld::processCellOverlaps() {
    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, overlapSetupNanos);
        invalidateCellContactCache();
        mHasFertilizationIntents = false;
#if defined(CELLULARITY_DEFAULT_INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP)
        // Fuse the packed broadphase snapshot with the mandatory per-cell
        // overlap reset so production does not traverse the SoA a second time.
        const int cellCount = mCellData.mCellCount;
        CellIncrementalBucketedF64Broadphase::PackedSource32* const packedSource =
                mCellSweepBroadphase.beginIncrementalPackedSourceWrite(
                        static_cast<std::size_t>(cellCount));
        const double* const positionX = mCellData.mPositionX.data();
        const double* const positionY = mCellData.mPositionY.data();
        const double* const radius = mCellData.mRadius.data();
        double* const radiusSq = mCellData.mRadiusSq.data();
        std::uint8_t* const deathIntent = mOverlapDeathIntent.data();
        int* const contactCount =
                mCellData.mCellContactBuffer.cellContactCount.data();
        int* const fertilizationTarget =
                mCellData.mFertilizationTarget.data();
        for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
            const double cellRadius = radius[cellIndex];
            radiusSq[cellIndex] = cellRadius * cellRadius;
            deathIntent[cellIndex] = false;
            contactCount[cellIndex] = 0;
            fertilizationTarget[cellIndex] = -1;
            packedSource[cellIndex] = {
                    positionX[cellIndex],
                    positionY[cellIndex],
                    cellRadius,
                    0.0
            };
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->targetCells++;
                if (mCellData.mIsDead[cellIndex]) {
                    mProfile->skippedDeadTargets++;
                }
                if (mCellData.isCellReadyForFertilization(cellIndex)) {
                    mProfile->readyForFertilizationTargets++;
                }
            }
        }
        mCellSweepBroadphase.commitIncrementalPackedSourceWrite();
#else
        mCellData.refreshAllRadiusSq();
        std::fill(mOverlapDeathIntent.begin(), mOverlapDeathIntent.begin() + mCellData.mCellCount, false);
        mCellData.clearCellCollisionContacts();
        for (int cellIndex = 0; cellIndex < mCellData.mCellCount; cellIndex++) {
            CELLULARITY_WORLD_PROFILE_IF {
                mProfile->targetCells++;
                if (mCellData.mIsDead[cellIndex]) {
                    mProfile->skippedDeadTargets++;
                }
                if (mCellData.isCellReadyForFertilization(cellIndex)) {
                    mProfile->readyForFertilizationTargets++;
                }
            }
            mCellData.mFertilizationTarget[cellIndex] = -1;
        }
#endif
    }
    processCellOverlapsSweepPairs();
    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, overlapDeathCleanupNanos);
        applyCollisionOverlapDeathIntents();
    }
    mCellContactCacheFresh = true;
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::ContactCacheFreshness,
            cellularity::verification::traceBool(true),
            cellularity::verification::traceI32(2));
}

void SimulationWorld::addCorpseFood(int targetCellIndex) {
    if (mFoodData.mFoodCount >= mFoodData.MAX_FOOD_COUNT) {
        return;
    }

    double corpseFoodMass = mCellData.mMass[targetCellIndex] * CellData::CORPSE_EFFICIENCY;
    CELLULARITY_WORLD_PROFILE_IF {
        mProfile->foodMassSpawnedFromCorpses += corpseFoodMass;
    }
    mFoodData.addFood(mCellData.mPositionX[targetCellIndex],
            mCellData.mPositionY[targetCellIndex],
            corpseFoodMass,
            mCellData.mActiveCellType[targetCellIndex] == CellType::LIPOCYTE ? 0.5f : 0.0f,
            mEnvironment);
}

void SimulationWorld::addDeadSplash(int cellIndex) {
    if (mDeadSplashData.mDeadSplashCount >= mDeadSplashData.MAX_DEAD_SPLASH_COUNT) {
        return;
    }

    if (mEnvironment.geometry().isOutside(mCellData.mPositionX[cellIndex], mCellData.mPositionY[cellIndex])) {
        return;
    }

    mDeadSplashData.addDeadSplash(
            static_cast<float>(mCellData.mPositionX[cellIndex]),
            static_cast<float>(mCellData.mPositionY[cellIndex]),
            static_cast<float>(mCellData.mRadius[cellIndex]),
            mCellData.mColor[cellIndex]);
}

void SimulationWorld::removeCellImmediately(int targetCellIndex) {
    invalidateCellContactCache();
    int lastCellIndex = mCellData.mCellCount - 1;
    mCellData.mTopology.removeAllLinksOfCell(targetCellIndex);

    addDeadSplash(targetCellIndex);
    mCellData.markCellAsDead(targetCellIndex);
    mCellData.moveLastCellForCompaction(lastCellIndex, targetCellIndex);
    if (targetCellIndex != lastCellIndex) {
        mCellData.mTopology.moveCellIndex(lastCellIndex, targetCellIndex);
    }
    mCellData.releaseLastCellSlot();
}

void SimulationWorld::removeDeadCells() {
    for (int targetCellIndex = 0; targetCellIndex < mCellData.mCellCount; targetCellIndex++) {
        bool isOutOfBounds = false;
        if (mEnvironment.geometry().isOutsideOrOnBoundary(mCellData.mPositionX[targetCellIndex], mCellData.mPositionY[targetCellIndex])) {
            mCellData.markCellAsDead(targetCellIndex);
            isOutOfBounds = true;
        }

        if (!mCellData.mIsDead[targetCellIndex]) {
            continue;
        }

        if (!isOutOfBounds && !mEnvironment.mIsPreviewMode) {
            addCorpseFood(targetCellIndex);
        }

        removeCellImmediately(targetCellIndex);
        targetCellIndex--;
    }
}

void SimulationWorld::removeFoodImmediately(int foodIndex) {
    mFoodData.removeFood(foodIndex);
}

void SimulationWorld::eatFoods(double deltaTime) {
    mFoodSmellGridConsumerMaskActive = true;
    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, eatFoodDecayNanos);
        if (mEnvironment.mTestFoodDecay != 0.0) {
            for (int foodIndex = 0; foodIndex < mFoodData.mFoodCount; foodIndex++) {
                CELLULARITY_WORLD_PROFILE_IF {
                    mProfile->eatFoodVisits++;
                }
                double oldFoodMass = mFoodData.mMass[foodIndex];
                double decayedFoodMass = std::min(oldFoodMass, mEnvironment.mTestFoodDecay * deltaTime);
                mFoodData.mMass[foodIndex] -= static_cast<float>(mEnvironment.mTestFoodDecay * deltaTime);
                CELLULARITY_WORLD_PROFILE_IF {
                    mProfile->foodMassDecayed += decayedFoodMass;
                }

                if (mFoodData.mMass[foodIndex] < 0.0f) {
                    CELLULARITY_WORLD_PROFILE_IF {
                        mProfile->eatFoodDecayRemovals++;
                    }
                    removeFoodImmediately(foodIndex);
                    foodIndex--;
                    continue;
                }

                if (mFoodData.mCellCollisionGridMappingGeneration[foodIndex]
                        == mCellCollisionGridMappingGeneration) {
                    CELLULARITY_WORLD_PROFILE_IF {
                        mProfile->eatFoodGridIndexCacheHits++;
                    }
                } else {
                    CELLULARITY_WORLD_PROFILE_IF {
                        mProfile->eatFoodGridIndexCacheMisses++;
                    }
                }
            }
        }
    }

    if (mHasFoodPhysicsQueryConsumers) {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, eatFoodPreQueryIndexBuildNanos);
        ensureFoodSpatialIndex();
    }
    if (mHasFoodPhysicsQueryConsumers) {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, eatFoodQueryAndUpdateNanos);
        int gridStride = mCellCollisionGrid->stride();
        for (int cellIndex = 0; cellIndex < mCellData.mCellCount; cellIndex++) {
            if (mCellData.mIsDead[cellIndex]) {
                continue;
            }
            CellType cellType = mCellData.mActiveCellType[cellIndex];
            bool canInteractWithFood = cellType == CellType::PHAGOCYTE || cellType == CellType::SECROCYTE || (mEnvironment.mobileFood && cellType == CellType::CILIOCYTE);
            if (!canInteractWithFood) {
                continue;
            }

            int cellGridIndex = mCellData.mGridIndex1[cellIndex];
            if (!mCellCollisionGrid->containsIndex(cellGridIndex)) {
                cellGridIndex = mCellCollisionGrid->indexAtWorldPosition(mCellData.mPositionX[cellIndex],
                        mCellData.mPositionY[cellIndex]);
                if (!mCellCollisionGrid->containsIndex(cellGridIndex)) {
                    continue;
                }
            }

            double cellPositionX = mCellData.mPositionX[cellIndex];
            double cellPositionY = mCellData.mPositionY[cellIndex];
            double cellRadius = mCellData.mRadius[cellIndex];
            double collisionDistance = cellRadius + FoodData::RADIUS;
            double collisionDistSq = collisionDistance * collisionDistance;
            int centerGridX = cellGridIndex % gridStride;
            int centerGridY = cellGridIndex / gridStride;
            for (int gridY = centerGridY - 1; gridY <= centerGridY + 1; gridY++) {
                if (gridY < 0 || gridY >= gridStride) {
                    continue;
                }
                for (int gridX = centerGridX - 1; gridX <= centerGridX + 1; gridX++) {
                    if (gridX < 0 || gridX >= gridStride) {
                        continue;
                    }
                    int gridIndex = gridX + gridY * gridStride;
                    CELLULARITY_WORLD_PROFILE_IF {
                        mProfile->eatFoodNeighborGridVisits++;
                    }
                    for (int foodIndex = mFoodSpatialGridHead[gridIndex]; foodIndex != -1;
                            foodIndex = mFoodSpatialGridNext[foodIndex]) {
                        CELLULARITY_WORLD_PROFILE_IF {
                            mProfile->eatFoodCandidateChecks++;
                            mProfile->eatFoodNeighborGridEntries++;
                        }
                        if (mFoodData.mMass[foodIndex] <= 0.0f) {
                            continue;
                        }

                        double distanceX = cellPositionX - mFoodData.mPositionX[foodIndex];
                        double distanceY = cellPositionY - mFoodData.mPositionY[foodIndex];
                        double distanceSq = distanceX * distanceX + distanceY * distanceY;

                        if (distanceSq >= collisionDistSq) {
                            continue;
                        }
                        CELLULARITY_WORLD_PROFILE_IF {
                            mProfile->eatFoodContactChecks++;
                        }

                        if (mEnvironment.mobileFood && distanceSq < cellRadius * cellRadius) {
                            CELLULARITY_WORLD_PROFILE_IF {
                                mProfile->eatFoodMobileInnerPushes++;
                            }
                            double distance = std::sqrt(distanceSq);
                            double forceMagnitude =
                                    (CellData::TURGOR_PRESSURE * (cellRadius - distance) * 3.0) / distance;
                            mFoodData.mVelocityX[foodIndex] += static_cast<float>(
                                    ((mFoodData.mPositionX[foodIndex] - cellPositionX) * forceMagnitude) * deltaTime);
                            mFoodData.mVelocityY[foodIndex] += static_cast<float>(
                                    ((mFoodData.mPositionY[foodIndex] - cellPositionY) * forceMagnitude) * deltaTime);
                        }

                        if (cellType == CellType::CILIOCYTE) {
                            CELLULARITY_WORLD_PROFILE_IF {
                                mProfile->eatFoodCiliocyteContacts++;
                            }
                            double distance = std::sqrt(distanceSq);
                            double nx = (mFoodData.mPositionX[foodIndex] - cellPositionX) / distance;
                            double ny = (mFoodData.mPositionY[foodIndex] - cellPositionY) / distance;
                            double dx = std::cos(mCellData.mAngle[cellIndex]);
                            double dy = std::sin(mCellData.mAngle[cellIndex]);
                            double cross = ny * dx - nx * dy;
                            double cv;
                            if ((0 < cross) != mCellData.mIsMirrored[cellIndex]) {
                                cv = cross * MAX_TAX_SPEED * mCellData.getValue(cellIndex, Gene::MYOCYTE_BENDING_VALUE_INDEX, 0);
                            } else {
                                cv = cross * MAX_TAX_SPEED * mCellData.getValue(cellIndex, Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0);
                            }
                            double dv = 40.0 * (cv - ny * (mFoodData.mVelocityX[foodIndex] - mCellData.mVelocityX[cellIndex]) + nx * (mFoodData.mVelocityY[foodIndex] - mCellData.mVelocityY[cellIndex]) - mCellData.mAngularVelocity[cellIndex] * cellRadius);
                            mFoodData.mVelocityX[foodIndex] += static_cast<float>((-0.12 * nx + ny * dv) * deltaTime);
                            mFoodData.mVelocityY[foodIndex] += static_cast<float>((-0.12 * ny - nx * dv) * deltaTime);
                        }

                        if (cellType == CellType::SECROCYTE) {
                            CELLULARITY_WORLD_PROFILE_IF {
                                mProfile->eatFoodSecrocyteContacts++;
                            }
                            const cellularity::GeneView cellActiveGene =
                                    mCellData.getActiveGeneView(cellIndex);
                            if (cellActiveGene.integerValue(
                                        Gene::SECRETE_TYPE_INTEGER_INDEX)
                                    == Gene::SECRETE_TYPE_LIPASE) {
                                mFoodData.mLipidLayer[foodIndex] -= static_cast<float>(deltaTime);
                            }
                        }

                        if (cellType == CellType::PHAGOCYTE) {
                            CELLULARITY_WORLD_PROFILE_IF {
                                mProfile->eatFoodPhagocyteContacts++;
                            }
                            if (mFoodData.mLipidLayer[foodIndex] > 0.0f) {
                                continue;
                            }
                            double eatMax = std::min(static_cast<double>(mFoodData.mMass[foodIndex]),
                                    CellData::MAX_MASS - mCellData.mMass[cellIndex]);
                            double deltaMass =
                                    deltaTime * CellData::MAX_EAT_SPEED * mEnvironment.mTestPhagocyteEatSpeedScale;
                            if (eatMax <= 0.0) {
                                continue;
                            }
                            CELLULARITY_WORLD_PROFILE_IF {
                                mProfile->eatFoodPhagocyteEatEvents++;
                            }

                            mCellData.mVelocityX[cellIndex] *= mCellData.mMass[cellIndex];
                            mCellData.mVelocityY[cellIndex] *= mCellData.mMass[cellIndex];

                            if (deltaMass < eatMax) {
                                mCellData.mMass[cellIndex] += deltaMass;
                                mCellData.mVelocityX[cellIndex] /= mCellData.mMass[cellIndex];
                                mCellData.mVelocityY[cellIndex] /= mCellData.mMass[cellIndex];
                                mFoodData.mMass[foodIndex] -= static_cast<float>(deltaMass);
                                CELLULARITY_WORLD_PROFILE_IF {
                                    mProfile->foodMassConsumedByCells += deltaMass;
                                }
                            } else {
                                mCellData.mMass[cellIndex] += eatMax;
                                mCellData.mVelocityX[cellIndex] /= mCellData.mMass[cellIndex];
                                mCellData.mVelocityY[cellIndex] /= mCellData.mMass[cellIndex];
                                mFoodData.mMass[foodIndex] -= static_cast<float>(eatMax);
                                CELLULARITY_WORLD_PROFILE_IF {
                                    mProfile->foodMassConsumedByCells += eatMax;
                                }

                                if (mFoodData.mMass[foodIndex] <= 0.0f) {
                                    CELLULARITY_WORLD_PROFILE_IF {
                                        mProfile->eatFoodConsumedRemovals++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, eatFoodRemovalNanos);
        for (int foodIndex = 0; foodIndex < mFoodData.mFoodCount; foodIndex++) {
            if (mFoodData.mMass[foodIndex] > 0.0f) {
                continue;
            }
            removeFoodImmediately(foodIndex);
            foodIndex--;
        }
    }

    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, eatFoodMotionNanos);
        if (mEnvironment.mobileFood) {
            const auto geometry = mEnvironment.geometry();
            bool foodPositionsChanged = false;
            for (int foodIndex = 0; foodIndex < mFoodData.mFoodCount; foodIndex++) {
                const float oldPositionX = mFoodData.mPositionX[foodIndex];
                const float oldPositionY = mFoodData.mPositionY[foodIndex];
                float newVelocityX = mFoodData.mVelocityX[foodIndex];
                float newVelocityY = mFoodData.mVelocityY[foodIndex];
                newVelocityX -=
                        static_cast<float>(0.2 * mFoodData.mVelocityX[foodIndex] * mEnvironment.mViscosity * deltaTime);
                newVelocityY -=
                        static_cast<float>(0.2 * mFoodData.mVelocityY[foodIndex] * mEnvironment.mViscosity * deltaTime);

                mFoodData.mPositionX[foodIndex] += newVelocityX * static_cast<float>(deltaTime);
                mFoodData.mPositionY[foodIndex] += newVelocityY * static_cast<float>(deltaTime);
                CELLULARITY_WORLD_PROFILE_IF {
                    mProfile->eatFoodMovedFoods++;
                }

                std::array<double, 2> innerHit;
                const bool crossedInnerBoundary = geometry.clipMotionAtInnerBoundary(
                        oldPositionX, oldPositionY, mFoodData.mPositionX[foodIndex], mFoodData.mPositionY[foodIndex], innerHit);
                if (crossedInnerBoundary) {
                    mFoodData.mPositionX[foodIndex] = static_cast<float>(innerHit[0]);
                    mFoodData.mPositionY[foodIndex] = static_cast<float>(innerHit[1]);
                }
                if (crossedInnerBoundary || geometry.isOutsideOrOnBoundary(mFoodData.mPositionX[foodIndex], mFoodData.mPositionY[foodIndex])) {
                    if (mEnvironment.dieEdge) {
                        CELLULARITY_WORLD_PROFILE_IF {
                            mProfile->eatFoodBoundaryRemovals++;
                            mProfile->foodMassRemovedAtBoundary += mFoodData.mMass[foodIndex];
                        }
                        removeFoodImmediately(foodIndex);
                        foodIndex--;
                        continue;
                    }

                    float distance = std::sqrt(mFoodData.mPositionX[foodIndex] * mFoodData.mPositionX[foodIndex] + mFoodData.mPositionY[foodIndex] * mFoodData.mPositionY[foodIndex]);
                    const auto boundaryPoint = geometry.projectToBoundary(mFoodData.mPositionX[foodIndex], mFoodData.mPositionY[foodIndex], distance);
                    mFoodData.mPositionX[foodIndex] = boundaryPoint[0];
                    mFoodData.mPositionY[foodIndex] = boundaryPoint[1];
                    float dp;
                    if (geometry.isRing()) {
                        // Reflect using the wall's unit normal, not the length
                        // of a point that may have stepped inside the hole.
                        const float px = boundaryPoint[0];
                        const float py = boundaryPoint[1];
                        dp = 2.0f * (newVelocityX * px + newVelocityY * py) / (px * px + py * py);
                        const float normalVelocity = newVelocityX * px + newVelocityY * py;
                        const bool inner = crossedInnerBoundary || distance <= geometry.innerRadius();
                        if ((inner && normalVelocity >= 0.0f) || (!inner && normalVelocity <= 0.0f)) {
                            dp = 0.0f;
                        }
                    } else {
                        dp = 2.0f * (newVelocityX * mFoodData.mPositionX[foodIndex] + newVelocityY * mFoodData.mPositionY[foodIndex]) / (distance * distance);
                    }
                    newVelocityX -= dp * mFoodData.mPositionX[foodIndex];
                    newVelocityY -= dp * mFoodData.mPositionY[foodIndex];
                    CELLULARITY_WORLD_PROFILE_IF {
                        mProfile->eatFoodBoundaryReflections++;
                    }
                }

                mFoodData.mVelocityX[foodIndex] = newVelocityX;
                mFoodData.mVelocityY[foodIndex] = newVelocityY;
                if (mFoodData.mPositionX[foodIndex] != oldPositionX
                        || mFoodData.mPositionY[foodIndex] != oldPositionY) {
                    mFoodData.invalidateCellCollisionGridMapping(foodIndex);
                    foodPositionsChanged = true;
                }
            }
            if (foodPositionsChanged) {
                mFoodData.markSpatialStateChanged();
            }
        }
    }
    {
        CELLULARITY_PROFILE_COARSE_REGION(mProfile, eatFoodSmellIndexBuildNanos);
        if (mHasFoodSmellConsumers) {
            ensureFoodSpatialIndex();
            addFoodSmellsToGridForConsumers();
        }
    }
    mFoodSmellGridConsumerMaskActive = false;
}

void SimulationWorld::updateCellSmellInteraction(int targetCellIndex) {
    if (mCellData.mIsDead[targetCellIndex]) {
        return;
    }

    CellType targetCellType = mCellData.mActiveCellType[targetCellIndex];
    if (targetCellType != CellType::STEREOCYTE && targetCellType != CellType::SENSEOCYTE) {
        return;
    }

    const cellularity::GeneView targetCellGene =
            mCellData.getActiveGeneView(targetCellIndex);

    double targetCellPositionX = mCellData.mPositionX[targetCellIndex];
    double targetCellPositionY = mCellData.mPositionY[targetCellIndex];

    double cos = 0.0;
    double sin = 0.0;
    if (targetCellType == CellType::STEREOCYTE) {
        double targetCellAngle = mCellData.mAngle[targetCellIndex];
        cos = std::cos(targetCellAngle);
        sin = std::sin(targetCellAngle);
    }

    float resultSmellSignalValue = 0.0f;
    int smellType =
            targetCellGene.integerValue(Gene::SMELL_TYPE_INTEGER_INDEX);

    if (smellType == SMELL_TYPE_LIGHT) {
        if (targetCellType == CellType::SENSEOCYTE) {
            resultSmellSignalValue += static_cast<float>(mEnvironment.getLight(targetCellPositionX, targetCellPositionY) * 0.2);
        } else {
            resultSmellSignalValue += static_cast<float>(-((mEnvironment.getLightddx(targetCellPositionX, targetCellPositionY) * sin
                                                                 - mEnvironment.getLightddy(targetCellPositionX, targetCellPositionY) * cos))
                    * 0.2);
        }
    }

    if (smellType == SMELL_TYPE_VELOCITY) {
        double targetCellVelocityX = mCellData.mVelocityX[targetCellIndex];
        double targetCellVelocityY = mCellData.mVelocityY[targetCellIndex];
        if (targetCellType == CellType::SENSEOCYTE) {
            resultSmellSignalValue += static_cast<float>(std::sqrt(targetCellVelocityX * targetCellVelocityX
                                                                 + targetCellVelocityY * targetCellVelocityY)
                    * 3.0);
        } else {
            resultSmellSignalValue += static_cast<float>((targetCellVelocityX * sin - targetCellVelocityY * cos) * 3.0);
        }
    }

    if (smellType == SMELL_TYPE_WALL) {
        double senseRadius = SMELL_RADIUS * 3.0;
        const auto boundary = mEnvironment.geometry().boundaryLocation(targetCellPositionX, targetCellPositionY);
        const double distance = boundary.centerDistance;
        double closeness = std::max(std::min(1.0, 1.0 - boundary.clearance / senseRadius), 0.0);
        if (targetCellType == CellType::SENSEOCYTE) {
            resultSmellSignalValue += static_cast<float>(closeness);
        } else if (distance != 0.0) {
            resultSmellSignalValue += static_cast<float>(-closeness * (targetCellPositionX * sin - targetCellPositionY * cos) / distance * boundary.normalDirection);
        }
    }

    if (smellType == SMELL_TYPE_CELL
            || smellType == SMELL_TYPE_FOOD
            || smellType == SMELL_TYPE_FAT
            || smellType == SMELL_TYPE_LIGHT
            || smellType == SMELL_TYPE_WALL) {
        int gridPositionX = mSmellCollisionGrid->convertWorldToGridPosition(targetCellPositionX);
        int gridPositionY = mSmellCollisionGrid->convertWorldToGridPosition(targetCellPositionY);
        for (int offsetIndexX = -1; offsetIndexX < 2; offsetIndexX++) {
            for (int offsetIndexY = -1; offsetIndexY < 2; offsetIndexY++) {
                int localGridPositionX = gridPositionX + offsetIndexX;
                int localGridPositionY = gridPositionY + offsetIndexY;

                if (!mSmellCollisionGrid->containsGridPosition(localGridPositionX, localGridPositionY)) {
                    continue;
                }

                int smellGridIndex = mSmellCollisionGrid->indexAtGridPosition(localGridPositionX, localGridPositionY);
                int entryOffset = mSmellCollisionGrid->entryOffsetAt(smellGridIndex);
                int smellGridCount = mSmellCollisionGrid->countAt(smellGridIndex);
                for (int j = 0; j < smellGridCount; j++) {
                    int smellEntryOffset = entryOffset + j;
                    if (mSmellCollisionGrid->typeAtOffset(smellEntryOffset) != smellType) {
                        continue;
                    }

                    int smellGridIndexValue = mSmellCollisionGrid->indexAtOffset(smellEntryOffset);
                    if (smellType == SMELL_TYPE_CELL
                            && smellGridIndexValue == targetCellIndex) {
                        continue;
                    }

                    if (smellType == SMELL_TYPE_CELL) {
                        float distanceRed =
                                targetCellGene.floatValue(
                                        Gene::FloatType::SMELL_RED)
                                - mCellData.mColor[smellGridIndexValue][0];
                        float distanceGreen =
                                targetCellGene.floatValue(
                                        Gene::FloatType::SMELL_GREEN)
                                - mCellData.mColor[smellGridIndexValue][1];
                        float distanceBlue =
                                targetCellGene.floatValue(
                                        Gene::FloatType::SMELL_BLUE)
                                - mCellData.mColor[smellGridIndexValue][2];
                        float smellThreshold = targetCellGene.floatValue(
                                Gene::FloatType::SMELL_THRESHOLD);

                        if (distanceRed * distanceRed + distanceGreen * distanceGreen + distanceBlue * distanceBlue >= smellThreshold * smellThreshold) {
                            continue;
                        }

                        double distanceX = targetCellPositionX - mCellData.mPositionX[smellGridIndexValue];
                        double distanceY = targetCellPositionY - mCellData.mPositionY[smellGridIndexValue];
                        double r = std::sqrt(distanceX * distanceX + distanceY * distanceY);

                        if (r <= 0.0) {
                            continue;
                        }

                        if (targetCellType == CellType::SENSEOCYTE) {
                            resultSmellSignalValue += static_cast<float>(mCellData.mMass[smellGridIndexValue]
                                    * BesselLookup::k0(r / SMELL_RADIUS)
                                    * CellData::SMELL_SENSITIVITY);
                        } else {
                            resultSmellSignalValue += static_cast<float>(mCellData.mMass[smellGridIndexValue]
                                    * BesselLookup::k1(r / SMELL_RADIUS)
                                    * (distanceX * sin - distanceY * cos) / r
                                    * CellData::SMELL_SENSITIVITY);
                        }

                    } else {
                        double r = 0.0;
                        double distanceX = 0.0;
                        double distanceY = 0.0;
                        double smellStrength = 0.0;

                        if (smellGridIndexValue < 0) {
                            constexpr float LONGER = 0.25f;
                            int smelledCellIndex = -(smellGridIndexValue + 1);
                            double smelledCellPositionX = mCellData.mPositionX[smelledCellIndex];
                            double smelledCellPositionY = mCellData.mPositionY[smelledCellIndex];
                            double smelledCellMass = mCellData.mMass[smelledCellIndex];

                            distanceX = (targetCellPositionX - smelledCellPositionX) * LONGER;
                            distanceY = (targetCellPositionY - smelledCellPositionY) * LONGER;
                            r = std::sqrt(distanceX * distanceX + distanceY * distanceY);
                            smellStrength = smelledCellMass * CellData::SMELL_SENSITIVITY;
                        } else {
                            int smelledFoodIndex = smellGridIndexValue;
                            float smelledFoodLipidLayer = mFoodData.mLipidLayer[smelledFoodIndex];
                            float smelledFoodMass = mFoodData.mMass[smelledFoodIndex];
                            float smelledFoodPositionX = mFoodData.mPositionX[smelledFoodIndex];
                            float smelledFoodPositionY = mFoodData.mPositionY[smelledFoodIndex];

                            if ((smelledFoodLipidLayer <= 0.0f && smellType == SMELL_TYPE_FOOD)
                                    || (smelledFoodLipidLayer > 0.0f && smellType == SMELL_TYPE_FAT)) {
                                distanceX = targetCellPositionX - smelledFoodPositionX;
                                distanceY = targetCellPositionY - smelledFoodPositionY;
                                r = std::sqrt(distanceX * distanceX + distanceY * distanceY);
                                smellStrength = smelledFoodMass * CellData::SMELL_SENSITIVITY;
                            }
                        }

                        if (r > 0.0) {
                            if (targetCellType == CellType::SENSEOCYTE) {
                                resultSmellSignalValue += static_cast<float>(smellStrength * BesselLookup::k0(r / SMELL_RADIUS));
                            } else {
                                resultSmellSignalValue += static_cast<float>(smellStrength
                                        * BesselLookup::k1(r / SMELL_RADIUS)
                                        * (distanceX * sin - distanceY * cos) / r);
                            }
                        }
                    }
                }
            }
        }
    }

    if (targetCellType == CellType::STEREOCYTE) {
        resultSmellSignalValue *= (mCellData.mIsMirrored[targetCellIndex] ? -1.0f : 1.0f);
    }

    int smellOutIndex =
            targetCellGene.integerValue(Gene::SMELL_OUT_INTEGER_INDEX);
    float output = targetCellGene.floatValue(Gene::FloatType::SMELL_AMPLITUDE)
            * resultSmellSignalValue;
    output = std::max(std::min(output, CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT), -CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT);
    mCellData.mSignalSubstanceOut[targetCellIndex][smellOutIndex] += output;
}

void SimulationWorld::updateAllCellSmellInteractions() {
    if (!mHasAnySmellConsumers) {
        return;
    }

    for (int cellIndex = 0; cellIndex < mCellData.mCellCount; cellIndex++) {
        updateCellSmellInteraction(cellIndex);
    }
}

void SimulationWorld::updateAllCellInteractionsPartial(bool isForRendering, double deltaTime) {
    {
        CELLULARITY_PROFILE_STAGE(CELL_CELL_COLLISIONS);
        mCellData.handleAllCellToCellCollisionsPartial(
                deltaTime,
                mEnvironment,
                CELLULARITY_PROFILE_ARG);
    }
    {
        CELLULARITY_PROFILE_STAGE(LINK_INTERACTIONS);
        mCellData.updateAllCellLinkInteractions(deltaTime, CELLULARITY_PROFILE_ARG, isForRendering);
    }
    {
        CELLULARITY_PROFILE_STAGE(WALL_COLLISIONS);
        mCellData.handleAllCellToBoundaryCollisionsPartial(mEnvironment);
    }
    {
        CELLULARITY_PROFILE_STAGE(SMELL_INTERACTIONS);
        updateAllCellSmellInteractions();
    }
    {
        CELLULARITY_PROFILE_STAGE(CELL_FORCES);
        mCellData.updateAllCellForces(deltaTime, mEnvironment, CELLULARITY_PROFILE_ARG);
    }
}

void SimulationWorld::applyNewCellMetabolism() {
    const hn::CappedTag<double, 4> d;
    const int laneCount = static_cast<int>(hn::Lanes(d));
    int cellIndex = 0;
    if (laneCount > 1) {
        for (; cellIndex + laneCount <= mCellData.mCellCount; cellIndex += laneCount) {
            const auto newMass = hn::LoadU(d, mCellData.mNewMass.data() + cellIndex);
            const auto nanMask = hn::Ne(newMass, newMass);
            if (!hn::AllFalse(d, nanMask)) {
                for (int lane = 0; lane < laneCount; lane++) {
                    int scalarCellIndex = cellIndex + lane;
                    if (std::isnan(mCellData.mNewMass[scalarCellIndex])) {
                        mCellData.mNewMass[scalarCellIndex] = 0.0;
                        mCellData.markCellAsDead(scalarCellIndex);
                        continue;
                    }

                    mCellData.mMass[scalarCellIndex] = mCellData.mNewMass[scalarCellIndex];
                    mCellData.mToxin[scalarCellIndex] = mCellData.mNewToxin[scalarCellIndex];
                    mCellData.mNitrates[scalarCellIndex] = mCellData.mNewNitrates[scalarCellIndex];
                }
                continue;
            }

            hn::StoreU(newMass, d, mCellData.mMass.data() + cellIndex);
            const auto newNitrates = hn::LoadU(d, mCellData.mNewNitrates.data() + cellIndex);
            hn::StoreU(newNitrates, d, mCellData.mNitrates.data() + cellIndex);
            for (int lane = 0; lane < laneCount; lane++) {
                int scalarCellIndex = cellIndex + lane;
                mCellData.mToxin[scalarCellIndex] = mCellData.mNewToxin[scalarCellIndex];
            }
        }
    }

    for (; cellIndex < mCellData.mCellCount; cellIndex++) {
        if (std::isnan(mCellData.mNewMass[cellIndex])) {
            mCellData.mNewMass[cellIndex] = 0.0;
            mCellData.markCellAsDead(cellIndex);
            continue;
        }

        mCellData.mMass[cellIndex] = mCellData.mNewMass[cellIndex];
        mCellData.mToxin[cellIndex] = mCellData.mNewToxin[cellIndex];
        mCellData.mNitrates[cellIndex] = mCellData.mNewNitrates[cellIndex];
    }
}

void SimulationWorld::update1Partial(bool isForRendering, double speed) {
    update1Partial(isForRendering, speed, nullptr);
}

void SimulationWorld::update1Partial(
        bool isForRendering,
        double speed,
        cellularity::execution::SimulationExecutionContext* executionContext) {
    // C-01 carries the session-owned execution boundary to SimulationWorld without
    // changing any physics stage. C-03 will consume it for exact signals.
    (void)executionContext;
    double deltaTime = DELTA_TIME * speed;
    {
        CELLULARITY_PROFILE_STAGE(TIME_LIGHT);
        mTime += deltaTime;
        mEnvironment.updateLight(deltaTime);
    }

    {
        CELLULARITY_PROFILE_STAGE(RANDOM_FOOD_CELLS);
        if (!mEnvironment.mIsPreviewMode) {
            double foodMassBefore = 0.0;
            CELLULARITY_WORLD_PROFILE_IF {
                foodMassBefore = mFoodData.getFoodMass();
            }
            mFoodData.addRandomFood(deltaTime, mEnvironment);
            CELLULARITY_WORLD_PROFILE_IF {
                double foodMassAfter = mFoodData.getFoodMass();
                if (foodMassAfter > foodMassBefore) {
                    mProfile->foodMassSpawned += foodMassAfter - foodMassBefore;
                }
            }
            mCellData.addRandomCells(mEnvironment);
        }
    }

    {
        CELLULARITY_PROFILE_STAGE(KINEMATICS);
        mCellData.updateCellsKinematics(deltaTime);
    }
    {
        CELLULARITY_PROFILE_STAGE(SPLIT);
        mCellData.handleCellSplit(*this);
    }
    {
        CELLULARITY_PROFILE_STAGE(RESET_FORCES);
        mCellData.resetCellForces();
    }
    {
        CELLULARITY_PROFILE_STAGE(FLAGELLOCYTES);
        mCellData.updateFlagellocytesForce(deltaTime, mEnvironment);
    }
    {
        CELLULARITY_PROFILE_STAGE(REMOVE_DEAD);
        removeDeadCells();
    }

    {
        CELLULARITY_PROFILE_STAGE(UPDATE_GRIDS);
        updateGrids(mEnvironment.mIsPreviewMode);
    }
    {
        CELLULARITY_PROFILE_STAGE(EAT_FOODS);
        if (!mEnvironment.mIsPreviewMode) {
            eatFoods(deltaTime);
        }
    }
    {
        CELLULARITY_PROFILE_STAGE(DEAD_SPLASHES);
        mDeadSplashData.updateDeadSplashes(deltaTime);
    }
    {
        CELLULARITY_PROFILE_STAGE(OVERLAPS);
        processCellOverlaps();
    }
    {
        CELLULARITY_PROFILE_STAGE(FERTILIZATION);
        if (mHasFertilizationIntents) {
            if (mCellData.resolveFertilizationIntents(
                        mEnvironment, CELLULARITY_PROFILE_ARG)) {
                invalidateCellContactCache();
            }
        }
    }

    updateAllCellInteractionsPartial(isForRendering, deltaTime);
    {
        CELLULARITY_PROFILE_STAGE(VELOCITIES);
        mCellData.updateCellVelocities(mEnvironment, deltaTime);
    }
    {
        CELLULARITY_PROFILE_STAGE(CILIOCYTES);
        mCellData.updateCiliocyteInteractions(mEnvironment);
    }
}

void SimulationWorld::update(bool isForRendering, double speed) {
    update(isForRendering, speed, nullptr);
}

void SimulationWorld::update(
        bool isForRendering,
        double speed,
        cellularity::execution::SimulationExecutionContext* executionContext) {
    update1(isForRendering, speed, executionContext);
    update2(isForRendering, speed, executionContext);
}

void SimulationWorld::update1(bool isForRendering, double speed) {
    update1(isForRendering, speed, nullptr);
}

void SimulationWorld::update1(
        bool isForRendering,
        double speed,
        cellularity::execution::SimulationExecutionContext* executionContext) {
    if (updateStage != UPDATE_STAGE_CAN_TOUCH) {
        throw std::runtime_error("Tried to run update1 too early");
    }

#ifdef CELLULARITY_PERFORMANCE_HUD
    beginStageTimingSample();
#endif
    updateStage++;
    update1Partial(isForRendering, speed, executionContext);
    updateStage++;
}

void SimulationWorld::update2Partial(double speed) {
    update2Partial(speed, nullptr);
}

void SimulationWorld::update2Partial(
        double speed,
        cellularity::execution::SimulationExecutionContext* executionContext) {
    (void)executionContext;
    double deltaTime = DELTA_TIME * speed;
    {
        CELLULARITY_PROFILE_STAGE(METABOLISM);
        mCellData.updateCellsMetabolism(deltaTime, mEnvironment, CELLULARITY_PROFILE_ARG);
    }
    {
        CELLULARITY_PROFILE_STAGE(APPLY_METABOLISM);
        applyNewCellMetabolism();
    }
    {
        CELLULARITY_PROFILE_STAGE(SIGNALS);
        mCellData.updateSignals(deltaTime, CELLULARITY_PROFILE_ARG);
    }
}

void SimulationWorld::setProfile(SimulationWorldProfile* profile) {
#ifdef CELLULARITY_PROFILE
    if (profile != nullptr) {
        mProfileCellGridPositionX.resize(mCellData.MAX_CELL_COUNT);
        mProfileCellGridPositionY.resize(mCellData.MAX_CELL_COUNT);
        mProfileCellGridAdmission.resize(mCellData.MAX_CELL_COUNT);
    }
#endif
    mProfile = profile;
}

SimulationWorldProfile* SimulationWorld::profile() const {
    return mProfile;
}

void SimulationWorld::requestStageTimingSample() noexcept {
#ifdef CELLULARITY_PERFORMANCE_HUD
    mStageTimingSampleRequested = true;
#endif
}

const SimulationWorldStageTimingSnapshot& SimulationWorld::stageTimingSnapshot() const noexcept {
    return mStageTimingSnapshot;
}

void SimulationWorld::beginStageTimingSample() noexcept {
#ifdef CELLULARITY_PERFORMANCE_HUD
    if (!mStageTimingSampleRequested) {
        mStageTimingSampleActive = false;
        return;
    }
    mStageTimingWorking.fill(0);
    mStageTimingSampleRequested = false;
    mStageTimingSampleActive = true;
#endif
}

void SimulationWorld::finishStageTimingSample() noexcept {
#ifdef CELLULARITY_PERFORMANCE_HUD
    if (!mStageTimingSampleActive) {
        return;
    }

    constexpr double kSmoothingWeight = 0.25;
    const bool firstSample = mStageTimingSnapshot.sequence == 0;
    double totalNanos = 0.0;
    for (std::size_t stage = 0; stage < mStageTimingWorking.size(); ++stage) {
        const double sampledNanos = static_cast<double>(mStageTimingWorking[stage]);
        double& smoothedNanos = mStageTimingSnapshot.stageNanos[stage];
        smoothedNanos = firstSample
                ? sampledNanos
                : smoothedNanos + (sampledNanos - smoothedNanos) * kSmoothingWeight;
        totalNanos += smoothedNanos;
    }
    mStageTimingSnapshot.totalNanos = totalNanos;
    ++mStageTimingSnapshot.sequence;
    mStageTimingSampleActive = false;
#endif
}

void SimulationWorld::update2(bool forView, double speed) {
    update2(forView, speed, nullptr);
}

void SimulationWorld::update2(
        bool /*forView*/,
        double speed,
        cellularity::execution::SimulationExecutionContext* executionContext) {
    if (updateStage != 2) {
        throw std::runtime_error("Tried to run update2 too early");
    }

    update2Partial(speed, executionContext);
#ifdef CELLULARITY_PERFORMANCE_HUD
    finishStageTimingSample();
#endif
    updateStage = UPDATE_STAGE_CAN_TOUCH;
}

void SimulationWorld::beginSplitEventRecording(SplitEvent* events, int capacity) {
    mSplitEventBuffer = events;
    mSplitEventCapacity = capacity;
    mSplitEventCount = 0;
}

void SimulationWorld::endSplitEventRecording() {
    mSplitEventBuffer = nullptr;
    mSplitEventCapacity = 0;
    mSplitEventCount = 0;
}

void SimulationWorld::recordSplitEvent(int parentCellIndex,
        int child1CellIndex,
        int child2CellIndex,
        int parentGeneIndex,
        int child1GeneIndex,
        int child2GeneIndex) {
    if (mSplitEventBuffer == nullptr || mSplitEventCount >= mSplitEventCapacity) {
        return;
    }
    mSplitEventBuffer[mSplitEventCount++] = {
            parentCellIndex,
            child1CellIndex,
            child2CellIndex,
            parentGeneIndex,
            child1GeneIndex,
            child2GeneIndex,
            mTime
    };
}

int SimulationWorld::splitEventCount() const {
    return mSplitEventCount;
}

void SimulationWorld::invalidateCellContactCache() noexcept {
    mCellContactCacheFresh = false;
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::ContactCacheFreshness,
            cellularity::verification::traceBool(false),
            cellularity::verification::traceI32(0));
}

bool SimulationWorld::cellContactCacheIsFresh() const noexcept {
    return mCellContactCacheFresh;
}

int SimulationWorld::getUpdateStage() const {
    return updateStage;
}
