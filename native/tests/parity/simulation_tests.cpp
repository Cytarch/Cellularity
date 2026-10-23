#include "core/BesselLookup.h"
#include "core/CanonicalGene.h"
#include "core/CellData.h"
#include "core/CellSweepBroadphase.h"
#include "core/CellTopology.h"
#include "core/SimulationWorld.h"
#include "core/CollisionGrid.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "core/RenderContactChord.h"
#include "core/SimulationRandom.h"
#include "render/frame/RenderContactChordBuffer.h"
#include "render/frame/RenderContactCollector.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cellularity::tests;

namespace {

using cellularity::GeneBuilder;
using cellularity::GeneIndex;
using cellularity::GeneView;

template <typename Editor>
bool editGenomeGene(
        CellData& cellData,
        int cellIndex,
        int geneIndex,
        Editor&& editor) {
    return cellData.editGene(
            cellIndex,
            GeneIndex(static_cast<GeneIndex::Value>(geneIndex)),
            std::forward<Editor>(editor));
}

GeneView viewGenomeGene(const CellData& cellData, int cellIndex, int geneIndex) {
    return cellData.getGeneView(
            cellIndex,
            GeneIndex(static_cast<GeneIndex::Value>(geneIndex)));
}

void configureFoodSenseocyte(SimulationWorld& world,
        int cellIndex,
        int smellType = SimulationWorld::SMELL_TYPE_FOOD) {
    world.mCellData.editActiveGene(
            cellIndex,
            [=](GeneBuilder& builder) {
                builder.setFloatValue(
                        Gene::FloatType::SMELL_AMPLITUDE, 1.0f);
                builder.setIntegerValue(Gene::SMELL_OUT_INTEGER_INDEX, 0);
                builder.setIntegerValue(
                        Gene::SMELL_TYPE_INTEGER_INDEX, smellType);
            });
}

int foodSmellEntryCountAt(const SimulationWorld& world,
        double x,
        double y,
        int smellType) {
    const int gridIndex = world.mSmellCollisionGrid->indexAtWorldPosition(x, y);
    if (!world.mSmellCollisionGrid->containsIndex(gridIndex)) {
        return 0;
    }
    const int begin = world.mSmellCollisionGrid->entryOffsetAt(gridIndex);
    const int count = world.mSmellCollisionGrid->countAt(gridIndex);
    int result = 0;
    for (int offset = 0; offset < count; offset++) {
        const int entry = begin + offset;
        if (world.mSmellCollisionGrid->indexAtOffset(entry) >= 0
                && world.mSmellCollisionGrid->typeAtOffset(entry) == smellType) {
            result++;
        }
    }
    return result;
}

int totalFoodSmellEntryCount(const SimulationWorld& world) {
    int result = 0;
    const int slotCount = world.mSmellCollisionGrid->stride()
            * world.mSmellCollisionGrid->stride();
    for (int gridIndex = 0; gridIndex < slotCount; gridIndex++) {
        const int begin = world.mSmellCollisionGrid->entryOffsetAt(gridIndex);
        const int count = world.mSmellCollisionGrid->countAt(gridIndex);
        for (int offset = 0; offset < count; offset++) {
            const int entry = begin + offset;
            if (world.mSmellCollisionGrid->indexAtOffset(entry) >= 0
                    && (world.mSmellCollisionGrid->typeAtOffset(entry)
                                    == SimulationWorld::SMELL_TYPE_FOOD
                            || world.mSmellCollisionGrid->typeAtOffset(entry)
                                    == SimulationWorld::SMELL_TYPE_FAT)) {
                result++;
            }
        }
    }
    return result;
}

void testSimulationWorldGridInitializationMatchesJavaSizing() {
    Environment environment = Environment::constructDefaultEnvironment(4, 5, 1.0);
    SimulationWorld world(true, environment);

    assertTrue(world.mEnvironment.mIsPreviewMode, "SimulationWorld constructor sets preview mode");
    assertTrue(world.mEnvironment.maxDeadSplashCount == 1, "SimulationWorld constructor sets maxDeadSplashCount");
    assertTrue(world.mCellData.MAX_CELL_COUNT == 4, "SimulationWorld cell data capacity");
    assertTrue(world.mFoodData.MAX_FOOD_COUNT == 5, "SimulationWorld food data capacity");
    assertTrue(world.mCellCollisionGrid->stride() == 34, "SimulationWorld physical grid stride");
    assertTrue(world.mSmellCollisionGrid->stride() == 7, "SimulationWorld smell grid stride");
}

void testSimulationWorldUpdatePhysicalGridSuppressesDuplicateBuckets() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(true, environment);
    int cell = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[cell] = 0.02;
    world.mCellData.mPositionY[cell] = 0.02;
    world.mCellData.mRadius[cell] = 0.001;

    world.updatePhysicalGrid(cell);

    int expectedGridIndex = world.mCellCollisionGrid->indexAtWorldPosition(0.02, 0.02);
    int entryOffset = world.mCellCollisionGrid->entryOffsetAt(expectedGridIndex);
    assertTrue(world.mCellData.mGridIndex1[cell] == expectedGridIndex, "SimulationWorld physical grid center index");
    assertTrue(world.mCellCollisionGrid->countAt(expectedGridIndex) == 1, "SimulationWorld physical grid center count");
    assertTrue(world.mCellCollisionGrid->indexAtOffset(entryOffset) == cell, "SimulationWorld physical grid center stored cell");
}

void testSimulationWorldUpdatePhysicalGridStoresCenterBucketForLargeCell() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(true, environment);
    int cell = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[cell] = 0.02;
    world.mCellData.mPositionY[cell] = 0.02;
    world.mCellData.mRadius[cell] = 0.04;

    world.updatePhysicalGrid(cell);

    int grid1 = world.mCellCollisionGrid->indexAtWorldPosition(0.02, 0.02);
    assertTrue(world.mCellData.mGridIndex1[cell] == grid1, "SimulationWorld physical grid distinct first index");
    assertTrue(world.mCellCollisionGrid->countAt(grid1) == 1, "SimulationWorld physical grid distinct center count");
}

void testSimulationWorldUpdateGridsAddsSmellEntriesAndSkipsDeadCells() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(true, environment);
    int secrocyte = addAliveCell(world.mCellData, CellType::SECROCYTE);
    int dead = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[secrocyte] = 0.02;
    world.mCellData.mPositionY[secrocyte] = 0.02;
    world.mCellData.mRadius[secrocyte] = 0.001;
    world.mCellData.mNitrates[secrocyte] = CellData::MAX_NITRATES;
    world.mCellData.editActiveGene(secrocyte, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_WALL_SMELL);
    });
    world.mCellData.mPositionX[dead] = 0.5;
    world.mCellData.mPositionY[dead] = 0.5;
    world.mCellData.mRadius[dead] = 0.001;
    world.mCellData.markCellAsDead(dead);
    world.mCellData.mPositionX[senseocyte] = -0.5;
    world.mCellData.mPositionY[senseocyte] = -0.5;
    world.mCellData.mRadius[senseocyte] = 0.001;
    world.mCellData.editActiveGene(senseocyte, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SMELL_TYPE_INTEGER_INDEX,
                SimulationWorld::SMELL_TYPE_WALL);
    });

    world.updateGrids();

    assertTrue(world.mHasAnySmellConsumers, "SimulationWorld updateGrids detects any smell consumer");
    assertTrue(world.mHasSmellGridConsumers, "SimulationWorld updateGrids detects smell grid consumer");
    int smellGridIndex = world.mSmellCollisionGrid->indexAtWorldPosition(0.02, 0.02);
    int smellOffset = world.mSmellCollisionGrid->entryOffsetAt(smellGridIndex);
    assertTrue(world.mSmellCollisionGrid->countAt(smellGridIndex) == 2, "SimulationWorld updateGrids secrocyte smell count");
    assertTrue(world.mSmellCollisionGrid->indexAtOffset(smellOffset) == secrocyte, "SimulationWorld updateGrids secrocyte cell smell index");
    assertTrue(world.mSmellCollisionGrid->typeAtOffset(smellOffset) == SimulationWorld::SMELL_TYPE_CELL, "SimulationWorld updateGrids secrocyte cell smell type");
    assertTrue(world.mSmellCollisionGrid->indexAtOffset(smellOffset + 1) == -1 - secrocyte, "SimulationWorld updateGrids secrocyte virtual smell index");
    assertTrue(world.mSmellCollisionGrid->typeAtOffset(smellOffset + 1) == SimulationWorld::SMELL_TYPE_WALL, "SimulationWorld updateGrids secrocyte virtual smell type");

    int deadPhysicalGridIndex = world.mCellCollisionGrid->indexAtWorldPosition(0.499, 0.499);
    assertTrue(world.mCellCollisionGrid->countAt(deadPhysicalGridIndex) == 0, "SimulationWorld updateGrids skips dead physical grid");
}

#ifdef CELLULARITY_PROFILE
void assertCollisionGridStateEqual(const CollisionGrid& fused,
        const CollisionGrid& profiled,
        bool hasTypes) {
    assertTrue(fused.stride() == profiled.stride(),
            "Profiled spatial split preserves CollisionGrid stride");
    assertTrue(fused.touchedCount() == profiled.touchedCount(),
            "Profiled spatial split preserves touched bucket count");
    for (int touchedOffset = 0; touchedOffset < fused.touchedCount(); touchedOffset++) {
        assertTrue(fused.touchedIndexAt(touchedOffset)
                        == profiled.touchedIndexAt(touchedOffset),
                "Profiled spatial split preserves touched bucket order");
    }
    const int slotCount = fused.stride() * fused.stride();
    for (int gridIndex = 0; gridIndex < slotCount; gridIndex++) {
        const int fusedCount = fused.countAt(gridIndex);
        assertTrue(fusedCount == profiled.countAt(gridIndex),
                "Profiled spatial split preserves bucket counts");
        const int fusedOffset = fused.entryOffsetAt(gridIndex);
        const int profiledOffset = profiled.entryOffsetAt(gridIndex);
        for (int entryIndex = 0; entryIndex < fusedCount; entryIndex++) {
            assertTrue(fused.indexAtOffset(fusedOffset + entryIndex)
                            == profiled.indexAtOffset(profiledOffset + entryIndex),
                    "Profiled spatial split preserves bucket entry order");
            if (hasTypes) {
                assertTrue(fused.typeAtOffset(fusedOffset + entryIndex)
                                == profiled.typeAtOffset(profiledOffset + entryIndex),
                        "Profiled spatial split preserves typed bucket entries");
            }
        }
    }
}

void assertSpatialIndexStateEqual(const SimulationWorld& fused,
        const SimulationWorld& profiled) {
    assertCollisionGridStateEqual(*fused.mCellCollisionGrid,
            *profiled.mCellCollisionGrid,
            false);
    assertCollisionGridStateEqual(*fused.mSmellCollisionGrid,
            *profiled.mSmellCollisionGrid,
            true);
    assertTrue(fused.mCellData.mGridIndex1 == profiled.mCellData.mGridIndex1,
            "Profiled spatial split preserves Cell collision indexes");
    assertTrue(fused.mCellRenderOverflowGridHead
                    == profiled.mCellRenderOverflowGridHead,
            "Profiled spatial split preserves render overflow heads");
    assertTrue(fused.mCellRenderOverflowGridNext
                    == profiled.mCellRenderOverflowGridNext,
            "Profiled spatial split preserves render overflow links");
    assertTrue(fused.mCellRenderOverflowGridTouched
                    == profiled.mCellRenderOverflowGridTouched,
            "Profiled spatial split preserves render overflow touched order");
    assertTrue(fused.mCellRenderGridOccupied
                    == profiled.mCellRenderGridOccupied,
            "Profiled spatial split preserves render occupancy bits");
    assertTrue(fused.mFoodSpatialGridHead == profiled.mFoodSpatialGridHead,
            "Profiled spatial split preserves Food spatial heads");
    assertTrue(fused.mFoodSpatialGridNext == profiled.mFoodSpatialGridNext,
            "Profiled spatial split preserves Food spatial links");
    assertTrue(fused.mFoodSpatialGridTouched
                    == profiled.mFoodSpatialGridTouched,
            "Profiled spatial split preserves Food touched-head order");
    assertTrue(fused.mFoodData.mCellCollisionGridIndex
                    == profiled.mFoodData.mCellCollisionGridIndex,
            "Profiled spatial split preserves Food collision indexes");
    assertTrue(fused.mFoodData.mCellCollisionGridIndexValid
                    == profiled.mFoodData.mCellCollisionGridIndexValid,
            "Profiled spatial split preserves Food index validity");
    assertTrue(fused.mFoodData.mCellCollisionGridMappingGeneration
                    == profiled.mFoodData.mCellCollisionGridMappingGeneration,
            "Profiled spatial split preserves Food mapping generations");
    assertTrue(fused.foodSpatialIndexIsFresh()
                    == profiled.foodSpatialIndexIsFresh(),
            "Profiled spatial split preserves Food index freshness");
    assertTrue(fused.mFoodSmellGridConsumerMask
                    == profiled.mFoodSmellGridConsumerMask,
            "Profiled spatial split preserves Food smell consumer mask");
    assertTrue(fused.mHasAnySmellConsumers == profiled.mHasAnySmellConsumers
                    && fused.mHasSmellGridConsumers
                            == profiled.mHasSmellGridConsumers
                    && fused.mHasFoodSmellConsumers
                            == profiled.mHasFoodSmellConsumers
                    && fused.mFoodSmellGridConsumerMaskActive
                            == profiled.mFoodSmellGridConsumerMaskActive,
            "Profiled spatial split preserves smell consumer flags");
}

void populateSpatialAttributionParityWorld(SimulationWorld& world) {
    for (int cellIndex = 0; cellIndex < 220; cellIndex++) {
        CellType type = CellType::PHAGOCYTE;
        if (cellIndex == 0) {
            type = CellType::SECROCYTE;
        } else if (cellIndex == 1) {
            type = CellType::SENSEOCYTE;
        }
        const int cell = addAliveCell(world.mCellData, type);
        world.mCellData.mPositionX[cell] = 0.02;
        world.mCellData.mPositionY[cell] = 0.02;
        world.mCellData.mRadius[cell] = 0.001;
    }

    world.mCellData.editActiveGene(0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_WALL_SMELL);
    });
    world.mCellData.editActiveGene(1, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SMELL_TYPE_INTEGER_INDEX,
                SimulationWorld::SMELL_TYPE_WALL);
    });
    world.mCellData.markCellAsDead(2);
    world.mCellData.mPositionX[3] = 1.5;

    world.mFoodData.addFood(-0.4, 0.1, 0.5, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.1, 0.1, 0.5, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.2, -0.2, 0.5, 0.0f, world.mEnvironment);
    const int outsideFood = world.mFoodData.addFood(
            0.3, -0.3, 0.5, 0.0f, world.mEnvironment);
    world.mFoodData.mPositionX[outsideFood] = 1.5f;
    world.mFoodData.markFoodPositionChanged(outsideFood);
}

void testProfiledSpatialAttributionPreservesFusedGridState() {
    Environment environment = Environment::constructDefaultEnvironment(
            220, 4, 1.0);
    SimulationWorld fused(true, environment);
    SimulationWorld profiled(true, environment);
    populateSpatialAttributionParityWorld(fused);
    populateSpatialAttributionParityWorld(profiled);

    fused.updateGrids(true);
    profiled.updateGrids(true);
    fused.mCellData.mPositionY[4] = 0.3;
    profiled.mCellData.mPositionY[4] = 0.3;
    fused.mFoodData.mPositionY[1] = -0.25f;
    profiled.mFoodData.mPositionY[1] = -0.25f;
    fused.mFoodData.markFoodPositionChanged(1);
    profiled.mFoodData.markFoodPositionChanged(1);

    SimulationWorldProfile profile;
    profile.clear();
    profiled.setProfile(&profile);
    fused.updateGrids(true);
    profiled.updateGrids(true);
    profiled.setProfile(nullptr);

    assertSpatialIndexStateEqual(fused, profiled);
    assertTrue(profile.gridCellPositionConversions == 220,
            "Profiled spatial split counts every Cell position conversion");
    assertTrue(profile.gridCellIndexAttempts == 219,
            "Profiled spatial split counts every live physical admission attempt");
    assertTrue(profile.gridCellRenderOccupancyMarks
                    == profile.gridCellIndexAdmissions
                            + profile.gridCellRenderOverflowAdmissions,
            "Profiled spatial split accounts for every render occupancy mark");
    assertTrue(profile.foodRenderIndexBuildCalls == 1
                    && profile.foodRenderMappingVisits == 4
                    && profile.foodRenderInsertions == 3
                    && profile.foodRenderMappingCacheHits == 3
                    && profile.foodRenderMappingCacheMisses == 1
                    && profile.foodRenderOutOfBoundsSkips == 1,
            "Profiled spatial split accounts for Food spatial indexing");
    assertTrue(profile.foodSpatialGenerationMisses == 1,
            "Profiled spatial split records a stale Food generation");
    assertTrue(profile.gridCellTouchedBucketsCleared > 0
                    && profile.gridSmellTouchedBucketsCleared > 0
                    && profile.gridRenderOverflowHeadsCleared > 0
                    && profile.gridRenderOccupancyWordsCleared > 0,
            "Profiled spatial split accounts for every grid clear strategy");

    profile.clear();
    profiled.setProfile(&profile);
    fused.updateGrids(false);
    profiled.updateGrids(false);
    profiled.setProfile(nullptr);

    assertSpatialIndexStateEqual(fused, profiled);
    assertTrue(profile.gridFoodCollisionRefreshCalls == 0
                    && profile.gridFoodCollisionRefreshVisits == 0
                    && profile.foodRenderIndexBuildCalls == 0
                    && profile.foodSpatialGenerationHits == 1,
            "Profiled spatial split reuses a fresh Food generation");
}
#endif

void testSimulationWorldFoodSpatialIndexTracksStorageAndGridGenerations() {
    Environment environment = Environment::constructDefaultEnvironment(
            4, 6, 1.0);
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    world.mFoodData.addFood(0.010, 0.010, 0.5, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.011, 0.010, 0.5, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.500, 0.000, 0.5, 0.0f, world.mEnvironment);

    world.updateGrids(false);
    assertTrue(!world.foodSpatialIndexIsFresh(),
            "Food index remains lazy without simulation consumers");

    world.ensureFoodSpatialIndex();
    assertTrue(world.foodSpatialIndexIsFresh(),
            "Food index becomes fresh on explicit demand");
    const int sharedGridIndex = world.mCellCollisionGrid->indexAtWorldPosition(
            0.010, 0.010);
    assertTrue(world.mFoodSpatialGridHead[sharedGridIndex] == 1
                    && world.mFoodSpatialGridNext[1] == 0,
            "Food bucket preserves descending active-slot order");
    const std::vector<int> firstHeads = world.mFoodSpatialGridHead;
    const std::vector<int> firstTouched = world.mFoodSpatialGridTouched;
    world.ensureFoodSpatialIndex();
    assertTrue(world.mFoodSpatialGridHead == firstHeads
                    && world.mFoodSpatialGridTouched == firstTouched,
            "Fresh Food index ensure performs no rebuild");

    const int addedFood = world.mFoodData.addFood(
            0.012, 0.010, 0.5, 0.0f, world.mEnvironment);
    assertTrue(addedFood == 3 && !world.foodSpatialIndexIsFresh(),
            "Adding Food invalidates index freshness");
    world.ensureFoodSpatialIndex();
    assertTrue(world.mFoodSpatialGridHead[sharedGridIndex] == 3
                    && world.mFoodSpatialGridNext[3] == 1
                    && world.mFoodSpatialGridNext[1] == 0,
            "Rebuild retains exact insertion order after add");

    world.removeFoodImmediately(1);
    assertTrue(!world.foodSpatialIndexIsFresh(),
            "Swap-removing Food invalidates index freshness");
    world.ensureFoodSpatialIndex();
    assertTrue(world.mFoodSpatialGridHead[sharedGridIndex] == 1
                    && world.mFoodSpatialGridNext[1] == 0,
            "Swap-remove moves cached mapping with the surviving Food");

    const std::uint64_t oldMappingGeneration =
            world.mFoodData.mCellCollisionGridMappingGeneration[0];
    world.initializeCellCollisionGrid();
    assertTrue(!world.foodSpatialIndexIsFresh(),
            "Collision-grid recreation invalidates Food index freshness");
    world.ensureFoodSpatialIndex();
    assertTrue(world.foodSpatialIndexIsFresh()
                    && world.mFoodData.mCellCollisionGridMappingGeneration[0]
                            != oldMappingGeneration,
            "Food mappings are recomputed for the recreated grid generation");

    const std::vector<int> occupiedHeads = world.mFoodSpatialGridTouched;
    world.mFoodData.resizeActiveFoodCount(0);
    world.ensureFoodSpatialIndex();
    for (int gridIndex : occupiedHeads) {
        assertTrue(world.mFoodSpatialGridHead[gridIndex] == -1,
                "Touched-head clearing removes retired Food buckets");
    }
    assertTrue(world.mFoodSpatialGridTouched.empty()
                    && world.foodSpatialIndexIsFresh(),
            "Empty reset leaves a fresh empty Food index");
}

void testSimulationWorldAddFoodSmellToGridStoresFoodAndFatTypes() {
    Environment environment = Environment::constructDefaultEnvironment(2, 3, 1.0);
    SimulationWorld world(true, environment);
    world.mFoodData.addFood(0.02, 0.02, 0.5, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.03, 0.02, 0.5, 0.25f, world.mEnvironment);

    world.addFoodSmellToGrid(0);
    world.addFoodSmellToGrid(1);

    int smellGridIndex = world.mSmellCollisionGrid->indexAtWorldPosition(0.02, 0.02);
    int offset = world.mSmellCollisionGrid->entryOffsetAt(smellGridIndex);
    assertTrue(world.mSmellCollisionGrid->countAt(smellGridIndex) == 2, "SimulationWorld food smell count");
    assertTrue(world.mSmellCollisionGrid->indexAtOffset(offset) == 0, "SimulationWorld food smell food index");
    assertTrue(world.mSmellCollisionGrid->typeAtOffset(offset) == SimulationWorld::SMELL_TYPE_FOOD, "SimulationWorld food smell food type");
    assertTrue(world.mSmellCollisionGrid->indexAtOffset(offset + 1) == 1, "SimulationWorld fat smell food index");
    assertTrue(world.mSmellCollisionGrid->typeAtOffset(offset + 1) == SimulationWorld::SMELL_TYPE_FAT, "SimulationWorld fat smell type");
}

void testSimulationWorldEatFoodsSkipsFoodSmellUntilFoodSmellConsumerAppears() {
    Environment environment = Environment::constructDefaultEnvironment(4, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    int phagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[phagocyte] = 0.2;
    world.mCellData.mPositionY[phagocyte] = 0.2;
    world.mFoodData.addFood(0.0, 0.0, 0.5, 0.0f, world.mEnvironment);

    world.updateGrids();
    assertTrue(!world.mHasFoodSmellConsumers, "SimulationWorld no food smell consumers initially");
    world.eatFoods(SimulationWorld::DELTA_TIME);

    int smellGridIndex = world.mSmellCollisionGrid->indexAtWorldPosition(0.0, 0.0);
    assertTrue(world.mSmellCollisionGrid->countAt(smellGridIndex) == 0, "SimulationWorld eatFoods skips food smell without consumers");

    int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[senseocyte] = 0.2;
    world.mCellData.mPositionY[senseocyte] = -0.2;
    world.mCellData.editActiveGene(senseocyte, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SMELL_TYPE_INTEGER_INDEX,
                SimulationWorld::SMELL_TYPE_FOOD);
    });

    world.updateGrids();
    assertTrue(world.mHasFoodSmellConsumers, "SimulationWorld detects added food smell consumer");
    world.eatFoods(SimulationWorld::DELTA_TIME);

    smellGridIndex = world.mSmellCollisionGrid->indexAtWorldPosition(0.0, 0.0);
    int offset = world.mSmellCollisionGrid->entryOffsetAt(smellGridIndex);
    int count = world.mSmellCollisionGrid->countAt(smellGridIndex);
    bool foundFoodSmell = false;
    for (int i = 0; i < count; i++) {
        if (world.mSmellCollisionGrid->indexAtOffset(offset + i) == 0
                && world.mSmellCollisionGrid->typeAtOffset(offset + i) == SimulationWorld::SMELL_TYPE_FOOD) {
            foundFoodSmell = true;
        }
    }
    assertTrue(foundFoodSmell, "SimulationWorld eatFoods adds food smell after consumer appears");
}

void testSimulationWorldProcessCellOverlapsCollectsCollisionsOnceAcrossBuckets() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(true, environment);
    int target = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int neighbor = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[target] = 0.02;
    world.mCellData.mPositionY[target] = 0.02;
    world.mCellData.mRadius[target] = CellData::MAX_CELL_RADIUS;
    world.mCellData.mPositionX[neighbor] = 0.04;
    world.mCellData.mPositionY[neighbor] = 0.02;
    world.mCellData.mRadius[neighbor] = CellData::MAX_CELL_RADIUS;

    world.updateGrids();
    world.processCellOverlaps();

    assertTrue(world.mCellData.cellCollisionContactCount(target) == 1, "SimulationWorld process overlaps target collision deduplicated");
    assertTrue(collisionContactTargetAt(world.mCellData, target, 0) == neighbor, "SimulationWorld process overlaps target collision neighbor");
    assertTrue(world.mCellData.cellCollisionContactCount(neighbor) == 1, "SimulationWorld process overlaps neighbor collision deduplicated");
    assertTrue(collisionContactTargetAt(world.mCellData, neighbor, 0) == target, "SimulationWorld process overlaps neighbor collision target");
}

void testSimulationWorldProcessCellOverlapsCollectsNeighborBucketCollisions() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(true, environment);
    int target = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int neighbor = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[target] = 0.03;
    world.mCellData.mPositionY[target] = 0.0;
    world.mCellData.mRadius[target] = 0.03;
    world.mCellData.mPositionX[neighbor] = 0.085;
    world.mCellData.mPositionY[neighbor] = 0.0;
    world.mCellData.mRadius[neighbor] = 0.03;

    world.updateGrids();
    assertTrue(world.mCellData.mGridIndex1[target] != world.mCellData.mGridIndex1[neighbor],
            "SimulationWorld center grid places nearby overlap in adjacent buckets");
    world.processCellOverlaps();

    assertTrue(world.mCellData.cellCollisionContactCount(target) == 1, "SimulationWorld process overlaps scans neighbor buckets for target");
    assertTrue(collisionContactTargetAt(world.mCellData, target, 0) == neighbor, "SimulationWorld process overlaps neighbor bucket target");
    assertTrue(world.mCellData.cellCollisionContactCount(neighbor) == 1, "SimulationWorld process overlaps scans neighbor buckets for neighbor");
    assertTrue(collisionContactTargetAt(world.mCellData, neighbor, 0) == target, "SimulationWorld process overlaps neighbor bucket reverse");
}

void testSimulationWorldProcessCellOverlapsAppliesDeathIntentsAndCleansReferences() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(true, environment);
    int small = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int large = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int other = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[small] = 0.01;
    world.mCellData.mPositionY[small] = 0.0;
    world.mCellData.mRadius[small] = 0.01;
    world.mCellData.mPositionX[large] = 0.0;
    world.mCellData.mPositionY[large] = 0.0;
    world.mCellData.mRadius[large] = 0.03;
    world.mCellData.mPositionX[other] = 0.035;
    world.mCellData.mPositionY[other] = 0.0;
    world.mCellData.mRadius[other] = 0.01;
    world.mCellData.mFertilizationTarget[other] = small;

    world.updateGrids();
    world.processCellOverlaps();

    assertTrue(world.mCellData.mIsDead[small], "SimulationWorld process overlaps marks contained smaller cell dead");
    assertTrue(world.mCellData.cellCollisionContactCount(small) == 0, "SimulationWorld process overlaps clears dead cell collisions");
    for (int i = 0; i < world.mCellData.cellCollisionContactCount(large); i++) {
        assertTrue(collisionContactTargetAt(world.mCellData, large, i) != small, "SimulationWorld process overlaps removes dead collision from large");
    }
    for (int i = 0; i < world.mCellData.cellCollisionContactCount(other); i++) {
        assertTrue(collisionContactTargetAt(world.mCellData, other, i) != small, "SimulationWorld process overlaps removes dead collision from other");
    }
    assertTrue(world.mCellData.mFertilizationTarget[other] == -1, "SimulationWorld process overlaps clears fertilization target pointing to dead cell");
}

void testSimulationWorldProcessCellOverlapsSetsFertilizationTargetForReadyGametes() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    SimulationWorld world(true, environment);
    int first = addAliveCell(world.mCellData, CellType::GAMETE);
    int second = addAliveCell(world.mCellData, CellType::GAMETE);
    int young = addAliveCell(world.mCellData, CellType::GAMETE);
    world.mCellData.mPositionX[first] = 0.0;
    world.mCellData.mPositionY[first] = 0.0;
    world.mCellData.mRadius[first] = 0.02;
    world.mCellData.mAge[first] = CellData::AGE_OF_CONSENT + 0.1;
    world.mCellData.mPositionX[second] = 0.000001;
    world.mCellData.mPositionY[second] = 0.0;
    world.mCellData.mRadius[second] = 0.02;
    world.mCellData.mAge[second] = CellData::AGE_OF_CONSENT + 0.1;
    world.mCellData.mPositionX[young] = 0.0;
    world.mCellData.mPositionY[young] = 0.025;
    world.mCellData.mRadius[young] = 0.02;
    world.mCellData.mAge[young] = 0.0;

    world.updateGrids();
    world.processCellOverlaps();

    assertTrue(world.mCellData.mFertilizationTarget[first] == second, "SimulationWorld process overlaps sets fertilization target for first ready gamete");
    assertTrue(world.mCellData.mFertilizationTarget[second] == first, "SimulationWorld process overlaps sets fertilization target for second ready gamete");
    assertTrue(world.mCellData.mFertilizationTarget[young] == -1, "SimulationWorld process overlaps skips young gamete fertilization target");
}

void testSimulationWorldRemoveCellImmediatelyCompactsAndRetargetsLinks() {
    Environment environment = Environment::constructDefaultEnvironment(5, 0, 1.0);
    SimulationWorld world(true, environment);
    int linkedToRemoved = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int removed = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int linkedToLast = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int last = addAliveCell(world.mCellData, CellType::GLUEOCYTE);
    assertTrue(world.mCellData.editActiveGene(
                       removed,
                       [](GeneBuilder& gene) {
                           gene.setColor(0, 0.3125f);
                       }),
            "SimulationWorld compaction fixture gives the removed Cell a unique Genome");
    const CellRuntimeId removedRuntimeId = world.mCellData.runtimeId(removed);
    const CellRuntimeId lastRuntimeId = world.mCellData.runtimeId(last);
    const cellularity::GenomeId removedGenomeId =
            world.mCellData.genomeId(removed);
    const cellularity::GenomeId lastGenomeId = world.mCellData.genomeId(last);
    const std::size_t liveGenomesBefore =
            world.mCellData.genomePool().diagnostics().liveSlots;
    world.mCellData.mPositionX[removed] = 0.25;
    world.mCellData.mPositionY[removed] = -0.125;
    world.mCellData.mRadius[removed] = 0.02;
    world.mCellData.mColor[removed] = {0.1f, 0.2f, 0.3f, 0.4f};
    world.mCellData.mOriginTag[last] = 77;
    addPairLink(world.mCellData, removed, linkedToRemoved, 0.0, 0.0, 0.5f, 0.0f);
    addPairLink(world.mCellData, last, linkedToLast, 0.0, 0.0, 0.5f, 0.0f);

    world.removeCellImmediately(removed);

    assertTrue(world.mCellData.mCellCount == 3, "SimulationWorld removeCellImmediately decrements cell count");
    assertTrue(world.mCellData.getActiveGeneView(removed).cellType() == CellType::GLUEOCYTE, "SimulationWorld removeCellImmediately copies last into target");
    assertTrue(world.mCellData.mOriginTag[removed] == 77, "SimulationWorld removeCellImmediately copies last origin tag");
    assertTrue(world.mCellData.runtimeId(removed) == lastRuntimeId
                    && world.mCellData.runtimeId(removed) != removedRuntimeId,
            "SimulationWorld compaction moves the surviving runtime identity with the Cell");
    assertTrue(world.mCellData.genomeId(removed) == lastGenomeId,
            "SimulationWorld compaction moves the last Cell Genome handle");
    assertTrue(!world.mCellData.genomePool().referenceCount(removedGenomeId).has_value(),
            "SimulationWorld compaction releases the removed Cell Genome record");
    assertTrue(world.mCellData.genomePool().diagnostics().liveSlots + 1
                    == liveGenomesBefore,
            "SimulationWorld compaction removes one Genome record without cloning");
    assertTrue(world.mCellData.genomePool().validateInvariants().isValid()
                    && world.mCellData.genePool().validateInvariants().isValid(),
            "SimulationWorld compaction preserves pooled ownership invariants");
    assertTrue(world.mCellData.mTopology.edgeCount() == 1, "SimulationWorld removeCellImmediately keeps one topology edge");
    assertTrue(world.mCellData.mTopology.isLinked(removed, linkedToLast), "SimulationWorld removeCellImmediately retargets topology edge to moved last cell");
    assertTrue(!world.mCellData.mTopology.isLinked(removed, linkedToRemoved), "SimulationWorld removeCellImmediately removes topology edge to deleted cell");
    assertTrue(world.mDeadSplashData.mDeadSplashCount == 1, "SimulationWorld removeCellImmediately adds dead splash");
    assertNear(0.25f, world.mDeadSplashData.mPositionX[0], 0.0, "SimulationWorld removeCellImmediately dead splash x");
    assertNear(-0.125f, world.mDeadSplashData.mPositionY[0], 0.0, "SimulationWorld removeCellImmediately dead splash y");
}

void testSimulationWorldRemoveDeadCellsAddsCorpseFoodAndSkipsOutOfBoundsCorpse() {
    Environment environment = Environment::constructDefaultEnvironment(8, 4, 1.0);
    SimulationWorld world(false, environment);
    int deadInside = addAliveCell(world.mCellData, CellType::LIPOCYTE);
    int outOfBounds = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int survivor = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[deadInside] = 0.1;
    world.mCellData.mPositionY[deadInside] = 0.2;
    world.mCellData.mMass[deadInside] = 0.5;
    world.mCellData.mRadius[deadInside] = 0.02;
    world.mCellData.mColor[deadInside] = {0.3f, 0.4f, 0.5f, 0.6f};
    world.mCellData.markCellAsDead(deadInside);
    world.mCellData.mPositionX[outOfBounds] = 1.1;
    world.mCellData.mPositionY[outOfBounds] = 0.0;
    world.mCellData.mRadius[outOfBounds] = 0.02;
    world.mCellData.mOriginTag[survivor] = 42;

    world.removeDeadCells();

    assertTrue(world.mCellData.mCellCount == 1, "SimulationWorld removeDeadCells removes dead and out-of-bounds cells");
    assertTrue(world.mCellData.mOriginTag[0] == 42, "SimulationWorld removeDeadCells keeps survivor via compaction");
    assertTrue(world.mFoodData.mFoodCount == 1, "SimulationWorld removeDeadCells adds corpse food only for in-bounds dead cell");
    assertNear(0.1f, world.mFoodData.mPositionX[0], 0.0, "SimulationWorld removeDeadCells corpse food x");
    assertNear(0.2f, world.mFoodData.mPositionY[0], 0.0, "SimulationWorld removeDeadCells corpse food y");
    assertNear(0.5 * CellData::CORPSE_EFFICIENCY, world.mFoodData.mMass[0], 1e-6, "SimulationWorld removeDeadCells corpse food mass");
    assertNear(0.5f, world.mFoodData.mLipidLayer[0], 0.0, "SimulationWorld removeDeadCells lipocyte corpse lipid layer");
    assertTrue(world.mDeadSplashData.mDeadSplashCount == 1, "SimulationWorld removeDeadCells adds splash only for in-bounds dead cell");
    assertNear(0.1f, world.mDeadSplashData.mPositionX[0], 0.0, "SimulationWorld removeDeadCells dead splash x");
}

