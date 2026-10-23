#include "core/Gene.h"

#include <algorithm>
#include <cmath>

#include "core/Environment.h"
#include "core/GeneAccess.h"
#include "core/SimulationRandom.h"

namespace {

constexpr float PI_F = 3.14159265358979323846f;
constexpr float SQRT_THREE_F = 1.73205080756887729353f;

struct HsvColor {
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
};

float wrapAngle(float angle) {
    return angle - 2.0f * PI_F * std::floor(angle / (2.0f * PI_F));
}

float reflectFloat(float min, float max, float value) {
    float width = max - min;
    if (width <= 0.0f) {
        return min;
    }

    float reflected = std::fmod(value - min, 2.0f * width);
    if (reflected < 0.0f) {
        reflected += 2.0f * width;
    }
    if (reflected > width) {
        reflected = 2.0f * width - reflected;
    }

    return min + reflected;
}

HsvColor rgbToHsv(const std::array<float, 3>& color) {
    float red = std::clamp(color[0], 0.0f, 1.0f);
    float green = std::clamp(color[1], 0.0f, 1.0f);
    float blue = std::clamp(color[2], 0.0f, 1.0f);
    float maxComponent = std::max({red, green, blue});
    float minComponent = std::min({red, green, blue});
    float chroma = maxComponent - minComponent;

    if (maxComponent == 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }

    float hue = 0.0f;
    if (chroma == 0.0f) {
        return {hue, 0.0f, maxComponent};
    }
    if (maxComponent == red) {
        hue = (green - blue) / chroma;
        if (hue < 0.0f) {
            hue += 6.0f;
        }
    } else if (maxComponent == green) {
        hue = (blue - red) / chroma + 2.0f;
    } else {
        hue = (red - green) / chroma + 4.0f;
    }

    float saturation = chroma / maxComponent;
    return {hue * (PI_F / 3.0f), saturation, maxComponent};
}

std::array<float, 3> hsvToRgb(const HsvColor& color) {
    float hue = wrapAngle(color.hue) * (3.0f / PI_F);
    float saturation = std::clamp(color.saturation, 0.0f, 1.0f);
    float value = std::clamp(color.value, 0.0f, 1.0f);
    if (saturation == 0.0f || value == 0.0f) {
        return {value, value, value};
    }

    int sector = static_cast<int>(std::floor(hue));
    float fraction = hue - static_cast<float>(sector);
    float p = value * (1.0f - saturation);
    float q = value * (1.0f - saturation * fraction);
    float t = value * (1.0f - saturation * (1.0f - fraction));

    switch (sector % 6) {
        case 0:
            return {value, t, p};
        case 1:
            return {q, value, p};
        case 2:
            return {p, value, t};
        case 3:
            return {p, q, value};
        case 4:
            return {t, p, value};
        default:
            return {value, p, q};
    }
}

} // namespace

const std::array<float, GeneData::MAX_PARAMETRIC_VALUES_COUNT>
        GeneData::VALUE_MIN = [] {
            std::array<float, GeneData::MAX_PARAMETRIC_VALUES_COUNT> values{};
            values[GeneData::SWIM_FORCE_VALUE_INDEX] = 0.0f;
            values[GeneData::DENSITY_VALUE_INDEX] = -3.0f;
            values[GeneData::MYOCYTE_BENDING_VALUE_INDEX] = -1.0f;
            values[GeneData::MYOCYTE_CONTRACTION_VALUE_INDEX] = -0.5f;
            values[GeneData::MYOCYTE_LIFTING_VALUE_INDEX] = -1.0f;
            values[GeneData::STEM_THRESHOLD_1_VALUE_INDEX] = -1.0f;
            values[GeneData::STEM_THRESHOLD_2_VALUE_INDEX] = -1.0f;
            values[GeneData::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 0] =
                    -CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT;
            values[GeneData::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 1] =
                    -CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT;
            values[GeneData::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 2] =
                    -CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT;
            values[GeneData::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 3] =
                    -CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT;
            return values;
        }();

