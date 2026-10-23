#include "formats/release/ReleaseGenomeCodec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "core/CanonicalGene.h"
#include "core/CellType.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "core/GenomeMetadata.h"
#include "formats/GenomeDraftCodec.h"
#include "io/DecodeError.h"

namespace cellularity::save_format::release {
namespace {

static_assert(sizeof(float) == 4);
static_assert(sizeof(std::int8_t) == 1);
static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(std::uint64_t) == 8);

constexpr std::uint8_t kKnownBehaviorFlags = 0x3fU;

[[noreturn]] void fail(DecodeErrorCode code, const std::string& message) {
    throw DecodeError(code, message);
}

template <typename Unsigned>
void appendUnsignedLittleEndian(std::vector<std::uint8_t>& bytes,
        Unsigned value) {
    static_assert(std::is_unsigned_v<Unsigned>);
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(
                value >> static_cast<unsigned>(index * 8U)));
    }
}

template <typename Value>
void appendLittleEndian(std::vector<std::uint8_t>& bytes, Value value) {
    if constexpr (std::is_same_v<Value, std::uint8_t>) {
        bytes.push_back(value);
    } else if constexpr (std::is_same_v<Value, std::int8_t>) {
        std::uint8_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        bytes.push_back(bits);
    } else if constexpr (std::is_same_v<Value, std::uint16_t>
            || std::is_same_v<Value, std::uint32_t>
            || std::is_same_v<Value, std::uint64_t>) {
        appendUnsignedLittleEndian(bytes, value);
    } else if constexpr (std::is_same_v<Value, float>) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendUnsignedLittleEndian(bytes, bits);
    } else {
        static_assert(!sizeof(Value), "Unsupported release genome scalar type");
    }
}

template <typename Value, typename Getter>
LogicalColumn makeColumn(std::uint32_t fieldId,
        PrimitiveType primitiveType,
        std::string canonicalName,
        std::size_t count,
        Getter&& getter) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
        throw std::length_error("Release genome column is too large");
    }
    LogicalColumn column;
    column.fieldId = fieldId;
    column.primitiveType = primitiveType;
    column.canonicalName = std::move(canonicalName);
    column.logicalValueCount = count;
    column.decodedBytes.reserve(count * sizeof(Value));
    for (std::size_t index = 0; index < count; ++index) {
        appendLittleEndian<Value>(column.decodedBytes, getter(index));
    }
    return column;
}

template <typename Value>
Value readLittleEndian(const LogicalColumn& column, std::size_t index) {
    const std::size_t width = sizeof(Value);
    if (index >= column.logicalValueCount
            || index > std::numeric_limits<std::size_t>::max() / width
            || index * width > column.decodedBytes.size()
            || column.decodedBytes.size() - index * width < width) {
        fail(DecodeErrorCode::TruncatedInput,
                "Release genome column value is truncated");
    }
    using Bits = std::conditional_t<
            sizeof(Value) == 8,
            std::uint64_t,
            std::conditional_t<
                    sizeof(Value) == 4,
                    std::uint32_t,
                    std::conditional_t<
                            sizeof(Value) == 2,
                            std::uint16_t,
                            std::uint8_t>>>;
    Bits bits = 0;
    const std::size_t offset = index * width;
    for (std::size_t byte = 0; byte < width; ++byte) {
        bits |= static_cast<Bits>(column.decodedBytes[offset + byte])
                << static_cast<unsigned>(byte * 8U);
    }
    if constexpr (std::is_floating_point_v<Value>
            || std::is_signed_v<Value>) {
        Value value{};
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    } else {
        return bits;
    }
}

template <typename Value>
Value readLittleEndian(const std::uint8_t* bytes) {
    using Bits = std::conditional_t<
            sizeof(Value) == 8,
            std::uint64_t,
            std::conditional_t<
                    sizeof(Value) == 4,
                    std::uint32_t,
                    std::conditional_t<
                            sizeof(Value) == 2,
                            std::uint16_t,
                            std::uint8_t>>>;
    Bits bits = 0;
    for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
        bits |= static_cast<Bits>(bytes[byte])
                << static_cast<unsigned>(byte * 8U);
    }
    if constexpr (std::is_floating_point_v<Value>
            || std::is_signed_v<Value>) {
        Value value{};
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    } else {
        return bits;
    }
}

template <typename Value, typename Setter>
void loadTypedColumn(const ReleaseColumnLoader& loadColumn,
        std::uint32_t fieldId,
        PrimitiveType primitiveType,
        const std::string& canonicalName,
        std::uint64_t rowCount,
        Setter&& setter) {
    std::uint64_t nextValue = 0;
    loadColumn(
            fieldId,
            primitiveType,
            canonicalName,
            rowCount,
            [&](std::uint64_t firstValue,
                    std::uint64_t valueCount,
                    const std::uint8_t* bytes,
                    std::size_t byteCount) {
                if (firstValue != nextValue
                        || valueCount
                                > std::numeric_limits<std::size_t>::max()
                                        / sizeof(Value)
                        || byteCount
                                != static_cast<std::size_t>(valueCount)
                                        * sizeof(Value)) {
                    fail(DecodeErrorCode::InvalidLength,
                            "Release gene column chunks are inconsistent");
                }
                for (std::uint64_t offset = 0; offset < valueCount; ++offset) {
                    setter(
                            static_cast<std::size_t>(firstValue + offset),
                            readLittleEndian<Value>(
                                    bytes + static_cast<std::size_t>(offset) * sizeof(Value)));
                }
                nextValue += valueCount;
            });
    if (nextValue != rowCount) {
        fail(DecodeErrorCode::TruncatedInput,
                "Release gene column is incomplete");
    }
}

