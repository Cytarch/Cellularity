#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/CanonicalGene.h"
#include "core/GeneAccess.h"
#include "core/GenomeIdentity.h"

namespace cellularity {

enum class GenePoolError : std::uint8_t {
    None = 0,
    InvalidGeneId,
    ReleasedGeneId,
    ReferenceCountOverflow,
    ReferenceCountUnderflow,
    CapacityExhausted,
    AllocationFailed,
    CorruptInternIndex
};

struct GenePoolInternResult final {
    GeneId id = INVALID_GENE_ID;
    GenePoolError error = GenePoolError::None;

    [[nodiscard]] constexpr bool hasValue() const noexcept {
        return error == GenePoolError::None && id != INVALID_GENE_ID;
    }
};

struct GenePoolConfig final {
    std::size_t slotsPerSlab = 256;
    std::size_t initialReservedSlots = 256;
    std::size_t maximumSlots =
            static_cast<std::size_t>(INVALID_GENE_ID.value());
};

struct GenePoolDiagnostics final {
    std::size_t liveSlots = 0;
    std::size_t freeSlots = 0;
    std::size_t reservedSlots = 0;
    std::size_t slabCount = 0;
    std::size_t peakLiveSlots = 0;
    std::uint64_t liveGeneDataBytes = 0;
    std::uint64_t reservedSlotStorageBytes = 0;
    std::size_t internIndexBuckets = 0;
    std::size_t internIndexEntries = 0;
    std::uint32_t defaultGeneReferences = 0;
    std::uint64_t internLookups = 0;
    std::uint64_t internHits = 0;
    std::uint64_t internMisses = 0;
    std::uint64_t hashCollisions = 0;
    std::uint64_t retainCalls = 0;
    std::uint64_t releaseCalls = 0;
    std::uint64_t reclaimedSlots = 0;
    std::uint64_t slabGrowths = 0;
    std::uint64_t failedCapacityRequests = 0;
    std::uint64_t failedReferenceOperations = 0;
};

enum class GenePoolInvariantError : std::uint8_t {
    None = 0,
    CountMismatch,
    MissingDefaultSlot,
    InvalidDefaultSlot,
    FreeListOutOfRange,
    FreeListCycle,
    InvalidFreeSlot,
    InvalidLiveSlot,
    InvalidCachedHash,
    InvalidIndexEntry,
    DuplicateIndexEntry,
    MissingIndexEntry,
    DuplicateCanonicalGene
};

struct GenePoolInvariantReport final {
    GenePoolInvariantError error = GenePoolInvariantError::None;
    GeneId id = INVALID_GENE_ID;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return error == GenePoolInvariantError::None;
    }
};

/**
 * Simulation-thread-owned storage for immutable canonical genes.
 *
 * Slabs never move, so a GeneView remains valid while its GeneId is retained,
 * even if later interning grows the pool. Slot zero is the immortal canonical
 * default. It participates in logical retain/release accounting but is never
 * reclaimed.
 */
class GenePool final {
public:
    explicit GenePool(GenePoolConfig config = {});

    /** Exact ABI storage used by one reserved slab slot. */
    [[nodiscard]] static std::size_t slotStorageBytes() noexcept;

    GenePool(const GenePool&) = delete;
    GenePool& operator=(const GenePool&) = delete;
    GenePool(GenePool&&) = delete;
    GenePool& operator=(GenePool&&) = delete;

    /** Interns one validated canonical value and returns one retained handle. */
    [[nodiscard]] GenePoolInternResult intern(const CanonicalGene& gene);

    [[nodiscard]] GenePoolError retain(GeneId id) noexcept;
    [[nodiscard]] GenePoolError release(GeneId id) noexcept;

    /** Adds stable capacity without changing any live GeneId or GeneView. */
    [[nodiscard]] GenePoolError reserve(std::size_t minimumSlots) noexcept;

    [[nodiscard]] std::optional<GeneView> view(GeneId id) const noexcept;

    /** Resolves canonical self references for one concrete genome position. */
    [[nodiscard]] GeneView contextualView(
            GeneId id,
            GeneIndex owner,
            bool isInitial) const noexcept;

    /** Stable backing address for a live slot; intended for verified hot caches. */
    [[nodiscard]] const GeneData* dataPointer(GeneId id) const noexcept;

    /** Materializes canonical references and compatibility metadata for a boundary. */
    [[nodiscard]] std::optional<GeneData> materialize(
            GeneId id,
            GeneIndex owner,
            std::size_t geneCount,
            bool isInitial) const noexcept;

    /** Returns an unshared local candidate; mutating pool storage is impossible. */
    [[nodiscard]] std::optional<GeneData> copyForMutation(
            GeneId id) const noexcept;

    [[nodiscard]] bool isLive(GeneId id) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> referenceCount(
            GeneId id) const noexcept;

    [[nodiscard]] GenePoolDiagnostics diagnostics() const noexcept;
    void resetDiagnosticCounters() noexcept;

    /** Expensive whole-pool audit intended for tests and controlled diagnostics. */
    [[nodiscard]] GenePoolInvariantReport validateInvariants() const;

private:
    struct Slot final {
        std::optional<CanonicalGene> gene;
        std::uint64_t canonicalHash = 0;
        std::uint32_t referenceCount = 0;
        GeneId nextFree = INVALID_GENE_ID;
        std::uint32_t generation = 0;
        bool live = false;
    };

    struct Slab final {
        std::unique_ptr<Slot[]> slots;
        std::size_t size = 0;
    };

    struct EventCounters final {
        std::uint64_t internLookups = 0;
        std::uint64_t internHits = 0;
        std::uint64_t internMisses = 0;
        std::uint64_t hashCollisions = 0;
        std::uint64_t retainCalls = 0;
        std::uint64_t releaseCalls = 0;
        std::uint64_t reclaimedSlots = 0;
        std::uint64_t slabGrowths = 0;
        std::uint64_t failedCapacityRequests = 0;
        std::uint64_t failedReferenceOperations = 0;
    };

    using InternBucket = std::vector<GeneId>;
    using InternIndex = std::unordered_map<std::uint64_t, InternBucket>;

    [[nodiscard]] Slot* slot(GeneId id) noexcept;
    [[nodiscard]] const Slot* slot(GeneId id) const noexcept;
    [[nodiscard]] GenePoolError appendSlab(bool recordGrowth) noexcept;
    [[nodiscard]] GenePoolError incrementReference(Slot& target) noexcept;
    [[nodiscard]] GenePoolInternResult failure(GenePoolError error) noexcept;

    GenePoolConfig config_;
    std::vector<Slab> slabs_;
    InternIndex internIndex_;
    GeneId freeHead_ = INVALID_GENE_ID;
    std::size_t liveSlots_ = 0;
    std::size_t freeSlots_ = 0;
    std::size_t reservedSlots_ = 0;
    std::size_t peakLiveSlots_ = 0;
    std::size_t internIndexEntries_ = 0;
    EventCounters counters_;
};

} // namespace cellularity
