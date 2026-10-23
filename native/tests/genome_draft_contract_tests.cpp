#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "core/CellType.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "formats/GenomeDraftCodec.h"
#include "io/DecodeError.h"
#include "io/NativeByteStream.h"
#include "tests/support/TestAssertions.h"

using namespace cellularity::tests;

namespace {

constexpr int32_t MAGIC = 0x43474457;
constexpr int32_t SCHEMA = 2;

struct InvalidField {
    int geneCount = 2;
    int parameterCount = Gene::MAX_PARAMETRIC_VALUES_COUNT;
    int integerCount = Gene::MAX_INTEGERS_COUNT;
    int floatCount = Gene::MAX_FLOATS_COUNT;
    int childIndex = 0;
    int cellType = 0;
    int formulaType = 0;
    int signalChannel = 0;
    int invalidIntegerIndex = 0;
    int integerValue = 0;
    float color = 0.2f;
    int additionalInitialGene = -1;
    bool noInitialGene = false;
};

void writeGene(ByteWriter& writer, int geneIndex, const InvalidField& invalid) {
    writer.writeFloat(geneIndex == 0 ? invalid.color : 0.2f);
    writer.writeFloat(0.4f);
    writer.writeFloat(0.6f);
    writer.writeFloat(0.1f);
    writer.writeFloat(0.5f);
    writer.writeFloat(0.25f);
    writer.writeFloat(0.5f);
    writer.writeFloat(0.75f);
    writer.writeFloat(1.0f);
    writer.writeFloat(7.0f);

    writer.writeInt(geneIndex == 0 ? invalid.childIndex : geneIndex);
    writer.writeInt(geneIndex);

    writer.writeBool(true);
    writer.writeBool(true);
    writer.writeBool(false);
    writer.writeBool(true);
    writer.writeBool(!invalid.noInitialGene
            && (geneIndex == 0 || geneIndex == invalid.additionalInitialGene));
    writer.writeBool(false);
    writer.writeBool(true);
    writer.writeInt(geneIndex == 0 ? invalid.cellType : 0);

    for (int valueIndex = 0; valueIndex < Gene::MAX_PARAMETRIC_VALUES_COUNT; valueIndex++) {
        writer.writeFloat(valueIndex == 0 ? 0.5f : 0.0f);
        writer.writeFloat(0.0f);
        writer.writeFloat(0.0f);
        writer.writeShort(static_cast<int16_t>(
                geneIndex == 0 && valueIndex == 0 ? invalid.signalChannel : 0));
        writer.writeShort(static_cast<int16_t>(
                geneIndex == 0 && valueIndex == 0 ? invalid.formulaType : 0));
    }
    for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
        writer.writeInt(geneIndex == 0
                                && integerIndex == invalid.invalidIntegerIndex
                        ? invalid.integerValue
                        : 0);
    }
    writer.writeFloat(0.0f);
    writer.writeFloat(0.25f);
    writer.writeFloat(0.5f);
    writer.writeFloat(0.75f);
    writer.writeFloat(0.5f);
    writer.writeFloat(0.005f);
    writer.writeFloat(1.25f);
}

std::vector<uint8_t> writeGenome(const InvalidField& invalid = {}) {
    ByteWriter writer;
    writer.writeInt(MAGIC);
    writer.writeInt(SCHEMA);
    writer.writeInt(invalid.geneCount);
    writer.writeInt(invalid.parameterCount);
    writer.writeInt(invalid.integerCount);
    writer.writeInt(invalid.floatCount);
    if (invalid.geneCount >= 1
            && invalid.geneCount
                    <= static_cast<int>(
                            cellularity::MAX_SUPPORTED_GENES_PER_GENOME)
            && invalid.parameterCount == Gene::MAX_PARAMETRIC_VALUES_COUNT
            && invalid.integerCount == Gene::MAX_INTEGERS_COUNT
            && invalid.floatCount == Gene::MAX_FLOATS_COUNT) {
        for (int geneIndex = 0; geneIndex < invalid.geneCount; geneIndex++) {
            writeGene(writer, geneIndex, invalid);
        }
    }
    return writer.takeBytes();
}

void assertDecodeError(const std::vector<uint8_t>& bytes,
        DecodeErrorCode expected,
        const std::string& message) {
    try {
        ByteReader reader(bytes);
        (void)readGenomeDraft(reader);
    } catch (const DecodeError& error) {
        assertTrue(error.code() == expected, message + " (wrong error code)");
        return;
    }
    fail(message + " (not rejected)");
}

void testValidDraftMapsEveryFieldFamily() {
    ByteReader reader(writeGenome());
    const auto genome = readGenomeDraft(reader);
    const Gene& gene = genome[0];
    const cellularity::GeneView view(gene);
    assertNear(0.2, view.color(0), 1e-6, "Genome draft color");
    assertNear(0.1, view.splitMass(), 1e-6, "Genome draft split mass");
    assertNear(7.0, view.linkStrength(), 0.0, "Genome draft link strength");
    assertTrue(view.makesLink() && view.staysAlive()
                    && view.initialCompatibilityFlag(),
            "Genome draft booleans");
    assertTrue(view.cellType() == CellType::PHAGOCYTE,
            "Genome draft cell type");
    assertNear(0.5, view.parametricVariableA(0), 0.0,
            "Genome draft parametric value");
    assertNear(1.25, view.floatValue(Gene::FloatType::CYTOSKELETON), 0.0,
            "Genome draft float value");
    assertTrue(!cellularity::GeneView(genome[1]).initialCompatibilityFlag(),
            "Genome draft keeps one initial gene");

    const std::vector<uint8_t> encoded = writeGenomeDraft(genome);
    ByteReader roundTripReader(encoded);
    const auto roundTrip = readGenomeDraft(roundTripReader);
    const cellularity::GeneView roundTripView(roundTrip[0]);
    assertNear(view.color(0), roundTripView.color(0), 0.0,
            "Genome draft writer preserves color");
    assertTrue(roundTripView.child2GeneIndex() == view.child2GeneIndex(),
            "Genome draft writer preserves child references");
    assertNear(view.floatValue(Gene::FloatType::CYTOSKELETON),
            roundTripView.floatValue(Gene::FloatType::CYTOSKELETON), 0.0,
            "Genome draft writer preserves float values");
}

