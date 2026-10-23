#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core/Gene.h"
#include "core/GenomeIdentity.h"

class Environment;

namespace cellularity {

class GenePool;
class GenomePool;

/** Immutable dense view retained for the hot parametric evaluation path. */
class GeneParametricValueView final {
public:
    [[nodiscard]] constexpr float variableA() const noexcept {
        return value_->mVariableA;
    }

    [[nodiscard]] constexpr float variableB() const noexcept {
        return value_->mVariableB;
    }

    [[nodiscard]] constexpr float variableC() const noexcept {
        return value_->mVariableC;
    }

    [[nodiscard]] constexpr int signalChannel() const noexcept {
        return value_->mSignalSubstanceChannelIndex;
    }

    [[nodiscard]] constexpr int formulaType() const noexcept {
        return value_->mFormulaType;
    }

private:
    explicit constexpr GeneParametricValueView(
            const GeneData::ParametricValue& value) noexcept
            : value_(&value) {
    }

    const GeneData::ParametricValue* value_;

    friend class GeneView;
};

/** Immutable view of one dense gene. Its interface is independent of the backing layout. */
class GeneView final {
public:
    explicit constexpr GeneView(const GeneData& gene) noexcept
            : gene_(&gene), context_(NO_CONTEXT) {
    }

    /**
     * Views canonical pooled data in one concrete local-genome position.
     * Canonical SELF_GENE_INDEX references and genome-owned initial metadata
     * are resolved without materializing a mutable GeneData copy.
     */
    constexpr GeneView(
            const GeneData& gene,
            GeneIndex owner,
            bool isInitial) noexcept
            : gene_(&gene),
              context_(static_cast<std::uint16_t>(
                      owner.value() | (isInitial ? INITIAL_CONTEXT_BIT : 0U))) {
        assert(isExplicitGeneIndex(owner));
    }

    [[nodiscard]] constexpr float color(int channel) const noexcept {
        assert(channel >= 0 && channel < static_cast<int>(gene_->mColor.size()));
        return gene_->mColor[static_cast<std::size_t>(channel)];
    }

    [[nodiscard]] constexpr float splitMass() const noexcept {
        return gene_->mSplitMass;
    }

    [[nodiscard]] constexpr float splitRatio() const noexcept {
        return gene_->mSplitRatio;
    }

    [[nodiscard]] constexpr float splitAngle() const noexcept {
        return gene_->mSplitAngle;
    }

    [[nodiscard]] constexpr float child1Angle() const noexcept {
        return gene_->mChild1Angle;
    }

    [[nodiscard]] constexpr float child2Angle() const noexcept {
        return gene_->mChild2Angle;
    }

    [[nodiscard]] constexpr float massFlowPriority() const noexcept {
        return gene_->mMassFlowPriority;
    }

    [[nodiscard]] constexpr float linkStrength() const noexcept {
        return gene_->mLinkStrength;
    }

    [[nodiscard]] constexpr GeneIndex child1GeneIndex() const noexcept {
        return resolveLocalReference(GeneIndex(
                static_cast<GeneIndex::Value>(gene_->mChild1GeneIndex)));
    }

    [[nodiscard]] constexpr GeneIndex child2GeneIndex() const noexcept {
        return resolveLocalReference(GeneIndex(
                static_cast<GeneIndex::Value>(gene_->mChild2GeneIndex)));
    }

    [[nodiscard]] constexpr bool makesLink() const noexcept {
        return gene_->flag(GeneData::MAKES_LINK);
    }

    [[nodiscard]] constexpr bool child1KeepsLinks() const noexcept {
        return gene_->flag(GeneData::CHILD_1_KEEPS_LINKS);
    }

    [[nodiscard]] constexpr bool child2KeepsLinks() const noexcept {
        return gene_->flag(GeneData::CHILD_2_KEEPS_LINKS);
    }

