#include "core/CanonicalGene.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include "core/CellDataConstants.h"
#include "core/CellType.h"
#include "core/GeneAccess.h"

namespace cellularity {
namespace {

constexpr float TWO_PI = 6.28318530717958647692f;
constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(sizeof(int) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);

CanonicalGeneResult failure(CanonicalGeneError error, int fieldIndex = -1) {
    return {std::nullopt, {error, fieldIndex}};
}

CanonicalGeneValidation normalizeFloat(
        float& value,
        float minimum,
        float maximum,
        int fieldIndex = -1) {
    if (!std::isfinite(value)) {
        return {CanonicalGeneError::NonFiniteFloat, fieldIndex};
    }
    if (value < minimum || value > maximum) {
        return {CanonicalGeneError::FloatOutOfRange, fieldIndex};
    }
    if (value == 0.0f) {
        value = 0.0f;
    }
    return {};
}

CanonicalGeneValidation normalizeAngle(float& value, int fieldIndex) {
    return normalizeFloat(value, 0.0f, TWO_PI, fieldIndex);
}

CanonicalGeneValidation normalizeReference(
        int& rawReference,
        GeneIndex owner,
        std::size_t geneCount,
        int fieldIndex) {
    const auto reference = geneReferenceFromBoundary(rawReference, geneCount);
    if (!reference.has_value()) {
        return {CanonicalGeneError::InvalidGeneReference, fieldIndex};
    }
    rawReference = (*reference == owner ? SELF_GENE_INDEX : *reference).value();
    return {};
}

bool sameFloat(float left, float right) noexcept {
    std::uint32_t leftBits = 0;
    std::uint32_t rightBits = 0;
    static_assert(sizeof(leftBits) == sizeof(left));
    std::memcpy(&leftBits, &left, sizeof(left));
    std::memcpy(&rightBits, &right, sizeof(right));
    return leftBits == rightBits;
}

class CanonicalHashWriter final {
public:
    void appendByte(std::uint8_t value) noexcept {
        hash_ ^= value;
        hash_ *= FNV_PRIME;
    }

    void appendBool(bool value) noexcept {
        appendByte(value ? 1U : 0U);
    }

    void appendU16(std::uint16_t value) noexcept {
        appendByte(static_cast<std::uint8_t>(value));
        appendByte(static_cast<std::uint8_t>(value >> 8U));
    }

    void appendU32(std::uint32_t value) noexcept {
        appendByte(static_cast<std::uint8_t>(value));
        appendByte(static_cast<std::uint8_t>(value >> 8U));
        appendByte(static_cast<std::uint8_t>(value >> 16U));
        appendByte(static_cast<std::uint8_t>(value >> 24U));
    }

    void appendInt(int value) noexcept {
        appendU32(static_cast<std::uint32_t>(value));
    }

    void appendFloat(float value) noexcept {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(value));
        appendU32(bits);
    }

