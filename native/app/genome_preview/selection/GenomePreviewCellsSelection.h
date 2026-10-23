#pragma once

class SimulationWorld;

class GenomePreviewCellsSelection {
public:
    int selectGeneAt(const SimulationWorld& world, double worldX, double worldY) const;
};
