#include "formats/LegacyGeneCodec.h"

#include <algorithm>
#include <array>
#include <utility>

#include "core/CellDataConstants.h"
#include "core/CellType.h"
#include "core/Environment.h"
#include "core/GeneAccess.h"
#include "core/GenomeMetadata.h"
#include "formats/DecodedWorldValidation.h"
#include "formats/LegacyFormatConstants.h"
#include "formats/SnapshotCodecSupport.h"
#include "io/JavaObjectStreamReader.h"

namespace cellularity::snapshot_codec {
namespace {

static_assert(
        CellData::MAX_GENES_COUNT
                == legacy_format::kEffectiveImportedGenomeGeneCount,
        "The fixed backend adapter must preserve 40-slot legacy imports");

void readLegacyFloatValue(
        JavaObjectStreamReader& input,
        cellularity::GeneBuilder& builder,
        Gene::FloatType type) {
    builder.setFloatValue(type, input.readFloat());
}

int clampLegacyInteger(int value, int integerIndex) {
    return std::clamp(value, 0, Gene::INTEGER_MAX[integerIndex] - 1);
}

GeneData legacyWireDefaults() {
    // These are historical decoder defaults, not the current schema defaults.
    // Keep every omitted legacy field explicit so a future current-format default
    // change cannot alter imported bytes.
    GeneData gene;
    cellularity::GeneBuilder builder(gene);
    for (int channel = 0; channel < 3; channel++) {
        builder.setColor(channel, 0.0f);
    }
    builder.setSplitMass(0.0f);
    builder.setSplitRatio(0.0f);
    builder.setSplitAngle(0.0f);
    builder.setChild1Angle(0.0f);
    builder.setChild2Angle(0.0f);
    builder.setMassFlowPriority(0.0f);
    builder.setLinkStrength(0.0f);
    builder.setChild1GeneIndex(cellularity::GeneIndex(0));
    builder.setChild2GeneIndex(cellularity::GeneIndex(0));
    builder.setMakesLink(false);
    builder.setChild1KeepsLinks(false);
    builder.setChild2KeepsLinks(false);
    builder.setStaysAlive(false);
    builder.setInitialCompatibilityFlag(false);
    builder.setChild1Mirrored(false);
    builder.setChild2Mirrored(false);
    builder.setCellType(CellType::PHAGOCYTE);
    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
        builder.setParametricVariableA(index, 0.0f);
        builder.setParametricVariableB(index, 0.0f);
        builder.setParametricVariableC(index, 0.0f);
        builder.setParametricSignalChannel(index, 0);
        builder.setParametricFormulaType(index, 0);
    }
    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
        builder.setIntegerValue(index, 0);
    }
    builder.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 0.0f);
    builder.setFloatValue(Gene::FloatType::SMELL_RED, 0.0f);
    builder.setFloatValue(Gene::FloatType::SMELL_GREEN, 0.0f);
    builder.setFloatValue(Gene::FloatType::SMELL_BLUE, 0.0f);
    builder.setFloatValue(Gene::FloatType::SMELL_THRESHOLD, 0.0f);
    builder.setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
    builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 0.0f);
    return gene;
}

} // namespace

