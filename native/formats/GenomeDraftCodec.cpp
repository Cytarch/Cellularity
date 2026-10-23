#include "formats/GenomeDraftCodec.h"

#include <limits>
#include <string>

#include "core/CanonicalGene.h"
#include "core/CellType.h"
#include "core/GeneAccess.h"
#include "core/GenomeIdentity.h"
#include "core/GenomeMetadata.h"
#include "io/DecodeError.h"
#include "io/NativeByteStream.h"

namespace {

constexpr int GENOME_DRAFT_MAGIC = 0x43474457; // CGDW
constexpr int GENOME_DRAFT_SCHEMA_VERSION = 2;

[[noreturn]] void fail(DecodeErrorCode code, const std::string& message) {
    throw DecodeError(code, message);
}

void requireExactCount(int actual, int expected, const char* name) {
    if (actual != expected) {
        fail(DecodeErrorCode::InvalidCount,
                std::string("Genome draft ") + name + " count is " + std::to_string(actual) + ", expected " + std::to_string(expected));
    }
}

int requireGeneCount(int actual) {
    if (actual < 0
            || !cellularity::isSupportedGeneCount(
                    static_cast<std::size_t>(actual))) {
        fail(DecodeErrorCode::InvalidCount,
                "Genome draft gene count is outside the supported range");
    }
    return actual;
}

void requireIndex(int value, int upperBound, const std::string& name) {
    if (value < 0 || value >= upperBound) {
        fail(DecodeErrorCode::InvalidValue, name + " is outside its valid index range");
    }
}

int requireCompactUnsigned(int value, int maximum, const std::string& name) {
    if (value < 0 || value > maximum) {
        fail(DecodeErrorCode::InvalidValue,
                name + " cannot be represented by compact GeneData storage");
    }
    return value;
}

cellularity::GeneIndex requireGeneIndex(
        int value,
        int geneCount,
        const std::string& name) {
    const auto index = cellularity::geneIndexFromBoundary(
            value, static_cast<std::size_t>(geneCount));
    if (!index.has_value()) {
        fail(DecodeErrorCode::InvalidValue,
                name + " is outside its valid gene-index range");
    }
    return *index;
}

void validateGene(const GeneData& gene, int geneIndex, int geneCount) {
    const cellularity::GeneView view(gene);
    const std::string prefix = "Genome draft gene[" + std::to_string(geneIndex) + "] ";
    (void)requireGeneIndex(view.child1GeneIndex().value(),
            geneCount,
            prefix + "child 1 gene index");
    (void)requireGeneIndex(view.child2GeneIndex().value(),
            geneCount,
            prefix + "child 2 gene index");
    (void)requireGeneIndex(view.virusFromGeneIndex().value(),
            geneCount,
            prefix + "virus source gene index");
    (void)requireGeneIndex(view.virusToGeneIndex().value(),
            geneCount,
            prefix + "virus target gene index");

    const auto canonical = cellularity::canonicalizeGene(
            gene,
            cellularity::GeneIndex(
                    static_cast<cellularity::GeneIndex::Value>(geneIndex)),
            static_cast<std::size_t>(geneCount));
    if (!canonical.hasValue()) {
        fail(DecodeErrorCode::InvalidValue,
                prefix + "contains an invalid canonical field (category "
                        + std::to_string(static_cast<int>(canonical.validation.error))
                        + ", index "
                        + std::to_string(canonical.validation.fieldIndex) + ")");
    }
}

GeneData readGene(ByteReader& reader, int geneCount) {
    GeneData gene;
    cellularity::GeneBuilder builder(gene);
    for (int channel = 0; channel < 3; channel++) {
        builder.setColor(channel, reader.readFloat());
    }
    builder.setSplitMass(reader.readFloat());
    builder.setSplitRatio(reader.readFloat());
    builder.setSplitAngle(reader.readFloat());
    builder.setChild1Angle(reader.readFloat());
    builder.setChild2Angle(reader.readFloat());
    builder.setMassFlowPriority(reader.readFloat());
    builder.setLinkStrength(reader.readFloat());

    builder.setChild1GeneIndex(requireGeneIndex(
            reader.readInt(), geneCount, "Genome draft child 1 gene index"));
    builder.setChild2GeneIndex(requireGeneIndex(
            reader.readInt(), geneCount, "Genome draft child 2 gene index"));
    builder.setMakesLink(reader.readBool());
    builder.setChild1KeepsLinks(reader.readBool());
    builder.setChild2KeepsLinks(reader.readBool());
    builder.setStaysAlive(reader.readBool());
    builder.setInitialCompatibilityFlag(reader.readBool());
    builder.setChild1Mirrored(reader.readBool());
    builder.setChild2Mirrored(reader.readBool());
    int cellTypeOrdinal = reader.readInt();
    requireIndex(cellTypeOrdinal, CellTypes::length, "Genome draft cell type");
    builder.setCellType(CellTypes::fromOrdinal(cellTypeOrdinal));

    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
        builder.setParametricVariableA(index, reader.readFloat());
        builder.setParametricVariableB(index, reader.readFloat());
        builder.setParametricVariableC(index, reader.readFloat());
        builder.setParametricSignalChannel(index, reader.readShort());
        builder.setParametricFormulaType(index, reader.readShort());
    }
    for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
        const int value = reader.readInt();
        builder.setIntegerValue(
                integerIndex,
                integerIndex == Gene::VIRUS_FROM_INTEGER_INDEX
                                || integerIndex == Gene::VIRUS_TO_INTEGER_INDEX
                        ? requireGeneIndex(
                                  value,
                                  geneCount,
                                  "Genome draft virus gene index")
                                  .value()
                        : requireCompactUnsigned(
                                  value,
                                  static_cast<int>(
                                          std::numeric_limits<
                                                  std::uint16_t>::max()),
                                  "Genome draft integer parameter"));
    }

    builder.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, reader.readFloat());
    builder.setFloatValue(Gene::FloatType::SMELL_RED, reader.readFloat());
    builder.setFloatValue(Gene::FloatType::SMELL_GREEN, reader.readFloat());
    builder.setFloatValue(Gene::FloatType::SMELL_BLUE, reader.readFloat());
    builder.setFloatValue(Gene::FloatType::SMELL_THRESHOLD, reader.readFloat());
    builder.setFloatValue(Gene::FloatType::ADHESIN_LENGTH, reader.readFloat());
    builder.setFloatValue(Gene::FloatType::CYTOSKELETON, reader.readFloat());
    return gene;
}

