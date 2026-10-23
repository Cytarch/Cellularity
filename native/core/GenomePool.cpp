#include "core/GenomePool.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace cellularity {
namespace {

constexpr std::uint32_t nextGeneration(std::uint32_t generation) noexcept {
    return generation == std::numeric_limits<std::uint32_t>::max()
            ? 1U
            : generation + 1U;
}

std::optional<std::size_t> referenceSizeClassIndex(
        std::size_t geneCount) noexcept {
    if (!isSupportedGeneCount(geneCount)) {
        return std::nullopt;
    }
    for (std::size_t index = 0;
            index < GENOME_REFERENCE_SIZE_CLASSES.size();
            ++index) {
        if (geneCount <= GENOME_REFERENCE_SIZE_CLASSES[index]) {
            return index;
        }
    }
    return std::nullopt;
}

GeneIndex normalizedInitialGeneIndex(
        const GeneData* genes,
        std::size_t geneCount) noexcept {
    for (std::size_t index = 0; index < geneCount; ++index) {
        if (GeneView(genes[index]).initialCompatibilityFlag()) {
            return GeneIndex(static_cast<GeneIndex::Value>(index));
        }
    }
    return GeneIndex(0);
}

} // namespace

GenomePool::GenomePool(GenePool& genePool, GenomePoolConfig config)
        : genePool_(genePool), config_(config) {
    const std::size_t maximumRepresentableSlots =
            static_cast<std::size_t>(INVALID_GENOME_ID.value());
    if (config_.slotsPerSlab == 0
            || config_.initialReservedSlots == 0
            || config_.maximumSlots == 0
            || config_.maximumSlots > maximumRepresentableSlots
            || config_.initialReservedSlots > config_.maximumSlots
            || config_.referenceSlabTargetBytes == 0
            || config_.minimumReferenceBlocksPerSlab == 0
            || config_.maximumReferenceBlocksPerSlab == 0
            || config_.minimumReferenceBlocksPerSlab
                    > config_.maximumReferenceBlocksPerSlab) {
        throw std::invalid_argument("Invalid GenomePool configuration");
    }
    while (reservedSlots_ < config_.initialReservedSlots) {
        if (appendSlab(false) != GenomePoolError::None) {
            throw std::bad_alloc();
        }
    }
}

std::size_t GenomePool::slotStorageBytes() noexcept {
    return sizeof(Slot);
}

std::size_t GenomePool::referenceClassCapacity(
        std::size_t geneCount) noexcept {
    const auto index = referenceSizeClassIndex(geneCount);
    return index.has_value() ? GENOME_REFERENCE_SIZE_CLASSES[*index] : 0;
}

std::size_t GenomePool::referenceBlocksPerSlab(
        std::size_t geneCount,
        const GenomePoolConfig& config) noexcept {
    const std::size_t capacity = referenceClassCapacity(geneCount);
    if (capacity == 0
            || config.referenceSlabTargetBytes == 0
            || config.minimumReferenceBlocksPerSlab == 0
            || config.maximumReferenceBlocksPerSlab == 0
            || config.minimumReferenceBlocksPerSlab
                    > config.maximumReferenceBlocksPerSlab) {
        return 0;
    }
    const std::size_t blockBytes = capacity * sizeof(GeneId);
    const std::size_t targetBlocks = std::max<std::size_t>(
            1, config.referenceSlabTargetBytes / blockBytes);
    return std::clamp(
            targetBlocks,
            config.minimumReferenceBlocksPerSlab,
            config.maximumReferenceBlocksPerSlab);
}

std::size_t GenomePool::initialReferenceStorageBytes(
        std::size_t geneCount,
        const GenomePoolConfig& config) noexcept {
    const std::size_t capacity = referenceClassCapacity(geneCount);
    const std::size_t blocks = referenceBlocksPerSlab(geneCount, config);
    if (capacity == 0 || blocks == 0) {
        return 0;
    }
    return blocks * (capacity * sizeof(GeneId) + sizeof(GeneId*));
}

GenomePool::~GenomePool() {
    // CellData normally balances every record. This fallback keeps GenePool
    // ownership sound for isolated pools and exceptional exits.
    for (std::size_t rawId = 0; rawId < reservedSlots_; ++rawId) {
        Slot* current = slot(GenomeId(static_cast<GenomeId::Value>(rawId)));
        if (current == nullptr || !current->live) {
            continue;
        }
        for (std::size_t index = 0; index < current->geneCount; ++index) {
            const GenePoolError released = genePool_.release(current->genes[index]);
            (void)released;
            assert(released == GenePoolError::None);
        }
    }
}

