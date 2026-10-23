#include "formats/release/ReleaseSubstrateCodec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "core/CanonicalGene.h"
#include "core/CellData.h"
#include "core/CellTopology.h"
#include "core/CellType.h"
#include "core/SimulationWorld.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/GeneAccess.h"
#include "core/GenomePool.h"
#include "formats/DecodedWorldValidation.h"
#include "formats/SnapshotCodecSupport.h"
#include "formats/release/ReleaseGenomeCodec.h"
#include "io/DecodeError.h"
#include "io/Sha256.h"

namespace cellularity::save_format::release {
namespace {

static_assert(sizeof(float) == 4);
static_assert(sizeof(double) == 8);
static_assert(sizeof(std::int8_t) == 1);
static_assert(sizeof(int) == 4);

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
    } else if constexpr (std::is_same_v<Value, std::int32_t>) {
        appendUnsignedLittleEndian(bytes, static_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<Value, std::int64_t>) {
        appendUnsignedLittleEndian(bytes, static_cast<std::uint64_t>(value));
    } else if constexpr (std::is_same_v<Value, float>) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendUnsignedLittleEndian(bytes, bits);
    } else if constexpr (std::is_same_v<Value, double>) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendUnsignedLittleEndian(bytes, bits);
    } else {
        static_assert(!sizeof(Value), "Unsupported release scalar type");
    }
}

template <typename Value, typename Getter>
class ColumnDefinition final {
public:
    ColumnDefinition(std::uint32_t fieldId,
            PrimitiveType primitiveType,
            std::string canonicalName,
            std::uint64_t count,
            Getter getter)
            : fieldId_(fieldId),
              primitiveType_(primitiveType),
              canonicalName_(std::move(canonicalName)),
              count_(count),
              getter_(std::move(getter)) {
    }

    operator LogicalColumn() && {
        LogicalColumn column;
        column.fieldId = fieldId_;
        column.primitiveType = primitiveType_;
        column.canonicalName = std::move(canonicalName_);
        column.logicalValueCount = count_;
        if (count_ > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
            throw std::length_error("Release logical column is too large");
        }
        column.decodedBytes.reserve(
                static_cast<std::size_t>(count_) * sizeof(Value));
        for (std::uint64_t index = 0; index < count_; ++index) {
            appendLittleEndian<Value>(
                    column.decodedBytes,
                    getter_(static_cast<std::size_t>(index)));
        }
        return column;
    }

    operator StreamingLogicalColumn() && {
        StreamingLogicalColumn column;
        column.fieldId = fieldId_;
        column.primitiveType = primitiveType_;
        column.canonicalName = std::move(canonicalName_);
        column.logicalValueCount = count_;
        column.readRange = [getter = std::move(getter_)](
                                   std::uint64_t firstValue,
                                   std::uint64_t valueCount,
                                   std::vector<std::uint8_t>& destination) {
            if (valueCount
                    > std::numeric_limits<std::size_t>::max() / sizeof(Value)) {
                throw std::length_error(
                        "Release streamed column range is too large");
            }
            destination.clear();
            destination.reserve(
                    static_cast<std::size_t>(valueCount) * sizeof(Value));
            for (std::uint64_t offset = 0; offset < valueCount; ++offset) {
                appendLittleEndian<Value>(
                        destination,
                        getter(static_cast<std::size_t>(firstValue + offset)));
            }
        };
        return column;
    }

private:
    std::uint32_t fieldId_;
    PrimitiveType primitiveType_;
    std::string canonicalName_;
    std::uint64_t count_;
    Getter getter_;
};

template <typename Value, typename Getter>
auto makeColumn(std::uint32_t fieldId,
        PrimitiveType primitiveType,
        std::string canonicalName,
        std::size_t count,
        Getter&& getter) {
    return ColumnDefinition<Value, std::decay_t<Getter>>(
            fieldId,
            primitiveType,
            std::move(canonicalName),
            count,
            std::forward<Getter>(getter));
}

template <typename Value>
auto makeScalarColumn(std::uint32_t fieldId,
        PrimitiveType primitiveType,
        std::string canonicalName,
        Value value) {
    return makeColumn<Value>(fieldId,
            primitiveType,
            std::move(canonicalName),
            1,
            [value](std::size_t) {
                return value;
            });
}

std::uint32_t environmentBehaviorFlags(const Environment& environment) {
    return (environment.IsOnlyTweakMutations ? 1U << 0U : 0U)
            | (environment.mutationOnlyAfterSplit ? 1U << 1U : 0U)
            | (environment.aging ? 1U << 2U : 0U)
            | (environment.addRandom ? 1U << 3U : 0U)
            | (environment.dieEdge ? 1U << 4U : 0U)
            | (environment.mobileFood ? 1U << 5U : 0U);
}

std::uint32_t environmentGenePoolMask(const Environment& environment) {
    std::uint32_t result = 0;
    for (std::size_t index = 0; index < environment.mGenePool.size(); ++index) {
        if (environment.mGenePool[index]) {
            result |= 1U << static_cast<unsigned>(index);
        }
    }
    return result;
}

template <typename Section>
Section exportEnvironment(const Environment& environment) {
    Section section;
    section.sectionId = section_id::kEnvironment;
    section.canonicalName = "environment";
    section.logicalRowCount = 1;
    auto& columns = section.columns;
    columns.reserve(53);

    columns.push_back(makeScalarColumn<std::int64_t>(
            1, PrimitiveType::I64, "random_seed", environment.mRandom.getSeed()));
    columns.push_back(makeScalarColumn<double>(
            2, PrimitiveType::F64, "light_angle", environment.mLightAngle));
    columns.push_back(makeScalarColumn<std::uint32_t>(
            3, PrimitiveType::U32, "max_cell_count",
            static_cast<std::uint32_t>(environment.maxCellCount)));
    columns.push_back(makeScalarColumn<std::uint32_t>(
            4, PrimitiveType::U32, "max_food_count",
            static_cast<std::uint32_t>(environment.maxFoodCount)));
    columns.push_back(makeScalarColumn<std::uint32_t>(
            5, PrimitiveType::U32, "max_dead_splash_count",
            static_cast<std::uint32_t>(environment.maxDeadSplashCount)));
    columns.push_back(makeScalarColumn<std::uint32_t>(
            6, PrimitiveType::U32, "behavior_flags",
            environmentBehaviorFlags(environment)));
    columns.push_back(makeScalarColumn<std::uint32_t>(
            7, PrimitiveType::U32, "gene_pool_mask",
            environmentGenePoolMask(environment)));

    const auto addF64 = [&columns](std::uint32_t id,
                                const char* name,
                                double value) {
        columns.push_back(makeScalarColumn<double>(
                id, PrimitiveType::F64, name, value));
    };
    addF64(8, "radius", environment.mRadius);
    addF64(9, "food_speed", environment.mFoodSpeed);
    addF64(10, "food_size", environment.mFoodSize);
    addF64(11, "radiation", environment.mRadiation);
    addF64(12, "light_intensity", environment.mLightIntensity);
    addF64(13, "light_speed", environment.mLightSpeed);
    addF64(14, "light_range", environment.mLightRange);
    addF64(15, "viscosity", environment.mViscosity);
    addF64(16, "legacy_density", environment.mDensity);
    addF64(17, "legacy_gravity", environment.mGravity);
    addF64(18, "legacy_density_gradient", environment.mDensityGradient);
    addF64(19, "multicellular_coefficient", environment.mTestMulticellularCoef);
    addF64(20, "mutation_coefficient", environment.mTestMutationCoef);
    addF64(21, "food_decay", environment.mTestFoodDecay);
    addF64(22, "static_friction", environment.mFrictionStatic);
    addF64(23, "dynamic_friction", environment.mFrictionDynamic);
    addF64(24, "food_lumpiness", environment.mFoodLumpiness);
    addF64(25, "food_lump_size", environment.mFoodLumpSize);
    addF64(26, "nitrates", environment.mNitrates);
    addF64(27, "idle_metabolism_scale", environment.mTestIdleMetabolismScale);
    addF64(28, "flagellocyte_cost_scale", environment.mTestFlagellocyteCostScale);
    addF64(29, "phagocyte_eat_speed_scale",
            environment.mTestPhagocyteEatSpeedScale);
    columns.push_back(makeScalarColumn<float>(
            30, PrimitiveType::F32, "salinity", environment.mSalinity));
    columns.push_back(makeScalarColumn<float>(
            31, PrimitiveType::F32, "food_lipid_layer",
            environment.mFoodLipidLayer));
    columns.push_back(makeScalarColumn<std::uint32_t>(
            32, PrimitiveType::U32, "gravity_field_type",
            static_cast<std::uint32_t>(environment.mGravityFieldType)));
    addF64(33, "gravity_field_strength", environment.mGravityFieldStrength);
    addF64(34, "gravity_field_angle", environment.mGravityFieldAngle);
    columns.push_back(makeScalarColumn<std::uint32_t>(
            35, PrimitiveType::U32, "density_field_type",
            static_cast<std::uint32_t>(environment.mDensityFieldType)));
    addF64(36, "density_field_base", environment.mDensityFieldBase);
    addF64(37, "density_field_strength", environment.mDensityFieldStrength);
    addF64(38, "density_field_angle", environment.mDensityFieldAngle);
    addF64(39, "density_field_offset", environment.mDensityFieldOffset);
    addF64(40, "density_field_softness", environment.mDensityFieldSoftness);
    columns.push_back(makeScalarColumn<std::uint32_t>(
            41, PrimitiveType::U32, "salinity_field_type",
            static_cast<std::uint32_t>(environment.mSalinityFieldType)));
    addF64(42, "salinity_field_base", environment.mSalinityFieldBase);
    addF64(43, "salinity_field_strength", environment.mSalinityFieldStrength);
    addF64(44, "salinity_field_angle", environment.mSalinityFieldAngle);
    addF64(45, "salinity_field_offset", environment.mSalinityFieldOffset);
    addF64(46, "salinity_field_softness", environment.mSalinityFieldSoftness);
    columns.push_back(makeScalarColumn<std::uint32_t>(
            47, PrimitiveType::U32, "food_lipid_layer_field_type",
            static_cast<std::uint32_t>(environment.mFoodLipidLayerFieldType)));
    addF64(48, "food_lipid_layer_field_base",
            environment.mFoodLipidLayerFieldBase);
    addF64(49, "food_lipid_layer_field_strength",
            environment.mFoodLipidLayerFieldStrength);
    addF64(50, "food_lipid_layer_field_angle",
            environment.mFoodLipidLayerFieldAngle);
    addF64(51, "food_lipid_layer_field_offset",
            environment.mFoodLipidLayerFieldOffset);
    addF64(52, "food_lipid_layer_field_softness",
            environment.mFoodLipidLayerFieldSoftness);
    columns.push_back(makeColumn<double>(
            53,
            PrimitiveType::F64,
            "mutation_weights",
            environment.mMutationWeights.size(),
            [&environment](std::size_t index) {
                return environment.mMutationWeights[index];
            }));
    if (environment.mInnerRadius != 0.0) {
        addF64(54, "inner_radius", environment.mInnerRadius);
    }
    return section;
}

struct PoolExport final {
    std::vector<std::uint32_t> cellGenomeRefs;
    std::vector<std::uint64_t> genomeOffsets{0};
    std::vector<std::uint32_t> genomeGeneRefs;
    std::vector<std::uint16_t> genomeInitialGenes;
    std::vector<const GeneData*> genes;
};

std::string genomeIdentityKey(
        std::uint16_t initialGene,
        const std::vector<std::uint32_t>& references) {
    std::string key;
    key.reserve(sizeof(initialGene) + references.size() * sizeof(std::uint32_t));
    key.push_back(static_cast<char>(initialGene));
    key.push_back(static_cast<char>(initialGene >> 8U));
    for (std::uint32_t reference : references) {
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            key.push_back(static_cast<char>(reference >> shift));
        }
    }
    return key;
}

