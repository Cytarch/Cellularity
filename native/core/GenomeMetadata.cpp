#include "core/GenomeMetadata.h"

#include "core/GeneAccess.h"

namespace cellularity {

std::optional<GenomeMetadata> extractGenomeMetadataFromCompatibilityFlags(
        const GeneData* genes,
        std::size_t geneCount) noexcept {
    if (genes == nullptr || !isSupportedGeneCount(geneCount)) {
        return std::nullopt;
    }
    GeneIndex selected = INVALID_GENE_INDEX;
    for (std::size_t index = 0; index < geneCount; index++) {
        if (!GeneView(genes[index]).initialCompatibilityFlag()) {
            continue;
        }
        if (selected != INVALID_GENE_INDEX) {
            return std::nullopt;
        }
        selected = GeneIndex(static_cast<GeneIndex::Value>(index));
    }
    if (selected == INVALID_GENE_INDEX) {
        return std::nullopt;
    }
    return GenomeMetadata{selected};
}

std::optional<GenomeMetadata> normalizeLegacyInitialGeneFlags(
        GeneData* genes,
        std::size_t geneCount) noexcept {
    if (genes == nullptr || !isSupportedGeneCount(geneCount)) {
        return std::nullopt;
    }
    GeneIndex selected(0);
    bool found = false;
    for (std::size_t index = 0; index < geneCount; index++) {
        if (!found && GeneView(genes[index]).initialCompatibilityFlag()) {
            selected = GeneIndex(static_cast<GeneIndex::Value>(index));
            found = true;
        }
    }
    const GenomeMetadata metadata{selected};
    if (!applyGenomeMetadataToCompatibilityFlags(genes, geneCount, metadata)) {
        return std::nullopt;
    }
    return metadata;
}

bool applyGenomeMetadataToCompatibilityFlags(
        GeneData* genes,
        std::size_t geneCount,
        GenomeMetadata metadata) noexcept {
    if (genes == nullptr
            || !isGeneIndexInBounds(metadata.initialGeneIndex, geneCount)) {
        return false;
    }
    for (std::size_t index = 0; index < geneCount; index++) {
        GeneBuilder(genes[index]).setInitialCompatibilityFlag(index == metadata.initialGeneIndex.value());
    }
    return true;
}

} // namespace cellularity
