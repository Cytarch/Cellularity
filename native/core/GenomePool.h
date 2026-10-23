#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include "core/CellDataConstants.h"
#include "core/GenePool.h"
#include "core/GenomeMetadata.h"

namespace cellularity {

inline constexpr std::size_t FIXED_GENOME_GENE_COUNT =
        CellDataConstants::MAX_GENES_COUNT;

inline constexpr std::array<std::size_t, 9> GENOME_REFERENCE_SIZE_CLASSES{
        1, 2, 4, 8, 16, 32, 64, 128, 256
};

using FixedGenome = std::array<GeneData, FIXED_GENOME_GENE_COUNT>;
using GenomeData = std::vector<GeneData>;

enum class GenomePoolError : std::uint8_t {
    None = 0,
    InvalidGenomeId,
    ReleasedGenomeId,
    InvalidGeneCount,
    InvalidGeneIndex,
    InvalidGenomeMetadata,
    InvalidGeneData,
    ReferenceCountOverflow,
    ReferenceCountUnderflow,
    CapacityExhausted,
    AllocationFailed,
    GenePoolFailure,
    CorruptRecord
};

struct GenomePoolResult final {
    GenomeId id = INVALID_GENOME_ID;
    GenomePoolError error = GenomePoolError::None;
    GenePoolError genePoolError = GenePoolError::None;
    CanonicalGeneValidation geneValidation{};
    GeneIndex failedGeneIndex = INVALID_GENE_INDEX;
    bool changed = false;
    bool detached = false;

    [[nodiscard]] constexpr bool hasValue() const noexcept {
        return error == GenomePoolError::None && id != INVALID_GENOME_ID;
    }
};

struct GenomePoolConfig final {
    std::size_t slotsPerSlab = 128;
    std::size_t initialReservedSlots = 128;
    std::size_t maximumSlots =
            static_cast<std::size_t>(INVALID_GENOME_ID.value());
    std::size_t referenceSlabTargetBytes = 16 * 1024;
    std::size_t minimumReferenceBlocksPerSlab = 16;
    std::size_t maximumReferenceBlocksPerSlab = 128;
};

struct GenomeReferenceSizeClassDiagnostics final {
    std::size_t capacity = 0;
    std::size_t liveBlocks = 0;
    std::size_t freeBlocks = 0;
    std::size_t reservedBlocks = 0;
    std::size_t slabCount = 0;
    std::size_t peakLiveBlocks = 0;
    std::uint64_t reservedBytes = 0;
    std::uint64_t slabGrowths = 0;
};

struct GenomePoolDiagnostics final {
    std::size_t liveSlots = 0;
    std::size_t freeSlots = 0;
    std::size_t reservedSlots = 0;
    std::size_t slabCount = 0;
    std::size_t peakLiveSlots = 0;
    std::uint64_t liveGeneReferenceBytes = 0;
    std::uint64_t liveReferenceCapacityBytes = 0;
    std::uint64_t reservedRecordStorageBytes = 0;
    std::uint64_t reservedReferenceStorageBytes = 0;
    std::uint64_t reservedSlotStorageBytes = 0;
    std::array<GenomeReferenceSizeClassDiagnostics,
            GENOME_REFERENCE_SIZE_CLASSES.size()>
            sizeClasses{};
    std::uint64_t createCalls = 0;
    std::uint64_t retainCalls = 0;
    std::uint64_t releaseCalls = 0;
    std::uint64_t detachCalls = 0;
    std::uint64_t detachedRecords = 0;
    std::uint64_t copiedGeneIds = 0;
    std::uint64_t copiedGeneBuilders = 0;
    std::uint64_t replacedGenes = 0;
    std::uint64_t unchangedGeneReplacements = 0;
    std::uint64_t reclaimedSlots = 0;
    std::uint64_t slabGrowths = 0;
    std::uint64_t failedCapacityRequests = 0;
    std::uint64_t failedReferenceOperations = 0;
    std::uint64_t failedGeneOperations = 0;
    std::uint64_t materializedGenomes = 0;
    std::uint64_t activeCacheVerificationChecks = 0;
    std::uint64_t activeCacheVerificationFailures = 0;
};

enum class GenomePoolInvariantError : std::uint8_t {
    None = 0,
    CountMismatch,
    FreeListOutOfRange,
    FreeListCycle,
    InvalidFreeSlot,
    InvalidLiveSlot,
    InvalidGeneCount,
    InvalidSizeClass,
    InvalidReferenceBlock,
    DuplicateReferenceBlock,
    ReferenceBlockCountMismatch,
    InvalidInitialGeneIndex,
    InvalidGeneId,
    InvalidMaterializedGene
};

struct GenomePoolInvariantReport final {
    GenomePoolInvariantError error = GenomePoolInvariantError::None;
    GenomeId genomeId = INVALID_GENOME_ID;
    GeneIndex geneIndex = INVALID_GENE_INDEX;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return error == GenomePoolInvariantError::None;
    }
};

