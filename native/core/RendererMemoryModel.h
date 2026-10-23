#pragma once

#include <cstdint>

namespace cellularity::memory {

// Dependency-free upper bounds for the compact render-source records. The
// concrete render types assert that they fit these values.
inline constexpr std::uint64_t kRenderSourceCellBudgetBytes = 88;
// One shared normal/boundary record: three 64-bit IDs, three indices, nine floats.
inline constexpr std::uint64_t kRenderSourceLinkBudgetBytes = 72;
inline constexpr std::uint64_t kRenderSourceFoodBudgetBytes = 40;
inline constexpr std::uint64_t kRenderSourceDeadSplashBudgetBytes = 40;
inline constexpr std::uint64_t kRenderMarkerBudgetBytes = 24;
inline constexpr std::uint64_t kSparseRenderContactChordEntryBudgetBytes = 48;

// Element counts used by the native render-buffer reset path. Keeping these
// formulas in a dependency-free model lets allocation admission and the actual
// allocation path share one checked source of truth.
struct RendererInitialBufferLayout {
    std::uint64_t sourceOrderedCellEntriesPerBucket = 0;
    std::uint64_t cellVisitEntries = 0;
    std::uint64_t sourceRecordIndexEntries = 0;
    std::uint64_t sourceRecordGenerationEntries = 0;
    std::uint64_t rawContactChordFirstEntries = 0;
    std::uint64_t rawContactChordLastEntries = 0;
    std::uint64_t rawContactChordCountEntries = 0;
    std::uint64_t collectorBroadphaseEntries = 0;
    std::uint64_t renderContactChordGenerationEntries = 0;
    std::uint64_t renderContactChordOffsetEntries = 0;
    std::uint64_t renderContactChordCountEntries = 0;
    std::uint64_t linkCandidateWordEntries = 0;
    std::uint64_t cellTriangleFloatEntries = 0;
    std::uint64_t cellTextureFloatEntries = 0;
    std::uint64_t cellCenterFloatEntries = 0;
    std::uint64_t cellColorFloatEntries = 0;
    std::uint64_t cellIndexEntries = 0;
    std::uint64_t foodInstanceFloatEntries = 0;
    std::uint64_t foodVertexFloatEntries = 0;
    std::uint64_t deadInstanceFloatEntries = 0;
    std::uint64_t deadVertexFloatEntries = 0;
    std::uint64_t linkTriangleFloatEntries = 0;
    std::uint64_t linkTextureFloatEntries = 0;

    // Absolute logical bounds supported by the current exact renderer. These
    // are not preallocated; high-water buffers grow only when a frame needs
    // them and then retain their storage until reset.
    std::uint64_t maximumCellVertexCount = 0;
    std::uint64_t maximumCellIndexCount = 0;
    std::uint64_t maximumCellCenterFloatEntries = 0;
    std::uint64_t maximumNormalizedContactChordEntries = 0;
    std::uint64_t maximumRawContactChordEntries = 0;
    std::uint64_t maximumNormalEdgeCount = 0;
    std::uint64_t maximumDrawableLinkCount = 0;
    std::uint64_t maximumLinkVertexCount = 0;
};

struct RendererMemoryEstimate {
    // Aggregate inline state of the microscope three-slot source/prepared
    // exchanges, their single capture/builder pair and GL consumer.
    std::uint64_t rendererEmbeddedStateBytes = 0;
    // Inline vector controls and draw-bunch tables owned by one Ghost snapshot.
    std::uint64_t ghostSnapshotEmbeddedStateBytes = 0;
    // Payload allocated immediately by reset(maxCellCount).
    std::uint64_t initialPayloadBytes = 0;
    // Maximum requested payload of all three reusable source/prepared slots
    // plus the single capture/builder scratch owner, including compact input
    // records, markers, normalized contact chords and world food/dead capacities.
    std::uint64_t maximumReusablePayloadBytes = 0;
    // Maximum active ranges and marker state retained by a Genome Preview
    // Ghost snapshot.
    std::uint64_t maximumGhostSnapshotPayloadBytes = 0;
    // Conservative admission amount. It includes capacity-growth headroom for
    // reusable and Ghost vectors. The world policy applies its separate general
    // headroom only to simulation storage before adding this already-budgeted
    // renderer amount.
    std::uint64_t budgetedBytes = 0;
    std::uint64_t reusableFrameSlotCount = 0;
};

[[nodiscard]] bool tryBuildRendererInitialBufferLayout(
        int maxCellCount, RendererInitialBufferLayout& output) noexcept;

[[nodiscard]] bool
tryEstimateRendererMemory(int maxCellCount, int maxFoodCount,
        unsigned int addressBits,
        RendererMemoryEstimate& output) noexcept;

} // namespace cellularity::memory