void testSimulationWorldEatFoodsTransfersMassAndRemovesConsumedFood() {
    Environment environment = Environment::constructDefaultEnvironment(2, 3, 1.0);
    environment.mTestFoodDecay = 0.0;
    SimulationWorld world(false, environment);
    int phagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[phagocyte] = 0.0;
    world.mCellData.mPositionY[phagocyte] = 0.0;
    world.mCellData.mRadius[phagocyte] = 0.02;
    world.mCellData.mMass[phagocyte] = CellData::INITIAL_MASS;
    world.mFoodData.addFood(0.0, 0.0, 0.01, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.5, 0.0, 0.2, 0.0f, world.mEnvironment);

    world.updateGrids();
    world.eatFoods(SimulationWorld::DELTA_TIME);

    assertTrue(world.mFoodData.mFoodCount == 1, "SimulationWorld eatFoods removes fully consumed food");
    assertNear(0.5f, world.mFoodData.mPositionX[0], 0.0, "SimulationWorld eatFoods compacts remaining food");
    assertNear(CellData::INITIAL_MASS + 0.01, world.mCellData.mMass[phagocyte], 1e-8, "SimulationWorld eatFoods adds eaten mass");
    int smellGridIndex = world.mSmellCollisionGrid->indexAtWorldPosition(0.0, 0.0);
    assertTrue(world.mSmellCollisionGrid->countAt(smellGridIndex) == 0, "SimulationWorld eatFoods skips smell grid without consumers");
}

void testSimulationWorldEatFoodsPartiallyConsumesFoodAndConservesVelocityMomentum() {
    Environment environment = Environment::constructDefaultEnvironment(2, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    SimulationWorld world(false, environment);
    int phagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[phagocyte] = 0.0;
    world.mCellData.mPositionY[phagocyte] = 0.0;
    world.mCellData.mRadius[phagocyte] = 0.02;
    world.mCellData.mMass[phagocyte] = CellData::INITIAL_MASS;
    world.mCellData.mVelocityX[phagocyte] = 0.3;
    world.mCellData.mVelocityY[phagocyte] = -0.1;
    world.mFoodData.addFood(0.0, 0.0, 0.2, 0.0f, world.mEnvironment);

    world.updateGrids();
    world.eatFoods(SimulationWorld::DELTA_TIME);

    double expectedMass = CellData::INITIAL_MASS + SimulationWorld::DELTA_TIME * CellData::MAX_EAT_SPEED;
    assertTrue(world.mFoodData.mFoodCount == 1, "SimulationWorld eatFoods keeps partially consumed food");
    assertNear(0.2f - SimulationWorld::DELTA_TIME * CellData::MAX_EAT_SPEED, world.mFoodData.mMass[0], 1e-6, "SimulationWorld eatFoods subtracts partial mass");
    assertNear(expectedMass, world.mCellData.mMass[phagocyte], 1e-12, "SimulationWorld eatFoods partial cell mass");
    assertNear(0.3 * CellData::INITIAL_MASS / expectedMass, world.mCellData.mVelocityX[phagocyte], 1e-12, "SimulationWorld eatFoods velocityX mass scaling");
    assertNear(-0.1 * CellData::INITIAL_MASS / expectedMass, world.mCellData.mVelocityY[phagocyte], 1e-12, "SimulationWorld eatFoods velocityY mass scaling");
}

void testSimulationWorldEatFoodsTwoPhagocytesDoNotOverConsumeSharedFood() {
    Environment environment = Environment::constructDefaultEnvironment(4, 1, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    int leftPhagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int rightPhagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[leftPhagocyte] = -0.001;
    world.mCellData.mPositionY[leftPhagocyte] = 0.0;
    world.mCellData.mRadius[leftPhagocyte] = 0.02;
    world.mCellData.mMass[leftPhagocyte] = CellData::INITIAL_MASS;
    world.mCellData.mPositionX[rightPhagocyte] = 0.001;
    world.mCellData.mPositionY[rightPhagocyte] = 0.0;
    world.mCellData.mRadius[rightPhagocyte] = 0.02;
    world.mCellData.mMass[rightPhagocyte] = CellData::INITIAL_MASS;
    double initialLeftMass = world.mCellData.mMass[leftPhagocyte];
    double initialRightMass = world.mCellData.mMass[rightPhagocyte];
    double initialFoodMass = 0.001;
    world.mFoodData.addFood(0.0, 0.0, initialFoodMass, 0.0f, world.mEnvironment);

    world.updateGrids();
    world.eatFoods(SimulationWorld::DELTA_TIME);

    double gainedMass = (world.mCellData.mMass[leftPhagocyte] - initialLeftMass)
            + (world.mCellData.mMass[rightPhagocyte] - initialRightMass);
    assertTrue(gainedMass <= initialFoodMass + 1e-9, "SimulationWorld eatFoods shared food cannot be over-consumed");
    assertNear(initialFoodMass, gainedMass + world.mFoodData.getFoodMass(), 1e-6, "SimulationWorld eatFoods shared food conserves mass");
}

void testSimulationWorldEatFoodsPreservesDescendingFoodBucketOrder() {
    Environment environment = Environment::constructDefaultEnvironment(
            2, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    const int phagocyte = addAliveCell(
            world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[phagocyte] = 0.0;
    world.mCellData.mPositionY[phagocyte] = 0.0;
    world.mCellData.mRadius[phagocyte] = 0.02;
    world.mCellData.mMass[phagocyte] = CellData::MAX_MASS - 0.005;
    world.mFoodData.addFood(0.0, 0.0, 0.01, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.0, 0.0, 0.01, 0.0f, world.mEnvironment);

    world.updateGrids(false);
    world.eatFoods(SimulationWorld::DELTA_TIME);

    assertNear(0.01, world.mFoodData.mMass[0], 1e-7,
            "Lower Food slot remains untouched after the Cell fills");
    assertNear(0.005, world.mFoodData.mMass[1], 1e-7,
            "Higher Food slot is consumed first within one bucket");
}

void testSimulationWorldMobileFoodDefersPostMotionIndexRebuildUntilDemand() {
    Environment environment = Environment::constructDefaultEnvironment(
            2, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = true;
    environment.mViscosity = 0.0;
    SimulationWorld world(false, environment);
    const int ciliocyte = addAliveCell(
            world.mCellData, CellType::CILIOCYTE);
    world.mCellData.mPositionX[ciliocyte] = 0.5;
    world.mCellData.mPositionY[ciliocyte] = 0.5;
    world.mCellData.mRadius[ciliocyte] = 0.02;
    const int food = world.mFoodData.addFood(
            0.0, 0.0, 0.1, 0.0f, world.mEnvironment);
    world.mFoodData.mVelocityX[food] = 0.1f;

    world.updateGrids(false);
    assertTrue(world.mHasFoodPhysicsQueryConsumers
                    && world.foodSpatialIndexIsFresh(),
            "Mobile-Food Ciliocyte requests a fresh pre-query index");
    world.eatFoods(SimulationWorld::DELTA_TIME);
    assertTrue(!world.foodSpatialIndexIsFresh(),
            "Food motion leaves the index stale without a post-update rebuild");

    world.ensureFoodSpatialIndexForCapture();
    const int movedGridIndex = world.mCellCollisionGrid->indexAtWorldPosition(
            world.mFoodData.mPositionX[food],
            world.mFoodData.mPositionY[food]);
    assertTrue(world.foodSpatialIndexIsFresh()
                    && world.mFoodSpatialGridHead[movedGridIndex] == food,
            "Capture demand rebuilds the index at the moved Food position");
}

void testSimulationWorldEatFoodsSkipsOverfullPhagocyte() {
    Environment environment = Environment::constructDefaultEnvironment(2, 1, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    int phagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[phagocyte] = 0.0;
    world.mCellData.mPositionY[phagocyte] = 0.0;
    world.mCellData.mRadius[phagocyte] = 0.02;
    world.mCellData.mMass[phagocyte] = CellData::MAX_MASS + 0.1;
    world.mCellData.mVelocityX[phagocyte] = 0.25;
    world.mCellData.mVelocityY[phagocyte] = -0.5;
    world.mFoodData.addFood(0.0, 0.0, 0.2, 0.0f, world.mEnvironment);

    world.updateGrids();
    world.eatFoods(SimulationWorld::DELTA_TIME);

    assertTrue(world.mFoodData.mFoodCount == 1, "SimulationWorld eatFoods overfull phagocyte keeps food");
    assertNear(CellData::MAX_MASS + 0.1, world.mCellData.mMass[phagocyte], 1e-12, "SimulationWorld eatFoods overfull phagocyte mass unchanged");
    assertNear(0.2f, world.mFoodData.mMass[0], 1e-6, "SimulationWorld eatFoods overfull phagocyte does not add food mass");
    assertNear(0.25, world.mCellData.mVelocityX[phagocyte], 1e-12, "SimulationWorld eatFoods overfull phagocyte velocityX unchanged");
    assertNear(-0.5, world.mCellData.mVelocityY[phagocyte], 1e-12, "SimulationWorld eatFoods overfull phagocyte velocityY unchanged");
}

void testSimulationWorldUpdate1PartialSkipsEatFoodsInPreview() {
    Environment environment = Environment::constructDefaultEnvironment(2, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mViscosity = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mGravity = 0.0;
    SimulationWorld world(true, environment);
    int phagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[phagocyte] = 0.0;
    world.mCellData.mPositionY[phagocyte] = 0.0;
    world.mCellData.mRadius[phagocyte] = 0.02;
    world.mCellData.mMass[phagocyte] = CellData::INITIAL_MASS;
    world.mFoodData.addFood(0.0, 0.0, 0.01, 0.0f, world.mEnvironment);

    world.update1Partial(false, 1.0);

    assertTrue(world.mFoodData.mFoodCount == 1, "SimulationWorld update1Partial preview keeps food count");
    assertNear(0.01f, world.mFoodData.mMass[0], 0.0, "SimulationWorld update1Partial preview skips food eating");
    assertNear(CellData::INITIAL_MASS, world.mCellData.mMass[phagocyte], 1e-12, "SimulationWorld update1Partial preview skips cell mass gain");
}

void testSimulationWorldEatFoodsLipidLayerBlocksUntilLipaseReducesCoating() {
    Environment environment = Environment::constructDefaultEnvironment(3, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    SimulationWorld world(false, environment);
    int phagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int secrocyte = addAliveCell(world.mCellData, CellType::SECROCYTE);
    world.mCellData.mPositionX[phagocyte] = 0.0;
    world.mCellData.mPositionY[phagocyte] = 0.0;
    world.mCellData.mRadius[phagocyte] = 0.02;
    world.mCellData.mMass[phagocyte] = CellData::INITIAL_MASS;
    world.mCellData.mPositionX[secrocyte] = 0.001;
    world.mCellData.mPositionY[secrocyte] = 0.0;
    world.mCellData.mRadius[secrocyte] = 0.02;
    world.mCellData.editActiveGene(secrocyte, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_LIPASE);
    });
    world.mFoodData.addFood(0.0, 0.0, 0.1, 1.0f, world.mEnvironment);

    world.updateGrids();
    world.eatFoods(0.25);

    assertTrue(world.mFoodData.mFoodCount == 1, "SimulationWorld eatFoods keeps coated food");
    assertNear(0.1f, world.mFoodData.mMass[0], 0.0, "SimulationWorld eatFoods phagocyte cannot eat coated food");
    assertNear(0.75f, world.mFoodData.mLipidLayer[0], 0.0, "SimulationWorld eatFoods lipase reduces lipid layer");
    assertNear(CellData::INITIAL_MASS, world.mCellData.mMass[phagocyte], 1e-12, "SimulationWorld eatFoods coated food no phagocyte mass gain");
}

void testSimulationWorldEatFoodsDecayRemovesAndCompactsFood() {
    Environment environment = Environment::constructDefaultEnvironment(1, 3, 1.0);
    environment.mTestFoodDecay = 1.0;
    SimulationWorld world(false, environment);
    world.mFoodData.addFood(0.1, 0.0, 0.001, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.2, 0.0, 0.2, 0.5f, world.mEnvironment);

    world.updateGrids();
    world.eatFoods(SimulationWorld::DELTA_TIME);

    assertTrue(world.mFoodData.mFoodCount == 1, "SimulationWorld eatFoods decay removes negative mass food");
    assertNear(0.2f, world.mFoodData.mPositionX[0], 0.0, "SimulationWorld eatFoods decay compacts last food");
    assertNear(0.2f - SimulationWorld::DELTA_TIME, world.mFoodData.mMass[0], 1e-6, "SimulationWorld eatFoods decay processes copied last food");
    assertNear(0.5f, world.mFoodData.mLipidLayer[0], 0.0, "SimulationWorld eatFoods decay copied last lipid layer");
}

void testSimulationWorldFoodSmellDropsConsumedNonLastFoodAndTracksSwapIdentity() {
    Environment environment = Environment::constructDefaultEnvironment(4, 3, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    const int phagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    const int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[phagocyte] = -0.6;
    world.mCellData.mPositionY[phagocyte] = 0.0;
    world.mCellData.mRadius[phagocyte] = 0.02;
    world.mCellData.mPositionX[senseocyte] = -0.57;
    world.mCellData.mPositionY[senseocyte] = 0.0;
    configureFoodSenseocyte(world, senseocyte);
    world.mFoodData.addFood(-0.6, 0.0, 0.01, 0.0f, world.mEnvironment);
    const int survivingFood = world.mFoodData.addFood(
            0.6, 0.0, 0.2, 0.0f, world.mEnvironment);
    const FoodRuntimeId survivingIdentity = world.mFoodData.runtimeId(survivingFood);

    world.updateGrids(false);
    world.eatFoods(SimulationWorld::DELTA_TIME);
    world.updateCellSmellInteraction(senseocyte);

    assertTrue(world.mFoodData.mFoodCount == 1,
            "Food smell swap fixture consumes one non-last Food");
    assertTrue(world.mFoodData.runtimeId(0) == survivingIdentity,
            "Food swap-remove preserves the surviving runtime identity");
    assertNear(0.6f, world.mFoodData.mPositionX[0], 0.0,
            "Food swap-remove preserves the surviving position");
    assertTrue(totalFoodSmellEntryCount(world) == 0,
            "Post-mutation smell build does not leave a stale entry in the consumed bucket");
    assertNear(0.0, world.mCellData.mSignalSubstanceOut[senseocyte][0], 0.0,
            "Senseocyte receives no ghost smell from a swapped Food slot");
}

void testSimulationWorldFoodSmellDropsConsumedLastFood() {
    Environment environment = Environment::constructDefaultEnvironment(4, 3, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    const int phagocyte = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    const int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[phagocyte] = -0.6;
    world.mCellData.mPositionY[phagocyte] = 0.0;
    world.mCellData.mRadius[phagocyte] = 0.02;
    world.mCellData.mPositionX[senseocyte] = -0.57;
    world.mCellData.mPositionY[senseocyte] = 0.0;
    configureFoodSenseocyte(world, senseocyte);
    const int survivingFood = world.mFoodData.addFood(
            0.6, 0.0, 0.2, 0.0f, world.mEnvironment);
    const FoodRuntimeId survivingIdentity = world.mFoodData.runtimeId(survivingFood);
    world.mFoodData.addFood(-0.6, 0.0, 0.01, 0.0f, world.mEnvironment);

    world.updateGrids(false);
    world.eatFoods(SimulationWorld::DELTA_TIME);
    world.updateCellSmellInteraction(senseocyte);

    assertTrue(world.mFoodData.mFoodCount == 1
                    && world.mFoodData.runtimeId(0) == survivingIdentity,
            "Removing the last Food leaves the earlier identity active");
    assertTrue(totalFoodSmellEntryCount(world) == 0,
            "Removing the last Food leaves no stale smell entry");
    assertNear(0.0, world.mCellData.mSignalSubstanceOut[senseocyte][0], 0.0,
            "Removed last Food cannot contribute ghost smell");
}

void testSimulationWorldFoodSmellUsesPostMotionBucketAndPosition() {
    Environment environment = Environment::constructDefaultEnvironment(3, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = true;
    environment.mViscosity = 0.0;
    SimulationWorld world(false, environment);
    const int dummy = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.markCellAsDead(dummy);
    const int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[senseocyte] = 0.26;
    world.mCellData.mPositionY[senseocyte] = 0.0;
    configureFoodSenseocyte(world, senseocyte);
    const int food = world.mFoodData.addFood(
            0.19, 0.0, 0.2, 0.0f, world.mEnvironment);
    world.mFoodData.mVelocityX[food] = 2.0f;
    const int oldBucket = world.mSmellCollisionGrid->indexAtWorldPosition(
            world.mFoodData.mPositionX[food], world.mFoodData.mPositionY[food]);

    world.updateGrids(false);
    world.eatFoods(SimulationWorld::DELTA_TIME);
    const int newBucket = world.mSmellCollisionGrid->indexAtWorldPosition(
            world.mFoodData.mPositionX[food], world.mFoodData.mPositionY[food]);
    world.updateCellSmellInteraction(senseocyte);

    assertTrue(oldBucket != newBucket,
            "Mobile Food fixture crosses a smell-grid boundary");
    assertTrue(world.foodSpatialIndexIsFresh(),
            "Food-smell demand rebuilds the spatial index after motion");
    assertTrue(foodSmellEntryCountAt(
                       world,
                       world.mFoodData.mPositionX[food],
                       world.mFoodData.mPositionY[food],
                       SimulationWorld::SMELL_TYPE_FOOD)
                    == 1,
            "Moved Food is inserted in its post-motion smell bucket");
    assertTrue(foodSmellEntryCountAt(world, 0.19, 0.0,
                       SimulationWorld::SMELL_TYPE_FOOD)
                    == 0,
            "Moved Food is absent from its old smell bucket");
    const double distance = world.mCellData.mPositionX[senseocyte]
            - world.mFoodData.mPositionX[food];
    const double expected = world.mFoodData.mMass[food]
            * CellData::SMELL_SENSITIVITY
            * BesselLookup::k0(std::abs(distance) / SimulationWorld::SMELL_RADIUS);
    assertNear(expected, world.mCellData.mSignalSubstanceOut[senseocyte][0],
            1e-6,
            "Senseocyte uses the moved Food's current position");
}

void testSimulationWorldFoodSmellUsesPostLipaseType() {
    Environment environment = Environment::constructDefaultEnvironment(4, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    const int secrocyte = addAliveCell(world.mCellData, CellType::SECROCYTE);
    const int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[secrocyte] = 0.0;
    world.mCellData.mPositionY[secrocyte] = 0.0;
    world.mCellData.mRadius[secrocyte] = 0.02;
    world.mCellData.editActiveGene(secrocyte, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_LIPASE);
    });
    world.mCellData.mPositionX[senseocyte] = 0.03;
    world.mCellData.mPositionY[senseocyte] = 0.0;
    configureFoodSenseocyte(world, senseocyte);
    const int food = world.mFoodData.addFood(
            0.0, 0.0, 0.2, 0.01f, world.mEnvironment);

    world.updateGrids(false);
    world.eatFoods(SimulationWorld::DELTA_TIME);
    world.updateCellSmellInteraction(senseocyte);

    assertTrue(world.mFoodData.mLipidLayer[food] <= 0.0f,
            "Lipase fixture removes the Food coating in this update");
    assertTrue(foodSmellEntryCountAt(world, 0.0, 0.0,
                       SimulationWorld::SMELL_TYPE_FOOD)
                            == 1
                    && foodSmellEntryCountAt(world, 0.0, 0.0,
                               SimulationWorld::SMELL_TYPE_FAT)
                            == 0,
            "Food smell type reflects the post-lipase coating state");
    assertTrue(world.mCellData.mSignalSubstanceOut[senseocyte][0] > 0.0f,
            "Food sensor observes a coating removed in the same update");
}

void testSimulationWorldFoodSmellPreservesPhagocyteOwnerOrderAndPostEatMass() {
    Environment environment = Environment::constructDefaultEnvironment(5, 2, 1.0);
    environment.mTestFoodDecay = 0.0;
    environment.mobileFood = false;
    SimulationWorld world(false, environment);
    const int first = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    const int second = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    const int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[first] = -0.001;
    world.mCellData.mPositionY[first] = 0.0;
    world.mCellData.mRadius[first] = 0.02;
    world.mCellData.mMass[first] = CellData::MAX_MASS - 0.01;
    world.mCellData.mPositionX[second] = 0.001;
    world.mCellData.mPositionY[second] = 0.0;
    world.mCellData.mRadius[second] = 0.02;
    world.mCellData.mMass[second] = CellData::INITIAL_MASS;
    world.mCellData.mPositionX[senseocyte] = 0.03;
    world.mCellData.mPositionY[senseocyte] = 0.0;
    configureFoodSenseocyte(world, senseocyte);
    const double firstMassBefore = world.mCellData.mMass[first];
    const double secondMassBefore = world.mCellData.mMass[second];
    const int food = world.mFoodData.addFood(
            0.0, 0.0, 0.05, 0.0f, world.mEnvironment);
    const FoodRuntimeId identity = world.mFoodData.runtimeId(food);

    world.updateGrids(false);
    world.eatFoods(SimulationWorld::DELTA_TIME);
    world.updateCellSmellInteraction(senseocyte);

    assertNear(0.01, world.mCellData.mMass[first] - firstMassBefore, 1e-12,
            "First phagocyte keeps ownership of the first capacity-limited bite");
    assertNear(0.02, world.mCellData.mMass[second] - secondMassBefore, 1e-12,
            "Second phagocyte consumes only after the first owner");
    assertTrue(world.mFoodData.mFoodCount == 1
                    && world.mFoodData.runtimeId(0) == identity,
            "Partial consumption preserves Food identity");
    const double remainingMass = world.mFoodData.mMass[0];
    const double distance = world.mCellData.mPositionX[senseocyte]
            - world.mFoodData.mPositionX[0];
    const double expected = remainingMass * CellData::SMELL_SENSITIVITY
            * BesselLookup::k0(std::abs(distance) / SimulationWorld::SMELL_RADIUS);
    assertNear(expected, world.mCellData.mSignalSubstanceOut[senseocyte][0],
            1e-6,
            "Food smell contribution uses mass remaining after ordered consumption");
}

void testSimulationWorldFoodSmellDoesNotTreatMatchingFoodIndexAsSelf() {
    Environment environment = Environment::constructDefaultEnvironment(2, 1, 1.0);
    SimulationWorld world(false, environment);
    const int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[senseocyte] = 0.0;
    world.mCellData.mPositionY[senseocyte] = 0.0;
    configureFoodSenseocyte(world, senseocyte);
    const int food = world.mFoodData.addFood(
            0.03, 0.0, 0.2, 0.0f, world.mEnvironment);

    assertTrue(senseocyte == food,
            "Food-smell index-alias fixture uses matching Cell and Food indices");
    world.updateGrids();
    world.addFoodSmellToGrid(food);
    world.updateCellSmellInteraction(senseocyte);

    const double expected = world.mFoodData.mMass[food]
            * CellData::SMELL_SENSITIVITY
            * BesselLookup::k0(0.03 / SimulationWorld::SMELL_RADIUS);
    assertNear(expected, world.mCellData.mSignalSubstanceOut[senseocyte][0],
            1e-6,
            "Food index matching the consumer Cell index is not a self-smell");
}

void testSimulationWorldSenseocyteFoodAndFatSmellFiltersByLipidLayer() {
    Environment environment = Environment::constructDefaultEnvironment(4, 3, 1.0);
    SimulationWorld world(false, environment);
    int dummy0 = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int dummy1 = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.markCellAsDead(dummy0);
    world.mCellData.markCellAsDead(dummy1);
    int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[senseocyte] = 0.0;
    world.mCellData.mPositionY[senseocyte] = 0.0;
    world.mCellData.editActiveGene(senseocyte, [](GeneBuilder& senseGene) {
        senseGene.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 1.0f);
        senseGene.setIntegerValue(Gene::SMELL_OUT_INTEGER_INDEX, 0);
        senseGene.setIntegerValue(
                Gene::SMELL_TYPE_INTEGER_INDEX, SimulationWorld::SMELL_TYPE_FOOD);
    });
    world.mFoodData.addFood(0.03, 0.0, 0.2, 0.0f, world.mEnvironment);
    world.mFoodData.addFood(0.04, 0.0, 0.3, 1.0f, world.mEnvironment);
    world.updateGrids();
    world.addFoodSmellToGrid(0);
    world.addFoodSmellToGrid(1);

    world.updateCellSmellInteraction(senseocyte);

    double expectedFood = 0.2 * CellData::SMELL_SENSITIVITY * BesselLookup::k0(0.03 / SimulationWorld::SMELL_RADIUS);
    assertNear(expectedFood, world.mCellData.mSignalSubstanceOut[senseocyte][0], 1e-6, "SimulationWorld senseocyte food smell ignores fat food");

    world.mCellData.mSignalSubstanceOut[senseocyte][0] = 0.0f;
    world.mCellData.editActiveGene(senseocyte, [](GeneBuilder& senseGene) {
        senseGene.setIntegerValue(
                Gene::SMELL_TYPE_INTEGER_INDEX, SimulationWorld::SMELL_TYPE_FAT);
    });

    world.updateCellSmellInteraction(senseocyte);

    double expectedFat = 0.3 * CellData::SMELL_SENSITIVITY * BesselLookup::k0(0.04 / SimulationWorld::SMELL_RADIUS);
    assertNear(expectedFat, world.mCellData.mSignalSubstanceOut[senseocyte][0], 1e-6, "SimulationWorld senseocyte fat smell ignores uncoated food");
}

void testSimulationWorldStereocyteFoodSmellUsesDirectionAndMirror() {
    Environment environment = Environment::constructDefaultEnvironment(3, 2, 1.0);
    SimulationWorld world(false, environment);
    int dummy = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.markCellAsDead(dummy);
    int stereocyte = addAliveCell(world.mCellData, CellType::STEREOCYTE);
    world.mCellData.mPositionX[stereocyte] = 0.0;
    world.mCellData.mPositionY[stereocyte] = 0.0;
    world.mCellData.mAngle[stereocyte] = std::acos(-1.0) / 2.0;
    world.mCellData.editActiveGene(stereocyte, [](GeneBuilder& stereoGene) {
        stereoGene.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 1.0f);
        stereoGene.setIntegerValue(Gene::SMELL_OUT_INTEGER_INDEX, 0);
        stereoGene.setIntegerValue(
                Gene::SMELL_TYPE_INTEGER_INDEX, SimulationWorld::SMELL_TYPE_FOOD);
    });
    world.mFoodData.addFood(0.03, 0.0, 0.2, 0.0f, world.mEnvironment);
    world.updateGrids();
    world.addFoodSmellToGrid(0);

    world.updateCellSmellInteraction(stereocyte);

    double expected = -0.2 * CellData::SMELL_SENSITIVITY * BesselLookup::k1(0.03 / SimulationWorld::SMELL_RADIUS);
    assertNear(expected, world.mCellData.mSignalSubstanceOut[stereocyte][0], 1e-6, "SimulationWorld stereocyte directional food smell");

    world.mCellData.mSignalSubstanceOut[stereocyte][0] = 0.0f;
    world.mCellData.mIsMirrored[stereocyte] = true;

    world.updateCellSmellInteraction(stereocyte);

    assertNear(-expected, world.mCellData.mSignalSubstanceOut[stereocyte][0], 1e-6, "SimulationWorld stereocyte mirrored food smell");
}

void testSimulationWorldUpdateAllCellInteractionsPartialRunsSmellBeforeForces() {
    Environment environment = Environment::constructDefaultEnvironment(3, 2, 1.0);
    environment.mViscosity = 0.0;
    environment.mGravity = 0.0;
    SimulationWorld world(false, environment);
    int dummy = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.markCellAsDead(dummy);
    int senseocyte = addAliveCell(world.mCellData, CellType::SENSEOCYTE);
    world.mCellData.mPositionX[senseocyte] = 0.0;
    world.mCellData.mPositionY[senseocyte] = 0.0;
    world.mCellData.mRadius[senseocyte] = 0.02;
    world.mCellData.editActiveGene(senseocyte, [](GeneBuilder& senseGene) {
        senseGene.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 1.0f);
        senseGene.setIntegerValue(Gene::SMELL_OUT_INTEGER_INDEX, 0);
        senseGene.setIntegerValue(
                Gene::SMELL_TYPE_INTEGER_INDEX, SimulationWorld::SMELL_TYPE_FOOD);
    });
    world.mFoodData.addFood(0.03, 0.0, 0.2, 0.0f, world.mEnvironment);
    world.updateGrids();
    world.addFoodSmellToGrid(0);

    world.updateAllCellInteractionsPartial(false, SimulationWorld::DELTA_TIME);

    double expectedFood = 0.2 * CellData::SMELL_SENSITIVITY * BesselLookup::k0(0.03 / SimulationWorld::SMELL_RADIUS);
    assertNear(expectedFood, world.mCellData.mSignalSubstanceOut[senseocyte][0], 1e-6, "SimulationWorld updateAll interactions runs smell pass");
    assertNear(0.0, world.mCellData.mForceX[senseocyte], 1e-12, "SimulationWorld updateAll interactions keeps forceX after zero viscosity");
    assertNear(0.0, world.mCellData.mForceY[senseocyte], 1e-12, "SimulationWorld updateAll interactions keeps forceY after zero gravity");
}

void testSimulationWorldUpdate1PartialResolvesFertilizationAfterOverlaps() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    environment.mViscosity = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mGravity = 0.0;
    SimulationWorld world(true, environment);
    int first = addAliveCell(world.mCellData, CellType::GAMETE);
    int second = addAliveCell(world.mCellData, CellType::GAMETE);
    world.mCellData.mPositionX[first] = 0.0;
    world.mCellData.mPositionY[first] = 0.0;
    world.mCellData.mRadius[first] = 0.02;
    world.mCellData.mMass[first] = 0.1;
    world.mCellData.mAge[first] = CellData::AGE_OF_CONSENT + 0.2;
    world.mCellData.mPositionX[second] = 0.001;
    world.mCellData.mPositionY[second] = 0.0;
    world.mCellData.mRadius[second] = 0.02;
    world.mCellData.mMass[second] = 0.1;
    world.mCellData.mAge[second] = CellData::AGE_OF_CONSENT + 0.2;
    world.mEnvironment.mRandom.setSeed(123456);
    SimulationRandom expectedRandom;
    expectedRandom.setSeed(123456);
    bool firstIsSperm = expectedRandom.nextInt(2) == 0;

    world.update1Partial(false, 1.0);

    int sperm = firstIsSperm ? first : second;
    int egg = firstIsSperm ? second : first;
    assertTrue(world.mCellData.mIsDead[sperm], "SimulationWorld update1Partial resolves random equal-radius gamete as sperm");
    assertTrue(!world.mCellData.mIsDead[egg], "SimulationWorld update1Partial keeps random equal-radius gamete as egg");
    assertNear(0.2, world.mCellData.mMass[egg], 1e-12, "SimulationWorld update1Partial equal-radius fertilization transfers mass");
    assertTrue(world.mCellData.mFertilizationTarget[first] == -1, "SimulationWorld update1Partial clears first fertilization target");
    assertTrue(world.mCellData.mFertilizationTarget[second] == -1, "SimulationWorld update1Partial clears second fertilization target");
}

void testSimulationWorldUpdate1PartialFertilizesContainedGametesBeforeOverlapDeath() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    environment.mViscosity = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mGravity = 0.0;
    SimulationWorld world(true, environment);
    int larger = addAliveCell(world.mCellData, CellType::GAMETE);
    int smaller = addAliveCell(world.mCellData, CellType::GAMETE);
    double largerRadius = 0.024;
    double smallerRadius = 0.014;
    double largerMass = CellData::MASS_PER_AREA * largerRadius * largerRadius;
    double smallerMass = CellData::MASS_PER_AREA * smallerRadius * smallerRadius;
    world.mCellData.mPositionX[larger] = 0.0;
    world.mCellData.mPositionY[larger] = 0.0;
    world.mCellData.mRadius[larger] = largerRadius;
    world.mCellData.mMass[larger] = largerMass;
    world.mCellData.mNitrates[larger] = 0.2;
    world.mCellData.mAge[larger] = CellData::AGE_OF_CONSENT + 0.2;
    world.mCellData.mPositionX[smaller] = 0.01;
    world.mCellData.mPositionY[smaller] = 0.0;
    world.mCellData.mRadius[smaller] = smallerRadius;
    world.mCellData.mMass[smaller] = smallerMass;
    world.mCellData.mNitrates[smaller] = 0.3;
    world.mCellData.mAge[smaller] = CellData::AGE_OF_CONSENT + 0.2;
    world.mEnvironment.mRandom.setSeed(7811);
    SimulationRandom expectedRandom;
    expectedRandom.setSeed(7811);
    bool largerIsSperm = expectedRandom.nextInt(2) == 0;

    world.update1Partial(false, 1.0);

    int sperm = largerIsSperm ? larger : smaller;
    int egg = largerIsSperm ? smaller : larger;
    assertTrue(!world.mCellData.mIsDead[egg], "SimulationWorld update1Partial keeps random contained gamete as egg");
    assertTrue(world.mCellData.mIsDead[sperm], "SimulationWorld update1Partial resolves random contained gamete as sperm");
    assertNear(largerMass + smallerMass, world.mCellData.mMass[egg], 1e-12, "SimulationWorld update1Partial contained gamete transfers mass");
    assertNear(0.5, world.mCellData.mNitrates[egg], 1e-12, "SimulationWorld update1Partial contained gamete transfers nitrates");
}

void testSimulationWorldFertilizationRemovesIncomingCiliocyteContactToDeadSperm() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.addRandom = false;
    environment.mFoodSpeed = 0.0;
    environment.mViscosity = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mGravity = 0.0;
    SimulationWorld world(true, environment);
    const int first = addAliveCell(world.mCellData, CellType::GAMETE);
    const int second = addAliveCell(world.mCellData, CellType::GAMETE);
    const int ciliocyte = addCiliocyteTestCell(
            world.mCellData, CellType::CILIOCYTE, 0.0, 0.0, 0.1, 0.02);
    constexpr std::int64_t seed = 90217;
    SimulationRandom expectedRandom;
    expectedRandom.setSeed(seed);
    const bool firstIsSperm = expectedRandom.nextInt(2) == 0;
    const int sperm = firstIsSperm ? first : second;
    const int egg = firstIsSperm ? second : first;
    const double spermX = firstIsSperm ? 0.0 : 0.001;
    world.mEnvironment.mRandom.setSeed(seed);
    world.mCellData.mPositionX[first] = 0.0;
    world.mCellData.mPositionY[first] = 0.0;
    world.mCellData.mPositionX[second] = 0.001;
    world.mCellData.mPositionY[second] = 0.0;
    world.mCellData.mPositionX[ciliocyte] = spermX + (firstIsSperm ? -0.03 : 0.03);
    world.mCellData.mPositionY[ciliocyte] = 0.0;
    for (int gamete : {first, second}) {
        world.mCellData.mRadius[gamete] = 0.02;
        world.mCellData.mMass[gamete] = CellData::MASS_PER_AREA * 0.02 * 0.02;
        world.mCellData.mAge[gamete] = CellData::AGE_OF_CONSENT + 0.2;
    }
    const CellRuntimeId spermIdentity = world.mCellData.runtimeId(sperm);

    world.update1Partial(false, 1.0);

    assertTrue(world.mCellData.mIsDead[sperm],
            "Three-Cell fixture resolves the predicted sperm");
    assertTrue(!world.mCellData.mIsDead[egg],
            "Three-Cell fixture preserves the predicted egg");
    for (int actor = 0; actor < world.mCellData.mCellCount; actor++) {
        const int begin = world.mCellData.cellCollisionContactBegin(actor);
        const int end = world.mCellData.cellCollisionContactEnd(actor);
        for (int contact = begin; contact < end; contact++) {
            const int target = world.mCellData.cellCollisionContactTarget(contact);
            assertTrue(world.mCellData.runtimeId(target) != spermIdentity,
                    "Fertilization removes every incoming contact to the dead sperm identity");
        }
    }
    for (int intent = 0; intent < world.mCellData.mCellCollisionIntentBuffer.count; intent++) {
        const int target = world.mCellData.mCellCollisionIntentBuffer.collidedCellIndex[intent];
        assertTrue(world.mCellData.runtimeId(target) != spermIdentity,
                "Dead sperm is absent from ordinary and typed collision intents");
    }
    for (int interaction = 0;
            interaction < world.mCellData.mCiliocyteInteractionCount;
            interaction++) {
        const int target = world.mCellData.mCiliocyteInteractionCellIndex2[interaction];
        assertTrue(target == CellData::BOUNDARY_CONNECTION_INDEX
                        || world.mCellData.runtimeId(target) != spermIdentity,
                "Dead sperm is absent from the Ciliocyte queue");
    }
    for (int cell = 0; cell < world.mCellData.mCellCount; cell++) {
        assertTrue(std::isfinite(world.mCellData.mVelocityX[cell])
                        && std::isfinite(world.mCellData.mVelocityY[cell])
                        && std::isfinite(world.mCellData.mAngularVelocity[cell]),
                "Fertilization/Ciliocyte update1 state remains finite");
    }
}

void testCellDataFertilizationDeadTargetCleanupPreservesTypedContactOrder() {
    constexpr std::array<CellType, 4> actorTypes{
            CellType::VIROCYTE,
            CellType::GLUEOCYTE,
            CellType::DEVOROCYTE,
            CellType::SECROCYTE,
    };
    for (const CellType actorType : actorTypes) {
        Environment environment = Environment::constructDefaultEnvironment(6, 0, 1.0);
        constexpr std::int64_t seed = 7331;
        environment.mRandom.setSeed(seed);
        SimulationRandom expectedRandom;
        expectedRandom.setSeed(seed);
        CellData cells(6);
        const int actor = addAliveCell(cells, actorType);
        const int firstGamete = addAliveCell(cells, CellType::GAMETE);
        const int secondGamete = addAliveCell(cells, CellType::GAMETE);
        const int firstSurvivor = addAliveCell(cells, CellType::PHAGOCYTE);
        const int secondSurvivor = addAliveCell(cells, CellType::PHAGOCYTE);
        const bool firstIsSperm = expectedRandom.nextInt(2) == 0;
        const int sperm = firstIsSperm ? firstGamete : secondGamete;
        for (int cellIndex = 0; cellIndex < cells.mCellCount; cellIndex++) {
            cells.mFertilizationTarget[cellIndex] = -1;
        }
        cells.mPositionX[actor] = 0.0;
        cells.mPositionX[firstSurvivor] = 0.01;
        cells.mPositionX[sperm] = 0.015;
        cells.mPositionX[secondSurvivor] = -0.01;
        cells.mFertilizationTarget[firstGamete] = secondGamete;
        cells.mFertilizationTarget[secondGamete] = firstGamete;
        addCollisionContact(cells, actor, firstSurvivor);
        addCollisionContact(cells, actor, sperm);
        addCollisionContact(cells, actor, secondSurvivor);

        assertTrue(cells.resolveFertilizationIntents(environment),
                "Typed cleanup fixture applies fertilization");

        assertNear(2.0,
                cells.cellCollisionContactCount(actor),
                0.0,
                "Typed cleanup removes exactly the incoming dead-target contact");
        assertTrue(collisionContactTargetAt(cells, actor, 0) == firstSurvivor
                        && collisionContactTargetAt(cells, actor, 1) == secondSurvivor,
                "Typed cleanup preserves surviving directed-contact order");
        cells.collectAllCellToCellCollisionIntentsPartial();
        for (int intent = 0; intent < cells.mCellCollisionIntentBuffer.count; intent++) {
            assertTrue(cells.mCellCollisionIntentBuffer.collidedCellIndex[intent] != sperm,
                    "Typed cleanup matrix never collects an intent to dead sperm");
        }
    }
}

void testCellDataCiliocyteDefensivelyRejectsDeadOrInvalidMassEndpoint() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    CellData cells(3);
    const int ciliocyte = addCiliocyteTestCell(
            cells, CellType::CILIOCYTE, 0.0, 0.0, 0.1, 0.02);
    const int target = addCiliocyteTestCell(
            cells, CellType::PHAGOCYTE, 0.03, 0.0, 0.1, 0.02);
    cells.markCellAsDead(target);
    cells.mMass[target] = 0.0;

    cells.handleCiliocyteCollision(ciliocyte, target, 1.0, 0.0);
    assertTrue(cells.mCiliocyteInteractionCount == 0,
            "Ciliocyte does not queue a dead zero-mass endpoint");
    cells.addCiliocyteInteraction(ciliocyte, target);
    cells.updateCiliocyteInteractions(environment);
    assertTrue(std::isfinite(cells.mVelocityX[ciliocyte])
                    && std::isfinite(cells.mVelocityY[ciliocyte])
                    && std::isfinite(cells.mAngularVelocity[ciliocyte])
                    && std::isfinite(cells.mVelocityX[target])
                    && std::isfinite(cells.mVelocityY[target])
                    && std::isfinite(cells.mAngularVelocity[target]),
            "Queued stale Ciliocyte endpoint is ignored without NaN/Inf");
}

void testSimulationWorldUpdate1PartialRunsPortedJavaStageOrder() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mLightAngle = 0.0;
    environment.mLightSpeed = 2.0;
    environment.mViscosity = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mGravity = 0.0;
    SimulationWorld world(true, environment);
    world.mEnvironment.mLightAngle = 0.0;
    world.mEnvironment.mLightAngleCos = 1.0;
    world.mEnvironment.mLightAngleSin = 0.0;
    int left = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    int right = addAliveCell(world.mCellData, CellType::PHAGOCYTE);
    world.mCellData.mPositionX[left] = 0.0;
    world.mCellData.mPositionY[left] = 0.0;
    world.mCellData.mRadius[left] = 0.02;
    world.mCellData.mMass[left] = 1.0;
    world.mCellData.mPositionX[right] = 0.03;
    world.mCellData.mPositionY[right] = 0.0;
    world.mCellData.mRadius[right] = 0.02;
    world.mCellData.mMass[right] = 1.0;

    world.update1Partial(false, 2.0);

    assertNear(SimulationWorld::DELTA_TIME * 2.0, world.mTime, 1e-12, "SimulationWorld update1Partial advances time");
    assertNear(2.0 * SimulationWorld::DELTA_TIME * 2.0, world.mEnvironment.mLightAngle, 1e-12, "SimulationWorld update1Partial updates light angle");
    assertNear(std::cos(world.mEnvironment.mLightAngle), world.mEnvironment.mLightAngleCos, 1e-12, "SimulationWorld update1Partial updates light cos");
    assertTrue(world.mCellData.cellCollisionContactCount(left) == 1, "SimulationWorld update1Partial grid overlap feeds left collision");
    assertTrue(collisionContactTargetAt(world.mCellData, left, 0) == right, "SimulationWorld update1Partial left collision target");
    assertTrue(world.mCellData.cellCollisionContactCount(right) == 1, "SimulationWorld update1Partial grid overlap feeds right collision");
    assertTrue(collisionContactTargetAt(world.mCellData, right, 0) == left, "SimulationWorld update1Partial right collision target");
    assertTrue(world.mCellData.mVelocityX[left] < 0.0, "SimulationWorld update1Partial applies collision velocity left");
    assertTrue(world.mCellData.mVelocityX[right] > 0.0, "SimulationWorld update1Partial applies collision velocity right");
    assertNear(world.mCellData.mVelocityX[left], world.mCellData.mForceX[left], 1e-12, "SimulationWorld update1Partial stores velocity scratch forceX left");
    assertNear(world.mCellData.mVelocityX[right], world.mCellData.mForceX[right], 1e-12, "SimulationWorld update1Partial stores velocity scratch forceX right");
}

