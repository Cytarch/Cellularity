#pragma once

#include <vector>

#include "app/genome_preview/tree/GenomePreviewTreeModel.h"

class SimulationWorld;

class GenomePreviewTreeLayout {
public:
    void reset();
    void update(const SimulationWorld& world, std::vector<GenomePreviewTreeNode>& nodes);

    const std::vector<float>& lineVertices() const;
    double layoutTime() const;

private:
    void appendSegment(float x1, float y1, float x2, float y2, float width);

    std::vector<float> mLineVertices;
    double mLayoutTime = 0.0;
};
