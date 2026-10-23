#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "core/CanonicalGene.h"
#include "core/CellDataConstants.h"
#include "core/GeneAccess.h"
#include "core/GenomeMetadata.h"
#include "formats/GenomeDraftCodec.h"
#include "io/NativeByteStream.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity;
using namespace cellularity::tests;

namespace {

CanonicalGene requireCanonical(
        const Gene& gene,
        int owner = 0,
        std::size_t geneCount = 2) {
    CanonicalGeneResult result = canonicalizeGene(
            gene,
            GeneIndex(static_cast<GeneIndex::Value>(owner)),
            geneCount);
    assertTrue(result.hasValue(), "Expected a valid canonical Gene");
    return *result.value;
}

Gene requireMaterializedDefault(
        int owner,
        std::size_t geneCount,
        bool isInitial = false) {
    const auto result = materializeCanonicalGene(
            CanonicalGeneDefaults::value(),
            GeneIndex(static_cast<GeneIndex::Value>(owner)),
            geneCount,
            isInitial);
    assertTrue(result.has_value(), "Canonical defaults must materialize");
    return Gene(*result);
}

void assertCanonicalDifference(
        const Gene& candidate,
        const std::string& fieldName) {
    const CanonicalGene& defaults = CanonicalGeneDefaults::value();
    const CanonicalGene changed = requireCanonical(candidate);
    assertTrue(!canonicalGeneEqual(defaults, changed),
            "Canonical equality omitted " + fieldName);
    assertTrue(canonicalGeneHash(defaults) != canonicalGeneHash(changed),
            "Canonical hash omitted " + fieldName);
}

template <typename Mutation>
void assertRejected(
        Mutation mutation,
        CanonicalGeneError expected,
        const std::string& message) {
    Gene gene = requireMaterializedDefault(0, 2);
    mutation(gene);
    const CanonicalGeneResult result = canonicalizeGene(gene, GeneIndex(0), 2);
    assertTrue(!result.hasValue(), message + " must be rejected");
    assertTrue(result.validation.error == expected,
            message + " returned the wrong validation category");
}

void testCanonicalDefaultsSpecifyEveryField() {
    const GeneData& gene = CanonicalGeneDefaults::value().data();
    const GeneView view(gene);
    for (int channel = 0; channel < 3; channel++) {
        assertNear(0.0, view.color(channel), 0.0, "Canonical default color");
    }
    assertNear(static_cast<float>(CellDataConstants::MIN_MASS) * 3.5f,
            view.splitMass(), 0.0, "Canonical default split mass");
    assertNear(0.5, view.splitRatio(), 0.0, "Canonical default split ratio");
    assertNear(0.0, view.splitAngle(), 0.0, "Canonical default split angle");
    assertNear(0.0, view.child1Angle(), 0.0, "Canonical default child 1 angle");
    assertNear(0.0, view.child2Angle(), 0.0, "Canonical default child 2 angle");
    assertNear(1.0, view.massFlowPriority(), 0.0,
            "Canonical default mass-flow priority");
    assertNear(0.0, view.linkStrength(), 0.0, "Canonical default link strength");
    assertTrue(view.child1GeneIndex() == SELF_GENE_INDEX,
            "Canonical child 1 reference is self-relative");
    assertTrue(view.child2GeneIndex() == SELF_GENE_INDEX,
            "Canonical child 2 reference is self-relative");
    assertTrue(!view.makesLink() && view.child1KeepsLinks()
                    && view.child2KeepsLinks() && view.staysAlive()
                    && !view.initialCompatibilityFlag()
                    && !view.child1Mirrored() && !view.child2Mirrored(),
            "Canonical flags");
    assertTrue(view.cellType() == CellType::PHAGOCYTE,
            "Canonical Cell type");
    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
        const GeneParametricValueView value = view.parametricValue(index);
        assertNear(0.0, value.variableA(), 0.0, "Canonical parametric A");
        assertNear(0.0, value.variableB(), 0.0, "Canonical parametric B");
        assertNear(0.0, value.variableC(), 0.0, "Canonical parametric C");
        assertTrue(value.signalChannel() == 0,
                "Canonical parametric signal");
        assertTrue(value.formulaType() == 0, "Canonical parametric formula");
    }
    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
        const int expected = index == Gene::VIRUS_FROM_INTEGER_INDEX
                        || index == Gene::VIRUS_TO_INTEGER_INDEX
                ? SELF_GENE_INDEX.value()
                : index == Gene::MAX_CONNECTION_COUNT_INDEX
                ? Gene::INTEGER_MAX[index] - 1
                : 0;
        assertTrue(view.integerValue(index) == expected,
                "Canonical integer value " + std::to_string(index));
    }
    assertNear(0.0, view.floatValue(Gene::FloatType::SMELL_AMPLITUDE), 0.0,
            "Canonical smell amplitude");
    assertNear(0.0, view.floatValue(Gene::FloatType::SMELL_RED), 0.0,
            "Canonical smell red");
    assertNear(0.0, view.floatValue(Gene::FloatType::SMELL_GREEN), 0.0,
            "Canonical smell green");
    assertNear(0.0, view.floatValue(Gene::FloatType::SMELL_BLUE), 0.0,
            "Canonical smell blue");
    assertNear(0.0, view.floatValue(Gene::FloatType::SMELL_THRESHOLD), 0.0,
            "Canonical smell threshold");
    assertNear(0.0, view.floatValue(Gene::FloatType::ADHESIN_LENGTH), 0.0,
            "Canonical adhesin length");
    assertNear(1.0, view.floatValue(Gene::FloatType::CYTOSKELETON), 0.0,
            "Canonical cytoskeleton");
}