    [[nodiscard]] std::uint64_t value() const noexcept {
        return hash_;
    }

private:
    std::uint64_t hash_ = FNV_OFFSET_BASIS;
};

void appendReference(CanonicalHashWriter& writer, GeneIndex reference) noexcept {
    writer.appendU16(reference.value());
}

GeneData explicitCanonicalDefaults() {
    GeneData gene;
    GeneBuilder builder(gene);
    for (int channel = 0; channel < 3; ++channel) {
        builder.setColor(channel, 0.0f);
    }
    builder.setSplitMass(
            static_cast<float>(CellDataConstants::MIN_MASS) * 3.5f);
    builder.setSplitRatio(0.5f);
    builder.setSplitAngle(0.0f);
    builder.setChild1Angle(0.0f);
    builder.setChild2Angle(0.0f);
    builder.setMassFlowPriority(1.0f);
    builder.setLinkStrength(0.0f);

    builder.setChild1GeneIndex(SELF_GENE_INDEX);
    builder.setChild2GeneIndex(SELF_GENE_INDEX);

    builder.setMakesLink(false);
    builder.setChild1KeepsLinks(true);
    builder.setChild2KeepsLinks(true);
    builder.setStaysAlive(true);
    builder.setInitialCompatibilityFlag(false);
    builder.setChild1Mirrored(false);
    builder.setChild2Mirrored(false);
    builder.setCellType(CellType::PHAGOCYTE);

    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; ++index) {
        builder.setParametricVariableA(index, 0.0f);
        builder.setParametricVariableB(index, 0.0f);
        builder.setParametricVariableC(index, 0.0f);
        builder.setParametricSignalChannel(index, 0);
        builder.setParametricFormulaType(index, 0);
    }

    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; ++index) {
        builder.setIntegerValue(index, 0);
    }
    builder.setVirusFromGeneIndex(SELF_GENE_INDEX);
    builder.setVirusToGeneIndex(SELF_GENE_INDEX);
    builder.setIntegerValue(
            Gene::MAX_CONNECTION_COUNT_INDEX,
            Gene::INTEGER_MAX[Gene::MAX_CONNECTION_COUNT_INDEX] - 1);

    for (int index = 0; index < Gene::MAX_FLOATS_COUNT; ++index) {
        builder.setFloatValue(static_cast<Gene::FloatType>(index), 0.0f);
    }
    builder.setFloatValue(Gene::FloatType::CYTOSKELETON, 1.0f);
    return gene;
}

} // namespace