/**
 * Simulation-thread-owned bounded dynamic genome records.
 *
 * A record owns exactly one GenePool reference for each of its 1..256 GeneId
 * entries. The variable reference arrays come from stable size-class slabs;
 * cells retain records, not their genes, so cloning a cell remains O(1).
 */
class GenomePool final {
public:
    explicit GenomePool(GenePool& genePool, GenomePoolConfig config = {});
    ~GenomePool();

    /** Exact ABI storage used by one reserved record slot (reference block excluded). */
    [[nodiscard]] static std::size_t slotStorageBytes() noexcept;
    [[nodiscard]] static std::size_t referenceClassCapacity(
            std::size_t geneCount) noexcept;
    [[nodiscard]] static std::size_t referenceBlocksPerSlab(
            std::size_t geneCount,
            const GenomePoolConfig& config = {}) noexcept;
    [[nodiscard]] static std::size_t initialReferenceStorageBytes(
            std::size_t geneCount,
            const GenomePoolConfig& config = {}) noexcept;

    GenomePool(const GenomePool&) = delete;
    GenomePool& operator=(const GenomePool&) = delete;
    GenomePool(GenomePool&&) = delete;
    GenomePool& operator=(GenomePool&&) = delete;

    /** Imports a bounded dense representation and normalizes legacy metadata. */
    [[nodiscard]] GenomePoolResult create(
            const GeneData* genes,
            std::size_t geneCount);

    /**
     * Builds one record from already interned GeneIds. The caller keeps its
     * own references; this record retains one reference per Genome entry.
     */
    [[nodiscard]] GenomePoolResult createFromGeneIds(
            const GeneId* genes,
            std::size_t geneCount,
            GenomeMetadata metadata);

    template <typename Container>
    [[nodiscard]] GenomePoolResult create(const Container& genome) {
        static_assert(std::is_same_v<
                std::remove_cv_t<typename Container::value_type>, GeneData>);
        return create(genome.data(), genome.size());
    }

    /** Creates a canonical default genome without materializing dense genes. */
    [[nodiscard]] GenomePoolResult createDefault(
            std::size_t geneCount = FIXED_GENOME_GENE_COUNT);

    [[nodiscard]] GenomePoolError retain(GenomeId id) noexcept;
    [[nodiscard]] GenomePoolError release(GenomeId id) noexcept;

    /** Transfers one owner to a private record only when the source is shared. */
    [[nodiscard]] GenomePoolResult detach(GenomeId id) noexcept;

    /**
     * Canonicalizes and swaps one immutable gene. The returned GenomeId may
     * differ when whole-genome CoW was required.
     */
    [[nodiscard]] GenomePoolResult replaceGene(
            GenomeId id,
            GeneIndex index,
            const GeneData& candidate);

    [[nodiscard]] GenomePoolError reserve(std::size_t minimumSlots) noexcept;
    [[nodiscard]] GenomePoolError reserveReferenceBlocks(
            std::size_t geneCount,
            std::size_t minimumBlocks) noexcept;