void testDynamicGeneCountsRoundTripExactly() {
    constexpr std::array<int, 5> GENE_COUNTS{1, 2, 40, 150, 256};
    for (const int geneCount : GENE_COUNTS) {
        InvalidField field;
        field.geneCount = geneCount;
        ByteReader reader(writeGenome(field));
        const cellularity::GenomeData genome = readGenomeDraft(reader);
        assertTrue(genome.size() == static_cast<std::size_t>(geneCount),
                "Genome draft preserves its exact dynamic gene count");
        assertTrue(reader.remaining() == 0,
                "Dynamic Genome draft consumes its exact payload");

        ByteReader roundTripReader(writeGenomeDraft(genome));
        const cellularity::GenomeData roundTrip =
                readGenomeDraft(roundTripReader);
        assertTrue(roundTrip.size() == genome.size(),
                "Genome draft writer preserves dynamic gene count");
    }
}

void testHeaderAndCountsAreValidated() {
    auto wrongMagic = writeGenome();
    wrongMagic[0] = 0;
    assertDecodeError(wrongMagic, DecodeErrorCode::InvalidHeader,
            "Genome draft rejects wrong magic");

    auto newerSchema = writeGenome();
    newerSchema[7] = 3;
    assertDecodeError(newerSchema, DecodeErrorCode::NewerSnapshotVersion,
            "Genome draft rejects newer schema");

    InvalidField invalid;
    invalid.geneCount = 0;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidCount,
            "Genome draft rejects empty gene count");
    invalid = {};
    invalid.geneCount = static_cast<int>(
            cellularity::MAX_SUPPORTED_GENES_PER_GENOME + 1U);
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidCount,
            "Genome draft rejects gene count above its safety limit");
    invalid = {};
    invalid.parameterCount--;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidCount,
            "Genome draft rejects parameter count");
    invalid = {};
    invalid.integerCount--;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidCount,
            "Genome draft rejects integer count");
    invalid = {};
    invalid.floatCount--;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidCount,
            "Genome draft rejects float count");
}

void testIndicesEnumsAndFiniteValuesAreValidated() {
    InvalidField invalid;
    invalid.childIndex = invalid.geneCount;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidValue,
            "Genome draft rejects child index");

    invalid = {};
    invalid.cellType = CellTypes::length;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidValue,
            "Genome draft rejects cell type ordinal");

    invalid = {};
    invalid.formulaType = 4;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidValue,
            "Genome draft rejects formula ordinal");

    invalid = {};
    invalid.signalChannel = CellDataConstants::SIGNAL_SUBSTANCE_COUNT
            + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidValue,
            "Genome draft rejects signal index");

    invalid = {};
    invalid.integerValue = Gene::INTEGER_MAX[0];
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidValue,
            "Genome draft rejects integer index");

    invalid = {};
    invalid.invalidIntegerIndex = Gene::SMELL_OUT_INTEGER_INDEX;
    invalid.integerValue = 65536;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidValue,
            "Genome draft rejects compact integer storage aliasing");

    invalid = {};
    invalid.color = std::numeric_limits<float>::quiet_NaN();
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidValue,
            "Genome draft rejects non-finite value");
}

void testWholePayloadInvariantsAreValidated() {
    InvalidField invalid;
    invalid.noInitialGene = true;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidCount,
            "Genome draft requires an initial gene");

    invalid = {};
    invalid.additionalInitialGene = 1;
    assertDecodeError(writeGenome(invalid), DecodeErrorCode::InvalidCount,
            "Genome draft rejects multiple initial genes");

    auto trailing = writeGenome();
    trailing.push_back(0);
    assertDecodeError(trailing, DecodeErrorCode::TrailingData,
            "Genome draft rejects trailing bytes");

    InvalidField maximum;
    maximum.geneCount = static_cast<int>(
            cellularity::MAX_SUPPORTED_GENES_PER_GENOME);
    std::vector<uint8_t> tooLarge = writeGenome(maximum);
    tooLarge.push_back(0);
    assertDecodeError(tooLarge, DecodeErrorCode::LimitExceeded,
            "Genome draft enforces wire size limit");

    assertDecodeError({0x43, 0x47}, DecodeErrorCode::TruncatedInput,
            "Genome draft rejects truncated input");
}

} // namespace

int main() {
    testValidDraftMapsEveryFieldFamily();
    testDynamicGeneCountsRoundTripExactly();
    testHeaderAndCountsAreValidated();
    testIndicesEnumsAndFiniteValuesAreValidated();
    testWholePayloadInvariantsAreValidated();
    std::cout << "Genome draft contract tests passed\n";
    return 0;
}