const std::array<float, GeneData::MAX_PARAMETRIC_VALUES_COUNT>
        GeneData::VALUE_MAX = [] {
            std::array<float, GeneData::MAX_PARAMETRIC_VALUES_COUNT> values{};
            values[GeneData::SWIM_FORCE_VALUE_INDEX] = CellDataConstants::MAX_SWIM_FORCE;
            values[GeneData::DENSITY_VALUE_INDEX] = 3.0f;
            values[GeneData::MYOCYTE_BENDING_VALUE_INDEX] = 1.0f;
            values[GeneData::MYOCYTE_CONTRACTION_VALUE_INDEX] = 0.5f;
            values[GeneData::MYOCYTE_LIFTING_VALUE_INDEX] = 1.0f;
            values[GeneData::STEM_THRESHOLD_1_VALUE_INDEX] = 1.0f;
            values[GeneData::STEM_THRESHOLD_2_VALUE_INDEX] = 1.0f;
            values[GeneData::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 0] =
                    CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT;
            values[GeneData::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 1] =
                    CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT;
            values[GeneData::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 2] =
                    CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT;
            values[GeneData::GEN_SIGNAL_SUBSTANCE_VALUE_0_INDEX + 3] =
                    CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT;
            return values;
        }();

const std::array<GeneData::FloatDescriptor, GeneData::MAX_FLOATS_COUNT>
        GeneData::FLOAT_DESCRIPTORS{{
                {-3.0f * CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT,
                        3.0f * CellDataConstants::MAX_SIGNAL_SUBSTANCE_OUTPUT},
                {0.0f, 1.0f},
                {0.0f, 1.0f},
                {0.0f, 1.0f},
                {0.0f, SQRT_THREE_F},
                {0.0f, 0.01f},
                {0.5f, 2.0f},
        }};

bool isGameteGeneIndexForMutation(
        cellularity::GenomeView genome,
        int geneIndex) {
    const auto index = cellularity::geneIndexFromBoundary(
            geneIndex, genome.geneCount());
    return index.has_value()
            && genome.gene(*index).cellType() == CellType::GAMETE;
}

int randomGameteGeneIndexForMutation(
        cellularity::GenomeView genome,
        SimulationRandom& random) {
    int gameteGeneCount = 0;
    for (std::size_t geneIndex = 0; geneIndex < genome.geneCount(); geneIndex++) {
        if (genome.gene(geneIndex).cellType() == CellType::GAMETE) {
            gameteGeneCount++;
        }
    }

    if (gameteGeneCount == 0) {
        return random.nextInt(static_cast<int>(genome.geneCount()));
    }

    int selectedGamete = random.nextInt(gameteGeneCount);
    for (std::size_t geneIndex = 0; geneIndex < genome.geneCount(); geneIndex++) {
        if (genome.gene(geneIndex).cellType() != CellType::GAMETE) {
            continue;
        }
        if (selectedGamete == 0) {
            return static_cast<int>(geneIndex);
        }
        selectedGamete--;
    }
    return 0;
}

void markSignalChannelForMutation(std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT>& usedSignals,
        int signalIndex) {
    if (signalIndex >= 0 && signalIndex < CellDataConstants::SIGNAL_SUBSTANCE_COUNT) {
        usedSignals[signalIndex] = true;
    }
}

