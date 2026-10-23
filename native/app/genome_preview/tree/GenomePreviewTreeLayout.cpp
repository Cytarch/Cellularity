#include "app/genome_preview/tree/GenomePreviewTreeLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include "core/CellData.h"
#include "core/SimulationWorld.h"

namespace {

constexpr float GENOME_TREE_NODE_GAP_FACTOR = 0.35f;

} // namespace

void GenomePreviewTreeLayout::reset() {
    mLineVertices.clear();
    mLayoutTime = 0.0;
}

void GenomePreviewTreeLayout::update(const SimulationWorld& world, std::vector<GenomePreviewTreeNode>& nodes) {
    mLineVertices.clear();
    if (nodes.empty()) {
        return;
    }

    double maxTime = std::max(world.mTime, SimulationWorld::DELTA_TIME);
    for (const GenomePreviewTreeNode& node : nodes) {
        maxTime = std::max(maxTime, node.birthTime);
        if (node.splitTime >= 0.0) {
            maxTime = std::max(maxTime, node.splitTime);
        }
    }
    mLayoutTime = maxTime;

    const float worldRadius = static_cast<float>(world.mEnvironment.geometry().radius());
    const float left = -worldRadius * 0.88f;
    const float right = worldRadius * 0.88f;
    const bool alignToGrid = world.mCellCollisionGrid != nullptr && world.mCellCollisionGrid->gridSize() > 0;
    const float gridRadius = alignToGrid
            ? static_cast<float>(world.mCellCollisionGrid->worldRadius())
            : worldRadius;
    const float gridCellSize = alignToGrid
            ? static_cast<float>((2.0 * world.mCellCollisionGrid->worldRadius())
                      / static_cast<double>(world.mCellCollisionGrid->gridSize()))
            : 0.0f;
    auto snapToGridLine = [&](float value) -> float {
        if (!alignToGrid || gridCellSize <= 0.0f) {
            return value;
        }
        return -gridRadius + std::round((value + gridRadius) / gridCellSize) * gridCellSize;
    };

    std::vector<int> subtreeSplitCounts(nodes.size(), 0);
    std::vector<int> subtreeLeafCounts(nodes.size(), 0);
    std::function<void(int)> measureSubtree = [&](int nodeIndex) {
        GenomePreviewTreeNode& node = nodes[nodeIndex];
        if (node.child1NodeIndex < 0 || node.child2NodeIndex < 0) {
            subtreeSplitCounts[nodeIndex] = 0;
            subtreeLeafCounts[nodeIndex] = 1;
            return;
        }

        measureSubtree(node.child1NodeIndex);
        measureSubtree(node.child2NodeIndex);
        subtreeSplitCounts[nodeIndex] = 1
                + subtreeSplitCounts[node.child1NodeIndex]
                + subtreeSplitCounts[node.child2NodeIndex];
        subtreeLeafCounts[nodeIndex] = subtreeLeafCounts[node.child1NodeIndex]
                + subtreeLeafCounts[node.child2NodeIndex];
    };
    measureSubtree(0);

    auto orderedChildren = [&](const GenomePreviewTreeNode& node) -> std::array<int, 2> {
        int first = node.child1NodeIndex;
        int second = node.child2NodeIndex;
        if (subtreeSplitCounts[first] > subtreeSplitCounts[second]
                || (subtreeSplitCounts[first] == subtreeSplitCounts[second]
                        && subtreeLeafCounts[first] > subtreeLeafCounts[second])) {
            std::swap(first, second);
        }
        return {first, second};
    };

    struct LeafSpan {
        float first = 0.0f;
        float last = 0.0f;
    };

    int leafCount = 0;
    std::function<LeafSpan(int)> assignY = [&](int nodeIndex) -> LeafSpan {
        GenomePreviewTreeNode& node = nodes[nodeIndex];
        if (node.child1NodeIndex < 0 || node.child2NodeIndex < 0) {
            float y = static_cast<float>(leafCount);
            leafCount++;
            node.y = y;
            return {y, y};
        }
        std::array<int, 2> children = orderedChildren(node);
        LeafSpan firstSpan = assignY(children[0]);
        LeafSpan secondSpan = assignY(children[1]);
        LeafSpan span{
                std::min(firstSpan.first, secondSpan.first),
                std::max(firstSpan.last, secondSpan.last)
        };
        node.y = (span.first + span.last) * 0.5f;
        return span;
    };
    assignY(0);

    const float nodeRadius = static_cast<float>(CellData::MAX_CELL_RADIUS);
    const float minLaneSpacing = nodeRadius * (2.0f + GENOME_TREE_NODE_GAP_FACTOR);
    const float laneSpacing = alignToGrid && gridCellSize > 0.0f
            ? gridCellSize * std::ceil(minLaneSpacing / gridCellSize)
            : minLaneSpacing;
    if (leafCount > 1) {
        float treeTop = laneSpacing * static_cast<float>(leafCount - 1) * 0.5f;
        for (GenomePreviewTreeNode& node : nodes) {
            node.y = snapToGridLine(treeTop - node.y * laneSpacing);
        }
    } else {
        for (GenomePreviewTreeNode& node : nodes) {
            node.y = snapToGridLine(0.0f);
        }
    }

    const float lineWidth = std::max(worldRadius * 0.004f, nodeRadius * 0.16f);
    auto timeToX = [&](double time) -> float {
        double normalized = maxTime <= 0.0 ? 0.0 : std::max(0.0, std::min(1.0, time / maxTime));
        return left + static_cast<float>(normalized) * (right - left);
    };
    for (GenomePreviewTreeNode& node : nodes) {
        node.lineStartX = snapToGridLine(timeToX(node.birthTime));
        node.lineEndX = snapToGridLine(
                node.splitTime >= 0.0 ? timeToX(node.splitTime) : timeToX(maxTime));
        if (alignToGrid && gridCellSize > 0.0f && node.lineEndX <= node.lineStartX) {
            node.lineEndX = node.lineStartX + gridCellSize;
        }
        node.x = snapToGridLine((node.lineStartX + node.lineEndX) * 0.5f);
        node.radius = nodeRadius;
    }

    for (const GenomePreviewTreeNode& node : nodes) {
        if (node.child1NodeIndex >= 0 && node.child2NodeIndex >= 0) {
            const GenomePreviewTreeNode& child1 = nodes[node.child1NodeIndex];
            const GenomePreviewTreeNode& child2 = nodes[node.child2NodeIndex];
            appendSegment(node.lineStartX, node.y, node.lineEndX, node.y, lineWidth);
            appendSegment(node.lineEndX, child1.y, node.lineEndX, child2.y, lineWidth);
        } else {
            appendSegment(node.lineStartX, node.y, node.lineEndX, node.y, lineWidth);
        }
    }
}

const std::vector<float>& GenomePreviewTreeLayout::lineVertices() const {
    return mLineVertices;
}

double GenomePreviewTreeLayout::layoutTime() const {
    return mLayoutTime;
}

void GenomePreviewTreeLayout::appendSegment(float x1,
        float y1,
        float x2,
        float y2,
        float width) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.000001f) {
        return;
    }
    float extension = width * 0.5f;
    x1 -= dx / length * extension;
    y1 -= dy / length * extension;
    x2 += dx / length * extension;
    y2 += dy / length * extension;
    float nx = -dy / length * width * 0.5f;
    float ny = dx / length * width * 0.5f;
    float vertices[12] = {
            x1 + nx, y1 + ny,
            x1 - nx, y1 - ny,
            x2 - nx, y2 - ny,
            x1 + nx, y1 + ny,
            x2 - nx, y2 - ny,
            x2 + nx, y2 + ny
    };
    mLineVertices.insert(mLineVertices.end(), vertices, vertices + 12);
}
