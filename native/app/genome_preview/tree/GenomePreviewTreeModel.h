#pragma once

#include <array>

#include "core/Gene.h"

struct GenomePreviewTreeNode {
    int geneIndex = -1;
    int child1NodeIndex = -1;
    int child2NodeIndex = -1;
    double birthTime = 0.0;
    double splitTime = -1.0;
    float lineStartX = 0.0f;
    float lineEndX = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
    std::array<float, 3> color{0.6f, 0.6f, 0.6f};
    GeneData renderGene{};
};