std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT> collectProducedSignalChannelsForMutation(
        cellularity::GenomeView genome) {
    std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT> usedSignals{};
    for (std::size_t geneIndex = 0; geneIndex < genome.geneCount(); geneIndex++) {
        const cellularity::GeneView gene = genome.gene(geneIndex);
        switch (gene.cellType()) {
            case CellType::SENSEOCYTE:
            case CellType::STEREOCYTE:
                markSignalChannelForMutation(
                        usedSignals,
                        gene.integerValue(Gene::SMELL_OUT_INTEGER_INDEX));
                break;
            case CellType::NEUROCYTE:
                for (int outputIndex = 0; outputIndex < CellDataConstants::NEUROCYTE_OUTPUT_COUNT; outputIndex++) {
                    markSignalChannelForMutation(
                            usedSignals,
                            gene.integerValue(
                                    Gene::GEN_SIGNAL_SUBSTANCE_ID_0_INTEGER_INDEX
                                    + outputIndex));
                }
                break;
            case CellType::SECROCYTE: {
                const int secreteType =
                        gene.integerValue(Gene::SECRETE_TYPE_INTEGER_INDEX);
                if (secreteType >= Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                        && secreteType < Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                                        + CellDataConstants::SIGNAL_SUBSTANCE_COUNT) {
                    markSignalChannelForMutation(
                            usedSignals,
                            secreteType - Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT);
                }
                if (secreteType >= Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                                        + CellDataConstants::SIGNAL_SUBSTANCE_COUNT
                        && secreteType < Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                                        + 2 * CellDataConstants::SIGNAL_SUBSTANCE_COUNT) {
                    markSignalChannelForMutation(
                            usedSignals,
                            secreteType - Gene::SECRETE_NON_SIGNAL_SUBSTANCE_TYPE_COUNT
                                    - CellDataConstants::SIGNAL_SUBSTANCE_COUNT);
                }
                break;
            }
            default:
                break;
        }
    }
    return usedSignals;
}

int randomProducedSignalChannelForMutation(const std::array<bool, CellDataConstants::SIGNAL_SUBSTANCE_COUNT>& usedSignals,
        SimulationRandom& random) {
    int usedCount = 0;
    for (bool used : usedSignals) {
        usedCount += used ? 1 : 0;
    }
    if (usedCount == 0) {
        return -1;
    }

    int selected = random.nextInt(usedCount);
    for (int signalIndex = 0; signalIndex < CellDataConstants::SIGNAL_SUBSTANCE_COUNT; signalIndex++) {
        if (!usedSignals[signalIndex]) {
            continue;
        }
        if (selected == 0) {
            return signalIndex;
        }
        selected--;
    }
    return 0;
}

int randomReceiverSignalInputForMutation(
        cellularity::GenomeView genome,
        SimulationRandom& random) {
    if (random.nextInt(5) == 0) {
        return random.nextInt(CellDataConstants::SIGNAL_SUBSTANCE_COUNT
                + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT);
    }

    auto usedSignals = collectProducedSignalChannelsForMutation(genome);
    int signalIndex = randomProducedSignalChannelForMutation(usedSignals, random);
    if (signalIndex >= 0) {
        return signalIndex;
    }
    return random.nextInt(CellDataConstants::SIGNAL_SUBSTANCE_COUNT
            + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT);
}

bool GeneData::isPointMutationWeightAllowed(int mutationWeightIndex, bool onlyTweakMutations, CellType cellType) {
    if (mutationWeightIndex < 0 || mutationWeightIndex >= Environment::MUTATION_POINT_WEIGHT_COUNT) {
        return false;
    }
    if (cellType != CellType::GAMETE
            && mutationWeightIndex == Environment::MUTATION_INTEGER_WEIGHT_OFFSET + Gene::TELOMERASE_LENGTH_INDEX) {
        return false;
    }
    if (!onlyTweakMutations) {
        return true;
    }
    if (mutationWeightIndex < Environment::MUTATION_CHILD_INDEX_WEIGHT_OFFSET) {
        return true;
    }
    if (mutationWeightIndex >= Environment::MUTATION_PARAMETRIC_WEIGHT_OFFSET
            && mutationWeightIndex < Environment::MUTATION_PARAMETRIC_WEIGHT_OFFSET + Environment::MUTATION_PARAMETRIC_WEIGHT_COUNT) {
        int parametricFieldIndex = mutationWeightIndex - Environment::MUTATION_PARAMETRIC_WEIGHT_OFFSET;
        return parametricFieldIndex % 5 < 3;
    }
    return mutationWeightIndex >= Environment::MUTATION_FLOAT_VALUE_WEIGHT_OFFSET
            && mutationWeightIndex < Environment::MUTATION_FLOAT_VALUE_WEIGHT_OFFSET + Environment::MUTATION_FLOAT_VALUE_WEIGHT_COUNT;
}

