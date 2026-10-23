#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "core/Gene.h"
#include "tests/support/SimulationTestSupport.h"
#include "tests/support/TestAssertions.h"

namespace allocation_probe {

std::atomic<bool> tracking{false};
std::atomic<std::uint64_t> allocationCount{0};

void recordAllocation() noexcept {
    if (tracking.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
    }
}

void* allocate(std::size_t size) {
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    recordAllocation();
    return memory;
}

void* allocateAligned(std::size_t size, std::size_t alignment) {
    void* memory = nullptr;
#if defined(_WIN32)
    memory = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
    if (posix_memalign(&memory, alignment, size == 0 ? 1 : size) != 0) {
        memory = nullptr;
    }
#endif
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    recordAllocation();
    return memory;
}

void freeAligned(void* memory) noexcept {
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

void begin() noexcept {
    allocationCount.store(0, std::memory_order_relaxed);
    tracking.store(true, std::memory_order_release);
}

std::uint64_t end() noexcept {
    tracking.store(false, std::memory_order_release);
    return allocationCount.load(std::memory_order_relaxed);
}

} // namespace allocation_probe

void* operator new(std::size_t size) {
    return allocation_probe::allocate(size);
}

void* operator new[](std::size_t size) {
    return allocation_probe::allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocation_probe::allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocation_probe::allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size,
        std::align_val_t alignment,
        const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocateAligned(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size,
        std::align_val_t alignment,
        const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocateAligned(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::freeAligned(memory);
}

namespace {

using namespace cellularity::tests;

Environment makeStableEnvironment() {
    Environment environment = Environment::constructDefaultEnvironment(32, 32, 1.0);
    environment.mFoodSpeed = 0.0;
    environment.mLightSpeed = 0.0;
    environment.mRadiation = 0.0;
    environment.mTestFoodDecay = 0.0;
    environment.mTestIdleMetabolismScale = 0.0;
    environment.aging = false;
    environment.addRandom = false;
    environment.mobileFood = false;
    return environment;
}

void addStableCellsAndFood(SimulationWorld& world) {
    constexpr int COLUMN_COUNT = 4;
    constexpr double SPACING = 0.16;
    for (int cellOrdinal = 0; cellOrdinal < 16; cellOrdinal++) {
        const CellType type = cellOrdinal == 0
                ? CellType::SENSEOCYTE
                : CellType::KERATINOCYTE;
        const int cellIndex = addAliveCell(world.mCellData, type);
        world.mCellData.mPositionX[cellIndex] =
                (cellOrdinal % COLUMN_COUNT - 1.5) * SPACING;
        world.mCellData.mPositionY[cellIndex] =
                (cellOrdinal / COLUMN_COUNT - 1.5) * SPACING;
        world.mCellData.mTelomeresCount[cellIndex] = -1;
        world.mCellData.refreshRadiusSq(cellIndex);

        world.mCellData.editActiveGene(
                cellIndex,
                [cellOrdinal](cellularity::GeneBuilder& gene) {
                    gene.setStaysAlive(true);
                    gene.setSplitMass(CellData::MAX_SPLIT_MASS);
                    if (cellOrdinal == 0) {
                        gene.setIntegerValue(
                                Gene::SMELL_TYPE_INTEGER_INDEX,
                                SimulationWorld::SMELL_TYPE_FOOD);
                    }
                });
    }

    const double consumerX = world.mCellData.mPositionX[0];
    const double consumerY = world.mCellData.mPositionY[0];
    for (int foodOrdinal = 0; foodOrdinal < 8; foodOrdinal++) {
        const double offsetX = 0.015 * (1 + foodOrdinal % 4);
        const double offsetY = 0.015 * (foodOrdinal / 4 == 0 ? 1 : -1);
        const int foodIndex = world.mFoodData.addFood(
                consumerX + offsetX,
                consumerY + offsetY,
                0.05,
                0.0f,
                world.mEnvironment);
        assertTrue(foodIndex >= 0, "Steady allocation fixture adds Food");
    }
}

void runFullUpdate(SimulationWorld& world) {
    world.update1(false, 1.0);
    world.update2(false, 1.0);
}

void verifyAllocationProbeSelfCheck() {
    allocation_probe::begin();
    void* memory = ::operator new(64);
    const std::uint64_t allocations = allocation_probe::end();
    ::operator delete(memory);
    assertTrue(allocations == 1, "Allocation probe observes an explicit allocation");
}

void testSteadyFullUpdateDoesNotAllocate() {
    SimulationWorld world(false, makeStableEnvironment());
    addStableCellsAndFood(world);

    for (int step = 0; step < 16; step++) {
        runFullUpdate(world);
    }

    assertTrue(world.mCellData.mCellCount == 16,
            "Steady allocation fixture preserves Cell count");
    assertTrue(world.mFoodData.mFoodCount == 8,
            "Steady allocation fixture preserves Food count");
    assertTrue(world.mHasFoodSmellConsumers,
            "Steady allocation fixture exercises Food smell consumers");
    assertTrue(world.mCellCollisionGrid->touchedCount() > 0,
            "Steady allocation fixture exercises the physical grid");
    assertTrue(world.mSmellCollisionGrid->touchedCount() > 0,
            "Steady allocation fixture exercises the smell grid");

    allocation_probe::begin();
    for (int step = 0; step < 64; step++) {
        runFullUpdate(world);
    }
    const std::uint64_t allocations = allocation_probe::end();

    assertTrue(allocations == 0,
            "Warmed full simulation updates must not allocate; observed "
                    + std::to_string(allocations));
}

#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
void testSignalAdjacencyRebuildDoesNotAllocateAfterWarmup() {
    SimulationWorld world(false, makeStableEnvironment());
    addStableCellsAndFood(world);
    for (int cellIndex = 0; cellIndex + 1 < world.mCellData.mCellCount;
            ++cellIndex) {
        assertTrue(
                world.mCellData.addLink(
                        cellIndex,
                        cellIndex + 1,
                        CellLinkCreationReason::UNSPECIFIED,
                        0.0,
                        Environment::PI,
                        0.5f,
                        0.0f)
                        == 0,
                "Signal allocation fixture adds topology edge");
    }
    world.mCellData.setSignalUpdateAlgorithm(
            CellSignalUpdateAlgorithm::CELL_OWNED_CSR);
    world.mCellData.updateSignals(1.0, nullptr);

    allocation_probe::begin();
    for (int step = 0; step < 64; ++step) {
        world.mCellData.updateSignals(1.0, nullptr);
    }
    const std::uint64_t steadyAllocations = allocation_probe::end();
    assertTrue(
            steadyAllocations == 0,
            "Warmed Cell-owned signal updates must not allocate");

    allocation_probe::begin();
    const bool removed =
            world.mCellData.mTopology.removeCellLinkBetween(0, 1);
    world.mCellData.updateSignals(1.0, nullptr);
    const int addResult = world.mCellData.addLink(
            0,
            1,
            CellLinkCreationReason::UNSPECIFIED,
            0.0,
            Environment::PI,
            0.5f,
            0.0f);
    world.mCellData.updateSignals(1.0, nullptr);
    const std::uint64_t rebuildAllocations = allocation_probe::end();

    assertTrue(removed && addResult == 0,
            "Signal allocation fixture rebuilds topology");
    assertTrue(
            rebuildAllocations == 0,
            "Warmed signal adjacency rebuilds must not allocate");
}
#endif

} // namespace

int main() {
    verifyAllocationProbeSelfCheck();
    testSteadyFullUpdateDoesNotAllocate();
#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
    testSignalAdjacencyRebuildDoesNotAllocateAfterWarmup();
#endif
    std::cout << "native steady-state allocation tests passed\n";
    return 0;
}
