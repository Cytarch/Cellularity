#include "app/genome_preview/tree/GenomePreviewTreeSelection.h"

#include <limits>

int GenomePreviewTreeSelection::selectGeneAt(const std::vector<GenomePreviewTreeNode>& nodes,
        double worldX,
        double worldY) const {
    int selectedGeneIndex = -1;
    float bestDistanceSq = std::numeric_limits<float>::max();
    for (const GenomePreviewTreeNode& node : nodes) {
        float dx = static_cast<float>(worldX) - node.x;
        float dy = static_cast<float>(worldY) - node.y;
        float radius = node.radius * 1.45f;
        float distanceSq = dx * dx + dy * dy;
        if (distanceSq <= radius * radius && distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            selectedGeneIndex = node.geneIndex;
        }
    }
    return selectedGeneIndex;
}