PoolExport exportPools(const CellData& cells,
        std::size_t cellCount) {
    PoolExport result;
    result.cellGenomeRefs.reserve(cellCount);

    const GenomePool& genomePool = cells.genomePool();
    const GenePool& genePool = cells.genePool();
    std::unordered_map<std::uint32_t, std::uint32_t> runtimeGeneToFile;
    std::unordered_map<std::uint32_t, std::uint32_t> runtimeGenomeToFile;
    std::unordered_map<std::string, std::uint32_t> identityToFile;

    for (std::size_t index = 0; index < cellCount; ++index) {
        const int cellIndex = static_cast<int>(index);
        const GenomeId runtimeGenome = cells.genomeId(cellIndex);
        const auto cached = runtimeGenomeToFile.find(runtimeGenome.value());
        if (cached != runtimeGenomeToFile.end()) {
            result.cellGenomeRefs.push_back(cached->second);
            continue;
        }

        const auto geneCount = genomePool.geneCount(runtimeGenome);
        const auto metadata = genomePool.metadata(runtimeGenome);
        if (!geneCount.has_value() || !metadata.has_value()
                || !isSupportedGeneCount(*geneCount)) {
            throw std::logic_error("Runtime genome pool is inconsistent");
        }

        std::vector<std::uint32_t> references;
        references.reserve(*geneCount);
        for (std::size_t localIndex = 0; localIndex < *geneCount; ++localIndex) {
            const auto runtimeGene = genomePool.geneId(
                    runtimeGenome,
                    GeneIndex(static_cast<GeneIndex::Value>(localIndex)));
            if (!runtimeGene.has_value()) {
                throw std::logic_error("Runtime genome has a missing gene reference");
            }
            const auto knownGene = runtimeGeneToFile.find(runtimeGene->value());
            if (knownGene != runtimeGeneToFile.end()) {
                references.push_back(knownGene->second);
                continue;
            }
            const GeneData* gene = genePool.dataPointer(*runtimeGene);
            if (gene == nullptr) {
                throw std::logic_error("Runtime gene pool is inconsistent");
            }
            if (result.genes.size()
                    >= static_cast<std::size_t>(
                            std::numeric_limits<std::uint32_t>::max())) {
                throw std::length_error("Release unique gene table is too large");
            }
            const std::uint32_t fileGene =
                    static_cast<std::uint32_t>(result.genes.size());
            runtimeGeneToFile.emplace(runtimeGene->value(), fileGene);
            result.genes.push_back(gene);
            references.push_back(fileGene);
        }

        const std::uint16_t initial = metadata->initialGeneIndex.value();
        const std::string identity = genomeIdentityKey(initial, references);
        const auto duplicate = identityToFile.find(identity);
        std::uint32_t fileGenome = 0;
        if (duplicate != identityToFile.end()) {
            fileGenome = duplicate->second;
        } else {
            if (result.genomeInitialGenes.size()
                    >= static_cast<std::size_t>(
                            std::numeric_limits<std::uint32_t>::max())) {
                throw std::length_error("Release unique genome table is too large");
            }
            fileGenome = static_cast<std::uint32_t>(
                    result.genomeInitialGenes.size());
            identityToFile.emplace(identity, fileGenome);
            result.genomeGeneRefs.insert(
                    result.genomeGeneRefs.end(),
                    references.begin(),
                    references.end());
            result.genomeOffsets.push_back(result.genomeGeneRefs.size());
            result.genomeInitialGenes.push_back(initial);
        }
        runtimeGenomeToFile.emplace(runtimeGenome.value(), fileGenome);
        result.cellGenomeRefs.push_back(fileGenome);
    }
    return result;
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

template <typename Section>
Section exportGenes(const PoolExport& pools) {
    Section section;
    section.sectionId = section_id::kGenes;
    section.canonicalName = "genes";
    section.logicalRowCount = pools.genes.size();
    auto& columns = section.columns;
    columns.reserve(87);
    const std::size_t count = pools.genes.size();
    const auto view = [&pools](std::size_t index) {
        return GeneView(*pools.genes[index]);
    };
    const auto addF32 = [&](std::uint32_t id,
                                const char* name,
                                auto getter) {
        columns.push_back(makeColumn<float>(
                id, PrimitiveType::F32, name, count,
                [getter, view](std::size_t index) {
                    return getter(view(index));
                }));
    };
    const auto addU16 = [&](std::uint32_t id,
                                const char* name,
                                auto getter) {
        columns.push_back(makeColumn<std::uint16_t>(
                id, PrimitiveType::U16, name, count,
                [getter, view](std::size_t index) {
                    return static_cast<std::uint16_t>(getter(view(index)));
                }));
    };
    const auto addU8 = [&](std::uint32_t id,
                               const char* name,
                               auto getter) {
        columns.push_back(makeColumn<std::uint8_t>(
                id, PrimitiveType::U8, name, count,
                [getter, view](std::size_t index) {
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
        const std::uint32_t firstId = 100U + static_cast<std::uint32_t>(parameter * 5);
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

template <typename Section>
Section exportGenomes(const PoolExport& pools) {
    Section section;
    section.sectionId = section_id::kGenomes;
    section.canonicalName = "genomes";
    section.logicalRowCount = pools.genomeInitialGenes.size();
    section.columns.push_back(makeColumn<std::uint64_t>(
            1, PrimitiveType::U64, "gene_ref_offsets", pools.genomeOffsets.size(),
            [&pools](std::size_t index) {
                return pools.genomeOffsets[index];
            }));
    section.columns.push_back(makeColumn<std::uint32_t>(
            2, PrimitiveType::U32, "gene_refs", pools.genomeGeneRefs.size(),
            [&pools](std::size_t index) {
                return pools.genomeGeneRefs[index];
            }));
    section.columns.push_back(makeColumn<std::uint16_t>(
            3, PrimitiveType::U16, "initial_gene", pools.genomeInitialGenes.size(),
            [&pools](std::size_t index) {
                return pools.genomeInitialGenes[index];
            }));
    return section;
}

std::uint8_t cellFlags(const CellData& cells, std::size_t index) {
    return static_cast<std::uint8_t>(
            (cells.mIsDead[index] ? 1U << 0U : 0U)
            | (cells.mIsMirrored[index] ? 1U << 1U : 0U)
            | (cells.mIsStuck[index] ? 1U << 2U : 0U));
}

template <typename Section>
Section exportCells(const CellData& cells, const PoolExport& pools) {
    Section section;
    section.sectionId = section_id::kCells;
    section.schemaMinor = 1;
    section.canonicalName = "cells";
    section.logicalRowCount = static_cast<std::uint64_t>(cells.mCellCount);
    const std::size_t count = static_cast<std::size_t>(cells.mCellCount);
    auto& columns = section.columns;
    columns.reserve(61);
    const auto addF64 = [&](std::uint32_t id, const char* name, const auto& values) {
        columns.push_back(makeColumn<double>(
                id, PrimitiveType::F64, name, count,
                [&values](std::size_t index) {
                    return values[index];
                }));
    };
    const auto addF32 = [&](std::uint32_t id, const char* name, const auto& values) {
        columns.push_back(makeColumn<float>(
                id, PrimitiveType::F32, name, count,
                [&values](std::size_t index) {
                    return values[index];
                }));
    };
    const auto addI32 = [&](std::uint32_t id, const char* name, const auto& values) {
        columns.push_back(makeColumn<std::int32_t>(
                id, PrimitiveType::I32, name, count,
                [&values](std::size_t index) {
                    return static_cast<std::int32_t>(values[index]);
                }));
    };
    const auto addU64 = [&](std::uint32_t id, const char* name, const auto& values) {
        columns.push_back(makeColumn<std::uint64_t>(
                id, PrimitiveType::U64, name, count,
                [&values](std::size_t index) {
                    return static_cast<std::uint64_t>(values[index]);
                }));
    };

    addF64(1, "position_x", cells.mPositionX);
    addF64(2, "position_y", cells.mPositionY);
    addF64(3, "position_z", cells.mPositionZ);
    addF64(4, "radius", cells.mRadius);
    addF64(5, "velocity_x", cells.mVelocityX);
    addF64(6, "velocity_y", cells.mVelocityY);
    addF64(7, "force_x", cells.mForceX);
    addF64(8, "force_y", cells.mForceY);
    addF64(9, "force_z", cells.mForceZ);
    addF64(10, "torque", cells.mTorque);
    addF64(11, "angle", cells.mAngle);
    addF64(12, "angle_cytoplasm", cells.mAngleCytoplasm);
    addF64(13, "angular_velocity", cells.mAngularVelocity);
    addF64(14, "angular_velocity_cytoplasm", cells.mAngularVelocityCytoplasm);
    addF64(15, "age", cells.mAge);
    addF64(16, "nitrates", cells.mNitrates);
    addF64(17, "hair_rotation", cells.mHairRotation);
    addF64(18, "hair_x", cells.hairX);
    addF64(19, "hair_y", cells.hairY);
    addF64(20, "mass", cells.mMass);
    addF32(21, "swim_animation_time", cells.mSwimAnimationTime);
    addF32(22, "swim_animation_time_2", cells.mSwimAnimationTime2);
    columns.push_back(makeColumn<std::uint32_t>(
            23, PrimitiveType::U32, "genome_ref", pools.cellGenomeRefs.size(),
            [&pools](std::size_t index) {
                return pools.cellGenomeRefs[index];
            }));
    columns.push_back(makeColumn<std::uint16_t>(
            24, PrimitiveType::U16, "active_gene_index", count,
            [&cells](std::size_t index) {
                return cells.activeGeneIndex(static_cast<int>(index)).value();
            }));
    columns.push_back(makeColumn<std::uint8_t>(
            25, PrimitiveType::U8, "flags", count,
            [&cells](std::size_t index) {
                return cellFlags(cells, index);
            }));
    addI32(27, "origin_tag", cells.mOriginTag);
    addI32(28, "select_tag", cells.mSelectTag);
    addI32(29, "mutation_count", cells.mMutationCount);
    addI32(30, "pending_mutation_count", cells.mPendingMutationCount);
    addI32(31, "seldom_counter", cells.mSeldomCounter);
    addI32(32, "old_link_count", cells.mOldLinkCount);
    addI32(33, "telomeres_count", cells.mTelomeresCount);
    addF32(34, "lipids", cells.mLipids);
    for (int channel = 0; channel < 4; ++channel) {
        columns.push_back(makeColumn<float>(
                35U + static_cast<std::uint32_t>(channel),
                PrimitiveType::F32,
                std::string("color_") + "rgba"[channel],
                count,
                [&cells, channel](std::size_t index) {
                    return cells.mColor[index][channel];
                }));
    }
    addU64(39, "organism_id", cells.mOrganismIds);
    addU64(40, "parent_organism_id", cells.mParentOrganismIds);
    for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT * 2; ++signal) {
        columns.push_back(makeColumn<float>(
                100U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "signal_substance_" + std::to_string(signal),
                count,
                [&cells, signal](std::size_t index) {
                    return cells.mSignalSubstance[index][signal];
                }));
    }
    for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT; ++signal) {
        columns.push_back(makeColumn<float>(
                110U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "new_signal_substance_" + std::to_string(signal),
                count,
                [&cells, signal](std::size_t index) {
                    return cells.mNewSignalSubstance[index][signal];
                }));
    }
    for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT; ++signal) {
        columns.push_back(makeColumn<float>(
                120U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "signal_substance_out_" + std::to_string(signal),
                count,
                [&cells, signal](std::size_t index) {
                    return cells.mSignalSubstanceOut[index][signal];
                }));
    }
    addF32(130, "toxin", cells.mToxin);
    addF32(131, "toxication", cells.mToxication);
    addF32(132, "new_toxin", cells.mNewToxin);
    addF32(133, "myocyte_contraction", cells.mMyocyteContraction);
    addF32(134, "myocyte_bending", cells.mMyocyteBending);
    addF32(135, "myocyte_lifting", cells.mMyocyteLifting);
    return section;
}

template <typename Section>
Section exportTopologyEdges(const CellTopology& topology) {
    Section section;
    section.sectionId = section_id::kTopologyEdges;
    section.schemaMinor = 1;
    section.canonicalName = "topology_edges";
    section.logicalRowCount = static_cast<std::uint64_t>(topology.edgeCount());
    const std::size_t count = static_cast<std::size_t>(topology.edgeCount());
    const CellTopology::EdgeView edges = topology.edgeView();
    const CellTopology::RenderEdgeView render = topology.renderEdgeView();
    section.columns.push_back(makeColumn<std::uint32_t>(
            1, PrimitiveType::U32, "cell_a", count,
            [edges](std::size_t index) {
                return static_cast<std::uint32_t>(edges.cellA[index]);
            }));
    section.columns.push_back(makeColumn<std::uint32_t>(
            2, PrimitiveType::U32, "cell_b", count,
            [edges](std::size_t index) {
                return static_cast<std::uint32_t>(edges.cellB[index]);
            }));
    section.columns.push_back(makeColumn<double>(
            3, PrimitiveType::F64, "angle_a", count,
            [edges](std::size_t index) {
                return edges.angleA[index];
            }));
    section.columns.push_back(makeColumn<double>(
            4, PrimitiveType::F64, "angle_b", count,
            [edges](std::size_t index) {
                return edges.angleB[index];
            }));
    section.columns.push_back(makeColumn<float>(
            5, PrimitiveType::F32, "torsional_stiffness", count,
            [edges](std::size_t index) {
                return edges.torsionalStiffness[index];
            }));
    section.columns.push_back(makeColumn<float>(
            6, PrimitiveType::F32, "rest_length", count,
            [edges](std::size_t index) {
                return edges.restLength[index];
            }));
    constexpr std::array<const char*, 4> names{{"render_start_offset_a_x", "render_start_offset_a_y",
            "render_end_offset_a_x", "render_end_offset_a_y"}};
    const std::array<const float*, 4> values{{render.renderStartOffsetAX, render.renderStartOffsetAY,
            render.renderEndOffsetAX, render.renderEndOffsetAY}};
    for (std::size_t component = 0; component < values.size(); ++component) {
        section.columns.push_back(makeColumn<float>(
                7U + static_cast<std::uint32_t>(component),
                PrimitiveType::F32,
                names[component],
                count,
                [values, component](std::size_t index) {
                    return values[component][index];
                }));
    }
    section.columns.push_back(makeColumn<std::uint8_t>(
            11,
            PrimitiveType::U8,
            "creation_reason",
            count,
            [edges](std::size_t index) {
                return static_cast<std::uint8_t>(
                        edges.creationReason[index]);
            }));
    return section;
}

template <typename Section>
Section exportTopologyBoundaries(const CellTopology& topology) {
    Section section;
    section.sectionId = section_id::kTopologyBoundaries;
    section.canonicalName = "topology_boundaries";
    section.logicalRowCount = static_cast<std::uint64_t>(topology.boundaryCount());
    const std::size_t count = static_cast<std::size_t>(topology.boundaryCount());
    const CellTopology::BoundaryView boundaries = topology.boundaryView();
    section.columns.push_back(makeColumn<std::uint32_t>(
            1, PrimitiveType::U32, "cell", count,
            [boundaries](std::size_t index) {
                return static_cast<std::uint32_t>(boundaries.cell[index]);
            }));
    section.columns.push_back(makeColumn<double>(
            2, PrimitiveType::F64, "angle", count,
            [boundaries](std::size_t index) {
                return boundaries.angleA[index];
            }));
    section.columns.push_back(makeColumn<double>(
            3, PrimitiveType::F64, "anchor_x", count,
            [boundaries](std::size_t index) {
                return boundaries.anchorX[index];
            }));
    section.columns.push_back(makeColumn<double>(
            4, PrimitiveType::F64, "anchor_y", count,
            [boundaries](std::size_t index) {
                return boundaries.anchorY[index];
            }));
    section.columns.push_back(makeColumn<float>(
            5, PrimitiveType::F32, "torsional_stiffness", count,
            [boundaries](std::size_t index) {
                return boundaries.torsionalStiffness[index];
            }));
    section.columns.push_back(makeColumn<float>(
            6, PrimitiveType::F32, "rest_length", count,
            [boundaries](std::size_t index) {
                return boundaries.restLength[index];
            }));
    section.columns.push_back(makeColumn<float>(
            7, PrimitiveType::F32, "render_start_offset_x", count,
            [&topology](std::size_t index) {
                return topology.boundaryRenderStartOffsetX(
                        static_cast<int>(index));
            }));
    section.columns.push_back(makeColumn<float>(
            8, PrimitiveType::F32, "render_start_offset_y", count,
            [&topology](std::size_t index) {
                return topology.boundaryRenderStartOffsetY(
                        static_cast<int>(index));
            }));
    return section;
}

template <typename Section>
Section exportFood(const FoodData& food) {
    Section section;
    section.sectionId = section_id::kFood;
    section.canonicalName = "food";
    section.logicalRowCount = static_cast<std::uint64_t>(food.mFoodCount);
    const std::size_t count = static_cast<std::size_t>(food.mFoodCount);
    const std::array<const char*, 6> names{{"position_x", "position_y", "mass", "velocity_x", "velocity_y",
            "lipid_layer"}};
    const std::array<const std::vector<float>*, 6> values{{&food.mPositionX, &food.mPositionY, &food.mMass,
            &food.mVelocityX, &food.mVelocityY, &food.mLipidLayer}};
    for (std::size_t field = 0; field < values.size(); ++field) {
        section.columns.push_back(makeColumn<float>(
                static_cast<std::uint32_t>(field + 1U),
                PrimitiveType::F32,
                names[field],
                count,
                [values, field](std::size_t index) {
                    return (*values[field])[index];
                }));
    }
    return section;
}

template <typename Section>
Section exportDeadSplashes(const DeadSplashData& splashes) {
    Section section;
    section.sectionId = section_id::kDeadSplashes;
    section.canonicalName = "dead_splashes";
    section.logicalRowCount = static_cast<std::uint64_t>(splashes.mDeadSplashCount);
    const std::size_t count = static_cast<std::size_t>(splashes.mDeadSplashCount);
    const std::array<const char*, 4> scalarNames{{"position_x", "position_y", "radius", "age"}};
    const std::array<const std::vector<float>*, 4> scalarValues{{&splashes.mPositionX, &splashes.mPositionY,
            &splashes.mRadius, &splashes.mAge}};
    for (std::size_t field = 0; field < scalarValues.size(); ++field) {
        section.columns.push_back(makeColumn<float>(
                static_cast<std::uint32_t>(field + 1U),
                PrimitiveType::F32,
                scalarNames[field],
                count,
                [scalarValues, field](std::size_t index) {
                    return (*scalarValues[field])[index];
                }));
    }
    for (int channel = 0; channel < 4; ++channel) {
        section.columns.push_back(makeColumn<float>(
                5U + static_cast<std::uint32_t>(channel),
                PrimitiveType::F32,
                std::string("color_") + "rgba"[channel],
                count,
                [&splashes, channel](std::size_t index) {
                    return splashes.mColor[index][channel];
                }));
    }
    return section;
}

std::vector<MetadataEntry> substrateMetadata(const Environment& environment,
        const CellData& cells,
        const FoodData& food,
        const DeadSplashData& deadSplashes,
        double simulationTimeSeconds,
        const PoolExport& pools,
        std::int64_t createdAtUnixMillis) {
    if (!cells.validateOrganismIdentities()) {
        throw std::invalid_argument(
                "Cannot save invalid Cell Organism identities");
    }
    const CellData::OrganismIdentitySummary organismIdentity =
            cells.organismIdentitySummary();
    std::vector<MetadataEntry> result{
            MetadataEntry::u32(metadata_field::kContentSchemaMajor,
                    "content_schema_major",
                    kSubstrateContentSchemaMajor,
                    true),
            MetadataEntry::u32(metadata_field::kContentSchemaMinor,
                    "content_schema_minor",
                    kSubstrateContentSchemaMinor,
                    true),
            MetadataEntry::f64(metadata_field::kSubstrateSimulationTimeHours,
                    "simulation_time_hours",
                    simulationTimeSeconds / 3600.0,
                    true),
            MetadataEntry::u64(metadata_field::kSubstrateCellCount,
                    "cell_count", cells.mCellCount, true),
            MetadataEntry::u64(metadata_field::kSubstrateFoodCount,
                    "food_count", food.mFoodCount, true),
            MetadataEntry::f64(metadata_field::kSubstrateFoodMass,
                    "food_mass", food.getFoodMass(), true),
            MetadataEntry::f64(metadata_field::kSubstrateDishRadius,
                    "dish_radius", environment.mRadius, true),
            MetadataEntry::u64(metadata_field::kSubstrateMaximumCellCount,
                    "max_cell_count", environment.maxCellCount,
                    true),
            MetadataEntry::u64(metadata_field::kSubstrateMaximumFoodCount,
                    "max_food_count", environment.maxFoodCount,
                    true),
            MetadataEntry::u64(metadata_field::kSubstrateGenomeCount,
                    "genome_count", pools.genomeInitialGenes.size(),
                    false),
            MetadataEntry::u64(metadata_field::kSubstrateUniqueGeneCount,
                    "unique_gene_count", pools.genes.size(), false),
            MetadataEntry::u64(metadata_field::kSubstrateTopologyEdgeCount,
                    "topology_edge_count",
                    cells.mTopology.edgeCount(), false),
            MetadataEntry::u64(metadata_field::kSubstrateDeadSplashCount,
                    "dead_splash_count",
                    deadSplashes.mDeadSplashCount, false),
            MetadataEntry::u64(metadata_field::kSubstrateKnownOrganismCount,
                    "known_organism_count",
                    organismIdentity.knownOrganismCount, true),
            MetadataEntry::u32(
                    metadata_field::kSubstrateOrganismIdentityComplete,
                    "organism_identity_complete",
                    organismIdentity.complete ? 1U : 0U,
                    true),
            MetadataEntry::u64(metadata_field::kSubstrateNextOrganismId,
                    "next_organism_id",
                    cells.nextOrganismId(), true),
    };
    if (createdAtUnixMillis > 0) {
        result.push_back(MetadataEntry::i64(
                metadata_field::kSubstrateCreatedAtUnixMillis,
                "created_at_unix_millis",
                createdAtUnixMillis,
                false));
    }
    if (environment.mInnerRadius != 0.0) {
        result.push_back(MetadataEntry::f64(
                metadata_field::kSubstrateDishInnerRadius,
                "dish_inner_radius", environment.mInnerRadius, false));
    }
    return result;
}

std::uint64_t decodedBodyBytes(const std::vector<LogicalSection>& sections) {
    std::uint64_t result = 0;
    for (const LogicalSection& section : sections) {
        for (const LogicalColumn& column : section.columns) {
            if (column.decodedBytes.size()
                    > std::numeric_limits<std::uint64_t>::max() - result) {
                throw std::length_error("Release decoded body is too large");
            }
            result += column.decodedBytes.size();
        }
    }
    return result;
}

} // namespace

LogicalSnapshot exportSubstrateRuntimeSoa(const SimulationWorld& world) {
    return exportSubstrateRuntimeSoa(
            world.mEnvironment,
            world.mCellData,
            world.mFoodData,
            world.mDeadSplashData,
            world.mTime,
            world.mCreatedAtUnixMillis);
}

LogicalSnapshot exportSubstrateRuntimeSoa(
        const Environment& environment,
        const CellData& cells,
        const FoodData& food,
        const DeadSplashData& deadSplashes,
        double simulationTimeSeconds,
        std::int64_t createdAtUnixMillis) {
    const GenomePoolDiagnostics before = cells.genomePool().diagnostics();
    const std::size_t cellCount = static_cast<std::size_t>(cells.mCellCount);
    PoolExport pools = exportPools(cells, cellCount);

    LogicalSnapshot result;
    result.documentKind = DocumentKind::Substrate;
    result.metadata = substrateMetadata(
            environment, cells, food, deadSplashes,
            simulationTimeSeconds, pools, createdAtUnixMillis);
    result.sections.reserve(8);
    result.sections.push_back(exportEnvironment<LogicalSection>(environment));
    result.sections.push_back(exportGenes<LogicalSection>(pools));
    result.sections.push_back(exportGenomes<LogicalSection>(pools));
    result.sections.push_back(exportCells<LogicalSection>(cells, pools));
    result.sections.push_back(
            exportTopologyEdges<LogicalSection>(cells.mTopology));
    result.sections.push_back(
            exportTopologyBoundaries<LogicalSection>(cells.mTopology));
    result.sections.push_back(exportFood<LogicalSection>(food));
    result.sections.push_back(exportDeadSplashes<LogicalSection>(deadSplashes));

    const GenomePoolDiagnostics after = cells.genomePool().diagnostics();
    if (after.materializedGenomes != before.materializedGenomes) {
        throw std::logic_error(
                "Release SoA export materialized a per-cell genome");
    }
    result.diagnostics.cellCount = cellCount;
    result.diagnostics.uniqueGenomeCount = pools.genomeInitialGenes.size();
    result.diagnostics.uniqueGeneCount = pools.genes.size();
    result.diagnostics.totalGeneReferences = pools.genomeGeneRefs.size();
    result.diagnostics.canonicalDecodedBodyBytes = decodedBodyBytes(result.sections);
    for (std::size_t index = 0; index < cellCount; ++index) {
        const int cellIndex = static_cast<int>(index);
        const std::size_t geneCount = cells.geneCount(cellIndex);
        const std::uint64_t bytes = static_cast<std::uint64_t>(geneCount)
                * sizeof(GeneData);
        if (bytes > std::numeric_limits<std::uint64_t>::max()
                        - result.diagnostics.avoidedPerCellGeneDataBytes) {
            throw std::length_error("Release avoided byte count overflow");
        }
        result.diagnostics.avoidedPerCellGeneDataBytes += bytes;
    }
    return result;
}

} // namespace cellularity::save_format::release

namespace cellularity::save_format::release {
namespace {

constexpr std::uint32_t kKnownEnvironmentBehaviorFlags = 0x3fU;
constexpr std::uint8_t kKnownCellFlags = 0x07U;

[[noreturn]] void failReleaseSubstrate(
        DecodeErrorCode code,
        const std::string& message) {
    throw DecodeError(code, message);
}

template <typename Value>
Value readLittleEndianValue(
        const LogicalColumn& column,
        std::size_t index) {
    const std::size_t width = sizeof(Value);
    if (index >= column.logicalValueCount
            || index > std::numeric_limits<std::size_t>::max() / width
            || index * width > column.decodedBytes.size()
            || column.decodedBytes.size() - index * width < width) {
        failReleaseSubstrate(
                DecodeErrorCode::TruncatedInput,
                "Release substrate column value is truncated");
    }
    static_assert(
            sizeof(Value) == 1 || sizeof(Value) == 2
                    || sizeof(Value) == 4 || sizeof(Value) == 8,
            "Unsupported release scalar width");
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
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    } else {
        return bits;
    }
}

template <typename Value>
Value readLittleEndianValue(const std::uint8_t* bytes) {
    static_assert(
            sizeof(Value) == 1 || sizeof(Value) == 2
                    || sizeof(Value) == 4 || sizeof(Value) == 8,
            "Unsupported release scalar width");
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

const SectionDescriptor& requireFileSection(
        const std::vector<SectionDescriptor>& sections,
        std::uint32_t id,
        const char* name,
        std::size_t exactColumnCount,
        std::uint16_t expectedSchemaMinor = 0) {
    const auto found = std::find_if(
            sections.begin(), sections.end(),
            [id](const SectionDescriptor& section) {
                return section.sectionId == id;
            });
    if (found == sections.end() || found->canonicalName != name
            || found->schemaMajor != 1
            || found->schemaMinor != expectedSchemaMinor
            || found->sectionFlags != kDescriptorCriticalFlag
            || (found->columns.size() != exactColumnCount
                    && !(id == section_id::kEnvironment
                            && found->columns.size() == exactColumnCount + 1
                            && std::any_of(found->columns.begin(), found->columns.end(), [](const ColumnDescriptor& column) {
                                   return column.fieldId == 54;
                               })))) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                std::string("Release substrate section does not match schema: ")
                        + name);
    }
    return *found;
}

ReleaseColumnLoader fileColumnLoader(
        ContainerFileReader& reader,
        const SectionDescriptor& section) {
    return [&reader, &section](
                   std::uint32_t fieldId,
                   PrimitiveType primitiveType,
                   const std::string& canonicalName,
                   std::optional<std::uint64_t> exactValueCount,
                   const DecodedChunkConsumer& consumer) {
        const auto found = std::find_if(
                section.columns.begin(), section.columns.end(),
                [fieldId](const ColumnDescriptor& column) {
                    return column.fieldId == fieldId;
                });
        if (found == section.columns.end()
                || found->primitiveType
                        != static_cast<std::uint8_t>(primitiveType)
                || found->physicalEncoding
                        > static_cast<std::uint8_t>(PhysicalEncoding::Constant)
                || found->fieldFlags != kDescriptorCriticalFlag
                || found->canonicalName != canonicalName
                || (exactValueCount.has_value()
                        && found->logicalValueCount != *exactValueCount)) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release substrate column does not match schema: "
                            + section.canonicalName + "." + canonicalName);
        }
        reader.decodeColumn(*found, consumer);
    };
}

