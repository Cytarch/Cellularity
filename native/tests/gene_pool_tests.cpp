#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "core/GenePool.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity;
using namespace cellularity::tests;

namespace {

CanonicalGene canonicalVariant(int variant) {
    const auto materialized = materializeCanonicalGene(
            CanonicalGeneDefaults::value(), GeneIndex(0), 1);
    assertTrue(materialized.has_value(), "Default Gene must materialize");
    GeneData candidate = *materialized;
    GeneBuilder(candidate).setColor(
            0, static_cast<float>(variant + 1) / 1024.0f);
    CanonicalGeneResult result = canonicalizeGene(candidate, GeneIndex(0), 1);
    assertTrue(result.hasValue(), "Test Gene variant must be canonical");
    return std::move(*result.value);
}

void assertValid(const GenePool& pool, const std::string& context) {
    const GenePoolInvariantReport report = pool.validateInvariants();
    assertTrue(report.isValid(),
            context + ": invariant error "
                    + std::to_string(static_cast<int>(report.error))
                    + " at GeneId " + std::to_string(report.id.value()));
}

void testDefaultSlotIsImmortalAndReferenceCounted() {
    assertThrows(
            [] {
                GenePool invalid(GenePoolConfig{0, 1, 1});
                (void)invalid;
            },
            "Zero-sized GenePool slabs must be rejected");

    GenePool pool({4, 4, 12});
    const GenePoolDiagnostics initial = pool.diagnostics();
    assertTrue(initial.liveSlots == 1 && initial.freeSlots == 3
                    && initial.reservedSlots == 4 && initial.slabCount == 1,
            "GenePool reserves one slab with one live default");
    assertTrue(initial.defaultGeneReferences == 0,
            "Default Gene starts with no logical owners");

    const auto defaultView = pool.view(DEFAULT_GENE_ID);
    assertTrue(defaultView.has_value(), "Default GeneId is always readable");
    assertNear(GeneView(CanonicalGeneDefaults::value().data()).splitMass(),
            defaultView->splitMass(), 0.0,
            "Default pool slot stores CanonicalGeneDefaults");

    const GenePoolInternResult first = pool.intern(CanonicalGeneDefaults::value());
    const GenePoolInternResult second = pool.intern(CanonicalGeneDefaults::value());
    assertTrue(first.hasValue() && first.id == DEFAULT_GENE_ID
                    && second.hasValue() && second.id == DEFAULT_GENE_ID,
            "Canonical defaults always intern to slot zero");
    assertTrue(pool.referenceCount(DEFAULT_GENE_ID) == 2,
            "Default slot tracks logical references");
    assertTrue(pool.release(DEFAULT_GENE_ID) == GenePoolError::None
                    && pool.release(DEFAULT_GENE_ID) == GenePoolError::None,
            "Default references release normally");
    assertTrue(pool.release(DEFAULT_GENE_ID)
                    == GenePoolError::ReferenceCountUnderflow,
            "Default reference underflow is explicit");
    assertTrue(pool.isLive(DEFAULT_GENE_ID)
                    && pool.referenceCount(DEFAULT_GENE_ID) == 0,
            "Slot zero remains live at zero references");
    assertValid(pool, "Default slot lifecycle");
}

void testExactInterningAndImmutableMutationCopies() {
    GenePool pool({4, 4, 32});
    const CanonicalGene original = canonicalVariant(20);
    const GenePoolInternResult first = pool.intern(original);
    const GenePoolInternResult duplicate = pool.intern(original);
    assertTrue(first.hasValue() && duplicate.hasValue()
                    && first.id == duplicate.id,
            "Identical canonical Genes share one GeneId");
    assertTrue(pool.referenceCount(first.id) == 2,
            "Intern hits retain the shared slot");

    const auto stableOriginalView = pool.view(first.id);
    const auto mutationCopy = pool.copyForMutation(first.id);
    assertTrue(stableOriginalView.has_value() && mutationCopy.has_value(),
            "A live slot provides a view and an independent mutation copy");
    const float originalColor = stableOriginalView->color(0);
    GeneData changedData = *mutationCopy;
    GeneBuilder(changedData).setColor(0, originalColor + 0.125f);
    CanonicalGeneResult changedCanonical =
            canonicalizeGene(changedData, GeneIndex(0), 1);
    assertTrue(changedCanonical.hasValue(), "Changed Gene remains valid");
    const GenePoolInternResult changed = pool.intern(*changedCanonical.value);
    assertTrue(changed.hasValue() && changed.id != first.id,
            "A changed canonical Gene receives a distinct slot");
    assertNear(originalColor, stableOriginalView->color(0), 0.0,
            "Mutating a copy cannot modify shared pool storage");

    const auto defaultData = materializeCanonicalGene(
            CanonicalGeneDefaults::value(), GeneIndex(0), 1);
    assertTrue(defaultData.has_value(), "Default Gene materializes for boundary test");
    GeneData negativeZero = *defaultData;
    GeneBuilder(negativeZero).setColor(0, -0.0f);
    CanonicalGeneResult normalized =
            canonicalizeGene(negativeZero, GeneIndex(0), 1);
    assertTrue(normalized.hasValue(), "Negative zero is canonicalizable");
    const GenePoolInternResult normalizedDefault = pool.intern(*normalized.value);
    assertTrue(normalizedDefault.hasValue()
                    && normalizedDefault.id == DEFAULT_GENE_ID,
            "Canonical float normalization deduplicates negative zero");

    GeneData invalid = *defaultData;
    GeneBuilder(invalid).setSplitRatio(
            std::numeric_limits<float>::quiet_NaN());
    const std::uint64_t lookupsBeforeRejection = pool.diagnostics().internLookups;
    const CanonicalGeneResult rejected =
            canonicalizeGene(invalid, GeneIndex(0), 1);
    assertTrue(!rejected.hasValue(), "Non-finite Gene data is rejected at boundary");
    assertTrue(pool.diagnostics().internLookups == lookupsBeforeRejection,
            "Rejected boundary data never reaches GenePool interning");

    assertTrue(pool.release(first.id) == GenePoolError::None
                    && pool.release(duplicate.id) == GenePoolError::None
                    && pool.release(changed.id) == GenePoolError::None
                    && pool.release(DEFAULT_GENE_ID) == GenePoolError::None,
            "Every successful intern can be balanced by release");
    assertValid(pool, "Exact interning and immutable mutation");
}

void testViewsRemainStableAcrossSlabGrowth() {
    GenePool pool({4, 4, 128});
    std::vector<GeneId> ids;
    ids.reserve(80);

    const GenePoolInternResult first = pool.intern(canonicalVariant(0));
    assertTrue(first.hasValue(), "First slab intern succeeds");
    ids.push_back(first.id);
    const auto firstView = pool.view(first.id);
    assertTrue(firstView.has_value(), "First slab view is available");
    const float firstColor = firstView->color(0);

    for (int variant = 1; variant < 48; ++variant) {
        const GenePoolInternResult result = pool.intern(canonicalVariant(variant));
        assertTrue(result.hasValue(), "Slab growth intern succeeds");
        ids.push_back(result.id);
    }
    assertTrue(pool.reserve(84) == GenePoolError::None,
            "Explicit reservation grows stable slabs ahead of demand");

    for (int variant = 48; variant < 80; ++variant) {
        const GenePoolInternResult result = pool.intern(canonicalVariant(variant));
        assertTrue(result.hasValue(), "Additional slab growth intern succeeds");
        ids.push_back(result.id);
    }

    const GenePoolDiagnostics grown = pool.diagnostics();
    assertTrue(grown.slabCount == 21 && grown.slabGrowths == 20
                    && grown.liveSlots == 81 && grown.reservedSlots == 84,
            "GenePool grows only by stable configured slabs");
    assertNear(firstColor, firstView->color(0), 0.0,
            "A retained GeneView survives slab-vector growth");
    assertTrue(grown.reservedSlotStorageBytes
                            == grown.reservedSlots * GenePool::slotStorageBytes()
                    && GenePool::slotStorageBytes() >= sizeof(GeneData),
            "Diagnostics exactly account for compact data and slot metadata");
    assertValid(pool, "Stable slab growth");

    for (GeneId id : ids) {
        assertTrue(pool.release(id) == GenePoolError::None,
                "Slab growth references release");
    }
    assertTrue(pool.diagnostics().liveSlots == 1
                    && pool.diagnostics().internIndexEntries == 1,
            "Releasing grown slabs leaves only the immortal default");
    assertValid(pool, "Stable slab reclamation");
}

void testFreelistReuseCapacityFailureAndStressCycles() {
    GenePool bounded({4, 4, 4});
    assertTrue(bounded.reserve(5) == GenePoolError::CapacityExhausted,
            "Reservation beyond the configured maximum is recoverable");
    const GenePoolInternResult first = bounded.intern(canonicalVariant(100));
    const GenePoolInternResult second = bounded.intern(canonicalVariant(101));
    const GenePoolInternResult third = bounded.intern(canonicalVariant(102));
    assertTrue(first.id == GeneId(1) && second.id == GeneId(2)
                    && third.id == GeneId(3),
            "Fresh freelist allocation is deterministic and ascending");
    const GenePoolInternResult exhausted = bounded.intern(canonicalVariant(103));
    assertTrue(!exhausted.hasValue()
                    && exhausted.error == GenePoolError::CapacityExhausted,
            "Pool exhaustion is explicit and recoverable");
    assertValid(bounded, "Capacity failure leaves the pool intact");

    assertTrue(bounded.release(second.id) == GenePoolError::None,
            "A middle slot can be reclaimed");
    assertTrue(!bounded.view(second.id).has_value(),
            "A reclaimed slot is not readable");
    const GenePoolInternResult reused = bounded.intern(canonicalVariant(103));
    assertTrue(reused.hasValue() && reused.id == second.id,
            "The most recently reclaimed slot is reused without allocation");
    assertTrue(bounded.retain(INVALID_GENE_ID) == GenePoolError::InvalidGeneId
                    && bounded.release(GeneId(99))
                            == GenePoolError::InvalidGeneId,
            "Invalid ownership operations fail explicitly");
    assertTrue(bounded.release(first.id) == GenePoolError::None
                    && bounded.release(third.id) == GenePoolError::None
                    && bounded.release(reused.id) == GenePoolError::None,
            "Bounded pool references balance after reuse");
    assertValid(bounded, "Freelist reuse");

    GenePool stress({8, 8, 64});
    for (int cycle = 0; cycle < 200; ++cycle) {
        std::vector<GeneId> cycleIds;
        cycleIds.reserve(24);
        for (int variant = 0; variant < 24; ++variant) {
            const CanonicalGene gene = canonicalVariant(variant);
            const GenePoolInternResult created = stress.intern(gene);
            const GenePoolInternResult shared = stress.intern(gene);
            assertTrue(created.hasValue() && shared.hasValue()
                            && created.id == shared.id,
                    "Stress cycle exact interning");
            cycleIds.push_back(created.id);
        }
        for (GeneId id : cycleIds) {
            assertTrue(stress.release(id) == GenePoolError::None
                            && stress.release(id) == GenePoolError::None,
                    "Stress cycle balances shared references");
        }
        assertTrue(stress.diagnostics().liveSlots == 1
                        && stress.diagnostics().internIndexEntries == 1,
                "Stress cycle leaves no historical intern entries");
        assertValid(stress, "Repeated intern/release/reuse cycle");
    }
    const GenePoolDiagnostics stressed = stress.diagnostics();
    assertTrue(stressed.internHits == 200 * 24
                    && stressed.internMisses == 200 * 24
                    && stressed.reclaimedSlots == 200 * 24,
            "Stress diagnostics distinguish sharing and reclamation");
    stress.resetDiagnosticCounters();
    const GenePoolDiagnostics reset = stress.diagnostics();
    assertTrue(reset.internLookups == 0 && reset.internHits == 0
                    && reset.reclaimedSlots == 0
                    && reset.liveSlots == 1 && reset.peakLiveSlots == 1,
            "Event counters reset without losing structural diagnostics");
    assertValid(stress, "Counter reset");
}

} // namespace

int main() {
    testDefaultSlotIsImmortalAndReferenceCounted();
    testExactInterningAndImmutableMutationCopies();
    testViewsRemainStableAcrossSlabGrowth();
    testFreelistReuseCapacityFailureAndStressCycles();
    std::cout << "Gene pool tests passed\n";
    return 0;
}