GenomePoolResult GenomePool::create(
        const GeneData* genes,
        std::size_t geneCount) {
    ++counters_.createCalls;
    const auto sizeClassIndex = referenceSizeClassIndex(geneCount);
    if (!sizeClassIndex.has_value()) {
        return failure(GenomePoolError::InvalidGeneCount);
    }
    if (genes == nullptr) {
        return failure(GenomePoolError::InvalidGeneData);
    }
    const GeneIndex initialGeneIndex =
            normalizedInitialGeneIndex(genes, geneCount);
    const GenomePoolError recordCapacity = ensureFreeSlot();
    if (recordCapacity != GenomePoolError::None) {
        return failure(recordCapacity);
    }
    ReferenceSizeClass& sizeClass = referenceClasses_[*sizeClassIndex];
    if (sizeClass.freeBlocks.empty()) {
        const GenomePoolError referenceCapacity =
                appendReferenceSlab(*sizeClassIndex, true);
        if (referenceCapacity != GenomePoolError::None) {
            return failure(referenceCapacity);
        }
    }
    GeneId* ids = claimReferenceBlock(*sizeClassIndex);

    std::size_t internedCount = 0;
    for (; internedCount < geneCount; ++internedCount) {
        const GeneIndex owner(
                static_cast<GeneIndex::Value>(internedCount));
        CanonicalGeneResult canonical = canonicalizeGene(
                genes[internedCount], owner, geneCount);
        if (!canonical.hasValue()) {
            releaseGeneReferencePrefix(ids, internedCount);
            releaseReferenceBlock(*sizeClassIndex, ids);
            return failure(
                    GenomePoolError::InvalidGeneData,
                    GenePoolError::None,
                    canonical.validation,
                    owner);
        }
        const GenePoolInternResult interned = genePool_.intern(*canonical.value);
        if (!interned.hasValue()) {
            releaseGeneReferencePrefix(ids, internedCount);
            releaseReferenceBlock(*sizeClassIndex, ids);
            return failure(
                    GenomePoolError::GenePoolFailure,
                    interned.error,
                    {},
                    owner);
        }
        ids[internedCount] = interned.id;
    }

    const GenomeId id = claimFreeSlot(
            ids, geneCount, *sizeClassIndex, initialGeneIndex);
    assert(id != INVALID_GENOME_ID);
    return {id, GenomePoolError::None};
}

GenomePoolResult GenomePool::createFromGeneIds(
        const GeneId* genes,
        std::size_t geneCount,
        GenomeMetadata metadata) {
    ++counters_.createCalls;
    const auto sizeClassIndex = referenceSizeClassIndex(geneCount);
    if (!sizeClassIndex.has_value()) {
        return failure(GenomePoolError::InvalidGeneCount);
    }
    if (genes == nullptr) {
        return failure(GenomePoolError::InvalidGeneData);
    }
    if (!isGeneIndexInBounds(metadata.initialGeneIndex, geneCount)) {
        return failure(GenomePoolError::InvalidGenomeMetadata);
    }

    // A canonical Gene may be shared between file rows only when all explicit
    // references remain valid in every owner context that uses it.
    for (std::size_t index = 0; index < geneCount; ++index) {
        const GeneIndex owner(static_cast<GeneIndex::Value>(index));
        const auto materialized = genePool_.materialize(
                genes[index], owner, geneCount, false);
        if (!materialized.has_value()) {
            return failure(
                    GenomePoolError::InvalidGeneData,
                    GenePoolError::None,
                    {CanonicalGeneError::InvalidGeneReference,
                            static_cast<int>(index)},
                    owner);
        }
    }

    const GenomePoolError recordCapacity = ensureFreeSlot();
    if (recordCapacity != GenomePoolError::None) {
        return failure(recordCapacity);
    }
    ReferenceSizeClass& sizeClass = referenceClasses_[*sizeClassIndex];
    if (sizeClass.freeBlocks.empty()) {
        const GenomePoolError referenceCapacity =
                appendReferenceSlab(*sizeClassIndex, true);
        if (referenceCapacity != GenomePoolError::None) {
            return failure(referenceCapacity);
        }
    }
    GeneId* ids = claimReferenceBlock(*sizeClassIndex);

    std::size_t retainedCount = 0;
    for (; retainedCount < geneCount; ++retainedCount) {
        const GenePoolError retained = genePool_.retain(genes[retainedCount]);
        if (retained != GenePoolError::None) {
            releaseGeneReferencePrefix(ids, retainedCount);
            releaseReferenceBlock(*sizeClassIndex, ids);
            return failure(
                    GenomePoolError::GenePoolFailure,
                    retained,
                    {},
                    GeneIndex(static_cast<GeneIndex::Value>(retainedCount)));
        }
        ids[retainedCount] = genes[retainedCount];
    }

    const GenomeId id = claimFreeSlot(
            ids,
            geneCount,
            *sizeClassIndex,
            metadata.initialGeneIndex);
    assert(id != INVALID_GENOME_ID);
    return {id, GenomePoolError::None};
}

