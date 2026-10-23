#pragma once

#include <vector>

#include "app/genome_preview/tree/GenomePreviewTreeModel.h"

class GenomePreviewTreeSelection {
public:
    int selectGeneAt(const std::vector<GenomePreviewTreeNode>& nodes,
            double worldX,
            double worldY) const;
};
