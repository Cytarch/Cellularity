#pragma once

#include <memory>

#include "app/genome_preview/GenomePreviewViewState.h"

class GenomePreviewCellsRenderer;
struct GenomePreviewDrawContext;
class GenomePreviewGhostController;
class GenomePreviewGhostRenderer;
class GenomePreviewGridRenderer;
class GenomePreviewTreeController;

class GenomePreviewRenderPipeline {
public:
    GenomePreviewRenderPipeline();
    ~GenomePreviewRenderPipeline();

    int draw(const GenomePreviewDrawContext& drawContext,
            GenomePreviewTreeController& tree,
            GenomePreviewGhostController& ghost,
            const GenomePreviewViewState& viewState);

private:
    std::unique_ptr<GenomePreviewCellsRenderer> mCellsRenderer;
    std::unique_ptr<GenomePreviewGridRenderer> mGridRenderer;
    std::unique_ptr<GenomePreviewGhostRenderer> mGhostRenderer;
};