GenomePoolResult GenomePool::createDefault(std::size_t geneCount) {
    ++counters_.createCalls;
    const auto sizeClassIndex = referenceSizeClassIndex(geneCount);
    if (!sizeClassIndex.has_value()) {
        return failure(GenomePoolError::InvalidGeneCount);
    }
    const GenomePoolError recordCapacity = ensureFreeSlot();
    if (recordCapacity != GenomePoolError::None) {
        return failure(recordCapacity);
    }
    ReferenceSizeClass& sizeClass = referenceClasses_[*sizeClassIndex];
    if (sizeClass.freeBlocks.empty()) {
        const GenomePoolError referenceCapacity =
                appendReferenceSlab(*sizeClassIndex, true);
        if (referenceCapacity != GenomePoolError::None) {
            return failure(referenceCapacity);
        }
    }
    GeneId* ids = claimReferenceBlock(*sizeClassIndex);

    std::size_t retainedCount = 0;
    for (; retainedCount < geneCount; ++retainedCount) {
        const GenePoolError retained = genePool_.retain(DEFAULT_GENE_ID);
        if (retained != GenePoolError::None) {
            releaseGeneReferencePrefix(ids, retainedCount);
            releaseReferenceBlock(*sizeClassIndex, ids);
            return failure(
                    GenomePoolError::GenePoolFailure,
                    retained,
                    {},
                    GeneIndex(static_cast<GeneIndex::Value>(retainedCount)));
        }
        ids[retainedCount] = DEFAULT_GENE_ID;
    }
    const GenomeId id = claimFreeSlot(
            ids, geneCount, *sizeClassIndex, GeneIndex(0));
    assert(id != INVALID_GENOME_ID);
    return {id, GenomePoolError::None};
}

GenomePoolError GenomePool::retain(GenomeId id) noexcept {
    ++counters_.retainCalls;
    Slot* target = slot(id);
    if (target == nullptr || id == INVALID_GENOME_ID) {
        ++counters_.failedReferenceOperations;
        return GenomePoolError::InvalidGenomeId;
    }
    if (!target->live) {
        ++counters_.failedReferenceOperations;
        return GenomePoolError::ReleasedGenomeId;
    }
    if (target->referenceCount == std::numeric_limits<std::uint32_t>::max()) {
        ++counters_.failedReferenceOperations;
        return GenomePoolError::ReferenceCountOverflow;
    }
    ++target->referenceCount;
    return GenomePoolError::None;
}

GenomePoolError GenomePool::release(GenomeId id) noexcept {
    ++counters_.releaseCalls;
    Slot* target = slot(id);
    if (target == nullptr || id == INVALID_GENOME_ID) {
        ++counters_.failedReferenceOperations;
        return GenomePoolError::InvalidGenomeId;
    }
    if (!target->live) {
        ++counters_.failedReferenceOperations;
        return GenomePoolError::ReleasedGenomeId;
    }
    if (target->referenceCount == 0) {
        ++counters_.failedReferenceOperations;
        return GenomePoolError::ReferenceCountUnderflow;
    }
    if (target->referenceCount > 1) {
        --target->referenceCount;
        return GenomePoolError::None;
    }

    for (std::size_t index = 0; index < target->geneCount; ++index) {
        const GeneId geneId = target->genes[index];
        if (!genePool_.isLive(geneId)
                || genePool_.referenceCount(geneId).value_or(0) == 0) {
            ++counters_.failedGeneOperations;
            return GenomePoolError::CorruptRecord;
        }
    }
    for (std::size_t index = 0; index < target->geneCount; ++index) {
        const GenePoolError released = genePool_.release(target->genes[index]);
        if (released != GenePoolError::None) {
            ++counters_.failedGeneOperations;
            return GenomePoolError::GenePoolFailure;
        }
    }
    reclaimSlot(id, *target);
    return GenomePoolError::None;
}

GenomePoolResult GenomePool::detach(GenomeId id) noexcept {
    ++counters_.detachCalls;
    Slot* source = slot(id);
    if (source == nullptr || id == INVALID_GENOME_ID) {
        return failure(GenomePoolError::InvalidGenomeId);
    }
    if (!source->live) {
        return failure(GenomePoolError::ReleasedGenomeId);
    }
    if (source->referenceCount == 0) {
        return failure(GenomePoolError::ReferenceCountUnderflow);
    }
    if (source->referenceCount == 1) {
        return {id, GenomePoolError::None};
    }
    const GenomePoolError recordCapacity = ensureFreeSlot();
    if (recordCapacity != GenomePoolError::None) {
        return failure(recordCapacity);
    }
    ReferenceSizeClass& sizeClass =
            referenceClasses_[source->sizeClassIndex];
    if (sizeClass.freeBlocks.empty()) {
        const GenomePoolError referenceCapacity =
                appendReferenceSlab(source->sizeClassIndex, true);
        if (referenceCapacity != GenomePoolError::None) {
            return failure(referenceCapacity);
        }
    }
    GeneId* copiedGenes = claimReferenceBlock(source->sizeClassIndex);

    std::size_t retainedCount = 0;
    for (; retainedCount < source->geneCount; ++retainedCount) {
        const GeneId sourceGene = source->genes[retainedCount];
        const GenePoolError retained = genePool_.retain(sourceGene);
        if (retained != GenePoolError::None) {
            releaseGeneReferencePrefix(copiedGenes, retainedCount);
            releaseReferenceBlock(source->sizeClassIndex, copiedGenes);
            return failure(GenomePoolError::GenePoolFailure, retained);
        }
        copiedGenes[retainedCount] = sourceGene;
    }

    const std::size_t copiedGeneCount = source->geneCount;
    const std::size_t copiedSizeClass = source->sizeClassIndex;
    const GeneIndex copiedInitial = source->initialGeneIndex;
    const GenomeId detachedId = claimFreeSlot(
            copiedGenes,
            copiedGeneCount,
            copiedSizeClass,
            copiedInitial);
    assert(detachedId != INVALID_GENOME_ID);
    --source->referenceCount;
    ++counters_.detachedRecords;
    counters_.copiedGeneIds += copiedGeneCount;
    GenomePoolResult result{detachedId, GenomePoolError::None};
    result.detached = true;
    return result;
}