std::uint8_t geneBehaviorFlags(GeneView gene) {
    return static_cast<std::uint8_t>(
            (gene.makesLink() ? 1U << 0U : 0U)
            | (gene.child1KeepsLinks() ? 1U << 1U : 0U)
            | (gene.child2KeepsLinks() ? 1U << 2U : 0U)
            | (gene.staysAlive() ? 1U << 3U : 0U)
            | (gene.child1Mirrored() ? 1U << 4U : 0U)
            | (gene.child2Mirrored() ? 1U << 5U : 0U));
}

struct GenomeExport final {
    std::vector<CanonicalGene> genes;
    std::vector<std::uint32_t> geneReferences;
    std::uint16_t initialGene = 0;
};

GenomeExport collectGenome(const GenomeData& genome) {
    validateGenomeDraft(genome);
    const auto metadata = extractGenomeMetadataFromCompatibilityFlags(
            genome.data(), genome.size());
    if (!metadata.has_value()) {
        fail(DecodeErrorCode::InvalidCount,
                "Release genome must contain exactly one initial gene");
    }

    GenomeExport result;
    result.initialGene = metadata->initialGeneIndex.value();
    result.genes.reserve(genome.size());
    result.geneReferences.reserve(genome.size());
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> identities;
    for (std::size_t index = 0; index < genome.size(); ++index) {
        CanonicalGeneResult canonical = canonicalizeGene(
                genome[index],
                GeneIndex(static_cast<GeneIndex::Value>(index)),
                genome.size());
        if (!canonical.hasValue()) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release genome contains an invalid gene");
        }
        const std::uint64_t hash = canonicalGeneHash(*canonical.value);
        auto& candidates = identities[hash];
        const auto duplicate = std::find_if(
                candidates.begin(), candidates.end(), [&](std::uint32_t id) {
                    return canonicalGeneEqual(
                            result.genes[id], *canonical.value);
                });
        if (duplicate != candidates.end()) {
            result.geneReferences.push_back(*duplicate);
            continue;
        }
        if (result.genes.size() >= std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("Release genome unique gene table is too large");
        }
        const std::uint32_t id = static_cast<std::uint32_t>(result.genes.size());
        result.genes.push_back(std::move(*canonical.value));
        candidates.push_back(id);
        result.geneReferences.push_back(id);
    }
    return result;
}