void testSimulationWorldUpdate1PartialResetsForcesBeforeFlagellocytes() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    environment.mViscosity = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mGravity = 0.0;
    SimulationWorld world(false, environment);
    int flagellocyte = addCiliocyteTestCell(world.mCellData, CellType::FLAGELLOCYTE, 0.0, 0.0, 1.0, 0.02);
    setConstantValue(
            world.mCellData,
            flagellocyte,
            Gene::SWIM_FORCE_VALUE_INDEX,
            1.0f);
    world.mCellData.mAngle[flagellocyte] = 0.0;
    world.mCellData.mForceX[flagellocyte] = 100.0;
    world.mCellData.mForceY[flagellocyte] = 50.0;

    world.update1Partial(false, 1.0);

    assertNear(CellData::MAX_SWIM_FORCE * SimulationWorld::DELTA_TIME, world.mCellData.mVelocityX[flagellocyte], 1e-12, "SimulationWorld update1Partial resets old force before flagellocyte force");
    assertNear(0.0, world.mCellData.mVelocityY[flagellocyte], 1e-12, "SimulationWorld update1Partial resets old forceY before flagellocyte force");
    assertNear(world.mCellData.mVelocityX[flagellocyte], world.mCellData.mForceX[flagellocyte], 1e-12, "SimulationWorld update1Partial flagellocyte forceX velocity scratch");
    assertNear(CellData::MAX_SWIM_FORCE * SimulationWorld::DELTA_TIME, world.mCellData.mSwimAnimationTime[flagellocyte], 1e-6, "SimulationWorld update1Partial advances flagellocyte animation");
}

void testSimulationWorldUpdate1PartialRunsCellSplitAfterKinematics() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.aging = false;
    environment.mNitrates = 0.0;
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    assertTrue(cellData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    }),
            "CellData update1Partial configures split parent");
    cellData.editGene(parent, GeneIndex(1), [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    cellData.editGene(parent, GeneIndex(2), [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    cellData.mMass[parent] = 0.2;
    cellData.mRadius[parent] = 0.02;
    cellData.mAge[parent] = CellData::AGE_OF_CONSENT + 1.0;
    cellData.mNitrates[parent] = 1.0;
    cellData.mTelomeresCount[parent] = -1;

    world.update1Partial(false, 1.0);

    assertTrue(cellData.mCellCount == 2, "SimulationWorld update1Partial runs handleCellSplit");
    assertTrue(cellData.activeGeneIndex(0) == GeneIndex(1), "SimulationWorld update1Partial split child1 gene index");
    assertTrue(cellData.activeGeneIndex(1) == GeneIndex(2), "SimulationWorld update1Partial split child2 gene index");
}

void testCellDataCollisionGeometry() {
    CellData cellData(3);
    int current = addAliveCell(cellData, CellType::PHAGOCYTE);
    int collided = addAliveCell(cellData, CellType::PHAGOCYTE);
    int third = addAliveCell(cellData, CellType::PHAGOCYTE);

    cellData.mPositionX[current] = 0.0;
    cellData.mPositionY[current] = 0.0;
    cellData.mRadius[current] = 0.06;
    cellData.mPositionX[collided] = 0.08;
    cellData.mPositionY[collided] = 0.06;
    cellData.mRadius[collided] = 0.05;

    double distanceX = -0.08;
    double distanceY = -0.06;
    double distanceSq = distanceX * distanceX + distanceY * distanceY;
    CellData::CellCollisionGeometry geometry;
    assertTrue(cellData.calculateCellCollisionGeometryFromDistance(current,
                       collided,
                       distanceX,
                       distanceY,
                       distanceSq,
                       geometry),
            "CellData collision geometry detects overlap");
    double distance = std::sqrt(distanceSq);
    double expectedContactChordLengthSquared = ((-distance + 0.06 - 0.05)
                                                       * (-distance - 0.06 + 0.05)
                                                       * (-distance + 0.06 + 0.05)
                                                       * (distance + 0.06 + 0.05))
            / distanceSq;
    assertNear(distanceX, geometry.distanceX, 1e-12, "CellData collision geometry distanceX");
    assertNear(distanceY, geometry.distanceY, 1e-12, "CellData collision geometry distanceY");
    assertNear(distance, geometry.distance, 1e-12, "CellData collision geometry distance");
    assertNear(expectedContactChordLengthSquared, geometry.contactChordLengthSquared, 1e-12, "CellData collision geometry overlap chord sq");
    assertNear(0.8, geometry.normalX, 1e-12, "CellData collision geometry normalX");
    assertNear(0.6, geometry.normalY, 1e-12, "CellData collision geometry normalY");

    assertTrue(cellData.isValidCellCollisionTarget(current, collided), "CellData valid collision target");
    assertTrue(!cellData.isValidCellCollisionTarget(current, current), "CellData invalid self collision target");
    assertTrue(!cellData.isValidCellCollisionTarget(current, -1), "CellData invalid negative collision target");
    assertTrue(!cellData.isValidCellCollisionTarget(current, 99), "CellData invalid out of range collision target");
    cellData.markCellAsDead(collided);
    assertTrue(!cellData.isValidCellCollisionTarget(current, collided), "CellData invalid dead collision target");
    assertTrue(!cellData.calculateCellCollisionGeometryFromDistance(current,
                       collided,
                       distanceX,
                       distanceY,
                       distanceSq,
                       geometry),
            "CellData collision geometry rejects dead target");
    cellData.markCellAsAlive(collided);

    cellData.mPositionX[third] = 1.0;
    cellData.mPositionY[third] = 0.0;
    cellData.mRadius[third] = 0.05;
    assertTrue(!cellData.calculateCellCollisionGeometryFromDistance(current,
                       third,
                       -1.0,
                       0.0,
                       1.0,
                       geometry),
            "CellData collision geometry rejects separated cells");

    cellData.mPositionX[third] = 0.0;
    cellData.mPositionY[third] = 0.0;
    assertTrue(!cellData.calculateCellCollisionGeometryFromDistance(current,
                       third,
                       0.0,
                       0.0,
                       0.0,
                       geometry),
            "CellData collision geometry rejects zero distance");

    cellData.clearCellCollisionContacts();
    assertTrue(cellData.addCellCollisionContact(current, collided, distanceX, distanceY, distanceSq),
            "CellData contact buffer accepts overlap");
    CellData::CellCollisionGeometry contactGeometry;
    cellData.getCellCollisionContactGeometry(cellData.cellCollisionContactBegin(current), contactGeometry);
    assertNear(expectedContactChordLengthSquared,
            contactGeometry.contactChordLengthSquared,
            1e-12,
            "CellData contact buffer reconstructs overlap chord sq");
}

void testCellDataCollisionForce() {
    CellData cellData(2);
    int left = addAliveCell(cellData, CellType::PHAGOCYTE);
    int right = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[left] = 0.0;
    cellData.mPositionY[left] = 0.0;
    cellData.mRadius[left] = 0.02;
    cellData.mPositionX[right] = 0.03;
    cellData.mPositionY[right] = 0.0;
    cellData.mRadius[right] = 0.02;
    cellData.editActiveGene(left, [](GeneBuilder& gene) {
        gene.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    });
    cellData.editActiveGene(right, [](GeneBuilder& gene) {
        gene.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    });

    CellData::CellCollisionGeometry geometry;
    double distanceX = cellData.mPositionX[left] - cellData.mPositionX[right];
    double distanceY = cellData.mPositionY[left] - cellData.mPositionY[right];
    double distanceSq = distanceX * distanceX + distanceY * distanceY;
    assertTrue(cellData.calculateCellCollisionGeometryFromDistance(left,
                       right,
                       distanceX,
                       distanceY,
                       distanceSq,
                       geometry),
            "CellData collision force geometry");
    cellData.applyCellCollisionForce(left, right, geometry);

    double distance = 0.03;
    double radius = 0.02;
    double contactChordLengthSquared = ((-distance + radius - radius)
                                               * (-distance - radius + radius)
                                               * (-distance + radius + radius)
                                               * (distance + radius + radius))
            / (distance * distance);
    double expectedForce = -distance * CellData::TURGOR_PRESSURE * contactChordLengthSquared / distance;

    assertNear(expectedForce, cellData.mForceX[left], 1e-12, "CellData applyCellCollisionForce forceX");
    assertNear(0.0, cellData.mForceY[left], 1e-12, "CellData applyCellCollisionForce forceY");
    assertNear(0.0, cellData.mForceX[right], 1e-12, "CellData applyCellCollisionForce only writes current cell");
}

void testCellDataGameteForceModifier() {
    CellData cellData(2);
    int a = addAliveCell(cellData, CellType::GAMETE);
    int b = addAliveCell(cellData, CellType::GAMETE);
    cellData.editGene(a, GeneIndex(1), [](GeneBuilder& gene) {
        gene.setCellType(CellType::GAMETE);
    });
    cellData.editGene(b, GeneIndex(2), [](GeneBuilder& gene) {
        gene.setCellType(CellType::GAMETE);
    });
    cellData.setActiveGeneIndex(a, GeneIndex(1));
    cellData.setActiveGeneIndex(b, GeneIndex(2));
    cellData.mAge[a] = CellData::AGE_OF_CONSENT + 0.1;
    cellData.mAge[b] = CellData::AGE_OF_CONSENT + 0.1;

    cellData.editActiveGene(a, [](GeneBuilder& gene) {
        gene.setVirusToGeneIndex(GeneIndex(2));
    });
    cellData.editActiveGene(b, [](GeneBuilder& gene) {
        gene.setVirusToGeneIndex(GeneIndex(1));
    });
    assertNear(-0.6, cellData.calculateGameteForceModifier(a, b), 0.0, "CellData gamete mutual match modifier");

    cellData.editActiveGene(b, [](GeneBuilder& gene) {
        gene.setVirusToGeneIndex(GeneIndex(0));
    });
    assertNear(0.0, cellData.calculateGameteForceModifier(a, b), 0.0, "CellData gamete one-way match modifier");

    cellData.editActiveGene(a, [](GeneBuilder& gene) {
        gene.setVirusToGeneIndex(GeneIndex(0));
    });
    assertNear(1.0, cellData.calculateGameteForceModifier(a, b), 0.0, "CellData gamete no match modifier");

    cellData.mAge[a] = CellData::AGE_OF_CONSENT;
    cellData.editActiveGene(a, [](GeneBuilder& gene) {
        gene.setVirusToGeneIndex(GeneIndex(2));
    });
    cellData.editActiveGene(b, [](GeneBuilder& gene) {
        gene.setVirusToGeneIndex(GeneIndex(1));
    });
    assertNear(1.0, cellData.calculateGameteForceModifier(a, b), 0.0, "CellData young gamete default modifier");

    cellData.editActiveGene(a, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    cellData.mAge[a] = CellData::AGE_OF_CONSENT + 0.1;
    assertNear(1.0, cellData.calculateGameteForceModifier(a, b), 0.0, "CellData non-gamete default modifier");
}

void testCellDataCollisionEffectPass() {
    CellData cellData(3);
    int left = addAliveCell(cellData, CellType::PHAGOCYTE);
    int right = addAliveCell(cellData, CellType::PHAGOCYTE);
    int far = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[left] = 0.0;
    cellData.mPositionY[left] = 0.0;
    cellData.mRadius[left] = 0.02;
    cellData.mPositionX[right] = 0.03;
    cellData.mPositionY[right] = 0.0;
    cellData.mRadius[right] = 0.02;
    cellData.mPositionX[far] = 1.0;
    cellData.mPositionY[far] = 0.0;
    cellData.mRadius[far] = 0.02;

    addCollisionContact(cellData, left, right);

    cellData.handleCellCollisionEffects(left);

    double distance = 0.03;
    double radius = 0.02;
    double contactChordLengthSquared = ((-distance + radius - radius)
                                               * (-distance - radius + radius)
                                               * (-distance + radius + radius)
                                               * (distance + radius + radius))
            / (distance * distance);
    double expectedForce = -distance * CellData::TURGOR_PRESSURE * contactChordLengthSquared / distance;

    assertNear(expectedForce, cellData.mForceX[left], 1e-12, "CellData handleCellCollisionEffects forceX");
    assertNear(0.0, cellData.mForceY[left], 1e-12, "CellData handleCellCollisionEffects forceY");
    assertNear(0.0, cellData.mForceX[right], 1e-12, "CellData handleCellCollisionEffects does not write target");

    cellData.mForceX[left] = 0.0;
    cellData.mForceY[left] = 0.0;
    cellData.markCellAsDead(left);
    cellData.handleCellCollisionEffects(left);
    assertNear(0.0, cellData.mForceX[left], 0.0, "CellData handleCellCollisionEffects skips dead current");
}

void testCellDataCiliocyteCollisionAddsInteractionOnlyForUnlinkedCiliocyte() {
    CellData cellData(2);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.0, 0.0, 1.0, 0.02);
    int other = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.03, 0.0, 1.0, 0.02);
    cellData.mVelocityX[other] = 0.5;

    cellData.handleCiliocyteCollision(ciliocyte, other, 1.0, 0.0);

    assertTrue(cellData.mCiliocyteInteractionCount == 1, "CellData ciliocyte collision interaction count");
    assertTrue(cellData.mCiliocyteInteractionCellIndex1[0] == ciliocyte, "CellData ciliocyte collision first index");
    assertTrue(cellData.mCiliocyteInteractionCellIndex2[0] == other, "CellData ciliocyte collision second index");
    assertNear(2.5, cellData.mForceX[ciliocyte], 1e-12, "CellData ciliocyte collision forceX");
    assertNear(0.0, cellData.mForceY[ciliocyte], 1e-12, "CellData ciliocyte collision forceY");
}

void testCellDataCiliocyteCollisionSkipsNonCiliocyteOrLinkedCell() {
    CellData cellData(3);
    int phagocyte = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 1.0, 0.02);
    int other = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.03, 0.0, 1.0, 0.02);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, -0.03, 0.0, 1.0, 0.02);

    cellData.handleCiliocyteCollision(phagocyte, other, 1.0, 0.0);
    assertTrue(cellData.mCiliocyteInteractionCount == 0, "CellData ciliocyte collision skips non-ciliocyte");

    cellData.addLink(
            ciliocyte, other, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    cellData.handleCiliocyteCollision(ciliocyte, other, 1.0, 0.0);
    assertTrue(cellData.mCiliocyteInteractionCount == 0, "CellData ciliocyte collision skips linked cell");
}

void testCellDataCiliocyteBoundaryCollisionAddsInteractionUnlessBoundaryLinked() {
    CellData cellData(1);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.99, 0.0, 1.0, 0.02);

    cellData.handleCiliocyteToBoundaryCollision(ciliocyte);

    assertTrue(cellData.mCiliocyteInteractionCount == 1, "CellData ciliocyte wall interaction count");
    assertTrue(cellData.mCiliocyteInteractionCellIndex1[0] == ciliocyte, "CellData ciliocyte wall first index");
    assertTrue(cellData.mCiliocyteInteractionCellIndex2[0] == CellData::BOUNDARY_CONNECTION_INDEX, "CellData ciliocyte wall boundary index");

    cellData.mCiliocyteInteractionCount = 0;
    cellData.addLink(
            ciliocyte,
            CellData::BOUNDARY_CONNECTION_INDEX,
            CellLinkCreationReason::UNSPECIFIED,
            0.0,
            0.0,
            0.0f,
            0.0f);
    cellData.handleCiliocyteToBoundaryCollision(ciliocyte);
    assertTrue(cellData.mCiliocyteInteractionCount == 0, "CellData ciliocyte wall skips boundary-linked cell");
}

void testCellDataBoundaryCiliocyteInteractionRemovesTangentialSlip() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    environment.mIsPreviewMode = true;
    environment.mViscosity = 0.0;
    environment.mGravity = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.dieEdge = false;

    CellData cellData(1);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.99, 0.0, 1.0, 0.02);
    cellData.mForceX[ciliocyte] = 0.3;
    cellData.mForceY[ciliocyte] = 0.4;
    cellData.mTorque[ciliocyte] = 0.0;
    cellData.addCiliocyteInteraction(ciliocyte, CellData::BOUNDARY_CONNECTION_INDEX);

    cellData.updateCiliocyteInteractions(environment);

    double x = environment.mRadius - 0.99;
    double inertia = 1.0 * 0.02 * 0.02 * 0.7;
    double iem = 1.0 / (1.0 + x * x / inertia) / SimulationWorld::DELTA_TIME * 0.9;
    double expectedFy = -0.4 * iem * SimulationWorld::DELTA_TIME;
    double expectedOmega = x * expectedFy / inertia;

    assertNear(0.0, cellData.mVelocityX[ciliocyte], 1e-12, "CellData boundary ciliocyte velocityX");
    assertNear(expectedFy, cellData.mVelocityY[ciliocyte], 1e-12, "CellData boundary ciliocyte velocityY");
    assertNear(expectedOmega, cellData.mAngularVelocity[ciliocyte], 1e-12, "CellData boundary ciliocyte angular velocity");
}

void testCellDataCiliocyteInteractionAppliesEqualAndOppositeImpulse() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(2);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.0, 0.0, 1.0, 0.02);
    int other = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.03, 0.0, 2.0, 0.02);
    cellData.mForceY[ciliocyte] = 0.6;
    cellData.mForceY[other] = 0.1;
    cellData.addCiliocyteInteraction(ciliocyte, other);

    cellData.updateCiliocyteInteractions(environment);

    double distance = 0.03;
    double x = distance / 2.0;
    double inertia1 = 1.0 * 0.02 * 0.02 * 0.7;
    double inertia2 = 2.0 * 0.02 * 0.02 * 0.7;
    double iem = 1.0 / (1.0 / 1.0 + 1.0 / 2.0 + x * x / inertia1 + (distance - x) * (distance - x) / inertia2)
            / SimulationWorld::DELTA_TIME * 0.9;
    double fy = -(0.6 - 0.1) * iem;

    assertNear(fy * SimulationWorld::DELTA_TIME / 1.0, cellData.mVelocityY[ciliocyte], 1e-12, "CellData cell ciliocyte first velocityY");
    assertNear(-fy * SimulationWorld::DELTA_TIME / 2.0, cellData.mVelocityY[other], 1e-12, "CellData cell ciliocyte second velocityY");
    assertNear(x * fy * SimulationWorld::DELTA_TIME / inertia1, cellData.mAngularVelocity[ciliocyte], 1e-12, "CellData cell ciliocyte first angular velocity");
    assertNear((distance - x) * fy * SimulationWorld::DELTA_TIME / inertia2, cellData.mAngularVelocity[other], 1e-12, "CellData cell ciliocyte second angular velocity");
}

void testCellDataActiveCiliocyteUsesBendingOrLiftingByMirrorAndCrossSign() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(1);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.99, 0.0, 1.0, 0.02);
    cellData.mAngle[ciliocyte] = std::acos(-1.0) / 2.0;
    setConstantValue(cellData, ciliocyte, Gene::MYOCYTE_BENDING_VALUE_INDEX, 1.0f);
    setConstantValue(cellData, ciliocyte, Gene::MYOCYTE_LIFTING_VALUE_INDEX, -1.0f);
    cellData.addCiliocyteInteraction(ciliocyte, CellData::BOUNDARY_CONNECTION_INDEX);

    cellData.updateCiliocyteInteractions(environment);
    double velocityFromBending = cellData.mVelocityY[ciliocyte];

    cellData.mVelocityY[ciliocyte] = 0.0;
    cellData.mAngularVelocity[ciliocyte] = 0.0;
    cellData.mCiliocyteInteractionCount = 0;
    cellData.mIsMirrored[ciliocyte] = true;
    cellData.addCiliocyteInteraction(ciliocyte, CellData::BOUNDARY_CONNECTION_INDEX);

    cellData.updateCiliocyteInteractions(environment);

    assertNear(-velocityFromBending, cellData.mVelocityY[ciliocyte], 1e-12, "CellData mirrored ciliocyte reverses active velocity");
}

void testCellDataSecondCiliocyteContributesActiveTangentialVelocity() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(2);
    int first = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 1.0, 0.02);
    int second = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.03, 0.0, 1.0, 0.02);
    cellData.mAngle[second] = std::acos(-1.0) / 2.0;
    setConstantValue(cellData, second, Gene::MYOCYTE_BENDING_VALUE_INDEX, 1.0f);
    setConstantValue(cellData, second, Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0.0f);
    cellData.addCiliocyteInteraction(first, second);

    cellData.updateCiliocyteInteractions(environment);

    assertTrue(cellData.mVelocityY[first] > 0.0, "CellData second ciliocyte active first velocity positive");
    assertTrue(cellData.mVelocityY[second] < 0.0, "CellData second ciliocyte active second velocity negative");
    assertNear(-cellData.mVelocityY[first], cellData.mVelocityY[second], 1e-12, "CellData second ciliocyte active equal opposite");
}

void testCellDataSmallCiliocyteOrientationCrossDoesNotAddActiveVelocity() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(1);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.99, 0.0, 1.0, 0.02);
    cellData.mAngle[ciliocyte] = 0.0;
    setConstantValue(cellData, ciliocyte, Gene::MYOCYTE_BENDING_VALUE_INDEX, 1.0f);
    setConstantValue(cellData, ciliocyte, Gene::MYOCYTE_LIFTING_VALUE_INDEX, 1.0f);
    cellData.addCiliocyteInteraction(ciliocyte, CellData::BOUNDARY_CONNECTION_INDEX);

    cellData.updateCiliocyteInteractions(environment);

    assertNear(0.0, cellData.mVelocityX[ciliocyte], 1e-12, "CellData small cross velocityX");
    assertNear(0.0, cellData.mVelocityY[ciliocyte], 1e-12, "CellData small cross velocityY");
    assertNear(0.0, cellData.mAngularVelocity[ciliocyte], 1e-12, "CellData small cross angular velocity");
}

void testCellDataSingleCollisionPartialCombinesForceAndCiliocyte() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    CellData cellData(2);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.0, 0.0, 1.0, 0.02);
    int other = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.03, 0.0, 1.0, 0.02);
    cellData.mVelocityX[other] = 0.5;

    double distanceX = cellData.mPositionX[ciliocyte] - cellData.mPositionX[other];
    double distanceY = cellData.mPositionY[ciliocyte] - cellData.mPositionY[other];
    double distanceSq = distanceX * distanceX + distanceY * distanceY;
    CellData::CellCollisionGeometry geometry;
    assertTrue(cellData.calculateCellCollisionGeometryFromDistance(ciliocyte,
                       other,
                       distanceX,
                       distanceY,
                       distanceSq,
                       geometry),
            "CellData partial single contact geometry");
    cellData.handleSingleCellCollisionPartial(
            ciliocyte, other, geometry, 0.01, environment);

    double distance = 0.03;
    double radius = 0.02;
    double contactChordLengthSquared = ((-distance + radius - radius)
                                               * (-distance - radius + radius)
                                               * (-distance + radius + radius)
                                               * (distance + radius + radius))
            / (distance * distance);
    double expectedTurgorForce = -distance * CellData::TURGOR_PRESSURE * contactChordLengthSquared / distance;

    assertNear(expectedTurgorForce + 2.5, cellData.mForceX[ciliocyte], 1e-12, "CellData partial single force includes ciliocyte");
    assertNear(0.0, cellData.mForceY[ciliocyte], 1e-12, "CellData partial single forceY");
    assertTrue(cellData.mCiliocyteInteractionCount == 1, "CellData partial single ciliocyte interaction count");
    assertTrue(cellData.mCiliocyteInteractionCellIndex1[0] == ciliocyte, "CellData partial single ciliocyte first index");
    assertTrue(cellData.mCiliocyteInteractionCellIndex2[0] == other, "CellData partial single ciliocyte second index");
}

void testCellDataCollisionPartialWrapperCombinesGlueocyteSideEffects() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    CellData cellData(2);
    int glueocyte = addAliveCell(cellData, CellType::GLUEOCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mAge[glueocyte] = CellData::AGE_OF_CONSENT;
    cellData.mAge[victim] = CellData::AGE_OF_CONSENT;
    cellData.mPositionX[glueocyte] = 0.0;
    cellData.mPositionY[glueocyte] = 0.0;
    cellData.mRadius[glueocyte] = 0.02;
    cellData.mPositionX[victim] = 0.03;
    cellData.mPositionY[victim] = 0.0;
    cellData.mRadius[victim] = 0.02;
    addCollisionContact(cellData, glueocyte, victim);

    cellData.handleCellToCellCollisionsPartial(
            glueocyte, 0.01, environment);

    assertTrue(cellData.mTopology.isLinked(glueocyte, victim), "CellData partial wrapper glueocyte topology link");
    assertTrue(cellData.mForceX[glueocyte] < 0.0, "CellData partial wrapper applies force");
}

void testCellDataAllCollisionPartialWrapperResetsCiliocyteBufferAndTransfersMass() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    CellData cellData(4);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.0, 0.0, 1.0, 0.02);
    int other = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.03, 0.0, 1.0, 0.02);
    int devorocyte = addAliveCell(cellData, CellType::DEVOROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);

    cellData.mPositionX[devorocyte] = 0.0;
    cellData.mPositionY[devorocyte] = 0.1;
    cellData.mRadius[devorocyte] = 0.02;
    cellData.mPositionX[victim] = 0.03;
    cellData.mPositionY[victim] = 0.1;
    cellData.mRadius[victim] = 0.02;
    cellData.mMass[devorocyte] = 0.2;
    cellData.mMass[victim] = 0.1;
    cellData.mNitrates[victim] = 0.5;
    cellData.mToxin[victim] = 0.4f;

    cellData.mCiliocyteInteractionCount = 1;
    cellData.mCiliocyteInteractionCellIndex1[0] = 99;
    cellData.mCiliocyteInteractionCellIndex2[0] = 99;

    addCollisionContact(cellData, ciliocyte, other);
    addCollisionContact(cellData, devorocyte, victim);

    double deltaTime = 0.01;
    double deltaMass = CellData::DEVOROCYTE_SUCK_SPEED * deltaTime;
    cellData.handleAllCellToCellCollisionsPartial(
            deltaTime, environment);

    assertTrue(cellData.mCiliocyteInteractionCount == 1, "CellData all partial wrapper resets and records ciliocyte buffer");
    assertTrue(cellData.mCiliocyteInteractionCellIndex1[0] == ciliocyte, "CellData all partial wrapper ciliocyte first index");
    assertTrue(cellData.mCiliocyteInteractionCellIndex2[0] == other, "CellData all partial wrapper ciliocyte second index");
    assertNear(0.2 + deltaMass, cellData.mMass[devorocyte], 1e-12, "CellData all partial wrapper devorocyte mass");
    assertNear(0.1 - deltaMass, cellData.mMass[victim], 1e-12, "CellData all partial wrapper victim mass");
}