template <typename Value, typename Setter>
void loadFileColumn(
        const ReleaseColumnLoader& loader,
        std::uint32_t fieldId,
        PrimitiveType primitiveType,
        const std::string& canonicalName,
        std::uint64_t valueCount,
        Setter&& setter) {
    std::uint64_t nextValue = 0;
    loader(
            fieldId,
            primitiveType,
            canonicalName,
            valueCount,
            [&](std::uint64_t firstValue,
                    std::uint64_t chunkValueCount,
                    const std::uint8_t* bytes,
                    std::size_t byteCount) {
                if (firstValue != nextValue
                        || chunkValueCount
                                > std::numeric_limits<std::size_t>::max()
                                        / sizeof(Value)
                        || byteCount
                                != static_cast<std::size_t>(chunkValueCount)
                                        * sizeof(Value)) {
                    failReleaseSubstrate(
                            DecodeErrorCode::InvalidLength,
                            "Release substrate column chunks are inconsistent");
                }
                for (std::uint64_t offset = 0;
                        offset < chunkValueCount;
                        ++offset) {
                    setter(
                            static_cast<std::size_t>(firstValue + offset),
                            readLittleEndianValue<Value>(
                                    bytes + static_cast<std::size_t>(offset) * sizeof(Value)));
                }
                nextValue += chunkValueCount;
            });
    if (nextValue != valueCount) {
        failReleaseSubstrate(
                DecodeErrorCode::TruncatedInput,
                "Release substrate column is incomplete");
    }
}

LogicalSection materializeSmallFileSection(
        ContainerFileReader& reader,
        const SectionDescriptor& descriptor) {
    LogicalSection section;
    section.sectionId = descriptor.sectionId;
    section.schemaMajor = descriptor.schemaMajor;
    section.schemaMinor = descriptor.schemaMinor;
    section.canonicalName = descriptor.canonicalName;
    section.logicalRowCount = descriptor.logicalRowCount;
    section.columns.reserve(descriptor.columns.size());
    for (const ColumnDescriptor& source : descriptor.columns) {
        LogicalColumn column;
        column.fieldId = source.fieldId;
        column.primitiveType = static_cast<PrimitiveType>(source.primitiveType);
        column.canonicalName = source.canonicalName;
        column.logicalValueCount = source.logicalValueCount;
        reader.decodeColumn(
                source,
                [&](std::uint64_t,
                        std::uint64_t,
                        const std::uint8_t* bytes,
                        std::size_t byteCount) {
                    column.decodedBytes.insert(
                            column.decodedBytes.end(), bytes, bytes + byteCount);
                });
        section.columns.push_back(std::move(column));
    }
    return section;
}

const LogicalSection& requireReleaseSection(
        const LogicalSnapshot& snapshot,
        std::uint32_t id,
        const char* name,
        std::size_t exactColumnCount,
        std::uint16_t expectedSchemaMinor = 0) {
    const LogicalSection* section = findLogicalSection(snapshot, id);
    if (section == nullptr || section->canonicalName != name
            || section->schemaMajor != 1
            || section->schemaMinor != expectedSchemaMinor
            || (section->columns.size() != exactColumnCount
                    && !(id == section_id::kEnvironment
                            && section->columns.size() == exactColumnCount + 1
                            && findLogicalColumn(*section, 54) != nullptr))) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                std::string("Release substrate section does not match schema: ")
                        + name);
    }
    return *section;
}

const LogicalColumn& requireReleaseColumn(
        const LogicalSection& section,
        std::uint32_t id,
        PrimitiveType type,
        const std::string& name,
        std::optional<std::uint64_t> exactValueCount) {
    const LogicalColumn* column = findLogicalColumn(section, id);
    if (column == nullptr || column->primitiveType != type
            || column->canonicalName != name
            || (exactValueCount.has_value()
                    && column->logicalValueCount != *exactValueCount)) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                std::string("Release substrate column does not match schema: ")
                        + section.canonicalName + "." + name);
    }
    return *column;
}

template <typename Value>
Value readScalar(
        const LogicalSection& section,
        std::uint32_t id,
        PrimitiveType type,
        const char* name) {
    return readLittleEndianValue<Value>(
            requireReleaseColumn(section, id, type, name, 1), 0);
}

const MetadataEntry& requireSubstrateMetadata(
        const std::vector<MetadataEntry>& metadata,
        std::uint32_t fieldId) {
    const auto found = std::find_if(
            metadata.begin(), metadata.end(),
            [fieldId](const MetadataEntry& entry) {
                return entry.fieldId == fieldId;
            });
    if (found == metadata.end()) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release substrate is missing required metadata");
    }
    return *found;
}

std::optional<std::uint64_t> optionalSubstrateU64(
        const std::vector<MetadataEntry>& metadata,
        std::uint32_t fieldId) {
    const auto found = std::find_if(
            metadata.begin(), metadata.end(),
            [fieldId](const MetadataEntry& entry) {
                return entry.fieldId == fieldId;
            });
    return found == metadata.end()
            ? std::nullopt
            : std::optional<std::uint64_t>(found->asU64());
}

std::optional<std::int64_t> optionalSubstrateI64(
        const std::vector<MetadataEntry>& metadata,
        std::uint32_t fieldId) {
    const auto found = std::find_if(
            metadata.begin(), metadata.end(),
            [fieldId](const MetadataEntry& entry) {
                return entry.fieldId == fieldId;
            });
    return found == metadata.end()
            ? std::nullopt
            : std::optional<std::int64_t>(found->asI64());
}

struct SubstrateCard final {
    std::uint32_t schemaMajor = 0;
    std::uint32_t schemaMinor = 0;
    double simulationTimeHours = 0.0;
    std::uint64_t cellCount = 0;
    std::uint64_t foodCount = 0;
    double foodMass = 0.0;
    double dishRadius = 0.0;
    double dishInnerRadius = 0.0;
    std::uint64_t maximumCellCount = 0;
    std::uint64_t maximumFoodCount = 0;
    std::optional<std::uint64_t> genomeCount;
    std::optional<std::uint64_t> uniqueGeneCount;
    std::optional<std::uint64_t> topologyEdgeCount;
    std::optional<std::uint64_t> deadSplashCount;
    std::uint64_t knownOrganismCount = 0;
    bool organismIdentityComplete = false;
    OrganismId nextOrganismId = 1;
    std::optional<std::int64_t> createdAtUnixMillis;
};