void testCanonicalDefaultsRoundTripAcrossOwnersAndDraftCodec() {
    constexpr std::size_t GENE_COUNT = 40;
    cellularity::GenomeData genome(GENE_COUNT);
    for (std::size_t index = 0; index < genome.size(); index++) {
        genome[index] = requireMaterializedDefault(
                static_cast<int>(index), genome.size());
    }
    const GenomeMetadata metadata{GeneIndex(17)};
    assertTrue(applyGenomeMetadataToCompatibilityFlags(
                       genome.data(), genome.size(), metadata),
            "Genome metadata materializes into compatibility flags");

    const std::vector<std::uint8_t> encoded = writeGenomeDraft(genome);
    ByteReader reader(encoded);
    const auto decoded = readGenomeDraft(reader);
    assertTrue(reader.remaining() == 0, "Canonical draft round-trip consumes bytes");
    const auto decodedMetadata = extractGenomeMetadataFromCompatibilityFlags(
            decoded.data(), decoded.size());
    assertTrue(decodedMetadata.has_value() && *decodedMetadata == metadata,
            "Canonical draft round-trip preserves Genome metadata");
    for (std::size_t index = 0; index < genome.size(); index++) {
        const CanonicalGene roundTrip = requireCanonical(
                decoded[index], index, genome.size());
        assertTrue(canonicalGeneEqual(CanonicalGeneDefaults::value(), roundTrip),
                "Canonical default round-trips at slot "
                        + std::to_string(index));
        assertTrue(canonicalGeneHash(CanonicalGeneDefaults::value())
                        == canonicalGeneHash(roundTrip),
                "Canonical default hash round-trips at slot "
                        + std::to_string(index));
    }
}