void testCellDataBoundaryCollisionPartialAppliesForce() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(2);
    int inside = addAliveCell(cellData, CellType::PHAGOCYTE);
    int touching = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[inside] = 0.5;
    cellData.mPositionY[inside] = 0.0;
    cellData.mRadius[inside] = 0.02;
    cellData.mPositionX[touching] = 0.99;
    cellData.mPositionY[touching] = 0.0;
    cellData.mRadius[touching] = 0.02;
    cellData.editActiveGene(touching, [](GeneBuilder& gene) {
        gene.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    });

    cellData.handleCellToBoundaryCollisionPartial(
            inside, environment);
    assertNear(0.0, cellData.mForceX[inside], 0.0, "CellData wall partial skips non-contact force");

    cellData.handleCellToBoundaryCollisionPartial(
            touching, environment);

    double distance = 0.99;
    double radius = 0.02;
    double contactChordLength = std::sqrt((-distance + radius - environment.mRadius)
                                        * (-distance - radius + environment.mRadius)
                                        * (-distance + radius + environment.mRadius)
                                        * (distance + radius + environment.mRadius))
            / distance;
    double forceMagnitude = CellData::TURGOR_PRESSURE / distance * contactChordLength * contactChordLength * 2.0;

    assertNear(-0.99 * forceMagnitude, cellData.mForceX[touching], 1e-12, "CellData wall partial forceX");
    assertNear(0.0, cellData.mForceY[touching], 1e-12, "CellData wall partial forceY");
}

void testRenderContactCollectorKeepsBoundaryChordForDieEdge() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    environment.dieEdge = true;
    CellData cellData(1);
    RenderContactChordBuffer renderContactChords(1);
    RenderContactCollector collector;
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[cell] = 0.99;
    cellData.mPositionY[cell] = 0.0;
    cellData.mRadius[cell] = 0.02;

    cellData.handleCellToBoundaryCollisionPartial(
            cell, environment);
    collector.collect(RenderContactInput::CurrentGeometry,
            cellData,
            environment,
            renderContactChords);

    assertTrue(renderContactChords.activeChordCount(cell) == 1, "RenderContactCollector keeps a boundary chord when dieEdge disables wall force");
    assertNear(0.0, cellData.mForceX[cell], 0.0, "CellData wall partial dieEdge skips force");
    assertNear(0.0, cellData.mForceY[cell], 0.0, "CellData wall partial dieEdge skips forceY");
}

void testCellDataBoundaryCollisionPartialCreatesGlueocyteBoundaryLink() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(1);
    int glueocyte = addAliveCell(cellData, CellType::GLUEOCYTE);
    cellData.mPositionX[glueocyte] = 0.0;
    cellData.mPositionY[glueocyte] = 0.99;
    cellData.mRadius[glueocyte] = 0.02;
    cellData.mAngle[glueocyte] = 0.25;

    cellData.handleCellToBoundaryCollisionPartial(glueocyte, environment);
    cellData.handleCellToBoundaryCollisionPartial(glueocyte, environment);

    double distance = 0.99;
    double expectedAttach = environment.mRadius + CellData::MIN_CELL_RADIUS * CellData::LINK_ATTACH_DEPTH;
    assertTrue(cellData.mTopology.boundaryCount() == 1, "CellData wall partial glueocyte boundary link count");
    assertTrue(cellData.mTopology.hasBoundaryLink(glueocyte), "CellData wall partial glueocyte boundary target");
    assertNear(std::acos(-1.0) / 2.0 - 0.25, cellData.mTopology.boundaryAngleA(0), 1e-12, "CellData wall partial glueocyte angleA");
    assertNear(0.0, cellData.mTopology.boundaryAnchorX(0), 1e-12, "CellData wall partial glueocyte attach x");
    assertNear(expectedAttach, cellData.mTopology.boundaryAnchorY(0), 1e-12, "CellData wall partial glueocyte attach y");
    assertNear(0.5f, cellData.mTopology.boundaryTorsionalStiffness(0), 0.0, "CellData wall partial glueocyte stiffness");
    assertNear(0.0f, cellData.mTopology.boundaryRestLength(0), 0.0, "CellData wall partial glueocyte rest length");
    assertNear(distance, std::sqrt(cellData.mPositionX[glueocyte] * cellData.mPositionX[glueocyte] + cellData.mPositionY[glueocyte] * cellData.mPositionY[glueocyte]), 0.0, "CellData wall partial glueocyte distance setup");
}

void testCellDataBoundaryCollisionPartialAddsCiliocyteInteractionAndAllPass() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(3);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.99, 0.0, 1.0, 0.02);
    int linkedCiliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.0, 0.99, 1.0, 0.02);
    int deadCiliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, -0.99, 0.0, 1.0, 0.02);
    cellData.addLink(
            linkedCiliocyte,
            CellData::BOUNDARY_CONNECTION_INDEX,
            CellLinkCreationReason::UNSPECIFIED,
            0.0,
            0.0,
            0.0f,
            0.0f);
    cellData.markCellAsDead(deadCiliocyte);

    cellData.handleAllCellToBoundaryCollisionsPartial(
            environment);

    assertTrue(cellData.mCiliocyteInteractionCount == 1, "CellData wall partial all pass ciliocyte interaction count");
    assertTrue(cellData.mCiliocyteInteractionCellIndex1[0] == ciliocyte, "CellData wall partial all pass ciliocyte first index");
    assertTrue(cellData.mCiliocyteInteractionCellIndex2[0] == CellData::BOUNDARY_CONNECTION_INDEX, "CellData wall partial all pass boundary target");
}

void testCellDataCollectAllCollisionInteractionsPartialCollectsCellAndBoundaryCiliocyteContacts() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(3);
    int cellCiliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.0, 0.0, 1.0, 0.02);
    int other = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.03, 0.0, 1.0, 0.02);
    int wallCiliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.0, 0.99, 1.0, 0.02);
    cellData.mVelocityX[other] = 0.5;
    addCollisionContact(cellData, cellCiliocyte, other);
    cellData.mCiliocyteInteractionCount = 1;
    cellData.mCiliocyteInteractionCellIndex1[0] = 99;
    cellData.mCiliocyteInteractionCellIndex2[0] = 99;

    cellData.collectAllCollisionInteractionsPartial(
            0.01, environment);

    assertTrue(cellData.mCiliocyteInteractionCount == 2, "CellData collect partial resets and collects two ciliocyte contacts");
    assertTrue(cellData.mCiliocyteInteractionCellIndex1[0] == cellCiliocyte, "CellData collect partial first contact source");
    assertTrue(cellData.mCiliocyteInteractionCellIndex2[0] == other, "CellData collect partial first contact target");
    assertTrue(cellData.mCiliocyteInteractionCellIndex1[1] == wallCiliocyte, "CellData collect partial wall contact source");
    assertTrue(cellData.mCiliocyteInteractionCellIndex2[1] == CellData::BOUNDARY_CONNECTION_INDEX, "CellData collect partial wall contact target");
    assertNear(0.0, cellData.mVelocityY[wallCiliocyte], 0.0, "CellData collect partial does not apply ciliocyte impulses");
}

void testCellDataUpdateAllCellInteractionsPartialKeepsJavaStageOrder() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    environment.mIsPreviewMode = true;
    environment.mViscosity = 0.0;
    environment.mGravity = 0.0;
    environment.dieEdge = false;

    CellData cellData(1);
    int glueocyte = addCiliocyteTestCell(cellData, CellType::GLUEOCYTE, 0.99, 0.0, 1.0, 0.02);

    cellData.updateAllCellInteractionsPartial(0.02, environment);

    double distance = 0.99;
    double radius = 0.02;
    double contactChordLength = std::sqrt((-distance + radius - environment.mRadius)
                                        * (-distance - radius + environment.mRadius)
                                        * (-distance + radius + environment.mRadius)
                                        * (distance + radius + environment.mRadius))
            / distance;
    double forceMagnitude = CellData::TURGOR_PRESSURE / distance * contactChordLength * contactChordLength * 2.0;
    double expectedWallForceX = -0.99 * forceMagnitude;

    assertTrue(cellData.mTopology.hasBoundaryLink(glueocyte), "CellData updateAllCellInteractionsPartial creates topology boundary link after link stage");
    assertNear(expectedWallForceX, cellData.mForceX[glueocyte], 1e-12, "CellData updateAllCellInteractionsPartial does not apply new boundary link same pass");
    assertNear(0.0, cellData.mForceY[glueocyte], 1e-12, "CellData updateAllCellInteractionsPartial wall forceY");

    cellData.resetCellForces();
    cellData.updateAllCellLinkInteractions(0.02);

    assertTrue(cellData.mForceX[glueocyte] > 0.0,
            "Wall-created Glueocyte link first contributes at the next ordered link stage");
}

void testCellDataBuoyocyteParametricDensityReadsItsCurrentLinkCount() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    environment.mIsPreviewMode = false;
    environment.mViscosity = 0.0;
    environment.mDensity = 0.0;
    environment.mDensityGradient = 0.0;
    environment.syncDensityFieldFromLegacyGradient();
    environment.mGravity = 2.0;
    environment.syncGravityFieldFromLegacyGravity();

    CellData cellData(1);
    const int buoyocyte = addCiliocyteTestCell(
            cellData, CellType::BUOYOCYTE, 0.0, 0.0, 1.0, 0.02);
    cellData.editActiveGene(buoyocyte, [](GeneBuilder& densityGene) {
        densityGene.setParametricFormulaType(Gene::DENSITY_VALUE_INDEX, 1);
        densityGene.setParametricVariableA(Gene::DENSITY_VALUE_INDEX, 1.0f);
        densityGene.setParametricVariableB(Gene::DENSITY_VALUE_INDEX, 0.0f);
        densityGene.setParametricSignalChannel(
                Gene::DENSITY_VALUE_INDEX,
                CellData::SIGNAL_SUBSTANCE_COUNT + 3);
    });
    cellData.addLink(
            buoyocyte,
            CellData::BOUNDARY_CONNECTION_INDEX,
            CellLinkCreationReason::UNSPECIFIED,
            0.0,
            0.0,
            0.0f,
            0.0f);

    assertTrue(cellData.mTopology.linkCount(buoyocyte) == 1,
            "Buoyocyte fixture owns one boundary link before force evaluation");
    const double expectedDensity = 3.0 / CellData::MAX_NEIGHBORS;
    assertNear(expectedDensity,
            cellData.getValue(buoyocyte, Gene::DENSITY_VALUE_INDEX, 0),
            1e-6,
            "Buoyocyte density parameter reads the current own-link count");

    cellData.updateAllCellForces(0.02, environment);

    assertNear(-cellData.mMass[buoyocyte] * expectedDensity
                    * environment.mGravity,
            cellData.mForceY[buoyocyte],
            1e-6,
            "Buoyocyte force stage consumes the current link-count-driven density");
}

void testCellDataApplyCollectedCiliocyteInteractionsPartialUpdatesVelocitiesExplicitly() {
    Environment environment = Environment::constructDefaultEnvironment(8, 0, 1.0);
    CellData cellData(1);
    int ciliocyte = addCiliocyteTestCell(cellData, CellType::CILIOCYTE, 0.99, 0.0, 1.0, 0.02);
    cellData.mForceX[ciliocyte] = 0.3;
    cellData.mForceY[ciliocyte] = 0.4;
    cellData.mTorque[ciliocyte] = 0.0;
    cellData.addCiliocyteInteraction(ciliocyte, CellData::BOUNDARY_CONNECTION_INDEX);

    cellData.applyCollectedCiliocyteInteractionsPartial(environment);

    assertNear(0.0, cellData.mVelocityX[ciliocyte], 1e-12, "CellData apply collected ciliocyte velocityX");
    assertTrue(cellData.mVelocityY[ciliocyte] < 0.0, "CellData apply collected ciliocyte velocityY");
    assertTrue(cellData.mAngularVelocity[ciliocyte] < 0.0, "CellData apply collected ciliocyte angular velocity");
    assertTrue(cellData.mCiliocyteInteractionCount == 1, "CellData apply collected keeps buffer count until next collect");
}

void testCellDataUpdateCellVelocitiesAppliesForceWithoutDrag() {
    Environment environment = Environment::constructDefaultEnvironment(10, 10, 1.0);
    environment.mIsPreviewMode = true;
    environment.mViscosity = 0.0;
    environment.mGravity = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mFrictionStatic = 0.0;

    CellData cellData(1);
    int cell = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 2.0, 0.02);
    cellData.mForceX[cell] = 0.6;
    cellData.mForceY[cell] = -0.2;
    cellData.mTorque[cell] = 0.004;

    cellData.updateCellVelocities(environment, 0.5);

    double inertia = 2.0 * 0.02 * 0.02 * 0.7;
    assertNear(0.15, cellData.mVelocityX[cell], 1e-12, "CellData updateCellVelocities velocityX");
    assertNear(-0.05, cellData.mVelocityY[cell], 1e-12, "CellData updateCellVelocities velocityY");
    assertNear(0.004 * 0.5 / inertia, cellData.mAngularVelocity[cell], 1e-12, "CellData updateCellVelocities angular velocity");
    assertNear(cellData.mVelocityX[cell], cellData.mForceX[cell], 1e-12, "CellData updateCellVelocities forceX scratch");
    assertNear(cellData.mVelocityY[cell], cellData.mForceY[cell], 1e-12, "CellData updateCellVelocities forceY scratch");
    assertNear(cellData.mAngularVelocity[cell], cellData.mTorque[cell], 1e-12, "CellData updateCellVelocities torque scratch");
}

void testCellDataUpdateCellForcesViscosityAndGravity() {
    Environment environment = Environment::constructDefaultEnvironment(10, 10, 1.0);
    environment.mIsPreviewMode = false;
    environment.mViscosity = 2.0;
    environment.mDensity = 0.25;
    environment.mDensityGradient = 0.5;
    environment.syncDensityFieldFromLegacyGradient();
    environment.mGravity = 3.0;
    environment.syncGravityFieldFromLegacyGravity();

    CellData cellData(2);
    int phagocyte = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.2, 1.5, 0.03);
    cellData.mVelocityX[phagocyte] = 0.4;
    cellData.mVelocityY[phagocyte] = -0.1;
    cellData.mAngularVelocity[phagocyte] = 0.2;

    cellData.updateCellForces(phagocyte, 0.02, environment);

    double totalViscosity = 0.03 * 2.0;
    double effectiveViscosity = totalViscosity * (1.0 - 1.0 * 0.02 * totalViscosity * 0.5 / 1.5);
    double density = 0.25 + 0.5 * 0.2 / 1.0;
    double expectedGravityForceY = -(1.5 * (0.0 - density) * 3.0);

    assertNear(-0.4 * effectiveViscosity, cellData.mForceX[phagocyte], 1e-12, "CellData updateCellForces viscosity forceX");
    assertNear(0.1 * effectiveViscosity + expectedGravityForceY, cellData.mForceY[phagocyte], 1e-12, "CellData updateCellForces forceY");
    assertNear(-0.2 * 0.03 * 0.03 * 0.03 * 2.0 * 0.5, cellData.mTorque[phagocyte], 1e-12, "CellData updateCellForces torque viscosity");

    int buoyocyte = addCiliocyteTestCell(cellData, CellType::BUOYOCYTE, 0.0, -0.2, 2.0, 0.02);
    setConstantValue(cellData, buoyocyte, Gene::DENSITY_VALUE_INDEX, 1.0f);
    cellData.updateCellForces(buoyocyte, 0.02, environment);

    double buoyDensity = environment.getDensity(cellData.mPositionX[buoyocyte], cellData.mPositionY[buoyocyte]);
    double buoyCurrentDensity = cellData.getValue(buoyocyte, Gene::DENSITY_VALUE_INDEX, 0) - buoyDensity;
    assertNear(-(2.0 * buoyCurrentDensity * environment.mGravity), cellData.mForceY[buoyocyte], 1e-12, "CellData updateCellForces buoyocyte gravity");

    Environment radialGravity = Environment::constructDefaultEnvironment(10, 10, 1.0);
    radialGravity.mIsPreviewMode = false;
    radialGravity.mDensity = 1.0;
    radialGravity.mDensityGradient = 0.0;
    radialGravity.syncDensityFieldFromLegacyGradient();
    radialGravity.mGravityFieldType = Environment::VECTOR_FIELD_RADIAL_INWARD;
    radialGravity.mGravityFieldStrength = 2.0;
    radialGravity.syncLegacyGravityFromVectorField();

    CellData radialCellData(1);
    int radialBuoyocyte = addCiliocyteTestCell(radialCellData, CellType::BUOYOCYTE, 0.5, 0.0, 1.0, 0.02);
    setConstantValue(
            radialCellData,
            radialBuoyocyte,
            Gene::DENSITY_VALUE_INDEX,
            2.0f);
    radialCellData.updateCellForces(radialBuoyocyte, 0.02, radialGravity);
    double radialCurrentDensity = radialCellData.getValue(radialBuoyocyte, Gene::DENSITY_VALUE_INDEX, 0)
            - radialGravity.getDensity(0.5, 0.0);
    assertNear(-radialCellData.mMass[radialBuoyocyte] * radialCurrentDensity * radialGravity.mGravityFieldStrength,
            radialCellData.mForceX[radialBuoyocyte],
            1e-12,
            "CellData radial inward gravity forceX");
    assertNear(0.0, radialCellData.mForceY[radialBuoyocyte], 1e-12, "CellData radial inward gravity forceY");
}

void testCellDataUpdateAllCellForcesProcessesLiveCellsAndSkipsDeadCells() {
    Environment environment = Environment::constructDefaultEnvironment(10, 10, 1.0);
    environment.mIsPreviewMode = false;
    environment.mViscosity = 1.5;
    environment.mDensity = 0.25;
    environment.mDensityGradient = 0.0;
    environment.syncDensityFieldFromLegacyGradient();
    environment.mGravity = 2.0;
    environment.syncGravityFieldFromLegacyGravity();

    CellData cellData(3);
    int phagocyte = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 1.0, 0.02);
    int buoyocyte = addCiliocyteTestCell(cellData, CellType::BUOYOCYTE, 0.0, 0.0, 2.0, 0.02);
    int dead = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 1.0, 0.02);
    setConstantValue(cellData, buoyocyte, Gene::DENSITY_VALUE_INDEX, 1.0f);
    cellData.mVelocityX[phagocyte] = 0.2;
    cellData.mVelocityY[phagocyte] = -0.3;
    cellData.mVelocityX[dead] = 10.0;
    cellData.mForceX[dead] = 4.0;
    cellData.mForceY[dead] = 5.0;
    cellData.mTorque[dead] = 6.0;
    cellData.markCellAsDead(dead);

    cellData.updateAllCellForces(0.02, environment);

    double totalViscosity = 0.02 * 1.5;
    double effectiveViscosity = totalViscosity * (1.0 - 1.0 * 0.02 * totalViscosity * 0.5 / 1.0);
    double expectedPhagocyteGravityForceY = -(1.0 * (0.0 - 0.25) * 2.0);
    double buoyCurrentDensity = cellData.getValue(buoyocyte, Gene::DENSITY_VALUE_INDEX, 0) - 0.25;

    assertNear(-0.2 * effectiveViscosity, cellData.mForceX[phagocyte], 1e-12, "CellData updateAllCellForces phagocyte forceX");
    assertNear(0.3 * effectiveViscosity + expectedPhagocyteGravityForceY, cellData.mForceY[phagocyte], 1e-12, "CellData updateAllCellForces phagocyte forceY");
    assertNear(-(2.0 * buoyCurrentDensity * 2.0), cellData.mForceY[buoyocyte], 1e-12, "CellData updateAllCellForces buoyocyte forceY");
    assertNear(4.0, cellData.mForceX[dead], 0.0, "CellData updateAllCellForces dead forceX unchanged");
    assertNear(5.0, cellData.mForceY[dead], 0.0, "CellData updateAllCellForces dead forceY unchanged");
    assertNear(6.0, cellData.mTorque[dead], 0.0, "CellData updateAllCellForces dead torque unchanged");
}

void testCellDataApplyCellFertilizationTransfersStateAndMixesGenome() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mRandom.setSeed(12345);
    SimulationRandom expectedRandom(12345);
    CellData cellData(2);
    int egg = addAliveCell(cellData, CellType::GAMETE);
    int sperm = addAliveCell(cellData, CellType::GAMETE);
    cellData.mMass[egg] = CellData::MAX_MASS - 0.01;
    cellData.mMass[sperm] = 0.05;
    cellData.mNitrates[egg] = 0.75;
    cellData.mNitrates[sperm] = 0.5;
    cellData.mAge[egg] = 2.0;
    cellData.mOriginTag[egg] = SimulationWorld::LEVEL_CELL_ORIGIN;
    cellData.mOriginTag[sperm] = SimulationWorld::RANDOM_CELL_ORIGIN;
    cellularity::GenomeData eggGenome = cellData.materializeGenome(egg);
    cellularity::GenomeData spermGenome = cellData.materializeGenome(sperm);
    GeneBuilder eggActiveGene(eggGenome[0]);
    eggActiveGene.setIntegerValue(Gene::TELOMERASE_LENGTH_INDEX, 2);
    eggActiveGene.setVirusFromGeneIndex(GeneIndex(3));
    for (int geneIndex = 0; geneIndex < CellData::MAX_GENES_COUNT; geneIndex++) {
        GeneBuilder eggGene(eggGenome[geneIndex]);
        eggGene.setCellType(CellType::PHAGOCYTE);
        eggGene.setColor(0, 0.1f);
        GeneBuilder spermGene(spermGenome[geneIndex]);
        spermGene.setCellType(CellType::STEREOCYTE);
        spermGene.setColor(0, 0.9f);
    }
    assertTrue(cellData.replaceGenome(egg, eggGenome, GeneIndex(0)),
            "Fertilization egg Genome setup");
    assertTrue(cellData.replaceGenome(sperm, spermGenome, GeneIndex(0)),
            "Fertilization sperm Genome setup");

    cellData.applyCellFertilization(sperm, egg, environment);

    assertTrue(cellData.mIsDead[sperm], "CellData applyCellFertilization marks sperm dead");
    assertNear(0.0, cellData.mAge[egg], 0.0, "CellData applyCellFertilization resets egg age");
    assertNear(CellData::MAX_MASS, cellData.mMass[egg], 0.0, "CellData applyCellFertilization caps egg mass");
    assertNear(0.0, cellData.mMass[sperm], 0.0, "CellData applyCellFertilization clears sperm mass");
    assertNear(CellData::MAX_NITRATES, cellData.mNitrates[egg], 0.0, "CellData applyCellFertilization caps egg nitrates");
    assertNear(0.0, cellData.mNitrates[sperm], 0.0, "CellData applyCellFertilization clears sperm nitrates");
    assertTrue(cellData.mOriginTag[egg] == SimulationWorld::HYBRID_CELL_ORIGIN, "CellData applyCellFertilization hybrid origin");
    assertTrue(cellData.mTelomeresCount[egg] == Gene::TELOMERASE_LENGTHS[2], "CellData applyCellFertilization telomeres from egg active gene");
    assertTrue(cellData.activeGeneIndex(egg) == GeneIndex(3), "CellData applyCellFertilization gene index from egg virus-from");

    for (int geneIndex = 0; geneIndex < CellData::MAX_GENES_COUNT; geneIndex++) {
        bool copied = expectedRandom.nextInt(2) == 1;
        CellType expectedType = copied ? CellType::STEREOCYTE : CellType::PHAGOCYTE;
        float expectedColor = copied ? 0.9f : 0.1f;
        const GeneView gene = cellData.getGeneView(
                egg,
                GeneIndex(static_cast<GeneIndex::Value>(geneIndex)));
        assertTrue(gene.cellType() == expectedType, "CellData applyCellFertilization genome type mix");
        assertNear(expectedColor, gene.color(0), 0.0, "CellData applyCellFertilization genome color mix");
    }
}

void testCellDataResolveFertilizationIntentsRequiresMutualTargets() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    CellData cellData(3);
    int first = addAliveCell(cellData, CellType::GAMETE);
    int second = addAliveCell(cellData, CellType::GAMETE);
    int third = addAliveCell(cellData, CellType::GAMETE);
    cellData.mRadius[first] = 0.03;
    cellData.mRadius[second] = 0.02;
    cellData.mRadius[third] = 0.02;
    cellData.mMass[first] = 0.1;
    cellData.mMass[second] = 0.1;
    cellData.mMass[third] = 0.1;
    cellData.mFertilizationTarget[first] = second;
    cellData.mFertilizationTarget[second] = third;
    cellData.mFertilizationTarget[third] = -1;

    cellData.resolveFertilizationIntents(environment);

    assertTrue(!cellData.mIsDead[first], "CellData resolveFertilizationIntents non-mutual first alive");
    assertTrue(!cellData.mIsDead[second], "CellData resolveFertilizationIntents non-mutual second alive");
    assertTrue(!cellData.mIsDead[third], "CellData resolveFertilizationIntents non-mutual third alive");
    assertTrue(cellData.mFertilizationTarget[first] == second, "CellData resolveFertilizationIntents keeps non-mutual first target");
    assertTrue(cellData.mFertilizationTarget[second] == third, "CellData resolveFertilizationIntents keeps non-mutual second target");
}

void testCellDataResolveFertilizationIntentsFertilizesMutualPairOnce() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mRandom.setSeed(4417);
    SimulationRandom expectedRandom;
    expectedRandom.setSeed(4417);

    CellData cellData(2);
    int larger = addAliveCell(cellData, CellType::GAMETE);
    int smaller = addAliveCell(cellData, CellType::GAMETE);
    cellData.mRadius[larger] = 0.03;
    cellData.mRadius[smaller] = 0.02;
    cellData.mMass[larger] = 0.1;
    cellData.mMass[smaller] = 0.05;
    cellData.mNitrates[larger] = 0.2;
    cellData.mNitrates[smaller] = 0.3;
    cellData.mFertilizationTarget[larger] = smaller;
    cellData.mFertilizationTarget[smaller] = larger;
    bool largerIsSperm = expectedRandom.nextInt(2) == 0;
    int expectedSperm = largerIsSperm ? larger : smaller;
    int expectedEgg = largerIsSperm ? smaller : larger;

    cellData.resolveFertilizationIntents(environment);

    assertTrue(cellData.mIsDead[expectedSperm], "CellData resolveFertilizationIntents random sperm");
    assertTrue(!cellData.mIsDead[expectedEgg], "CellData resolveFertilizationIntents random egg");
    assertNear(0.15, cellData.mMass[expectedEgg], 1e-12, "CellData resolveFertilizationIntents transfers mass once");
    assertNear(0.0, cellData.mMass[expectedSperm], 0.0, "CellData resolveFertilizationIntents clears sperm mass");
    assertNear(0.5, cellData.mNitrates[expectedEgg], 1e-12, "CellData resolveFertilizationIntents transfers nitrates once");
    assertTrue(cellData.mFertilizationTarget[larger] == -1, "CellData resolveFertilizationIntents clears egg target");
    assertTrue(cellData.mFertilizationTarget[smaller] == -1, "CellData resolveFertilizationIntents clears sperm target");
}

void testCellDataResolveFertilizationIntentsUsesRandomEggForUnequalGametes() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mRandom.setSeed(1902);
    SimulationRandom expectedRandom;
    expectedRandom.setSeed(1902);

    CellData cellData(2);
    int larger = addAliveCell(cellData, CellType::GAMETE);
    int smaller = addAliveCell(cellData, CellType::GAMETE);
    cellData.mRadius[larger] = 0.03;
    cellData.mRadius[smaller] = 0.02;
    cellData.mPositionX[larger] = 0.0;
    cellData.mPositionY[larger] = 0.0;
    cellData.mPositionX[smaller] = 0.03;
    cellData.mPositionY[smaller] = 0.0;
    cellData.mMass[larger] = 0.1;
    cellData.mMass[smaller] = 0.05;
    cellData.mFertilizationTarget[larger] = smaller;
    cellData.mFertilizationTarget[smaller] = larger;

    bool largerIsSperm = expectedRandom.nextInt(2) == 0;
    int expectedSperm = largerIsSperm ? larger : smaller;
    int expectedEgg = largerIsSperm ? smaller : larger;

    cellData.resolveFertilizationIntents(environment);

    assertTrue(cellData.mIsDead[expectedSperm], "CellData resolveFertilizationIntents random unequal sperm");
    assertTrue(!cellData.mIsDead[expectedEgg], "CellData resolveFertilizationIntents random unequal egg");
    assertNear(0.15, cellData.mMass[expectedEgg], 1e-12, "CellData resolveFertilizationIntents random unequal mass transfer");
}

void testCellDataUpdateCellVelocitiesStaticFrictionKeepsCellStuck() {
    Environment environment = Environment::constructDefaultEnvironment(10, 10, 1.0);
    environment.mFrictionStatic = 10.0;
    environment.mFrictionDynamic = 0.0;

    CellData cellData(1);
    int cell = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 2.0, 0.02);
    cellData.mIsStuck[cell] = true;
    cellData.mForceX[cell] = 0.01;
    cellData.mForceY[cell] = 0.0;
    cellData.mTorque[cell] = 0.0;

    cellData.updateCellVelocities(environment, 0.02);

    assertTrue(cellData.mIsStuck[cell], "CellData updateCellVelocities remains stuck under static friction");
    assertNear(0.0, cellData.mVelocityX[cell], 0.0, "CellData updateCellVelocities stuck velocityX");
    assertNear(0.0, cellData.mVelocityY[cell], 0.0, "CellData updateCellVelocities stuck velocityY");
    assertNear(0.0, cellData.mForceX[cell], 0.0, "CellData updateCellVelocities stuck force scratch");
}

void testCellDataUpdateCellVelocitiesDynamicFrictionMatchesFormula() {
    Environment environment = Environment::constructDefaultEnvironment(10, 10, 1.0);
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.5;

    CellData cellData(1);
    int cell = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, 2.0, 0.02);
    cellData.mVelocityX[cell] = 0.3;
    cellData.mVelocityY[cell] = 0.4;
    cellData.mAngularVelocity[cell] = 2.0;
    cellData.mForceX[cell] = 0.2;
    cellData.mForceY[cell] = -0.1;
    cellData.mTorque[cell] = 0.001;

    double deltaTime = 0.02;
    double cellMass = 2.0;
    double radius = 0.02;
    double inertia = cellMass * radius * radius * 0.7;
    double deltaForceX = 0.2 / cellMass * deltaTime;
    double deltaForceY = -0.1 / cellMass * deltaTime;
    double deltaTorque = 0.001 * deltaTime / inertia;
    double newVelocityX = 0.3 + deltaForceX;
    double newVelocityY = 0.4 + deltaForceY;
    double newAngularVelocity = 2.0 + deltaTorque;
    double velocityMagnitude = std::sqrt(newVelocityX * newVelocityX + newVelocityY * newVelocityY);
    double epsilon = velocityMagnitude / (radius * std::abs(2.0));
    double epsilon2 = epsilon * epsilon;
    double forceFactor = (epsilon2 + 0.1 * epsilon) / (epsilon2 + 0.1 * epsilon + 0.15);
    double torqueFactor = 0.65 * (1.0 - (epsilon2 + epsilon) / (epsilon + epsilon2 + 5.0));
    double weight = cellMass;
    double frictionVelocity = forceFactor * environment.mFrictionDynamic * weight / cellMass * deltaTime;
    double angularFrictionVelocity = torqueFactor * environment.mFrictionDynamic * cellMass / inertia * deltaTime;

    cellData.updateCellVelocities(environment, deltaTime);

    assertNear(newVelocityX - newVelocityX * frictionVelocity / velocityMagnitude, cellData.mVelocityX[cell], 1e-12, "CellData updateCellVelocities dynamic friction velocityX");
    assertNear(newVelocityY - newVelocityY * frictionVelocity / velocityMagnitude, cellData.mVelocityY[cell], 1e-12, "CellData updateCellVelocities dynamic friction velocityY");
    assertNear(newAngularVelocity - angularFrictionVelocity, cellData.mAngularVelocity[cell], 1e-12, "CellData updateCellVelocities dynamic friction angular velocity");
}

void testCellDataUpdateCellsKinematicsMovesAndWrapsAngle() {
    CellData cellData(3);
    int cell = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 1.0, -2.0, CellData::INITIAL_MASS, 0.02);
    int linkedA = addAliveCell(cellData, CellType::PHAGOCYTE);
    int linkedB = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mAge[cell] = 1.0;
    cellData.mVelocityX[cell] = 0.3;
    cellData.mVelocityY[cell] = -0.4;
    cellData.mAngularVelocity[cell] = 10.0;
    cellData.mAngularVelocityCytoplasm[cell] = -2.0;
    cellData.mAngle[cell] = std::acos(-1.0) - 0.05;
    cellData.mAngleCytoplasm[cell] = 0.2;
    cellData.mTopology.addCellLink(
            cell, linkedA, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    cellData.mTopology.addCellLink(
            cell, linkedB, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    cellData.mOldLinkCount[cell] = 1;

    cellData.updateCellsKinematics(cell, 0.02);

    double pi = std::acos(-1.0);
    assertNear(1.0 + 0.3 * 0.02, cellData.mPositionX[cell], 1e-12, "CellData updateCellsKinematics positionX");
    assertNear(-2.0 - 0.4 * 0.02, cellData.mPositionY[cell], 1e-12, "CellData updateCellsKinematics positionY");
    assertNear(pi - 0.05 + 10.0 * 0.02 - 2.0 * pi, cellData.mAngle[cell], 1e-12, "CellData updateCellsKinematics angle wrap positive");
    assertNear(0.2 - 2.0 * 0.02, cellData.mAngleCytoplasm[cell], 1e-12, "CellData updateCellsKinematics cytoplasm angle");
    assertTrue(cellData.mOldLinkCount[cell] == 2, "CellData updateCellsKinematics stores old link count");
}

void testCellDataUpdateCellsKinematicsYoungCellMovesButSkipsRadiusRelaxation() {
    CellData cellData(1);
    int cell = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 1.0, 2.0, CellData::INITIAL_MASS, 0.02);
    cellData.mAge[cell] = 0.09;
    cellData.mVelocityX[cell] = 1.0;
    cellData.mVelocityY[cell] = 1.0;
    cellData.mAngularVelocity[cell] = 1.0;
    cellData.mAngularVelocityCytoplasm[cell] = 1.0;
    cellData.mAngle[cell] = 0.5;
    cellData.mAngleCytoplasm[cell] = 0.25;
    double radiusBefore = cellData.mRadius[cell];

    cellData.updateCellsKinematics(cell, 0.02);

    assertNear(1.0 + 1.0 * 0.02, cellData.mPositionX[cell], 1e-12, "CellData updateCellsKinematics young positionX");
    assertNear(2.0 + 1.0 * 0.02, cellData.mPositionY[cell], 1e-12, "CellData updateCellsKinematics young positionY");
    assertNear(0.5 + 1.0 * 0.02, cellData.mAngle[cell], 1e-12, "CellData updateCellsKinematics young angle");
    assertNear(0.25 + 1.0 * 0.02, cellData.mAngleCytoplasm[cell], 1e-12, "CellData updateCellsKinematics young cytoplasm angle");
    assertNear(radiusBefore, cellData.mRadius[cell], 0.0, "CellData updateCellsKinematics young radius");
}