    [[nodiscard]] constexpr bool staysAlive() const noexcept {
        return gene_->flag(GeneData::STAYS_ALIVE);
    }

    [[nodiscard]] constexpr bool initialCompatibilityFlag() const noexcept {
        return hasContext()
                ? (context_ & INITIAL_CONTEXT_BIT) != 0U
                : gene_->flag(GeneData::INITIAL_COMPATIBILITY);
    }

    [[nodiscard]] constexpr bool child1Mirrored() const noexcept {
        return gene_->flag(GeneData::CHILD_1_MIRRORED);
    }

    [[nodiscard]] constexpr bool child2Mirrored() const noexcept {
        return gene_->flag(GeneData::CHILD_2_MIRRORED);
    }

    [[nodiscard]] constexpr CellType cellType() const noexcept {
        return static_cast<CellType>(gene_->mCellTypeOrdinal);
    }

    [[nodiscard]] constexpr float parametricVariableA(int index) const noexcept {
        return parametricValue(index).variableA();
    }

    [[nodiscard]] constexpr float parametricVariableB(int index) const noexcept {
        return parametricValue(index).variableB();
    }

    [[nodiscard]] constexpr float parametricVariableC(int index) const noexcept {
        return parametricValue(index).variableC();
    }

    [[nodiscard]] constexpr int parametricSignalChannel(int index) const noexcept {
        return parametricValue(index).signalChannel();
    }

    [[nodiscard]] constexpr int parametricFormulaType(int index) const noexcept {
        return parametricValue(index).formulaType();
    }

    [[nodiscard]] constexpr GeneParametricValueView parametricValue(
            int index) const noexcept {
        assert(index >= 0 && index < GeneData::MAX_PARAMETRIC_VALUES_COUNT);
        return GeneParametricValueView(
                gene_->mParametricValue[static_cast<std::size_t>(index)]);
    }

    [[nodiscard]] constexpr int integerValue(int index) const noexcept {
        assert(index >= 0 && index < GeneData::MAX_INTEGERS_COUNT);
        return gene_->mInteger[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] constexpr GeneIndex virusFromGeneIndex() const noexcept {
        return resolveLocalReference(GeneIndex(static_cast<GeneIndex::Value>(
                integerValue(GeneData::VIRUS_FROM_INTEGER_INDEX))));
    }

    [[nodiscard]] constexpr GeneIndex virusToGeneIndex() const noexcept {
        return resolveLocalReference(GeneIndex(static_cast<GeneIndex::Value>(
                integerValue(GeneData::VIRUS_TO_INTEGER_INDEX))));
    }

    [[nodiscard]] float floatValue(GeneData::FloatType type) const noexcept {
        return gene_->floatStorage(type);
    }

    [[nodiscard]] float floatMinimum(GeneData::FloatType type) const noexcept {
        return GeneData::floatMinimum(type);
    }

    [[nodiscard]] float floatMaximum(GeneData::FloatType type) const noexcept {
        return GeneData::floatMaximum(type);
    }

    [[nodiscard]] bool hasValidValues() const {
        return gene_->isValidGeneValues();
    }

private:
    static constexpr std::uint16_t INITIAL_CONTEXT_BIT = 0x8000U;
    static constexpr std::uint16_t OWNER_CONTEXT_MASK = 0x7FFFU;
    static constexpr std::uint16_t NO_CONTEXT = 0xFFFFU;

    [[nodiscard]] constexpr bool hasContext() const noexcept {
        return context_ != NO_CONTEXT;
    }

    [[nodiscard]] constexpr GeneIndex resolveLocalReference(
            GeneIndex reference) const noexcept {
        if (!hasContext() || reference != SELF_GENE_INDEX) {
            return reference;
        }
        return GeneIndex(static_cast<GeneIndex::Value>(
                context_ & OWNER_CONTEXT_MASK));
    }

    const GeneData* gene_;
    std::uint16_t context_;

    friend class GeneBuilder;
    friend class GenePool;
};

/** Bounded immutable access to a genome without exposing its concrete container. */
class GenomeView final {
public:
    constexpr GenomeView(const GeneData* genes, std::size_t geneCount) noexcept
            : denseGenes_(genes), geneCount_(geneCount) {
        assert(genes != nullptr);
        assert(isSupportedGeneCount(geneCount));
    }

