#include "app/genome_preview/selection/GenomePreviewCellsSelection.h"

#include "core/CellData.h"
#include "core/SimulationWorld.h"

int GenomePreviewCellsSelection::selectGeneAt(const SimulationWorld& world, double worldX, double worldY) const {
    const CellData& cellData = world.mCellData;
    int cellIndex = cellData.getCellIndex(worldX, worldY);
    if (cellIndex < 0 || cellIndex >= cellData.mCellCount) {
        return -1;
    }
    return cellData.activeGeneIndex(cellIndex).value();
}