SubstrateCard readSubstrateCard(
        const std::vector<MetadataEntry>& metadata,
        bool requireSupportedSchema) {
    SubstrateCard card;
    card.schemaMajor = requireSubstrateMetadata(
            metadata, metadata_field::kContentSchemaMajor)
                               .asU32();
    card.schemaMinor = requireSubstrateMetadata(
            metadata, metadata_field::kContentSchemaMinor)
                               .asU32();
    if (card.schemaMajor == 0) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidVersion,
                "Release substrate content schema major version is zero");
    }
    if (requireSupportedSchema
            && (card.schemaMajor != kSubstrateContentSchemaMajor
                    || card.schemaMinor != kSubstrateContentSchemaMinor)) {
        failReleaseSubstrate(
                DecodeErrorCode::NewerSnapshotVersion,
                "Release substrate content schema is unsupported");
    }
    card.simulationTimeHours = requireSubstrateMetadata(
            metadata,
            metadata_field::kSubstrateSimulationTimeHours)
                                       .asF64();
    card.cellCount = requireSubstrateMetadata(
            metadata, metadata_field::kSubstrateCellCount)
                             .asU64();
    card.foodCount = requireSubstrateMetadata(
            metadata, metadata_field::kSubstrateFoodCount)
                             .asU64();
    card.foodMass = requireSubstrateMetadata(
            metadata, metadata_field::kSubstrateFoodMass)
                            .asF64();
    card.dishRadius = requireSubstrateMetadata(
            metadata, metadata_field::kSubstrateDishRadius)
                              .asF64();
    const auto innerRadius = std::find_if(metadata.begin(), metadata.end(), [](const MetadataEntry& entry) {
        return entry.fieldId == metadata_field::kSubstrateDishInnerRadius;
    });
    if (innerRadius != metadata.end()) {
        card.dishInnerRadius = innerRadius->asF64();
    }
    card.maximumCellCount = requireSubstrateMetadata(
            metadata, metadata_field::kSubstrateMaximumCellCount)
                                    .asU64();
    card.maximumFoodCount = requireSubstrateMetadata(
            metadata, metadata_field::kSubstrateMaximumFoodCount)
                                    .asU64();
    card.genomeCount = optionalSubstrateU64(
            metadata, metadata_field::kSubstrateGenomeCount);
    card.uniqueGeneCount = optionalSubstrateU64(
            metadata, metadata_field::kSubstrateUniqueGeneCount);
    card.topologyEdgeCount = optionalSubstrateU64(
            metadata, metadata_field::kSubstrateTopologyEdgeCount);
    card.deadSplashCount = optionalSubstrateU64(
            metadata, metadata_field::kSubstrateDeadSplashCount);
    card.knownOrganismCount = requireSubstrateMetadata(
            metadata,
            metadata_field::kSubstrateKnownOrganismCount)
                                      .asU64();
    const std::uint32_t organismIdentityComplete =
            requireSubstrateMetadata(
                    metadata,
                    metadata_field::kSubstrateOrganismIdentityComplete)
                    .asU32();
    card.organismIdentityComplete = organismIdentityComplete != 0;
    card.nextOrganismId = requireSubstrateMetadata(
            metadata,
            metadata_field::kSubstrateNextOrganismId)
                                  .asU64();
    card.createdAtUnixMillis = optionalSubstrateI64(
            metadata, metadata_field::kSubstrateCreatedAtUnixMillis);

    for (double value : {
                 card.simulationTimeHours,
                 card.foodMass,
                 card.dishRadius
         }) {
        cellularity::snapshot_codec::requireFinite(
                value, "Release substrate metadata value");
    }
    if (card.simulationTimeHours < 0.0 || card.foodMass < 0.0
            || !SubstrateGeometry(card.dishRadius, card.dishInnerRadius).isValid(CellData::MAX_CELL_RADIUS)
            || card.cellCount > card.maximumCellCount
            || card.foodCount > card.maximumFoodCount
            || card.knownOrganismCount > card.cellCount
            || organismIdentityComplete > 1
            || card.nextOrganismId == kUnknownOrganismId
            || card.nextOrganismId > kMaximumOrganismId + OrganismId{1}
            || card.maximumCellCount
                    > static_cast<std::uint64_t>(
                            std::numeric_limits<int>::max())
            || card.maximumFoodCount
                    > static_cast<std::uint64_t>(
                            std::numeric_limits<int>::max())
            || card.createdAtUnixMillis.value_or(0) < 0) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release substrate metadata contains invalid bounds");
    }
    return card;
}

void restoreAndValidateOrganismIdentity(
        CellData& cells,
        const SubstrateCard& card) {
    try {
        cells.restoreNextOrganismId(card.nextOrganismId);
    } catch (const std::exception&) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release Cell organism identities are invalid");
    }
    const CellData::OrganismIdentitySummary summary =
            cells.organismIdentitySummary();
    if (summary.knownOrganismCount != card.knownOrganismCount
            || summary.complete != card.organismIdentityComplete) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release organism metadata does not match Cell identities");
    }
}

std::vector<std::uint8_t> sliceReleaseBytes(
        const std::vector<std::uint8_t>& bytes,
        std::uint64_t offset,
        std::uint64_t count,
        const char* label) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        failReleaseSubstrate(
                DecodeErrorCode::TruncatedInput,
                std::string("Release substrate ") + label + " is truncated");
    }
    return std::vector<std::uint8_t>(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
}

Environment decodeReleaseEnvironment(
        const LogicalSection& section,
        bool previewMode) {
    if (section.logicalRowCount != 1) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release environment must contain exactly one row");
    }
    Environment environment;
    environment.mIsPreviewMode = previewMode;
    environment.mRandom.setSeed(readScalar<std::int64_t>(
            section, 1, PrimitiveType::I64, "random_seed"));
    environment.mLightAngle = readScalar<double>(
            section, 2, PrimitiveType::F64, "light_angle");
    const std::uint32_t maxCellCount = readScalar<std::uint32_t>(
            section, 3, PrimitiveType::U32, "max_cell_count");
    const std::uint32_t maxFoodCount = readScalar<std::uint32_t>(
            section, 4, PrimitiveType::U32, "max_food_count");
    const std::uint32_t maxDeadSplashCount = readScalar<std::uint32_t>(
            section, 5, PrimitiveType::U32, "max_dead_splash_count");
    if (maxCellCount > static_cast<std::uint32_t>(
                std::numeric_limits<int>::max())
            || maxFoodCount > static_cast<std::uint32_t>(
                       std::numeric_limits<int>::max())
            || maxDeadSplashCount > static_cast<std::uint32_t>(
                       std::numeric_limits<int>::max())) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release environment capacities exceed runtime integers");
    }
    environment.maxCellCount = static_cast<int>(maxCellCount);
    environment.maxFoodCount = static_cast<int>(maxFoodCount);
    environment.maxDeadSplashCount = static_cast<int>(maxDeadSplashCount);
    const int expectedDeadSplashCount =
            (environment.maxCellCount + 3) / 4;
    if (environment.maxDeadSplashCount != expectedDeadSplashCount) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release environment dead-splash capacity is not canonical");
    }

    const std::uint32_t behaviorFlags = readScalar<std::uint32_t>(
            section, 6, PrimitiveType::U32, "behavior_flags");
    const std::uint32_t genePoolMask = readScalar<std::uint32_t>(
            section, 7, PrimitiveType::U32, "gene_pool_mask");
    const std::uint32_t knownGeneMask =
            (std::uint32_t{1} << Environment::CELL_TYPE_COUNT) - 1U;
    if ((behaviorFlags & ~kKnownEnvironmentBehaviorFlags) != 0
            || (genePoolMask & ~knownGeneMask) != 0) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release environment contains unknown flags");
    }
    environment.IsOnlyTweakMutations = (behaviorFlags & (1U << 0U)) != 0;
    environment.mutationOnlyAfterSplit =
            (behaviorFlags & (1U << 1U)) != 0;
    environment.aging = (behaviorFlags & (1U << 2U)) != 0;
    environment.addRandom = (behaviorFlags & (1U << 3U)) != 0;
    environment.dieEdge = (behaviorFlags & (1U << 4U)) != 0;
    environment.mobileFood = (behaviorFlags & (1U << 5U)) != 0;
    for (std::size_t index = 0; index < environment.mGenePool.size(); ++index) {
        environment.mGenePool[index] =
                (genePoolMask & (1U << static_cast<unsigned>(index))) != 0;
    }

    const auto f64 = [&section](std::uint32_t id, const char* name) {
        return readScalar<double>(section, id, PrimitiveType::F64, name);
    };
    environment.mRadius = f64(8, "radius");
    if (findLogicalColumn(section, 54) != nullptr) {
        environment.mInnerRadius = f64(54, "inner_radius");
    }
    environment.mFoodSpeed = f64(9, "food_speed");
    environment.mFoodSize = f64(10, "food_size");
    environment.mRadiation = f64(11, "radiation");
    environment.mLightIntensity = f64(12, "light_intensity");
    environment.mLightSpeed = f64(13, "light_speed");
    environment.mLightRange = f64(14, "light_range");
    environment.mViscosity = f64(15, "viscosity");
    environment.mDensity = f64(16, "legacy_density");
    environment.mGravity = f64(17, "legacy_gravity");
    environment.mDensityGradient = f64(18, "legacy_density_gradient");
    environment.mTestMulticellularCoef = f64(19, "multicellular_coefficient");
    environment.mTestMutationCoef = f64(20, "mutation_coefficient");
    environment.mTestFoodDecay = f64(21, "food_decay");
    environment.mFrictionStatic = f64(22, "static_friction");
    environment.mFrictionDynamic = f64(23, "dynamic_friction");
    environment.mFoodLumpiness = f64(24, "food_lumpiness");
    environment.mFoodLumpSize = f64(25, "food_lump_size");
    environment.mNitrates = f64(26, "nitrates");
    environment.mTestIdleMetabolismScale = f64(27, "idle_metabolism_scale");
    environment.mTestFlagellocyteCostScale = f64(
            28, "flagellocyte_cost_scale");
    environment.mTestPhagocyteEatSpeedScale = f64(
            29, "phagocyte_eat_speed_scale");
    environment.mSalinity = readScalar<float>(
            section, 30, PrimitiveType::F32, "salinity");
    environment.mFoodLipidLayer = readScalar<float>(
            section, 31, PrimitiveType::F32, "food_lipid_layer");
    environment.mGravityFieldType = static_cast<int>(readScalar<std::uint32_t>(
            section, 32, PrimitiveType::U32, "gravity_field_type"));
    environment.mGravityFieldStrength = f64(33, "gravity_field_strength");
    environment.mGravityFieldAngle = f64(34, "gravity_field_angle");
    environment.mDensityFieldType = static_cast<int>(readScalar<std::uint32_t>(
            section, 35, PrimitiveType::U32, "density_field_type"));
    environment.mDensityFieldBase = f64(36, "density_field_base");
    environment.mDensityFieldStrength = f64(37, "density_field_strength");
    environment.mDensityFieldAngle = f64(38, "density_field_angle");
    environment.mDensityFieldOffset = f64(39, "density_field_offset");
    environment.mDensityFieldSoftness = f64(40, "density_field_softness");
    environment.mSalinityFieldType = static_cast<int>(readScalar<std::uint32_t>(
            section, 41, PrimitiveType::U32, "salinity_field_type"));
    environment.mSalinityFieldBase = f64(42, "salinity_field_base");
    environment.mSalinityFieldStrength = f64(43, "salinity_field_strength");
    environment.mSalinityFieldAngle = f64(44, "salinity_field_angle");
    environment.mSalinityFieldOffset = f64(45, "salinity_field_offset");
    environment.mSalinityFieldSoftness = f64(46, "salinity_field_softness");
    environment.mFoodLipidLayerFieldType = static_cast<int>(
            readScalar<std::uint32_t>(
                    section,
                    47,
                    PrimitiveType::U32,
                    "food_lipid_layer_field_type"));
    environment.mFoodLipidLayerFieldBase = f64(
            48, "food_lipid_layer_field_base");
    environment.mFoodLipidLayerFieldStrength = f64(
            49, "food_lipid_layer_field_strength");
    environment.mFoodLipidLayerFieldAngle = f64(
            50, "food_lipid_layer_field_angle");
    environment.mFoodLipidLayerFieldOffset = f64(
            51, "food_lipid_layer_field_offset");
    environment.mFoodLipidLayerFieldSoftness = f64(
            52, "food_lipid_layer_field_softness");

    const LogicalColumn& weights = requireReleaseColumn(
            section,
            53,
            PrimitiveType::F64,
            "mutation_weights",
            Environment::MUTATION_WEIGHT_COUNT);
    for (std::size_t index = 0; index < environment.mMutationWeights.size();
            ++index) {
        environment.mMutationWeights[index] =
                readLittleEndianValue<double>(weights, index);
    }

    const auto validScalarFieldType = [](int value) {
        return value >= Environment::DENSITY_FIELD_CONSTANT
                && value <= Environment::DENSITY_FIELD_RADIAL;
    };
    if (environment.mGravityFieldType
                    < Environment::VECTOR_FIELD_LINEAR_DIRECTIONAL
            || environment.mGravityFieldType
                    > Environment::VECTOR_FIELD_RADIAL_OUTWARD
            || !validScalarFieldType(environment.mDensityFieldType)
            || !validScalarFieldType(environment.mSalinityFieldType)
            || !validScalarFieldType(
                    environment.mFoodLipidLayerFieldType)) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release environment contains an invalid field type");
    }
    environment.mLightAngleCos = std::cos(environment.mLightAngle);
    environment.mLightAngleSin = std::sin(environment.mLightAngle);
    snapshot_codec::validateEnvironmentForAllocation(environment);
    return environment;
}

std::size_t checkedRowCount(std::uint64_t value, const char* label) {
    if (value > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                std::string("Release substrate ") + label
                        + " exceeds addressable memory");
    }
    return static_cast<std::size_t>(value);
}

template <typename Value, typename Destination>
void copyReleaseColumn(
        const LogicalSection& section,
        std::uint32_t id,
        PrimitiveType type,
        const std::string& name,
        std::size_t count,
        Destination& destination) {
    const LogicalColumn& column = requireReleaseColumn(
            section, id, type, name, count);
    for (std::size_t index = 0; index < count; ++index) {
        destination[index] = static_cast<typename Destination::value_type>(
                readLittleEndianValue<Value>(column, index));
    }
}

struct GenomeImport final {
    std::vector<cellularity::CanonicalGene> uniqueGenes;
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint32_t> references;
    std::vector<cellularity::GeneIndex> initialGenes;
    std::vector<std::uint32_t> cellGenomeReferences;
    std::vector<cellularity::GeneIndex> activeGenes;
};