LogicalSection exportGenes(const GenomeExport& genome) {
    LogicalSection section;
    section.sectionId = section_id::kGenes;
    section.canonicalName = "genes";
    section.logicalRowCount = genome.genes.size();
    auto& columns = section.columns;
    columns.reserve(87);
    const std::size_t count = genome.genes.size();
    const auto view = [&genome](std::size_t index) {
        return GeneView(genome.genes[index].data());
    };
    const auto addF32 = [&](std::uint32_t id, const char* name, auto getter) {
        columns.push_back(makeColumn<float>(
                id, PrimitiveType::F32, name, count,
                [&](std::size_t index) {
                    return getter(view(index));
                }));
    };
    const auto addU16 = [&](std::uint32_t id, const char* name, auto getter) {
        columns.push_back(makeColumn<std::uint16_t>(
                id, PrimitiveType::U16, name, count,
                [&](std::size_t index) {
                    return static_cast<std::uint16_t>(getter(view(index)));
                }));
    };
    const auto addU8 = [&](std::uint32_t id, const char* name, auto getter) {
        columns.push_back(makeColumn<std::uint8_t>(
                id, PrimitiveType::U8, name, count,
                [&](std::size_t index) {
                    return static_cast<std::uint8_t>(getter(view(index)));
                }));
    };
    addF32(1, "color_r", [](GeneView gene) {
        return gene.color(0);
    });
    addF32(2, "color_g", [](GeneView gene) {
        return gene.color(1);
    });
    addF32(3, "color_b", [](GeneView gene) {
        return gene.color(2);
    });
    addF32(4, "split_mass", [](GeneView gene) {
        return gene.splitMass();
    });
    addF32(5, "split_ratio", [](GeneView gene) {
        return gene.splitRatio();
    });
    addF32(6, "split_angle", [](GeneView gene) {
        return gene.splitAngle();
    });
    addF32(7, "child_1_angle", [](GeneView gene) {
        return gene.child1Angle();
    });
    addF32(8, "child_2_angle", [](GeneView gene) {
        return gene.child2Angle();
    });
    addF32(9, "mass_flow_priority",
            [](GeneView gene) {
                return gene.massFlowPriority();
            });
    addF32(10, "link_strength", [](GeneView gene) {
        return gene.linkStrength();
    });
    addU16(11, "child_1_gene",
            [](GeneView gene) {
                return gene.child1GeneIndex().value();
            });
    addU16(12, "child_2_gene",
            [](GeneView gene) {
                return gene.child2GeneIndex().value();
            });
    addU8(13, "behavior_flags",
            [](GeneView gene) {
                return geneBehaviorFlags(gene);
            });
    addU8(14, "cell_type",
            [](GeneView gene) {
                return CellTypes::ordinal(gene.cellType());
            });

    constexpr std::array<const char*, Gene::MAX_PARAMETRIC_VALUES_COUNT>
            parametricNames{{"swim_force", "density", "myocyte_bending",
                    "myocyte_contraction", "myocyte_lifting",
                    "stem_threshold_1", "stem_threshold_2",
                    "signal_output_0", "signal_output_1",
                    "signal_output_2", "signal_output_3"}};
    for (int parameter = 0; parameter < Gene::MAX_PARAMETRIC_VALUES_COUNT;
            ++parameter) {
        const std::uint32_t firstId =
                100U + static_cast<std::uint32_t>(parameter * 5);
        const std::string prefix = parametricNames[parameter];
        addF32(firstId, (prefix + "_variable_a").c_str(),
                [parameter](GeneView gene) {
                    return gene.parametricVariableA(parameter);
                });
        addF32(firstId + 1U, (prefix + "_variable_b").c_str(),
                [parameter](GeneView gene) {
                    return gene.parametricVariableB(parameter);
                });
        addF32(firstId + 2U, (prefix + "_variable_c").c_str(),
                [parameter](GeneView gene) {
                    return gene.parametricVariableC(parameter);
                });
        addU8(firstId + 3U, (prefix + "_signal_channel").c_str(),
                [parameter](GeneView gene) {
                    return gene.parametricSignalChannel(parameter);
                });
        addU8(firstId + 4U, (prefix + "_formula_type").c_str(),
                [parameter](GeneView gene) {
                    return gene.parametricFormulaType(parameter);
                });
    }

    constexpr std::array<const char*, Gene::MAX_INTEGERS_COUNT> integerNames{{"virus_from_gene", "virus_to_gene", "smell_output", "smell_type",
            "secretion_type", "signal_output_channel_0",
            "signal_output_channel_1", "signal_output_channel_2",
            "signal_output_channel_3", "max_connection_count",
            "telomerase_length"}};
    for (int integer = 0; integer < Gene::MAX_INTEGERS_COUNT; ++integer) {
        addU16(200U + static_cast<std::uint32_t>(integer), integerNames[integer],
                [integer](GeneView gene) {
                    return gene.integerValue(integer);
                });
    }

    constexpr std::array<const char*, Gene::MAX_FLOATS_COUNT> floatNames{{"smell_amplitude", "smell_red", "smell_green", "smell_blue",
            "smell_threshold", "adhesin_length", "cytoskeleton"}};
    for (int floatIndex = 0; floatIndex < Gene::MAX_FLOATS_COUNT; ++floatIndex) {
        addF32(300U + static_cast<std::uint32_t>(floatIndex),
                floatNames[floatIndex],
                [floatIndex](GeneView gene) {
                    return gene.floatValue(
                            static_cast<Gene::FloatType>(floatIndex));
                });
    }
    return section;
}

LogicalSection exportGenomes(const GenomeExport& genome) {
    LogicalSection section;
    section.sectionId = section_id::kGenomes;
    section.canonicalName = "genomes";
    section.logicalRowCount = 1;
    section.columns.push_back(makeColumn<std::uint64_t>(
            1, PrimitiveType::U64, "gene_ref_offsets", 2,
            [&genome](std::size_t index) {
                return index == 0
                        ? std::uint64_t{0}
                        : static_cast<std::uint64_t>(genome.geneReferences.size());
            }));
    section.columns.push_back(makeColumn<std::uint32_t>(
            2, PrimitiveType::U32, "gene_refs", genome.geneReferences.size(),
            [&genome](std::size_t index) {
                return genome.geneReferences[index];
            }));
    section.columns.push_back(makeColumn<std::uint16_t>(
            3, PrimitiveType::U16, "initial_gene", 1,
            [&genome](std::size_t) {
                return genome.initialGene;
            }));
    return section;
}

std::uint32_t cellTypeMask(const GenomeExport& genome) {
    std::uint32_t result = 0;
    for (const CanonicalGene& gene : genome.genes) {
        const int ordinal = CellTypes::ordinal(GeneView(gene.data()).cellType());
        if (ordinal >= 0 && ordinal < 32) {
            result |= 1U << static_cast<unsigned>(ordinal);
        }
    }
    return result;
}

LogicalSnapshot exportGenomeSnapshot(const GenomeData& source) {
    GenomeExport genome = collectGenome(source);
    LogicalSnapshot snapshot;
    snapshot.documentKind = DocumentKind::Genome;
    snapshot.metadata = {
            MetadataEntry::u32(metadata_field::kContentSchemaMajor,
                    "content_schema_major",
                    kGenomeContentSchemaMajor, true),
            MetadataEntry::u32(metadata_field::kContentSchemaMinor,
                    "content_schema_minor",
                    kGenomeContentSchemaMinor, true),
            MetadataEntry::u32(metadata_field::kGenomeGeneCount,
                    "gene_count",
                    static_cast<std::uint32_t>(
                            genome.geneReferences.size()),
                    true),
            MetadataEntry::u32(metadata_field::kGenomeInitialGeneIndex,
                    "initial_gene_index",
                    genome.initialGene, true),
            MetadataEntry::u32(metadata_field::kGenomeUniqueGeneCount,
                    "unique_gene_count",
                    static_cast<std::uint32_t>(genome.genes.size()),
                    false),
            MetadataEntry::u32(metadata_field::kGenomeCellTypeMask,
                    "cell_type_mask", cellTypeMask(genome), false),
    };
    snapshot.sections.push_back(exportGenes(genome));
    snapshot.sections.push_back(exportGenomes(genome));
    return snapshot;
}

