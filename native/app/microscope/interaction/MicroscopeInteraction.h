#pragma once

#include "core/CellData.h"

class SimulationWorld;

namespace MicroscopeInteraction {

struct MicroscopeTapResult {
    bool changed = false;
    int selectedCellId = -1;
};

MicroscopeTapResult applyMicroscopeTapAtWorld(SimulationWorld& world,
        double worldX,
        double worldY,
        int toolOrdinal,
        bool hadSelection,
        const cellularity::GenomeData* genome,
        bool requireGenomeForCell);

} // namespace MicroscopeInteraction
