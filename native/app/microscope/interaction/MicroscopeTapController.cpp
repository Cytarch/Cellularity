#include "app/microscope/interaction/MicroscopeTapController.h"

#include "app/microscope/MicroscopeInputController.h"
#include "app/microscope/interaction/MicroscopeInteraction.h"
#include "app/microscope/interaction/MicroscopeSelectionController.h"

bool MicroscopeTapController::applyQueuedTap(
        NativeWorldContext context, MicroscopeInputController& input,
        MicroscopeSelectionController& selection, int toolOrdinal) {
    QueuedMicroscopeTap tap;
    if (!input.consumeQueuedTap(tap)) {
        return false;
    }

    SimulationWorld& world = context.world;
    bool hadSelection = world.mCellData.getIdFromSelectTag(1) != -1;
    MicroscopeInteraction::MicroscopeTapResult result =
            MicroscopeInteraction::applyMicroscopeTapAtWorld(
                    world, tap.worldX, tap.worldY, toolOrdinal, hadSelection,
                    tap.hasGenome ? &tap.genome : nullptr, false);
    selection.syncFromViewport(context);
    return result.changed;
}
