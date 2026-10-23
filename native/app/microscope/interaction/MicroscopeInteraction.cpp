#include "app/microscope/interaction/MicroscopeInteraction.h"

#include <stdexcept>

#include "app/NativeWorldEditing.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"

namespace {

constexpr int TOOL_CELL = 0;
constexpr int TOOL_MOVE = 1;
constexpr int TOOL_ADD = 2;
constexpr int TOOL_REMOVE = 3;
constexpr int TOOL_DIAGNOSIS = 4;

} // namespace

namespace MicroscopeInteraction {

MicroscopeTapResult applyMicroscopeTapAtWorld(SimulationWorld& world,
        double worldX,
        double worldY,
        int toolOrdinal,
        bool hadSelection,
        const cellularity::GenomeData* genome,
        bool requireGenomeForCell) {
    CellData& cellData = world.mCellData;
    int clickedCellIndex = cellData.getCellIndex(worldX, worldY);
    bool changed = false;

    if (hadSelection && (toolOrdinal == TOOL_ADD || toolOrdinal == TOOL_REMOVE || toolOrdinal == TOOL_MOVE)) {
        cellData.deselectAll();
        changed = true;
    }

    if (clickedCellIndex == -1) {
        if (toolOrdinal == TOOL_CELL) {
            if (hadSelection) {
                cellData.deselectAll();
                changed = true;
            } else {
                if (genome == nullptr) {
                    if (requireGenomeForCell) {
                        throw std::runtime_error("Microscope CELL tap requires genome bytes");
                    }
                } else {
                    changed = addUserCellToWorld(world, worldX, worldY, *genome) || changed;
                }
            }
        }

        if (toolOrdinal == TOOL_DIAGNOSIS && hadSelection) {
            cellData.deselectAll();
            changed = true;
        }

        if (toolOrdinal == TOOL_ADD) {
            const int previousFoodCount = world.mFoodData.mFoodCount;
            world.mFoodData.addFood(worldX, worldY, Environment::MAX_FOOD_SIZE,
                    0.0f, world.mEnvironment);
            changed = world.mFoodData.mFoodCount != previousFoodCount || changed;
        }

        return {changed, cellData.getIdFromSelectTag(1)};
    }

    if (toolOrdinal == TOOL_DIAGNOSIS || toolOrdinal == TOOL_CELL) {
        if (cellData.mSelectTag[clickedCellIndex] != 1) {
            cellData.deselectAll();
            cellData.mSelectTag[clickedCellIndex] = 1;
        } else {
            cellData.deselectAll();
        }
        changed = true;
    }

    if (toolOrdinal == TOOL_ADD) {
        cellData.applySupportTool(clickedCellIndex);
        changed = true;
    }

    if (toolOrdinal == TOOL_REMOVE) {
        cellData.markCellAsDead(clickedCellIndex);
        changed = true;
    }

    return {changed, cellData.getIdFromSelectTag(1)};
}

} // namespace MicroscopeInteraction