GeneData readLegacyGene(JavaObjectStreamReader& input, int cellVersion) {
    GeneData gene = legacyWireDefaults();
    cellularity::GeneBuilder builder(gene);
    const int version = cellVersion >= 3 ? input.readInt() : 1;
    if (version < legacy_format::kMinimumProductVersion) {
        failDecode(DecodeErrorCode::InvalidVersion,
                "Invalid legacy Gene product version");
    }
    if (version > legacy_format::kMaximumProductVersion) {
        failDecode(DecodeErrorCode::NewerProductVersion,
                "Unsupported newer legacy Gene product version");
    }
    float density = 0.0f;
    float swimForce = 0.0f;
    int virusFromGeneIndex = 0;
    int virusToGeneIndex = 0;
    int child1GeneIndex = 0;
    int child2GeneIndex = 0;

    builder.setColor(0, input.readFloat());
    builder.setColor(1, input.readFloat());
    builder.setColor(2, input.readFloat());

    if (version >= 7) {
        float splitMass = input.readFloat();
        float splitRatio = input.readFloat();
        float splitAngle = input.readFloat();
        float child1Angle = input.readFloat();
        float child2Angle = input.readFloat();
        if (version < 9) {
            splitAngle += static_cast<float>(Environment::PI);
            child1Angle += static_cast<float>(Environment::PI);
            child2Angle += static_cast<float>(Environment::PI);
            splitRatio = 1.0f - splitRatio;
        }
        builder.setSplitMass(splitMass);
        builder.setSplitRatio(splitRatio);
        builder.setSplitAngle(splitAngle);
        builder.setChild1Angle(child1Angle);
        builder.setChild2Angle(child2Angle);
        builder.setMassFlowPriority(input.readFloat());
        child1GeneIndex = input.readInt();
        child2GeneIndex = input.readInt();
        if (version >= legacy_format::kObsoleteExtensionProductVersion) {
            for (int field = 0;
                    field < legacy_format::kObsoleteGeneExtensionFieldCount;
                    ++field) {
                (void)input.readInt();
            }
        }
        builder.setMakesLink(input.readBoolean());
        builder.setChild1KeepsLinks(input.readBoolean());
        builder.setChild2KeepsLinks(input.readBoolean());
        const int cellTypeOrdinal = input.readInt();
        if (cellTypeOrdinal < 0 || cellTypeOrdinal >= CellTypes::length) {
            failDecode(DecodeErrorCode::InvalidValue,
                    "Invalid legacy Gene Cell type ordinal");
        }
        builder.setCellType(CellTypes::fromOrdinal(cellTypeOrdinal));
        input.readInt();
        builder.setStaysAlive(input.readBoolean());
        if (version < 9) {
            density = input.readFloat();
            virusFromGeneIndex = input.readInt();
            virusToGeneIndex = input.readInt();
        }
        builder.setInitialCompatibilityFlag(input.readBoolean());
        builder.setChild1Mirrored(input.readBoolean());
        builder.setChild2Mirrored(input.readBoolean());
        builder.setLinkStrength(version >= 16
                        ? input.readFloat()
                        : input.readFloat()
                                * CellDataConstants::MAX_LINK_STIFFNESS);
    } else {
        const float splitMass = static_cast<float>(input.readDouble());
        const float splitRatio = static_cast<float>(input.readDouble());
        float splitAngle = static_cast<float>(input.readDouble());
        float child1Angle = static_cast<float>(
                input.readDouble() + (version == 5 ? Environment::PI : 0.0));
        float child2Angle = static_cast<float>(input.readDouble());
        if (version < 9) {
            splitAngle += static_cast<float>(Environment::PI);
            child1Angle += static_cast<float>(Environment::PI);
            child2Angle += static_cast<float>(Environment::PI);
        }
        builder.setSplitMass(splitMass);
        builder.setSplitRatio(splitRatio);
        builder.setSplitAngle(splitAngle);
        builder.setChild1Angle(child1Angle);
        builder.setChild2Angle(child2Angle);
        builder.setMassFlowPriority(static_cast<float>(input.readDouble()));
        child1GeneIndex = input.readInt();
        child2GeneIndex = input.readInt();
        builder.setMakesLink(input.readBoolean());
        builder.setChild1KeepsLinks(input.readBoolean());
        builder.setChild2KeepsLinks(input.readBoolean());
        const int cellTypeOrdinal = input.readInt();
        if (cellTypeOrdinal < 0 || cellTypeOrdinal >= CellTypes::length) {
            failDecode(DecodeErrorCode::InvalidValue,
                    "Invalid legacy Gene Cell type ordinal");
        }
        builder.setCellType(CellTypes::fromOrdinal(cellTypeOrdinal));
        input.readInt();
        builder.setStaysAlive(input.readBoolean());
        density = static_cast<float>(input.readDouble());
        if (version >= 3) {
            virusFromGeneIndex = input.readInt();
            virusToGeneIndex = input.readInt();
        }
        if (version >= 4) {
            builder.setInitialCompatibilityFlag(input.readBoolean());
            builder.setChild1Mirrored(input.readBoolean());
            builder.setChild2Mirrored(input.readBoolean());
            builder.setLinkStrength(static_cast<float>(input.readDouble()));
        } else {
            builder.setInitialCompatibilityFlag(false);
            builder.setChild1Mirrored(false);
            builder.setChild2Mirrored(false);
            builder.setLinkStrength(0.0f);
        }
    }

    if (version < 8) {
        swimForce = 0.4f;
    } else if (version == 8) {
        swimForce = input.readFloat();
    }

    child1GeneIndex = std::clamp(
            child1GeneIndex,
            0,
            legacy_format::kEffectiveImportedGenomeGeneCount - 1);
    child2GeneIndex = std::clamp(
            child2GeneIndex,
            0,
            legacy_format::kEffectiveImportedGenomeGeneCount - 1);
    builder.setChild1GeneIndex(cellularity::GeneIndex(
            static_cast<cellularity::GeneIndex::Value>(child1GeneIndex)));
    builder.setChild2GeneIndex(cellularity::GeneIndex(
            static_cast<cellularity::GeneIndex::Value>(child2GeneIndex)));

    if (version >= 9) {
        for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
            int signalChannel = input.readShort();
            int formulaType;
            if (version >= 17) {
                formulaType = input.readShort();
            } else if (signalChannel == -1) {
                signalChannel = 0;
                formulaType = 0;
            } else if (signalChannel < CellData::SIGNAL_SUBSTANCE_COUNT) {
                formulaType = 1;
            } else {
                signalChannel -= CellData::SIGNAL_SUBSTANCE_COUNT;
                formulaType = 2;
            }
            builder.setParametricSignalChannel(index, signalChannel);
            builder.setParametricFormulaType(index, formulaType);
            builder.setParametricVariableA(index, input.readFloat());
            builder.setParametricVariableB(index, input.readFloat());
            builder.setParametricVariableC(index, input.readFloat());
        }
        for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
            if (version == 10 && index == Gene::SECRETE_TYPE_INTEGER_INDEX) {
                builder.setIntegerValue(index, 1);
            } else if (version < 15 && index == Gene::MAX_CONNECTION_COUNT_INDEX) {
                builder.setIntegerValue(index, CellData::MAX_NEIGHBORS - 2);
            } else if (version < 20 && index == Gene::TELOMERASE_LENGTH_INDEX) {
                builder.setIntegerValue(index, 0);
            } else {
                builder.setIntegerValue(
                        index, clampLegacyInteger(input.readInt(), index));
            }
        }
        readLegacyFloatValue(input, builder, Gene::FloatType::SMELL_AMPLITUDE);
        readLegacyFloatValue(input, builder, Gene::FloatType::SMELL_RED);
        readLegacyFloatValue(input, builder, Gene::FloatType::SMELL_GREEN);
        readLegacyFloatValue(input, builder, Gene::FloatType::SMELL_BLUE);
        readLegacyFloatValue(input, builder, Gene::FloatType::SMELL_THRESHOLD);
        builder.setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
        if (version >= 16) {
            readLegacyFloatValue(
                    input, builder, Gene::FloatType::ADHESIN_LENGTH);
        }
        if (version >= 14) {
            readLegacyFloatValue(
                    input, builder, Gene::FloatType::CYTOSKELETON);
        }
    } else {
        for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
            builder.setParametricSignalChannel(index, 0);
            builder.setParametricFormulaType(index, 0);
            builder.setParametricVariableA(index, 0.0f);
            builder.setParametricVariableB(index, 0.0f);
            builder.setParametricVariableC(index, 0.0f);
        }
        for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
            builder.setIntegerValue(index, 0);
        }
        builder.setFloatValue(Gene::FloatType::SMELL_AMPLITUDE, 0.0f);
        builder.setFloatValue(Gene::FloatType::SMELL_RED, 0.0f);
        builder.setFloatValue(Gene::FloatType::SMELL_GREEN, 0.0f);
        builder.setFloatValue(Gene::FloatType::SMELL_BLUE, 0.0f);
        builder.setFloatValue(Gene::FloatType::SMELL_THRESHOLD, 0.0f);
        builder.setFloatValue(Gene::FloatType::ADHESIN_LENGTH, 0.0f);
        builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 0.0f);
        builder.setParametricVariableB(
                Gene::SWIM_FORCE_VALUE_INDEX,
                -1.0f + 2.0f * (swimForce - Gene::VALUE_MIN[Gene::SWIM_FORCE_VALUE_INDEX]) / (Gene::VALUE_MAX[Gene::SWIM_FORCE_VALUE_INDEX] - Gene::VALUE_MIN[Gene::SWIM_FORCE_VALUE_INDEX]));
        builder.setParametricVariableB(
                Gene::DENSITY_VALUE_INDEX,
                -1.0f + 2.0f * (density - Gene::VALUE_MIN[Gene::SWIM_FORCE_VALUE_INDEX]) / (Gene::VALUE_MAX[Gene::SWIM_FORCE_VALUE_INDEX] - Gene::VALUE_MIN[Gene::SWIM_FORCE_VALUE_INDEX]));
        builder.setIntegerValue(
                Gene::VIRUS_FROM_INTEGER_INDEX,
                clampLegacyInteger(
                        virusFromGeneIndex, Gene::VIRUS_FROM_INTEGER_INDEX));
        builder.setIntegerValue(
                Gene::VIRUS_TO_INTEGER_INDEX,
                clampLegacyInteger(
                        virusToGeneIndex, Gene::VIRUS_TO_INTEGER_INDEX));
        builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
        builder.setIntegerValue(
                Gene::MAX_CONNECTION_COUNT_INDEX,
                Gene::INTEGER_MAX[Gene::MAX_CONNECTION_COUNT_INDEX] - 1);
    }

    builder.fix();
    validateDecodedGene(
            gene,
            "Legacy",
            legacy_format::kEffectiveImportedGenomeGeneCount);
    return gene;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> readLegacyGenome(
        JavaObjectStreamReader& input,
        int cellVersion) {
    const int geneCount = input.readInt();
    if (geneCount <= 0) {
        failDecode(DecodeErrorCode::InvalidCount,
                "Legacy Cell Genome must contain at least one Gene");
    }
    if (geneCount > legacy_format::kEffectiveImportedGenomeGeneCount) {
        failDecode(DecodeErrorCode::LimitExceeded,
                "Legacy Cell Genome gene count exceeds the safety limit");
    }
    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int index = 0; index < geneCount; index++) {
        genome[index] = readLegacyGene(input, cellVersion);
    }
    for (int index = geneCount;
            index < legacy_format::kEffectiveImportedGenomeGeneCount;
            index++) {
        cellularity::GeneBuilder builder(genome[index]);
        builder.copyFrom(cellularity::GeneView(genome[0]));
        builder.setInitialCompatibilityFlag(false);
    }

    if (!cellularity::normalizeLegacyInitialGeneFlags(
                genome.data(), genome.size())
                    .has_value()) {
        failDecode(DecodeErrorCode::InvalidValue,
                "Could not normalize legacy Cell Genome initial Gene");
    }
    return genome;
}