    template <std::size_t Size>
    explicit constexpr GenomeView(const std::array<GeneData, Size>& genes) noexcept
            : GenomeView(genes.data(), Size) {
    }

    [[nodiscard]] constexpr std::size_t geneCount() const noexcept {
        return geneCount_;
    }

    [[nodiscard]] GeneView gene(GeneIndex index) const noexcept;

    [[nodiscard]] GeneView gene(std::size_t index) const noexcept;

private:
    constexpr GenomeView(
            const GenePool& genePool,
            const GeneId* geneIds,
            std::size_t geneCount,
            GeneIndex initialGeneIndex) noexcept
            : genePool_(&genePool),
              geneIds_(geneIds),
              geneCount_(geneCount),
              initialGeneIndex_(initialGeneIndex) {
        assert(geneIds != nullptr);
        assert(isSupportedGeneCount(geneCount));
        assert(isGeneIndexInBounds(initialGeneIndex, geneCount));
    }

    const GeneData* denseGenes_ = nullptr;
    const GenePool* genePool_ = nullptr;
    const GeneId* geneIds_ = nullptr;
    std::size_t geneCount_;
    GeneIndex initialGeneIndex_ = INVALID_GENE_INDEX;

    friend class GeneBuilder;
    friend class GenomePool;
};

/**
 * Mutable facade for constructing or changing an unshared dense gene.
 * Mutation through this type is intentionally explicit so future pooled genes
 * can replace the backing layout without exposing mutable interned storage.
 */
class GeneBuilder final {
public:
    explicit constexpr GeneBuilder(GeneData& gene) noexcept : gene_(&gene) {
    }

    [[nodiscard]] constexpr GeneView view() const noexcept {
        return GeneView(*gene_);
    }

    void copyFrom(GeneView source) {
        *gene_ = *source.gene_;
    }

    constexpr void setColor(int channel, float value) noexcept {
        assert(channel >= 0 && channel < static_cast<int>(gene_->mColor.size()));
        gene_->mColor[static_cast<std::size_t>(channel)] = value;
    }

    constexpr void setSplitMass(float value) noexcept {
        gene_->mSplitMass = value;
    }

    constexpr void setSplitRatio(float value) noexcept {
        gene_->mSplitRatio = value;
    }

    constexpr void setSplitAngle(float value) noexcept {
        gene_->mSplitAngle = value;
    }

    constexpr void setChild1Angle(float value) noexcept {
        gene_->mChild1Angle = value;
    }

    constexpr void setChild2Angle(float value) noexcept {
        gene_->mChild2Angle = value;
    }

    constexpr void setMassFlowPriority(float value) noexcept {
        gene_->mMassFlowPriority = value;
    }

    constexpr void setLinkStrength(float value) noexcept {
        gene_->mLinkStrength = value;
    }

    constexpr void setChild1GeneIndex(GeneIndex value) noexcept {
        gene_->mChild1GeneIndex = value.value();
    }

    constexpr void setChild2GeneIndex(GeneIndex value) noexcept {
        gene_->mChild2GeneIndex = value.value();
    }

    constexpr void setMakesLink(bool value) noexcept {
        gene_->setFlag(GeneData::MAKES_LINK, value);
    }

    constexpr void setChild1KeepsLinks(bool value) noexcept {
        gene_->setFlag(GeneData::CHILD_1_KEEPS_LINKS, value);
    }

    constexpr void setChild2KeepsLinks(bool value) noexcept {
        gene_->setFlag(GeneData::CHILD_2_KEEPS_LINKS, value);
    }