void testCanonicalizationNormalizesFloatsAndSelfReferences() {
    Gene gene = requireMaterializedDefault(1, 2, true);
    GeneBuilder builder(gene);
    for (int channel = 0; channel < 3; channel++) {
        builder.setColor(channel, -0.0f);
    }
    builder.setSplitAngle(-0.0f);
    builder.setChild1Angle(-0.0f);
    builder.setChild2Angle(-0.0f);
    builder.setLinkStrength(-0.0f);
    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
        builder.setParametricVariableA(index, -0.0f);
        builder.setParametricVariableB(index, -0.0f);
        builder.setParametricVariableC(index, -0.0f);
    }
    builder.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, -0.0f);
    builder.setFloatValue(Gene::FloatType::SMELL_RED, -0.0f);
    builder.setFloatValue(Gene::FloatType::SMELL_GREEN, -0.0f);
    builder.setFloatValue(Gene::FloatType::SMELL_BLUE, -0.0f);
    builder.setFloatValue(Gene::FloatType::SMELL_THRESHOLD, -0.0f);
    builder.setFloatValue(Gene::FloatType::ADHESIN_LENGTH, -0.0f);

    const CanonicalGene normalized = requireCanonical(gene, 1, 2);
    assertTrue(canonicalGeneEqual(CanonicalGeneDefaults::value(), normalized),
            "Equivalent float and self-reference forms canonicalize equally");
    assertTrue(canonicalGeneHash(CanonicalGeneDefaults::value())
                    == canonicalGeneHash(normalized),
            "Equivalent canonical forms have the same hash");
    assertTrue(!GeneView(normalized.data())
                       .initialCompatibilityFlag(),
            "Initial state is removed from canonical Gene identity");
}

void testCanonicalValidationRejectsInvalidDomains() {
    Gene valid = requireMaterializedDefault(0, 2);
    assertTrue(!canonicalizeGene(valid, GeneIndex(0), 0).hasValue(),
            "Canonicalization rejects zero Gene count");
    assertTrue(!canonicalizeGene(valid, GeneIndex(2), 2).hasValue(),
            "Canonicalization rejects an out-of-bounds owner");

    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setColor(
                        0, std::numeric_limits<float>::quiet_NaN());
            },
            CanonicalGeneError::NonFiniteFloat,
            "NaN color");
    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setSplitMass(0.0f);
            },
            CanonicalGeneError::FloatOutOfRange,
            "Out-of-range split mass");
    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setChild1GeneIndex(GeneIndex(2));
            },
            CanonicalGeneError::InvalidGeneReference,
            "Out-of-range child reference");
    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setCellType(
                        static_cast<CellType>(CellTypes::length));
            },
            CanonicalGeneError::InvalidCellType,
            "Unknown Cell type");
    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setParametricVariableA(
                        3, CellDataConstants::VALUE_VARIABLE_MAX + 1.0f);
            },
            CanonicalGeneError::FloatOutOfRange,
            "Out-of-range parametric variable");
    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setParametricFormulaType(3, 4);
            },
            CanonicalGeneError::InvalidParametricFormula,
            "Unknown parametric formula");
    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setParametricSignalChannel(
                        3,
                        CellDataConstants::SIGNAL_SUBSTANCE_COUNT
                                + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT);
            },
            CanonicalGeneError::InvalidSignalChannel,
            "Unknown signal channel");
    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setIntegerValue(
                        Gene::SMELL_OUT_INTEGER_INDEX,
                        Gene::INTEGER_MAX[Gene::SMELL_OUT_INTEGER_INDEX]);
            },
            CanonicalGeneError::InvalidIntegerValue,
            "Out-of-range integer value");
    assertRejected(
            [](Gene& gene) {
                GeneBuilder(gene).setFloatValue(
                        Gene::FloatType::SMELL_RED, 2.0f);
            },
            CanonicalGeneError::FloatOutOfRange,
            "Out-of-range FloatValue");
}