CanonicalGeneResult canonicalizeGene(
        const GeneData& source,
        GeneIndex owner,
        std::size_t geneCount) {
    if (!isSupportedGeneCount(geneCount)) {
        return failure(CanonicalGeneError::InvalidGeneCount);
    }
    if (!isGeneIndexInBounds(owner, geneCount)) {
        return failure(CanonicalGeneError::InvalidOwnerIndex);
    }

    GeneData value(source);
    GeneBuilder builder(value);
    const GeneView view(value);

    const auto normalizeAndWriteFloat = [&](
                                                float current,
                                                float minimum,
                                                float maximum,
                                                int fieldIndex,
                                                auto write) {
        const CanonicalGeneValidation validation = normalizeFloat(
                current, minimum, maximum, fieldIndex);
        if (validation.isValid()) {
            write(current);
        }
        return validation;
    };
    const auto requireValid = [&](CanonicalGeneValidation validation)
            -> std::optional<CanonicalGeneResult> {
        if (validation.isValid()) {
            return std::nullopt;
        }
        return failure(validation.error, validation.fieldIndex);
    };

    for (int index = 0; index < 3; ++index) {
        if (const auto invalid = requireValid(normalizeAndWriteFloat(
                    view.color(index),
                    0.0f,
                    1.0f,
                    index,
                    [&](float normalized) {
                        builder.setColor(index, normalized);
                    }))) {
            return *invalid;
        }
    }

    if (const auto invalid = requireValid(normalizeAndWriteFloat(
                view.splitMass(),
                CellDataConstants::MIN_SPLIT_MASS,
                CellDataConstants::MAX_SPLIT_MASS,
                0,
                [&](float normalized) {
                    builder.setSplitMass(normalized);
                }))) {
        return *invalid;
    }
    if (const auto invalid = requireValid(normalizeAndWriteFloat(
                view.splitRatio(),
                CellDataConstants::MIN_RATIO,
                1.0f - CellDataConstants::MIN_RATIO,
                1,
                [&](float normalized) {
                    builder.setSplitRatio(normalized);
                }))) {
        return *invalid;
    }
    const auto normalizeAndWriteAngle = [&](
                                                float current, int fieldIndex, auto write) {
        CanonicalGeneValidation validation = normalizeAngle(current, fieldIndex);
        if (validation.isValid()) {
            write(current);
        }
        return validation;
    };
    if (const auto invalid = requireValid(normalizeAndWriteAngle(
                view.splitAngle(),
                2,
                [&](float normalized) {
                    builder.setSplitAngle(normalized);
                }))) {
        return *invalid;
    }
    if (const auto invalid = requireValid(normalizeAndWriteAngle(
                view.child1Angle(),
                3,
                [&](float normalized) {
                    builder.setChild1Angle(normalized);
                }))) {
        return *invalid;
    }
    if (const auto invalid = requireValid(normalizeAndWriteAngle(
                view.child2Angle(),
                4,
                [&](float normalized) {
                    builder.setChild2Angle(normalized);
                }))) {
        return *invalid;
    }
    if (const auto invalid = requireValid(normalizeAndWriteFloat(
                view.massFlowPriority(),
                CellDataConstants::MIN_MASS_FLOW_PRIORITY,
                CellDataConstants::MAX_MASS_FLOW_PRIORITY,
                5,
                [&](float normalized) {
                    builder.setMassFlowPriority(normalized);
                }))) {
        return *invalid;
    }
    if (const auto invalid = requireValid(normalizeAndWriteFloat(
                view.linkStrength(),
                0.0f,
                CellDataConstants::MAX_LINK_STIFFNESS,
                6,
                [&](float normalized) {
                    builder.setLinkStrength(normalized);
                }))) {
        return *invalid;
    }

    const auto normalizeAndWriteReference = [&](
                                                    GeneIndex reference, int fieldIndex, auto write) {
        int rawReference = reference.value();
        const CanonicalGeneValidation validation = normalizeReference(
                rawReference, owner, geneCount, fieldIndex);
        if (validation.isValid()) {
            write(GeneIndex(static_cast<GeneIndex::Value>(rawReference)));
        }
        return validation;
    };
    if (const auto invalid = requireValid(normalizeAndWriteReference(
                view.child1GeneIndex(),
                0,
                [&](GeneIndex reference) {
                    builder.setChild1GeneIndex(reference);
                }))) {
        return *invalid;
    }
    if (const auto invalid = requireValid(normalizeAndWriteReference(
                view.child2GeneIndex(),
                1,
                [&](GeneIndex reference) {
                    builder.setChild2GeneIndex(reference);
                }))) {
        return *invalid;
    }
    if (const auto invalid = requireValid(normalizeAndWriteReference(
                view.virusFromGeneIndex(),
                2,
                [&](GeneIndex reference) {
                    builder.setVirusFromGeneIndex(reference);
                }))) {
        return *invalid;
    }
    if (const auto invalid = requireValid(normalizeAndWriteReference(
                view.virusToGeneIndex(),
                3,
                [&](GeneIndex reference) {
                    builder.setVirusToGeneIndex(reference);
                }))) {
        return *invalid;
    }

    const int cellTypeOrdinal = CellTypes::ordinal(view.cellType());
    if (cellTypeOrdinal < 0 || cellTypeOrdinal >= CellTypes::length) {
        return failure(CanonicalGeneError::InvalidCellType);
    }

    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
        const GeneParametricValueView parameter = view.parametricValue(index);
        const auto normalizeVariable = [&](float current, auto write) {
            return normalizeAndWriteFloat(
                    current,
                    -CellDataConstants::VALUE_VARIABLE_MAX,
                    CellDataConstants::VALUE_VARIABLE_MAX,
                    index,
                    write);
        };
        if (const auto invalid = requireValid(normalizeVariable(
                    parameter.variableA(),
                    [&](float normalized) {
                        builder.setParametricVariableA(index, normalized);
                    }))) {
            return *invalid;
        }
        if (const auto invalid = requireValid(normalizeVariable(
                    parameter.variableB(),
                    [&](float normalized) {
                        builder.setParametricVariableB(index, normalized);
                    }))) {
            return *invalid;
        }
        if (const auto invalid = requireValid(normalizeVariable(
                    parameter.variableC(),
                    [&](float normalized) {
                        builder.setParametricVariableC(index, normalized);
                    }))) {
            return *invalid;
        }
        if (parameter.formulaType() < 0 || parameter.formulaType() > 3) {
            return failure(CanonicalGeneError::InvalidParametricFormula, index);
        }
        if (parameter.signalChannel() < 0
                || parameter.signalChannel()
                        >= CellDataConstants::SIGNAL_SUBSTANCE_COUNT
                                + CellDataConstants::AUX_SIGNAL_SUBSTANCE_COUNT) {
            return failure(CanonicalGeneError::InvalidSignalChannel, index);
        }
    }

    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
        if (index == Gene::VIRUS_FROM_INTEGER_INDEX
                || index == Gene::VIRUS_TO_INTEGER_INDEX) {
            continue;
        }
        if (view.integerValue(index) < 0
                || view.integerValue(index) >= Gene::INTEGER_MAX[index]) {
            return failure(CanonicalGeneError::InvalidIntegerValue, index);
        }
    }

    for (int index = 0; index < Gene::MAX_FLOATS_COUNT; index++) {
        const Gene::FloatType type = static_cast<Gene::FloatType>(index);
        if (const auto invalid = requireValid(normalizeAndWriteFloat(
                    view.floatValue(type),
                    Gene::floatMinimum(type),
                    Gene::floatMaximum(type),
                    index,
                    [&](float normalized) {
                        builder.setFloatValue(type, normalized);
                    }))) {
            return *invalid;
        }
    }

    // Initial-gene identity belongs to GenomeMetadata, never to an interned gene.
    builder.setInitialCompatibilityFlag(false);
    return {CanonicalGene(std::move(value)), {}};
}

