#include "app/genome_preview/layers/GenomePreviewGhostRenderer.h"

#include "app/RenderStateSnapshots.h"
#include "app/genome_preview/GenomePreviewDrawContext.h"
#include "app/genome_preview/ghost/GenomePreviewGhostController.h"
#include "render/backend/RenderSurfaceSession.h"

void GenomePreviewGhostRenderer::draw(const GenomePreviewDrawContext& drawContext,
        GenomePreviewGhostController& ghost) {
    if (!ghost.hasGhost()) {
        return;
    }
    ghost.restore(drawContext.synchronousRenderer);

    NativeSimulationWorldRenderer& renderer = drawContext.synchronousRenderer;
    const auto presentation = cellularity::app::snapshotPresentation(
            drawContext.viewport, drawContext.frame.matrix, {});
    const auto& frame = renderer.publishPreparedFrame(
            presentation);
    cellularity::render::GenomePreviewDrawOptions options;
    options.drawMarkers = false;
    drawContext.backend.drawGenomePreview(presentation, frame, options);
}