const MetadataEntry& requireMetadata(
        const std::vector<MetadataEntry>& metadata,
        std::uint32_t fieldId) {
    const auto found = std::find_if(
            metadata.begin(), metadata.end(), [fieldId](const MetadataEntry& entry) {
                return entry.fieldId == fieldId;
            });
    if (found == metadata.end()) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome is missing required metadata");
    }
    return *found;
}

std::optional<std::uint32_t> optionalU32Metadata(
        const std::vector<MetadataEntry>& metadata,
        std::uint32_t fieldId) {
    const auto found = std::find_if(
            metadata.begin(), metadata.end(), [fieldId](const MetadataEntry& entry) {
                return entry.fieldId == fieldId;
            });
    return found == metadata.end()
            ? std::nullopt
            : std::optional<std::uint32_t>(found->asU32());
}

struct GenomeCard final {
    std::uint32_t geneCount = 0;
    std::uint32_t initialGene = 0;
    std::optional<std::uint32_t> uniqueGeneCount;
    std::optional<std::uint32_t> cellTypes;
};

GenomeCard readGenomeCard(const std::vector<MetadataEntry>& metadata) {
    const std::uint32_t schemaMajor = requireMetadata(
            metadata, metadata_field::kContentSchemaMajor)
                                              .asU32();
    if (schemaMajor == 0) {
        fail(DecodeErrorCode::InvalidVersion,
                "Release genome content schema major version is zero");
    }
    if (schemaMajor != kGenomeContentSchemaMajor) {
        fail(DecodeErrorCode::NewerSnapshotVersion,
                "Release genome content schema is unsupported");
    }
    (void)requireMetadata(
            metadata, metadata_field::kContentSchemaMinor)
            .asU32();
    GenomeCard card;
    card.geneCount = requireMetadata(
            metadata, metadata_field::kGenomeGeneCount)
                             .asU32();
    card.initialGene = requireMetadata(
            metadata, metadata_field::kGenomeInitialGeneIndex)
                               .asU32();
    card.uniqueGeneCount = optionalU32Metadata(
            metadata, metadata_field::kGenomeUniqueGeneCount);
    card.cellTypes = optionalU32Metadata(
            metadata, metadata_field::kGenomeCellTypeMask);
    if (!isSupportedGeneCount(card.geneCount)
            || card.initialGene >= card.geneCount) {
        fail(DecodeErrorCode::InvalidCount,
                "Release genome card contains an invalid gene count or initial index");
    }
    if (card.uniqueGeneCount.has_value()
            && (*card.uniqueGeneCount == 0
                    || *card.uniqueGeneCount > card.geneCount)) {
        fail(DecodeErrorCode::InvalidCount,
                "Release genome card contains an invalid unique gene count");
    }
    return card;
}

const LogicalSection& requireSection(const LogicalSnapshot& snapshot,
        std::uint32_t id,
        const char* name,
        std::size_t columnCount) {
    const LogicalSection* section = findLogicalSection(snapshot, id);
    if (section == nullptr || section->canonicalName != name
            || section->schemaMajor != 1
            || section->columns.size() != columnCount) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome section inventory does not match schema");
    }
    return *section;
}

const LogicalColumn& requireColumn(const LogicalSection& section,
        std::uint32_t id,
        PrimitiveType type,
        const std::string& name,
        std::optional<std::uint64_t> valueCount) {
    const LogicalColumn* column = findLogicalColumn(section, id);
    if (column == nullptr || column->primitiveType != type
            || column->canonicalName != name
            || (valueCount.has_value()
                    && column->logicalValueCount != *valueCount)) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome column inventory does not match schema");
    }
    return *column;
}

struct GeneColumns final {
    std::array<const LogicalColumn*, 14> core{};
    std::array<std::array<const LogicalColumn*, 5>,
            Gene::MAX_PARAMETRIC_VALUES_COUNT>
            parametric{};
    std::array<const LogicalColumn*, Gene::MAX_INTEGERS_COUNT> integers{};
    std::array<const LogicalColumn*, Gene::MAX_FLOATS_COUNT> floats{};
};

