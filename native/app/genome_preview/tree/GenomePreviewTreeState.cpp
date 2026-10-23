#include "app/genome_preview/tree/GenomePreviewTreeState.h"

#include "core/CellData.h"

void GenomePreviewTreeState::reset(SimulationWorld& world) {
    mNodes.clear();
    mCellTreeNodeIds.assign(world.mEnvironment.maxCellCount, -1);
    mSplitEvents.assign(world.mEnvironment.maxCellCount, {});

    CellData& cellData = world.mCellData;
    if (cellData.mCellCount == 0) {
        return;
    }

    int rootCellIndex = 0;
    int rootGeneIndex = cellData.activeGeneIndex(rootCellIndex).value();
    GenomePreviewTreeNode root;
    root.geneIndex = rootGeneIndex;
    if (rootGeneIndex >= 0
            && static_cast<std::size_t>(rootGeneIndex)
                    < cellData.geneCount(rootCellIndex)) {
        const cellularity::GeneView gene = cellData.getGeneView(
                rootCellIndex,
                cellularity::GeneIndex(static_cast<
                        cellularity::GeneIndex::Value>(rootGeneIndex)));
        root.color = {gene.color(0), gene.color(1), gene.color(2)};
        cellularity::GeneBuilder(root.renderGene).copyFrom(gene);
    }
    mNodes.push_back(root);
    mCellTreeNodeIds[rootCellIndex] = 0;
}

void GenomePreviewTreeState::beginStep(SimulationWorld& world) {
    if (mSplitEvents.size() < static_cast<size_t>(world.mEnvironment.maxCellCount)) {
        mSplitEvents.assign(world.mEnvironment.maxCellCount, {});
    }
    world.beginSplitEventRecording(mSplitEvents.data(), static_cast<int>(mSplitEvents.size()));
}

void GenomePreviewTreeState::recordSplits(SimulationWorld& world) {
    int splitEventCount = world.splitEventCount();
    world.endSplitEventRecording();
    if (splitEventCount <= 0) {
        return;
    }
    if (mCellTreeNodeIds.size() < static_cast<size_t>(world.mEnvironment.maxCellCount)) {
        mCellTreeNodeIds.resize(world.mEnvironment.maxCellCount, -1);
    }

    CellData& cellData = world.mCellData;
    for (int splitEventIndex = 0; splitEventIndex < splitEventCount; splitEventIndex++) {
        const SimulationWorld::SplitEvent& event = mSplitEvents[splitEventIndex];
        int parentCellIndex = event.parentCellIndex;
        int child2CellIndex = event.child2CellIndex;
        int child1GeneIndex = event.child1GeneIndex;
        int child2GeneIndex = event.child2GeneIndex;
        if (parentCellIndex < 0 || child2CellIndex < 0
                || parentCellIndex >= static_cast<int>(mCellTreeNodeIds.size())
                || child2CellIndex >= cellData.mCellCount) {
            continue;
        }
        int parentNodeIndex = mCellTreeNodeIds[parentCellIndex];
        if (parentNodeIndex < 0 || parentNodeIndex >= static_cast<int>(mNodes.size())) {
            continue;
        }

        GenomePreviewTreeNode child1;
        child1.geneIndex = child1GeneIndex;
        child1.birthTime = event.splitTime;
        if (child1GeneIndex >= 0
                && static_cast<std::size_t>(child1GeneIndex)
                        < cellData.geneCount(parentCellIndex)) {
            const cellularity::GeneView gene = cellData.getGeneView(
                    parentCellIndex,
                    cellularity::GeneIndex(static_cast<
                            cellularity::GeneIndex::Value>(child1GeneIndex)));
            child1.color = {gene.color(0), gene.color(1), gene.color(2)};
            cellularity::GeneBuilder(child1.renderGene).copyFrom(gene);
        }

        GenomePreviewTreeNode child2;
        child2.geneIndex = child2GeneIndex;
        child2.birthTime = event.splitTime;
        if (child2GeneIndex >= 0
                && static_cast<std::size_t>(child2GeneIndex)
                        < cellData.geneCount(child2CellIndex)) {
            const cellularity::GeneView gene = cellData.getGeneView(
                    child2CellIndex,
                    cellularity::GeneIndex(static_cast<
                            cellularity::GeneIndex::Value>(child2GeneIndex)));
            child2.color = {gene.color(0), gene.color(1), gene.color(2)};
            cellularity::GeneBuilder(child2.renderGene).copyFrom(gene);
        }

        int child1NodeIndex = static_cast<int>(mNodes.size());
        int child2NodeIndex = child1NodeIndex + 1;
        mNodes.push_back(child1);
        mNodes.push_back(child2);

        GenomePreviewTreeNode& parent = mNodes[parentNodeIndex];
        parent.child1NodeIndex = child1NodeIndex;
        parent.child2NodeIndex = child2NodeIndex;
        parent.splitTime = event.splitTime;

        mCellTreeNodeIds[parentCellIndex] = child1NodeIndex;
        if (child2CellIndex >= 0 && child2CellIndex < static_cast<int>(mCellTreeNodeIds.size())) {
            mCellTreeNodeIds[child2CellIndex] = child2NodeIndex;
        }
    }
}

int GenomePreviewTreeState::nodeCount() const {
    return static_cast<int>(mNodes.size());
}

const std::vector<GenomePreviewTreeNode>& GenomePreviewTreeState::nodes() const {
    return mNodes;
}

std::vector<GenomePreviewTreeNode>& GenomePreviewTreeState::mutableNodes() {
    return mNodes;
}
