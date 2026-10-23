#include "core/GenePool.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace cellularity {
namespace {

constexpr std::uint32_t nextGeneration(std::uint32_t generation) noexcept {
    return generation == std::numeric_limits<std::uint32_t>::max()
            ? 1U
            : generation + 1U;
}

} // namespace

GenePool::GenePool(GenePoolConfig config) : config_(config) {
    const std::size_t maximumRepresentableSlots =
            static_cast<std::size_t>(INVALID_GENE_ID.value());
    if (config_.slotsPerSlab == 0
            || config_.initialReservedSlots == 0
            || config_.maximumSlots == 0
            || config_.maximumSlots > maximumRepresentableSlots
            || config_.initialReservedSlots > config_.maximumSlots) {
        throw std::invalid_argument("Invalid GenePool configuration");
    }

    const GenePoolError firstSlab = appendSlab(false);
    if (firstSlab != GenePoolError::None) {
        throw std::bad_alloc();
    }

    // The first slab is linked in ascending order, so slot zero is its head.
    if (freeHead_ != DEFAULT_GENE_ID) {
        throw std::logic_error("GenePool did not reserve default slot zero");
    }
    Slot& defaultSlot = *slot(DEFAULT_GENE_ID);
    freeHead_ = defaultSlot.nextFree;
    defaultSlot.nextFree = INVALID_GENE_ID;
    defaultSlot.gene.emplace(CanonicalGeneDefaults::value());
    defaultSlot.canonicalHash = canonicalGeneHash(*defaultSlot.gene);
    defaultSlot.generation = 1;
    defaultSlot.live = true;
    --freeSlots_;
    liveSlots_ = 1;
    peakLiveSlots_ = 1;

    internIndex_[defaultSlot.canonicalHash].push_back(DEFAULT_GENE_ID);
    internIndexEntries_ = 1;

    while (reservedSlots_ < config_.initialReservedSlots) {
        const GenePoolError result = appendSlab(false);
        if (result != GenePoolError::None) {
            throw std::bad_alloc();
        }
    }
}

std::size_t GenePool::slotStorageBytes() noexcept {
    return sizeof(Slot);
}

GenePoolInternResult GenePool::intern(const CanonicalGene& gene) {
    ++counters_.internLookups;
    const std::uint64_t hash = canonicalGeneHash(gene);
    const auto existingBucket = internIndex_.find(hash);
    if (existingBucket != internIndex_.end()) {
        for (GeneId candidateId : existingBucket->second) {
            Slot* candidate = slot(candidateId);
            if (candidate == nullptr
                    || !candidate->live
                    || !candidate->gene.has_value()
                    || candidate->canonicalHash != hash) {
                return failure(GenePoolError::CorruptInternIndex);
            }
            if (canonicalGeneEqual(*candidate->gene, gene)) {
                ++counters_.internHits;
                const GenePoolError retained = incrementReference(*candidate);
                return retained == GenePoolError::None
                        ? GenePoolInternResult{candidateId, GenePoolError::None}
                        : failure(retained);
            }
            ++counters_.hashCollisions;
        }
    }

    ++counters_.internMisses;
    if (freeHead_ == INVALID_GENE_ID) {
        const GenePoolError growth = appendSlab(true);
        if (growth != GenePoolError::None) {
            return failure(growth);
        }
    }

    const GeneId newId = freeHead_;
    Slot& newSlot = *slot(newId);

    bool createdBucket = false;
    try {
        newSlot.gene.emplace(gene);
        auto bucketResult = internIndex_.try_emplace(hash);
        createdBucket = bucketResult.second;
        try {
            bucketResult.first->second.push_back(newId);
        } catch (...) {
            if (createdBucket && bucketResult.first->second.empty()) {
                internIndex_.erase(bucketResult.first);
            }
            throw;
        }
    } catch (const std::bad_alloc&) {
        newSlot.gene.reset();
        ++counters_.failedCapacityRequests;
        return failure(GenePoolError::AllocationFailed);
    } catch (const std::length_error&) {
        newSlot.gene.reset();
        ++counters_.failedCapacityRequests;
        return failure(GenePoolError::AllocationFailed);
    } catch (...) {
        newSlot.gene.reset();
        throw;
    }

    freeHead_ = newSlot.nextFree;
    newSlot.nextFree = INVALID_GENE_ID;
    newSlot.canonicalHash = hash;
    newSlot.referenceCount = 1;
    newSlot.generation = nextGeneration(newSlot.generation);
    newSlot.live = true;
    --freeSlots_;
    ++liveSlots_;
    ++internIndexEntries_;
    peakLiveSlots_ = std::max(peakLiveSlots_, liveSlots_);
    return {newId, GenePoolError::None};
}