GeneColumns bindGeneColumns(const LogicalSection& section,
        std::uint64_t count) {
    GeneColumns result;
    constexpr std::array<const char*, 14> coreNames{{"color_r", "color_g", "color_b", "split_mass", "split_ratio",
            "split_angle", "child_1_angle", "child_2_angle",
            "mass_flow_priority", "link_strength", "child_1_gene",
            "child_2_gene", "behavior_flags", "cell_type"}};
    for (std::uint32_t index = 0; index < result.core.size(); ++index) {
        const PrimitiveType type = index < 10
                ? PrimitiveType::F32
                : (index == 10 || index == 11
                                  ? PrimitiveType::U16
                                  : PrimitiveType::U8);
        result.core[index] = &requireColumn(
                section, index + 1U, type, coreNames[index], count);
    }

    constexpr std::array<const char*, Gene::MAX_PARAMETRIC_VALUES_COUNT>
            parameterNames{{"swim_force", "density", "myocyte_bending",
                    "myocyte_contraction", "myocyte_lifting",
                    "stem_threshold_1", "stem_threshold_2",
                    "signal_output_0", "signal_output_1",
                    "signal_output_2", "signal_output_3"}};
    constexpr std::array<const char*, 5> parameterSuffixes{{"variable_a", "variable_b", "variable_c",
            "signal_channel", "formula_type"}};
    for (std::size_t parameter = 0; parameter < result.parametric.size();
            ++parameter) {
        const std::uint32_t firstId =
                100U + static_cast<std::uint32_t>(parameter * 5U);
        for (std::size_t value = 0; value < 5; ++value) {
            result.parametric[parameter][value] = &requireColumn(
                    section,
                    firstId + static_cast<std::uint32_t>(value),
                    value < 3 ? PrimitiveType::F32 : PrimitiveType::U8,
                    std::string(parameterNames[parameter]) + "_"
                            + parameterSuffixes[value],
                    count);
        }
    }

    constexpr std::array<const char*, Gene::MAX_INTEGERS_COUNT> integerNames{{"virus_from_gene", "virus_to_gene", "smell_output", "smell_type",
            "secretion_type", "signal_output_channel_0",
            "signal_output_channel_1", "signal_output_channel_2",
            "signal_output_channel_3", "max_connection_count",
            "telomerase_length"}};
    for (std::size_t index = 0; index < result.integers.size(); ++index) {
        result.integers[index] = &requireColumn(
                section, 200U + static_cast<std::uint32_t>(index),
                PrimitiveType::U16, integerNames[index], count);
    }
    constexpr std::array<const char*, Gene::MAX_FLOATS_COUNT> floatNames{{"smell_amplitude", "smell_red", "smell_green", "smell_blue",
            "smell_threshold", "adhesin_length", "cytoskeleton"}};
    for (std::size_t index = 0; index < result.floats.size(); ++index) {
        result.floats[index] = &requireColumn(
                section, 300U + static_cast<std::uint32_t>(index),
                PrimitiveType::F32, floatNames[index], count);
    }
    return result;
}

GeneData readGeneRow(const GeneColumns& columns, std::size_t row) {
    GeneData gene;
    GeneBuilder builder(gene);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        builder.setColor(static_cast<int>(channel),
                readLittleEndian<float>(*columns.core[channel], row));
    }
    builder.setSplitMass(readLittleEndian<float>(*columns.core[3], row));
    builder.setSplitRatio(readLittleEndian<float>(*columns.core[4], row));
    builder.setSplitAngle(readLittleEndian<float>(*columns.core[5], row));
    builder.setChild1Angle(readLittleEndian<float>(*columns.core[6], row));
    builder.setChild2Angle(readLittleEndian<float>(*columns.core[7], row));
    builder.setMassFlowPriority(readLittleEndian<float>(*columns.core[8], row));
    builder.setLinkStrength(readLittleEndian<float>(*columns.core[9], row));
    builder.setChild1GeneIndex(GeneIndex(
            readLittleEndian<std::uint16_t>(*columns.core[10], row)));
    builder.setChild2GeneIndex(GeneIndex(
            readLittleEndian<std::uint16_t>(*columns.core[11], row)));
    const std::uint8_t flags =
            readLittleEndian<std::uint8_t>(*columns.core[12], row);
    if ((flags & ~kKnownBehaviorFlags) != 0) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome gene has unknown behavior flags");
    }
    builder.setMakesLink((flags & (1U << 0U)) != 0);
    builder.setChild1KeepsLinks((flags & (1U << 1U)) != 0);
    builder.setChild2KeepsLinks((flags & (1U << 2U)) != 0);
    builder.setStaysAlive((flags & (1U << 3U)) != 0);
    builder.setChild1Mirrored((flags & (1U << 4U)) != 0);
    builder.setChild2Mirrored((flags & (1U << 5U)) != 0);
    const int cellType = readLittleEndian<std::uint8_t>(
            *columns.core[13], row);
    if (cellType < 0 || cellType >= CellTypes::length) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome gene has an invalid cell type");
    }
    builder.setCellType(CellTypes::fromOrdinal(cellType));
    builder.setInitialCompatibilityFlag(false);

    for (std::size_t parameter = 0; parameter < columns.parametric.size();
            ++parameter) {
        builder.setParametricVariableA(
                static_cast<int>(parameter),
                readLittleEndian<float>(*columns.parametric[parameter][0], row));
        builder.setParametricVariableB(
                static_cast<int>(parameter),
                readLittleEndian<float>(*columns.parametric[parameter][1], row));
        builder.setParametricVariableC(
                static_cast<int>(parameter),
                readLittleEndian<float>(*columns.parametric[parameter][2], row));
        builder.setParametricSignalChannel(
                static_cast<int>(parameter),
                readLittleEndian<std::uint8_t>(
                        *columns.parametric[parameter][3], row));
        builder.setParametricFormulaType(
                static_cast<int>(parameter),
                readLittleEndian<std::uint8_t>(
                        *columns.parametric[parameter][4], row));
    }
    for (std::size_t index = 0; index < columns.integers.size(); ++index) {
        builder.setIntegerValue(
                static_cast<int>(index),
                readLittleEndian<std::uint16_t>(*columns.integers[index], row));
    }
    for (std::size_t index = 0; index < columns.floats.size(); ++index) {
        builder.setFloatValue(
                static_cast<Gene::FloatType>(index),
                readLittleEndian<float>(*columns.floats[index], row));
    }
    return gene;
}