const CanonicalGene& CanonicalGeneDefaults::value() {
    static const CanonicalGene defaults = [] {
        CanonicalGeneResult result = canonicalizeGene(
                explicitCanonicalDefaults(), GeneIndex(0), 1);
        if (!result.hasValue()) {
            throw std::logic_error("CanonicalGeneDefaults is invalid");
        }
        return std::move(*result.value);
    }();
    return defaults;
}

std::optional<GeneData> materializeCanonicalGene(
        const CanonicalGene& source,
        GeneIndex owner,
        std::size_t geneCount,
        bool isInitial) {
    if (!isGeneIndexInBounds(owner, geneCount)) {
        return std::nullopt;
    }
    GeneData result(source.data());
    GeneBuilder builder(result);
    const GeneView view(result);
    const auto resolveAndWrite = [&](GeneIndex rawReference, auto write) {
        const auto reference = geneReferenceFromBoundary(
                rawReference.value(), geneCount);
        if (!reference.has_value()) {
            return false;
        }
        const GeneIndex resolved = resolveGeneReference(*reference, owner, geneCount);
        if (resolved == INVALID_GENE_INDEX) {
            return false;
        }
        write(resolved);
        return true;
    };
    if (!resolveAndWrite(view.child1GeneIndex(), [&](GeneIndex reference) {
            builder.setChild1GeneIndex(reference);
        }) || !resolveAndWrite(view.child2GeneIndex(), [&](GeneIndex reference) {
            builder.setChild2GeneIndex(reference);
        }) || !resolveAndWrite(view.virusFromGeneIndex(), [&](GeneIndex reference) {
            builder.setVirusFromGeneIndex(reference);
        }) || !resolveAndWrite(view.virusToGeneIndex(), [&](GeneIndex reference) {
            builder.setVirusToGeneIndex(reference);
        })) {
        return std::nullopt;
    }
    builder.setInitialCompatibilityFlag(isInitial);
    return result;
}