GenomePoolResult GenomePool::replaceGene(
        GenomeId id,
        GeneIndex index,
        const GeneData& candidate) {
    Slot* original = slot(id);
    if (original == nullptr || id == INVALID_GENOME_ID) {
        return failure(GenomePoolError::InvalidGenomeId);
    }
    if (!original->live) {
        return failure(GenomePoolError::ReleasedGenomeId);
    }
    if (!isGeneIndexInBounds(index, original->geneCount)) {
        return failure(
                GenomePoolError::InvalidGeneIndex,
                GenePoolError::None,
                {},
                index);
    }
    ++counters_.copiedGeneBuilders;
    CanonicalGeneResult canonical = canonicalizeGene(
            candidate, index, original->geneCount);
    if (!canonical.hasValue()) {
        return failure(
                GenomePoolError::InvalidGeneData,
                GenePoolError::None,
                canonical.validation,
                index);
    }
    const GenePoolInternResult interned = genePool_.intern(*canonical.value);
    if (!interned.hasValue()) {
        return failure(
                GenomePoolError::GenePoolFailure,
                interned.error,
                {},
                index);
    }
    const GeneId oldGeneId = original->genes[index.value()];
    if (interned.id == oldGeneId) {
        const GenePoolError released = genePool_.release(interned.id);
        if (released != GenePoolError::None) {
            return failure(GenomePoolError::GenePoolFailure, released);
        }
        ++counters_.unchangedGeneReplacements;
        return {id, GenomePoolError::None};
    }

    GenomePoolResult writable = detach(id);
    if (!writable.hasValue()) {
        const GenePoolError released = genePool_.release(interned.id);
        (void)released;
        assert(released == GenePoolError::None);
        return writable;
    }
    Slot& destination = *slot(writable.id);
    const GeneId replaced = destination.genes[index.value()];
    destination.genes[index.value()] = interned.id;
    const GenePoolError released = genePool_.release(replaced);
    if (released != GenePoolError::None) {
        destination.genes[index.value()] = replaced;
        const GenePoolError rollback = genePool_.release(interned.id);
        (void)rollback;
        assert(rollback == GenePoolError::None);
        return failure(GenomePoolError::GenePoolFailure, released);
    }
    ++counters_.replacedGenes;
    writable.changed = true;
    return writable;
}

GenomePoolError GenomePool::reserve(std::size_t minimumSlots) noexcept {
    if (minimumSlots > config_.maximumSlots) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::CapacityExhausted;
    }
    while (reservedSlots_ < minimumSlots) {
        const GenomePoolError result = appendSlab(true);
        if (result != GenomePoolError::None) {
            return result;
        }
    }
    return GenomePoolError::None;
}

GenomePoolError GenomePool::reserveReferenceBlocks(
        std::size_t geneCount,
        std::size_t minimumBlocks) noexcept {
    const auto sizeClassIndex = referenceSizeClassIndex(geneCount);
    if (!sizeClassIndex.has_value()) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::InvalidGeneCount;
    }
    if (minimumBlocks > config_.maximumSlots) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::CapacityExhausted;
    }
    ReferenceSizeClass& sizeClass = referenceClasses_[*sizeClassIndex];
    while (sizeClass.reservedBlocks < minimumBlocks) {
        const GenomePoolError result =
                appendReferenceSlab(*sizeClassIndex, true);
        if (result != GenomePoolError::None) {
            return result;
        }
    }
    return GenomePoolError::None;
}

std::optional<GenomeView> GenomePool::view(GenomeId id) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live) {
        return std::nullopt;
    }
    return GenomeView(
            genePool_,
            target->genes,
            target->geneCount,
            target->initialGeneIndex);
}

std::optional<GeneView> GenomePool::geneView(
        GenomeId id,
        GeneIndex index) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr
            || !target->live
            || !isGeneIndexInBounds(index, target->geneCount)) {
        return std::nullopt;
    }
    return genePool_.contextualView(
            target->genes[index.value()],
            index,
            index == target->initialGeneIndex);
}

std::optional<GeneData> GenomePool::copyGeneForMutation(
        GenomeId id,
        GeneIndex index) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr
            || !target->live
            || !isGeneIndexInBounds(index, target->geneCount)) {
        return std::nullopt;
    }
    return genePool_.materialize(
            target->genes[index.value()],
            index,
            target->geneCount,
            index == target->initialGeneIndex);
}