void writeGene(ByteWriter& writer, const GeneData& gene, int geneCount) {
    const cellularity::GeneView view(gene);
    for (int channel = 0; channel < 3; channel++) {
        writer.writeFloat(view.color(channel));
    }
    writer.writeFloat(view.splitMass());
    writer.writeFloat(view.splitRatio());
    writer.writeFloat(view.splitAngle());
    writer.writeFloat(view.child1Angle());
    writer.writeFloat(view.child2Angle());
    writer.writeFloat(view.massFlowPriority());
    writer.writeFloat(view.linkStrength());

    writer.writeInt(requireGeneIndex(
            view.child1GeneIndex().value(),
            geneCount,
            "Genome draft child 1 gene index")
                    .value());
    writer.writeInt(requireGeneIndex(
            view.child2GeneIndex().value(),
            geneCount,
            "Genome draft child 2 gene index")
                    .value());
    writer.writeBool(view.makesLink());
    writer.writeBool(view.child1KeepsLinks());
    writer.writeBool(view.child2KeepsLinks());
    writer.writeBool(view.staysAlive());
    writer.writeBool(view.initialCompatibilityFlag());
    writer.writeBool(view.child1Mirrored());
    writer.writeBool(view.child2Mirrored());
    writer.writeInt(CellTypes::ordinal(view.cellType()));

    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
        const cellularity::GeneParametricValueView value =
                view.parametricValue(index);
        writer.writeFloat(value.variableA());
        writer.writeFloat(value.variableB());
        writer.writeFloat(value.variableC());
        writer.writeShort(value.signalChannel());
        writer.writeShort(value.formulaType());
    }
    for (int integerIndex = 0; integerIndex < Gene::MAX_INTEGERS_COUNT; integerIndex++) {
        const int value = view.integerValue(integerIndex);
        if (integerIndex == Gene::VIRUS_FROM_INTEGER_INDEX
                || integerIndex == Gene::VIRUS_TO_INTEGER_INDEX) {
            writer.writeInt(requireGeneIndex(
                    value, geneCount, "Genome draft virus gene index")
                            .value());
        } else {
            writer.writeInt(value);
        }
    }
    writer.writeFloat(view.floatValue(Gene::FloatType::SMELL_AMPLITUDE));
    writer.writeFloat(view.floatValue(Gene::FloatType::SMELL_RED));
    writer.writeFloat(view.floatValue(Gene::FloatType::SMELL_GREEN));
    writer.writeFloat(view.floatValue(Gene::FloatType::SMELL_BLUE));
    writer.writeFloat(view.floatValue(Gene::FloatType::SMELL_THRESHOLD));
    writer.writeFloat(view.floatValue(Gene::FloatType::ADHESIN_LENGTH));
    writer.writeFloat(view.floatValue(Gene::FloatType::CYTOSKELETON));
}

} // namespace