bool canonicalGeneEqual(
        const CanonicalGene& leftCanonical,
        const CanonicalGene& rightCanonical) noexcept {
    const GeneView left(leftCanonical.data());
    const GeneView right(rightCanonical.data());
    for (int index = 0; index < 3; index++) {
        if (!sameFloat(left.color(index), right.color(index))) {
            return false;
        }
    }
    const std::array<float, 7> leftScalars{
            left.splitMass(),
            left.splitRatio(),
            left.splitAngle(),
            left.child1Angle(),
            left.child2Angle(),
            left.massFlowPriority(),
            left.linkStrength()
    };
    const std::array<float, 7> rightScalars{
            right.splitMass(),
            right.splitRatio(),
            right.splitAngle(),
            right.child1Angle(),
            right.child2Angle(),
            right.massFlowPriority(),
            right.linkStrength()
    };
    for (std::size_t index = 0; index < leftScalars.size(); index++) {
        if (!sameFloat(leftScalars[index], rightScalars[index])) {
            return false;
        }
    }
    if (left.child1GeneIndex() != right.child1GeneIndex()
            || left.child2GeneIndex() != right.child2GeneIndex()
            || left.makesLink() != right.makesLink()
            || left.child1KeepsLinks() != right.child1KeepsLinks()
            || left.child2KeepsLinks() != right.child2KeepsLinks()
            || left.staysAlive() != right.staysAlive()
            || left.child1Mirrored() != right.child1Mirrored()
            || left.child2Mirrored() != right.child2Mirrored()
            || left.cellType() != right.cellType()) {
        return false;
    }
    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; index++) {
        const GeneParametricValueView leftValue = left.parametricValue(index);
        const GeneParametricValueView rightValue = right.parametricValue(index);
        if (!sameFloat(leftValue.variableA(), rightValue.variableA())
                || !sameFloat(leftValue.variableB(), rightValue.variableB())
                || !sameFloat(leftValue.variableC(), rightValue.variableC())
                || leftValue.signalChannel() != rightValue.signalChannel()
                || leftValue.formulaType() != rightValue.formulaType()) {
            return false;
        }
    }
    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; ++index) {
        if (left.integerValue(index) != right.integerValue(index)) {
            return false;
        }
    }
    for (int index = 0; index < Gene::MAX_FLOATS_COUNT; index++) {
        const Gene::FloatType type = static_cast<Gene::FloatType>(index);
        if (!sameFloat(left.floatValue(type), right.floatValue(type))) {
            return false;
        }
    }
    return true;
}

std::uint64_t canonicalGeneHash(const CanonicalGene& canonical) noexcept {
    const GeneView gene(canonical.data());
    CanonicalHashWriter writer;
    writer.appendByte('C');
    writer.appendByte('G');
    writer.appendByte(0);
    writer.appendByte(2);

    for (int channel = 0; channel < 3; ++channel) {
        writer.appendFloat(gene.color(channel));
    }
    writer.appendFloat(gene.splitMass());
    writer.appendFloat(gene.splitRatio());
    writer.appendFloat(gene.splitAngle());
    writer.appendFloat(gene.child1Angle());
    writer.appendFloat(gene.child2Angle());
    writer.appendFloat(gene.massFlowPriority());
    writer.appendFloat(gene.linkStrength());
    appendReference(writer, gene.child1GeneIndex());
    appendReference(writer, gene.child2GeneIndex());
    writer.appendBool(gene.makesLink());
    writer.appendBool(gene.child1KeepsLinks());
    writer.appendBool(gene.child2KeepsLinks());
    writer.appendBool(gene.staysAlive());
    writer.appendBool(gene.child1Mirrored());
    writer.appendBool(gene.child2Mirrored());
    writer.appendByte(
            static_cast<std::uint8_t>(CellTypes::ordinal(gene.cellType())));

    for (int index = 0; index < Gene::MAX_PARAMETRIC_VALUES_COUNT; ++index) {
        const GeneParametricValueView value = gene.parametricValue(index);
        writer.appendFloat(value.variableA());
        writer.appendFloat(value.variableB());
        writer.appendFloat(value.variableC());
        writer.appendU16(static_cast<std::uint16_t>(value.signalChannel()));
        writer.appendU16(static_cast<std::uint16_t>(value.formulaType()));
    }
    for (int index = 0; index < Gene::MAX_INTEGERS_COUNT; index++) {
        if (index == Gene::VIRUS_FROM_INTEGER_INDEX
                || index == Gene::VIRUS_TO_INTEGER_INDEX) {
            appendReference(
                    writer,
                    GeneIndex(static_cast<GeneIndex::Value>(
                            gene.integerValue(index))));
        } else {
            writer.appendInt(gene.integerValue(index));
        }
    }
    for (int index = 0; index < Gene::MAX_FLOATS_COUNT; ++index) {
        writer.appendFloat(
                gene.floatValue(static_cast<Gene::FloatType>(index)));
    }
    return writer.value();
}

} // namespace cellularity
