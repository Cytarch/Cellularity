#include "app/genome_preview/GenomePreviewRenderPipeline.h"

#include <memory>

#include "app/genome_preview/GenomePreviewDrawContext.h"
#include "app/genome_preview/ghost/GenomePreviewGhostController.h"
#include "app/genome_preview/layers/GenomePreviewCellsRenderer.h"
#include "app/genome_preview/layers/GenomePreviewGhostRenderer.h"
#include "app/genome_preview/layers/GenomePreviewGridRenderer.h"
#include "app/genome_preview/tree/GenomePreviewTreeController.h"

GenomePreviewRenderPipeline::GenomePreviewRenderPipeline()
        : mCellsRenderer(std::make_unique<GenomePreviewCellsRenderer>()),
          mGridRenderer(std::make_unique<GenomePreviewGridRenderer>()),
          mGhostRenderer(std::make_unique<GenomePreviewGhostRenderer>()) {
}

GenomePreviewRenderPipeline::~GenomePreviewRenderPipeline() = default;

int GenomePreviewRenderPipeline::draw(const GenomePreviewDrawContext& drawContext,
        GenomePreviewTreeController& tree,
        GenomePreviewGhostController& ghost,
        const GenomePreviewViewState& viewState) {
    mGridRenderer->draw(drawContext, viewState.gridEnabled);

    if (viewState.baseMode == GenomePreviewBaseMode::Tree) {
        return tree.draw(drawContext, viewState.selectedGeneIndex);
    }

    if (viewState.ghostEnabled) {
        mGhostRenderer->draw(drawContext, ghost);
    }

    const int visibleCount = mCellsRenderer->draw(
            drawContext,
            viewState.selectedGeneIndex,
            viewState.directionArrowEnabled());
    return visibleCount;
}
