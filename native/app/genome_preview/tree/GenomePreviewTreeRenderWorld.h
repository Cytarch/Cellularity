#pragma once

#include <memory>
#include <vector>

#include "app/genome_preview/tree/GenomePreviewTreeModel.h"

class SimulationWorld;
class NativeSimulationWorldRenderer;

class GenomePreviewTreeRenderWorld {
public:
    ~GenomePreviewTreeRenderWorld();

    const SimulationWorld* update(const SimulationWorld& sourceWorld,
            const std::vector<GenomePreviewTreeNode>& nodes,
            NativeSimulationWorldRenderer& renderer);

private:
    std::unique_ptr<SimulationWorld> mWorld;
};