void testCellDataUpdateCellsKinematicsStuckCellStillMovesLikeJava() {
    CellData cellData(1);
    int cell = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 1.0, 2.0, CellData::INITIAL_MASS, 0.02);
    cellData.mAge[cell] = 1.0;
    cellData.mIsStuck[cell] = true;
    cellData.mVelocityX[cell] = 1.0;
    cellData.mVelocityY[cell] = 1.0;
    cellData.mAngularVelocity[cell] = 1.0;
    cellData.mAngularVelocityCytoplasm[cell] = 2.0;
    cellData.mAngle[cell] = 0.5;
    cellData.mAngleCytoplasm[cell] = 0.25;
    double radiusBefore = cellData.mRadius[cell];
    double expectedRadius = radiusBefore + (cellData.mMass[cell] / CellData::MASS_PER_AREA / radiusBefore - radiusBefore) * 0.5 * CellData::RADIUS_CHANGE_SPEED * 0.02;

    cellData.updateCellsKinematics(cell, 0.02);

    assertNear(1.0 + 1.0 * 0.02, cellData.mPositionX[cell], 1e-12, "CellData updateCellsKinematics stuck positionX");
    assertNear(2.0 + 1.0 * 0.02, cellData.mPositionY[cell], 1e-12, "CellData updateCellsKinematics stuck positionY");
    assertNear(0.5 + 1.0 * 0.02, cellData.mAngle[cell], 1e-12, "CellData updateCellsKinematics stuck angle");
    assertNear(0.25 + 2.0 * 0.02, cellData.mAngleCytoplasm[cell], 1e-12, "CellData updateCellsKinematics stuck cytoplasm angle");
    assertNear(expectedRadius, cellData.mRadius[cell], 1e-12, "CellData updateCellsKinematics stuck radius relaxes");
}

void testCellDataUpdateCellsKinematicsRadiusClampAndDeathChecks() {
    CellData radiusData(1);
    int radiusCell = addCiliocyteTestCell(radiusData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::MAX_MASS * 10.0, CellData::MAX_CELL_RADIUS);
    radiusData.mAge[radiusCell] = 1.0;
    radiusData.updateCellsKinematics(radiusCell, 0.02);
    assertNear(CellData::MAX_CELL_RADIUS, radiusData.mRadius[radiusCell], 0.0, "CellData updateCellsKinematics radius clamps to max");
    assertTrue(!radiusData.mIsDead[radiusCell], "CellData updateCellsKinematics max radius clamp keeps cell alive");

    CellData lowMassData(1);
    int lowMassCell = addCiliocyteTestCell(lowMassData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::MIN_MASS * 0.9, CellData::MIN_CELL_RADIUS);
    lowMassData.mAge[lowMassCell] = 1.0;
    lowMassData.updateCellsKinematics(lowMassCell, 0.02);
    assertTrue(lowMassData.mIsDead[lowMassCell], "CellData updateCellsKinematics kills low mass cell");

    CellData lowRadiusData(1);
    int lowRadiusCell = addCiliocyteTestCell(lowRadiusData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, CellData::MIN_CELL_RADIUS * 0.9);
    lowRadiusData.mAge[lowRadiusCell] = 0.0;
    lowRadiusData.updateCellsKinematics(lowRadiusCell, 0.02);
    assertTrue(lowRadiusData.mIsDead[lowRadiusCell], "CellData updateCellsKinematics kills low radius cell");

    CellData toxData(1);
    int toxCell = addCiliocyteTestCell(toxData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    toxData.mToxication[toxCell] = 1.01f;
    toxData.updateCellsKinematics(toxCell, 0.02);
    assertTrue(toxData.mIsDead[toxCell], "CellData updateCellsKinematics kills toxicated cell");
}

void testCellDataUpdateAllCellsKinematicsMovesDeadCellsLikeJava() {
    CellData cellData(2);
    int live = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    int dead = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 1.0, 1.0, CellData::INITIAL_MASS, 0.02);
    cellData.mAge[live] = 1.0;
    cellData.mAge[dead] = 1.0;
    cellData.mVelocityX[live] = 1.0;
    cellData.mVelocityX[dead] = 1.0;
    cellData.markCellAsDead(dead);

    cellData.updateCellsKinematics(0.02);

    assertNear(0.02, cellData.mPositionX[live], 1e-12, "CellData updateAllCellsKinematics live moves");
    assertNear(1.0 + 1.0 * 0.02, cellData.mPositionX[dead], 1e-12, "CellData updateAllCellsKinematics dead moves");
}

void testCellDataResetCellForcesMatchesJavaDeadCellBehavior() {
    CellData cellData(2);
    int live = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    int dead = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);

    cellData.mForceX[live] = 1.0;
    cellData.mForceY[live] = 2.0;
    cellData.mTorque[live] = 3.0;
    cellData.mForceZ[live] = 4.0;
    cellData.mPositionZ[live] = 0.5;

    cellData.mForceX[dead] = 5.0;
    cellData.mForceY[dead] = 6.0;
    cellData.mTorque[dead] = 7.0;
    cellData.mForceZ[dead] = 8.0;
    cellData.mPositionZ[dead] = 0.25;
    cellData.markCellAsDead(dead);

    cellData.resetCellForces();

    assertNear(0.0, cellData.mForceX[live], 0.0, "CellData resetCellForces live forceX");
    assertNear(0.0, cellData.mForceY[live], 0.0, "CellData resetCellForces live forceY");
    assertNear(0.0, cellData.mTorque[live], 0.0, "CellData resetCellForces live torque");
    assertNear(-0.5 * 0.3f, cellData.mForceZ[live], 1e-12, "CellData resetCellForces live forceZ");

    assertNear(5.0, cellData.mForceX[dead], 0.0, "CellData resetCellForces dead keeps forceX");
    assertNear(6.0, cellData.mForceY[dead], 0.0, "CellData resetCellForces dead keeps forceY");
    assertNear(0.0, cellData.mTorque[dead], 0.0, "CellData resetCellForces dead resets torque only");
    assertNear(8.0, cellData.mForceZ[dead], 0.0, "CellData resetCellForces dead keeps forceZ");
}

void testCellDataUpdateFlagellocytesForcePreviewOnlyAnimates() {
    Environment environment = Environment::constructDefaultEnvironment(10, 10, 1.0);
    environment.mIsPreviewMode = true;
    CellData cellData(2);
    int flagellocyte = addCiliocyteTestCell(cellData, CellType::FLAGELLOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    int phagocyte = addCiliocyteTestCell(cellData, CellType::PHAGOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    setConstantValue(cellData, flagellocyte, Gene::SWIM_FORCE_VALUE_INDEX, 1.0f);
    cellData.mForceX[flagellocyte] = 0.25;
    cellData.mForceY[flagellocyte] = -0.5;
    cellData.mSwimAnimationTime[flagellocyte] = 0.1f;
    cellData.mSwimAnimationTime[phagocyte] = 0.2f;

    cellData.updateFlagellocytesForce(0.5, environment);

    float animationSpeedFactor = 45.0f / 0.4f;
    float period = static_cast<float>(20.0 * std::acos(-1.0)) / animationSpeedFactor;
    float expectedAnimation = 0.1f + CellData::MAX_SWIM_FORCE * 0.5f;
    while (expectedAnimation * animationSpeedFactor > static_cast<float>(20.0 * std::acos(-1.0))) {
        expectedAnimation -= period;
    }
    assertNear(expectedAnimation, cellData.mSwimAnimationTime[flagellocyte], 1e-6, "CellData updateFlagellocytesForce preview animation");
    assertNear(0.25, cellData.mForceX[flagellocyte], 0.0, "CellData updateFlagellocytesForce preview forceX unchanged");
    assertNear(-0.5, cellData.mForceY[flagellocyte], 0.0, "CellData updateFlagellocytesForce preview forceY unchanged");
    assertNear(0.2f, cellData.mSwimAnimationTime[phagocyte], 0.0, "CellData updateFlagellocytesForce skips non-flagellocyte");
}

void testCellDataUpdateFlagellocytesForceAppliesDirectionalForce() {
    Environment environment = Environment::constructDefaultEnvironment(10, 10, 1.0);
    environment.mIsPreviewMode = false;
    CellData cellData(1);
    int flagellocyte = addCiliocyteTestCell(cellData, CellType::FLAGELLOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    setConstantValue(cellData, flagellocyte, Gene::SWIM_FORCE_VALUE_INDEX, 1.0f);
    cellData.mAngle[flagellocyte] = std::acos(-1.0) / 3.0;
    cellData.mForceX[flagellocyte] = 0.1;
    cellData.mForceY[flagellocyte] = 0.2;

    cellData.updateFlagellocytesForce(0.25, environment);

    assertNear(0.1 + std::cos(std::acos(-1.0) / 3.0) * CellData::MAX_SWIM_FORCE, cellData.mForceX[flagellocyte], 1e-12, "CellData updateFlagellocytesForce forceX");
    assertNear(0.2 + std::sin(std::acos(-1.0) / 3.0) * CellData::MAX_SWIM_FORCE, cellData.mForceY[flagellocyte], 1e-12, "CellData updateFlagellocytesForce forceY");
    assertNear(CellData::MAX_SWIM_FORCE * 0.25f, cellData.mSwimAnimationTime[flagellocyte], 1e-6, "CellData updateFlagellocytesForce animation");
}

void testCellDataUpdateFlagellocytesForceWrapsPositiveAnimationTime() {
    Environment environment = Environment::constructDefaultEnvironment(10, 10, 1.0);
    environment.mIsPreviewMode = true;
    CellData cellData(1);
    int flagellocyte = addCiliocyteTestCell(cellData, CellType::FLAGELLOCYTE, 0.0, 0.0, CellData::INITIAL_MASS, 0.02);
    setConstantValue(cellData, flagellocyte, Gene::SWIM_FORCE_VALUE_INDEX, 1.0f);
    float animationSpeedFactor = 45.0f / 0.4f;
    float period = static_cast<float>(20.0 * std::acos(-1.0)) / animationSpeedFactor;
    cellData.mSwimAnimationTime[flagellocyte] = period - 0.01f;

    cellData.updateFlagellocytesForce(0.5, environment);

    float expected = period - 0.01f + CellData::MAX_SWIM_FORCE * 0.5f;
    while (expected * animationSpeedFactor > static_cast<float>(20.0 * std::acos(-1.0))) {
        expected -= period;
    }
    assertNear(expected, cellData.mSwimAnimationTime[flagellocyte], 1e-6, "CellData updateFlagellocytesForce wraps animation");
}

void testCellDataAllCollisionEffectPass() {
    CellData cellData(2);
    int left = addAliveCell(cellData, CellType::PHAGOCYTE);
    int right = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[left] = 0.0;
    cellData.mPositionY[left] = 0.0;
    cellData.mRadius[left] = 0.02;
    cellData.mPositionX[right] = 0.03;
    cellData.mPositionY[right] = 0.0;
    cellData.mRadius[right] = 0.02;

    addCollisionContact(cellData, left, right);
    addCollisionContact(cellData, right, left);

    cellData.handleAllCellCollisionEffects();

    assertNear(-cellData.mForceX[right], cellData.mForceX[left], 1e-12, "CellData handleAllCellCollisionEffects symmetric forceX");
    assertNear(0.0, cellData.mForceY[left], 1e-12, "CellData handleAllCellCollisionEffects left forceY");
    assertNear(0.0, cellData.mForceY[right], 1e-12, "CellData handleAllCellCollisionEffects right forceY");
}

void testSimulationWorldRenderContactFreshnessContract() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    RenderContactCollector renderContactCollector;
    RenderContactChordBuffer renderContactChords(environment.maxCellCount);
    const auto collectForCapture = [&]() {
        const RenderContactInput input = world.cellContactCacheIsFresh()
                ? RenderContactInput::FreshPhysicsContacts
                : RenderContactInput::CurrentGeometry;
        renderContactCollector.collect(
                input, cellData, world.mEnvironment, renderContactChords);
        return input;
    };
    const int left = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int oldNeighbor = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int newNeighbor = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[left] = 0.0;
    cellData.mPositionY[left] = 0.0;
    cellData.mRadius[left] = 0.02;
    cellData.mPositionX[oldNeighbor] = 0.03;
    cellData.mPositionY[oldNeighbor] = 0.0;
    cellData.mRadius[oldNeighbor] = 0.02;
    cellData.mPositionX[newNeighbor] = 0.2;
    cellData.mPositionY[newNeighbor] = 0.0;
    cellData.mRadius[newNeighbor] = 0.02;

    world.updateGrids();
    assertTrue(!world.cellContactCacheIsFresh(),
            "SimulationWorld grid rebuild invalidates the physics contact cache");
    world.processCellOverlaps();
    assertTrue(world.cellContactCacheIsFresh(),
            "SimulationWorld full overlap pass marks the physics contact cache fresh");
    assertTrue(collectForCapture() == RenderContactInput::FreshPhysicsContacts,
            "SimulationWorld render preparation selects the fresh O(1) cache decision");
    assertTrue(renderContactChords.activeChordCount(left) == 1
                    && renderContactChords.activeChords(left)[0]
                                    .neighborCellIndex
                            == oldNeighbor,
            "SimulationWorld fresh render preparation uses the cached overlap");

    const int cachedContactCount = cellData.cellCollisionContactCount(left);
    const int cachedContactTarget = cellData.cellCollisionContactTarget(
            cellData.cellCollisionContactBegin(left));
    cellData.mForceX[left] = 1.25;
    cellData.mForceY[left] = -0.75;
    cellData.mFertilizationTarget[left] = newNeighbor;
    const int topologyEdgeCount = cellData.mTopology.edgeCount();

    cellData.mPositionX[oldNeighbor] = 0.4;
    cellData.mPositionX[newNeighbor] = 0.03;
    world.updateGrids();
    assertTrue(collectForCapture() == RenderContactInput::CurrentGeometry,
            "SimulationWorld render preparation rebuilds stale contacts from current geometry");
    assertTrue(renderContactChords.activeChordCount(left) == 1
                    && renderContactChords.activeChords(left)[0]
                                    .neighborCellIndex
                            == newNeighbor,
            "SimulationWorld stale-contact fallback removes the old overlap and finds the new one");
    renderContactCollector.collect(
            RenderContactInput::CurrentGeometry,
            cellData,
            world.mEnvironment,
            renderContactChords);
    const auto movedChords = renderContactChords.activeChords(left);
    assertTrue(movedChords.size() == 1
                    && movedChords[0].neighborCellIndex == newNeighbor,
            "RenderContactCollector current-geometry path removes the old overlap and finds the new one");
    assertTrue(!world.cellContactCacheIsFresh(),
            "Render-only geometry collection does not claim to refresh physics contacts");
    assertTrue(cellData.cellCollisionContactCount(left) == cachedContactCount
                    && cellData.cellCollisionContactTarget(
                               cellData.cellCollisionContactBegin(left))
                            == cachedContactTarget,
            "Render-only geometry collection leaves physics contacts unchanged");
    assertNear(1.25, cellData.mForceX[left], 0.0,
            "Render-only geometry collection leaves forceX unchanged");
    assertNear(-0.75, cellData.mForceY[left], 0.0,
            "Render-only geometry collection leaves forceY unchanged");
    assertTrue(cellData.mFertilizationTarget[left] == newNeighbor,
            "Render-only geometry collection leaves fertilization intents unchanged");
    assertTrue(!cellData.mIsDead[left]
                    && !cellData.mIsDead[oldNeighbor]
                    && !cellData.mIsDead[newNeighbor],
            "Render-only geometry collection leaves death state unchanged");
    assertTrue(cellData.mTopology.edgeCount() == topologyEdgeCount,
            "Render-only geometry collection leaves topology unchanged");

    cellData.mPositionX[newNeighbor] = 0.05;
    cellData.mRadius[left] = 0.04;
    world.updateGrids();
    renderContactCollector.collect(
            RenderContactInput::CurrentGeometry,
            cellData,
            world.mEnvironment,
            renderContactChords);
    assertTrue(renderContactChords.activeChordCount(left) == 1
                    && renderContactChords.activeChords(left)[0]
                                    .neighborCellIndex
                            == newNeighbor,
            "RenderContactCollector reads a direct radius edit without a simulation tick");

    world.removeCellImmediately(oldNeighbor);
    assertTrue(!world.cellContactCacheIsFresh(),
            "SimulationWorld compaction invalidates physics contact indexes");
    renderContactCollector.collect(
            RenderContactInput::CurrentGeometry,
            cellData,
            world.mEnvironment,
            renderContactChords);
    assertTrue(cellData.mCellCount == 2
                    && renderContactChords.activeChordCount(left) == 1
                    && renderContactChords.activeChords(left)[0]
                                    .neighborCellIndex
                            == oldNeighbor,
            "RenderContactCollector follows the compacted live Cell index without ghost walls");

    Environment gameteEnvironment =
            Environment::constructDefaultEnvironment(2, 0, 1.0);
    SimulationWorld gameteWorld(false, gameteEnvironment);
    CellData& gametes = gameteWorld.mCellData;
    const int larger = addAliveCell(gametes, CellType::GAMETE);
    const int smaller = addAliveCell(gametes, CellType::GAMETE);
    gametes.mAge[larger] = CellData::AGE_OF_CONSENT + 1.0;
    gametes.mAge[smaller] = CellData::AGE_OF_CONSENT + 1.0;
    gametes.mPositionX[larger] = 0.0;
    gametes.mPositionX[smaller] = 0.005;
    gametes.mRadius[larger] = 0.03;
    gametes.mRadius[smaller] = 0.02;
    gametes.mFertilizationTarget[larger] = -1;
    gametes.mFertilizationTarget[smaller] = -1;
    gametes.clearCellCollisionContacts();
    gameteWorld.updateGrids();
    renderContactCollector.collect(
            RenderContactInput::CurrentGeometry,
            gametes,
            gameteWorld.mEnvironment,
            renderContactChords);
    assertTrue(gametes.mFertilizationTarget[larger] == -1
                    && gametes.mFertilizationTarget[smaller] == -1
                    && !gametes.mIsDead[larger]
                    && !gametes.mIsDead[smaller]
                    && gametes.cellCollisionContactCount(larger) == 0
                    && gametes.cellCollisionContactCount(smaller) == 0,
            "Render-only collection never applies fertilization, containment death, or physics contacts");
}

void testCellDataGlueocyteCollisionCreatesLinks() {
    CellData cellData(2);
    int glueocyte = addAliveCell(cellData, CellType::GLUEOCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mAge[glueocyte] = CellData::AGE_OF_CONSENT;
    cellData.mAge[victim] = CellData::AGE_OF_CONSENT;
    cellData.mPositionX[glueocyte] = 2.0;
    cellData.mPositionY[glueocyte] = 3.0;
    cellData.mPositionX[victim] = 5.0;
    cellData.mPositionY[victim] = 7.0;

    cellData.handleGlueocyteCollision(glueocyte, victim);
    cellData.handleGlueocyteCollision(glueocyte, victim);

    double expectedAngle = std::atan2(4.0, 3.0);
    double pi = std::acos(-1.0);
    assertTrue(cellData.mTopology.edgeCount() == 1, "CellData glueocyte collision edge count");
    assertTrue(cellData.mTopology.isLinked(glueocyte, victim), "CellData glueocyte collision edge target");
    assertTrue(
            cellData.mTopology.edgeCreationReason(0)
                    == CellLinkCreationReason::GLUEOCYTE_COLLISION,
            "CellData glueocyte collision records the link reason");
    assertNear(expectedAngle, cellData.mTopology.edgeAngleA(0), 1e-6, "CellData glueocyte collision angleA");
    assertNear(expectedAngle + pi, cellData.mTopology.edgeAngleB(0), 1e-6, "CellData glueocyte collision angleB");
    assertNear(0.5f, cellData.mTopology.edgeTorsionalStiffness(0), 0.0, "CellData glueocyte collision stiffness");
    assertNear(0.0f, cellData.mTopology.edgeRestLength(0), 0.0, "CellData glueocyte collision rest length");
}

void testCellDataGlueocyteCollisionSkipsInvalidCases() {
    CellData youngData(2);
    int youngGlueocyte = addAliveCell(youngData, CellType::GLUEOCYTE);
    int youngVictim = addAliveCell(youngData, CellType::PHAGOCYTE);
    youngData.handleGlueocyteCollision(youngGlueocyte, youngVictim);
    assertTrue(youngData.mTopology.edgeCount() == 0, "CellData glueocyte collision skips young cells");

    CellData nonGlueocyteData(2);
    int phagocyte = addAliveCell(nonGlueocyteData, CellType::PHAGOCYTE);
    int victim = addAliveCell(nonGlueocyteData, CellType::PHAGOCYTE);
    nonGlueocyteData.mAge[phagocyte] = CellData::AGE_OF_CONSENT;
    nonGlueocyteData.mAge[victim] = CellData::AGE_OF_CONSENT;
    nonGlueocyteData.handleGlueocyteCollision(phagocyte, victim);
    assertTrue(nonGlueocyteData.mTopology.edgeCount() == 0, "CellData glueocyte collision skips non-glueocyte");

    CellData protectedData(3);
    int glueocyte = addAliveCell(protectedData, CellType::GLUEOCYTE);
    int protectedVictim = addAliveCell(protectedData, CellType::PHAGOCYTE);
    int keratinocyte = addAliveCell(protectedData, CellType::KERATINOCYTE);
    protectedData.mAge[glueocyte] = CellData::AGE_OF_CONSENT;
    protectedData.mAge[protectedVictim] = CellData::AGE_OF_CONSENT;
    protectedData.addLink(
            protectedVictim, keratinocyte,
            CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);
    protectedData.handleGlueocyteCollision(glueocyte, protectedVictim);
    assertTrue(!protectedData.mTopology.isLinked(glueocyte, protectedVictim), "CellData glueocyte collision skips keratinocyte-protected victim");

    CellData fullData(8);
    int fullGlueocyte = addAliveCell(fullData, CellType::GLUEOCYTE);
    int fullVictim = addAliveCell(fullData, CellType::PHAGOCYTE);
    fullData.mAge[fullGlueocyte] = CellData::AGE_OF_CONSENT;
    fullData.mAge[fullVictim] = CellData::AGE_OF_CONSENT;
    for (int linkIndex = 0; linkIndex < CellData::MAX_GLUEOCYTE_LINK_COUNT; linkIndex++) {
        int other = addAliveCell(fullData, CellType::PHAGOCYTE);
        fullData.mTopology.addCellLink(
                fullGlueocyte, other, CellLinkCreationReason::UNSPECIFIED,
                0.0, 0.0, 0.0f, 0.0f);
    }
    fullData.handleGlueocyteCollision(fullGlueocyte, fullVictim);
    assertTrue(!fullData.mTopology.isLinked(fullGlueocyte, fullVictim), "CellData glueocyte collision skips full glueocyte");

    CellData maxNeighborData(2);
    int maxGlueocyte = addAliveCell(maxNeighborData, CellType::GLUEOCYTE);
    int maxVictim = addAliveCell(maxNeighborData, CellType::PHAGOCYTE);
    maxNeighborData.mAge[maxGlueocyte] = CellData::AGE_OF_CONSENT;
    maxNeighborData.mAge[maxVictim] = CellData::AGE_OF_CONSENT;
    for (int linkIndex = 0; linkIndex < CellData::MAX_NEIGHBORS; linkIndex++) {
        maxNeighborData.mTopology.addCellLink(
                maxVictim, maxGlueocyte,
                CellLinkCreationReason::UNSPECIFIED,
                0.0, 0.0, 0.0f, 0.0f);
    }
    maxNeighborData.handleGlueocyteCollision(maxGlueocyte, maxVictim);
    assertTrue(maxNeighborData.mTopology.edgeCount() == CellData::MAX_NEIGHBORS, "CellData glueocyte collision skips max-neighbor victim");
}

void testCellDataDevorocyteCollisionTransfersMassAndContents() {
    CellData cellData(2);
    int devorocyte = addAliveCell(cellData, CellType::DEVOROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[devorocyte] = 0.2;
    cellData.mMass[victim] = 0.1;
    cellData.mNitrates[devorocyte] = 0.2;
    cellData.mNitrates[victim] = 0.6;
    cellData.mToxin[devorocyte] = 0.1f;
    cellData.mToxin[victim] = 0.5f;

    double deltaTime = 0.01;
    double deltaMass = CellData::DEVOROCYTE_SUCK_SPEED * deltaTime;
    double victimMassAfter = cellData.mMass[victim] - deltaMass;
    double expectedCurrentNitrates = cellData.mNitrates[devorocyte] + deltaMass * cellData.mNitrates[victim] / victimMassAfter;
    double expectedVictimNitrates = cellData.mNitrates[victim] - deltaMass * cellData.mNitrates[victim] / victimMassAfter;
    float expectedCurrentToxin = static_cast<float>(cellData.mToxin[devorocyte] + deltaMass * cellData.mToxin[victim] / victimMassAfter);
    float expectedVictimToxin = static_cast<float>(cellData.mToxin[victim] - deltaMass * cellData.mToxin[victim] / victimMassAfter);

    cellData.handleDevorocyteCollision(devorocyte, victim, deltaTime);

    assertNear(0.2 + deltaMass, cellData.mMass[devorocyte], 1e-12, "CellData devorocyte collision current mass");
    assertNear(victimMassAfter, cellData.mMass[victim], 1e-12, "CellData devorocyte collision victim mass");
    assertNear(expectedCurrentNitrates, cellData.mNitrates[devorocyte], 1e-12, "CellData devorocyte collision current nitrates");
    assertNear(expectedVictimNitrates, cellData.mNitrates[victim], 1e-12, "CellData devorocyte collision victim nitrates");
    assertNear(expectedCurrentToxin, cellData.mToxin[devorocyte], 1e-7, "CellData devorocyte collision current toxin");
    assertNear(expectedVictimToxin, cellData.mToxin[victim], 1e-7, "CellData devorocyte collision victim toxin");
}

void testCellDataDevorocyteCollisionCapsVictimMass() {
    CellData cellData(2);
    int devorocyte = addAliveCell(cellData, CellType::DEVOROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[devorocyte] = 0.2;
    cellData.mMass[victim] = CellData::MIN_MASS * 0.6;
    cellData.mNitrates[victim] = 0.3;
    cellData.mToxin[victim] = 0.4f;

    double expectedDeltaMass = CellData::MIN_MASS * 0.1;
    cellData.handleDevorocyteCollision(devorocyte, victim, 10.0);

    assertNear(0.2 + expectedDeltaMass, cellData.mMass[devorocyte], 1e-12, "CellData devorocyte collision capped current mass");
    assertNear(CellData::MIN_MASS * 0.5, cellData.mMass[victim], 1e-12, "CellData devorocyte collision capped victim mass");
}

void testCellDataDevorocyteCollisionSkipsInvalidCases() {
    CellData nonDevorocyteData(2);
    int phagocyte = addAliveCell(nonDevorocyteData, CellType::PHAGOCYTE);
    int victim = addAliveCell(nonDevorocyteData, CellType::PHAGOCYTE);
    nonDevorocyteData.mMass[phagocyte] = 0.2;
    nonDevorocyteData.mMass[victim] = 0.1;
    nonDevorocyteData.handleDevorocyteCollision(phagocyte, victim, 1.0);
    assertNear(0.2, nonDevorocyteData.mMass[phagocyte], 0.0, "CellData devorocyte collision skips non-devorocyte current");
    assertNear(0.1, nonDevorocyteData.mMass[victim], 0.0, "CellData devorocyte collision keeps victim for non-devorocyte current");

    CellData protectedData(3);
    int devorocyte = addAliveCell(protectedData, CellType::DEVOROCYTE);
    int protectedVictim = addAliveCell(protectedData, CellType::PHAGOCYTE);
    int keratinocyte = addAliveCell(protectedData, CellType::KERATINOCYTE);
    protectedData.mMass[devorocyte] = 0.2;
    protectedData.mMass[protectedVictim] = 0.1;
    protectedData.addLink(
            protectedVictim, keratinocyte,
            CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);
    protectedData.handleDevorocyteCollision(devorocyte, protectedVictim, 1.0);
    assertNear(0.2, protectedData.mMass[devorocyte], 0.0, "CellData devorocyte collision skips protected victim current mass");
    assertNear(0.1, protectedData.mMass[protectedVictim], 0.0, "CellData devorocyte collision skips protected victim mass");
}

void testCellDataVirocyteCollisionInfectsOriginAndCopiesGeneTree() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    CellData cellData(3);
    int virocyte = addAliveCell(cellData, CellType::VIROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mOriginTag[victim] = SimulationWorld::RANDOM_CELL_ORIGIN;

    editGenomeGene(cellData, virocyte, 0, [](GeneBuilder& gene) {
        gene.setVirusFromGeneIndex(GeneIndex(1));
        gene.setVirusToGeneIndex(GeneIndex(20));
    });
    editGenomeGene(cellData, virocyte, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::SECROCYTE);
        gene.setChild1GeneIndex(GeneIndex(2));
        gene.setChild2GeneIndex(GeneIndex(3));
        gene.setColor(0, 0.25f);
    });
    editGenomeGene(cellData, virocyte, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::GLUEOCYTE);
        gene.setChild1GeneIndex(GeneIndex(4));
        gene.setChild2GeneIndex(GeneIndex(5));
    });
    editGenomeGene(cellData, virocyte, 3, [](GeneBuilder& gene) {
        gene.setCellType(CellType::DEVOROCYTE);
        gene.setChild1GeneIndex(GeneIndex(6));
        gene.setChild2GeneIndex(GeneIndex(7));
    });
    editGenomeGene(cellData, virocyte, 4, [](GeneBuilder& gene) {
        gene.setCellType(CellType::KERATINOCYTE);
    });
    editGenomeGene(cellData, virocyte, 5, [](GeneBuilder& gene) {
        gene.setCellType(CellType::SENSEOCYTE);
    });
    editGenomeGene(cellData, virocyte, 6, [](GeneBuilder& gene) {
        gene.setCellType(CellType::STEREOCYTE);
    });
    editGenomeGene(cellData, virocyte, 7, [](GeneBuilder& gene) {
        gene.setCellType(CellType::CILIOCYTE);
    });

    cellData.handleVirocyteCollision(virocyte, victim, 1.0, environment);

    assertTrue(cellData.mOriginTag[victim] == SimulationWorld::INFECTED_RANDOM_CELL_ORIGIN, "CellData virocyte infects random origin");
    assertTrue(viewGenomeGene(cellData, victim, 20).cellType() == CellType::SECROCYTE, "CellData virocyte copies source gene");
    assertTrue(viewGenomeGene(cellData, victim, 20).child1GeneIndex() == GeneIndex(2), "CellData virocyte preserves copied child index");
    assertNear(0.25, viewGenomeGene(cellData, victim, 20).color(0), 0.0, "CellData virocyte copies gene payload");
    assertTrue(viewGenomeGene(cellData, victim, 21).cellType() == CellType::GLUEOCYTE, "CellData virocyte copies child1 gene");
    assertTrue(viewGenomeGene(cellData, victim, 22).cellType() == CellType::DEVOROCYTE, "CellData virocyte copies child2 gene");
    assertTrue(viewGenomeGene(cellData, victim, 23).cellType() == CellType::KERATINOCYTE, "CellData virocyte copies child1 child1 gene");
    assertTrue(viewGenomeGene(cellData, victim, 24).cellType() == CellType::SENSEOCYTE, "CellData virocyte copies child1 child2 gene");
    assertTrue(viewGenomeGene(cellData, victim, 25).cellType() == CellType::STEREOCYTE, "CellData virocyte copies child2 child1 gene");
    assertTrue(viewGenomeGene(cellData, victim, 26).cellType() == CellType::CILIOCYTE, "CellData virocyte copies child2 child2 gene");
}

void testCellDataVirocyteCollisionSkipsPreviewAndKeratinocyteProtectedVictim() {
    Environment previewEnvironment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    previewEnvironment.mIsPreviewMode = true;
    CellData previewData(2);
    int previewVirocyte = addAliveCell(previewData, CellType::VIROCYTE);
    int previewVictim = addAliveCell(previewData, CellType::PHAGOCYTE);
    previewData.mOriginTag[previewVictim] = SimulationWorld::USER_CELL_ORIGIN;
    editGenomeGene(previewData, previewVirocyte, 0, [](GeneBuilder& gene) {
        gene.setVirusFromGeneIndex(GeneIndex(1));
        gene.setVirusToGeneIndex(GeneIndex(2));
    });
    editGenomeGene(previewData, previewVirocyte, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::SECROCYTE);
    });
    previewData.handleVirocyteCollision(previewVirocyte, previewVictim, 1.0, previewEnvironment);
    assertTrue(previewData.mOriginTag[previewVictim] == SimulationWorld::USER_CELL_ORIGIN, "CellData virocyte skips preview origin");
    assertTrue(viewGenomeGene(previewData, previewVictim, 2).cellType() == CellType::PHAGOCYTE, "CellData virocyte skips preview genome copy");

    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    CellData protectedData(3);
    int virocyte = addAliveCell(protectedData, CellType::VIROCYTE);
    int victim = addAliveCell(protectedData, CellType::PHAGOCYTE);
    int keratinocyte = addAliveCell(protectedData, CellType::KERATINOCYTE);
    protectedData.mOriginTag[victim] = SimulationWorld::USER_CELL_ORIGIN;
    editGenomeGene(protectedData, virocyte, 0, [](GeneBuilder& gene) {
        gene.setVirusFromGeneIndex(GeneIndex(1));
        gene.setVirusToGeneIndex(GeneIndex(2));
    });
    editGenomeGene(protectedData, virocyte, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::SECROCYTE);
    });
    protectedData.addLink(
            victim, keratinocyte, CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);
    protectedData.handleVirocyteCollision(virocyte, victim, 1.0, environment);
    assertTrue(protectedData.mOriginTag[victim] == SimulationWorld::USER_CELL_ORIGIN, "CellData virocyte skips protected origin");
    assertTrue(viewGenomeGene(protectedData, victim, 2).cellType() == CellType::PHAGOCYTE, "CellData virocyte skips protected genome copy");
}

void testCellDataSecrocyteCollisionAppliesToxinToxicationAndSignals() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    CellData cellData(4);
    int secrocyte = addAliveCell(cellData, CellType::SECROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mNitrates[secrocyte] = CellData::MAX_NITRATES;
    editGenomeGene(cellData, secrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_CYANIDE);
    });
    cellData.handleSecrocyteCollision(secrocyte, victim, 0.25, environment);
    assertNear(CellData::TOXIN_SPEED * 0.25, cellData.mToxin[victim], 1e-6, "CellData secrocyte cyanide toxin");

    editGenomeGene(cellData, secrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_LIPASE);
    });
    cellData.handleSecrocyteCollision(secrocyte, victim, 0.5, environment);
    assertNear(CellData::DEFENSIN_SPEED * 0.5, cellData.mToxication[victim], 1e-7, "CellData secrocyte lipase toxication");

    editGenomeGene(cellData, secrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT + 1);
    });
    cellData.handleSecrocyteCollision(secrocyte, victim, 0.1, environment);
    assertNear(-CellData::TOXIN_SPEED * 0.1, cellData.mSignalSubstance[victim][1], 1e-6, "CellData secrocyte negative signal");

    editGenomeGene(cellData, secrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                        + CellData::SIGNAL_SUBSTANCE_COUNT + 2);
    });
    cellData.handleSecrocyteCollision(secrocyte, victim, 0.1, environment);
    assertNear(CellData::TOXIN_SPEED * 0.1, cellData.mSignalSubstance[victim][2], 1e-6, "CellData secrocyte positive signal");

    int lipocyte = addAliveCell(cellData, CellType::LIPOCYTE);
    cellData.handleSecrocyteCollision(secrocyte, lipocyte, 0.1, environment);
    assertNear(0.0, cellData.mSignalSubstance[lipocyte][2], 0.0, "CellData secrocyte skips signal transfer to lipocyte");
}

