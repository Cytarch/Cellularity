#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "core/GenomePool.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity;
using namespace cellularity::tests;

namespace {

GenomeData makeGenome(
        std::size_t geneCount = FIXED_GENOME_GENE_COUNT,
        int variant = 0,
        std::size_t initialIndex = 0) {
    GenomeData genome(geneCount);
    for (std::size_t index = 0; index < genome.size(); ++index) {
        const auto materialized = materializeCanonicalGene(
                CanonicalGeneDefaults::value(),
                GeneIndex(static_cast<GeneIndex::Value>(index)),
                genome.size(),
                index == initialIndex);
        assertTrue(materialized.has_value(), "Default fixed Gene must materialize");
        genome[index] = *materialized;
    }
    const std::size_t variantIndex = std::min<std::size_t>(3, geneCount - 1);
    GeneBuilder(genome[variantIndex]).setColor(0, static_cast<float>(variant + 1) / 128.0f);
    return genome;
}

std::size_t sizeClassIndex(std::size_t geneCount) {
    const std::size_t capacity = GenomePool::referenceClassCapacity(geneCount);
    const auto found = std::find(
            GENOME_REFERENCE_SIZE_CLASSES.begin(),
            GENOME_REFERENCE_SIZE_CLASSES.end(),
            capacity);
    assertTrue(found != GENOME_REFERENCE_SIZE_CLASSES.end(),
            "Supported Genome length has a reference size class");
    return static_cast<std::size_t>(
            std::distance(GENOME_REFERENCE_SIZE_CLASSES.begin(), found));
}

void assertValid(const GenomePool& pool, const std::string& context) {
    const GenomePoolInvariantReport report = pool.validateInvariants();
    assertTrue(report.isValid(),
            context + ": invariant error "
                    + std::to_string(static_cast<int>(report.error))
                    + " at GenomeId "
                    + std::to_string(report.genomeId.value())
                    + " GeneIndex "
                    + std::to_string(report.geneIndex.value()));
}

void testImportViewsAndMetadataRoundTrip() {
    GenePool genes({64, 64, 512});
    GenomePool genomes(genes, {4, 4, 16});
    const GenomeData source = makeGenome(FIXED_GENOME_GENE_COUNT, 4, 7);
    const GenomePoolResult created = genomes.create(source);
    assertTrue(created.hasValue(), "Fixed Genome import succeeds");
    assertTrue(genomes.referenceCount(created.id) == 1,
            "A new GenomeRecord has one Cell owner");
    assertTrue(genomes.metadata(created.id)->initialGeneIndex == GeneIndex(7),
            "Initial Gene metadata moves into GenomeRecord");

    const auto view = genomes.view(created.id);
    assertTrue(view.has_value() && view->geneCount() == source.size(),
            "GenomePool exposes a bounded fixed GenomeView");
    assertTrue(view->gene(GeneIndex(7)).initialCompatibilityFlag()
                    && !view->gene(GeneIndex(6)).initialCompatibilityFlag(),
            "Pooled GeneView projects Genome-owned initial metadata");
    assertTrue(view->gene(GeneIndex(3)).child1GeneIndex() == GeneIndex(3)
                    && view->gene(GeneIndex(3)).virusFromGeneIndex()
                            == GeneIndex(3),
            "Pooled GeneView resolves canonical self references locally");

    const auto materialized = genomes.materialize(created.id);
    assertTrue(materialized.has_value(), "A pooled Genome materializes at a boundary");
    for (std::size_t index = 0; index < source.size(); ++index) {
        const GeneView actual((*materialized)[index]);
        assertTrue(actual.child1GeneIndex().value() == index
                        && actual.virusFromGeneIndex().value() == index,
                "Materialization restores explicit owner-local references");
        assertTrue(actual.initialCompatibilityFlag() == (index == 7),
                "Materialization restores exactly one initial flag");
    }
    assertNear(GeneView(source[3]).color(0),
            GeneView((*materialized)[3]).color(0), 0.0,
            "Genome materialization preserves payload values");
    assertTrue(genomes.release(created.id) == GenomePoolError::None,
            "Imported Genome releases");
    assertTrue(genes.diagnostics().liveSlots == 1,
            "Destroying the only Genome balances every Gene reference");
    assertValid(genomes, "Import and materialization");
}

void testCloneSharingAndWholeGenomeCow() {
    GenePool genes({64, 64, 512});
    GenomePool genomes(genes, {4, 4, 16});
    const GenomeData source = makeGenome(FIXED_GENOME_GENE_COUNT, 1);
    const GenomePoolResult created = genomes.create(source);
    assertTrue(created.hasValue(), "CoW source Genome import succeeds");
    assertTrue(genomes.retain(created.id) == GenomePoolError::None,
            "Cell split retains a GenomeRecord");
    assertTrue(genomes.referenceCount(created.id) == 2,
            "Two clone Cells share one GenomeRecord");

    const auto oldTargetId = genomes.geneId(created.id, GeneIndex(3));
    const auto oldNeighborId = genomes.geneId(created.id, GeneIndex(4));
    GeneData candidate = *genomes.copyGeneForMutation(created.id, GeneIndex(3));
    GeneBuilder(candidate).setColor(1, 0.75f);
    const GenomePoolResult replaced =
            genomes.replaceGene(created.id, GeneIndex(3), candidate);
    assertTrue(replaced.hasValue() && replaced.changed && replaced.detached
                    && replaced.id != created.id,
            "First write detaches one shared GenomeRecord");
    assertTrue(genomes.referenceCount(created.id) == 1
                    && genomes.referenceCount(replaced.id) == 1,
            "CoW transfers exactly one Cell owner to the detached record");
    assertTrue(genomes.geneId(created.id, GeneIndex(3)) == oldTargetId
                    && genomes.geneId(replaced.id, GeneIndex(3)) != oldTargetId,
            "Point mutation changes only the detached lineage");
    assertTrue(genomes.geneId(created.id, GeneIndex(4)) == oldNeighborId
                    && genomes.geneId(replaced.id, GeneIndex(4)) == oldNeighborId,
            "Point mutation preserves every unrelated GeneId entry");
    assertNear(0.75f,
            genomes.geneView(replaced.id, GeneIndex(3))->color(1), 0.0,
            "Detached record resolves the new immutable Gene");
    assertNear(0.0f,
            genomes.geneView(created.id, GeneIndex(3))->color(1), 0.0,
            "Original clone keeps the old immutable Gene");

    const GenomePoolDiagnostics diagnostics = genomes.diagnostics();
    assertTrue(diagnostics.detachedRecords == 1
                    && diagnostics.copiedGeneIds == source.size()
                    && diagnostics.replacedGenes == 1,
            "CoW diagnostics expose one logical-length ID copy and one swap");
    const std::size_t classIndex = sizeClassIndex(source.size());
    const auto& sizeClass = diagnostics.sizeClasses[classIndex];
    assertTrue(diagnostics.reservedRecordStorageBytes
                            == diagnostics.reservedSlots
                                    * GenomePool::slotStorageBytes()
                    && diagnostics.reservedSlotStorageBytes
                            == diagnostics.reservedRecordStorageBytes
                                    + diagnostics.reservedReferenceStorageBytes,
            "Genome diagnostics separately account for records and slabs");
    assertTrue(diagnostics.liveGeneReferenceBytes
                            == 2 * source.size() * sizeof(GeneId)
                    && diagnostics.liveReferenceCapacityBytes
                            == 2 * sizeClass.capacity * sizeof(GeneId)
                    && sizeClass.liveBlocks == 2
                    && sizeClass.liveBlocks + sizeClass.freeBlocks
                            == sizeClass.reservedBlocks,
            "Genome diagnostics distinguish logical IDs from size-class capacity");
    assertTrue(GenomePool::slotStorageBytes()
                    < FIXED_GENOME_GENE_COUNT * sizeof(GeneId),
            "Genome records no longer embed a fixed 40-entry ID table");
    assertTrue(genomes.release(created.id) == GenomePoolError::None
                    && genomes.release(replaced.id) == GenomePoolError::None,
            "Both CoW lineages release independently");
    assertTrue(genes.diagnostics().liveSlots == 1,
            "CoW teardown balances shared and replaced Gene references");
    assertValid(genomes, "Whole-Genome CoW");
}

void testNoOpReplacementDefaultAndCacheVerification() {
    GenePool genes({64, 64, 512});
    GenomePool genomes(genes, {2, 2, 8});
    const GenomePoolResult defaults = genomes.createDefault();
    assertTrue(defaults.hasValue(), "Canonical default Genome creation succeeds");
    assertTrue(genes.referenceCount(DEFAULT_GENE_ID)
                    == FIXED_GENOME_GENE_COUNT,
            "One default record owns 40 default Gene references");

    const GeneId active = *genomes.geneId(defaults.id, GeneIndex(0));
    assertTrue(genomes.verifyCachedGene(defaults.id, GeneIndex(0), active),
            "Matching active Gene cache verifies");
    assertTrue(!genomes.verifyCachedGene(
                       defaults.id, GeneIndex(0), INVALID_GENE_ID),
            "Stale active Gene cache is detected");

    const GeneData unchanged =
            *genomes.copyGeneForMutation(defaults.id, GeneIndex(0));
    const GenomePoolResult replacement =
            genomes.replaceGene(defaults.id, GeneIndex(0), unchanged);
    assertTrue(replacement.hasValue() && replacement.id == defaults.id
                    && !replacement.changed && !replacement.detached,
            "Canonical no-op mutation neither detaches nor swaps IDs");
    const GenomePoolDiagnostics diagnostics = genomes.diagnostics();
    assertTrue(diagnostics.unchangedGeneReplacements == 1
                    && diagnostics.activeCacheVerificationChecks == 2
                    && diagnostics.activeCacheVerificationFailures == 1,
            "No-op and cache diagnostics are exact");
    assertTrue(genomes.release(defaults.id) == GenomePoolError::None,
            "Default Genome releases");
    assertTrue(genes.referenceCount(DEFAULT_GENE_ID) == 0,
            "Immortal default Gene returns to zero logical owners");
    assertValid(genomes, "Default and cache verification");
}

void testCapacityFailureFreelistReuseAndStress() {
    GenePool genes({64, 64, 512});
    GenomePool bounded(genes, {2, 2, 2});
    const GenomePoolResult first = bounded.create(makeGenome(40, 10));
    const GenomePoolResult second = bounded.create(makeGenome(40, 11));
    const GenomePoolResult exhausted = bounded.create(makeGenome(40, 12));
    assertTrue(first.id == GenomeId(0) && second.id == GenomeId(1),
            "Genome freelist allocation is deterministic and ascending");
    assertTrue(!exhausted.hasValue()
                    && exhausted.error == GenomePoolError::CapacityExhausted,
            "Bounded Genome capacity failure is explicit");
    assertValid(bounded, "Capacity failure rollback");
    assertTrue(bounded.release(first.id) == GenomePoolError::None,
            "A fixed Genome slot can be reclaimed");
    const GenomePoolResult reused = bounded.create(makeGenome(40, 12));
    assertTrue(reused.hasValue() && reused.id == first.id,
            "Most recently reclaimed Genome slot is reused");
    assertTrue(bounded.release(second.id) == GenomePoolError::None
                    && bounded.release(reused.id) == GenomePoolError::None,
            "Bounded records release after reuse");
    assertValid(bounded, "Freelist reuse");

    GenomePool stress(genes, {8, 8, 64});
    for (int cycle = 0; cycle < 100; ++cycle) {
        const GenomePoolResult root = stress.create(makeGenome(40, cycle % 16));
        assertTrue(root.hasValue(), "Stress root creation succeeds");
        for (int clone = 0; clone < 31; ++clone) {
            assertTrue(stress.retain(root.id) == GenomePoolError::None,
                    "Stress split shares one record");
        }
        assertTrue(stress.referenceCount(root.id) == 32,
                "Large clone set remains one GenomeRecord");
        for (int clone = 0; clone < 32; ++clone) {
            assertTrue(stress.release(root.id) == GenomePoolError::None,
                    "Stress clone ownership balances");
        }
        assertTrue(stress.diagnostics().liveSlots == 0,
                "Stress cycle leaves no live Genome records");
        assertValid(stress, "Repeated clone/release cycle");
    }
    assertTrue(genes.diagnostics().liveSlots == 1,
            "Stress cycles leave no non-default Gene slots");
}

void testDynamicBoundaryLengthsAndExactCowCopies() {
    constexpr std::array<std::size_t, 5> lengths{1, 2, 40, 150, 256};
    constexpr std::array<std::size_t, 5> expectedCapacities{1, 2, 64, 256, 256};

    for (std::size_t caseIndex = 0; caseIndex < lengths.size(); ++caseIndex) {
        const std::size_t length = lengths[caseIndex];
        GenePool genes({512, 512, 4096});
        GenomePool genomes(genes, {8, 8, 32});
        const GenomeData source = makeGenome(
                length,
                static_cast<int>(caseIndex + 20),
                length - 1);
        const GenomePoolResult created = genomes.create(source);
        assertTrue(created.hasValue(),
                "Boundary-length Genome import succeeds");
        assertTrue(genomes.geneCount(created.id) == length,
                "GenomePool preserves the exact logical length");
        const auto view = genomes.view(created.id);
        assertTrue(view.has_value() && view->geneCount() == length,
                "GenomeView exposes the exact logical length");
        assertTrue(view->gene(GeneIndex(
                                      static_cast<GeneIndex::Value>(length - 1)))
                           .initialCompatibilityFlag(),
                "Last Gene can own initial metadata at every boundary length");
        assertTrue(!genomes.geneView(
                                   created.id,
                                   GeneIndex(static_cast<GeneIndex::Value>(length)))
                           .has_value(),
                "Logical length, not slab capacity, bounds Gene access");

        const GenomePoolDiagnostics beforeSplit = genomes.diagnostics();
        const std::size_t classIndex = sizeClassIndex(length);
        assertTrue(beforeSplit.sizeClasses[classIndex].capacity
                                == expectedCapacities[caseIndex]
                        && beforeSplit.sizeClasses[classIndex].liveBlocks == 1,
                "Boundary length uses its expected stable size class");

        assertTrue(genomes.retain(created.id) == GenomePoolError::None,
                "Boundary-length Cell split shares a Genome in O(1)");
        GeneData candidate = *genomes.copyGeneForMutation(
                created.id,
                GeneIndex(static_cast<GeneIndex::Value>(length - 1)));
        const float replacementColor =
                0.125f + static_cast<float>(caseIndex) * 0.0625f;
        GeneBuilder(candidate).setColor(2, replacementColor);
        const GenomePoolResult mutated = genomes.replaceGene(
                created.id,
                GeneIndex(static_cast<GeneIndex::Value>(length - 1)),
                candidate);
        assertTrue(mutated.hasValue() && mutated.changed && mutated.detached,
                "First mutation after split detaches the boundary-length Genome");
        const GenomePoolDiagnostics afterMutation = genomes.diagnostics();
        assertTrue(afterMutation.copiedGeneIds == length,
                "CoW copies only the Genome's logical GeneId count");
        const auto materialized = genomes.materialize(mutated.id);
        assertTrue(materialized.has_value()
                        && materialized->size() == length,
                "Boundary-length Genome materializes without padding");
        assertNear(replacementColor,
                GeneView(materialized->back()).color(2),
                0.0,
                "Boundary-length mutation reaches the requested last Gene");
        assertTrue(genomes.release(created.id) == GenomePoolError::None
                        && genomes.release(mutated.id) == GenomePoolError::None,
                "Both boundary-length lineages release independently");
        assertTrue(genes.diagnostics().liveSlots == 1,
                "Boundary-length teardown balances Gene ownership");
        assertValid(genomes, "Dynamic boundary length");
    }
}

void testInvalidLengthsAndReferenceSlabReservation() {
    GenePool genes({512, 512, 4096});
    GenomePoolConfig config;
    config.slotsPerSlab = 8;
    config.initialReservedSlots = 8;
    config.maximumSlots = 64;
    config.referenceSlabTargetBytes = 256;
    config.minimumReferenceBlocksPerSlab = 2;
    config.maximumReferenceBlocksPerSlab = 8;
    GenomePool genomes(genes, config);

    const GenomeData empty;
    const GenomeData oversized(MAX_SUPPORTED_GENES_PER_GENOME + 1);
    assertTrue(genomes.create(empty).error == GenomePoolError::InvalidGeneCount
                    && genomes.create(oversized).error
                            == GenomePoolError::InvalidGeneCount
                    && genomes.createDefault(0).error
                            == GenomePoolError::InvalidGeneCount
                    && genomes.createDefault(MAX_SUPPORTED_GENES_PER_GENOME + 1)
                                    .error
                            == GenomePoolError::InvalidGeneCount,
            "GenomePool rejects lengths outside the 1..256 contract");

    for (std::size_t length : std::array<std::size_t, 5>{1, 2, 40, 150, 256}) {
        assertTrue(genomes.reserveReferenceBlocks(length, 3)
                        == GenomePoolError::None,
                "Reference size class can be reserved without a live Genome");
        const GenomePoolDiagnostics reserved = genomes.diagnostics();
        const auto& sizeClass = reserved.sizeClasses[sizeClassIndex(length)];
        assertTrue(sizeClass.reservedBlocks >= 3
                        && sizeClass.freeBlocks == sizeClass.reservedBlocks
                        && sizeClass.liveBlocks == 0,
                "Reserved reference blocks enter the deterministic freelist");
    }

    const GenomePoolResult first = genomes.create(makeGenome(150, 41, 149));
    assertTrue(first.hasValue(), "Reserved 256-entry class serves a 150-Gene Genome");
    const auto afterCreate = genomes.diagnostics().sizeClasses[sizeClassIndex(150)];
    assertTrue(afterCreate.liveBlocks == 1
                    && afterCreate.freeBlocks + 1 == afterCreate.reservedBlocks,
            "Claiming a reserved block updates size-class accounting");
    assertTrue(genomes.release(first.id) == GenomePoolError::None,
            "Reserved block is reclaimable");
    const auto afterRelease = genomes.diagnostics().sizeClasses[sizeClassIndex(150)];
    assertTrue(afterRelease.liveBlocks == 0
                    && afterRelease.freeBlocks == afterRelease.reservedBlocks,
            "Released reference block returns to the same size-class freelist");
    assertValid(genomes, "Reference slab reservation and reuse");
}

} // namespace

int main() {
    testImportViewsAndMetadataRoundTrip();
    testCloneSharingAndWholeGenomeCow();
    testNoOpReplacementDefaultAndCacheVerification();
    testCapacityFailureFreelistReuseAndStress();
    testDynamicBoundaryLengthsAndExactCowCopies();
    testInvalidLengthsAndReferenceSlabReservation();
    std::cout << "Genome pool tests passed\n";
    return 0;
}