bool cellularity::hasGenomeDraftWireHeader(
        const std::uint8_t* bytes, std::size_t byteCount) noexcept {
    return bytes != nullptr
            && byteCount >= kGenomeDraftWireHeaderBytes
            && bytes[0] == 0x43U
            && bytes[1] == 0x47U
            && bytes[2] == 0x44U
            && bytes[3] == 0x57U
            && bytes[4] == 0U
            && bytes[5] == 0U
            && bytes[6] == 0U
            && bytes[7] == 2U;
}

void validateGenomeDraft(
        const cellularity::GenomeData& genome) {
    if (!cellularity::isSupportedGeneCount(genome.size())) {
        fail(DecodeErrorCode::InvalidCount,
                "Genome draft gene count is outside the supported range");
    }
    const int geneCount = static_cast<int>(genome.size());
    for (int geneIndex = 0; geneIndex < geneCount; geneIndex++) {
        validateGene(genome[static_cast<std::size_t>(geneIndex)],
                geneIndex,
                geneCount);
    }
    if (!cellularity::extractGenomeMetadataFromCompatibilityFlags(
                genome.data(), genome.size())
                    .has_value()) {
        fail(DecodeErrorCode::InvalidCount,
                "Genome draft must contain exactly one initial gene");
    }
}

cellularity::GenomeData readGenomeDraft(ByteReader& reader) {
    if (reader.remaining() > cellularity::kGenomeDraftMaximumWireBytes) {
        fail(DecodeErrorCode::LimitExceeded, "Genome draft exceeds the wire size limit");
    }
    if (reader.readInt() != GENOME_DRAFT_MAGIC) {
        fail(DecodeErrorCode::InvalidHeader, "Invalid genome draft header");
    }
    int schemaVersion = reader.readInt();
    if (schemaVersion != GENOME_DRAFT_SCHEMA_VERSION) {
        fail(schemaVersion > GENOME_DRAFT_SCHEMA_VERSION
                        ? DecodeErrorCode::NewerSnapshotVersion
                        : DecodeErrorCode::InvalidVersion,
                "Unsupported genome draft schema version");
    }
    const int geneCount = requireGeneCount(reader.readInt());
    requireExactCount(reader.readInt(), Gene::MAX_PARAMETRIC_VALUES_COUNT,
            "parametric value");
    requireExactCount(reader.readInt(), Gene::MAX_INTEGERS_COUNT, "integer value");
    requireExactCount(reader.readInt(), Gene::MAX_FLOATS_COUNT, "float value");

    const std::size_t expectedPayloadBytes =
            static_cast<std::size_t>(geneCount)
            * cellularity::kGenomeDraftWireBytesPerGene;
    if (reader.remaining() < expectedPayloadBytes) {
        fail(DecodeErrorCode::TruncatedInput, "Genome draft ended unexpectedly");
    }
    cellularity::GenomeData genome(static_cast<std::size_t>(geneCount));
    for (int geneIndex = 0; geneIndex < geneCount; geneIndex++) {
        genome[static_cast<std::size_t>(geneIndex)] = readGene(reader, geneCount);
    }
    validateGenomeDraft(genome);
    if (reader.remaining() != 0) {
        fail(DecodeErrorCode::TrailingData, "Genome draft contains trailing data");
    }
    return genome;
}

std::vector<uint8_t> writeGenomeDraft(
        const cellularity::GenomeData& genome) {
    validateGenomeDraft(genome);
    ByteWriter writer;
    writer.writeInt(GENOME_DRAFT_MAGIC);
    writer.writeInt(GENOME_DRAFT_SCHEMA_VERSION);
    writer.writeInt(static_cast<std::int32_t>(genome.size()));
    writer.writeInt(Gene::MAX_PARAMETRIC_VALUES_COUNT);
    writer.writeInt(Gene::MAX_INTEGERS_COUNT);
    writer.writeInt(Gene::MAX_FLOATS_COUNT);
    for (const GeneData& gene : genome) {
        writeGene(writer, gene, static_cast<int>(genome.size()));
    }
    return writer.takeBytes();
}
