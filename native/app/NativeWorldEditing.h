#pragma once

#include "core/CellData.h"

class SimulationWorld;

// Places a cell of [genome] at a world position, or, when [genome] is null, a cell of the
// plate's default genome. Returns false when the position is outside the dish or the cell
// could not be made.
bool addUserCellToWorld(SimulationWorld& world,
        double worldX,
        double worldY,
        const cellularity::GenomeData* genome);

// The same call for the callers that do have a genome: written as an overload so that naming a
// genome stays as ordinary as it was, and only the caller that has none has to say so.
inline bool addUserCellToWorld(SimulationWorld& world,
        double worldX,
        double worldY,
        const cellularity::GenomeData& genome) {
    return addUserCellToWorld(world, worldX, worldY, &genome);
}

void resetWorldContents(SimulationWorld& world);
void resetGenomePreviewWorld(SimulationWorld& world);