GenePoolError GenePool::retain(GeneId id) noexcept {
    ++counters_.retainCalls;
    Slot* target = slot(id);
    if (target == nullptr || id == INVALID_GENE_ID) {
        ++counters_.failedReferenceOperations;
        return GenePoolError::InvalidGeneId;
    }
    if (!target->live || !target->gene.has_value()) {
        ++counters_.failedReferenceOperations;
        return GenePoolError::ReleasedGeneId;
    }
    return incrementReference(*target);
}

GenePoolError GenePool::release(GeneId id) noexcept {
    ++counters_.releaseCalls;
    Slot* target = slot(id);
    if (target == nullptr || id == INVALID_GENE_ID) {
        ++counters_.failedReferenceOperations;
        return GenePoolError::InvalidGeneId;
    }
    if (!target->live || !target->gene.has_value()) {
        ++counters_.failedReferenceOperations;
        return GenePoolError::ReleasedGeneId;
    }
    if (target->referenceCount == 0) {
        ++counters_.failedReferenceOperations;
        return GenePoolError::ReferenceCountUnderflow;
    }

    if (id == DEFAULT_GENE_ID || target->referenceCount > 1) {
        --target->referenceCount;
        return GenePoolError::None;
    }

    const auto bucket = internIndex_.find(target->canonicalHash);
    if (bucket == internIndex_.end()) {
        ++counters_.failedReferenceOperations;
        return GenePoolError::CorruptInternIndex;
    }
    const auto entry = std::find(bucket->second.begin(), bucket->second.end(), id);
    if (entry == bucket->second.end()) {
        ++counters_.failedReferenceOperations;
        return GenePoolError::CorruptInternIndex;
    }

    bucket->second.erase(entry);
    if (bucket->second.empty()) {
        internIndex_.erase(bucket);
    }
    --internIndexEntries_;

    target->referenceCount = 0;
    target->canonicalHash = 0;
    target->gene.reset();
    target->live = false;
    target->nextFree = freeHead_;
    freeHead_ = id;
    --liveSlots_;
    ++freeSlots_;
    ++counters_.reclaimedSlots;
    return GenePoolError::None;
}

GenePoolError GenePool::reserve(std::size_t minimumSlots) noexcept {
    if (minimumSlots > config_.maximumSlots) {
        ++counters_.failedCapacityRequests;
        return GenePoolError::CapacityExhausted;
    }
    while (reservedSlots_ < minimumSlots) {
        const GenePoolError result = appendSlab(true);
        if (result != GenePoolError::None) {
            return result;
        }
    }
    return GenePoolError::None;
}

std::optional<GeneView> GenePool::view(GeneId id) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live || !target->gene.has_value()) {
        return std::nullopt;
    }
    return GeneView(target->gene->data());
}

GeneView GenePool::contextualView(
        GeneId id,
        GeneIndex owner,
        bool isInitial) const noexcept {
    const Slot* target = slot(id);
    assert(target != nullptr && target->live && target->gene.has_value());
    assert(isExplicitGeneIndex(owner));
    return GeneView(target->gene->data(), owner, isInitial);
}

const GeneData* GenePool::dataPointer(GeneId id) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live || !target->gene.has_value()) {
        return nullptr;
    }
    return &target->gene->data();
}

std::optional<GeneData> GenePool::materialize(
        GeneId id,
        GeneIndex owner,
        std::size_t geneCount,
        bool isInitial) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live || !target->gene.has_value()) {
        return std::nullopt;
    }
    return materializeCanonicalGene(
            *target->gene, owner, geneCount, isInitial);
}

std::optional<GeneData> GenePool::copyForMutation(GeneId id) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live || !target->gene.has_value()) {
        return std::nullopt;
    }
    return target->gene->data();
}

bool GenePool::isLive(GeneId id) const noexcept {
    const Slot* target = slot(id);
    return target != nullptr && target->live && target->gene.has_value();
}

std::optional<std::uint32_t> GenePool::referenceCount(GeneId id) const noexcept {
    const Slot* target = slot(id);
    if (target == nullptr || !target->live || !target->gene.has_value()) {
        return std::nullopt;
    }
    return target->referenceCount;
}