std::optional<GenomeData> GenomePool::materialize(GenomeId id) const {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live) {
        return std::nullopt;
    }
    GenomeData result(target->geneCount);
    for (std::size_t index = 0; index < result.size(); ++index) {
        const GeneIndex owner(static_cast<GeneIndex::Value>(index));
        const auto gene = genePool_.materialize(
                target->genes[index],
                owner,
                result.size(),
                owner == target->initialGeneIndex);
        if (!gene.has_value()) {
            return std::nullopt;
        }
        result[index] = *gene;
    }
    ++counters_.materializedGenomes;
    return result;
}

std::optional<GeneId> GenomePool::geneId(
        GenomeId id,
        GeneIndex index) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr
            || !target->live
            || !isGeneIndexInBounds(index, target->geneCount)) {
        return std::nullopt;
    }
    return target->genes[index.value()];
}

std::optional<GenomeMetadata> GenomePool::metadata(GenomeId id) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live) {
        return std::nullopt;
    }
    return GenomeMetadata{target->initialGeneIndex};
}

std::optional<std::size_t> GenomePool::geneCount(GenomeId id) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live) {
        return std::nullopt;
    }
    return target->geneCount;
}

std::optional<std::uint32_t> GenomePool::referenceCount(
        GenomeId id) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live) {
        return std::nullopt;
    }
    return target->referenceCount;
}

bool GenomePool::isLive(GenomeId id) const noexcept {
    const Slot* target = slot(id);
    return target != nullptr && target->live;
}

bool GenomePool::verifyCachedGene(
        GenomeId genomeId,
        GeneIndex index,
        GeneId cachedGeneId) const noexcept {
    ++counters_.activeCacheVerificationChecks;
    const auto expected = geneId(genomeId, index);
    const bool valid = expected.has_value()
            && *expected == cachedGeneId
            && genePool_.isLive(cachedGeneId);
    if (!valid) {
        ++counters_.activeCacheVerificationFailures;
    }
    return valid;
}

GenomePoolDiagnostics GenomePool::diagnostics() const noexcept {
    GenomePoolDiagnostics result;
    result.liveSlots = liveSlots_;
    result.freeSlots = freeSlots_;
    result.reservedSlots = reservedSlots_;
    result.slabCount = slabs_.size();
    result.peakLiveSlots = peakLiveSlots_;
    result.reservedRecordStorageBytes =
            static_cast<std::uint64_t>(reservedSlots_) * sizeof(Slot);

    for (std::size_t rawId = 0; rawId < reservedSlots_; ++rawId) {
        const Slot* current = slot(
                GenomeId(static_cast<GenomeId::Value>(rawId)));
        if (current != nullptr && current->live) {
            result.liveGeneReferenceBytes +=
                    static_cast<std::uint64_t>(current->geneCount)
                    * sizeof(GeneId);
        }
    }
    for (std::size_t index = 0;
            index < referenceClasses_.size();
            ++index) {
        const ReferenceSizeClass& source = referenceClasses_[index];
        GenomeReferenceSizeClassDiagnostics& destination =
                result.sizeClasses[index];
        destination.capacity = GENOME_REFERENCE_SIZE_CLASSES[index];
        destination.liveBlocks = source.liveBlocks;
        destination.freeBlocks = source.freeBlocks.size();
        destination.reservedBlocks = source.reservedBlocks;
        destination.slabCount = source.slabs.size();
        destination.peakLiveBlocks = source.peakLiveBlocks;
        destination.reservedBytes =
                static_cast<std::uint64_t>(source.reservedBlocks)
                * destination.capacity * sizeof(GeneId);
        destination.slabGrowths = source.slabGrowths;
        result.liveReferenceCapacityBytes +=
                static_cast<std::uint64_t>(source.liveBlocks)
                * destination.capacity * sizeof(GeneId);
        result.reservedReferenceStorageBytes += destination.reservedBytes;
    }
    result.reservedSlotStorageBytes = result.reservedRecordStorageBytes
            + result.reservedReferenceStorageBytes;
    result.createCalls = counters_.createCalls;
    result.retainCalls = counters_.retainCalls;
    result.releaseCalls = counters_.releaseCalls;
    result.detachCalls = counters_.detachCalls;
    result.detachedRecords = counters_.detachedRecords;
    result.copiedGeneIds = counters_.copiedGeneIds;
    result.copiedGeneBuilders = counters_.copiedGeneBuilders;
    result.replacedGenes = counters_.replacedGenes;
    result.unchangedGeneReplacements = counters_.unchangedGeneReplacements;
    result.reclaimedSlots = counters_.reclaimedSlots;
    result.slabGrowths = counters_.slabGrowths;
    result.failedCapacityRequests = counters_.failedCapacityRequests;
    result.failedReferenceOperations = counters_.failedReferenceOperations;
    result.failedGeneOperations = counters_.failedGeneOperations;
    result.materializedGenomes = counters_.materializedGenomes;
    result.activeCacheVerificationChecks =
            counters_.activeCacheVerificationChecks;
    result.activeCacheVerificationFailures =
            counters_.activeCacheVerificationFailures;
    return result;
}

void GenomePool::resetDiagnosticCounters() noexcept {
    counters_ = {};
    peakLiveSlots_ = liveSlots_;
    for (ReferenceSizeClass& sizeClass : referenceClasses_) {
        sizeClass.peakLiveBlocks = sizeClass.liveBlocks;
        sizeClass.slabGrowths = 0;
    }
}

