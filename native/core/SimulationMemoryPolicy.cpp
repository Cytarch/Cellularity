#include "core/SimulationMemoryPolicy.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "core/CellData.h"
#include "core/SimulationWorld.h"
#include "core/DeadSplashRuntimeId.h"
#include "core/Environment.h"
#include "core/FoodRuntimeId.h"
#include "core/Gene.h"
#include "core/RendererMemoryModel.h"
#include "core/TopologyEdgeRuntimeId.h"
#include "formats/FormatLimits.h"
#include "io/DecodeError.h"

namespace cellularity::memory {
namespace {

constexpr std::uint64_t kEstimateHeadroomNumerator = 5;
constexpr std::uint64_t kEstimateHeadroomDenominator = 4;
constexpr std::uint64_t kMinimumEstimateHeadroomBytes = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kPhysicalMemoryBudgetDivisor = 4;
constexpr std::uint64_t k64BitMaximumWorldAllocationBudgetBytes =
        1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t k32BitMaximumWorldAllocationBudgetBytes =
        384ULL * 1024ULL * 1024ULL;

[[noreturn]] void failLimit(const char* message) {
    throw DecodeError(DecodeErrorCode::LimitExceeded, message);
}

std::uint64_t checkedAdd(std::uint64_t left,
        std::uint64_t right,
        const char* message) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        failLimit(message);
    }
    return left + right;
}

std::uint64_t checkedMultiply(std::uint64_t left,
        std::uint64_t right,
        const char* message) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        failLimit(message);
    }
    return left * right;
}

void addProduct(std::uint64_t& total,
        std::uint64_t count,
        std::uint64_t elementBytes,
        const char* message) {
    total = checkedAdd(total, checkedMultiply(count, elementBytes, message), message);
}

std::uint64_t gridAllocationBytes(int gridSize,
        int entryLimit,
        bool hasTypes) {
    if (gridSize < 0 || entryLimit < 0) {
        failLimit("Simulation grid dimensions are invalid");
    }
    const std::uint64_t stride = static_cast<std::uint64_t>(gridSize) + 2;
    const std::uint64_t slots = checkedMultiply(
            stride, stride, "Simulation grid size arithmetic overflow");
    const std::uint64_t entries = checkedMultiply(
            slots,
            static_cast<std::uint64_t>(entryLimit),
            "Simulation grid entry arithmetic overflow");

    std::uint64_t bytes = 0;
    addProduct(bytes, entries, sizeof(int), "Simulation grid memory arithmetic overflow");
    if (hasTypes) {
        addProduct(bytes,
                entries,
                sizeof(std::int16_t),
                "Simulation grid type memory arithmetic overflow");
    }
    addProduct(bytes, slots, sizeof(int), "Simulation grid count memory arithmetic overflow");
    // The touched-index list can contain every grid slot at runtime.
    addProduct(bytes, slots, sizeof(int), "Simulation grid scratch memory arithmetic overflow");
    return bytes;
}