void testCanonicalEqualityAndHashCoverEveryFieldFamily() {
    Gene candidate = requireMaterializedDefault(0, 2);
    for (int index = 0; index < 3; index++) {
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder(candidate).setColor(index, 0.25f);
        assertCanonicalDifference(candidate, "color[" + std::to_string(index) + "]");
    }

    const std::array<float, 7> scalarValues{
            static_cast<float>(CellDataConstants::MIN_MASS) * 4.0f,
            0.6f,
            0.25f,
            0.25f,
            0.25f,
            2.0f,
            1.0f
    };
    for (int index = 0; index < static_cast<int>(scalarValues.size()); index++) {
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder builder(candidate);
        switch (index) {
            case 0:
                builder.setSplitMass(scalarValues[index]);
                break;
            case 1:
                builder.setSplitRatio(scalarValues[index]);
                break;
            case 2:
                builder.setSplitAngle(scalarValues[index]);
                break;
            case 3:
                builder.setChild1Angle(scalarValues[index]);
                break;
            case 4:
                builder.setChild2Angle(scalarValues[index]);
                break;
            case 5:
                builder.setMassFlowPriority(scalarValues[index]);
                break;
            case 6:
                builder.setLinkStrength(scalarValues[index]);
                break;
            default:
                break;
        }
        assertCanonicalDifference(candidate, "scalar[" + std::to_string(index) + "]");
    }

    for (int index = 0; index < 2; index++) {
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder builder(candidate);
        switch (index) {
            case 0:
                builder.setChild1GeneIndex(GeneIndex(1));
                break;
            case 1:
                builder.setChild2GeneIndex(GeneIndex(1));
                break;
            default:
                break;
        }
        assertCanonicalDifference(candidate, "structural integer[" + std::to_string(index) + "]");
    }

    for (int index = 0; index < 6; index++) {
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder builder(candidate);
        switch (index) {
            case 0:
                builder.setMakesLink(true);
                break;
            case 1:
                builder.setChild1KeepsLinks(false);
                break;
            case 2:
                builder.setChild2KeepsLinks(false);
                break;
            case 3:
                builder.setStaysAlive(false);
                break;
            case 4:
                builder.setChild1Mirrored(true);
                break;
            case 5:
                builder.setChild2Mirrored(true);
                break;
            default:
                break;
        }
        assertCanonicalDifference(candidate, "flag[" + std::to_string(index) + "]");
    }

    candidate = requireMaterializedDefault(0, 2);
    GeneBuilder(candidate).setCellType(CellType::FLAGELLOCYTE);
    assertCanonicalDifference(candidate, "Cell type");

    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder(candidate).setParametricVariableA(index, 1.0f);
        assertCanonicalDifference(candidate, "parametric A");
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder(candidate).setParametricVariableB(index, 1.0f);
        assertCanonicalDifference(candidate, "parametric B");
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder(candidate).setParametricVariableC(index, 1.0f);
        assertCanonicalDifference(candidate, "parametric C");
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder(candidate).setParametricSignalChannel(index, 1);
        assertCanonicalDifference(candidate, "parametric signal");
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder(candidate).setParametricFormulaType(index, 1);
        assertCanonicalDifference(candidate, "parametric formula");
    }

    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
        candidate = requireMaterializedDefault(0, 2);
        const int oldValue = GeneView(candidate).integerValue(index);
        GeneBuilder(candidate).setIntegerValue(index, oldValue == 0 ? 1 : 0);
        assertCanonicalDifference(candidate, "integer[" + std::to_string(index) + "]");
    }

    constexpr std::array<Gene::FloatType, Gene::MAX_FLOATS_COUNT> floatTypes{
            Gene::FloatType::SMELL_AMPLITUDE,
            Gene::FloatType::SMELL_RED,
            Gene::FloatType::SMELL_GREEN,
            Gene::FloatType::SMELL_BLUE,
            Gene::FloatType::SMELL_THRESHOLD,
            Gene::FloatType::ADHESIN_LENGTH,
            Gene::FloatType::CYTOSKELETON
    };
    const std::array<float, Gene::MAX_FLOATS_COUNT> changedValues{
            1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.005f, 1.5f
    };
    for (int index = 0; index < Gene::MAX_FLOATS_COUNT; index++) {
        candidate = requireMaterializedDefault(0, 2);
        GeneBuilder(candidate).setFloatValue(
                floatTypes[index], changedValues[index]);
        assertCanonicalDifference(candidate, "FloatValue[" + std::to_string(index) + "]");
    }
}