GenomePoolInvariantReport GenomePool::validateInvariants() const {
    const auto invalid = [](
                                 GenomePoolInvariantError error,
                                 GenomeId genomeId,
                                 GeneIndex geneIndex = INVALID_GENE_INDEX) {
        return GenomePoolInvariantReport{error, genomeId, geneIndex};
    };
    if (liveSlots_ + freeSlots_ != reservedSlots_) {
        return invalid(
                GenomePoolInvariantError::CountMismatch,
                INVALID_GENOME_ID);
    }

    std::vector<bool> freeSeen(reservedSlots_, false);
    std::size_t observedFree = 0;
    for (GenomeId id = freeHead_; id != INVALID_GENOME_ID;) {
        const Slot* freeSlot = slot(id);
        if (freeSlot == nullptr) {
            return invalid(GenomePoolInvariantError::FreeListOutOfRange, id);
        }
        if (freeSeen[id.value()]) {
            return invalid(GenomePoolInvariantError::FreeListCycle, id);
        }
        freeSeen[id.value()] = true;
        ++observedFree;
        if (freeSlot->live
                || freeSlot->referenceCount != 0
                || freeSlot->genes != nullptr
                || freeSlot->geneCount != 0
                || freeSlot->initialGeneIndex != INVALID_GENE_INDEX) {
            return invalid(GenomePoolInvariantError::InvalidFreeSlot, id);
        }
        id = freeSlot->nextFree;
    }
    if (observedFree != freeSlots_) {
        return invalid(
                GenomePoolInvariantError::CountMismatch,
                INVALID_GENOME_ID);
    }

    std::array<std::unordered_set<const GeneId*>,
            GENOME_REFERENCE_SIZE_CLASSES.size()>
            freeReferenceBlocks;
    for (std::size_t classIndex = 0;
            classIndex < referenceClasses_.size();
            ++classIndex) {
        const ReferenceSizeClass& sizeClass = referenceClasses_[classIndex];
        if (sizeClass.freeBlocks.size() + sizeClass.liveBlocks
                != sizeClass.reservedBlocks) {
            return invalid(
                    GenomePoolInvariantError::ReferenceBlockCountMismatch,
                    INVALID_GENOME_ID);
        }
        for (const GeneId* block : sizeClass.freeBlocks) {
            if (!isReferenceBlockInClass(classIndex, block)) {
                return invalid(
                        GenomePoolInvariantError::InvalidReferenceBlock,
                        INVALID_GENOME_ID);
            }
            if (!freeReferenceBlocks[classIndex].insert(block).second) {
                return invalid(
                        GenomePoolInvariantError::DuplicateReferenceBlock,
                        INVALID_GENOME_ID);
            }
        }
    }

    std::array<std::unordered_set<const GeneId*>,
            GENOME_REFERENCE_SIZE_CLASSES.size()>
            liveReferenceBlocks;
    std::array<std::size_t,
            GENOME_REFERENCE_SIZE_CLASSES.size()>
            observedLiveBlocks{};
    std::size_t observedLive = 0;
    for (std::size_t rawId = 0; rawId < reservedSlots_; ++rawId) {
        const GenomeId id(static_cast<GenomeId::Value>(rawId));
        const Slot& current = *slot(id);
        if (!current.live) {
            if (!freeSeen[rawId]) {
                return invalid(GenomePoolInvariantError::InvalidFreeSlot, id);
            }
            continue;
        }
        ++observedLive;
        if (current.referenceCount == 0
                || current.nextFree != INVALID_GENOME_ID
                || current.generation == 0
                || current.genes == nullptr) {
            return invalid(GenomePoolInvariantError::InvalidLiveSlot, id);
        }
        if (!isSupportedGeneCount(current.geneCount)) {
            return invalid(GenomePoolInvariantError::InvalidGeneCount, id);
        }
        const auto expectedClass = referenceSizeClassIndex(current.geneCount);
        if (!expectedClass.has_value()
                || current.sizeClassIndex != *expectedClass) {
            return invalid(GenomePoolInvariantError::InvalidSizeClass, id);
        }
        if (!isReferenceBlockInClass(
                    current.sizeClassIndex, current.genes)
                || freeReferenceBlocks[current.sizeClassIndex].count(
                           current.genes)
                        != 0) {
            return invalid(
                    GenomePoolInvariantError::InvalidReferenceBlock, id);
        }
        if (!liveReferenceBlocks[current.sizeClassIndex]
                        .insert(current.genes)
                        .second) {
            return invalid(
                    GenomePoolInvariantError::DuplicateReferenceBlock, id);
        }
        ++observedLiveBlocks[current.sizeClassIndex];
        if (!isGeneIndexInBounds(
                    current.initialGeneIndex, current.geneCount)) {
            return invalid(
                    GenomePoolInvariantError::InvalidInitialGeneIndex,
                    id,
                    current.initialGeneIndex);
        }
        for (std::size_t gene = 0; gene < current.geneCount; ++gene) {
            const GeneIndex index(static_cast<GeneIndex::Value>(gene));
            if (!genePool_.isLive(current.genes[gene])) {
                return invalid(
                        GenomePoolInvariantError::InvalidGeneId,
                        id,
                        index);
            }
            if (!genePool_.materialize(
                                  current.genes[gene],
                                  index,
                                  current.geneCount,
                                  index == current.initialGeneIndex)
                            .has_value()) {
                return invalid(
                        GenomePoolInvariantError::InvalidMaterializedGene,
                        id,
                        index);
            }
        }
    }
    if (observedLive != liveSlots_) {
        return invalid(
                GenomePoolInvariantError::CountMismatch,
                INVALID_GENOME_ID);
    }
    for (std::size_t classIndex = 0;
            classIndex < referenceClasses_.size();
            ++classIndex) {
        if (observedLiveBlocks[classIndex]
                != referenceClasses_[classIndex].liveBlocks) {
            return invalid(
                    GenomePoolInvariantError::ReferenceBlockCountMismatch,
                    INVALID_GENOME_ID);
        }
    }
    return {};
}