std::uint64_t cellAllocationBytesPerCapacityEntry() {
    std::uint64_t bytes = 0;

    // CellData fixed-capacity structure-of-arrays.
    addProduct(bytes, 29, sizeof(double), "Cell allocation arithmetic overflow");
    addProduct(bytes, 9, sizeof(float), "Cell allocation arithmetic overflow");
    addProduct(bytes, 3, sizeof(std::uint8_t), "Cell allocation arithmetic overflow");
    addProduct(bytes, 12, sizeof(int), "Cell allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(CellType), "Cell allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(CellRuntimeId), "Cell runtime identity allocation arithmetic overflow");
    addProduct(bytes, 2, sizeof(OrganismId), "Cell Organism identity allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(std::uint32_t),
            "Cell Organism traversal mark allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(int),
            "Cell Organism traversal queue allocation arithmetic overflow");
    // Pooled genome ownership/cache vectors. Genome and Gene payloads are no
    // longer reserved once per Cell capacity entry.
    addProduct(bytes, 1, sizeof(GenomeId),
            "Cell GenomeId allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(GeneIndex),
            "Cell active GeneIndex allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(GeneId),
            "Cell active GeneId allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(const GeneData*),
            "Cell active Gene pointer allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(std::uint8_t),
            "Cell active Gene context allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(std::array<float, 4>), "Cell color allocation arithmetic overflow");
    addProduct(bytes,
            1,
            sizeof(std::array<float, 2 * CellData::SIGNAL_SUBSTANCE_COUNT>),
            "Cell signal allocation arithmetic overflow");
    addProduct(bytes,
            2,
            sizeof(std::array<float, CellData::SIGNAL_SUBSTANCE_COUNT>),
            "Cell signal allocation arithmetic overflow");
    addProduct(bytes,
            2ULL * CellData::MAX_CILIOCYTE_INTERACTIONS_PER_CELL,
            sizeof(int),
            "Cell interaction allocation arithmetic overflow");

    // CellTopology fixed adjacency arrays.
    addProduct(bytes,
            1,
            sizeof(std::array<int, CellData::MAX_NEIGHBORS>),
            "Cell topology allocation arithmetic overflow");
    addProduct(bytes,
            1,
            sizeof(std::array<std::uint8_t, CellData::MAX_NEIGHBORS>),
            "Cell topology allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(int), "Cell topology allocation arithmetic overflow");

#if CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
    // Host/C-03 Cell-owned signal adjacency: one bounded incident-edge
    // reference per possible adjacency slot plus prefix/scatter offsets for
    // every Cell. Ordinary Android builds compile this candidate out.
    addProduct(bytes,
            CellData::MAX_NEIGHBORS + 2ULL,
            sizeof(std::uint32_t),
            "Cell signal adjacency allocation arithmetic overflow");
#endif

    // Collision contacts and collision intents are allocated for every capacity slot.
    addProduct(bytes,
            CellData::MAX_COLLISIONS_PER_CELL,
            sizeof(int) + sizeof(CellData::CellContactBuffer::StoredGeometry),
            "Cell collision allocation arithmetic overflow");
    addProduct(bytes, 1, sizeof(int), "Cell collision allocation arithmetic overflow");
    addProduct(bytes,
            CellData::MAX_COLLISIONS_PER_CELL,
            2 * sizeof(int) + 4 * sizeof(double),
            "Cell collision intent arithmetic overflow");

    // SimulationWorld fixed vectors and optional broadphase runtime buffers.
    addProduct(bytes,
            1,
            sizeof(int) + sizeof(std::uint8_t),
            "World cell allocation arithmetic overflow");
    addProduct(bytes,
            1,
            sizeof(int),
            "Cell render grid next-index arithmetic overflow");
    addProduct(bytes,
            1,
            9 * sizeof(int) + 7 * sizeof(double),
            "World broadphase allocation arithmetic overflow");
#if defined(CELLULARITY_DEFAULT_INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP)
    // Packed staging, resident row records, directional/fallback migration
    // buffers and two merge workspaces. The multiplier includes ordinary
    // vector growth headroom while keeping the experimental backend inside
    // the same pre-allocation memory gate as the rest of the world.
    addProduct(bytes,
            1,
            10 * 32ULL + 2 * 40ULL,
            "Incremental broadphase allocation arithmetic overflow");
#endif
    // A fully linked topology can use every adjacency slot. Vector growth can
    // temporarily retain up to twice the eventual element capacity. A normal
    // edge is shared by two Cells; a boundary entry belongs to one Cell.
    constexpr std::uint64_t normalEdgeBytes =
            sizeof(TopologyEdgeRuntimeId)
            + 2 * sizeof(int)
            + 6 * sizeof(double)
            + 6 * sizeof(float);
    constexpr std::uint64_t maximumBoundaryBytes =
            sizeof(int) + 5 * sizeof(double) + 4 * sizeof(float);
    addProduct(bytes,
            CellData::MAX_NEIGHBORS,
            std::max(normalEdgeBytes, 2 * maximumBoundaryBytes),
            "World topology capacity arithmetic overflow");
    return bytes;
}

std::uint64_t maximumBroadphaseRowCount(double radius) {
    const long double rowCount =
            2.0L * std::ceil(static_cast<long double>(radius) / (2.0L * CellData::MAX_CELL_RADIUS))
            + 1.0L;
    if (!std::isfinite(rowCount)
            || rowCount > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
        failLimit("Simulation broadphase row count is too large");
    }
    return static_cast<std::uint64_t>(rowCount);
}

std::uint64_t totalPhysicalMemoryBytes() noexcept {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) != 0
            ? static_cast<std::uint64_t>(status.ullTotalPhys)
            : 0;
#else
    const long pageCount = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageCount <= 0 || pageSize <= 0) {
        return 0;
    }
    const std::uint64_t pages = static_cast<std::uint64_t>(pageCount);
    const std::uint64_t bytesPerPage = static_cast<std::uint64_t>(pageSize);
    if (pages > std::numeric_limits<std::uint64_t>::max() / bytesPerPage) {
        return 0;
    }
    return pages * bytesPerPage;
#endif
}

} // namespace