    constexpr void setStaysAlive(bool value) noexcept {
        gene_->setFlag(GeneData::STAYS_ALIVE, value);
    }

    constexpr void setInitialCompatibilityFlag(bool value) noexcept {
        gene_->setFlag(GeneData::INITIAL_COMPATIBILITY, value);
    }

    constexpr void setChild1Mirrored(bool value) noexcept {
        gene_->setFlag(GeneData::CHILD_1_MIRRORED, value);
    }

    constexpr void setChild2Mirrored(bool value) noexcept {
        gene_->setFlag(GeneData::CHILD_2_MIRRORED, value);
    }

    constexpr void setCellType(CellType value) noexcept {
        const int ordinal = CellTypes::ordinal(value);
        assert(ordinal >= 0
                && ordinal <= static_cast<int>(
                           std::numeric_limits<std::uint8_t>::max()));
        gene_->mCellTypeOrdinal = static_cast<std::uint8_t>(ordinal);
    }

    constexpr void setParametricVariableA(int index, float value) noexcept {
        parametricValue(index).mVariableA = value;
    }

    constexpr void setParametricVariableB(int index, float value) noexcept {
        parametricValue(index).mVariableB = value;
    }

    constexpr void setParametricVariableC(int index, float value) noexcept {
        parametricValue(index).mVariableC = value;
    }

    constexpr void setParametricSignalChannel(int index, int value) noexcept {
        assert(value >= static_cast<int>(
                       std::numeric_limits<std::int16_t>::min())
                && value <= static_cast<int>(
                           std::numeric_limits<std::int16_t>::max()));
        parametricValue(index).mSignalSubstanceChannelIndex =
                static_cast<std::int16_t>(value);
    }

    constexpr void setParametricFormulaType(int index, int value) noexcept {
        assert(value >= static_cast<int>(
                       std::numeric_limits<std::int16_t>::min())
                && value <= static_cast<int>(
                           std::numeric_limits<std::int16_t>::max()));
        parametricValue(index).mFormulaType = static_cast<std::int16_t>(value);
    }

    constexpr void setIntegerValue(int index, int value) noexcept {
        assert(index >= 0 && index < GeneData::MAX_INTEGERS_COUNT);
        assert(value >= 0
                && value <= static_cast<int>(
                           std::numeric_limits<std::uint16_t>::max()));
        gene_->mInteger[static_cast<std::size_t>(index)] =
                static_cast<std::uint16_t>(value);
    }

    constexpr void setVirusFromGeneIndex(GeneIndex value) noexcept {
        setIntegerValue(GeneData::VIRUS_FROM_INTEGER_INDEX, value.value());
    }

    constexpr void setVirusToGeneIndex(GeneIndex value) noexcept {
        setIntegerValue(GeneData::VIRUS_TO_INTEGER_INDEX, value.value());
    }

    void setFloatValue(GeneData::FloatType type, float value) noexcept {
        gene_->floatStorage(type) = value;
    }

    void mutate(Environment& environment);
    void mutate(Environment& environment, GenomeView genome);
    bool mutatePointByWeightIndex(
            Environment& environment,
            int mutationWeightIndex,
            GenomeView genome);
    void fix(
            std::size_t geneCount =
                    CellDataConstants::MAX_GENES_COUNT);

private:
    [[nodiscard]] constexpr GeneData::ParametricValue& parametricValue(
            int index) noexcept {
        assert(index >= 0 && index < GeneData::MAX_PARAMETRIC_VALUES_COUNT);
        return gene_->mParametricValue[static_cast<std::size_t>(index)];
    }

    GeneData* gene_;
};

static_assert(
        sizeof(GeneParametricValueView)
        == sizeof(const GeneData::ParametricValue*));
static_assert(sizeof(GeneBuilder) == sizeof(GeneData*));

} // namespace cellularity
