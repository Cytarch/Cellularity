#pragma once

#include <vector>

#include "app/genome_preview/tree/GenomePreviewTreeModel.h"

class SimulationWorld;
struct GenomePreviewDrawContext;

class GenomePreviewTreeRenderer {
public:
    int draw(const GenomePreviewDrawContext& drawContext,
            const SimulationWorld& renderWorld,
            const std::vector<GenomePreviewTreeNode>& nodes,
            const std::vector<float>& lineVertices,
            int selectedGeneIndex);
};