GenomeImport finalizeGenomeImport(
        std::size_t geneRows,
        std::size_t genomeRows,
        std::size_t cellRows,
        std::vector<GeneData> geneData,
        GenomeImport result) {
    if ((cellRows == 0 && (geneRows != 0 || genomeRows != 0))
            || (cellRows != 0 && (geneRows == 0 || genomeRows == 0))
            || geneData.size() != geneRows
            || result.offsets.size() != genomeRows + 1U
            || result.initialGenes.size() != genomeRows
            || result.cellGenomeReferences.size() != cellRows
            || result.activeGenes.size() != cellRows) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release substrate Gene/Genome counts do not match Cells");
    }
    const std::size_t referenceCount = result.references.size();
    if (result.offsets.empty() || result.offsets.front() != 0
            || result.offsets.back() != referenceCount) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release Genome CSR endpoints are invalid");
    }

    std::vector<bool> usedGenomes(genomeRows, false);
    std::vector<bool> usedGenes(geneRows, false);
    std::vector<std::optional<cellularity::CanonicalGene>> canonicalRows(
            geneRows);
    for (std::size_t genome = 0; genome < genomeRows; ++genome) {
        const std::uint64_t begin = result.offsets[genome];
        const std::uint64_t end = result.offsets[genome + 1U];
        if (end < begin || end > referenceCount
                || !cellularity::isSupportedGeneCount(end - begin)
                || !cellularity::isGeneIndexInBounds(
                        result.initialGenes[genome],
                        static_cast<std::size_t>(end - begin))) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidCount,
                    "Release Genome CSR range or initial Gene is invalid");
        }
        for (std::uint64_t offset = begin; offset < end; ++offset) {
            const std::uint32_t row = result.references[static_cast<std::size_t>(offset)];
            if (row >= geneRows) {
                failReleaseSubstrate(
                        DecodeErrorCode::InvalidValue,
                        "Release Genome references an out-of-range Gene");
            }
            const std::size_t local = static_cast<std::size_t>(offset - begin);
            const cellularity::CanonicalGeneResult canonical =
                    cellularity::canonicalizeGene(
                            geneData[row],
                            cellularity::GeneIndex(static_cast<
                                    cellularity::GeneIndex::Value>(local)),
                            static_cast<std::size_t>(end - begin));
            if (!canonical.hasValue()) {
                failReleaseSubstrate(
                        DecodeErrorCode::InvalidValue,
                        "Release substrate contains an invalid canonical Gene");
            }
            if (canonicalRows[row].has_value()
                    && !cellularity::canonicalGeneEqual(
                            *canonicalRows[row], *canonical.value)) {
                failReleaseSubstrate(
                        DecodeErrorCode::InvalidValue,
                        "Release substrate reuses an owner-dependent Gene row");
            }
            if (!canonicalRows[row].has_value()) {
                canonicalRows[row].emplace(*canonical.value);
            }
            usedGenes[row] = true;
        }
    }

    std::size_t nextGenomeId = 0;
    std::size_t nextGeneId = 0;
    std::vector<bool> encounteredGenomes(genomeRows, false);
    std::vector<bool> encounteredGenes(geneRows, false);
    std::unordered_map<std::string, std::uint32_t> genomeIdentities;
    for (std::size_t cell = 0; cell < cellRows; ++cell) {
        const std::uint32_t genome = result.cellGenomeReferences[cell];
        if (genome >= genomeRows) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release Cell references an out-of-range Genome");
        }
        usedGenomes[genome] = true;
        const std::size_t count = static_cast<std::size_t>(
                result.offsets[genome + 1U] - result.offsets[genome]);
        if (!cellularity::isGeneIndexInBounds(result.activeGenes[cell], count)) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release Cell active Gene is out of range");
        }
        if (encounteredGenomes[genome]) {
            continue;
        }
        if (genome != nextGenomeId) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release Genome IDs are not in canonical first-use order");
        }
        encounteredGenomes[genome] = true;
        ++nextGenomeId;
        const std::size_t begin = static_cast<std::size_t>(
                result.offsets[genome]);
        const std::size_t end = static_cast<std::size_t>(
                result.offsets[genome + 1U]);
        std::vector<std::uint32_t> identityReferences(
                result.references.begin() + static_cast<std::ptrdiff_t>(begin),
                result.references.begin() + static_cast<std::ptrdiff_t>(end));
        const std::string identity = genomeIdentityKey(
                result.initialGenes[genome].value(), identityReferences);
        if (!genomeIdentities.emplace(identity, genome).second) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release substrate contains a duplicate Genome row");
        }
        for (std::size_t offset = begin; offset < end; ++offset) {
            const std::uint32_t gene = result.references[offset];
            if (encounteredGenes[gene]) {
                continue;
            }
            if (gene != nextGeneId) {
                failReleaseSubstrate(
                        DecodeErrorCode::InvalidValue,
                        "Release Gene IDs are not in canonical first-use order");
            }
            encounteredGenes[gene] = true;
            ++nextGeneId;
        }
    }
    if (std::find(usedGenomes.begin(), usedGenomes.end(), false)
                    != usedGenomes.end()
            || std::find(usedGenes.begin(), usedGenes.end(), false)
                    != usedGenes.end()) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release substrate contains unreferenced Gene/Genome rows");
    }

    std::unordered_map<std::uint64_t, std::vector<std::size_t>> geneIdentities;
    for (std::size_t row = 0; row < canonicalRows.size(); ++row) {
        if (!canonicalRows[row].has_value()) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release substrate contains a missing canonical Gene row");
        }
        const std::uint64_t hash = cellularity::canonicalGeneHash(
                *canonicalRows[row]);
        auto& candidates = geneIdentities[hash];
        const bool duplicate = std::any_of(
                candidates.begin(), candidates.end(),
                [&](std::size_t candidate) {
                    return cellularity::canonicalGeneEqual(
                            *canonicalRows[candidate], *canonicalRows[row]);
                });
        if (duplicate) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release substrate contains a duplicate canonical Gene row");
        }
        candidates.push_back(row);
        result.uniqueGenes.push_back(std::move(*canonicalRows[row]));
    }
    return result;
}

GenomeImport decodeReleaseGenomes(
        const LogicalSection& genes,
        const LogicalSection& genomes,
        const LogicalSection& cells) {
    const std::size_t geneRows = checkedRowCount(
            genes.logicalRowCount, "unique Gene count");
    const std::size_t genomeRows = checkedRowCount(
            genomes.logicalRowCount, "Genome count");
    const std::size_t cellRows = checkedRowCount(
            cells.logicalRowCount, "Cell count");
    if ((cellRows == 0
                && (geneRows != 0 || genomeRows != 0))
            || (cellRows != 0
                    && (geneRows == 0 || genomeRows == 0))) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release substrate Gene/Genome counts do not match Cells");
    }

    const LogicalColumn& offsetsColumn = requireReleaseColumn(
            genomes,
            1,
            PrimitiveType::U64,
            "gene_ref_offsets",
            genomeRows + 1U);
    const LogicalColumn& referencesColumn = requireReleaseColumn(
            genomes,
            2,
            PrimitiveType::U32,
            "gene_refs",
            std::nullopt);
    const LogicalColumn& initialColumn = requireReleaseColumn(
            genomes,
            3,
            PrimitiveType::U16,
            "initial_gene",
            genomeRows);
    const LogicalColumn& cellGenomeColumn = requireReleaseColumn(
            cells,
            23,
            PrimitiveType::U32,
            "genome_ref",
            cellRows);
    const LogicalColumn& activeGeneColumn = requireReleaseColumn(
            cells,
            24,
            PrimitiveType::U16,
            "active_gene_index",
            cellRows);

    GenomeImport result;
    result.offsets.resize(genomeRows + 1U);
    for (std::size_t index = 0; index < result.offsets.size(); ++index) {
        result.offsets[index] = readLittleEndianValue<std::uint64_t>(
                offsetsColumn, index);
    }
    const std::size_t referenceCount = checkedRowCount(
            referencesColumn.logicalValueCount, "Genome Gene-reference count");
    result.references.resize(referenceCount);
    for (std::size_t index = 0; index < referenceCount; ++index) {
        result.references[index] = readLittleEndianValue<std::uint32_t>(
                referencesColumn, index);
    }
    result.initialGenes.reserve(genomeRows);
    for (std::size_t genome = 0; genome < genomeRows; ++genome) {
        result.initialGenes.emplace_back(
                readLittleEndianValue<std::uint16_t>(initialColumn, genome));
    }
    result.cellGenomeReferences.resize(cellRows);
    result.activeGenes.reserve(cellRows);
    for (std::size_t cell = 0; cell < cellRows; ++cell) {
        result.cellGenomeReferences[cell] =
                readLittleEndianValue<std::uint32_t>(
                        cellGenomeColumn, cell);
        result.activeGenes.emplace_back(
                readLittleEndianValue<std::uint16_t>(activeGeneColumn, cell));
    }
    return finalizeGenomeImport(
            geneRows,
            genomeRows,
            cellRows,
            decodeReleaseGeneTable(genes),
            std::move(result));
}

GenomeImport decodeReleaseGenomesFromFile(
        ContainerFileReader& reader,
        const SectionDescriptor& genes,
        const SectionDescriptor& genomes,
        const SectionDescriptor& cells) {
    const std::size_t geneRows = checkedRowCount(
            genes.logicalRowCount, "unique Gene count");
    const std::size_t genomeRows = checkedRowCount(
            genomes.logicalRowCount, "Genome count");
    const std::size_t cellRows = checkedRowCount(
            cells.logicalRowCount, "Cell count");
    const ReleaseColumnLoader geneLoader = fileColumnLoader(reader, genes);
    const ReleaseColumnLoader genomeLoader = fileColumnLoader(reader, genomes);
    const ReleaseColumnLoader cellLoader = fileColumnLoader(reader, cells);

    GenomeImport result;
    result.offsets.resize(genomeRows + 1U);
    loadFileColumn<std::uint64_t>(
            genomeLoader,
            1,
            PrimitiveType::U64,
            "gene_ref_offsets",
            result.offsets.size(),
            [&](std::size_t index, std::uint64_t value) {
                result.offsets[index] = value;
            });

    const auto referencesDescriptor = std::find_if(
            genomes.columns.begin(), genomes.columns.end(),
            [](const ColumnDescriptor& column) {
                return column.fieldId == 2;
            });
    if (referencesDescriptor == genomes.columns.end()) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release Genome reference column is missing");
    }
    const std::size_t referenceCount = checkedRowCount(
            referencesDescriptor->logicalValueCount,
            "Genome Gene-reference count");
    result.references.resize(referenceCount);
    loadFileColumn<std::uint32_t>(
            genomeLoader,
            2,
            PrimitiveType::U32,
            "gene_refs",
            referenceCount,
            [&](std::size_t index, std::uint32_t value) {
                result.references[index] = value;
            });
    result.initialGenes.resize(genomeRows);
    loadFileColumn<std::uint16_t>(
            genomeLoader,
            3,
            PrimitiveType::U16,
            "initial_gene",
            genomeRows,
            [&](std::size_t index, std::uint16_t value) {
                result.initialGenes[index] = GeneIndex(value);
            });
    result.cellGenomeReferences.resize(cellRows);
    loadFileColumn<std::uint32_t>(
            cellLoader,
            23,
            PrimitiveType::U32,
            "genome_ref",
            cellRows,
            [&](std::size_t index, std::uint32_t value) {
                result.cellGenomeReferences[index] = value;
            });
    result.activeGenes.resize(cellRows);
    loadFileColumn<std::uint16_t>(
            cellLoader,
            24,
            PrimitiveType::U16,
            "active_gene_index",
            cellRows,
            [&](std::size_t index, std::uint16_t value) {
                result.activeGenes[index] = GeneIndex(value);
            });

    return finalizeGenomeImport(
            geneRows,
            genomeRows,
            cellRows,
            decodeReleaseGeneTableFromColumns(
                    genes.logicalRowCount, geneLoader),
            std::move(result));
}

void decodeReleaseCells(
        const LogicalSection& section,
        CellData& cells,
        const GenomeImport& genomes) {
    const std::size_t count = checkedRowCount(
            section.logicalRowCount, "Cell count");
    cells.replaceActiveCellCount(static_cast<int>(count));
    const auto f64 = [&](std::uint32_t id,
                             const std::string& name,
                             std::vector<double>& values) {
        copyReleaseColumn<double>(
                section, id, PrimitiveType::F64, name, count, values);
    };
    const auto f32 = [&](std::uint32_t id,
                             const std::string& name,
                             std::vector<float>& values) {
        copyReleaseColumn<float>(
                section, id, PrimitiveType::F32, name, count, values);
    };
    const auto i32 = [&](std::uint32_t id,
                             const std::string& name,
                             std::vector<int>& values) {
        copyReleaseColumn<std::int32_t>(
                section, id, PrimitiveType::I32, name, count, values);
    };
    const auto u64 = [&](std::uint32_t id,
                             const std::string& name,
                             std::vector<OrganismId>& values) {
        copyReleaseColumn<std::uint64_t>(
                section, id, PrimitiveType::U64, name, count, values);
    };
    f64(1, "position_x", cells.mPositionX);
    f64(2, "position_y", cells.mPositionY);
    f64(3, "position_z", cells.mPositionZ);
    f64(4, "radius", cells.mRadius);
    f64(5, "velocity_x", cells.mVelocityX);
    f64(6, "velocity_y", cells.mVelocityY);
    f64(7, "force_x", cells.mForceX);
    f64(8, "force_y", cells.mForceY);
    f64(9, "force_z", cells.mForceZ);
    f64(10, "torque", cells.mTorque);
    f64(11, "angle", cells.mAngle);
    f64(12, "angle_cytoplasm", cells.mAngleCytoplasm);
    f64(13, "angular_velocity", cells.mAngularVelocity);
    f64(14, "angular_velocity_cytoplasm", cells.mAngularVelocityCytoplasm);
    f64(15, "age", cells.mAge);
    f64(16, "nitrates", cells.mNitrates);
    f64(17, "hair_rotation", cells.mHairRotation);
    f64(18, "hair_x", cells.hairX);
    f64(19, "hair_y", cells.hairY);
    f64(20, "mass", cells.mMass);
    f32(21, "swim_animation_time", cells.mSwimAnimationTime);
    f32(22, "swim_animation_time_2", cells.mSwimAnimationTime2);

    const LogicalColumn& flags = requireReleaseColumn(
            section, 25, PrimitiveType::U8, "flags", count);
    for (std::size_t cell = 0; cell < count; ++cell) {
        const std::uint8_t value = readLittleEndianValue<std::uint8_t>(
                flags, cell);
        if ((value & ~kKnownCellFlags) != 0) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release Cell contains unknown flags");
        }
        cells.mIsDead[cell] = (value & (1U << 0U)) != 0;
        cells.mIsMirrored[cell] = (value & (1U << 1U)) != 0;
        cells.mIsStuck[cell] = (value & (1U << 2U)) != 0;
    }
    i32(27, "origin_tag", cells.mOriginTag);
    i32(28, "select_tag", cells.mSelectTag);
    i32(29, "mutation_count", cells.mMutationCount);
    i32(30, "pending_mutation_count", cells.mPendingMutationCount);
    i32(31, "seldom_counter", cells.mSeldomCounter);
    i32(32, "old_link_count", cells.mOldLinkCount);
    i32(33, "telomeres_count", cells.mTelomeresCount);
    f32(34, "lipids", cells.mLipids);
    u64(39, "organism_id", cells.mOrganismIds);
    u64(40, "parent_organism_id", cells.mParentOrganismIds);
    for (int channel = 0; channel < 4; ++channel) {
        const LogicalColumn& color = requireReleaseColumn(
                section,
                35U + static_cast<std::uint32_t>(channel),
                PrimitiveType::F32,
                std::string("color_") + "rgba"[channel],
                count);
        for (std::size_t cell = 0; cell < count; ++cell) {
            cells.mColor[cell][channel] = readLittleEndianValue<float>(
                    color, cell);
        }
    }
    for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT * 2;
            ++signal) {
        const LogicalColumn& column = requireReleaseColumn(
                section,
                100U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "signal_substance_" + std::to_string(signal),
                count);
        for (std::size_t cell = 0; cell < count; ++cell) {
            cells.mSignalSubstance[cell][signal] =
                    readLittleEndianValue<float>(column, cell);
        }
    }
    for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT; ++signal) {
        const LogicalColumn& incoming = requireReleaseColumn(
                section,
                110U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "new_signal_substance_" + std::to_string(signal),
                count);
        const LogicalColumn& outgoing = requireReleaseColumn(
                section,
                120U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "signal_substance_out_" + std::to_string(signal),
                count);
        for (std::size_t cell = 0; cell < count; ++cell) {
            cells.mNewSignalSubstance[cell][signal] =
                    readLittleEndianValue<float>(incoming, cell);
            cells.mSignalSubstanceOut[cell][signal] =
                    readLittleEndianValue<float>(outgoing, cell);
        }
    }
    f32(130, "toxin", cells.mToxin);
    f32(131, "toxication", cells.mToxication);
    f32(132, "new_toxin", cells.mNewToxin);
    f32(133, "myocyte_contraction", cells.mMyocyteContraction);
    f32(134, "myocyte_bending", cells.mMyocyteBending);
    f32(135, "myocyte_lifting", cells.mMyocyteLifting);

    if (!cells.importSharedGenomes(
                genomes.uniqueGenes,
                genomes.offsets,
                genomes.references,
                genomes.initialGenes,
                genomes.cellGenomeReferences,
                genomes.activeGenes)) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release pooled Genomes could not be imported");
    }
    for (std::size_t cell = 0; cell < count; ++cell) {
        cells.refreshRadiusSq(static_cast<int>(cell));
        cells.mNewMass[cell] = cells.mMass[cell];
        cells.mNewNitrates[cell] = cells.mNitrates[cell];
        cells.mFertilizationTarget[cell] = -1;
        cells.clearCellCollisionContacts(static_cast<int>(cell));
    }
    cells.updateAllCellAngleDirectionCaches();
}