int selectPointMutationWeightIndex(Environment& environment, CellType cellType) {
    double totalWeight = 0.0;
    for (int mutationWeightIndex = 0; mutationWeightIndex < Environment::MUTATION_POINT_WEIGHT_COUNT; mutationWeightIndex++) {
        if (!Gene::isPointMutationWeightAllowed(mutationWeightIndex, environment.IsOnlyTweakMutations, cellType)) {
            continue;
        }
        double weight = environment.mMutationWeights[mutationWeightIndex];
        if (std::isfinite(weight) && weight > 0.0) {
            totalWeight += weight;
        }
    }
    if (totalWeight <= 0.0) {
        return -1;
    }

    double selectedWeight = environment.mRandom.nextUniform() * totalWeight;
    for (int mutationWeightIndex = 0; mutationWeightIndex < Environment::MUTATION_POINT_WEIGHT_COUNT; mutationWeightIndex++) {
        if (!Gene::isPointMutationWeightAllowed(mutationWeightIndex, environment.IsOnlyTweakMutations, cellType)) {
            continue;
        }
        double weight = environment.mMutationWeights[mutationWeightIndex];
        if (!std::isfinite(weight) || weight <= 0.0) {
            continue;
        }
        selectedWeight -= weight;
        if (selectedWeight < 0.0) {
            return mutationWeightIndex;
        }
    }
    return -1;
}

float GeneData::floatMinimum(FloatType type) noexcept {
    return FLOAT_DESCRIPTORS[static_cast<std::size_t>(type)].minimum;
}

float GeneData::floatMaximum(FloatType type) noexcept {
    return FLOAT_DESCRIPTORS[static_cast<std::size_t>(type)].maximum;
}

float& GeneData::floatStorage(FloatType type) noexcept {
    return mFloatValues[static_cast<std::size_t>(type)];
}

const float& GeneData::floatStorage(FloatType type) const noexcept {
    return mFloatValues[static_cast<std::size_t>(type)];
}

float GeneData::mutateFloat(float min, float max, float old, SimulationRandom& random, Environment& environment) {
    float delta = (max - min) * static_cast<float>(random.nextGaussian()) * static_cast<float>(environment.mTestMutationCoef);
    return reflectFloat(min, max, old + delta);
}

float GeneData::mutateAngle(float old, SimulationRandom& random, Environment& environment) {
    float delta = 2.0f * PI_F * static_cast<float>(random.nextGaussian()) * static_cast<float>(environment.mTestMutationCoef);
    return wrapAngle(old + delta);
}

float GeneData::mutateSplitMass(float old, SimulationRandom& random, Environment& environment) {
    constexpr float min = CellDataConstants::MIN_SPLIT_MASS;
    constexpr float noSplit = CellDataConstants::MAX_SPLIT_MASS;
    const float splitLimit = static_cast<float>(CellDataConstants::MAX_MASS);
    const float highestSplittable = std::nextafter(splitLimit, min);
    const float width = splitLimit - min;

    if (old >= splitLimit) {
        return highestSplittable;
    }

    float delta = width * static_cast<float>(random.nextGaussian()) * static_cast<float>(environment.mTestMutationCoef);
    float result = old + delta;
    if (result >= splitLimit) {
        return noSplit;
    }
    return reflectFloat(min, highestSplittable, result);
}

int GeneData::mutateInteger(int old, int max, SimulationRandom& random) {
    if (max <= 1) {
        return 0;
    }

    int value = old;
    if (value < 0) {
        value = 0;
    }
    if (value >= max) {
        value = max - 1;
    }

    if (value == 0) {
        return 1;
    }
    if (value == max - 1) {
        return max - 2;
    }
    return value + (random.nextInt(2) == 0 ? -1 : 1);
}

void GeneData::mutateColor(int colorFieldIndex, SimulationRandom& random, Environment& environment) {
    HsvColor color = rgbToHsv(mColor);
    switch (colorFieldIndex) {
        case 0:
            color.hue = mutateAngle(color.hue, random, environment);
            break;
        case 1:
            color.saturation = mutateFloat(0.0f, 1.0f, color.saturation, random, environment);
            break;
        case 2:
            color.value = mutateFloat(0.0f, 1.0f, color.value, random, environment);
            break;
    }
    mColor = hsvToRgb(color);
}