void testCellDataSecrocyteCollisionSkipsLowNitratesAndProtectedOrImmuneVictims() {
    Environment environment = Environment::constructDefaultEnvironment(5, 0, 1.0);
    CellData lowNitrateData(2);
    int lowSecrocyte = addAliveCell(lowNitrateData, CellType::SECROCYTE);
    int lowVictim = addAliveCell(lowNitrateData, CellType::PHAGOCYTE);
    lowNitrateData.mNitrates[lowSecrocyte] = CellData::MIN_SECRETE_NITRATES - 0.01;
    editGenomeGene(lowNitrateData, lowSecrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_CYANIDE);
    });
    lowNitrateData.handleSecrocyteCollision(lowSecrocyte, lowVictim, 1.0, environment);
    assertNear(0.0, lowNitrateData.mToxin[lowVictim], 0.0, "CellData secrocyte skips low nitrates");

    CellData protectedData(3);
    int secrocyte = addAliveCell(protectedData, CellType::SECROCYTE);
    int protectedVictim = addAliveCell(protectedData, CellType::PHAGOCYTE);
    int keratinocyte = addAliveCell(protectedData, CellType::KERATINOCYTE);
    protectedData.mNitrates[secrocyte] = CellData::MAX_NITRATES;
    editGenomeGene(protectedData, secrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_CYANIDE);
    });
    protectedData.addLink(
            protectedVictim, keratinocyte,
            CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);
    protectedData.handleSecrocyteCollision(secrocyte, protectedVictim, 1.0, environment);
    assertNear(0.0, protectedData.mToxin[protectedVictim], 0.0, "CellData secrocyte cyanide skips protected victim");

    CellData immuneData(2);
    int cyanideSecrocyte = addAliveCell(immuneData, CellType::SECROCYTE);
    int immuneVictim = addAliveCell(immuneData, CellType::SECROCYTE);
    immuneData.mNitrates[cyanideSecrocyte] = CellData::MAX_NITRATES;
    editGenomeGene(immuneData, cyanideSecrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_CYANIDE);
    });
    editGenomeGene(immuneData, immuneVictim, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_CYANIDE);
    });
    immuneData.handleSecrocyteCollision(cyanideSecrocyte, immuneVictim, 1.0, environment);
    assertNear(0.0, immuneData.mToxin[immuneVictim], 0.0, "CellData secrocyte cyanide skips immune secrocyte");
}

void testCellDataSecrocyteProteaseRemovesNearbyLinksAndBoundaryLinks() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    CellData cellData(4);
    int secrocyte = addAliveCell(cellData, CellType::SECROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    int linked = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mPositionX[secrocyte] = 0.0;
    cellData.mPositionY[secrocyte] = 0.0;
    cellData.mRadius[secrocyte] = 0.02;
    cellData.mPositionX[victim] = 0.01;
    cellData.mPositionY[victim] = 0.0;
    cellData.mPositionX[linked] = 0.015;
    cellData.mPositionY[linked] = 0.0;
    cellData.mRadius[linked] = 0.02;
    cellData.mAge[secrocyte] = CellData::AGE_OF_CONSENT + 1.0;
    cellData.mNitrates[secrocyte] = CellData::MAX_NITRATES;
    editGenomeGene(cellData, secrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_PROTEASE);
    });
    cellData.addLink(
            victim, linked, CellLinkCreationReason::UNSPECIFIED,
            0.0, std::acos(-1.0), 0.0f, 0.0f);

    cellData.handleSecrocyteCollision(secrocyte, victim, SimulationWorld::DELTA_TIME, environment);

    assertTrue(cellData.mTopology.edgeCount() == 0, "CellData secrocyte protease removes topology edge");

    CellData boundaryData(2);
    int boundarySecrocyte = addAliveCell(boundaryData, CellType::SECROCYTE);
    int boundaryVictim = addAliveCell(boundaryData, CellType::GLUEOCYTE);
    boundaryData.mPositionX[boundarySecrocyte] = 0.99;
    boundaryData.mPositionY[boundarySecrocyte] = 0.0;
    boundaryData.mRadius[boundarySecrocyte] = 0.02;
    boundaryData.mAge[boundarySecrocyte] = CellData::AGE_OF_CONSENT + 1.0;
    boundaryData.mNitrates[boundarySecrocyte] = CellData::MAX_NITRATES;
    editGenomeGene(boundaryData, boundarySecrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_PROTEASE);
    });
    boundaryData.addLink(
            boundaryVictim,
            CellData::BOUNDARY_CONNECTION_INDEX,
            CellLinkCreationReason::UNSPECIFIED,
            0.0,
            0.0,
            0.0f,
            0.0f);

    boundaryData.handleSecrocyteCollision(boundarySecrocyte, boundaryVictim, SimulationWorld::DELTA_TIME, environment);

    assertTrue(boundaryData.mTopology.boundaryCount() == 0, "CellData secrocyte protease removes topology boundary link");
}

void testCellDataBiologicalSideEffectsPassRunsVirocyteAndSecrocyte() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    CellData cellData(3);
    int virocyte = addAliveCell(cellData, CellType::VIROCYTE);
    int secrocyte = addAliveCell(cellData, CellType::SECROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mOriginTag[victim] = SimulationWorld::USER_CELL_ORIGIN;
    editGenomeGene(cellData, virocyte, 0, [](GeneBuilder& gene) {
        gene.setVirusFromGeneIndex(GeneIndex(1));
        gene.setVirusToGeneIndex(GeneIndex(2));
    });
    editGenomeGene(cellData, virocyte, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::SECROCYTE);
    });
    cellData.mNitrates[secrocyte] = CellData::MAX_NITRATES;
    editGenomeGene(cellData, secrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_CYANIDE);
    });
    cellData.mPositionX[victim] = 0.03;
    cellData.mPositionX[virocyte] = 0.0;
    cellData.mPositionX[secrocyte] = 0.06;
    cellData.mRadius[virocyte] = 0.02;
    cellData.mRadius[secrocyte] = 0.02;
    cellData.mRadius[victim] = 0.02;
    addCollisionContact(cellData, virocyte, victim);
    addCollisionContact(cellData, secrocyte, victim);

    cellData.handleAllCellCollisionBiologicalSideEffects(1.0, environment);

    assertTrue(cellData.mOriginTag[victim] == SimulationWorld::INFECTED_USER_CELL_ORIGIN, "CellData biological side effects runs virocyte");
    assertTrue(viewGenomeGene(cellData, victim, 2).cellType() == CellType::SECROCYTE, "CellData biological side effects copies virocyte gene");
    assertNear(CellData::TOXIN_SPEED, cellData.mToxin[victim], 1e-6, "CellData biological side effects runs secrocyte");
}

void testCellDataBiologicalSideEffectsPassCreatesGlueocyteLinks() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    CellData cellData(2);
    int glueocyte = addAliveCell(cellData, CellType::GLUEOCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mAge[glueocyte] = CellData::AGE_OF_CONSENT;
    cellData.mAge[victim] = CellData::AGE_OF_CONSENT;
    cellData.mPositionX[glueocyte] = 0.0;
    cellData.mPositionY[glueocyte] = 0.0;
    cellData.mPositionX[victim] = 0.03;
    cellData.mPositionY[victim] = 0.0;
    cellData.mRadius[glueocyte] = 0.02;
    cellData.mRadius[victim] = 0.02;
    addCollisionContact(cellData, glueocyte, victim);

    cellData.handleCellCollisionBiologicalSideEffects(glueocyte, 0.1, environment);

    assertTrue(cellData.mTopology.isLinked(glueocyte, victim), "CellData biological side effects glueocyte topology link");
    assertNear(0.0, cellData.mForceX[glueocyte], 0.0, "CellData biological side effects does not apply force");
}

void testCellDataBiologicalSideEffectsPassTransfersDevorocyteMass() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    CellData cellData(2);
    int devorocyte = addAliveCell(cellData, CellType::DEVOROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[devorocyte] = 0.2;
    cellData.mMass[victim] = 0.1;
    cellData.mNitrates[victim] = 0.5;
    cellData.mToxin[victim] = 0.4f;
    cellData.mPositionX[devorocyte] = 0.0;
    cellData.mPositionX[victim] = 0.03;
    cellData.mRadius[devorocyte] = 0.02;
    cellData.mRadius[victim] = 0.02;
    addCollisionContact(cellData, devorocyte, victim);

    double deltaTime = 0.01;
    double deltaMass = CellData::DEVOROCYTE_SUCK_SPEED * deltaTime;
    cellData.handleCellCollisionBiologicalSideEffects(devorocyte, deltaTime, environment);

    assertNear(0.2 + deltaMass, cellData.mMass[devorocyte], 1e-12, "CellData biological side effects devorocyte mass");
    assertNear(0.1 - deltaMass, cellData.mMass[victim], 1e-12, "CellData biological side effects victim mass");
    assertTrue(cellData.mNitrates[devorocyte] > 0.0, "CellData biological side effects transfers nitrates");
    assertTrue(cellData.mToxin[devorocyte] > 0.0f, "CellData biological side effects transfers toxin");
    assertNear(0.0, cellData.mForceX[devorocyte], 0.0, "CellData biological side effects devorocyte no force");
}

void testCellDataBiologicalSideEffectsPassSkipsInvalidTargets() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    CellData cellData(3);
    int devorocyte = addAliveCell(cellData, CellType::DEVOROCYTE);
    int validVictim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[devorocyte] = 0.2;
    cellData.mMass[validVictim] = 0.1;
    cellData.mPositionX[devorocyte] = 0.0;
    cellData.mPositionX[validVictim] = 0.03;
    cellData.mRadius[devorocyte] = 0.02;
    cellData.mRadius[validVictim] = 0.02;
    addCollisionContact(cellData, devorocyte, validVictim);

    double deltaMass = CellData::DEVOROCYTE_SUCK_SPEED * 0.01;
    cellData.handleCellCollisionBiologicalSideEffects(devorocyte, 0.01, environment);

    assertNear(0.1 - deltaMass, cellData.mMass[validVictim], 1e-12, "CellData biological side effects handles valid victim");

    cellData.markCellAsDead(devorocyte);
    double massBefore = cellData.mMass[validVictim];
    cellData.handleCellCollisionBiologicalSideEffects(devorocyte, 0.01, environment);
    assertNear(massBefore, cellData.mMass[validVictim], 0.0, "CellData biological side effects skips dead current");
}

void testCellDataAllBiologicalSideEffectsPass() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    CellData cellData(3);
    int glueocyte = addAliveCell(cellData, CellType::GLUEOCYTE);
    int devorocyte = addAliveCell(cellData, CellType::DEVOROCYTE);
    int victim = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mAge[glueocyte] = CellData::AGE_OF_CONSENT;
    cellData.mAge[victim] = CellData::AGE_OF_CONSENT;
    cellData.mMass[devorocyte] = 0.2;
    cellData.mMass[victim] = 0.1;
    cellData.mPositionX[glueocyte] = 0.0;
    cellData.mPositionY[glueocyte] = 0.0;
    cellData.mPositionX[victim] = 0.03;
    cellData.mPositionY[victim] = 0.0;
    cellData.mRadius[glueocyte] = 0.02;
    cellData.mRadius[devorocyte] = 0.02;
    cellData.mRadius[victim] = 0.02;
    addCollisionContact(cellData, glueocyte, victim);
    addCollisionContact(cellData, devorocyte, victim);

    cellData.handleAllCellCollisionBiologicalSideEffects(0.01, environment);

    assertTrue(cellData.mTopology.isLinked(glueocyte, victim), "CellData all biological side effects glueocyte topology link");
    assertTrue(cellData.mMass[devorocyte] > 0.2, "CellData all biological side effects devorocyte mass");
    assertTrue(cellData.mMass[victim] < 0.1, "CellData all biological side effects victim mass");
}

void testCellDataUpdateCellsMetabolismBuffersPreviewAndDeadCells() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    environment.mIsPreviewMode = true;
    environment.aging = false;
    CellData cellData(1);
    int phagocyte = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[phagocyte] = 0.2;
    cellData.mRadius[phagocyte] = 0.02;
    cellData.mNitrates[phagocyte] = 0.4;
    cellData.mToxin[phagocyte] = 2.0f;
    cellData.mToxication[phagocyte] = 0.5f;
    cellData.markCellAsDead(phagocyte);

    double deltaTime = 0.1;
    cellData.updateCellsMetabolism(deltaTime, environment);

    double expectedPreviewGrowth = CellData::MASS_PER_AREA * Environment::PREVIEW_SUN * 0.02 * 0.02 * deltaTime;
    double expectedIdleCost = expectedIdleMetabolismCost(cellData, phagocyte, CellType::PHAGOCYTE, deltaTime, environment);
    assertNear(0.2 + expectedPreviewGrowth - expectedIdleCost, cellData.mNewMass[phagocyte], 1e-12, "CellData metabolism preview dead cell mass buffer");
    assertNear(0.4 + 2.0 * CellData::MAX_NITRATE_FLOW * (environment.mNitrates - 0.4) * deltaTime,
            cellData.mNewNitrates[phagocyte],
            1e-12,
            "CellData metabolism nitrate uptake");
    assertNear(2.0 - CellData::TOXIN_DECAY * 2.0 * deltaTime, cellData.mNewToxin[phagocyte], 1e-7, "CellData metabolism toxin decay");
    assertNear(0.5 + (2.0 - 0.5) * CellData::TOXICATION_SPEED * deltaTime,
            cellData.mToxication[phagocyte],
            1e-7,
            "CellData metabolism toxication relaxes");
    assertNear(deltaTime, cellData.mAge[phagocyte], 1e-12, "CellData metabolism increments dead cell age like Java");
    assertTrue(cellData.mIsDead[phagocyte], "CellData metabolism does not revive dead cell");
}

void testCellDataUpdateCellsMetabolismTypeSpecificBranches() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.aging = false;
    environment.mNitrates = 0.0;
    environment.mLightIntensity = 1.25;
    environment.mLightRange = 1.0;
    CellData cellData(4);
    int flagellocyte = addAliveCell(cellData, CellType::FLAGELLOCYTE);
    int myocyte = addAliveCell(cellData, CellType::MYOCYTE);
    int photocyte = addAliveCell(cellData, CellType::PHOTOCYTE);
    int secrocyte = addAliveCell(cellData, CellType::SECROCYTE);

    cellData.mRadius[flagellocyte] = 0.02;
    cellData.mMass[flagellocyte] = 0.2;
    setConstantValue(
            cellData,
            flagellocyte,
            Gene::SWIM_FORCE_VALUE_INDEX,
            0.0f);
    float swimForce = cellData.getValue(flagellocyte, Gene::SWIM_FORCE_VALUE_INDEX, 0);

    cellData.mRadius[myocyte] = 0.02;
    cellData.mMass[myocyte] = 0.2;
    cellData.mIsMirrored[myocyte] = true;
    cellData.mMyocyteContraction[myocyte] = 0.1f;
    cellData.mMyocyteBending[myocyte] = 0.2f;
    cellData.mMyocyteLifting[myocyte] = -0.1f;
    setConstantValue(
            cellData,
            myocyte,
            Gene::MYOCYTE_CONTRACTION_VALUE_INDEX,
            0.0f);
    setConstantValue(
            cellData,
            myocyte,
            Gene::MYOCYTE_BENDING_VALUE_INDEX,
            0.0f);
    setConstantValue(
            cellData,
            myocyte,
            Gene::MYOCYTE_LIFTING_VALUE_INDEX,
            0.0f);
    float myoContraction = cellData.getValue(myocyte, Gene::MYOCYTE_CONTRACTION_VALUE_INDEX, 0);
    float myoBending = -cellData.getValue(myocyte, Gene::MYOCYTE_BENDING_VALUE_INDEX, 0);
    float myoLifting = cellData.getValue(myocyte, Gene::MYOCYTE_LIFTING_VALUE_INDEX, 0);

    cellData.mRadius[photocyte] = 0.02;
    cellData.mMass[photocyte] = 0.2;
    cellData.mAngularVelocity[photocyte] = 2.0;
    cellData.mAngularVelocityCytoplasm[photocyte] = 1.0;

    cellData.mRadius[secrocyte] = 0.02;
    cellData.mMass[secrocyte] = 0.2;
    cellData.mNitrates[secrocyte] = 0.5;
    editGenomeGene(cellData, secrocyte, 0, [](GeneBuilder& gene) {
        gene.setIntegerValue(
                Gene::SECRETE_TYPE_INTEGER_INDEX,
                Gene::SECRETE_TYPE_CYANIDE);
    });

    double deltaTime = 0.1;
    cellData.updateCellsMetabolism(deltaTime, environment);

    double flagellocyteExpected = 0.2
            - swimForce * (0.008 + swimForce * 0.0409756) * deltaTime
            - expectedIdleMetabolismCost(cellData, flagellocyte, CellType::FLAGELLOCYTE, deltaTime, environment);
    assertNear(flagellocyteExpected, cellData.mNewMass[flagellocyte], 1e-12, "CellData metabolism flagellocyte swim cost");

    double myocyteUseCost = CellData::MYOCYTE_USE_COST / deltaTime
            * ((myoContraction - 0.1) * (myoContraction - 0.1)
                    + (myoBending - 0.2) * (myoBending - 0.2)
                    + (myoLifting + 0.1) * (myoLifting + 0.1));
    double myocyteExpected = 0.2 - myocyteUseCost - expectedIdleMetabolismCost(cellData, myocyte, CellType::MYOCYTE, deltaTime, environment);
    assertNear(myocyteExpected, cellData.mNewMass[myocyte], 1e-9, "CellData metabolism myocyte use cost");
    assertNear(myoContraction, cellData.mMyocyteContraction[myocyte], 1e-7, "CellData metabolism myocyte contraction update");
    assertNear(myoBending, cellData.mMyocyteBending[myocyte], 1e-7, "CellData metabolism myocyte mirrored bending update");
    assertNear(myoLifting, cellData.mMyocyteLifting[myocyte], 1e-7, "CellData metabolism myocyte lifting update");

    double photocyteLightGrowth = CellData::MASS_PER_AREA * environment.getLight(0.0, 0.0) * 0.02 * 0.02 * deltaTime;
    double photocyteExpected = 0.2 + photocyteLightGrowth - expectedIdleMetabolismCost(cellData, photocyte, CellType::PHOTOCYTE, deltaTime, environment);
    assertNear(photocyteExpected, cellData.mNewMass[photocyte], 1e-12, "CellData metabolism photocyte light growth");
    assertNear(1.1, cellData.mAngularVelocityCytoplasm[photocyte], 1e-12, "CellData metabolism photocyte cytoplasm angular fallthrough");

    assertNear(0.5 - 0.75 * CellData::MAX_NITRATE_FLOW * deltaTime,
            cellData.mNewNitrates[secrocyte],
            1e-12,
            "CellData metabolism secrocyte secretion nitrate drain");
    assertNear(deltaTime, cellData.mSwimAnimationTime[secrocyte], 1e-7, "CellData metabolism secrocyte secretion animation");
}

void testCellDataUpdateCellsMetabolismLinkFlowsMassNitratesAndToxin() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    environment.aging = false;
    environment.mNitrates = 0.0;
    CellData cellData(2);
    int source = addAliveCell(cellData, CellType::PHAGOCYTE);
    int linked = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[source] = 0.2;
    cellData.mMass[linked] = 0.1;
    cellData.mRadius[source] = 0.02;
    cellData.mRadius[linked] = 0.02;
    cellData.mNitrates[source] = 0.8;
    cellData.mNitrates[linked] = 0.1;
    cellData.mToxin[source] = 2.0f;
    cellData.mToxin[linked] = 5.0f;
    cellData.editActiveGene(source, [](GeneBuilder& gene) {
        gene.setMassFlowPriority(1.0f);
    });
    cellData.editActiveGene(linked, [](GeneBuilder& gene) {
        gene.setMassFlowPriority(3.0f);
    });
    addPairLink(cellData, source, linked, 0.0, 0.0, 0.0f, 0.0f);

    double deltaTime = 0.1;
    cellData.updateCellsMetabolism(deltaTime, environment);

    double deltaMass = std::min(CellData::MAX_MASS_FLOW, 0.2 * 3.0 - 0.1 * 1.0);
    double expectedMass = 0.2 - expectedIdleMetabolismCost(cellData, source, CellType::PHAGOCYTE, deltaTime, environment) - deltaMass * deltaTime;
    assertNear(expectedMass, cellData.mNewMass[source], 1e-12, "CellData metabolism link mass flow");
    assertNear(0.8 - CellData::MAX_NITRATE_FLOW * deltaTime,
            cellData.mNewNitrates[source],
            1e-12,
            "CellData metabolism link nitrate flow clamps");
    assertNear(2.0 - CellData::TOXIN_DECAY * 2.0 * deltaTime + CellData::TOXIN_FLOW * (5.0 - 2.0) * deltaTime,
            cellData.mNewToxin[source],
            1e-6,
            "CellData metabolism link toxin flow");
}

void testCellDataUpdateCellsMetabolismLipocyteStoresAndReleasesLipids() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    environment.aging = false;
    environment.mNitrates = 0.0;
    CellData cellData(2);
    int storing = addAliveCell(cellData, CellType::LIPOCYTE);
    int releasing = addAliveCell(cellData, CellType::LIPOCYTE);
    cellData.mRadius[storing] = 0.02;
    cellData.mMass[storing] = CellData::MAX_MASS + 0.02;
    cellData.mRadius[releasing] = 0.02;
    cellData.mMass[releasing] = CellData::MAX_MASS - 0.02;
    cellData.mLipids[releasing] = 0.015f;

    double deltaTime = 0.1;
    cellData.updateCellsMetabolism(deltaTime, environment);

    double storingBeforeCap = CellData::MAX_MASS + 0.02
            - expectedIdleMetabolismCost(cellData, storing, CellType::LIPOCYTE, deltaTime, environment);
    assertNear(CellData::MAX_MASS, cellData.mNewMass[storing], 1e-12, "CellData metabolism lipocyte caps mass");
    assertNear(storingBeforeCap - CellData::MAX_MASS, cellData.mLipids[storing], 1e-6, "CellData metabolism lipocyte stores excess");

    double releasingBeforeMove = CellData::MAX_MASS - 0.02
            - expectedIdleMetabolismCost(cellData, releasing, CellType::LIPOCYTE, deltaTime, environment);
    double moved = std::min(0.015, CellData::MAX_MASS - releasingBeforeMove);
    assertNear(releasingBeforeMove + moved, cellData.mNewMass[releasing], 1e-6, "CellData metabolism lipocyte releases stored lipid mass");
    assertNear(0.015 - moved, cellData.mLipids[releasing], 1e-6, "CellData metabolism lipocyte decreases lipid store");
}

void testCellDataUpdateCellsMetabolismStemSwitchAgingAndDeathChecks() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    environment.aging = true;
    environment.mNitrates = 0.0;
    CellData cellData(2);
    int stemocyte = addAliveCell(cellData, CellType::STEMOCYTE);
    int starving = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mRadius[stemocyte] = 0.02;
    cellData.mMass[stemocyte] = 0.2;
    cellData.mAge[stemocyte] = CellData::AGE_OF_CONSENT + 0.01;
    cellData.editActiveGene(stemocyte, [](GeneBuilder& gene) {
        setConstantValue(gene, Gene::STEM_THRESHOLD_1_VALUE_INDEX, 1.0f);
        gene.setVirusFromGeneIndex(GeneIndex(3));
    });
    cellData.mRadius[starving] = 0.02;
    cellData.mMass[starving] = 0.2;
    cellData.mAge[starving] = CellData::MAX_AGE + 0.01;
    cellData.mNitrates[starving] = CellData::MIN_NITRATES - 0.01;

    cellData.updateCellsMetabolism(0.1, environment);

    assertTrue(cellData.activeGeneIndex(stemocyte) == GeneIndex(3), "CellData metabolism stemocyte switches to virus-from gene index");
    assertNear(0.0, cellData.mAge[stemocyte], 0.0, "CellData metabolism stemocyte resets age on switch");
    assertTrue(cellData.mIsDead[starving], "CellData metabolism marks old or nitrate-starved cell dead");
}

void testSimulationWorldApplyNewCellMetabolismCopiesBuffersAndHandlesNaN() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int valid = addAliveCell(cellData, CellType::PHAGOCYTE);
    int nanCell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mNewMass[valid] = 0.123;
    cellData.mNewToxin[valid] = 0.75f;
    cellData.mNewNitrates[valid] = 0.33;
    cellData.mNewMass[nanCell] = std::numeric_limits<double>::quiet_NaN();
    cellData.mNewToxin[nanCell] = 0.5f;
    cellData.mNewNitrates[nanCell] = 0.5;

    world.applyNewCellMetabolism();

    assertNear(0.123, cellData.mMass[valid], 0.0, "SimulationWorld apply metabolism mass");
    assertNear(0.75, cellData.mToxin[valid], 0.0, "SimulationWorld apply metabolism toxin");
    assertNear(0.33, cellData.mNitrates[valid], 0.0, "SimulationWorld apply metabolism nitrates");
    assertNear(0.0, cellData.mNewMass[nanCell], 0.0, "SimulationWorld apply metabolism clears NaN new mass");
    assertTrue(cellData.mIsDead[nanCell], "SimulationWorld apply metabolism marks NaN mass cell dead");
    assertNear(CellData::INITIAL_MASS, cellData.mMass[nanCell], 1e-12, "SimulationWorld apply metabolism skips copying NaN cell mass");
}

void testCellDataUpdateSignalsDecaysOutputAndResetsOut() {
    CellData cellData(1);
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mSignalSubstance[cell][0] = 0.2f;
    cellData.mSignalSubstanceOut[cell][0] = 0.4f;

    double deltaTime = 0.2;
    cellData.updateSignals(deltaTime);

    float expected = 0.2f;
    for (int pass = 0; pass < SimulationWorld::SIGNAL_PASSES; pass++) {
        expected = expected + (0.4f - CellData::SIGNAL_SUBSTANCE_DECAY * expected) * static_cast<float>(deltaTime / SimulationWorld::SIGNAL_PASSES);
    }

    assertNear(expected, cellData.mSignalSubstance[cell][0], 1e-7, "CellData updateSignals decays and applies output");
    assertNear(0.4, cellData.mNewSignalSubstance[cell][0], 1e-7, "CellData updateSignals stores last output");
    assertNear(0.0, cellData.mSignalSubstanceOut[cell][0], 0.0, "CellData updateSignals resets output on last pass");
}

void testCellDataUpdateSignalsNeurocyteGeneratesOutputsAndClamps() {
    CellData cellData(1);
    int neurocyte = addAliveCell(cellData, CellType::NEUROCYTE);
    for (int signalIndex = 0; signalIndex < CellData::NEUROCYTE_OUTPUT_COUNT; signalIndex++) {
        cellData.editActiveGene(
                neurocyte,
                [=](GeneBuilder& gene) {
                    gene.setIntegerValue(
                            Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX
                                    + signalIndex,
                            signalIndex);
                    setConstantValue(
                            gene,
                            Gene::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX
                                    + signalIndex,
                            1.0f);
                });
    }

    cellData.updateSignals(0.1);

    assertNear(CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT, cellData.mNewSignalSubstance[neurocyte][0], 0.0, "CellData updateSignals neurocyte output 0");
    assertNear(CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT, cellData.mNewSignalSubstance[neurocyte][3], 0.0, "CellData updateSignals neurocyte output 3");
    assertNear(0.0, cellData.mSignalSubstanceOut[neurocyte][0], 0.0, "CellData updateSignals neurocyte resets output");
    assertTrue(cellData.mSignalSubstance[neurocyte][0] <= 1.0f, "CellData updateSignals clamps signal high");
}

void testCellDataUpdateSignalsFlowsAcrossLinksAndSkipsLipocytes() {
    CellData flowData(2);
    int source = addAliveCell(flowData, CellType::PHAGOCYTE);
    int linked = addAliveCell(flowData, CellType::PHAGOCYTE);
    flowData.mSignalSubstance[source][0] = 0.0f;
    flowData.mSignalSubstance[linked][0] = 1.0f;
    addPairLink(flowData, source, linked, 0.0, 0.0, 0.0f, 0.0f);

    flowData.updateSignals(0.1);

    assertTrue(flowData.mSignalSubstance[source][0] > 0.0f, "CellData updateSignals flows from linked non-lipocyte");

    CellData lipocyteData(2);
    int target = addAliveCell(lipocyteData, CellType::PHAGOCYTE);
    int lipocyte = addAliveCell(lipocyteData, CellType::LIPOCYTE);
    lipocyteData.mSignalSubstance[target][0] = 0.0f;
    lipocyteData.mSignalSubstance[lipocyte][0] = 1.0f;
    addPairLink(lipocyteData, target, lipocyte, 0.0, 0.0, 0.0f, 0.0f);

    lipocyteData.updateSignals(0.1);

    assertNear(0.0, lipocyteData.mSignalSubstance[target][0], 0.0, "CellData updateSignals skips linked lipocyte source");
}

#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
std::unique_ptr<CellData> makeSignalKernelParityFixture() {
    auto cellData = std::make_unique<CellData>(12);
    constexpr std::array<CellType, 6> types{
            CellType::PHAGOCYTE,
            CellType::NEUROCYTE,
            CellType::LIPOCYTE,
            CellType::MYOCYTE,
            CellType::PHAGOCYTE,
            CellType::PHAGOCYTE
    };
    for (CellType type : types) {
        addAliveCell(*cellData, type);
    }
    for (int cellIndex = 0; cellIndex < cellData->mCellCount; ++cellIndex) {
        for (int channel = 0; channel < CellData::SIGNAL_SUBSTANCE_COUNT; ++channel) {
            cellData->mSignalSubstance[cellIndex][channel] =
                    0.03125f * static_cast<float>(cellIndex - channel);
            cellData->mSignalSubstance[cellIndex][CellData::SIGNAL_SUBSTANCE_COUNT + channel] =
                    -0.015625f * static_cast<float>(cellIndex + channel + 1);
            cellData->mNewSignalSubstance[cellIndex][channel] =
                    0.0078125f * static_cast<float>(cellIndex + channel);
            cellData->mSignalSubstanceOut[cellIndex][channel] =
                    0.0625f * static_cast<float>(channel - 1);
        }
    }

    constexpr std::array<int, CellData::NEUROCYTE_OUTPUT_COUNT> outputChannels{
            0, 1, 1, 3
    };
    constexpr std::array<float, CellData::NEUROCYTE_OUTPUT_COUNT> outputs{
            0.75f, -0.5f, 0.25f, 1.0f
    };
    cellData->editActiveGene(1, [&](GeneBuilder& neurocyteGene) {
        for (int outputIndex = 0;
                outputIndex < CellData::NEUROCYTE_OUTPUT_COUNT;
                ++outputIndex) {
            neurocyteGene.setIntegerValue(
                    Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX
                            + outputIndex,
                    outputChannels[outputIndex]);
            setConstantValue(
                    neurocyteGene,
                    Gene::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + outputIndex,
                    outputs[outputIndex]);
        }
    });

    addPairLink(*cellData, 0, 1, 0.0, 0.0, 0.0f, 0.0f);
    addPairLink(*cellData, 4, 0, 0.0, 0.0, 0.0f, 0.0f);
    addPairLink(*cellData, 1, 3, 0.0, 0.0, 0.0f, 0.0f);
    addPairLink(*cellData, 3, 4, 0.0, 0.0, 0.0f, 0.0f);
    addPairLink(*cellData, 2, 0, 0.0, 0.0, 0.0f, 0.0f);
    cellData->mTopology.addBoundaryLink(1, 0.0, 0.0, 0.0, 0.0f, 0.0f);
    cellData->mTopology.removeCellLink(1);
    addPairLink(*cellData, 5, 1, 0.0, 0.0, 0.0f, 0.0f);
    return cellData;
}

void assertSignalKernelStateBitExact(const CellData& expected,
        const CellData& actual,
        const std::string& context) {
    assertTrue(expected.mCellCount == actual.mCellCount,
            context + " Cell count");
    for (int cellIndex = 0; cellIndex < expected.mCellCount; ++cellIndex) {
        assertTrue(
                std::memcmp(expected.mSignalSubstance[cellIndex].data(),
                        actual.mSignalSubstance[cellIndex].data(),
                        sizeof(expected.mSignalSubstance[cellIndex]))
                        == 0,
                context + " signal state Cell " + std::to_string(cellIndex));
        assertTrue(
                std::memcmp(expected.mNewSignalSubstance[cellIndex].data(),
                        actual.mNewSignalSubstance[cellIndex].data(),
                        sizeof(expected.mNewSignalSubstance[cellIndex]))
                        == 0,
                context + " new signal state Cell " + std::to_string(cellIndex));
        assertTrue(
                std::memcmp(expected.mSignalSubstanceOut[cellIndex].data(),
                        actual.mSignalSubstanceOut[cellIndex].data(),
                        sizeof(expected.mSignalSubstanceOut[cellIndex]))
                        == 0,
                context + " signal output state Cell " + std::to_string(cellIndex));
    }
}

void testCellDataSignalKernelsRemainBitExactAcrossFeaturesAndTopologyChanges() {
    std::unique_ptr<CellData> globalEdge = makeSignalKernelParityFixture();
    std::unique_ptr<CellData> cellOwnedCsr = makeSignalKernelParityFixture();
    globalEdge->setSignalUpdateAlgorithm(
            CellSignalUpdateAlgorithm::GLOBAL_EDGE);
    cellOwnedCsr->setSignalUpdateAlgorithm(
            CellSignalUpdateAlgorithm::CELL_OWNED_CSR);

    for (int step = 0; step < 32; ++step) {
        if (step == 8) {
            globalEdge->mActiveCellType[4] = CellType::LIPOCYTE;
            cellOwnedCsr->mActiveCellType[4] = CellType::LIPOCYTE;
        } else if (step == 12) {
            globalEdge->mActiveCellType[4] = CellType::PHAGOCYTE;
            cellOwnedCsr->mActiveCellType[4] = CellType::PHAGOCYTE;
        } else if (step == 16) {
            globalEdge->mTopology.removeCellLink(0);
            cellOwnedCsr->mTopology.removeCellLink(0);
        } else if (step == 20) {
            addPairLink(*globalEdge, 0, 3, 0.0, 0.0, 0.0f, 0.0f);
            addPairLink(*cellOwnedCsr, 0, 3, 0.0, 0.0, 0.0f, 0.0f);
        } else if (step == 24) {
            globalEdge->mTopology.addBoundaryLink(
                    5, 0.0, 0.0, 0.0, 0.0f, 0.0f);
            cellOwnedCsr->mTopology.addBoundaryLink(
                    5, 0.0, 0.0, 0.0, 0.0f, 0.0f);
        }

        for (int cellIndex = 0; cellIndex < globalEdge->mCellCount; ++cellIndex) {
            if (globalEdge->mActiveCellType[cellIndex] == CellType::NEUROCYTE) {
                continue;
            }
            for (int channel = 0;
                    channel < CellData::SIGNAL_SUBSTANCE_COUNT;
                    ++channel) {
                const float output = 0.00390625f * static_cast<float>(((step + cellIndex * 3 + channel * 5) % 17) - 8);
                globalEdge->mSignalSubstanceOut[cellIndex][channel] = output;
                cellOwnedCsr->mSignalSubstanceOut[cellIndex][channel] = output;
            }
        }

        const double deltaTime = step % 3 == 0 ? 0.02 : 0.013;
        globalEdge->updateSignals(deltaTime);
        cellOwnedCsr->updateSignals(deltaTime);
        assertSignalKernelStateBitExact(
                *globalEdge,
                *cellOwnedCsr,
                "Signal kernel exact parity step " + std::to_string(step));
    }
}