void decodeReleaseCellsFromFile(
        ContainerFileReader& reader,
        const SectionDescriptor& section,
        CellData& cells,
        const GenomeImport& genomes) {
    const std::size_t count = checkedRowCount(
            section.logicalRowCount, "Cell count");
    if (count > static_cast<std::size_t>(cells.MAX_CELL_COUNT)) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release Cell count exceeds the Environment limit");
    }
    cells.replaceActiveCellCount(static_cast<int>(count));
    const ReleaseColumnLoader loader = fileColumnLoader(reader, section);
    const auto f64 = [&](std::uint32_t id,
                             const std::string& name,
                             std::vector<double>& destination) {
        loadFileColumn<double>(
                loader, id, PrimitiveType::F64, name, count,
                [&](std::size_t index, double value) {
                    destination[index] = value;
                });
    };
    const auto f32 = [&](std::uint32_t id,
                             const std::string& name,
                             std::vector<float>& destination) {
        loadFileColumn<float>(
                loader, id, PrimitiveType::F32, name, count,
                [&](std::size_t index, float value) {
                    destination[index] = value;
                });
    };
    const auto i32 = [&](std::uint32_t id,
                             const std::string& name,
                             std::vector<int>& destination) {
        loadFileColumn<std::int32_t>(
                loader, id, PrimitiveType::I32, name, count,
                [&](std::size_t index, std::int32_t value) {
                    destination[index] = value;
                });
    };
    const auto u64 = [&](std::uint32_t id,
                             const std::string& name,
                             std::vector<OrganismId>& destination) {
        loadFileColumn<std::uint64_t>(
                loader, id, PrimitiveType::U64, name, count,
                [&](std::size_t index, std::uint64_t value) {
                    destination[index] = value;
                });
    };

    f64(1, "position_x", cells.mPositionX);
    f64(2, "position_y", cells.mPositionY);
    f64(3, "position_z", cells.mPositionZ);
    f64(4, "radius", cells.mRadius);
    f64(5, "velocity_x", cells.mVelocityX);
    f64(6, "velocity_y", cells.mVelocityY);
    f64(7, "force_x", cells.mForceX);
    f64(8, "force_y", cells.mForceY);
    f64(9, "force_z", cells.mForceZ);
    f64(10, "torque", cells.mTorque);
    f64(11, "angle", cells.mAngle);
    f64(12, "angle_cytoplasm", cells.mAngleCytoplasm);
    f64(13, "angular_velocity", cells.mAngularVelocity);
    f64(14, "angular_velocity_cytoplasm", cells.mAngularVelocityCytoplasm);
    f64(15, "age", cells.mAge);
    f64(16, "nitrates", cells.mNitrates);
    f64(17, "hair_rotation", cells.mHairRotation);
    f64(18, "hair_x", cells.hairX);
    f64(19, "hair_y", cells.hairY);
    f64(20, "mass", cells.mMass);
    f32(21, "swim_animation_time", cells.mSwimAnimationTime);
    f32(22, "swim_animation_time_2", cells.mSwimAnimationTime2);

    loadFileColumn<std::uint8_t>(
            loader, 25, PrimitiveType::U8, "flags", count,
            [&](std::size_t index, std::uint8_t value) {
                if ((value & ~kKnownCellFlags) != 0) {
                    failReleaseSubstrate(
                            DecodeErrorCode::InvalidValue,
                            "Release Cell contains unknown flags");
                }
                cells.mIsDead[index] = (value & (1U << 0U)) != 0;
                cells.mIsMirrored[index] = (value & (1U << 1U)) != 0;
                cells.mIsStuck[index] = (value & (1U << 2U)) != 0;
            });
    i32(27, "origin_tag", cells.mOriginTag);
    i32(28, "select_tag", cells.mSelectTag);
    i32(29, "mutation_count", cells.mMutationCount);
    i32(30, "pending_mutation_count", cells.mPendingMutationCount);
    i32(31, "seldom_counter", cells.mSeldomCounter);
    i32(32, "old_link_count", cells.mOldLinkCount);
    i32(33, "telomeres_count", cells.mTelomeresCount);
    f32(34, "lipids", cells.mLipids);
    u64(39, "organism_id", cells.mOrganismIds);
    u64(40, "parent_organism_id", cells.mParentOrganismIds);
    for (int channel = 0; channel < 4; ++channel) {
        loadFileColumn<float>(
                loader,
                35U + static_cast<std::uint32_t>(channel),
                PrimitiveType::F32,
                std::string("color_") + "rgba"[channel],
                count,
                [&](std::size_t index, float value) {
                    cells.mColor[index][channel] = value;
                });
    }
    for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT * 2;
            ++signal) {
        loadFileColumn<float>(
                loader,
                100U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "signal_substance_" + std::to_string(signal),
                count,
                [&](std::size_t index, float value) {
                    cells.mSignalSubstance[index][signal] = value;
                });
    }
    for (int signal = 0; signal < CellData::SIGNAL_SUBSTANCE_COUNT; ++signal) {
        loadFileColumn<float>(
                loader,
                110U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "new_signal_substance_" + std::to_string(signal),
                count,
                [&](std::size_t index, float value) {
                    cells.mNewSignalSubstance[index][signal] = value;
                });
        loadFileColumn<float>(
                loader,
                120U + static_cast<std::uint32_t>(signal),
                PrimitiveType::F32,
                "signal_substance_out_" + std::to_string(signal),
                count,
                [&](std::size_t index, float value) {
                    cells.mSignalSubstanceOut[index][signal] = value;
                });
    }
    f32(130, "toxin", cells.mToxin);
    f32(131, "toxication", cells.mToxication);
    f32(132, "new_toxin", cells.mNewToxin);
    f32(133, "myocyte_contraction", cells.mMyocyteContraction);
    f32(134, "myocyte_bending", cells.mMyocyteBending);
    f32(135, "myocyte_lifting", cells.mMyocyteLifting);

    if (!cells.importSharedGenomes(
                genomes.uniqueGenes,
                genomes.offsets,
                genomes.references,
                genomes.initialGenes,
                genomes.cellGenomeReferences,
                genomes.activeGenes)) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release pooled Genomes could not be imported");
    }
    for (std::size_t cell = 0; cell < count; ++cell) {
        cells.refreshRadiusSq(static_cast<int>(cell));
        cells.mNewMass[cell] = cells.mMass[cell];
        cells.mNewNitrates[cell] = cells.mNitrates[cell];
        cells.mFertilizationTarget[cell] = -1;
        cells.clearCellCollisionContacts(static_cast<int>(cell));
    }
    cells.updateAllCellAngleDirectionCaches();
}

void decodeReleaseTopology(
        const LogicalSection& edges,
        const LogicalSection& boundaries,
        CellData& cells) {
    const std::size_t edgeCount = checkedRowCount(
            edges.logicalRowCount, "topology edge count");
    const std::size_t boundaryCount = checkedRowCount(
            boundaries.logicalRowCount, "topology boundary count");
    const std::uint64_t maximumEntries =
            static_cast<std::uint64_t>(cells.mCellCount)
            * CellData::MAX_NEIGHBORS;
    if (edgeCount > maximumEntries || boundaryCount > maximumEntries
            || edgeCount > static_cast<std::size_t>(
                       std::numeric_limits<int>::max())
            || boundaryCount > static_cast<std::size_t>(
                       std::numeric_limits<int>::max())) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release topology exceeds the Cell adjacency bound");
    }

    const LogicalColumn& cellA = requireReleaseColumn(
            edges, 1, PrimitiveType::U32, "cell_a", edgeCount);
    const LogicalColumn& cellB = requireReleaseColumn(
            edges, 2, PrimitiveType::U32, "cell_b", edgeCount);
    const LogicalColumn& angleA = requireReleaseColumn(
            edges, 3, PrimitiveType::F64, "angle_a", edgeCount);
    const LogicalColumn& angleB = requireReleaseColumn(
            edges, 4, PrimitiveType::F64, "angle_b", edgeCount);
    const LogicalColumn& stiffness = requireReleaseColumn(
            edges, 5, PrimitiveType::F32, "torsional_stiffness", edgeCount);
    const LogicalColumn& length = requireReleaseColumn(
            edges, 6, PrimitiveType::F32, "rest_length", edgeCount);
    const LogicalColumn& creationReason = requireReleaseColumn(
            edges,
            11,
            PrimitiveType::U8,
            "creation_reason",
            edgeCount);
    constexpr std::array<const char*, 4> renderNames{{"render_start_offset_a_x", "render_start_offset_a_y",
            "render_end_offset_a_x", "render_end_offset_a_y"}};
    std::array<const LogicalColumn*, 4> render{};
    for (std::size_t component = 0; component < render.size(); ++component) {
        render[component] = &requireReleaseColumn(
                edges,
                7U + static_cast<std::uint32_t>(component),
                PrimitiveType::F32,
                renderNames[component],
                edgeCount);
    }
    cells.mTopology.clearAll();
    for (std::size_t row = 0; row < edgeCount; ++row) {
        const std::uint32_t first = readLittleEndianValue<std::uint32_t>(
                cellA, row);
        const std::uint32_t second = readLittleEndianValue<std::uint32_t>(
                cellB, row);
        const double firstAngle = readLittleEndianValue<double>(angleA, row);
        const double secondAngle = readLittleEndianValue<double>(angleB, row);
        const float edgeStiffness = readLittleEndianValue<float>(
                stiffness, row);
        const float restLength = readLittleEndianValue<float>(length, row);
        const CellLinkCreationReason edgeCreationReason =
                static_cast<CellLinkCreationReason>(
                        readLittleEndianValue<std::uint8_t>(
                                creationReason, row));
        std::array<float, 4> offsets{};
        for (std::size_t component = 0; component < offsets.size(); ++component) {
            offsets[component] = readLittleEndianValue<float>(
                    *render[component], row);
        }
        for (double value : {
                     firstAngle,
                     secondAngle,
                     static_cast<double>(edgeStiffness),
                     static_cast<double>(restLength),
                     static_cast<double>(offsets[0]),
                     static_cast<double>(offsets[1]),
                     static_cast<double>(offsets[2]),
                     static_cast<double>(offsets[3])
             }) {
            cellularity::snapshot_codec::requireFinite(
                    value, "Release topology edge value");
        }
        if (first >= static_cast<std::uint32_t>(cells.mCellCount)
                || second >= static_cast<std::uint32_t>(cells.mCellCount)) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology edge references an invalid Cell");
        }
        if (!isValidCellLinkCreationReason(edgeCreationReason)) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology edge has an invalid creation reason");
        }
        const int edge = cells.mTopology.addCellLink(
                static_cast<int>(first),
                static_cast<int>(second),
                edgeCreationReason,
                firstAngle,
                secondAngle,
                edgeStiffness,
                restLength);
        if (edge < 0) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology edge adjacency is invalid");
        }
        cells.mTopology.setEdgeRenderOffsets(
                edge, offsets[0], offsets[1], offsets[2], offsets[3]);
    }

    const LogicalColumn& boundaryCell = requireReleaseColumn(
            boundaries, 1, PrimitiveType::U32, "cell", boundaryCount);
    const LogicalColumn& boundaryAngle = requireReleaseColumn(
            boundaries, 2, PrimitiveType::F64, "angle", boundaryCount);
    const LogicalColumn& anchorX = requireReleaseColumn(
            boundaries, 3, PrimitiveType::F64, "anchor_x", boundaryCount);
    const LogicalColumn& anchorY = requireReleaseColumn(
            boundaries, 4, PrimitiveType::F64, "anchor_y", boundaryCount);
    const LogicalColumn& boundaryStiffness = requireReleaseColumn(
            boundaries,
            5,
            PrimitiveType::F32,
            "torsional_stiffness",
            boundaryCount);
    const LogicalColumn& boundaryLength = requireReleaseColumn(
            boundaries, 6, PrimitiveType::F32, "rest_length", boundaryCount);
    const LogicalColumn& renderX = requireReleaseColumn(
            boundaries,
            7,
            PrimitiveType::F32,
            "render_start_offset_x",
            boundaryCount);
    const LogicalColumn& renderY = requireReleaseColumn(
            boundaries,
            8,
            PrimitiveType::F32,
            "render_start_offset_y",
            boundaryCount);
    for (std::size_t row = 0; row < boundaryCount; ++row) {
        const std::uint32_t cell = readLittleEndianValue<std::uint32_t>(
                boundaryCell, row);
        const double angle = readLittleEndianValue<double>(boundaryAngle, row);
        const double x = readLittleEndianValue<double>(anchorX, row);
        const double y = readLittleEndianValue<double>(anchorY, row);
        const float linkStiffness = readLittleEndianValue<float>(
                boundaryStiffness, row);
        const float restLength = readLittleEndianValue<float>(
                boundaryLength, row);
        const float offsetX = readLittleEndianValue<float>(renderX, row);
        const float offsetY = readLittleEndianValue<float>(renderY, row);
        for (double value : {
                     angle,
                     x,
                     y,
                     static_cast<double>(linkStiffness),
                     static_cast<double>(restLength),
                     static_cast<double>(offsetX),
                     static_cast<double>(offsetY)
             }) {
            cellularity::snapshot_codec::requireFinite(
                    value, "Release topology boundary value");
        }
        if (cell >= static_cast<std::uint32_t>(cells.mCellCount)) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology boundary references an invalid Cell");
        }
        const int boundary = cells.mTopology.addBoundaryLink(
                static_cast<int>(cell),
                angle,
                x,
                y,
                linkStiffness,
                restLength);
        if (boundary < 0) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology boundary adjacency is invalid");
        }
        cells.mTopology.setBoundaryRenderStartOffset(
                boundary, offsetX, offsetY);
    }
    if (!cells.mTopology.validate()) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release topology adjacency validation failed");
    }
}

struct FileEdgeRow final {
    std::uint32_t cellA = 0;
    std::uint32_t cellB = 0;
    std::uint8_t creationReason = 0;
    double angleA = 0.0;
    double angleB = 0.0;
    float stiffness = 0.0f;
    float restLength = 0.0f;
    std::array<float, 4> render{};
};

struct FileBoundaryRow final {
    std::uint32_t cell = 0;
    double angle = 0.0;
    double anchorX = 0.0;
    double anchorY = 0.0;
    float stiffness = 0.0f;
    float restLength = 0.0f;
    float renderX = 0.0f;
    float renderY = 0.0f;
};

