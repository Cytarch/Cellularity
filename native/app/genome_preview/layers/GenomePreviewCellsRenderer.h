#pragma once

struct GenomePreviewDrawContext;

class GenomePreviewCellsRenderer {
public:
    int draw(const GenomePreviewDrawContext& drawContext,
            int selectedGeneIndex,
            bool directionArrowEnabled);
};
