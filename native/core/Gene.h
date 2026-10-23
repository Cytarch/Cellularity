#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "core/CellDataConstants.h"
#include "core/CellType.h"
#include "core/GenomeIdentity.h"

class Environment;
class SimulationRandom;

namespace cellularity {
class GeneBuilder;
class GeneParametricValueView;
class GeneView;
class GenomeView;
} // namespace cellularity

/**
 * Dense runtime representation of one gene.
 *
 * Every floating-point field retains its original 32-bit representation. The
 * compact layout only removes repeated schema metadata and narrows validated
 * discrete domains. Consumers must use GeneView/GeneBuilder rather than rely
 * on this backing layout.
 */
class GeneData {
public:
    enum class FloatType : std::uint8_t {
        SMELL_AMPLITUDE = 0,
        SMELL_RED = 1,
        SMELL_GREEN = 2,
        SMELL_BLUE = 3,
        SMELL_THRESHOLD = 4,
        ADHESIN_LENGTH = 5,
        CYTOSKELETON = 6
    };

    struct FloatDescriptor final {
        float minimum;
        float maximum;
    };

    struct ParametricValue final {
        float mVariableA = 0.0f;
        float mVariableB = 0.0f;
        float mVariableC = 0.0f;
        std::int16_t mSignalSubstanceChannelIndex = 0;
        std::int16_t mFormulaType = 0;
    };

    static constexpr int SWIM_FORCE_VALUE_INDEX = 0;
    static constexpr int DENSITY_VALUE_INDEX = 1;
    static constexpr int MYOCYTE_BENDING_VALUE_INDEX = 2;
    static constexpr int MYOCYTE_CONTRACTION_VALUE_INDEX = 3;
    static constexpr int MYOCYTE_LIFTING_VALUE_INDEX = 4;
    static constexpr int STEM_THRESHOLD_1_VALUE_INDEX = 5;
    static constexpr int STEM_THRESHOLD_2_VALUE_INDEX = 6;
    static constexpr int GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX = 7;
    static constexpr int VIRUS_FROM_INTEGER_INDEX = 0;
    static constexpr int VIRUS_TO_INTEGER_INDEX = 1;
    static constexpr int SMELL_OUT_INTEGER_INDEX = 2;
    static constexpr int SMELL_TYPE_INTEGER_INDEX = 3;
    static constexpr int SECRETE_TYPE_INTEGER_INDEX = 4;
    static constexpr int GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX = 5;
    static constexpr int MAX_CONNECTION_COUNT_INDEX =
            GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX
            + CellDataConstants::NEUROCYTE_OUTPUT_COUNT;
    static constexpr int TELOMERASE_LENGTH_INDEX = MAX_CONNECTION_COUNT_INDEX + 1;
    static constexpr int SECRETE_TYPE_FOOD_SMELL = 0;
    static constexpr int SECRETE_TYPE_CYANIDE = 1;
    static constexpr int SECRETE_TYPE_LIPASE = 2;
    static constexpr int SECRETE_TYPE_PROTEASE = 3;
    static constexpr int SECRETE_TYPE_COATED_FOOD_SMELL = 4;
    static constexpr int SECRETE_TYPE_LIGHT_SMELL = 5;
    static constexpr int SECRETE_TYPE_WALL_SMELL = 6;
    static constexpr int SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT = 7;

    static constexpr int MAX_PARAMETRIC_VALUES_COUNT = 11;
    static constexpr int MAX_INTEGERS_COUNT =
            2 + 2 + 1 + CellDataConstants::NEUROCYTE_OUTPUT_COUNT + 1 + 1;
    static constexpr int MAX_FLOATS_COUNT = 7;
    static constexpr std::array<int, 6> TELOMERASE_LENGTHS =
            {-1, 5, 10, 20, 40, 80};

    inline static constexpr std::array<int, MAX_INTEGERS_COUNT> INTEGER_MAX = [] {
        std::array<int, MAX_INTEGERS_COUNT> values{};
        values[VIRUS_FROM_INTEGER_INDEX] =
                static_cast<int>(cellularity::MAX_SUPPORTED_GENES_PER_GENOME);
        values[VIRUS_TO_INTEGER_INDEX] =
                static_cast<int>(cellularity::MAX_SUPPORTED_GENES_PER_GENOME);
        values[SMELL_OUT_INTEGER_INDEX] = CellDataConstants::SIGNAL_SUBSTANCE_COUNT;
        values[SMELL_TYPE_INTEGER_INDEX] = 6;
        values[SECRETE_TYPE_INTEGER_INDEX] =
                SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                + 2 * CellDataConstants::SIGNAL_SUBSTANCE_COUNT;
        for (int output = 0; output < CellDataConstants::NEUROCYTE_OUTPUT_COUNT;
                ++output) {
            values[GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX + output] =
                    CellDataConstants::SIGNAL_SUBSTANCE_COUNT;
        }
        values[MAX_CONNECTION_COUNT_INDEX] = CellDataConstants::MAX_NEIGHBORS;
        values[TELOMERASE_LENGTH_INDEX] =
                static_cast<int>(TELOMERASE_LENGTHS.size());
        return values;
    }();

    static const std::array<float, MAX_PARAMETRIC_VALUES_COUNT> VALUE_MIN;
    static const std::array<float, MAX_PARAMETRIC_VALUES_COUNT> VALUE_MAX;
    static const std::array<FloatDescriptor, MAX_FLOATS_COUNT> FLOAT_DESCRIPTORS;