GenomePool::Slot* GenomePool::slot(GenomeId id) noexcept {
    return const_cast<Slot*>(std::as_const(*this).slot(id));
}

const GenomePool::Slot* GenomePool::slot(GenomeId id) const noexcept {
    const std::size_t rawId = static_cast<std::size_t>(id.value());
    if (id == INVALID_GENOME_ID || rawId >= reservedSlots_) {
        return nullptr;
    }
    const std::size_t slabIndex = rawId / config_.slotsPerSlab;
    const std::size_t offset = rawId % config_.slotsPerSlab;
    if (slabIndex >= slabs_.size() || offset >= slabs_[slabIndex].size) {
        return nullptr;
    }
    return &slabs_[slabIndex].slots[offset];
}

GenomePoolError GenomePool::appendSlab(bool recordGrowth) noexcept {
    if (reservedSlots_ >= config_.maximumSlots) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::CapacityExhausted;
    }
    const std::size_t newSlotCount = std::min(
            config_.slotsPerSlab,
            config_.maximumSlots - reservedSlots_);
    Slab slab;
    try {
        slab.slots = std::make_unique<Slot[]>(newSlotCount);
        slab.size = newSlotCount;
        GenomeId next = freeHead_;
        for (std::size_t offset = newSlotCount; offset > 0; --offset) {
            const std::size_t rawId = reservedSlots_ + offset - 1;
            slab.slots[offset - 1].nextFree = next;
            next = GenomeId(static_cast<GenomeId::Value>(rawId));
        }
        slabs_.push_back(std::move(slab));
        freeHead_ = next;
    } catch (const std::bad_alloc&) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::AllocationFailed;
    } catch (const std::length_error&) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::AllocationFailed;
    }
    reservedSlots_ += newSlotCount;
    freeSlots_ += newSlotCount;
    if (recordGrowth) {
        ++counters_.slabGrowths;
    }
    return GenomePoolError::None;
}

GenomePoolError GenomePool::ensureFreeSlot() noexcept {
    return freeHead_ == INVALID_GENOME_ID
            ? appendSlab(true)
            : GenomePoolError::None;
}

GenomePoolError GenomePool::appendReferenceSlab(
        std::size_t sizeClassIndex,
        bool recordGrowth) noexcept {
    if (sizeClassIndex >= referenceClasses_.size()) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::InvalidGeneCount;
    }
    ReferenceSizeClass& sizeClass = referenceClasses_[sizeClassIndex];
    if (sizeClass.reservedBlocks >= config_.maximumSlots) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::CapacityExhausted;
    }
    const std::size_t capacity =
            GENOME_REFERENCE_SIZE_CLASSES[sizeClassIndex];
    const std::size_t desiredBlocks = referenceBlocksPerSlab(
            capacity, config_);
    const std::size_t blockCount = std::min(
            desiredBlocks,
            config_.maximumSlots - sizeClass.reservedBlocks);
    if (blockCount == 0
            || capacity > std::numeric_limits<std::size_t>::max()
                            / blockCount) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::CapacityExhausted;
    }

    ReferenceSlab slab;
    try {
        slab.entries = std::make_unique<GeneId[]>(capacity * blockCount);
        slab.blockCount = blockCount;
        sizeClass.freeBlocks.reserve(
                sizeClass.reservedBlocks + blockCount);
        sizeClass.slabs.push_back(std::move(slab));
        GeneId* base = sizeClass.slabs.back().entries.get();
        for (std::size_t block = blockCount; block > 0; --block) {
            sizeClass.freeBlocks.push_back(
                    base + (block - 1) * capacity);
        }
    } catch (const std::bad_alloc&) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::AllocationFailed;
    } catch (const std::length_error&) {
        ++counters_.failedCapacityRequests;
        return GenomePoolError::AllocationFailed;
    }
    sizeClass.reservedBlocks += blockCount;
    if (recordGrowth) {
        ++sizeClass.slabGrowths;
        ++counters_.slabGrowths;
    }
    return GenomePoolError::None;
}