void GeneData::mutateSmellColor(int colorFieldIndex, SimulationRandom& random, Environment& environment) {
    std::array<float, 3> smellColor = {
            floatStorage(FloatType::SMELL_RED),
            floatStorage(FloatType::SMELL_GREEN),
            floatStorage(FloatType::SMELL_BLUE)
    };
    HsvColor color = rgbToHsv(smellColor);
    switch (colorFieldIndex) {
        case 0:
            color.hue = mutateAngle(color.hue, random, environment);
            break;
        case 1:
            color.saturation = mutateFloat(0.0f, 1.0f, color.saturation, random, environment);
            break;
        case 2:
            color.value = mutateFloat(0.0f, 1.0f, color.value, random, environment);
            break;
    }

    smellColor = hsvToRgb(color);
    floatStorage(FloatType::SMELL_RED) = smellColor[0];
    floatStorage(FloatType::SMELL_GREEN) = smellColor[1];
    floatStorage(FloatType::SMELL_BLUE) = smellColor[2];
}

void GeneData::mutate(
        Environment& environment,
        const cellularity::GenomeView* genome) {
    double totalWeight = 0.0;
    for (int mutationWeightIndex = 0; mutationWeightIndex < Environment::MUTATION_WEIGHT_COUNT; mutationWeightIndex++) {
        bool allowed = isPointMutationWeightAllowed(
                mutationWeightIndex,
                environment.IsOnlyTweakMutations,
                static_cast<CellType>(mCellTypeOrdinal));
        if (!allowed) {
            continue;
        }
        double weight = environment.mMutationWeights[mutationWeightIndex];
        if (std::isfinite(weight) && weight > 0.0) {
            totalWeight += weight;
        }
    }
    if (totalWeight <= 0.0) {
        return;
    }

    double selectedWeight = environment.mRandom.nextUniform() * totalWeight;
    for (int mutationWeightIndex = 0; mutationWeightIndex < Environment::MUTATION_WEIGHT_COUNT; mutationWeightIndex++) {
        bool allowed = isPointMutationWeightAllowed(
                mutationWeightIndex,
                environment.IsOnlyTweakMutations,
                static_cast<CellType>(mCellTypeOrdinal));
        if (!allowed) {
            continue;
        }
        double weight = environment.mMutationWeights[mutationWeightIndex];
        if (!std::isfinite(weight) || weight <= 0.0) {
            continue;
        }
        selectedWeight -= weight;
        if (selectedWeight >= 0.0) {
            continue;
        }
        mutatePointByWeightIndex(environment, mutationWeightIndex, genome);
        return;
    }
}