bool sameLogicalSections(const std::vector<LogicalSection>& left,
        const std::vector<LogicalSection>& right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t sectionIndex = 0; sectionIndex < left.size(); ++sectionIndex) {
        const LogicalSection& a = left[sectionIndex];
        const LogicalSection& b = right[sectionIndex];
        if (a.sectionId != b.sectionId || a.schemaMajor != b.schemaMajor
                || a.canonicalName != b.canonicalName
                || a.logicalRowCount != b.logicalRowCount
                || a.columns.size() != b.columns.size()) {
            return false;
        }
        for (std::size_t columnIndex = 0; columnIndex < a.columns.size();
                ++columnIndex) {
            const LogicalColumn& x = a.columns[columnIndex];
            const LogicalColumn& y = b.columns[columnIndex];
            if (x.fieldId != y.fieldId || x.primitiveType != y.primitiveType
                    || x.canonicalName != y.canonicalName
                    || x.logicalValueCount != y.logicalValueCount
                    || x.decodedBytes != y.decodedBytes) {
                return false;
            }
        }
    }
    return true;
}

std::vector<std::uint8_t> slice(const std::vector<std::uint8_t>& bytes,
        std::uint64_t offset,
        std::uint64_t count,
        const char* label) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        fail(DecodeErrorCode::TruncatedInput,
                std::string("Release genome ") + label + " is truncated");
    }
    return std::vector<std::uint8_t>(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
}

ReleaseGenomeInspection inspectHeaderAndMetadata(
        const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kBootstrapBytes) {
        fail(DecodeErrorCode::TruncatedInput,
                "Release genome bootstrap is truncated");
    }
    const std::vector<std::uint8_t> bootstrap(
            bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(kBootstrapBytes));
    ReleaseGenomeInspection result;
    result.header = decodeBootstrap(bootstrap, bytes.size());
    if (result.header.documentKind != DocumentKind::Genome) {
        fail(DecodeErrorCode::InvalidHeader,
                "CLTY document is not a standalone genome");
    }
    const std::vector<std::uint8_t> metadataBytes = slice(
            bytes, result.header.metadataOffset, result.header.metadataBytes,
            "metadata");
    const std::vector<MetadataEntry> metadata = decodeMetadata(
            DocumentKind::Genome, metadataBytes, result.header.metadataCrc32);
    const GenomeCard card = readGenomeCard(metadata);
    result.geneCount = card.geneCount;
    result.initialGeneIndex = card.initialGene;
    result.uniqueGeneCount = card.uniqueGeneCount.value_or(0);
    result.cellTypeMask = card.cellTypes.value_or(0);
    std::vector<std::uint8_t> preview;
    if (result.header.previewBytes != 0) {
        preview = slice(bytes, result.header.previewOffset,
                result.header.previewBytes, "preview");
    }
    result.previewDisposition = classifyPreview(result.header, preview);
    return result;
}

} // namespace