RuntimeLimits runtimeLimitsForAddressBits(unsigned int addressBits) {
    using namespace cellularity::format_limits;
    if (addressBits <= 32) {
        return {
                addressBits,
                kMaximumSaveFileBytes,
                k32BitOperationalSubstrateFileBytes,
                k32BitMaximumWorldAllocationBudgetBytes
        };
    }
    return {
            addressBits,
            kMaximumSaveFileBytes,
            kMaximumSaveFileBytes,
            k64BitMaximumWorldAllocationBudgetBytes
    };
}

RuntimeLimits runtimeLimitsForDevice(
        unsigned int addressBits,
        std::uint64_t physicalMemoryBytes) {
    RuntimeLimits limits = runtimeLimitsForAddressBits(addressBits);
    if (physicalMemoryBytes >= kPhysicalMemoryBudgetDivisor) {
        limits.worldAllocationBudgetBytes = std::min(
                limits.worldAllocationBudgetBytes,
                physicalMemoryBytes / kPhysicalMemoryBudgetDivisor);
    }
    return limits;
}

RuntimeLimits currentRuntimeLimits() {
    return runtimeLimitsForDevice(
            sizeof(void*) * CHAR_BIT,
            totalPhysicalMemoryBytes());
}

WorldAllocationEstimate estimateWorldAllocation(
        const Environment& environment,
        unsigned int addressBits) {
    const RuntimeLimits limits = runtimeLimitsForAddressBits(addressBits);
    if (environment.maxCellCount <= 0 || environment.maxFoodCount < 0
            || !environment.geometry().isValid(CellDataConstants::MAX_CELL_RADIUS)) {
        throw DecodeError(DecodeErrorCode::InvalidValue,
                "Invalid simulation allocation parameters");
    }
    if (environment.maxCellCount
            > std::numeric_limits<int>::max() / CellData::MAX_NEIGHBORS) {
        failLimit("Simulation Cell capacity exceeds index arithmetic limits");
    }

    const double maximumGridSafeRadius =
            static_cast<double>(std::numeric_limits<int>::max())
            * CellData::MAX_CELL_RADIUS;
    if (environment.mRadius > maximumGridSafeRadius) {
        failLimit("Simulation radius exceeds the grid safety limit");
    }

    const int cellGridSize = std::min(
                                     206,
                                     static_cast<int>(environment.mRadius / CellData::MAX_CELL_RADIUS - 0.1))
            / 2 * 2;
    const int smellGridSize = std::min(
            207,
            static_cast<int>(2.0 * environment.mRadius / SimulationWorld::SMELL_RADIUS_CUTOFF));

    std::uint64_t bytes = sizeof(SimulationWorld);
    const GenePoolConfig genePoolConfig;
    const GenomePoolConfig genomePoolConfig;
    addProduct(bytes,
            genePoolConfig.initialReservedSlots,
            GenePool::slotStorageBytes(),
            "Initial GenePool reservation arithmetic overflow");
    addProduct(bytes,
            genomePoolConfig.initialReservedSlots,
            GenomePool::slotStorageBytes(),
            "Initial GenomePool reservation arithmetic overflow");
    bytes = checkedAdd(
            bytes,
            GenomePool::initialReferenceStorageBytes(
                    FIXED_GENOME_GENE_COUNT, genomePoolConfig),
            "Initial Genome reference slab arithmetic overflow");
    addProduct(bytes,
            static_cast<std::uint64_t>(environment.maxCellCount),
            cellAllocationBytesPerCapacityEntry(),
            "Simulation Cell memory arithmetic overflow");

    // Dead splashes are allocated at ceil(maxCellCount / 4), including their
    // runtime-only identity stream.
    const std::uint64_t deadSplashCapacity =
            (static_cast<std::uint64_t>(environment.maxCellCount) + 3) / 4;
    addProduct(bytes,
            deadSplashCapacity,
            8 * sizeof(float) + sizeof(DeadSplashRuntimeId),
            "Dead splash memory arithmetic overflow");

    // FoodData plus SimulationWorld's spatial-next and smell-visit arrays. The
    // mapping generation belongs to FoodData; touched bucket heads are
    // bounded by both Food capacity and collision-grid slot count below.
    addProduct(bytes,
            static_cast<std::uint64_t>(environment.maxFoodCount),
            6 * sizeof(float) + 3 * sizeof(int) + sizeof(std::uint8_t)
                    + sizeof(std::uint64_t) + sizeof(FoodRuntimeId),
            "Food memory arithmetic overflow");

    bytes = checkedAdd(
            bytes,
            gridAllocationBytes(cellGridSize, SimulationWorld::CELL_COLLISION_GRID_COUNT_MAX, false),
            "Cell collision grid memory arithmetic overflow");
    bytes = checkedAdd(
            bytes,
            gridAllocationBytes(smellGridSize, SimulationWorld::SMELL_COLLISION_GRID_COUNT_MAX, true),
            "Smell collision grid memory arithmetic overflow");

    const std::uint64_t cellGridStride = static_cast<std::uint64_t>(cellGridSize) + 2;
    const std::uint64_t cellGridSlots = checkedMultiply(
            cellGridStride, cellGridStride, "Food render grid arithmetic overflow");
    addProduct(bytes,
            cellGridSlots,
            sizeof(int),
            "Food spatial grid memory arithmetic overflow");
    addProduct(bytes,
            std::min(static_cast<std::uint64_t>(environment.maxFoodCount),
                    cellGridSlots),
            sizeof(int),
            "Food touched-grid memory arithmetic overflow");
    addProduct(bytes,
            cellGridSlots,
            2 * sizeof(int),
            "Cell render grid memory arithmetic overflow");
    addProduct(bytes,
            (cellGridSlots + 63U) / 64U,
            sizeof(std::uint64_t),
            "Cell render grid occupancy arithmetic overflow");

    const std::uint64_t smellGridStride = static_cast<std::uint64_t>(smellGridSize) + 2;
    const std::uint64_t smellGridSlots = checkedMultiply(
            smellGridStride, smellGridStride, "Smell consumer grid arithmetic overflow");
    addProduct(bytes,
            smellGridSlots,
            sizeof(char),
            "Smell consumer grid memory arithmetic overflow");

    // Three broadphase instances may retain row metadata: two physics scans
    // and the collector's pure stale-geometry fallback. Seven int arrays per
    // row cover current and migration scratch storage conservatively.
    addProduct(bytes,
            maximumBroadphaseRowCount(environment.mRadius),
            3ULL * 7ULL * sizeof(int),
            "Broadphase row memory arithmetic overflow");
#if defined(CELLULARITY_DEFAULT_INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP)
    // Four std::vector descriptors are retained by every allocated row.
    addProduct(bytes,
            maximumBroadphaseRowCount(environment.mRadius),
            4ULL * 3ULL * sizeof(void*),
            "Incremental broadphase row memory arithmetic overflow");
#endif

    RendererMemoryEstimate rendererMemory;
    if (!tryEstimateRendererMemory(environment.maxCellCount,
                environment.maxFoodCount,
                addressBits,
                rendererMemory)) {
        failLimit("Renderer memory arithmetic or upload-range limit exceeded");
    }
    // RendererMemoryEstimate already includes its own conservative vector
    // capacity-growth reserve. Apply the general world headroom only to the
    // simulation allocation estimate so the renderer reserve is not inflated
    // a second time.
    const std::uint64_t proportionalHeadroom = checkedMultiply(
                                                       bytes,
                                                       kEstimateHeadroomNumerator,
                                                       "Simulation headroom arithmetic overflow")
            / kEstimateHeadroomDenominator;
    const std::uint64_t minimumHeadroom = checkedAdd(
            bytes,
            kMinimumEstimateHeadroomBytes,
            "Simulation headroom arithmetic overflow");
    bytes = std::max(proportionalHeadroom, minimumHeadroom);
    bytes = checkedAdd(bytes,
            rendererMemory.budgetedBytes,
            "World and renderer memory arithmetic overflow");
    return {bytes, limits.worldAllocationBudgetBytes, rendererMemory.budgetedBytes};
}