void testCellDataSignalKernelFeatureSpecializationsRemainBitExact() {
    for (int featureMask = 0; featureMask < 4; ++featureMask) {
        std::unique_ptr<CellData> globalEdge = makeSignalKernelParityFixture();
        std::unique_ptr<CellData> cellOwnedCsr = makeSignalKernelParityFixture();
        const bool hasNeurocyte = (featureMask & 1) != 0;
        const bool hasLipocyte = (featureMask & 2) != 0;
        if (!hasNeurocyte) {
            globalEdge->mActiveCellType[1] = CellType::PHAGOCYTE;
            cellOwnedCsr->mActiveCellType[1] = CellType::PHAGOCYTE;
        }
        if (!hasLipocyte) {
            globalEdge->mActiveCellType[2] = CellType::PHAGOCYTE;
            cellOwnedCsr->mActiveCellType[2] = CellType::PHAGOCYTE;
        }
        globalEdge->setSignalUpdateAlgorithm(
                CellSignalUpdateAlgorithm::GLOBAL_EDGE);
        cellOwnedCsr->setSignalUpdateAlgorithm(
                CellSignalUpdateAlgorithm::CELL_OWNED_CSR);

        for (int step = 0; step < 8; ++step) {
            globalEdge->updateSignals(0.011 + step * 0.001);
            cellOwnedCsr->updateSignals(0.011 + step * 0.001);
            assertSignalKernelStateBitExact(
                    *globalEdge,
                    *cellOwnedCsr,
                    "Signal feature specialization "
                            + std::to_string(featureMask)
                            + " step "
                            + std::to_string(step));
        }
    }
}

#ifdef CELLULARITY_PROFILE
void testCellDataSignalCsrCacheRebuildsOnlyForStructuralChanges() {
    std::unique_ptr<CellData> cellData = makeSignalKernelParityFixture();
    cellData->setSignalUpdateAlgorithm(
            CellSignalUpdateAlgorithm::CELL_OWNED_CSR);

    SimulationWorldProfile profile;
    cellData->updateSignals(0.02, &profile);
    assertTrue(profile.signalAdjacencyCacheRebuilds == 1
                    && profile.signalAdjacencyCacheHits == 0,
            "Signal CSR builds cache on first use");

    profile.clear();
    cellData->updateSignals(0.02, &profile);
    assertTrue(profile.signalAdjacencyCacheRebuilds == 0
                    && profile.signalAdjacencyCacheHits == 1,
            "Signal CSR reuses stable topology cache");

    cellData->mTopology.setEdgeRenderOffsets(0, 1.0f, 2.0f, 3.0f, 4.0f);
    profile.clear();
    cellData->updateSignals(0.02, &profile);
    assertTrue(profile.signalAdjacencyCacheRebuilds == 0
                    && profile.signalAdjacencyCacheHits == 1,
            "Signal CSR ignores render-only topology offsets");

    cellData->mTopology.addCellLink(
            0, 3, CellLinkCreationReason::UNSPECIFIED,
            0.0, 0.0, 0.0f, 0.0f);
    profile.clear();
    cellData->updateSignals(0.02, &profile);
    assertTrue(profile.signalAdjacencyCacheRebuilds == 1
                    && profile.signalAdjacencyCacheHits == 0,
            "Signal CSR rebuilds after structural topology mutation");
}
#endif
#endif

void testSimulationWorldUpdate2PartialAppliesMetabolismThenSignals() {
    Environment environment = Environment::constructDefaultEnvironment(2, 0, 1.0);
    environment.aging = false;
    environment.mNitrates = 0.0;
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[cell] = 0.2;
    cellData.mRadius[cell] = 0.02;
    cellData.mNitrates[cell] = 0.5;
    cellData.mToxin[cell] = 1.0f;
    cellData.mSignalSubstanceOut[cell][0] = 0.2f;

    world.update2Partial(1.0);

    assertNear(cellData.mNewMass[cell], cellData.mMass[cell], 0.0, "SimulationWorld update2Partial applies new mass");
    assertNear(cellData.mNewNitrates[cell], cellData.mNitrates[cell], 0.0, "SimulationWorld update2Partial applies new nitrates");
    assertNear(cellData.mNewToxin[cell], cellData.mToxin[cell], 0.0, "SimulationWorld update2Partial applies new toxin");
    assertNear(0.2, cellData.mNewSignalSubstance[cell][0], 1e-7, "SimulationWorld update2Partial updates signals after metabolism");
    assertNear(0.0, cellData.mSignalSubstanceOut[cell][0], 0.0, "SimulationWorld update2Partial resets signal output");
}

void testSimulationWorldFullUpdateStageGuard() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mFoodSpeed = 0.0;
    SimulationWorld world(true, environment);

    assertTrue(world.getUpdateStage() == SimulationWorld::UPDATE_STAGE_CAN_TOUCH, "SimulationWorld initial update stage can touch");

    bool update2TooEarlyThrown = false;
    try {
        world.update2(false, 1.0);
    } catch (const std::runtime_error& error) {
        update2TooEarlyThrown = std::string(error.what()) == "Tried to run update2 too early";
    }
    assertTrue(update2TooEarlyThrown, "SimulationWorld update2 guard before update1");

    world.update1(false, 1.0);
    assertTrue(world.getUpdateStage() == 2, "SimulationWorld update1 advances stage to update2");

    bool update1TooEarlyThrown = false;
    try {
        world.update1(false, 1.0);
    } catch (const std::runtime_error& error) {
        update1TooEarlyThrown = std::string(error.what()) == "Tried to run update1 too early";
    }
    assertTrue(update1TooEarlyThrown, "SimulationWorld update1 guard before update2");

    world.update2(false, 1.0);
    assertTrue(world.getUpdateStage() == SimulationWorld::UPDATE_STAGE_CAN_TOUCH, "SimulationWorld update2 resets stage");

    world.update(false, 1.0);
    assertTrue(world.getUpdateStage() == SimulationWorld::UPDATE_STAGE_CAN_TOUCH, "SimulationWorld update full cycle resets stage");
}

#ifdef CELLULARITY_PERFORMANCE_HUD
void testSimulationWorldStageTimingSamplesOnlyRequestedUpdates() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mFoodSpeed = 0.0;
    SimulationWorld world(true, environment);

    world.requestStageTimingSample();
    world.update(false, 1.0);
    const SimulationWorldStageTimingSnapshot first = world.stageTimingSnapshot();
    double stageSum = 0.0;
    for (double stageNanos : first.stageNanos) {
        assertTrue(stageNanos >= 0.0, "Debug stage timing remains non-negative");
        stageSum += stageNanos;
    }
    assertTrue(first.sequence == 1, "Requested debug stage timing publishes one sample");
    assertTrue(first.totalNanos > 0.0, "Requested debug stage timing measures work");
    assertNear(stageSum, first.totalNanos, 0.0,
            "Debug stage timing total is the sum of every stage");

    world.update(false, 1.0);
    assertTrue(world.stageTimingSnapshot().sequence == first.sequence,
            "Unrequested updates do not sample stage clocks");

    world.requestStageTimingSample();
    world.update(false, 1.0);
    assertTrue(world.stageTimingSnapshot().sequence == first.sequence + 1,
            "A later request publishes exactly one additional sample");
}
#endif

void testCellDataHandleCellSplitCreatesTwoChildrenAndSplitsState() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    const CellRuntimeId parentRuntimeId = cellData.runtimeId(parent);
    const OrganismId parentOrganismId =
            cellData.assignNewOrganism(parent);
    assertTrue(cellData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.25f);
        parentGene.setSplitAngle(0.0f);
        parentGene.setChild1Angle(0.5f);
        parentGene.setChild2Angle(
                static_cast<float>(2.0 * std::acos(-1.0) - 0.25));
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setChild1Mirrored(true);
        parentGene.setChild2Mirrored(false);
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    }),
            "CellData split configures parent gene");
    assertTrue(editGenomeGene(cellData, parent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::GLUEOCYTE);
    }),
            "CellData split configures child1 gene");
    assertTrue(editGenomeGene(cellData, parent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::DEVOROCYTE);
    }),
            "CellData split configures child2 gene");
    cellData.mMass[parent] = 0.2;
    cellData.mRadius[parent] = 0.02;
    cellData.refreshRadiusSq(parent);
    cellData.mAge[parent] = CellData::AGE_OF_CONSENT + 0.1;
    cellData.mNitrates[parent] = 0.8;
    cellData.mToxin[parent] = 0.6f;
    cellData.mToxication[parent] = 0.4f;
    cellData.mSignalSubstance[parent][0] = 0.5f;
    cellData.mTelomeresCount[parent] = -1;
    const cellularity::GenomeId parentGenomeBeforeSplit =
            cellData.genomeId(parent);

    cellData.handleCellSplit(world);

    int child1 = parent;
    int child2 = 1;
    assertTrue(cellData.mCellCount == 2, "CellData split allocates second child");
    assertTrue(cellData.runtimeId(child1) == parentRuntimeId
                    && cellData.runtimeId(child2) != parentRuntimeId
                    && isEntityCellRuntimeId(cellData.runtimeId(child2)),
            "CellData split preserves the continuing child and identifies the new child");
    assertTrue(
            cellData.organismId(child1) == parentOrganismId
                    && cellData.parentOrganismId(child1)
                            == kUnknownOrganismId,
            "Detached split keeps the parent Organism identity on the continuing Cell");
    assertTrue(
            cellData.organismId(child2) != kUnknownOrganismId
                    && cellData.organismId(child2) != parentOrganismId
                    && cellData.parentOrganismId(child2)
                            == parentOrganismId,
            "Detached split creates a child Organism with explicit parent identity");
    const CellData::OrganismIdentitySummary identitySummary =
            cellData.organismIdentitySummary();
    assertTrue(
            identitySummary.knownOrganismCount == 2
                    && identitySummary.complete,
            "Detached split exposes two complete Organism identities");
    assertTrue(cellData.activeGeneIndex(child1) == GeneIndex(1), "CellData split child1 gene index");
    assertTrue(cellData.activeGeneIndex(child2) == GeneIndex(2), "CellData split child2 gene index");
    assertNear(0.2 * 0.75, cellData.mMass[child1], 1e-12, "CellData split child1 mass");
    assertNear(0.2 * 0.25, cellData.mMass[child2], 1e-12, "CellData split child2 mass");
    for (int child : {child1, child2}) {
        assertNear(0.02, cellData.mRadius[child], 0.0, "CellData split inherits the actual parent radius independently of child mass");
        assertNear(0.02 * 0.02, cellData.mRadiusSq[child], 0.0, "CellData split inherits a synchronized radius-squared cache");
        assertNear(0.0, cellData.mAge[child], 0.0, "CellData split resets daughter age for radius relaxation");
    }
    assertNear(0.8 * 0.75, cellData.mNitrates[child1], 1e-12, "CellData split child1 nitrates");
    assertNear(0.8 * 0.25, cellData.mNitrates[child2], 1e-12, "CellData split child2 nitrates");
    assertNear(0.6 * 0.75, cellData.mToxin[child1], 1e-7, "CellData split child1 toxin");
    assertNear(0.6 * 0.25, cellData.mToxin[child2], 1e-7, "CellData split child2 toxin");
    assertNear(0.4 * 0.75, cellData.mToxication[child1], 1e-7, "CellData split child1 toxication");
    assertNear(0.4 * 0.25, cellData.mToxication[child2], 1e-7, "CellData split child2 toxication");
    assertNear(0.5 * 0.75 + SimulationWorld::SIGNAL_SUBSTANCE_SPLIT_NUDGE, cellData.mSignalSubstance[child1][0], 1e-7, "CellData split child1 signal");
    assertNear(0.5 * 0.25 + SimulationWorld::SIGNAL_SUBSTANCE_SPLIT_NUDGE, cellData.mSignalSubstance[child2][0], 1e-7, "CellData split child2 signal");
    assertNear(0.5, cellData.mAngle[child1], 1e-12, "CellData split child1 angle");
    assertNear(2.0 * std::acos(-1.0) - 0.25, cellData.mAngle[child2], 1e-6, "CellData split child2 angle");
    assertTrue(cellData.mIsMirrored[child1], "CellData split child1 mirror xor");
    assertTrue(!cellData.mIsMirrored[child2], "CellData split child2 mirror xor");
    assertTrue(!cellData.mIsDead[child1], "CellData split child1 alive");
    assertTrue(!cellData.mIsDead[child2], "CellData split child2 alive");
    assertTrue(cellData.genomeId(child1) == parentGenomeBeforeSplit
                    && cellData.genomeId(child2) == parentGenomeBeforeSplit,
            "CellData split retains one shared GenomeRecord in O(1)");
    assertTrue(cellData.genomeReferenceCount(child1) == 2,
            "CellData split adds exactly one GenomeRecord owner");

    const float originalChild2Color =
            cellData.getGeneView(child1, GeneIndex(2)).color(0);
    assertTrue(cellData.editGene(
                       child2,
                       GeneIndex(2),
                       [](GeneBuilder& gene) {
                           gene.setColor(0, 0.8125f);
                       }),
            "CellData child point mutation succeeds");
    assertTrue(cellData.genomeId(child1) != cellData.genomeId(child2),
            "First child write detaches only the mutated lineage");
    assertNear(originalChild2Color,
            cellData.getGeneView(child1, GeneIndex(2)).color(0),
            0.0,
            "Detached child mutation leaves the sibling Genome unchanged");
    assertNear(0.8125f,
            cellData.getGeneView(child2, GeneIndex(2)).color(0),
            0.0,
            "Detached child observes the replacement GeneId");
    assertTrue(cellData.genomePool().validateInvariants().isValid()
                    && cellData.genePool().validateInvariants().isValid(),
            "Split and mutation preserve pooled ownership invariants");
}

int addNonGrowingSplitParent(SimulationWorld& world,
        float ratio,
        bool makesLink,
        float child1Cytoskeleton = 1.0f) {
    CellData& cells = world.mCellData;
    const int parent = addAliveCell(cells, CellType::PHAGOCYTE);
    assertTrue(cells.editActiveGene(parent, [=](GeneBuilder& gene) {
        gene.setSplitMass(0.2f);
        gene.setSplitRatio(ratio);
        gene.setChild1GeneIndex(GeneIndex(1));
        gene.setChild2GeneIndex(GeneIndex(2));
        gene.setMakesLink(makesLink);
    }),
            "Newborn radius fixture configures the parent");
    for (int geneIndex : {1, 2}) {
        assertTrue(editGenomeGene(cells, parent, geneIndex, [=](GeneBuilder& gene) {
            gene.setCellType(CellType::PHAGOCYTE);
            gene.setSplitMass(CellData::MAX_SPLIT_MASS);
            gene.setFloatValue(Gene::FloatType::CYTOSKELETON,
                    geneIndex == 1 ? child1Cytoskeleton : 1.0f);
        }),
                "Newborn radius fixture configures non-dividing daughters");
    }
    makeCellSplitReady(cells, parent, CellData::MAX_MASS);
    return parent;
}

void testCellDataNewbornRadiusRelaxationMatchesJavaAgeGateAndFormula() {
    for (double deltaTime : {0.02, 0.005}) {
        SimulationWorld world(false, Environment::constructDefaultEnvironment(4, 0, 1.0));
        CellData& cells = world.mCellData;
        const int parent = addNonGrowingSplitParent(world, 0.4f, false);
        constexpr double parentRadius = 0.025;
        cells.mRadius[parent] = parentRadius;
        cells.refreshRadiusSq(parent);
        cells.mVelocityX[parent] = 0.15;
        cells.handleCellSplit(world);
        assertTrue(cells.mCellCount == 2, "Newborn radius fixture divides once");
        for (int child : {0, 1}) {
            assertNear(0.0, cells.mAge[child], 0.0, "Newborn daughter starts at age zero");
            for (double age : {0.0, deltaTime * 5.0}) {
                cells.mAge[child] = age;
                const double positionBefore = cells.mPositionX[child];
                cells.updateCellsKinematics(child, deltaTime);
                assertNear(parentRadius, cells.mRadius[child], 0.0, "Newborn radius stays inherited through the strict Java age threshold");
                assertNear(parentRadius * parentRadius, cells.mRadiusSq[child], 0.0, "Unchanged newborn radius keeps its cache synchronized");
                assertNear(positionBefore + 0.15 * deltaTime, cells.mPositionX[child], 0.0, "Newborn movement continues while radius relaxation is delayed");
                assertTrue(!cells.mIsDead[child], "Valid newborn survives age-gated kinematics");
            }
            cells.mAge[child] = std::nextafter(deltaTime * 5.0, std::numeric_limits<double>::infinity());
            const double expectedRadius = parentRadius
                    + (cells.mMass[child] / CellData::MASS_PER_AREA / parentRadius - parentRadius)
                            * 0.5 * CellData::RADIUS_CHANGE_SPEED * deltaTime;
            cells.updateCellsKinematics(child, deltaTime);
            assertTrue(std::isfinite(cells.mRadius[child]), "Relaxed newborn radius remains finite");
            assertNear(expectedRadius, cells.mRadius[child], 0.0, "Newborn radius uses the exact archived Java relaxation formula");
            assertNear(expectedRadius * expectedRadius, cells.mRadiusSq[child], 0.0, "Relaxed newborn radius updates its squared cache");
        }
    }
}

void testSimulationWorldNewbornRadiiPreserveViableDaughtersThroughFullUpdates() {
    for (bool makesLink : {false, true}) {
        for (float ratio : {0.3f, 0.4f, 0.5f}) {
            for (float child1Cytoskeleton : {1.0f, 2.0f}) {
                Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
                environment.aging = false;
                environment.mFoodSpeed = 0.0;
                environment.mTestIdleMetabolismScale = 0.0;
                SimulationWorld world(false, environment);
                CellData& cells = world.mCellData;
                const int parent = addNonGrowingSplitParent(world, ratio, makesLink, child1Cytoskeleton);
                const double parentRadius = cells.mRadius[parent];
                for (int update = 0; update < 100; ++update) {
                    world.update(false, 1.0);
                    assertTrue(cells.mCellCount == 2 && !cells.mIsDead[0] && !cells.mIsDead[1], "Both viable daughters survive the complete production pipeline");
                    for (int child : {0, 1}) {
                        assertTrue(std::isfinite(cells.mPositionX[child]) && std::isfinite(cells.mPositionY[child]) && std::isfinite(cells.mRadius[child]), "Newborn production state remains finite");
                        assertNear(cells.mRadius[child] * cells.mRadius[child], cells.mRadiusSq[child], 0.0, "Full updates keep the newborn radius cache synchronized");
                        if (update == 0) {
                            assertNear(parentRadius, cells.mRadius[child], 0.0, "Full split update preserves the actual parent radius");
                        }
                    }
                }
            }
        }
    }
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.aging = false;
    environment.mTestIdleMetabolismScale = 0.0;
    SimulationWorld world(false, environment);
    addNonGrowingSplitParent(world, 0.1f, false);
    world.update(false, 1.0);
    world.update(false, 1.0);
    assertTrue(world.mCellData.mCellCount == 1 && !world.mCellData.mIsDead[0], "Inherited newborn radius does not suppress legitimate underweight death");
}