GenePoolDiagnostics GenePool::diagnostics() const noexcept {
    GenePoolDiagnostics result;
    result.liveSlots = liveSlots_;
    result.freeSlots = freeSlots_;
    result.reservedSlots = reservedSlots_;
    result.slabCount = slabs_.size();
    result.peakLiveSlots = peakLiveSlots_;
    result.liveGeneDataBytes = static_cast<std::uint64_t>(liveSlots_)
            * sizeof(GeneData);
    result.reservedSlotStorageBytes =
            static_cast<std::uint64_t>(reservedSlots_) * sizeof(Slot);
    result.internIndexBuckets = internIndex_.size();
    result.internIndexEntries = internIndexEntries_;
    result.internLookups = counters_.internLookups;
    result.internHits = counters_.internHits;
    result.internMisses = counters_.internMisses;
    result.hashCollisions = counters_.hashCollisions;
    result.retainCalls = counters_.retainCalls;
    result.releaseCalls = counters_.releaseCalls;
    result.reclaimedSlots = counters_.reclaimedSlots;
    result.slabGrowths = counters_.slabGrowths;
    result.failedCapacityRequests = counters_.failedCapacityRequests;
    result.failedReferenceOperations = counters_.failedReferenceOperations;
    const Slot* defaultSlot = slot(DEFAULT_GENE_ID);
    result.defaultGeneReferences = defaultSlot == nullptr
            ? 0
            : defaultSlot->referenceCount;
    return result;
}

void GenePool::resetDiagnosticCounters() noexcept {
    counters_ = {};
    peakLiveSlots_ = liveSlots_;
}

GenePoolInvariantReport GenePool::validateInvariants() const {
    const auto invalid = [](GenePoolInvariantError error, GeneId id) {
        return GenePoolInvariantReport{error, id};
    };
    if (liveSlots_ + freeSlots_ != reservedSlots_) {
        return invalid(GenePoolInvariantError::CountMismatch, INVALID_GENE_ID);
    }

    const Slot* defaultSlot = slot(DEFAULT_GENE_ID);
    if (defaultSlot == nullptr) {
        return invalid(
                GenePoolInvariantError::MissingDefaultSlot,
                DEFAULT_GENE_ID);
    }
    if (!defaultSlot->live
            || !defaultSlot->gene.has_value()
            || defaultSlot->nextFree != INVALID_GENE_ID
            || !canonicalGeneEqual(
                    *defaultSlot->gene,
                    CanonicalGeneDefaults::value())) {
        return invalid(
                GenePoolInvariantError::InvalidDefaultSlot,
                DEFAULT_GENE_ID);
    }

    std::vector<bool> freeSeen(reservedSlots_, false);
    std::size_t observedFree = 0;
    for (GeneId id = freeHead_; id != INVALID_GENE_ID;) {
        const Slot* freeSlot = slot(id);
        if (freeSlot == nullptr) {
            return invalid(GenePoolInvariantError::FreeListOutOfRange, id);
        }
        if (freeSeen[id.value()]) {
            return invalid(GenePoolInvariantError::FreeListCycle, id);
        }
        freeSeen[id.value()] = true;
        ++observedFree;
        if (freeSlot->live
                || freeSlot->gene.has_value()
                || freeSlot->referenceCount != 0
                || freeSlot->canonicalHash != 0) {
            return invalid(GenePoolInvariantError::InvalidFreeSlot, id);
        }
        id = freeSlot->nextFree;
    }
    if (observedFree != freeSlots_) {
        return invalid(GenePoolInvariantError::CountMismatch, INVALID_GENE_ID);
    }

    std::vector<std::uint8_t> indexOccurrences(reservedSlots_, 0);
    std::size_t observedIndexEntries = 0;
    for (const auto& [hash, ids] : internIndex_) {
        for (std::size_t left = 0; left < ids.size(); ++left) {
            const GeneId id = ids[left];
            const Slot* indexedSlot = slot(id);
            if (indexedSlot == nullptr
                    || !indexedSlot->live
                    || !indexedSlot->gene.has_value()
                    || indexedSlot->canonicalHash != hash) {
                return invalid(GenePoolInvariantError::InvalidIndexEntry, id);
            }
            if (++indexOccurrences[id.value()] != 1) {
                return invalid(GenePoolInvariantError::DuplicateIndexEntry, id);
            }
            ++observedIndexEntries;
            for (std::size_t right = left + 1; right < ids.size(); ++right) {
                const Slot* other = slot(ids[right]);
                if (other != nullptr
                        && other->live
                        && other->gene.has_value()
                        && canonicalGeneEqual(*indexedSlot->gene, *other->gene)) {
                    return invalid(
                            GenePoolInvariantError::DuplicateCanonicalGene,
                            id);
                }
            }
        }
    }
    if (observedIndexEntries != internIndexEntries_
            || observedIndexEntries != liveSlots_) {
        return invalid(GenePoolInvariantError::CountMismatch, INVALID_GENE_ID);
    }

    std::size_t observedLive = 0;
    for (std::size_t rawId = 0; rawId < reservedSlots_; ++rawId) {
        const GeneId id(static_cast<GeneId::Value>(rawId));
        const Slot& current = *slot(id);
        if (!current.live) {
            if (!freeSeen[rawId]) {
                return invalid(GenePoolInvariantError::InvalidFreeSlot, id);
            }
            continue;
        }
        ++observedLive;
        if (!current.gene.has_value()
                || current.nextFree != INVALID_GENE_ID
                || current.generation == 0
                || (id != DEFAULT_GENE_ID && current.referenceCount == 0)) {
            return invalid(GenePoolInvariantError::InvalidLiveSlot, id);
        }
        if (canonicalGeneHash(*current.gene) != current.canonicalHash) {
            return invalid(GenePoolInvariantError::InvalidCachedHash, id);
        }
        if (indexOccurrences[rawId] != 1) {
            return invalid(GenePoolInvariantError::MissingIndexEntry, id);
        }
    }
    if (observedLive != liveSlots_) {
        return invalid(GenePoolInvariantError::CountMismatch, INVALID_GENE_ID);
    }
    return {};
}