void testCanonicalIdentityExcludesGenomeCompatibilityMetadata() {
    Gene first = requireMaterializedDefault(0, 2, false);
    Gene second(first);
    GeneBuilder(second).setInitialCompatibilityFlag(true);
    const CanonicalGene firstCanonical = requireCanonical(first);
    const CanonicalGene secondCanonical = requireCanonical(second);
    assertTrue(canonicalGeneEqual(firstCanonical, secondCanonical),
            "Initial Genome metadata is not Gene identity");
    assertTrue(canonicalGeneHash(firstCanonical) == canonicalGeneHash(secondCanonical),
            "Excluded compatibility metadata does not affect canonical hash");
}

void testGenomeInitialGeneCompatibilityAdapter() {
    std::array<GeneData, 4> genes{};
    GeneBuilder(genes[2]).setInitialCompatibilityFlag(true);
    auto metadata = extractGenomeMetadataFromCompatibilityFlags(
            genes.data(), genes.size());
    assertTrue(metadata.has_value()
                    && metadata->initialGeneIndex == GeneIndex(2),
            "Compatibility adapter extracts exactly one initial Gene");

    GeneBuilder(genes[3]).setInitialCompatibilityFlag(true);
    assertTrue(!extractGenomeMetadataFromCompatibilityFlags(
                       genes.data(), genes.size())
                       .has_value(),
            "Compatibility adapter rejects duplicate initial flags");
    GeneBuilder(genes[2]).setInitialCompatibilityFlag(false);
    GeneBuilder(genes[3]).setInitialCompatibilityFlag(false);
    assertTrue(!extractGenomeMetadataFromCompatibilityFlags(
                       genes.data(), genes.size())
                       .has_value(),
            "Compatibility adapter rejects a missing initial flag");

    GeneBuilder(genes[1]).setInitialCompatibilityFlag(true);
    GeneBuilder(genes[3]).setInitialCompatibilityFlag(true);
    metadata = normalizeLegacyInitialGeneFlags(genes.data(), genes.size());
    assertTrue(metadata.has_value()
                    && metadata->initialGeneIndex == GeneIndex(1),
            "Legacy normalization keeps the first initial Gene");
    assertTrue(GeneView(genes[1]).initialCompatibilityFlag()
                    && !GeneView(genes[3]).initialCompatibilityFlag(),
            "Legacy normalization clears later initial flags");

    GeneBuilder(genes[1]).setInitialCompatibilityFlag(false);
    metadata = normalizeLegacyInitialGeneFlags(genes.data(), genes.size());
    assertTrue(metadata.has_value()
                    && metadata->initialGeneIndex == GeneIndex(0)
                    && GeneView(genes[0]).initialCompatibilityFlag(),
            "Legacy normalization falls back to slot zero");

    assertTrue(applyGenomeMetadataToCompatibilityFlags(
                       genes.data(), genes.size(), GenomeMetadata{GeneIndex(3)}),
            "Valid Genome metadata materializes");
    assertTrue(GeneView(genes[3]).initialCompatibilityFlag()
                    && !GeneView(genes[0]).initialCompatibilityFlag(),
            "Metadata materialization writes exactly one flag");
    assertTrue(!applyGenomeMetadataToCompatibilityFlags(
                       genes.data(), genes.size(), GenomeMetadata{GeneIndex(4)}),
            "Out-of-bounds Genome metadata is rejected");
    assertTrue(GeneView(genes[3]).initialCompatibilityFlag(),
            "Rejected metadata does not partially modify flags");
}

} // namespace

int main() {
    testCanonicalDefaultsSpecifyEveryField();
    testCanonicalDefaultsRoundTripAcrossOwnersAndDraftCodec();
    testCanonicalizationNormalizesFloatsAndSelfReferences();
    testCanonicalValidationRejectsInvalidDomains();
    testCanonicalEqualityAndHashCoverEveryFieldFamily();
    testCanonicalIdentityExcludesGenomeCompatibilityMetadata();
    testGenomeInitialGeneCompatibilityAdapter();
    std::cout << "Canonical Gene tests passed\n";
    return 0;
}
