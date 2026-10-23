#pragma once

#include <vector>

#include "app/genome_preview/tree/GenomePreviewTreeModel.h"
#include "core/SimulationWorld.h"

class GenomePreviewTreeState {
public:
    void reset(SimulationWorld& world);
    void beginStep(SimulationWorld& world);
    void recordSplits(SimulationWorld& world);

    int nodeCount() const;
    const std::vector<GenomePreviewTreeNode>& nodes() const;
    std::vector<GenomePreviewTreeNode>& mutableNodes();

private:
    std::vector<GenomePreviewTreeNode> mNodes;
    std::vector<int> mCellTreeNodeIds;
    std::vector<SimulationWorld::SplitEvent> mSplitEvents;
};