std::array<GeneData, CellData::MAX_GENES_COUNT> decodeLegacyGenomeBytes(
        std::vector<std::uint8_t> bytes) {
    JavaObjectStreamReader input(std::move(bytes));
    const int version = input.readInt();
    if (version > legacy_format::kMaximumProductVersion) {
        failDecode(DecodeErrorCode::NewerProductVersion,
                "Unsupported newer legacy Genome product version");
    }
    if (version < legacy_format::kMinimumProductVersion) {
        failDecode(DecodeErrorCode::InvalidVersion,
                "Invalid legacy Genome product version");
    }

    const int geneCount = legacy_format::standaloneGenomeSerializedGeneCount(version);
    std::array<GeneData, CellData::MAX_GENES_COUNT> genome{};
    for (int index = 0; index < geneCount; index++) {
        genome[index] = readLegacyGene(input, version);
    }
    for (int index = geneCount;
            index < legacy_format::kEffectiveImportedGenomeGeneCount;
            index++) {
        cellularity::GeneBuilder builder(genome[index]);
        builder.copyFrom(cellularity::GeneView(genome[0]));
        builder.setInitialCompatibilityFlag(false);
    }

    if (!cellularity::normalizeLegacyInitialGeneFlags(
                genome.data(), genome.size())
                    .has_value()) {
        failDecode(DecodeErrorCode::InvalidValue,
                "Could not normalize legacy Genome initial Gene");
    }
    if (input.hasRemainingBytes()) {
        failDecode(DecodeErrorCode::TrailingData,
                "Unexpected trailing legacy Genome data");
    }
    return genome;
}

} // namespace cellularity::snapshot_codec