GenePool::Slot* GenePool::slot(GeneId id) noexcept {
    return const_cast<Slot*>(std::as_const(*this).slot(id));
}

const GenePool::Slot* GenePool::slot(GeneId id) const noexcept {
    const std::size_t rawId = static_cast<std::size_t>(id.value());
    if (id == INVALID_GENE_ID || rawId >= reservedSlots_) {
        return nullptr;
    }
    const std::size_t slabIndex = rawId / config_.slotsPerSlab;
    const std::size_t offset = rawId % config_.slotsPerSlab;
    if (slabIndex >= slabs_.size() || offset >= slabs_[slabIndex].size) {
        return nullptr;
    }
    return &slabs_[slabIndex].slots[offset];
}

GenePoolError GenePool::appendSlab(bool recordGrowth) noexcept {
    if (reservedSlots_ >= config_.maximumSlots) {
        ++counters_.failedCapacityRequests;
        return GenePoolError::CapacityExhausted;
    }
    const std::size_t newSlotCount = std::min(
            config_.slotsPerSlab,
            config_.maximumSlots - reservedSlots_);

    Slab slab;
    try {
        slab.slots = std::make_unique<Slot[]>(newSlotCount);
        slab.size = newSlotCount;
        GeneId next = freeHead_;
        for (std::size_t offset = newSlotCount; offset > 0; --offset) {
            const std::size_t rawId = reservedSlots_ + offset - 1;
            slab.slots[offset - 1].nextFree = next;
            next = GeneId(static_cast<GeneId::Value>(rawId));
        }
        slabs_.push_back(std::move(slab));
        freeHead_ = next;
    } catch (const std::bad_alloc&) {
        ++counters_.failedCapacityRequests;
        return GenePoolError::AllocationFailed;
    } catch (const std::length_error&) {
        ++counters_.failedCapacityRequests;
        return GenePoolError::AllocationFailed;
    }

    reservedSlots_ += newSlotCount;
    freeSlots_ += newSlotCount;
    if (recordGrowth) {
        ++counters_.slabGrowths;
    }
    return GenePoolError::None;
}

GenePoolError GenePool::incrementReference(Slot& target) noexcept {
    if (target.referenceCount == std::numeric_limits<std::uint32_t>::max()) {
        ++counters_.failedReferenceOperations;
        return GenePoolError::ReferenceCountOverflow;
    }
    ++target.referenceCount;
    return GenePoolError::None;
}

GenePoolInternResult GenePool::failure(GenePoolError error) noexcept {
    if (error == GenePoolError::CorruptInternIndex) {
        ++counters_.failedReferenceOperations;
    }
    return {INVALID_GENE_ID, error};
}

} // namespace cellularity