WorldAllocationEstimate estimateCurrentWorldAllocation(
        const Environment& environment) {
    const RuntimeLimits limits = currentRuntimeLimits();
    WorldAllocationEstimate estimate = estimateWorldAllocation(
            environment, limits.addressBits);
    estimate.budgetBytes = limits.worldAllocationBudgetBytes;
    return estimate;
}

void validateWorldAllocation(const Environment& environment) {
    const WorldAllocationEstimate estimate = estimateCurrentWorldAllocation(environment);
    if (estimate.bytes > estimate.budgetBytes) {
        failLimit("Simulation exceeds the operational memory budget for this ABI");
    }
}

bool isWorldAllocationSupported(const Environment& environment) noexcept {
    try {
        validateWorldAllocation(environment);
        return true;
    } catch (...) {
        return false;
    }
}

void validateSubstrateFileSize(std::uint64_t fileSizeBytes,
        const RuntimeLimits& limits) {
    if (fileSizeBytes > limits.absoluteSubstrateFileBytes) {
        failLimit("Substrate exceeds the absolute 2 GiB file limit");
    }
    if (fileSizeBytes > limits.operationalSubstrateFileBytes) {
        failLimit("Substrate exceeds the operational file limit for this ABI");
    }
}

} // namespace cellularity::memory
