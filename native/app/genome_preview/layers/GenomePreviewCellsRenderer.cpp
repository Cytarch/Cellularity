#include "app/genome_preview/layers/GenomePreviewCellsRenderer.h"

#include <vector>

#include "app/RenderStateSnapshots.h"
#include "app/genome_preview/GenomePreviewDrawContext.h"
#include "core/SimulationWorld.h"
#include "render/backend/RenderSurfaceSession.h"

int GenomePreviewCellsRenderer::draw(
        const GenomePreviewDrawContext& drawContext,
        int selectedGeneIndex,
        bool directionArrowEnabled) {
    const SimulationWorld& world = drawContext.world;
    NativeSimulationWorldRenderer& renderer = drawContext.synchronousRenderer;

    std::vector<NativeSimulationWorldRenderer::MarkerCaptureRequest> markerRequests;
    markerRequests.reserve(world.mCellData.mCellCount);
    for (int cellIndex = 0; cellIndex < world.mCellData.mCellCount; cellIndex++) {
        if (!world.mCellData.mIsDead[cellIndex]
                && world.mCellData.activeGeneIndex(cellIndex).value()
                        == selectedGeneIndex) {
            markerRequests.push_back({cellIndex, 0.01f, 0.3f});
        }
    }

    int visibleCount = renderer.copyToDraw(
            world,
            drawContext.frame.viewBounds,
            drawContext.captureMetadata,
            markerRequests.data(),
            markerRequests.size());
    const auto presentation = cellularity::app::snapshotPresentation(
            drawContext.viewport, drawContext.frame.matrix, {});
    const auto& frame = renderer.publishPreparedFrame(
            presentation);
    cellularity::render::GenomePreviewDrawOptions options;
    options.drawDirectionArrow = directionArrowEnabled;
    drawContext.backend.drawGenomePreview(presentation, frame, options);
    return visibleCount;
}