bool GeneData::mutatePointByWeightIndex(Environment& environment,
        int mutationWeightIndex,
        const cellularity::GenomeView* genome) {
    if (!isPointMutationWeightAllowed(
                mutationWeightIndex,
                environment.IsOnlyTweakMutations,
                static_cast<CellType>(mCellTypeOrdinal))) {
        return false;
    }

    auto mutateContinuousField = [&](int continuousFieldIndex) {
        switch (continuousFieldIndex) {
            case 0:
                mSplitAngle = mutateAngle(mSplitAngle, environment.mRandom, environment);
                break;
            case 1:
                mChild1Angle = mutateAngle(mChild1Angle, environment.mRandom, environment);
                break;
            case 2:
                mChild2Angle = mutateAngle(mChild2Angle, environment.mRandom, environment);
                break;
            case 3:
                mMassFlowPriority = mutateFloat(CellDataConstants::MIN_MASS_FLOW_PRIORITY, CellDataConstants::MAX_MASS_FLOW_PRIORITY, mMassFlowPriority, environment.mRandom, environment);
                break;
            case 4:
                mSplitMass = mutateSplitMass(mSplitMass, environment.mRandom, environment);
                break;
            case 5:
                mSplitRatio = mutateFloat(CellDataConstants::MIN_RATIO, 1.0f - CellDataConstants::MIN_RATIO, mSplitRatio, environment.mRandom, environment);
                break;
            case 6:
                mLinkStrength = mutateFloat(0.0f, CellDataConstants::MAX_LINK_STIFFNESS, mLinkStrength, environment.mRandom, environment);
                break;
        }
    };
    auto mutateFloatValueField = [&](int floatValueFieldIndex) {
        const FloatType type = static_cast<FloatType>(floatValueFieldIndex);
        switch (type) {
            case FloatType::SMELL_AMPLITUDE:
                floatStorage(type) = mutateFloat(
                        floatMinimum(type),
                        floatMaximum(type),
                        floatStorage(type),
                        environment.mRandom,
                        environment);
                break;
            case FloatType::SMELL_RED:
                mutateSmellColor(0, environment.mRandom, environment);
                break;
            case FloatType::SMELL_GREEN:
                mutateSmellColor(1, environment.mRandom, environment);
                break;
            case FloatType::SMELL_BLUE:
                mutateSmellColor(2, environment.mRandom, environment);
                break;
            case FloatType::SMELL_THRESHOLD:
            case FloatType::ADHESIN_LENGTH:
            case FloatType::CYTOSKELETON:
                floatStorage(type) = mutateFloat(
                        floatMinimum(type),
                        floatMaximum(type),
                        floatStorage(type),
                        environment.mRandom,
                        environment);
                break;
        }
    };

    if (mutationWeightIndex < Environment::MUTATION_COLOR_WEIGHT_OFFSET + Environment::MUTATION_COLOR_WEIGHT_COUNT) {
        mutateColor(mutationWeightIndex - Environment::MUTATION_COLOR_WEIGHT_OFFSET, environment.mRandom, environment);
        return true;
    }

    if (mutationWeightIndex < Environment::MUTATION_CONTINUOUS_WEIGHT_OFFSET + Environment::MUTATION_CONTINUOUS_WEIGHT_COUNT) {
        mutateContinuousField(mutationWeightIndex - Environment::MUTATION_CONTINUOUS_WEIGHT_OFFSET);
        return true;
    }

    if (mutationWeightIndex < Environment::MUTATION_CHILD_INDEX_WEIGHT_OFFSET + Environment::MUTATION_CHILD_INDEX_WEIGHT_COUNT) {
        const int geneCount = genome == nullptr
                ? CellDataConstants::MAX_GENES_COUNT
                : static_cast<int>(genome->geneCount());
        int childIndexField = mutationWeightIndex - Environment::MUTATION_CHILD_INDEX_WEIGHT_OFFSET;
        if (childIndexField == 0) {
            mChild1GeneIndex = static_cast<std::uint16_t>(
                    environment.mRandom.nextInt(geneCount));
        } else {
            mChild2GeneIndex = static_cast<std::uint16_t>(
                    environment.mRandom.nextInt(geneCount));
        }
        return true;
    }

    if (mutationWeightIndex < Environment::MUTATION_BOOLEAN_WEIGHT_OFFSET + Environment::MUTATION_BOOLEAN_WEIGHT_COUNT) {
        switch (mutationWeightIndex - Environment::MUTATION_BOOLEAN_WEIGHT_OFFSET) {
            case 0:
                setFlag(MAKES_LINK, !flag(MAKES_LINK));
                break;
            case 1:
                setFlag(CHILD_1_KEEPS_LINKS, !flag(CHILD_1_KEEPS_LINKS));
                break;
            case 2:
                setFlag(CHILD_2_KEEPS_LINKS, !flag(CHILD_2_KEEPS_LINKS));
                break;
            case 3:
                setFlag(STAYS_ALIVE, !flag(STAYS_ALIVE));
                break;
            case 4:
                setFlag(CHILD_1_MIRRORED, !flag(CHILD_1_MIRRORED));
                break;
            case 5:
                setFlag(CHILD_2_MIRRORED, !flag(CHILD_2_MIRRORED));
                break;
        }
        return true;
    }

    if (mutationWeightIndex < Environment::MUTATION_CELL_TYPE_WEIGHT_OFFSET + Environment::MUTATION_CELL_TYPE_WEIGHT_COUNT) {
        mutateCellType(environment);
        if (genome != nullptr
                && static_cast<CellType>(mCellTypeOrdinal) == CellType::GAMETE
                && !isGameteGeneIndexForMutation(
                        *genome,
                        mInteger[VIRUS_TO_INTEGER_INDEX])) {
            mInteger[VIRUS_TO_INTEGER_INDEX] = static_cast<std::uint16_t>(
                    randomGameteGeneIndexForMutation(
                            *genome,
                            environment.mRandom));
        }
        return true;
    }

    if (mutationWeightIndex < Environment::MUTATION_PARAMETRIC_WEIGHT_OFFSET + Environment::MUTATION_PARAMETRIC_WEIGHT_COUNT) {
        int parametricFieldIndex = mutationWeightIndex - Environment::MUTATION_PARAMETRIC_WEIGHT_OFFSET;
        ParametricValue& parametricValue = mParametricValue[parametricFieldIndex / 5];
        switch (parametricFieldIndex % 5) {
            case 0:
                parametricValue.mVariableA = mutateFloat(-CellDataConstants::VALUE_VARIABLE_MAX, CellDataConstants::VALUE_VARIABLE_MAX, parametricValue.mVariableA, environment.mRandom, environment);
                break;
            case 1:
                parametricValue.mVariableB = mutateFloat(-CellDataConstants::VALUE_VARIABLE_MAX, CellDataConstants::VALUE_VARIABLE_MAX, parametricValue.mVariableB, environment.mRandom, environment);
                break;
            case 2:
                parametricValue.mVariableC = mutateFloat(-CellDataConstants::VALUE_VARIABLE_MAX, CellDataConstants::VALUE_VARIABLE_MAX, parametricValue.mVariableC, environment.mRandom, environment);
                break;
            case 3:
                if (genome != nullptr) {
                    parametricValue.mSignalSubstanceChannelIndex =
                            static_cast<int16_t>(randomReceiverSignalInputForMutation(
                                    *genome,
                                    environment.mRandom));
                } else {
                    parametricValue.mSignalSubstanceChannelIndex =
                            static_cast<int16_t>(environment.mRandom.nextInt(CellDataConstants::SIGNAL_SUBSTANCE_COUNT + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT));
                }
                break;
            case 4:
                parametricValue.mFormulaType = static_cast<int16_t>(environment.mRandom.nextInt(4));
                break;
        }
        return true;
    }

    if (mutationWeightIndex < Environment::MUTATION_INTEGER_WEIGHT_OFFSET + Environment::MUTATION_INTEGER_WEIGHT_COUNT) {
        int integerIndex = mutationWeightIndex - Environment::MUTATION_INTEGER_WEIGHT_OFFSET;
        if (genome != nullptr
                && static_cast<CellType>(mCellTypeOrdinal) == CellType::GAMETE
                && integerIndex == VIRUS_TO_INTEGER_INDEX) {
            mInteger[integerIndex] = static_cast<std::uint16_t>(
                    randomGameteGeneIndexForMutation(
                            *genome,
                            environment.mRandom));
            return true;
        }
        const bool isGenomeReference =
                integerIndex == VIRUS_FROM_INTEGER_INDEX
                || integerIndex == VIRUS_TO_INTEGER_INDEX;
        const int integerMaximum = isGenomeReference
                ? genome == nullptr
                        ? CellDataConstants::MAX_GENES_COUNT
                        : static_cast<int>(genome->geneCount())
                : INTEGER_MAX[integerIndex];
        mInteger[integerIndex] = static_cast<std::uint16_t>(mutateInteger(
                mInteger[integerIndex],
                integerMaximum,
                environment.mRandom));
        return true;
    }

    mutateFloatValueField(mutationWeightIndex - Environment::MUTATION_FLOAT_VALUE_WEIGHT_OFFSET);
    return true;
}