std::vector<GeneData> decodeReleaseGeneTableFromColumns(
        std::uint64_t rowCount,
        const ReleaseColumnLoader& loadColumn) {
    if (rowCount > std::numeric_limits<std::size_t>::max()) {
        fail(DecodeErrorCode::LimitExceeded,
                "Release gene table exceeds addressable memory");
    }
    std::vector<GeneData> rows(static_cast<std::size_t>(rowCount));
    const auto f32 = [&](std::uint32_t id,
                             const std::string& name,
                             auto setter) {
        loadTypedColumn<float>(
                loadColumn, id, PrimitiveType::F32, name, rowCount,
                [&](std::size_t row, float value) {
                    setter(GeneBuilder(rows[row]), value);
                });
    };
    const auto u16 = [&](std::uint32_t id,
                             const std::string& name,
                             auto setter) {
        loadTypedColumn<std::uint16_t>(
                loadColumn, id, PrimitiveType::U16, name, rowCount,
                [&](std::size_t row, std::uint16_t value) {
                    setter(GeneBuilder(rows[row]), value);
                });
    };
    const auto u8 = [&](std::uint32_t id,
                            const std::string& name,
                            auto setter) {
        loadTypedColumn<std::uint8_t>(
                loadColumn, id, PrimitiveType::U8, name, rowCount,
                [&](std::size_t row, std::uint8_t value) {
                    setter(GeneBuilder(rows[row]), value);
                });
    };
    f32(1, "color_r", [](GeneBuilder builder, float value) {
        builder.setColor(0, value);
    });
    f32(2, "color_g", [](GeneBuilder builder, float value) {
        builder.setColor(1, value);
    });
    f32(3, "color_b", [](GeneBuilder builder, float value) {
        builder.setColor(2, value);
    });
    f32(4, "split_mass", [](GeneBuilder builder, float value) {
        builder.setSplitMass(value);
    });
    f32(5, "split_ratio", [](GeneBuilder builder, float value) {
        builder.setSplitRatio(value);
    });
    f32(6, "split_angle", [](GeneBuilder builder, float value) {
        builder.setSplitAngle(value);
    });
    f32(7, "child_1_angle", [](GeneBuilder builder, float value) {
        builder.setChild1Angle(value);
    });
    f32(8, "child_2_angle", [](GeneBuilder builder, float value) {
        builder.setChild2Angle(value);
    });
    f32(9, "mass_flow_priority", [](GeneBuilder builder, float value) {
        builder.setMassFlowPriority(value);
    });
    f32(10, "link_strength", [](GeneBuilder builder, float value) {
        builder.setLinkStrength(value);
    });
    u16(11, "child_1_gene", [](GeneBuilder builder, std::uint16_t value) {
        builder.setChild1GeneIndex(GeneIndex(value));
    });
    u16(12, "child_2_gene", [](GeneBuilder builder, std::uint16_t value) {
        builder.setChild2GeneIndex(GeneIndex(value));
    });
    u8(13, "behavior_flags", [](GeneBuilder builder, std::uint8_t flags) {
        if ((flags & ~kKnownBehaviorFlags) != 0) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release genome gene has unknown behavior flags");
        }
        builder.setMakesLink((flags & (1U << 0U)) != 0);
        builder.setChild1KeepsLinks((flags & (1U << 1U)) != 0);
        builder.setChild2KeepsLinks((flags & (1U << 2U)) != 0);
        builder.setStaysAlive((flags & (1U << 3U)) != 0);
        builder.setChild1Mirrored((flags & (1U << 4U)) != 0);
        builder.setChild2Mirrored((flags & (1U << 5U)) != 0);
    });
    u8(14, "cell_type", [](GeneBuilder builder, std::uint8_t value) {
        if (value >= CellTypes::length) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release genome gene has an invalid cell type");
        }
        builder.setCellType(CellTypes::fromOrdinal(value));
        builder.setInitialCompatibilityFlag(false);
    });

    constexpr std::array<const char*, Gene::MAX_PARAMETRIC_VALUES_COUNT>
            parameterNames{{"swim_force", "density", "myocyte_bending",
                    "myocyte_contraction", "myocyte_lifting",
                    "stem_threshold_1", "stem_threshold_2",
                    "signal_output_0", "signal_output_1",
                    "signal_output_2", "signal_output_3"}};
    for (int parameter = 0; parameter < Gene::MAX_PARAMETRIC_VALUES_COUNT;
            ++parameter) {
        const std::uint32_t firstId =
                100U + static_cast<std::uint32_t>(parameter * 5);
        const std::string prefix = parameterNames[parameter];
        f32(firstId, prefix + "_variable_a",
                [parameter](GeneBuilder builder, float value) {
                    builder.setParametricVariableA(parameter, value);
                });
        f32(firstId + 1U, prefix + "_variable_b",
                [parameter](GeneBuilder builder, float value) {
                    builder.setParametricVariableB(parameter, value);
                });
        f32(firstId + 2U, prefix + "_variable_c",
                [parameter](GeneBuilder builder, float value) {
                    builder.setParametricVariableC(parameter, value);
                });
        u8(firstId + 3U, prefix + "_signal_channel",
                [parameter](GeneBuilder builder, std::uint8_t value) {
                    builder.setParametricSignalChannel(parameter, value);
                });
        u8(firstId + 4U, prefix + "_formula_type",
                [parameter](GeneBuilder builder, std::uint8_t value) {
                    builder.setParametricFormulaType(parameter, value);
                });
    }

    constexpr std::array<const char*, Gene::MAX_INTEGERS_COUNT> integerNames{{"virus_from_gene", "virus_to_gene", "smell_output", "smell_type",
            "secretion_type", "signal_output_channel_0",
            "signal_output_channel_1", "signal_output_channel_2",
            "signal_output_channel_3", "max_connection_count",
            "telomerase_length"}};
    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; ++index) {
        u16(200U + static_cast<std::uint32_t>(index), integerNames[index],
                [index](GeneBuilder builder, std::uint16_t value) {
                    builder.setIntegerValue(index, value);
                });
    }

    constexpr std::array<const char*, Gene::MAX_FLOATS_COUNT> floatNames{{"smell_amplitude", "smell_red", "smell_green", "smell_blue",
            "smell_threshold", "adhesin_length", "cytoskeleton"}};
    for (int index = 0; index < Gene::MAX_FLOATS_COUNT; ++index) {
        f32(300U + static_cast<std::uint32_t>(index), floatNames[index],
                [index](GeneBuilder builder, float value) {
                    builder.setFloatValue(
                            static_cast<Gene::FloatType>(index), value);
                });
    }
    return rows;
}