void testCellDataHandleCellSplitSkipsInvalidCasesAndTelomereDeath() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld gameteWorld(false, environment);
    CellData& gameteData = gameteWorld.mCellData;
    int gamete = addAliveCell(gameteData, CellType::GAMETE);
    gameteData.editActiveGene(gamete, [](GeneBuilder& gene) {
        gene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        gene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    gameteData.mAge[gamete] = CellData::AGE_OF_CONSENT + 1.0;
    gameteData.handleCellSplit(gameteWorld);
    assertTrue(gameteData.mCellCount == 1, "CellData split skips gametes");

    SimulationWorld telomereWorld(false, environment);
    CellData& telomereData = telomereWorld.mCellData;
    int parent = addAliveCell(telomereData, CellType::PHAGOCYTE);
    telomereData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    editGenomeGene(telomereData, parent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    editGenomeGene(telomereData, parent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    telomereData.mMass[parent] = 0.2;
    telomereData.mAge[parent] = CellData::AGE_OF_CONSENT + 1.0;
    telomereData.mNitrates[parent] = 1.0;
    telomereData.mTelomeresCount[parent] = 1;

    telomereData.handleCellSplit(telomereWorld);

    assertTrue(telomereData.mCellCount == 2, "CellData split telomere still allocates child");
    assertTrue(telomereData.mIsDead[0], "CellData split telomere death child1");
    assertTrue(telomereData.mIsDead[1], "CellData split telomere death child2");
}

void testCellDataHandleCellSplitCreatesSiblingLink() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    const OrganismId parentOrganismId =
            cellData.assignNewOrganism(parent);
    cellData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setMakesLink(true);
        parentGene.setLinkStrength(0.25f);
        parentGene.setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.01f);
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    editGenomeGene(cellData, parent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    editGenomeGene(cellData, parent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    cellData.mMass[parent] = 0.2;
    cellData.mAge[parent] = CellData::AGE_OF_CONSENT + 1.0;
    cellData.mNitrates[parent] = 1.0;
    cellData.mTelomeresCount[parent] = -1;

    cellData.handleCellSplit(world);

    assertTrue(cellData.mTopology.linkCount(0) == 1, "CellData split child1 sibling link count");
    assertTrue(cellData.mTopology.linkCount(1) == 1, "CellData split child2 sibling link count");
    assertTrue(cellData.mTopology.isLinked(0, 1), "CellData split sibling target");
    assertTrue(
            cellData.mTopology.edgeCreationReason(0)
                    == CellLinkCreationReason::SPLIT_SIBLING,
            "CellData split records the sibling link reason");
    assertTrue(
            cellData.organismId(0) == parentOrganismId
                    && cellData.organismId(1) == parentOrganismId
                    && cellData.organismIdentitySummary().knownOrganismCount
                            == 1,
            "Sibling-linked split preserves one Organism identity");
    assertNear(0.25, cellData.mTopology.edgeTorsionalStiffness(0), 0.0, "CellData split sibling link stiffness");
    assertNear(0.01, cellData.mTopology.edgeRestLength(0), 1e-7, "CellData split sibling link rest length");
}

void testCellDataHandleCellSplitReassignsKeptExternalLinks() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    int external = addAliveCell(cellData, CellType::PHAGOCYTE);
    const OrganismId parentOrganismId =
            cellData.assignNewOrganism(parent);
    cellData.mOrganismIds[external] = parentOrganismId;
    cellData.mParentOrganismIds[external] = kUnknownOrganismId;
    cellData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setChild2KeepsLinks(true);
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    editGenomeGene(cellData, parent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    editGenomeGene(cellData, parent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    cellData.mMass[parent] = 0.2;
    cellData.mAge[parent] = CellData::AGE_OF_CONSENT + 1.0;
    cellData.mNitrates[parent] = 1.0;
    cellData.mTelomeresCount[parent] = -1;
    cellData.mPositionX[parent] = 0.0;
    cellData.mPositionY[parent] = 0.0;
    cellData.mPositionX[external] = 0.1;
    cellData.mPositionY[external] = 0.0;
    assertTrue(
            cellData.addLink(
                    parent,
                    external,
                    CellLinkCreationReason::SPLIT_SIBLING,
                    0.0,
                    std::acos(-1.0),
                    0.3f,
                    0.01f)
                    == 0,
            "CellData split inheritance fixture creates its source link");
    cellData.mOldLinkCount[parent] = 1;

    cellData.handleCellSplit(world);

    int child2 = 2;
    assertTrue(cellData.mTopology.linkCount(parent) == 0, "CellData split child1 does not keep right-side external link");
    assertTrue(cellData.mTopology.linkCount(child2) == 1, "CellData split child2 keeps right-side external link");
    assertTrue(cellData.mTopology.isLinked(child2, external), "CellData split child2 external link target");
    assertTrue(
            cellData.mTopology.edgeCreationReason(0)
                    == CellLinkCreationReason::SPLIT_INHERITANCE,
            "CellData split records the inherited link reason");
    assertTrue(cellData.mTopology.linkCount(external) == 1, "CellData split external reciprocal count");
    assertTrue(
            cellData.organismId(child2) == parentOrganismId
                    && cellData.organismId(parent) != parentOrganismId
                    && cellData.parentOrganismId(parent)
                            == parentOrganismId,
            "The child retaining an inherited split link keeps the parent Organism identity");
}

void testCellDataSplitSeparatesDisconnectedInheritedBranches() {
    // A daughter can retain links without retaining a path to its sibling.
    // Each daughter receives one side of the former organism, so both
    // immediate continuation checks succeed even though the organism has
    // split into two components.
    Environment branchedEnvironment =
            Environment::constructDefaultEnvironment(6, 0, 1.0);
    SimulationWorld branchedWorld(false, branchedEnvironment);
    CellData& branched = branchedWorld.mCellData;
    const int branchedParent =
            addAliveCell(branched, CellType::PHAGOCYTE);
    const int leftPeer = addAliveCell(branched, CellType::PHAGOCYTE);
    const int rightPeer = addAliveCell(branched, CellType::PHAGOCYTE);
    const OrganismId branchedParentOrganism =
            branched.assignNewOrganism(branchedParent);
    for (const int peer : {leftPeer, rightPeer}) {
        branched.mOrganismIds[peer] = branchedParentOrganism;
        branched.mParentOrganismIds[peer] = kUnknownOrganismId;
    }
    branched.editActiveGene(branchedParent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setMakesLink(false);
        parentGene.setChild1KeepsLinks(true);
        parentGene.setChild2KeepsLinks(true);
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    editGenomeGene(branched, branchedParent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    editGenomeGene(branched, branchedParent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    branched.mPositionX[branchedParent] = 0.0;
    branched.mPositionY[branchedParent] = 0.0;
    branched.mAngle[branchedParent] = 0.0;
    branched.mPositionX[leftPeer] = -0.1;
    branched.mPositionY[leftPeer] = 0.0;
    branched.mPositionX[rightPeer] = 0.1;
    branched.mPositionY[rightPeer] = 0.0;
    assertTrue(
            branched.addLink(
                    branchedParent,
                    leftPeer,
                    CellLinkCreationReason::SPLIT_SIBLING,
                    std::acos(-1.0),
                    0.0,
                    0.3f,
                    0.01f) >= 0
                    && branched.addLink(
                               branchedParent,
                               rightPeer,
                               CellLinkCreationReason::SPLIT_SIBLING,
                               0.0,
                               std::acos(-1.0),
                               0.3f,
                               0.01f)
                            >= 0,
            "Branched split fixture creates links on both sides");
    branched.mOldLinkCount[branchedParent] = 2;
    makeCellSplitReady(branched, branchedParent, 0.2);

    branched.handleCellSplit(branchedWorld);

    const int branchedChild2 = 3;
    const OrganismId detachedOrganism =
            branched.organismId(branchedChild2);
    assertTrue(
            branched.mTopology.isLinked(branchedParent, leftPeer)
                    && branched.mTopology.isLinked(
                            branchedChild2, rightPeer)
                    && !branched.mTopology.isLinked(
                            branchedParent, branchedChild2),
            "Branched split produces two disconnected inherited components");
    assertTrue(
            branched.organismId(branchedParent)
                            == branchedParentOrganism
                    && branched.organismId(leftPeer)
                            == branchedParentOrganism
                    && detachedOrganism != kUnknownOrganismId
                    && detachedOrganism != branchedParentOrganism
                    && branched.organismId(rightPeer) == detachedOrganism
                    && branched.parentOrganismId(branchedChild2)
                            == branchedParentOrganism
                    && branched.parentOrganismId(rightPeer)
                            == branchedParentOrganism,
            "Disconnected inherited branch becomes one descendant Organism");
    assertTrue(
            branched.organismIdentitySummary().knownOrganismCount == 2
                    && branched.validateOrganismIdentities(),
            "Branched split keeps complete valid Organism lineage metadata");
}

void testCellDataSplitClassifiesDroppedFormerBranch() {
    // rsh-0 reaches this topology: the daughters connect to each other while
    // the parent's former link to the developed body is not inherited.
    Environment severedEnvironment =
            Environment::constructDefaultEnvironment(5, 0, 1.0);
    SimulationWorld severedWorld(false, severedEnvironment);
    CellData& severed = severedWorld.mCellData;
    const int severedParent = addAliveCell(
            severed, CellType::PHAGOCYTE);
    const int severedPeer = addAliveCell(
            severed, CellType::PHAGOCYTE);
    const OrganismId severedParentOrganism =
            severed.assignNewOrganism(severedParent);
    severed.mOrganismIds[severedPeer] = severedParentOrganism;
    severed.mParentOrganismIds[severedPeer] = kUnknownOrganismId;
    severed.editActiveGene(severedParent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setMakesLink(true);
        parentGene.setChild1KeepsLinks(false);
        parentGene.setChild2KeepsLinks(false);
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    editGenomeGene(severed, severedParent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    editGenomeGene(severed, severedParent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    assertTrue(
            severed.addLink(
                    severedParent,
                    severedPeer,
                    CellLinkCreationReason::SPLIT_SIBLING,
                    0.0,
                    std::acos(-1.0),
                    0.3f,
                    0.01f)
                    >= 0,
            "Severed split fixture creates its former Organism link");
    severed.mOldLinkCount[severedParent] = 1;
    makeCellSplitReady(severed, severedParent, 0.2);

    severed.handleCellSplit(severedWorld);

    const int severedChild2 = 2;
    const OrganismId severedDaughterOrganism =
            severed.organismId(severedParent);
    assertTrue(
            severed.mTopology.isLinked(
                    severedParent, severedChild2)
                    && !severed.mTopology.isLinked(
                            severedParent, severedPeer)
                    && !severed.mTopology.isLinked(
                            severedChild2, severedPeer),
            "Sibling-linked daughters can sever a former Organism branch");
    assertTrue(
            severed.organismId(severedPeer)
                            == severedParentOrganism
                    && severedDaughterOrganism != kUnknownOrganismId
                    && severedDaughterOrganism != severedParentOrganism
                    && severed.organismId(severedChild2)
                            == severedDaughterOrganism
                    && severed.parentOrganismId(severedParent)
                            == severedParentOrganism
                    && severed.parentOrganismId(severedChild2)
                            == severedParentOrganism
                    && severed.organismIdentitySummary().knownOrganismCount
                            == 2
                    && severed.validateOrganismIdentities(),
            "A severed daughter pair becomes one descendant Organism");
}

void testCellDataSplitFromUnknownIdentityDoesNotReconstructLegacyOrganism() {
    Environment environment = Environment::constructDefaultEnvironment(
            4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int legacyPeer = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setMakesLink(false);
        parentGene.setChild2KeepsLinks(true);
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    editGenomeGene(cellData, parent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    editGenomeGene(cellData, parent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    cellData.mPositionX[parent] = 0.0;
    cellData.mPositionY[parent] = 0.0;
    cellData.mPositionX[legacyPeer] = 0.1;
    cellData.mPositionY[legacyPeer] = 0.0;
    assertTrue(
            cellData.addLink(
                    parent,
                    legacyPeer,
                    CellLinkCreationReason::UNSPECIFIED,
                    0.0,
                    std::acos(-1.0),
                    0.3f,
                    0.01f)
                    == 0,
            "Unknown legacy identity fixture creates an unclassified link");
    makeCellSplitReady(cellData, parent, 0.2);

    cellData.handleCellSplit(world);

    const int child2 = 2;
    assertTrue(
            cellData.organismId(parent) == kUnknownOrganismId
                    && cellData.parentOrganismId(parent)
                            == kUnknownOrganismId,
            "Unknown legacy identity remains unknown after a split");
    assertTrue(
            cellData.organismId(child2) != kUnknownOrganismId
                    && cellData.parentOrganismId(child2)
                            == kUnknownOrganismId,
            "A detached post-load descendant receives a known identity without an invented parent");
    assertTrue(
            cellData.mTopology.isLinked(child2, legacyPeer)
                    && cellData.mTopology.edgeCreationReason(0)
                            == CellLinkCreationReason::UNSPECIFIED
                    && cellData.organismId(legacyPeer)
                            == kUnknownOrganismId,
            "An unclassified legacy link is preserved without becoming Organism evidence");
    const CellData::OrganismIdentitySummary summary =
            cellData.organismIdentitySummary();
    assertTrue(
            summary.knownOrganismCount == 1 && !summary.complete,
            "Mixed legacy and post-load identities report incomplete coverage");
}

void testCellDataInheritedGlueocyteLinkDoesNotMergeOrganisms() {
    Environment environment = Environment::constructDefaultEnvironment(
            4, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    const int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    const int external = addAliveCell(cellData, CellType::GLUEOCYTE);
    const OrganismId parentOrganismId =
            cellData.assignNewOrganism(parent);
    const OrganismId externalOrganismId =
            cellData.assignNewOrganism(external);
    cellData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setMakesLink(false);
        parentGene.setChild2KeepsLinks(true);
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    editGenomeGene(cellData, parent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    editGenomeGene(cellData, parent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    cellData.mPositionX[parent] = 0.0;
    cellData.mPositionY[parent] = 0.0;
    cellData.mPositionX[external] = 0.1;
    cellData.mPositionY[external] = 0.0;
    assertTrue(
            cellData.addLink(
                    parent,
                    external,
                    CellLinkCreationReason::GLUEOCYTE_COLLISION,
                    0.0,
                    std::acos(-1.0),
                    0.3f,
                    0.01f)
                    == 0,
            "Glueocyte inheritance fixture creates its source link");
    makeCellSplitReady(cellData, parent, 0.2);

    cellData.handleCellSplit(world);

    const int child2 = 2;
    assertTrue(
            cellData.mTopology.isLinked(child2, external)
                    && cellData.mTopology.edgeCreationReason(0)
                            == CellLinkCreationReason::GLUEOCYTE_INHERITANCE,
            "A retained Glueocyte link remains explicitly non-organism-forming");
    assertTrue(
            cellData.organismId(parent) == parentOrganismId
                    && cellData.organismId(child2) != parentOrganismId
                    && cellData.parentOrganismId(child2)
                            == parentOrganismId
                    && cellData.organismId(external)
                            == externalOrganismId,
            "Glueocyte inheritance neither preserves nor merges Organism identity");
    assertTrue(
            cellData.organismIdentitySummary().knownOrganismCount == 3,
            "Glueocyte-connected parent, descendant, and peer remain three Organisms");
}

void testCellDataHandleCellSplitKillsFlagellocyteWithInwardLink() {
    Environment environment = Environment::constructDefaultEnvironment(3, 0, 1.0);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.editActiveGene(parent, [](GeneBuilder& parentGene) {
        parentGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        parentGene.setSplitRatio(0.5f);
        parentGene.setChild1GeneIndex(GeneIndex(1));
        parentGene.setChild2GeneIndex(GeneIndex(2));
        parentGene.setChild1Angle(static_cast<float>(std::acos(-1.0)));
        parentGene.setMakesLink(true);
        parentGene.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                CellData::MAX_NEIGHBORS - 1);
    });
    editGenomeGene(cellData, parent, 1, [](GeneBuilder& gene) {
        gene.setCellType(CellType::FLAGELLOCYTE);
    });
    editGenomeGene(cellData, parent, 2, [](GeneBuilder& gene) {
        gene.setCellType(CellType::PHAGOCYTE);
    });
    cellData.mMass[parent] = 0.2;
    cellData.mAge[parent] = CellData::AGE_OF_CONSENT + 1.0;
    cellData.mNitrates[parent] = 1.0;
    cellData.mTelomeresCount[parent] = -1;

    cellData.handleCellSplit(world);

    assertTrue(cellData.mIsDead[0], "CellData split kills flagellocyte with inward sibling link");
    assertTrue(!cellData.mIsDead[1], "CellData split keeps non-flagellocyte sibling alive");
}

void testCellDataAddRandomCellInitializesDeterministicallyAndRespectsGenePool() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mGenePool.fill(false);
    environment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    environment.mGenePool[CellTypes::ordinal(CellType::LIPOCYTE)] = true;
    environment.mRandom.setSeed(123456);

    CellData first(4);
    first.addRandomCell(0.125, -0.25, environment);

    Environment sameEnvironment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    sameEnvironment.mGenePool.fill(false);
    sameEnvironment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    sameEnvironment.mGenePool[CellTypes::ordinal(CellType::LIPOCYTE)] = true;
    sameEnvironment.mRandom.setSeed(123456);
    CellData second(4);
    second.addRandomCell(0.125, -0.25, sameEnvironment);

    assertTrue(first.mCellCount == 1, "CellData addRandomCell allocates cell");
    assertTrue(!first.mIsDead[0], "CellData addRandomCell marks alive");
    assertNear(0.125, first.mPositionX[0], 0.0, "CellData addRandomCell positionX");
    assertNear(-0.25, first.mPositionY[0], 0.0, "CellData addRandomCell positionY");
    assertNear(CellData::MAX_MASS, first.mMass[0], 1e-12, "CellData addRandomCell mass");
    assertNear(std::sqrt(CellData::MAX_MASS / CellData::MASS_PER_AREA), first.mRadius[0], 1e-12, "CellData addRandomCell radius");
    assertTrue(first.mOriginTag[0] == SimulationWorld::RANDOM_CELL_ORIGIN, "CellData addRandomCell origin");
    assertTrue(first.mNitrates[0] == CellData::MAX_NITRATES, "CellData addRandomCell nitrates");
    assertTrue(first.mTelomeresCount[0] == -1, "CellData addRandomCell telomeres");
    const std::size_t founderGeneCount = first.geneCount(0);
    assertTrue(founderGeneCount >= 1 && founderGeneCount <= 40,
            "CellData addRandomCell uses the configured 1..40 genome range");
    assertTrue(founderGeneCount == second.geneCount(0),
            "CellData addRandomCell deterministically selects genome length");
    const int activeGeneIndex = first.activeGeneIndex(0).value();
    assertTrue(activeGeneIndex >= 0
                    && activeGeneIndex < static_cast<int>(first.geneCount(0)),
            "CellData addRandomCell active gene range");
    assertTrue(activeGeneIndex == 0, "CellData addRandomCell starts from genome root");
    const GeneView activeSeed = first.getActiveGeneView(0);
    assertNear(activeSeed.color(0), first.mColor[0][0], 0.0, "CellData addRandomCell active red");
    assertNear(activeSeed.color(1), first.mColor[0][1], 0.0, "CellData addRandomCell active green");
    assertNear(activeSeed.color(2), first.mColor[0][2], 0.0, "CellData addRandomCell active blue");
    assertNear(1.0, first.mColor[0][3], 0.0, "CellData addRandomCell alpha");

    for (std::size_t geneIndex = 0; geneIndex < founderGeneCount; geneIndex++) {
        const GeneView gene = viewGenomeGene(
                first, 0, static_cast<int>(geneIndex));
        assertTrue(gene.cellType() == CellType::PHAGOCYTE
                        || gene.cellType() == CellType::LIPOCYTE,
                "CellData addRandomCell respects gene pool across its genome");
        assertTrue(gene.child1GeneIndex().value() < founderGeneCount
                        && gene.child2GeneIndex().value() < founderGeneCount
                        && gene.virusFromGeneIndex().value() < founderGeneCount
                        && gene.virusToGeneIndex().value() < founderGeneCount,
                "CellData addRandomCell keeps every gene reference in range");
    }
    const GeneView founder = viewGenomeGene(first, 0, 0);
    assertTrue(founder.splitMass() >= CellData::MIN_SPLIT_MASS
                    && founder.splitMass() < CellData::MAX_MASS,
            "CellData addRandomCell founder is splittable");
    assertTrue(founder.massFlowPriority() >= CellData::MIN_MASS_FLOW_PRIORITY
                    && founder.massFlowPriority() <= CellData::MAX_MASS_FLOW_PRIORITY,
            "CellData addRandomCell seeds valid mass flow priority");
    assertTrue(founder.integerValue(Gene::TELOMERASE_LENGTH_INDEX) == 0,
            "CellData addRandomCell seeds telomerase only for gamete");

    assertTrue(first.activeGeneIndex(0) == second.activeGeneIndex(0), "CellData addRandomCell deterministic gene index");
    assertNear(first.mAngle[0], second.mAngle[0], 0.0, "CellData addRandomCell deterministic angle");
    assertTrue(sameGenome(first.materializeGenome(0), second.materializeGenome(0)),
            "CellData addRandomCell deterministically creates the founder genome");

    Environment variedEnvironment =
            Environment::constructDefaultEnvironment(64, 0, 1.0);
    variedEnvironment.mGenePool.fill(false);
    variedEnvironment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    variedEnvironment.mRandom.setSeed(314159);
    CellData varied(64);
    std::size_t minimumObservedCount = 40;
    std::size_t maximumObservedCount = 1;
    for (int sample = 0; sample < 64; sample++) {
        varied.addRandomCell(0.0, 0.0, variedEnvironment);
        const std::size_t count = varied.geneCount(sample);
        assertTrue(count >= 1 && count <= 40,
                "CellData addRandomCell sampled genome length remains in range");
        minimumObservedCount = std::min(minimumObservedCount, count);
        maximumObservedCount = std::max(maximumObservedCount, count);
    }
    assertTrue(minimumObservedCount < maximumObservedCount,
            "CellData addRandomCell does not collapse to one fixed genome length");
}

void testCellDataAddRandomCellTargetsGameteCompatibilityToGameteGenes() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mGenePool.fill(false);
    environment.mGenePool[CellTypes::ordinal(CellType::GAMETE)] = true;
    environment.mRandom.setSeed(90210);

    CellData cellData(4);
    cellData.addRandomCell(0.0, 0.0, environment);

    const std::size_t geneCount = cellData.geneCount(0);
    assertTrue(geneCount >= 1 && geneCount <= 40,
            "CellData addRandomCell gamete founder length remains in range");
    for (std::size_t geneIndex = 0; geneIndex < geneCount; geneIndex++) {
        const GeneView gene = viewGenomeGene(
                cellData, 0, static_cast<int>(geneIndex));
        assertTrue(gene.cellType() == CellType::GAMETE,
                "CellData addRandomCell respects gamete-only pool");
        assertTrue(gene.virusToGeneIndex().value() < geneCount,
                "CellData addRandomCell gamete target remains in range");
        assertTrue(
                viewGenomeGene(
                        cellData, 0, gene.virusToGeneIndex().value())
                                .cellType()
                        == CellType::GAMETE,
                "CellData addRandomCell gamete target resolves to a gamete gene");
    }
}

void testCellDataAddRandomCellsGatesAndMaintainsPopulation() {
    Environment environment = Environment::constructDefaultEnvironment(20, 0, 1.0);
    environment.addRandom = false;
    environment.mRandom.setSeed(77);
    CellData cellData(20);

    cellData.addRandomCells(environment);

    assertTrue(cellData.mCellCount == 0, "CellData addRandomCells skips when disabled");

    environment.addRandom = true;
    environment.mGenePool.fill(false);
    cellData.addRandomCells(environment);
    assertTrue(cellData.mCellCount == 0, "CellData addRandomCells skips empty gene pool");

    environment.mGenePool[CellTypes::ordinal(CellType::PHAGOCYTE)] = true;
    cellData.addRandomCells(environment);

    assertTrue(cellData.mCellCount >= cellData.RANDOM_CELLS_MAINTAIN_COUNT, "CellData addRandomCells reaches maintain count");
    for (int cellIndex = 0; cellIndex < cellData.mCellCount; cellIndex++) {
        double distanceSq = cellData.mPositionX[cellIndex] * cellData.mPositionX[cellIndex]
                + cellData.mPositionY[cellIndex] * cellData.mPositionY[cellIndex];
        assertTrue(distanceSq <= environment.mRadius * environment.mRadius, "CellData addRandomCells places inside world");
    }
}

void testCellDataUpdateCellsMetabolismMutationBranchUsesGeneMutate() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.aging = false;
    environment.mRadiation = 1000.0;
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(999);
    CellData cellData(1);
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[cell] = CellData::INITIAL_MASS;
    cellData.mRadius[cell] = std::sqrt(cellData.mMass[cell] / CellData::MASS_PER_AREA);
    cellData.mNitrates[cell] = CellData::MAX_NITRATES;
    cellData.mSeldomCounter[cell] = CellData::MUTATE_PERIOD - 1;

    cellData.updateCellsMetabolism(SimulationWorld::DELTA_TIME, environment);

    assertTrue(cellData.mSeldomCounter[cell] == 0, "CellData metabolism mutation resets seldom counter");
    assertTrue(cellData.mMutationCount[cell] > 0, "CellData metabolism mutation increments mutation count");
}

void testCellDataMutationWeightsCanDisableAllMutations() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.aging = false;
    environment.mRadiation = 1000.0;
    environment.mTestMutationCoef = 1.0;
    environment.mMutationWeights.fill(0.0);
    environment.mRandom.setSeed(999);
    CellData cellData(1);
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[cell] = CellData::INITIAL_MASS;
    cellData.mRadius[cell] = std::sqrt(cellData.mMass[cell] / CellData::MASS_PER_AREA);
    cellData.mNitrates[cell] = CellData::MAX_NITRATES;
    cellData.mSeldomCounter[cell] = CellData::MUTATE_PERIOD - 1;
    auto originalGenome = copyGenome(cellData, cell);

    cellData.updateCellsMetabolism(SimulationWorld::DELTA_TIME, environment);

    assertTrue(cellData.mSeldomCounter[cell] == 0, "CellData disabled mutation weights still reset seldom counter");
    assertTrue(cellData.mMutationCount[cell] == 0, "CellData disabled mutation weights suppress mutation count");
    assertTrue(cellData.mPendingMutationCount[cell] == 0, "CellData disabled mutation weights suppress pending mutations");
    assertTrue(sameGenome(originalGenome, cellData, cell), "CellData disabled mutation weights leave genome unchanged");
}

void testCellDataDelayedMutationsAccumulateWithoutChangingGenome() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.aging = false;
    environment.mRadiation = 1000.0;
    environment.mTestMutationCoef = 1.0;
    environment.mutationOnlyAfterSplit = true;
    environment.mRandom.setSeed(999);
    CellData cellData(1);
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.mMass[cell] = CellData::INITIAL_MASS;
    cellData.mRadius[cell] = std::sqrt(cellData.mMass[cell] / CellData::MASS_PER_AREA);
    cellData.mNitrates[cell] = CellData::MAX_NITRATES;
    cellData.mSeldomCounter[cell] = CellData::MUTATE_PERIOD - 1;
    auto originalGenome = copyGenome(cellData, cell);

    cellData.updateCellsMetabolism(SimulationWorld::DELTA_TIME, environment);

    assertTrue(cellData.mSeldomCounter[cell] == 0, "CellData delayed mutation resets seldom counter");
    assertTrue(cellData.mMutationCount[cell] > 0, "CellData delayed mutation increments historical count");
    assertTrue(cellData.mPendingMutationCount[cell] == cellData.mMutationCount[cell],
            "CellData delayed mutation accumulates pending count");
    assertTrue(sameGenome(originalGenome, cellData, cell),
            "CellData delayed mutation leaves genome unchanged before split");
}

void testCellDataDelayedMutationsApplyToDaughterGenomesOnSplit() {
    Environment environment = Environment::constructDefaultEnvironment(4, 0, 1.0);
    environment.mutationOnlyAfterSplit = true;
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(202707);
    SimulationWorld world(false, environment);
    CellData& cellData = world.mCellData;
    int parent = addAliveCell(cellData, CellType::PHAGOCYTE);
    cellData.editActiveGene(parent, [](GeneBuilder& activeGene) {
        activeGene.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        activeGene.setSplitRatio(0.5f);
        activeGene.setChild1GeneIndex(GeneIndex(0));
        activeGene.setChild2GeneIndex(GeneIndex(0));
        activeGene.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    });
    cellData.mMass[parent] = CellData::INITIAL_MASS;
    cellData.mRadius[parent] = std::sqrt(cellData.mMass[parent] / CellData::MASS_PER_AREA);
    cellData.mNitrates[parent] = CellData::MAX_NITRATES;
    cellData.mAge[parent] = CellData::AGE_OF_CONSENT + 1.0;
    cellData.mPendingMutationCount[parent] = 100;
    cellData.mMutationCount[parent] = 100;
    auto originalGenome = copyGenome(cellData, parent);

    cellData.handleCellSplit(world);

    assertTrue(cellData.mCellCount == 2, "CellData delayed split creates two children");
    assertTrue(cellData.mPendingMutationCount[0] == 0, "CellData delayed split clears parent pending mutations");
    assertTrue(cellData.mPendingMutationCount[1] == 0, "CellData delayed split clears child pending mutations");
    assertTrue(cellData.mMutationCount[0] == 100, "CellData delayed split preserves parent historical mutation count");
    assertTrue(cellData.mMutationCount[1] == 100, "CellData delayed split copies historical mutation count to child");
    assertTrue(!sameGenome(originalGenome, cellData, 0),
            "CellData delayed split applies pending mutations to parent genome");
    assertTrue(sameGenome(
                       cellData.materializeGenome(0),
                       cellData.materializeGenome(1)),
            "CellData delayed split gives both daughters the same mutated genome");
}

void testCellDataStructuralGenomeMutationsKeepChildIndicesValid() {
    Environment environment = Environment::constructDefaultEnvironment(1, 0, 1.0);
    environment.aging = false;
    environment.mRadiation = 1000.0;
    environment.mTestMutationCoef = 1.0;
    environment.mRandom.setSeed(202706);
    environment.mMutationWeights.fill(0.0);
    for (int index = 0;
            index < Environment::MUTATION_STRUCTURAL_WEIGHT_COUNT;
            ++index) {
        environment.mMutationWeights[Environment::MUTATION_STRUCTURAL_WEIGHT_OFFSET + index] = 1.0;
    }

    CellData cellData(1);
    int cell = addAliveCell(cellData, CellType::PHAGOCYTE);
    auto genome = cellData.materializeGenome(cell);
    for (int geneIndex = 0; geneIndex < CellData::MAX_GENES_COUNT; geneIndex++) {
        GeneBuilder gene(genome[static_cast<std::size_t>(geneIndex)]);
        gene.setChild1GeneIndex(GeneIndex(
                (geneIndex + 1) % CellData::MAX_GENES_COUNT));
        gene.setChild2GeneIndex(GeneIndex(
                (geneIndex + 7) % CellData::MAX_GENES_COUNT));
        gene.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    }
    assertTrue(
            cellData.replaceGenome(cell, genome, GeneIndex(0)),
            "CellData structural mutation imports fixture genome");

    for (int step = 0; step < 400; step++) {
        cellData.mMass[cell] = CellData::INITIAL_MASS;
        cellData.mRadius[cell] = std::sqrt(cellData.mMass[cell] / CellData::MASS_PER_AREA);
        cellData.mNitrates[cell] = CellData::MAX_NITRATES;
        cellData.mSeldomCounter[cell] = CellData::MUTATE_PERIOD - 1;

        cellData.updateCellsMetabolism(SimulationWorld::DELTA_TIME, environment);

        for (int geneIndex = 0; geneIndex < CellData::MAX_GENES_COUNT; geneIndex++) {
            const GeneView gene = viewGenomeGene(cellData, cell, geneIndex);
            assertTrue(gene.child1GeneIndex().value() < CellData::MAX_GENES_COUNT,
                    "CellData structural mutation keeps child1 index valid");
            assertTrue(gene.child2GeneIndex().value() < CellData::MAX_GENES_COUNT,
                    "CellData structural mutation keeps child2 index valid");
        }
    }
    assertTrue(cellData.mMutationCount[cell] > 0, "CellData structural mutation stress test applies mutations");
}

cellularity::GenomeData dynamicRuntimeGenome(std::size_t geneCount) {
    cellularity::GenomeData genome(geneCount);
    for (std::size_t index = 0; index < geneCount; ++index) {
        const GeneIndex owner(static_cast<GeneIndex::Value>(index));
        const auto materialized = cellularity::materializeCanonicalGene(
                cellularity::CanonicalGeneDefaults::value(),
                owner,
                geneCount,
                index + 1 == geneCount);
        assertTrue(materialized.has_value(),
                "Dynamic runtime fixture Gene materializes");
        genome[index] = *materialized;
        GeneBuilder builder(genome[index]);
        builder.setCellType(CellType::PHAGOCYTE);
        builder.setSplitMass(static_cast<float>(CellData::MIN_MASS));
        builder.setSplitRatio(0.5f);
        builder.setChild1GeneIndex(owner);
        builder.setChild2GeneIndex(owner);
        builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
        builder.setColor(
                0,
                static_cast<float>(index + 1)
                        / static_cast<float>(geneCount + 1));
    }
    return genome;
}

void prepareDynamicRuntimeCell(CellData& cells, int cell) {
    cells.mPositionX[cell] = 0.0;
    cells.mPositionY[cell] = 0.0;
    cells.mMass[cell] = CellData::INITIAL_MASS;
    cells.mNewMass[cell] = cells.mMass[cell];
    cells.mRadius[cell] = std::sqrt(
            cells.mMass[cell] / CellData::MASS_PER_AREA);
    cells.refreshRadiusSq(cell);
    cells.mNitrates[cell] = CellData::MAX_NITRATES;
    cells.mNewNitrates[cell] = cells.mNitrates[cell];
    cells.mAge[cell] = CellData::AGE_OF_CONSENT + 1.0;
    cells.mOldLinkCount[cell] = 0;
}

void testDynamicGenomeBoundarySizesRunSplitAndMutate() {
    constexpr std::array<std::size_t, 5> lengths{1, 2, 40, 150, 256};
    for (std::size_t caseIndex = 0; caseIndex < lengths.size(); ++caseIndex) {
        const std::size_t length = lengths[caseIndex];
        Environment environment = Environment::constructDefaultEnvironment(
                2, 0, 1.0);
        environment.aging = false;
        environment.mRadiation = 1000.0;
        environment.mutationOnlyAfterSplit = false;
        environment.IsOnlyTweakMutations = true;
        environment.mMutationWeights.fill(0.0);
        environment.mMutationWeights[Environment::MUTATION_COLOR_WEIGHT_OFFSET] = 1.0;
        environment.mRandom.setSeed(
                static_cast<std::int64_t>(0x7000 + caseIndex));
        SimulationWorld world(false, environment);
        CellData& cells = world.mCellData;
        const int parent = addAliveCell(cells, CellType::PHAGOCYTE);
        const GeneIndex active(static_cast<GeneIndex::Value>(length - 1));
        assertTrue(cells.replaceGenome(
                           parent,
                           dynamicRuntimeGenome(length),
                           active),
                "Dynamic boundary Genome enters the runtime");
        prepareDynamicRuntimeCell(cells, parent);

        cells.mSeldomCounter[parent] = CellData::MUTATE_PERIOD - 1;
        cells.updateCellsMetabolism(SimulationWorld::DELTA_TIME, environment);
        assertTrue(cells.mMutationCount[parent] > 0,
                "Radiation mutates a dynamic boundary Genome");
        assertTrue(cells.geneCount(parent) == length,
                "Runtime mutation preserves dynamic Genome length");
        for (std::size_t gene = 0; gene < length; ++gene) {
            const GeneView view = cells.getGeneView(
                    parent,
                    GeneIndex(static_cast<GeneIndex::Value>(gene)));
            assertTrue(view.child1GeneIndex().value() < length
                            && view.child2GeneIndex().value() < length,
                    "Runtime mutation keeps all Gene references in bounds");
        }

        prepareDynamicRuntimeCell(cells, parent);
        cells.handleCellSplit(world);
        assertTrue(cells.mCellCount == 2,
                "Dynamic boundary Genome can split");
        assertTrue(cells.geneCount(0) == length
                        && cells.geneCount(1) == length,
                "Both split children preserve dynamic Genome length");
        assertTrue(cells.genomeId(0) == cells.genomeId(1)
                        && cells.genomeReferenceCount(0) == 2,
                "Split children share one dynamic Genome record");

        const GeneIndex mutationTarget(
                static_cast<GeneIndex::Value>(length - 1));
        const float originalColor =
                cells.getGeneView(1, mutationTarget).color(2);
        assertTrue(cells.editGene(
                           0,
                           mutationTarget,
                           [caseIndex](GeneBuilder& builder) {
                               builder.setColor(
                                       2,
                                       0.25f
                                               + static_cast<float>(caseIndex)
                                                       * 0.05f);
                           }),
                "Dynamic split child accepts a CoW mutation");
        assertTrue(cells.genomeId(0) != cells.genomeId(1)
                        && cells.genomeReferenceCount(0) == 1
                        && cells.genomeReferenceCount(1) == 1,
                "Dynamic split mutation detaches exactly one child");
        assertNear(originalColor,
                cells.getGeneView(1, mutationTarget).color(2),
                0.0,
                "Dynamic CoW mutation leaves the sibling unchanged");
    }
}

void testSmallDynamicGenomesRejectStructuralOnlyMutationPromptly() {
    for (std::size_t length : std::array<std::size_t, 2>{1, 2}) {
        Environment environment = Environment::constructDefaultEnvironment(
                1, 0, 1.0);
        environment.aging = false;
        environment.mRadiation = 1000.0;
        environment.mutationOnlyAfterSplit = false;
        environment.IsOnlyTweakMutations = false;
        environment.mMutationWeights.fill(0.0);
        for (int offset = 0;
                offset < Environment::MUTATION_STRUCTURAL_WEIGHT_COUNT;
                ++offset) {
            environment.mMutationWeights[Environment::MUTATION_STRUCTURAL_WEIGHT_OFFSET + offset] = 1.0;
        }
        environment.mRandom.setSeed(
                static_cast<std::int64_t>(0x7100 + length));
        CellData cells(1);
        const int cell = addAliveCell(cells, CellType::PHAGOCYTE);
        assertTrue(cells.replaceGenome(
                           cell,
                           dynamicRuntimeGenome(length),
                           GeneIndex(0)),
                "Small dynamic Genome enters mutation runtime");
        prepareDynamicRuntimeCell(cells, cell);
        cells.mSeldomCounter[cell] = CellData::MUTATE_PERIOD - 1;
        const cellularity::GenomeData before = cells.materializeGenome(cell);

        cells.updateCellsMetabolism(SimulationWorld::DELTA_TIME, environment);

        assertTrue(cells.mSeldomCounter[cell] == 0,
                "Small Genome completes its structural-only mutation tick");
        assertTrue(cells.mMutationCount[cell] == 0
                        && sameGenome(before, cells, cell),
                "Unavailable structural mutations terminate without changing a small Genome");
    }
}

void testSimulationWorldUpdate1PartialAddsRandomCellsInNonPreview() {
    Environment environment = Environment::constructDefaultEnvironment(20, 0, 1.0);
    environment.addRandom = true;
    environment.mFoodSpeed = 0.0;
    environment.mViscosity = 0.0;
    environment.mFrictionStatic = 0.0;
    environment.mFrictionDynamic = 0.0;
    environment.mGravity = 0.0;
    SimulationWorld world(false, environment);
    world.mEnvironment.mRandom.setSeed(2026);

    world.update1Partial(false, 1.0);

    assertTrue(world.mCellData.mCellCount >= world.mCellData.RANDOM_CELLS_MAINTAIN_COUNT, "SimulationWorld update1Partial adds random cells in non-preview");
}

} // namespace

int main() {
    testSimulationWorldGridInitializationMatchesJavaSizing();
    testSimulationWorldUpdatePhysicalGridSuppressesDuplicateBuckets();
    testSimulationWorldUpdatePhysicalGridStoresCenterBucketForLargeCell();
    testSimulationWorldUpdateGridsAddsSmellEntriesAndSkipsDeadCells();
#ifdef CELLULARITY_PROFILE
    testProfiledSpatialAttributionPreservesFusedGridState();
#endif
    testSimulationWorldFoodSpatialIndexTracksStorageAndGridGenerations();
    testSimulationWorldAddFoodSmellToGridStoresFoodAndFatTypes();
    testSimulationWorldEatFoodsSkipsFoodSmellUntilFoodSmellConsumerAppears();
    testSimulationWorldProcessCellOverlapsCollectsCollisionsOnceAcrossBuckets();
    testSimulationWorldProcessCellOverlapsCollectsNeighborBucketCollisions();
    testSimulationWorldProcessCellOverlapsAppliesDeathIntentsAndCleansReferences();
    testSimulationWorldProcessCellOverlapsSetsFertilizationTargetForReadyGametes();
    testSimulationWorldRemoveCellImmediatelyCompactsAndRetargetsLinks();
    testSimulationWorldRemoveDeadCellsAddsCorpseFoodAndSkipsOutOfBoundsCorpse();
    testSimulationWorldEatFoodsTransfersMassAndRemovesConsumedFood();
    testSimulationWorldEatFoodsPartiallyConsumesFoodAndConservesVelocityMomentum();
    testSimulationWorldEatFoodsTwoPhagocytesDoNotOverConsumeSharedFood();
    testSimulationWorldEatFoodsPreservesDescendingFoodBucketOrder();
    testSimulationWorldMobileFoodDefersPostMotionIndexRebuildUntilDemand();
    testSimulationWorldEatFoodsSkipsOverfullPhagocyte();
    testSimulationWorldUpdate1PartialSkipsEatFoodsInPreview();
    testSimulationWorldEatFoodsLipidLayerBlocksUntilLipaseReducesCoating();
    testSimulationWorldEatFoodsDecayRemovesAndCompactsFood();
    testSimulationWorldFoodSmellDropsConsumedNonLastFoodAndTracksSwapIdentity();
    testSimulationWorldFoodSmellDropsConsumedLastFood();
    testSimulationWorldFoodSmellUsesPostMotionBucketAndPosition();
    testSimulationWorldFoodSmellUsesPostLipaseType();
    testSimulationWorldFoodSmellPreservesPhagocyteOwnerOrderAndPostEatMass();
    testSimulationWorldFoodSmellDoesNotTreatMatchingFoodIndexAsSelf();
    testSimulationWorldSenseocyteFoodAndFatSmellFiltersByLipidLayer();
    testSimulationWorldStereocyteFoodSmellUsesDirectionAndMirror();
    testSimulationWorldUpdateAllCellInteractionsPartialRunsSmellBeforeForces();
    testSimulationWorldUpdate1PartialResolvesFertilizationAfterOverlaps();
    testSimulationWorldUpdate1PartialFertilizesContainedGametesBeforeOverlapDeath();
    testSimulationWorldFertilizationRemovesIncomingCiliocyteContactToDeadSperm();
    testCellDataFertilizationDeadTargetCleanupPreservesTypedContactOrder();
    testCellDataCiliocyteDefensivelyRejectsDeadOrInvalidMassEndpoint();
    testSimulationWorldUpdate1PartialRunsPortedJavaStageOrder();
    testSimulationWorldUpdate1PartialResetsForcesBeforeFlagellocytes();
    testSimulationWorldUpdate1PartialRunsCellSplitAfterKinematics();
    testCellDataCollisionGeometry();
    testCellDataCollisionForce();
    testCellDataGameteForceModifier();
    testCellDataCollisionEffectPass();
    testCellDataCiliocyteCollisionAddsInteractionOnlyForUnlinkedCiliocyte();
    testCellDataCiliocyteCollisionSkipsNonCiliocyteOrLinkedCell();
    testCellDataCiliocyteBoundaryCollisionAddsInteractionUnlessBoundaryLinked();
    testCellDataBoundaryCiliocyteInteractionRemovesTangentialSlip();
    testCellDataCiliocyteInteractionAppliesEqualAndOppositeImpulse();
    testCellDataActiveCiliocyteUsesBendingOrLiftingByMirrorAndCrossSign();
    testCellDataSecondCiliocyteContributesActiveTangentialVelocity();
    testCellDataSmallCiliocyteOrientationCrossDoesNotAddActiveVelocity();
    testCellDataSingleCollisionPartialCombinesForceAndCiliocyte();
    testCellDataCollisionPartialWrapperCombinesGlueocyteSideEffects();
    testCellDataAllCollisionPartialWrapperResetsCiliocyteBufferAndTransfersMass();
    testCellDataBoundaryCollisionPartialAppliesForce();
    testRenderContactCollectorKeepsBoundaryChordForDieEdge();
    testCellDataBoundaryCollisionPartialCreatesGlueocyteBoundaryLink();
    testCellDataBoundaryCollisionPartialAddsCiliocyteInteractionAndAllPass();
    testCellDataCollectAllCollisionInteractionsPartialCollectsCellAndBoundaryCiliocyteContacts();
    testCellDataUpdateAllCellInteractionsPartialKeepsJavaStageOrder();
    testCellDataBuoyocyteParametricDensityReadsItsCurrentLinkCount();
    testCellDataApplyCollectedCiliocyteInteractionsPartialUpdatesVelocitiesExplicitly();
    testCellDataUpdateCellVelocitiesAppliesForceWithoutDrag();
    testCellDataUpdateCellForcesViscosityAndGravity();
    testCellDataUpdateAllCellForcesProcessesLiveCellsAndSkipsDeadCells();
    testCellDataApplyCellFertilizationTransfersStateAndMixesGenome();
    testCellDataResolveFertilizationIntentsRequiresMutualTargets();
    testCellDataResolveFertilizationIntentsFertilizesMutualPairOnce();
    testCellDataResolveFertilizationIntentsUsesRandomEggForUnequalGametes();
    testCellDataUpdateCellVelocitiesStaticFrictionKeepsCellStuck();
    testCellDataUpdateCellVelocitiesDynamicFrictionMatchesFormula();
    testCellDataUpdateCellsKinematicsMovesAndWrapsAngle();
    testCellDataUpdateCellsKinematicsYoungCellMovesButSkipsRadiusRelaxation();
    testCellDataUpdateCellsKinematicsStuckCellStillMovesLikeJava();
    testCellDataUpdateCellsKinematicsRadiusClampAndDeathChecks();
    testCellDataUpdateAllCellsKinematicsMovesDeadCellsLikeJava();
    testCellDataResetCellForcesMatchesJavaDeadCellBehavior();
    testCellDataUpdateFlagellocytesForcePreviewOnlyAnimates();
    testCellDataUpdateFlagellocytesForceAppliesDirectionalForce();
    testCellDataUpdateFlagellocytesForceWrapsPositiveAnimationTime();
    testCellDataAllCollisionEffectPass();
    testSimulationWorldRenderContactFreshnessContract();
    testCellDataGlueocyteCollisionCreatesLinks();
    testCellDataGlueocyteCollisionSkipsInvalidCases();
    testCellDataDevorocyteCollisionTransfersMassAndContents();
    testCellDataDevorocyteCollisionCapsVictimMass();
    testCellDataDevorocyteCollisionSkipsInvalidCases();
    testCellDataVirocyteCollisionInfectsOriginAndCopiesGeneTree();
    testCellDataVirocyteCollisionSkipsPreviewAndKeratinocyteProtectedVictim();
    testCellDataSecrocyteCollisionAppliesToxinToxicationAndSignals();
    testCellDataSecrocyteCollisionSkipsLowNitratesAndProtectedOrImmuneVictims();
    testCellDataSecrocyteProteaseRemovesNearbyLinksAndBoundaryLinks();
    testCellDataBiologicalSideEffectsPassRunsVirocyteAndSecrocyte();
    testCellDataBiologicalSideEffectsPassCreatesGlueocyteLinks();
    testCellDataBiologicalSideEffectsPassTransfersDevorocyteMass();
    testCellDataBiologicalSideEffectsPassSkipsInvalidTargets();
    testCellDataAllBiologicalSideEffectsPass();
    testCellDataUpdateCellsMetabolismBuffersPreviewAndDeadCells();
    testCellDataUpdateCellsMetabolismTypeSpecificBranches();
    testCellDataUpdateCellsMetabolismLinkFlowsMassNitratesAndToxin();
    testCellDataUpdateCellsMetabolismLipocyteStoresAndReleasesLipids();
    testCellDataUpdateCellsMetabolismStemSwitchAgingAndDeathChecks();
    testSimulationWorldApplyNewCellMetabolismCopiesBuffersAndHandlesNaN();
    testCellDataUpdateSignalsDecaysOutputAndResetsOut();
    testCellDataUpdateSignalsNeurocyteGeneratesOutputsAndClamps();
    testCellDataUpdateSignalsFlowsAcrossLinksAndSkipsLipocytes();
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
    testCellDataSignalKernelsRemainBitExactAcrossFeaturesAndTopologyChanges();
    testCellDataSignalKernelFeatureSpecializationsRemainBitExact();
#ifdef CELLULARITY_PROFILE
    testCellDataSignalCsrCacheRebuildsOnlyForStructuralChanges();
#endif
#endif
    testSimulationWorldUpdate2PartialAppliesMetabolismThenSignals();
    testSimulationWorldFullUpdateStageGuard();
#ifdef CELLULARITY_PERFORMANCE_HUD
    testSimulationWorldStageTimingSamplesOnlyRequestedUpdates();
#endif
    testCellDataHandleCellSplitCreatesTwoChildrenAndSplitsState();
    testCellDataNewbornRadiusRelaxationMatchesJavaAgeGateAndFormula();
    testSimulationWorldNewbornRadiiPreserveViableDaughtersThroughFullUpdates();
    testCellDataHandleCellSplitSkipsInvalidCasesAndTelomereDeath();
    testCellDataHandleCellSplitCreatesSiblingLink();
    testCellDataHandleCellSplitReassignsKeptExternalLinks();
    testCellDataSplitSeparatesDisconnectedInheritedBranches();
    testCellDataSplitClassifiesDroppedFormerBranch();
    testCellDataSplitFromUnknownIdentityDoesNotReconstructLegacyOrganism();
    testCellDataInheritedGlueocyteLinkDoesNotMergeOrganisms();
    testCellDataHandleCellSplitKillsFlagellocyteWithInwardLink();
    testCellDataAddRandomCellInitializesDeterministicallyAndRespectsGenePool();
    testCellDataAddRandomCellTargetsGameteCompatibilityToGameteGenes();
    testCellDataAddRandomCellsGatesAndMaintainsPopulation();
    testCellDataUpdateCellsMetabolismMutationBranchUsesGeneMutate();
    testCellDataMutationWeightsCanDisableAllMutations();
    testCellDataDelayedMutationsAccumulateWithoutChangingGenome();
    testCellDataDelayedMutationsApplyToDaughterGenomesOnSplit();
    testCellDataStructuralGenomeMutationsKeepChildIndicesValid();
    testDynamicGenomeBoundarySizesRunSplitAndMutate();
    testSmallDynamicGenomesRejectStructuralOnlyMutationPromptly();
    testSimulationWorldUpdate1PartialAddsRandomCellsInNonPreview();
    std::cout << "native simulation parity tests passed\n";
    return 0;
}