    [[nodiscard]] std::optional<GenomeView> view(GenomeId id) const noexcept;
    [[nodiscard]] std::optional<GeneView> geneView(
            GenomeId id,
            GeneIndex index) const noexcept;
    [[nodiscard]] std::optional<GeneData> copyGeneForMutation(
            GenomeId id,
            GeneIndex index) const noexcept;
    [[nodiscard]] std::optional<GenomeData> materialize(
            GenomeId id) const;
    [[nodiscard]] std::optional<GeneId> geneId(
            GenomeId id,
            GeneIndex index) const noexcept;
    [[nodiscard]] std::optional<GenomeMetadata> metadata(
            GenomeId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> geneCount(
            GenomeId id) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> referenceCount(
            GenomeId id) const noexcept;
    [[nodiscard]] bool isLive(GenomeId id) const noexcept;

    /** Debug cache audit; counters are diagnostic and do not affect ownership. */
    [[nodiscard]] bool verifyCachedGene(
            GenomeId genomeId,
            GeneIndex index,
            GeneId cachedGeneId) const noexcept;

    [[nodiscard]] GenomePoolDiagnostics diagnostics() const noexcept;
    void resetDiagnosticCounters() noexcept;
    [[nodiscard]] GenomePoolInvariantReport validateInvariants() const;

private:
    struct Slot final {
        GeneId* genes = nullptr;
        std::uint32_t referenceCount = 0;
        GenomeId nextFree = INVALID_GENOME_ID;
        std::uint32_t generation = 0;
        GeneIndex initialGeneIndex = INVALID_GENE_INDEX;
        std::uint16_t geneCount = 0;
        std::uint8_t sizeClassIndex = 0;
        bool live = false;
    };

    struct Slab final {
        std::unique_ptr<Slot[]> slots;
        std::size_t size = 0;
    };

    struct ReferenceSlab final {
        std::unique_ptr<GeneId[]> entries;
        std::size_t blockCount = 0;
    };

    struct ReferenceSizeClass final {
        std::vector<ReferenceSlab> slabs;
        std::vector<GeneId*> freeBlocks;
        std::size_t liveBlocks = 0;
        std::size_t reservedBlocks = 0;
        std::size_t peakLiveBlocks = 0;
        std::uint64_t slabGrowths = 0;
    };

    struct EventCounters final {
        std::uint64_t createCalls = 0;
        std::uint64_t retainCalls = 0;
        std::uint64_t releaseCalls = 0;
        std::uint64_t detachCalls = 0;
        std::uint64_t detachedRecords = 0;
        std::uint64_t copiedGeneIds = 0;
        std::uint64_t copiedGeneBuilders = 0;
        std::uint64_t replacedGenes = 0;
        std::uint64_t unchangedGeneReplacements = 0;
        std::uint64_t reclaimedSlots = 0;
        std::uint64_t slabGrowths = 0;
        std::uint64_t failedCapacityRequests = 0;
        std::uint64_t failedReferenceOperations = 0;
        std::uint64_t failedGeneOperations = 0;
        std::uint64_t materializedGenomes = 0;
        std::uint64_t activeCacheVerificationChecks = 0;
        std::uint64_t activeCacheVerificationFailures = 0;
    };

    [[nodiscard]] Slot* slot(GenomeId id) noexcept;
    [[nodiscard]] const Slot* slot(GenomeId id) const noexcept;
    [[nodiscard]] GenomePoolError appendSlab(bool recordGrowth) noexcept;
    [[nodiscard]] GenomePoolError ensureFreeSlot() noexcept;
    [[nodiscard]] GenomePoolError appendReferenceSlab(
            std::size_t sizeClassIndex,
            bool recordGrowth) noexcept;
    [[nodiscard]] GeneId* claimReferenceBlock(
            std::size_t sizeClassIndex) noexcept;
    void releaseReferenceBlock(
            std::size_t sizeClassIndex,
            GeneId* block) noexcept;
    [[nodiscard]] GenomeId claimFreeSlot(
            GeneId* genes,
            std::size_t geneCount,
            std::size_t sizeClassIndex,
            GeneIndex initialGeneIndex) noexcept;
    void reclaimSlot(GenomeId id, Slot& target) noexcept;
    [[nodiscard]] GenomePoolResult failure(
            GenomePoolError error,
            GenePoolError genePoolError = GenePoolError::None,
            CanonicalGeneValidation validation = {},
            GeneIndex failedGeneIndex = INVALID_GENE_INDEX) noexcept;
    void releaseGeneReferencePrefix(
            const GeneId* ids,
            std::size_t count) noexcept;
    [[nodiscard]] bool isReferenceBlockInClass(
            std::size_t sizeClassIndex,
            const GeneId* block) const noexcept;

    GenePool& genePool_;
    GenomePoolConfig config_;
    std::vector<Slab> slabs_;
    std::array<ReferenceSizeClass,
            GENOME_REFERENCE_SIZE_CLASSES.size()>
            referenceClasses_{};
    GenomeId freeHead_ = INVALID_GENOME_ID;
    std::size_t liveSlots_ = 0;
    std::size_t freeSlots_ = 0;
    std::size_t reservedSlots_ = 0;
    std::size_t peakLiveSlots_ = 0;
    mutable EventCounters counters_;
};

} // namespace cellularity
