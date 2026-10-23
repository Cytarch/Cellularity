#include "app/genome_preview/tree/GenomePreviewTreeRenderer.h"

#include "app/RenderStateSnapshots.h"
#include "app/genome_preview/GenomePreviewDrawContext.h"
#include "core/SimulationWorld.h"
#include "render/backend/RenderSurfaceSession.h"

int GenomePreviewTreeRenderer::draw(const GenomePreviewDrawContext& drawContext,
        const SimulationWorld& renderWorld,
        const std::vector<GenomePreviewTreeNode>& nodes,
        const std::vector<float>& lineVertices,
        int selectedGeneIndex) {
    NativeSimulationWorldRenderer& renderer = drawContext.synchronousRenderer;

    std::vector<NativeSimulationWorldRenderer::MarkerCaptureRequest> markerRequests;
    markerRequests.reserve(nodes.size());
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const GenomePreviewTreeNode& node = nodes[nodeIndex];
        if (node.geneIndex == selectedGeneIndex) {
            markerRequests.push_back({static_cast<int>(nodeIndex), 0.01f, 0.3f});
        }
    }

    int visibleCount = renderer.copyToDraw(
            renderWorld,
            drawContext.frame.viewBounds,
            drawContext.captureMetadata,
            markerRequests.data(),
            markerRequests.size());
    const auto presentation = cellularity::app::snapshotPresentation(
            drawContext.viewport, drawContext.frame.matrix, {});
    const auto& frame = renderer.publishPreparedFrame(
            presentation);
    drawContext.backend.drawGenomePreviewTree(presentation, frame,
            drawContext.frame.matrix, lineVertices, {});
    return visibleCount;
}