    [[nodiscard]] static float floatMinimum(FloatType type) noexcept;
    [[nodiscard]] static float floatMaximum(FloatType type) noexcept;

    GeneData() = default;
    GeneData(const GeneData&) = default;
    GeneData(GeneData&&) noexcept = default;
    GeneData& operator=(const GeneData&) = default;
    GeneData& operator=(GeneData&&) noexcept = default;

    static bool isPointMutationWeightAllowed(
            int mutationWeightIndex,
            bool onlyTweakMutations,
            CellType cellType);

private:
    enum Flag : std::uint8_t {
        MAKES_LINK = 1U << 0U,
        CHILD_1_KEEPS_LINKS = 1U << 1U,
        CHILD_2_KEEPS_LINKS = 1U << 2U,
        STAYS_ALIVE = 1U << 3U,
        INITIAL_COMPATIBILITY = 1U << 4U,
        CHILD_1_MIRRORED = 1U << 5U,
        CHILD_2_MIRRORED = 1U << 6U
    };

    [[nodiscard]] constexpr bool flag(Flag value) const noexcept {
        return (mFlags & static_cast<std::uint8_t>(value)) != 0U;
    }

    constexpr void setFlag(Flag value, bool enabled) noexcept {
        const std::uint8_t mask = static_cast<std::uint8_t>(value);
        mFlags = enabled
                ? static_cast<std::uint8_t>(mFlags | mask)
                : static_cast<std::uint8_t>(
                          mFlags & static_cast<std::uint8_t>(~mask));
    }

    [[nodiscard]] float& floatStorage(FloatType type) noexcept;
    [[nodiscard]] const float& floatStorage(FloatType type) const noexcept;

    void mutate(
            Environment& environment,
            const cellularity::GenomeView* genome = nullptr);
    bool mutatePointByWeightIndex(
            Environment& environment,
            int mutationWeightIndex,
            const cellularity::GenomeView* genome = nullptr);
    void fix(
            std::size_t geneCount =
                    CellDataConstants::MAX_GENES_COUNT);
    [[nodiscard]] bool isValidGeneValues() const;

    static float mutateFloat(
            float min,
            float max,
            float old,
            SimulationRandom& random,
            Environment& environment);
    static float mutateAngle(
            float old,
            SimulationRandom& random,
            Environment& environment);
    static float mutateSplitMass(
            float old,
            SimulationRandom& random,
            Environment& environment);
    static int mutateInteger(int old, int max, SimulationRandom& random);
    void mutateColor(
            int colorFieldIndex,
            SimulationRandom& random,
            Environment& environment);
    void mutateSmellColor(
            int colorFieldIndex,
            SimulationRandom& random,
            Environment& environment);
    void mutateCellType(Environment& environment);

    // Keep the hot indexed parametric records dense and directly addressable.
    std::array<ParametricValue, MAX_PARAMETRIC_VALUES_COUNT> mParametricValue{};

    std::array<float, 3> mColor{};
    float mSplitMass = 0.0f;
    float mSplitRatio = 0.0f;
    float mSplitAngle = 0.0f;
    float mChild1Angle = 0.0f;
    float mChild2Angle = 0.0f;
    float mMassFlowPriority = 0.0f;
    float mLinkStrength = 0.0f;
    std::array<float, MAX_FLOATS_COUNT> mFloatValues{};

    std::array<std::uint16_t, MAX_INTEGERS_COUNT> mInteger{};
    std::uint16_t mChild1GeneIndex = 0;
    std::uint16_t mChild2GeneIndex = 0;
    std::uint8_t mFlags = 0;
    std::uint8_t mCellTypeOrdinal = 0;

    friend class cellularity::GeneBuilder;
    friend class cellularity::GeneParametricValueView;
    friend class cellularity::GeneView;
};

// Temporary source-compatibility name while later stages migrate public APIs.
using Gene = GeneData;

constexpr bool geneIntegerDomainsFitCompactStorage() noexcept {
    for (int upperBound : GeneData::INTEGER_MAX) {
        if (upperBound <= 0
                || upperBound - 1
                        > static_cast<int>(
                                std::numeric_limits<std::uint16_t>::max())) {
            return false;
        }
    }
    return true;
}

static_assert(geneIntegerDomainsFitCompactStorage());
static_assert(cellularity::MAX_SUPPORTED_GENES_PER_GENOME
        <= static_cast<int>(std::numeric_limits<std::uint16_t>::max()));
static_assert(CellTypes::length - 1
        <= static_cast<int>(std::numeric_limits<std::uint8_t>::max()));
static_assert(CellDataConstants::SIGNAL_SUBSTANCE_COUNT
                + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT - 1
        <= static_cast<int>(std::numeric_limits<std::int16_t>::max()));
static_assert(sizeof(GeneData::FloatType) == sizeof(std::uint8_t));
static_assert(sizeof(GeneData::FloatDescriptor) == 2 * sizeof(float));
static_assert(sizeof(GeneData::ParametricValue) == 16);
static_assert(alignof(GeneData) == alignof(float));
static_assert(sizeof(GeneData) == 272);
static_assert(sizeof(GeneData) * CellDataConstants::MAX_GENES_COUNT == 10880);
static_assert(std::is_standard_layout<GeneData>::value);
static_assert(std::is_trivially_copyable<GeneData>::value);
