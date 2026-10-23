#pragma once

#include <cstddef>
#include <optional>

#include "core/Gene.h"
#include "core/GenomeIdentity.h"

namespace cellularity {

/** Genome-owned state temporarily mirrored by the fixed GeneData backend. */
struct GenomeMetadata final {
    GeneIndex initialGeneIndex = INVALID_GENE_INDEX;

    friend constexpr bool operator==(
            GenomeMetadata left,
            GenomeMetadata right) noexcept {
        return left.initialGeneIndex == right.initialGeneIndex;
    }
};

/** Reads exactly one compatibility initial flag without modifying the array. */
[[nodiscard]] std::optional<GenomeMetadata>
extractGenomeMetadataFromCompatibilityFlags(
        const GeneData* genes,
        std::size_t geneCount) noexcept;

/**
 * Preserves the original legacy rule: keep the first initial flag, clear later
 * flags, and select slot zero when the input has none.
 */
[[nodiscard]] std::optional<GenomeMetadata> normalizeLegacyInitialGeneFlags(
        GeneData* genes,
        std::size_t geneCount) noexcept;

/** Materializes genome-owned metadata into the temporary fixed GeneData backend. */
[[nodiscard]] bool applyGenomeMetadataToCompatibilityFlags(
        GeneData* genes,
        std::size_t geneCount,
        GenomeMetadata metadata) noexcept;

} // namespace cellularity
