#include "core/GeneAccess.h"

#include "core/GenePool.h"

namespace cellularity {

GeneView GenomeView::gene(GeneIndex index) const noexcept {
    assert(isGeneIndexInBounds(index, geneCount_));
    return gene(static_cast<std::size_t>(index.value()));
}

GeneView GenomeView::gene(std::size_t index) const noexcept {
    assert(index < geneCount_);
    if (denseGenes_ != nullptr) {
        return GeneView(denseGenes_[index]);
    }
    assert(genePool_ != nullptr);
    assert(geneIds_ != nullptr);
    return genePool_->contextualView(
            geneIds_[index],
            GeneIndex(static_cast<GeneIndex::Value>(index)),
            index == initialGeneIndex_.value());
}

void GeneBuilder::mutate(Environment& environment) {
    gene_->mutate(environment);
}

void GeneBuilder::mutate(Environment& environment, GenomeView genome) {
    gene_->mutate(environment, &genome);
}

bool GeneBuilder::mutatePointByWeightIndex(
        Environment& environment,
        int mutationWeightIndex,
        GenomeView genome) {
    return gene_->mutatePointByWeightIndex(
            environment, mutationWeightIndex, &genome);
}

void GeneBuilder::fix(std::size_t geneCount) {
    gene_->fix(geneCount);
}

} // namespace cellularity
