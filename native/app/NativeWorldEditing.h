#pragma once

#include "core/CellData.h"

class SimulationWorld;

bool addUserCellToWorld(SimulationWorld& world,
        double worldX,
        double worldY,
        const cellularity::GenomeData& genome);

void resetWorldContents(SimulationWorld& world);
void resetGenomePreviewWorld(SimulationWorld& world);