void GeneData::mutateCellType(Environment& environment) {
    int allowedCellTypesCount = 0;
    for (bool isAllowed : environment.mGenePool) {
        if (isAllowed) {
            allowedCellTypesCount++;
        }
    }
    if (allowedCellTypesCount == 0) {
        return;
    }

    int allowedCellTypeIndex = environment.mRandom.nextInt(allowedCellTypesCount);
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length; cellTypeIndex++) {
        if (!environment.mGenePool[cellTypeIndex]) {
            continue;
        }
        if (allowedCellTypeIndex == 0) {
            mCellTypeOrdinal = static_cast<std::uint8_t>(cellTypeIndex);
            return;
        }
        allowedCellTypeIndex--;
    }
}

void GeneData::fix(std::size_t geneCount) {
    const int referenceUpperBound = static_cast<int>(
            cellularity::isSupportedGeneCount(geneCount)
                    ? geneCount
                    : static_cast<std::size_t>(
                              CellDataConstants::MAX_GENES_COUNT));
    mSplitAngle = wrapAngle(mSplitAngle);
    mChild1Angle = wrapAngle(mChild1Angle);
    mChild2Angle = wrapAngle(mChild2Angle);

    for (std::size_t index = 0; index < mFloatValues.size(); ++index) {
        mFloatValues[index] = std::clamp(
                mFloatValues[index],
                FLOAT_DESCRIPTORS[index].minimum,
                FLOAT_DESCRIPTORS[index].maximum);
    }

    for (int i = 0; i < MAX_INTEGERS_COUNT; i++) {
        const int upperBound =
                i == VIRUS_FROM_INTEGER_INDEX
                        || i == VIRUS_TO_INTEGER_INDEX
                ? referenceUpperBound
                : INTEGER_MAX[i];
        if (mInteger[i] >= upperBound) {
            mInteger[i] = static_cast<std::uint16_t>(upperBound - 1);
        }
    }
    for (int i = 0; i < MAX_PARAMETRIC_VALUES_COUNT; i++) {
        mParametricValue[i].mVariableA = std::clamp(mParametricValue[i].mVariableA, -CellDataConstants::VALUE_VARIABLE_MAX, CellDataConstants::VALUE_VARIABLE_MAX);
        mParametricValue[i].mVariableB = std::clamp(mParametricValue[i].mVariableB, -CellDataConstants::VALUE_VARIABLE_MAX, CellDataConstants::VALUE_VARIABLE_MAX);
        mParametricValue[i].mVariableC = std::clamp(mParametricValue[i].mVariableC, -CellDataConstants::VALUE_VARIABLE_MAX, CellDataConstants::VALUE_VARIABLE_MAX);

        if (mParametricValue[i].mFormulaType < 0) {
            mParametricValue[i].mFormulaType = 0;
        }
        if (mParametricValue[i].mFormulaType > 3) {
            mParametricValue[i].mFormulaType = 3;
        }
        if (mParametricValue[i].mSignalSubstanceChannelIndex < 0) {
            mParametricValue[i].mSignalSubstanceChannelIndex = 0;
        }
        if (mParametricValue[i].mSignalSubstanceChannelIndex >= CellDataConstants::SIGNAL_SUBSTANCE_COUNT + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT) {
            mParametricValue[i].mSignalSubstanceChannelIndex = CellDataConstants::SIGNAL_SUBSTANCE_COUNT + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT - 1;
        }
    }

    if (mSplitRatio < CellDataConstants::MIN_RATIO) {
        mSplitRatio = CellDataConstants::MIN_RATIO;
    }
    if (mSplitRatio > 1.0f - CellDataConstants::MIN_RATIO) {
        mSplitRatio = 1.0f - CellDataConstants::MIN_RATIO;
    }
    if (mMassFlowPriority > CellDataConstants::MAX_MASS_FLOW_PRIORITY) {
        mMassFlowPriority = CellDataConstants::MAX_MASS_FLOW_PRIORITY;
    }
    if (mMassFlowPriority < CellDataConstants::MIN_MASS_FLOW_PRIORITY) {
        mMassFlowPriority = CellDataConstants::MIN_MASS_FLOW_PRIORITY;
    }
}

bool GeneData::isValidGeneValues() const {
    if (mMassFlowPriority > CellDataConstants::MAX_MASS_FLOW_PRIORITY) {
        return false;
    }
    if (mMassFlowPriority < CellDataConstants::MIN_MASS_FLOW_PRIORITY) {
        return false;
    }
    if (mSplitRatio < CellDataConstants::MIN_RATIO) {
        return false;
    }
    return !(mSplitRatio > 1.0f - CellDataConstants::MIN_RATIO);
}
