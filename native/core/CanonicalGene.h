#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "core/Gene.h"
#include "core/GenomeIdentity.h"

namespace cellularity {

enum class CanonicalGeneError : std::uint8_t {
    None = 0,
    InvalidGeneCount,
    InvalidOwnerIndex,
    NonFiniteFloat,
    FloatOutOfRange,
    InvalidGeneReference,
    InvalidCellType,
    InvalidParametricFormula,
    InvalidSignalChannel,
    InvalidIntegerValue
};

struct CanonicalGeneValidation final {
    CanonicalGeneError error = CanonicalGeneError::None;
    int fieldIndex = -1;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return error == CanonicalGeneError::None;
    }
};

struct CanonicalGeneResult;

/**
 * Validated, normalized compact gene identity used by interning. The wrapped
 * GeneData always has its compatibility initial flag cleared, self references
 * use SELF_GENE_INDEX, and static field descriptors are schema metadata rather
 * than part of equality.
 */
class CanonicalGene final {
public:
    CanonicalGene(const CanonicalGene&) = default;
    CanonicalGene(CanonicalGene&&) noexcept = default;
    CanonicalGene& operator=(const CanonicalGene&) = default;
    CanonicalGene& operator=(CanonicalGene&&) noexcept = default;

    [[nodiscard]] const GeneData& data() const noexcept {
        return value_;
    }

private:
    explicit CanonicalGene(GeneData value) : value_(std::move(value)) {
    }

    GeneData value_;

    friend struct CanonicalGeneResult;
    friend CanonicalGeneResult canonicalizeGene(
            const GeneData&, GeneIndex, std::size_t);
};

struct CanonicalGeneResult final {
    std::optional<CanonicalGene> value;
    CanonicalGeneValidation validation;

    [[nodiscard]] bool hasValue() const noexcept {
        return value.has_value();
    }
};

/** One explicit, position-independent default for the current pre-release schema. */
class CanonicalGeneDefaults final {
public:
    [[nodiscard]] static const CanonicalGene& value();
};

/**
 * Validates every persisted field, normalizes float representations, replaces
 * owner-local references with SELF_GENE_INDEX, and removes initial-gene state
 * from gene identity.
 */
[[nodiscard]] CanonicalGeneResult canonicalizeGene(
        const GeneData& source,
        GeneIndex owner,
        std::size_t geneCount);

/** Resolves SELF_GENE_INDEX for the temporary fixed GeneData backend. */
[[nodiscard]] std::optional<GeneData> materializeCanonicalGene(
        const CanonicalGene& source,
        GeneIndex owner,
        std::size_t geneCount,
        bool isInitial = false);

/** Exact, field-wise equality over canonical schema fields; struct padding is ignored. */
[[nodiscard]] bool canonicalGeneEqual(
        const CanonicalGene& left,
        const CanonicalGene& right) noexcept;

/** Stable FNV-1a hash over the same explicit fields used by canonicalGeneEqual. */
[[nodiscard]] std::uint64_t canonicalGeneHash(
        const CanonicalGene& gene) noexcept;

} // namespace cellularity