std::vector<GeneData> decodeReleaseGeneTable(
        const LogicalSection& genes) {
    if (genes.sectionId != section_id::kGenes
            || genes.canonicalName != "genes"
            || genes.schemaMajor != 1
            || genes.columns.size() != 87) {
        fail(DecodeErrorCode::InvalidValue,
                "Release gene table does not match the frozen schema");
    }
    const GeneColumns columns = bindGeneColumns(
            genes, genes.logicalRowCount);
    std::vector<GeneData> rows;
    rows.reserve(static_cast<std::size_t>(genes.logicalRowCount));
    for (std::size_t row = 0; row < genes.logicalRowCount; ++row) {
        rows.push_back(readGeneRow(columns, row));
    }
    return rows;
}

std::vector<std::uint8_t> encodeReleaseGenome(
        const GenomeData& genome,
        const EncodedPreview& preview,
        const BodyWriteOptions& options) {
    return encodeContainer(exportGenomeSnapshot(genome), options, preview).bytes;
}

GenomeData decodeReleaseGenome(const std::vector<std::uint8_t>& bytes) {
    const ReleaseGenomeInspection inspection = inspectHeaderAndMetadata(bytes);
    DecodedContainer decoded = decodeContainer(bytes);
    if (decoded.snapshot.documentKind != DocumentKind::Genome
            || decoded.snapshot.sections.size() != 2) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome has an invalid section inventory");
    }

    const LogicalSection& genes = requireSection(
            decoded.snapshot, section_id::kGenes, "genes", 87);
    const LogicalSection& genomes = requireSection(
            decoded.snapshot, section_id::kGenomes, "genomes", 3);
    const LogicalColumn& references = requireColumn(
            genomes, 2, PrimitiveType::U32, "gene_refs", std::nullopt);
    if (!isSupportedGeneCount(references.logicalValueCount)
            || inspection.geneCount != references.logicalValueCount
            || genomes.logicalRowCount != 1
            || genes.logicalRowCount == 0
            || genes.logicalRowCount > references.logicalValueCount) {
        fail(DecodeErrorCode::InvalidCount,
                "Release genome has invalid gene/genome row counts");
    }
    if (inspection.uniqueGeneCount != 0
            && inspection.uniqueGeneCount != genes.logicalRowCount) {
        fail(DecodeErrorCode::InvalidCount,
                "Release genome unique gene metadata does not match its body");
    }

    const LogicalColumn& offsets = requireColumn(
            genomes, 1, PrimitiveType::U64, "gene_ref_offsets", 2);
    const LogicalColumn& initial = requireColumn(
            genomes, 3, PrimitiveType::U16, "initial_gene", 1);
    if (readLittleEndian<std::uint64_t>(offsets, 0) != 0
            || readLittleEndian<std::uint64_t>(offsets, 1)
                    != references.logicalValueCount
            || readLittleEndian<std::uint16_t>(initial, 0)
                    != inspection.initialGeneIndex) {
        fail(DecodeErrorCode::InvalidCount,
                "Release genome CSR offsets or initial index are invalid");
    }

    std::vector<GeneData> rows = decodeReleaseGeneTable(genes);

    GenomeData genome(static_cast<std::size_t>(references.logicalValueCount));
    std::vector<bool> used(rows.size(), false);
    std::vector<std::optional<CanonicalGene>> canonicalRows(rows.size());
    for (std::size_t localIndex = 0; localIndex < genome.size(); ++localIndex) {
        const std::uint32_t row = readLittleEndian<std::uint32_t>(
                references, localIndex);
        if (row >= rows.size()) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release genome contains an out-of-range file gene reference");
        }
        CanonicalGeneResult canonical = canonicalizeGene(
                rows[row],
                GeneIndex(static_cast<GeneIndex::Value>(localIndex)),
                genome.size());
        if (!canonical.hasValue()) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release genome contains an invalid canonical gene row");
        }
        if (canonicalRows[row].has_value()
                && !canonicalGeneEqual(*canonicalRows[row], *canonical.value)) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release genome reuses an owner-dependent gene row");
        }
        if (!canonicalRows[row].has_value()) {
            canonicalRows[row].emplace(*canonical.value);
        }
        used[row] = true;
        const auto materialized = materializeCanonicalGene(
                *canonical.value,
                GeneIndex(static_cast<GeneIndex::Value>(localIndex)),
                genome.size(),
                localIndex == inspection.initialGeneIndex);
        if (!materialized.has_value()) {
            fail(DecodeErrorCode::InvalidValue,
                    "Release genome canonical gene could not be materialized");
        }
        genome[localIndex] = *materialized;
    }
    if (std::find(used.begin(), used.end(), false) != used.end()) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome contains an unreferenced unique gene row");
    }

    validateGenomeDraft(genome);
    const LogicalSnapshot canonical = exportGenomeSnapshot(genome);
    if (!sameLogicalSections(canonical.sections, decoded.snapshot.sections)) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome body is not in canonical deterministic form");
    }
    const GenomeCard card = readGenomeCard(decoded.snapshot.metadata);
    const GenomeExport canonicalExport = collectGenome(genome);
    const std::uint32_t computedCellTypes = cellTypeMask(canonicalExport);
    if (card.cellTypes.has_value() && *card.cellTypes != computedCellTypes) {
        fail(DecodeErrorCode::InvalidValue,
                "Release genome cell type metadata does not match its body");
    }
    return genome;
}

ReleaseGenomeInspection inspectReleaseGenome(
        const std::vector<std::uint8_t>& bytes) {
    return inspectHeaderAndMetadata(bytes);
}

} // namespace cellularity::save_format::release