void decodeReleaseTopologyFromFile(
        ContainerFileReader& reader,
        const SectionDescriptor& edges,
        const SectionDescriptor& boundaries,
        CellData& cells) {
    const std::size_t edgeCount = checkedRowCount(
            edges.logicalRowCount, "topology edge count");
    const std::size_t boundaryCount = checkedRowCount(
            boundaries.logicalRowCount, "topology boundary count");
    const std::uint64_t maximumEntries =
            static_cast<std::uint64_t>(cells.mCellCount)
            * CellData::MAX_NEIGHBORS;
    if (edgeCount > maximumEntries || boundaryCount > maximumEntries
            || edgeCount > static_cast<std::size_t>(
                       std::numeric_limits<int>::max())
            || boundaryCount > static_cast<std::size_t>(
                       std::numeric_limits<int>::max())) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release topology exceeds the Cell adjacency bound");
    }

    std::vector<FileEdgeRow> edgeRows(edgeCount);
    const ReleaseColumnLoader edgeLoader = fileColumnLoader(reader, edges);
    loadFileColumn<std::uint32_t>(
            edgeLoader, 1, PrimitiveType::U32, "cell_a", edgeCount,
            [&](std::size_t row, std::uint32_t value) {
                edgeRows[row].cellA = value;
            });
    loadFileColumn<std::uint32_t>(
            edgeLoader, 2, PrimitiveType::U32, "cell_b", edgeCount,
            [&](std::size_t row, std::uint32_t value) {
                edgeRows[row].cellB = value;
            });
    loadFileColumn<double>(
            edgeLoader, 3, PrimitiveType::F64, "angle_a", edgeCount,
            [&](std::size_t row, double value) {
                edgeRows[row].angleA = value;
            });
    loadFileColumn<double>(
            edgeLoader, 4, PrimitiveType::F64, "angle_b", edgeCount,
            [&](std::size_t row, double value) {
                edgeRows[row].angleB = value;
            });
    loadFileColumn<float>(
            edgeLoader, 5, PrimitiveType::F32, "torsional_stiffness", edgeCount,
            [&](std::size_t row, float value) {
                edgeRows[row].stiffness = value;
            });
    loadFileColumn<float>(
            edgeLoader, 6, PrimitiveType::F32, "rest_length", edgeCount,
            [&](std::size_t row, float value) {
                edgeRows[row].restLength = value;
            });
    loadFileColumn<std::uint8_t>(
            edgeLoader, 11, PrimitiveType::U8, "creation_reason", edgeCount,
            [&](std::size_t row, std::uint8_t value) {
                edgeRows[row].creationReason = value;
            });
    constexpr std::array<const char*, 4> renderNames{{"render_start_offset_a_x", "render_start_offset_a_y",
            "render_end_offset_a_x", "render_end_offset_a_y"}};
    for (std::size_t component = 0; component < renderNames.size();
            ++component) {
        loadFileColumn<float>(
                edgeLoader,
                7U + static_cast<std::uint32_t>(component),
                PrimitiveType::F32,
                renderNames[component],
                edgeCount,
                [&](std::size_t row, float value) {
                    edgeRows[row].render[component] = value;
                });
    }

    std::vector<FileBoundaryRow> boundaryRows(boundaryCount);
    const ReleaseColumnLoader boundaryLoader = fileColumnLoader(
            reader, boundaries);
    loadFileColumn<std::uint32_t>(
            boundaryLoader, 1, PrimitiveType::U32, "cell", boundaryCount,
            [&](std::size_t row, std::uint32_t value) {
                boundaryRows[row].cell = value;
            });
    loadFileColumn<double>(
            boundaryLoader, 2, PrimitiveType::F64, "angle", boundaryCount,
            [&](std::size_t row, double value) {
                boundaryRows[row].angle = value;
            });
    loadFileColumn<double>(
            boundaryLoader, 3, PrimitiveType::F64, "anchor_x", boundaryCount,
            [&](std::size_t row, double value) {
                boundaryRows[row].anchorX = value;
            });
    loadFileColumn<double>(
            boundaryLoader, 4, PrimitiveType::F64, "anchor_y", boundaryCount,
            [&](std::size_t row, double value) {
                boundaryRows[row].anchorY = value;
            });
    loadFileColumn<float>(
            boundaryLoader,
            5,
            PrimitiveType::F32,
            "torsional_stiffness",
            boundaryCount,
            [&](std::size_t row, float value) {
                boundaryRows[row].stiffness = value;
            });
    loadFileColumn<float>(
            boundaryLoader, 6, PrimitiveType::F32, "rest_length", boundaryCount,
            [&](std::size_t row, float value) {
                boundaryRows[row].restLength = value;
            });
    loadFileColumn<float>(
            boundaryLoader,
            7,
            PrimitiveType::F32,
            "render_start_offset_x",
            boundaryCount,
            [&](std::size_t row, float value) {
                boundaryRows[row].renderX = value;
            });
    loadFileColumn<float>(
            boundaryLoader,
            8,
            PrimitiveType::F32,
            "render_start_offset_y",
            boundaryCount,
            [&](std::size_t row, float value) {
                boundaryRows[row].renderY = value;
            });

    cells.mTopology.clearAll();
    for (const FileEdgeRow& row : edgeRows) {
        const CellLinkCreationReason creationReason =
                static_cast<CellLinkCreationReason>(row.creationReason);
        for (double value : {
                     row.angleA,
                     row.angleB,
                     static_cast<double>(row.stiffness),
                     static_cast<double>(row.restLength),
                     static_cast<double>(row.render[0]),
                     static_cast<double>(row.render[1]),
                     static_cast<double>(row.render[2]),
                     static_cast<double>(row.render[3])
             }) {
            cellularity::snapshot_codec::requireFinite(
                    value, "Release topology edge value");
        }
        if (row.cellA >= static_cast<std::uint32_t>(cells.mCellCount)
                || row.cellB >= static_cast<std::uint32_t>(cells.mCellCount)) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology edge references an invalid Cell");
        }
        if (!isValidCellLinkCreationReason(creationReason)) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology edge has an invalid creation reason");
        }
        const int edge = cells.mTopology.addCellLink(
                static_cast<int>(row.cellA),
                static_cast<int>(row.cellB),
                creationReason,
                row.angleA,
                row.angleB,
                row.stiffness,
                row.restLength);
        if (edge < 0) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology edge adjacency is invalid");
        }
        cells.mTopology.setEdgeRenderOffsets(
                edge,
                row.render[0],
                row.render[1],
                row.render[2],
                row.render[3]);
    }
    for (const FileBoundaryRow& row : boundaryRows) {
        for (double value : {
                     row.angle,
                     row.anchorX,
                     row.anchorY,
                     static_cast<double>(row.stiffness),
                     static_cast<double>(row.restLength),
                     static_cast<double>(row.renderX),
                     static_cast<double>(row.renderY)
             }) {
            cellularity::snapshot_codec::requireFinite(
                    value, "Release topology boundary value");
        }
        if (row.cell >= static_cast<std::uint32_t>(cells.mCellCount)) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology boundary references an invalid Cell");
        }
        const int boundary = cells.mTopology.addBoundaryLink(
                static_cast<int>(row.cell),
                row.angle,
                row.anchorX,
                row.anchorY,
                row.stiffness,
                row.restLength);
        if (boundary < 0) {
            failReleaseSubstrate(
                    DecodeErrorCode::InvalidValue,
                    "Release topology boundary adjacency is invalid");
        }
        cells.mTopology.setBoundaryRenderStartOffset(
                boundary, row.renderX, row.renderY);
    }
    if (!cells.mTopology.validate()) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release topology adjacency validation failed");
    }
}

void decodeReleaseFood(
        const LogicalSection& section,
        FoodData& food) {
    const std::size_t count = checkedRowCount(
            section.logicalRowCount, "Food count");
    if (count > static_cast<std::size_t>(food.MAX_FOOD_COUNT)) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release Food count exceeds the Environment limit");
    }
    food.replaceActiveFoodCount(static_cast<int>(count));
    constexpr std::array<const char*, 6> names{{"position_x", "position_y", "mass", "velocity_x", "velocity_y",
            "lipid_layer"}};
    const std::array<std::vector<float>*, 6> values{{&food.mPositionX, &food.mPositionY, &food.mMass,
            &food.mVelocityX, &food.mVelocityY, &food.mLipidLayer}};
    for (std::size_t field = 0; field < values.size(); ++field) {
        copyReleaseColumn<float>(
                section,
                static_cast<std::uint32_t>(field + 1U),
                PrimitiveType::F32,
                names[field],
                count,
                *values[field]);
    }
}

void decodeReleaseDeadSplashes(
        const LogicalSection& section,
        DeadSplashData& splashes) {
    const std::size_t count = checkedRowCount(
            section.logicalRowCount, "dead-splash count");
    if (count > static_cast<std::size_t>(splashes.MAX_DEAD_SPLASH_COUNT)) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release dead-splash count exceeds the Environment limit");
    }
    splashes.replaceActiveDeadSplashCount(static_cast<int>(count));
    constexpr std::array<const char*, 4> scalarNames{{"position_x", "position_y", "radius", "age"}};
    const std::array<std::vector<float>*, 4> scalarValues{{&splashes.mPositionX,
            &splashes.mPositionY,
            &splashes.mRadius,
            &splashes.mAge}};
    for (std::size_t field = 0; field < scalarValues.size(); ++field) {
        copyReleaseColumn<float>(
                section,
                static_cast<std::uint32_t>(field + 1U),
                PrimitiveType::F32,
                scalarNames[field],
                count,
                *scalarValues[field]);
    }
    for (int channel = 0; channel < 4; ++channel) {
        const LogicalColumn& color = requireReleaseColumn(
                section,
                5U + static_cast<std::uint32_t>(channel),
                PrimitiveType::F32,
                std::string("color_") + "rgba"[channel],
                count);
        for (std::size_t splash = 0; splash < count; ++splash) {
            splashes.mColor[splash][channel] = readLittleEndianValue<float>(
                    color, splash);
        }
    }
}

void decodeReleaseFoodFromFile(
        ContainerFileReader& reader,
        const SectionDescriptor& section,
        FoodData& food) {
    const std::size_t count = checkedRowCount(
            section.logicalRowCount, "Food count");
    if (count > static_cast<std::size_t>(food.MAX_FOOD_COUNT)) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release Food count exceeds the Environment limit");
    }
    food.replaceActiveFoodCount(static_cast<int>(count));
    constexpr std::array<const char*, 6> names{{"position_x", "position_y", "mass", "velocity_x", "velocity_y",
            "lipid_layer"}};
    const std::array<std::vector<float>*, 6> destinations{{&food.mPositionX, &food.mPositionY, &food.mMass,
            &food.mVelocityX, &food.mVelocityY, &food.mLipidLayer}};
    const ReleaseColumnLoader loader = fileColumnLoader(reader, section);
    for (std::size_t field = 0; field < destinations.size(); ++field) {
        loadFileColumn<float>(
                loader,
                static_cast<std::uint32_t>(field + 1U),
                PrimitiveType::F32,
                names[field],
                count,
                [&, field](std::size_t index, float value) {
                    (*destinations[field])[index] = value;
                });
    }
}

void decodeReleaseDeadSplashesFromFile(
        ContainerFileReader& reader,
        const SectionDescriptor& section,
        DeadSplashData& splashes) {
    const std::size_t count = checkedRowCount(
            section.logicalRowCount, "dead-splash count");
    if (count > static_cast<std::size_t>(splashes.MAX_DEAD_SPLASH_COUNT)) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release dead-splash count exceeds the Environment limit");
    }
    splashes.replaceActiveDeadSplashCount(static_cast<int>(count));
    constexpr std::array<const char*, 4> scalarNames{{"position_x", "position_y", "radius", "age"}};
    const std::array<std::vector<float>*, 4> destinations{{&splashes.mPositionX,
            &splashes.mPositionY,
            &splashes.mRadius,
            &splashes.mAge}};
    const ReleaseColumnLoader loader = fileColumnLoader(reader, section);
    for (std::size_t field = 0; field < destinations.size(); ++field) {
        loadFileColumn<float>(
                loader,
                static_cast<std::uint32_t>(field + 1U),
                PrimitiveType::F32,
                scalarNames[field],
                count,
                [&, field](std::size_t index, float value) {
                    (*destinations[field])[index] = value;
                });
    }
    for (int channel = 0; channel < 4; ++channel) {
        loadFileColumn<float>(
                loader,
                5U + static_cast<std::uint32_t>(channel),
                PrimitiveType::F32,
                std::string("color_") + "rgba"[channel],
                count,
                [&](std::size_t index, float value) {
                    splashes.mColor[index][channel] = value;
                });
    }
}

bool sameLogicalSections(
        const std::vector<LogicalSection>& left,
        const std::vector<LogicalSection>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t section = 0; section < left.size(); ++section) {
        const LogicalSection& a = left[section];
        const LogicalSection& b = right[section];
        if (a.sectionId != b.sectionId || a.schemaMajor != b.schemaMajor
                || a.schemaMinor != b.schemaMinor
                || a.canonicalName != b.canonicalName
                || a.logicalRowCount != b.logicalRowCount
                || a.columns.size() != b.columns.size()) {
            return false;
        }
        for (std::size_t column = 0; column < a.columns.size(); ++column) {
            const LogicalColumn& x = a.columns[column];
            const LogicalColumn& y = b.columns[column];
            if (x.fieldId != y.fieldId
                    || x.primitiveType != y.primitiveType
                    || x.canonicalName != y.canonicalName
                    || x.logicalValueCount != y.logicalValueCount
                    || x.decodedBytes != y.decodedBytes) {
                return false;
            }
        }
    }
    return true;
}

struct ReleaseFileSections final {
    SubstrateCard card;
    const SectionDescriptor* environment = nullptr;
    const SectionDescriptor* genes = nullptr;
    const SectionDescriptor* genomes = nullptr;
    const SectionDescriptor* cells = nullptr;
    const SectionDescriptor* edges = nullptr;
    const SectionDescriptor* boundaries = nullptr;
    const SectionDescriptor* food = nullptr;
    const SectionDescriptor* splashes = nullptr;
};

ReleaseFileSections bindReleaseFileSections(ContainerFileReader& reader) {
    if (reader.header().documentKind != DocumentKind::Substrate
            || reader.sections().size() != 8) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidHeader,
                "CLTY document is not a release substrate");
    }
    ReleaseFileSections result;
    result.card = readSubstrateCard(reader.metadata(), true);
    result.environment = &requireFileSection(
            reader.sections(), section_id::kEnvironment, "environment", 53);
    result.genes = &requireFileSection(
            reader.sections(), section_id::kGenes, "genes", 87);
    result.genomes = &requireFileSection(
            reader.sections(), section_id::kGenomes, "genomes", 3);
    result.cells = &requireFileSection(
            reader.sections(), section_id::kCells, "cells", 61, 1);
    result.edges = &requireFileSection(
            reader.sections(),
            section_id::kTopologyEdges,
            "topology_edges",
            11,
            1);
    result.boundaries = &requireFileSection(
            reader.sections(),
            section_id::kTopologyBoundaries,
            "topology_boundaries",
            8);
    result.food = &requireFileSection(
            reader.sections(), section_id::kFood, "food", 6);
    result.splashes = &requireFileSection(
            reader.sections(),
            section_id::kDeadSplashes,
            "dead_splashes",
            8);

    if (result.card.cellCount != result.cells->logicalRowCount
            || result.card.foodCount != result.food->logicalRowCount
            || (result.card.genomeCount.has_value()
                    && *result.card.genomeCount
                            != result.genomes->logicalRowCount)
            || (result.card.uniqueGeneCount.has_value()
                    && *result.card.uniqueGeneCount
                            != result.genes->logicalRowCount)
            || (result.card.topologyEdgeCount.has_value()
                    && *result.card.topologyEdgeCount
                            != result.edges->logicalRowCount)
            || (result.card.deadSplashCount.has_value()
                    && *result.card.deadSplashCount
                            != result.splashes->logicalRowCount)) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release substrate metadata and section counts disagree");
    }
    const std::uint64_t maximumTopologyEntries = result.card.cellCount
            * static_cast<std::uint64_t>(CellData::MAX_NEIGHBORS);
    if (result.edges->logicalRowCount > maximumTopologyEntries
            || result.boundaries->logicalRowCount > maximumTopologyEntries) {
        failReleaseSubstrate(
                DecodeErrorCode::LimitExceeded,
                "Release topology exceeds the Cell adjacency bound");
    }
    return result;
}

template <typename Unsigned>
void updateDigestUnsigned(cellularity::io::Sha256& digest, Unsigned value) {
    static_assert(std::is_unsigned_v<Unsigned>);
    std::array<std::uint8_t, sizeof(Unsigned)> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(
                value >> static_cast<unsigned>(index * 8U));
    }
    digest.update(bytes.data(), bytes.size());
}

