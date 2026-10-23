#pragma once

struct GenomePreviewDrawContext;
class GenomePreviewGhostController;

class GenomePreviewGhostRenderer {
public:
    void draw(const GenomePreviewDrawContext& drawContext, GenomePreviewGhostController& ghost);
};