GeneId* GenomePool::claimReferenceBlock(
        std::size_t sizeClassIndex) noexcept {
    assert(sizeClassIndex < referenceClasses_.size());
    ReferenceSizeClass& sizeClass = referenceClasses_[sizeClassIndex];
    assert(!sizeClass.freeBlocks.empty());
    GeneId* block = sizeClass.freeBlocks.back();
    sizeClass.freeBlocks.pop_back();
    ++sizeClass.liveBlocks;
    sizeClass.peakLiveBlocks = std::max(
            sizeClass.peakLiveBlocks, sizeClass.liveBlocks);
    return block;
}

void GenomePool::releaseReferenceBlock(
        std::size_t sizeClassIndex,
        GeneId* block) noexcept {
    assert(sizeClassIndex < referenceClasses_.size());
    assert(block != nullptr);
    assert(isReferenceBlockInClass(sizeClassIndex, block));
    ReferenceSizeClass& sizeClass = referenceClasses_[sizeClassIndex];
    const std::size_t capacity =
            GENOME_REFERENCE_SIZE_CLASSES[sizeClassIndex];
    std::fill(block, block + capacity, INVALID_GENE_ID);
    assert(sizeClass.liveBlocks > 0);
    --sizeClass.liveBlocks;
    assert(sizeClass.freeBlocks.size() < sizeClass.freeBlocks.capacity());
    sizeClass.freeBlocks.push_back(block);
}

GenomeId GenomePool::claimFreeSlot(
        GeneId* genes,
        std::size_t geneCount,
        std::size_t sizeClassIndex,
        GeneIndex initialGeneIndex) noexcept {
    assert(freeHead_ != INVALID_GENOME_ID);
    assert(genes != nullptr);
    assert(sizeClassIndex < referenceClasses_.size());
    assert(referenceClassCapacity(geneCount)
            == GENOME_REFERENCE_SIZE_CLASSES[sizeClassIndex]);
    assert(isGeneIndexInBounds(initialGeneIndex, geneCount));
    const GenomeId id = freeHead_;
    Slot& target = *slot(id);
    freeHead_ = target.nextFree;
    target.genes = genes;
    target.referenceCount = 1;
    target.nextFree = INVALID_GENOME_ID;
    target.generation = nextGeneration(target.generation);
    target.initialGeneIndex = initialGeneIndex;
    target.geneCount = static_cast<std::uint16_t>(geneCount);
    target.sizeClassIndex = static_cast<std::uint8_t>(sizeClassIndex);
    target.live = true;
    --freeSlots_;
    ++liveSlots_;
    peakLiveSlots_ = std::max(peakLiveSlots_, liveSlots_);
    return id;
}

void GenomePool::reclaimSlot(GenomeId id, Slot& target) noexcept {
    releaseReferenceBlock(target.sizeClassIndex, target.genes);
    target.genes = nullptr;
    target.referenceCount = 0;
    target.initialGeneIndex = INVALID_GENE_INDEX;
    target.geneCount = 0;
    target.sizeClassIndex = 0;
    target.live = false;
    target.nextFree = freeHead_;
    freeHead_ = id;
    --liveSlots_;
    ++freeSlots_;
    ++counters_.reclaimedSlots;
}

GenomePoolResult GenomePool::failure(
        GenomePoolError error,
        GenePoolError genePoolError,
        CanonicalGeneValidation validation,
        GeneIndex failedGeneIndex) noexcept {
    if (error == GenomePoolError::CapacityExhausted
            || error == GenomePoolError::AllocationFailed) {
        // Slab growth records these failures at the allocation boundary.
    } else if (error == GenomePoolError::GenePoolFailure
            || error == GenomePoolError::InvalidGeneData) {
        ++counters_.failedGeneOperations;
    } else {
        ++counters_.failedReferenceOperations;
    }
    return {INVALID_GENOME_ID,
            error,
            genePoolError,
            validation,
            failedGeneIndex};
}

void GenomePool::releaseGeneReferencePrefix(
        const GeneId* ids,
        std::size_t count) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        const GenePoolError released = genePool_.release(ids[index]);
        (void)released;
        assert(released == GenePoolError::None);
    }
}

bool GenomePool::isReferenceBlockInClass(
        std::size_t sizeClassIndex,
        const GeneId* block) const noexcept {
    if (sizeClassIndex >= referenceClasses_.size() || block == nullptr) {
        return false;
    }
    const std::size_t capacity =
            GENOME_REFERENCE_SIZE_CLASSES[sizeClassIndex];
    const std::uintptr_t blockAddress =
            reinterpret_cast<std::uintptr_t>(block);
    for (const ReferenceSlab& slab :
            referenceClasses_[sizeClassIndex].slabs) {
        const std::uintptr_t base =
                reinterpret_cast<std::uintptr_t>(slab.entries.get());
        const std::size_t entryCount = slab.blockCount * capacity;
        const std::uintptr_t end = base + entryCount * sizeof(GeneId);
        if (blockAddress < base || blockAddress >= end) {
            continue;
        }
        const std::uintptr_t offset = blockAddress - base;
        return offset % (capacity * sizeof(GeneId)) == 0;
    }
    return false;
}

} // namespace cellularity
