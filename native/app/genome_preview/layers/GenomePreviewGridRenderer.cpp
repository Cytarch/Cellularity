#include "app/genome_preview/layers/GenomePreviewGridRenderer.h"

#include "app/genome_preview/GenomePreviewDrawContext.h"
#include "core/SimulationWorld.h"
#include "core/CollisionGrid.h"
#include "render/backend/RenderSurfaceSession.h"

void GenomePreviewGridRenderer::draw(const GenomePreviewDrawContext& drawContext, bool enabled) {
    const SimulationWorld& world = drawContext.world;
    if (!enabled || world.mCellCollisionGrid == nullptr) {
        return;
    }

    cellularity::render::GenomePreviewGridCommand command;
    command.matrix = drawContext.frame.matrix;
    command.minX = drawContext.frame.viewportBounds.minX;
    command.maxX = drawContext.frame.viewportBounds.maxX;
    command.minY = drawContext.frame.viewportBounds.minY;
    command.maxY = drawContext.frame.viewportBounds.maxY;
    command.worldRadius = world.mCellCollisionGrid->worldRadius();
    command.gridSize = world.mCellCollisionGrid->gridSize();
    drawContext.backend.drawGenomePreviewGrid(command);
}