void updateDigestText(
        cellularity::io::Sha256& digest,
        const std::string& value) {
    updateDigestUnsigned(
            digest, static_cast<std::uint64_t>(value.size()));
    digest.update(
            reinterpret_cast<const std::uint8_t*>(value.data()),
            value.size());
}

} // namespace

ContainerArtifact encodeReleaseSubstrate(
        const SimulationWorld& world,
        const EncodedPreview& preview,
        const BodyWriteOptions& options) {
    return encodeContainer(exportSubstrateRuntimeSoa(world), options, preview);
}

ContainerWriteResult writeReleaseSubstratePath(
        const std::string& path,
        const SimulationWorld& world,
        const EncodedPreview& preview,
        const BodyWriteOptions& options) {
    const CellData& cells = world.mCellData;
    const GenomePoolDiagnostics before = cells.genomePool().diagnostics();
    PoolExport pools = exportPools(
            cells, static_cast<std::size_t>(cells.mCellCount));

    StreamingLogicalSnapshot snapshot;
    snapshot.documentKind = DocumentKind::Substrate;
    snapshot.metadata = substrateMetadata(
            world.mEnvironment,
            cells,
            world.mFoodData,
            world.mDeadSplashData,
            world.mTime,
            pools,
            world.mCreatedAtUnixMillis);
    snapshot.sections.reserve(8);
    snapshot.sections.push_back(
            exportEnvironment<StreamingLogicalSection>(world.mEnvironment));
    snapshot.sections.push_back(
            exportGenes<StreamingLogicalSection>(pools));
    snapshot.sections.push_back(
            exportGenomes<StreamingLogicalSection>(pools));
    snapshot.sections.push_back(
            exportCells<StreamingLogicalSection>(cells, pools));
    snapshot.sections.push_back(exportTopologyEdges<StreamingLogicalSection>(
            cells.mTopology));
    snapshot.sections.push_back(
            exportTopologyBoundaries<StreamingLogicalSection>(
                    cells.mTopology));
    snapshot.sections.push_back(
            exportFood<StreamingLogicalSection>(world.mFoodData));
    snapshot.sections.push_back(exportDeadSplashes<StreamingLogicalSection>(
            world.mDeadSplashData));

    ContainerWriteResult result = writeContainerPath(
            path, snapshot, options, preview);
    const GenomePoolDiagnostics after = cells.genomePool().diagnostics();
    if (after.materializedGenomes != before.materializedGenomes) {
        throw std::logic_error(
                "Release streaming writer materialized a per-cell genome");
    }
    return result;
}

std::unique_ptr<SimulationWorld> decodeReleaseSubstrate(
        const std::vector<std::uint8_t>& bytes,
        bool previewMode) {
    DecodedContainer decoded = decodeContainer(bytes);
    if (decoded.header.documentKind != DocumentKind::Substrate
            || decoded.snapshot.documentKind != DocumentKind::Substrate
            || decoded.snapshot.sections.size() != 8) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidHeader,
                "CLTY document is not a release substrate");
    }
    const SubstrateCard card = readSubstrateCard(
            decoded.snapshot.metadata, true);
    const LogicalSection& environmentSection = requireReleaseSection(
            decoded.snapshot,
            section_id::kEnvironment,
            "environment",
            53);
    const LogicalSection& genes = requireReleaseSection(
            decoded.snapshot, section_id::kGenes, "genes", 87);
    const LogicalSection& genomes = requireReleaseSection(
            decoded.snapshot, section_id::kGenomes, "genomes", 3);
    const LogicalSection& cells = requireReleaseSection(
            decoded.snapshot, section_id::kCells, "cells", 61, 1);
    const LogicalSection& edges = requireReleaseSection(
            decoded.snapshot,
            section_id::kTopologyEdges,
            "topology_edges",
            11,
            1);
    const LogicalSection& boundaries = requireReleaseSection(
            decoded.snapshot,
            section_id::kTopologyBoundaries,
            "topology_boundaries",
            8);
    const LogicalSection& food = requireReleaseSection(
            decoded.snapshot, section_id::kFood, "food", 6);
    const LogicalSection& splashes = requireReleaseSection(
            decoded.snapshot,
            section_id::kDeadSplashes,
            "dead_splashes",
            8);

    Environment environment = decodeReleaseEnvironment(
            environmentSection, previewMode);
    if (card.maximumCellCount
                    != static_cast<std::uint64_t>(environment.maxCellCount)
            || card.maximumFoodCount
                    != static_cast<std::uint64_t>(environment.maxFoodCount)
            || card.dishRadius != environment.mRadius
            || card.dishInnerRadius != environment.mInnerRadius
            || card.cellCount != cells.logicalRowCount
            || card.foodCount != food.logicalRowCount
            || card.cellCount
                    > static_cast<std::uint64_t>(environment.maxCellCount)
            || card.foodCount
                    > static_cast<std::uint64_t>(environment.maxFoodCount)
            || splashes.logicalRowCount
                    > static_cast<std::uint64_t>(
                            environment.maxDeadSplashCount)) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release substrate metadata and section counts disagree");
    }
    if (card.genomeCount.has_value()
            && *card.genomeCount != genomes.logicalRowCount) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release Genome metadata does not match its section");
    }
    if (card.uniqueGeneCount.has_value()
            && *card.uniqueGeneCount != genes.logicalRowCount) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release Gene metadata does not match its section");
    }
    if (card.topologyEdgeCount.has_value()
            && *card.topologyEdgeCount != edges.logicalRowCount) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release topology metadata does not match its section");
    }
    if (card.deadSplashCount.has_value()
            && *card.deadSplashCount != splashes.logicalRowCount) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release dead-splash metadata does not match its section");
    }

    GenomeImport genomeImport = decodeReleaseGenomes(genes, genomes, cells);
    auto world = std::make_unique<SimulationWorld>(previewMode, environment);
    world->mCreatedAtUnixMillis = card.createdAtUnixMillis.value_or(0);
    const double timeSeconds = card.simulationTimeHours * 3600.0;
    cellularity::snapshot_codec::requireFinite(
            timeSeconds, "Release substrate simulation time");
    world->mTime = timeSeconds;
    decodeReleaseCells(cells, world->mCellData, genomeImport);
    restoreAndValidateOrganismIdentity(world->mCellData, card);
    decodeReleaseTopology(edges, boundaries, world->mCellData);
    decodeReleaseFood(food, world->mFoodData);
    decodeReleaseDeadSplashes(splashes, world->mDeadSplashData);
    cellularity::snapshot_codec::validateDecodedWorldDynamicValues(*world);
    if (world->mFoodData.getFoodMass() != card.foodMass) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release Food mass metadata does not match its body");
    }

    const LogicalSnapshot canonical = exportSubstrateRuntimeSoa(*world);
    if (!sameLogicalSections(
                canonical.sections, decoded.snapshot.sections)) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release substrate body is not in canonical deterministic form");
    }
    world->updateGrids();
    return world;
}

std::unique_ptr<SimulationWorld> decodeReleaseSubstratePath(
        const std::string& path,
        bool previewMode) {
    ContainerFileReader reader(path);
    const ReleaseFileSections sections = bindReleaseFileSections(reader);

    // Environment is one row plus the bounded mutation-weight vector, so it is
    // the only section intentionally materialized as a complete logical block.
    const LogicalSection environmentSection = materializeSmallFileSection(
            reader, *sections.environment);
    Environment environment = decodeReleaseEnvironment(
            environmentSection, previewMode);
    if (sections.card.maximumCellCount
                    != static_cast<std::uint64_t>(environment.maxCellCount)
            || sections.card.maximumFoodCount
                    != static_cast<std::uint64_t>(environment.maxFoodCount)
            || sections.card.dishRadius != environment.mRadius
            || sections.card.dishInnerRadius != environment.mInnerRadius
            || sections.card.cellCount
                    > static_cast<std::uint64_t>(environment.maxCellCount)
            || sections.card.foodCount
                    > static_cast<std::uint64_t>(environment.maxFoodCount)
            || sections.splashes->logicalRowCount
                    > static_cast<std::uint64_t>(
                            environment.maxDeadSplashCount)) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidCount,
                "Release substrate metadata and Environment disagree");
    }

    GenomeImport genomeImport = decodeReleaseGenomesFromFile(
            reader,
            *sections.genes,
            *sections.genomes,
            *sections.cells);
    auto world = std::make_unique<SimulationWorld>(previewMode, environment);
    world->mCreatedAtUnixMillis =
            sections.card.createdAtUnixMillis.value_or(0);
    const double timeSeconds = sections.card.simulationTimeHours * 3600.0;
    cellularity::snapshot_codec::requireFinite(
            timeSeconds, "Release substrate simulation time");
    world->mTime = timeSeconds;
    decodeReleaseCellsFromFile(
            reader, *sections.cells, world->mCellData, genomeImport);
    restoreAndValidateOrganismIdentity(
            world->mCellData, sections.card);
    decodeReleaseTopologyFromFile(
            reader,
            *sections.edges,
            *sections.boundaries,
            world->mCellData);
    decodeReleaseFoodFromFile(reader, *sections.food, world->mFoodData);
    decodeReleaseDeadSplashesFromFile(
            reader, *sections.splashes, world->mDeadSplashData);
    cellularity::snapshot_codec::validateDecodedWorldDynamicValues(*world);
    if (world->mFoodData.getFoodMass() != sections.card.foodMass) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidValue,
                "Release Food mass metadata does not match its body");
    }
    world->updateGrids();
    return world;
}

void validateReleaseSubstratePath(const std::string& path) {
    ContainerFileReader reader(path);
    (void)bindReleaseFileSections(reader);
    reader.validateBody();
}

ReleaseSubstrateInspection inspectReleaseSubstratePrefix(
        const std::vector<std::uint8_t>& bytes,
        std::uint64_t completeFileBytes) {
    if (bytes.size() < kBootstrapBytes) {
        failReleaseSubstrate(
                DecodeErrorCode::TruncatedInput,
                "Release substrate bootstrap is truncated");
    }
    const std::vector<std::uint8_t> bootstrap(
            bytes.begin(),
            bytes.begin() + static_cast<std::ptrdiff_t>(kBootstrapBytes));
    ReleaseSubstrateInspection result;
    result.header = decodeBootstrap(bootstrap, completeFileBytes);
    if (result.header.documentKind != DocumentKind::Substrate) {
        failReleaseSubstrate(
                DecodeErrorCode::InvalidHeader,
                "CLTY document is not a release substrate");
    }
    const std::vector<std::uint8_t> metadataBytes = sliceReleaseBytes(
            bytes,
            result.header.metadataOffset,
            result.header.metadataBytes,
            "metadata");
    const std::vector<MetadataEntry> metadata = decodeMetadata(
            DocumentKind::Substrate,
            metadataBytes,
            result.header.metadataCrc32);
    const SubstrateCard card = readSubstrateCard(metadata, false);
    result.contentSchemaMajor = card.schemaMajor;
    result.contentSchemaMinor = card.schemaMinor;
    result.simulationTimeHours = card.simulationTimeHours;
    result.cellCount = card.cellCount;
    result.foodCount = card.foodCount;
    result.foodMass = card.foodMass;
    result.dishRadius = card.dishRadius;
    result.dishInnerRadius = card.dishInnerRadius;
    result.maximumCellCount = card.maximumCellCount;
    result.maximumFoodCount = card.maximumFoodCount;
    result.genomeCount = card.genomeCount.value_or(0);
    result.uniqueGeneCount = card.uniqueGeneCount.value_or(0);
    result.topologyEdgeCount = card.topologyEdgeCount.value_or(0);
    result.deadSplashCount = card.deadSplashCount.value_or(0);
    result.knownOrganismCount = card.knownOrganismCount;
    result.organismIdentityComplete = card.organismIdentityComplete;
    result.createdAtUnixMillis = card.createdAtUnixMillis.value_or(0);
    std::vector<std::uint8_t> preview;
    if (result.header.previewBytes != 0
            && result.header.previewOffset <= bytes.size()
            && result.header.previewBytes
                    <= bytes.size() - result.header.previewOffset) {
        preview = sliceReleaseBytes(
                bytes,
                result.header.previewOffset,
                result.header.previewBytes,
                "preview");
    }
    result.previewDisposition = classifyPreview(result.header, preview);
    return result;
}

ReleaseSubstrateInspection inspectReleaseSubstrate(
        const std::vector<std::uint8_t>& bytes) {
    return inspectReleaseSubstratePrefix(bytes, bytes.size());
}

std::string logicalSnapshotSemanticDigest(
        const LogicalSnapshot& snapshot) {
    cellularity::io::Sha256 digest;
    static constexpr std::array<std::uint8_t, 8> domain{{'C', 'L', 'T', 'Y', 'S', 'E', 'M', 1}};
    digest.update(domain.data(), domain.size());
    updateDigestUnsigned(
            digest, static_cast<std::uint16_t>(snapshot.documentKind));

    std::vector<const MetadataEntry*> metadata;
    metadata.reserve(snapshot.metadata.size());
    for (const MetadataEntry& entry : snapshot.metadata) {
        if (entry.fieldId != metadata_field::kSavedAtUnixMillis
                && entry.fieldId
                        != metadata_field::kSubstrateCreatedAtUnixMillis) {
            metadata.push_back(&entry);
        }
    }
    std::sort(
            metadata.begin(), metadata.end(),
            [](const MetadataEntry* left, const MetadataEntry* right) {
                return left->fieldId < right->fieldId;
            });
    updateDigestUnsigned(
            digest, static_cast<std::uint64_t>(metadata.size()));
    for (const MetadataEntry* entry : metadata) {
        updateDigestUnsigned(digest, entry->fieldId);
        updateDigestUnsigned(
                digest, static_cast<std::uint8_t>(entry->valueType));
        updateDigestUnsigned(
                digest, static_cast<std::uint8_t>(entry->critical));
        updateDigestText(digest, entry->canonicalName);
        updateDigestUnsigned(digest, entry->rawBits);
    }

    std::vector<const LogicalSection*> sections;
    sections.reserve(snapshot.sections.size());
    for (const LogicalSection& section : snapshot.sections) {
        sections.push_back(&section);
    }
    std::sort(
            sections.begin(), sections.end(),
            [](const LogicalSection* left, const LogicalSection* right) {
                return left->sectionId < right->sectionId;
            });
    updateDigestUnsigned(
            digest, static_cast<std::uint64_t>(sections.size()));
    for (const LogicalSection* section : sections) {
        updateDigestUnsigned(digest, section->sectionId);
        updateDigestUnsigned(digest, section->schemaMajor);
        updateDigestUnsigned(digest, section->schemaMinor);
        updateDigestUnsigned(digest, section->logicalRowCount);
        updateDigestText(digest, section->canonicalName);
        std::vector<const LogicalColumn*> columns;
        columns.reserve(section->columns.size());
        for (const LogicalColumn& column : section->columns) {
            columns.push_back(&column);
        }
        std::sort(
                columns.begin(), columns.end(),
                [](const LogicalColumn* left, const LogicalColumn* right) {
                    return left->fieldId < right->fieldId;
                });
        updateDigestUnsigned(
                digest, static_cast<std::uint64_t>(columns.size()));
        for (const LogicalColumn* column : columns) {
            updateDigestUnsigned(digest, column->fieldId);
            updateDigestUnsigned(
                    digest,
                    static_cast<std::uint8_t>(column->primitiveType));
            updateDigestUnsigned(digest, column->logicalValueCount);
            updateDigestText(digest, column->canonicalName);
            updateDigestUnsigned(
                    digest,
                    static_cast<std::uint64_t>(column->decodedBytes.size()));
            digest.update(
                    column->decodedBytes.data(),
                    column->decodedBytes.size());
        }
    }
    return digest.finishHex();
}

std::string substrateSemanticDigest(const SimulationWorld& world) {
    return logicalSnapshotSemanticDigest(exportSubstrateRuntimeSoa(world));
}

} // namespace cellularity::save_format::release
