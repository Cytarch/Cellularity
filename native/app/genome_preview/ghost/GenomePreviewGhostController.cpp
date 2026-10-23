#include "app/genome_preview/ghost/GenomePreviewGhostController.h"

#include "app/RenderStateSnapshots.h"
#include "core/CellData.h"
#include "render/NativeSimulationWorldRenderer.h"

void GenomePreviewGhostController::capture(NativeWorldContext context,
        NativeSimulationWorldRenderer& synchronousRenderer,
        GenomePreviewBaseMode baseMode,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    NativeViewport& viewport = context.viewport;
    if (baseMode == GenomePreviewBaseMode::Tree
            || !viewport.isReady()
            || viewport.viewWidth() == 0.0f
            || viewport.viewHeight() == 0.0f) {
        clear();
        return;
    }
    const float radius = static_cast<float>(context.world.mEnvironment.geometry().radius() + CellData::MAX_CELL_RADIUS * 4.0);
    NativeSimulationWorldRenderer::ViewBounds viewBounds{-radius, radius, -radius, radius};
    const NativeViewport::ViewBounds visibleBounds = viewport.viewBounds(0.0f);
    captureMetadata.stage = cellularity::render::RenderCaptureStage::GenomePreviewGhost;
    captureMetadata.viewport = cellularity::app::snapshotViewport(
            viewport, viewBounds);
    captureMetadata.viewport.visibleBounds =
            cellularity::app::toRenderBounds(visibleBounds);
    int visibleCount = synchronousRenderer.copyToDraw(
            context.world, viewBounds, captureMetadata);
    synchronousRenderer.captureDrawSnapshot(mSnapshot, true);
    mEnabled = visibleCount > 0 && mSnapshot.valid;
}

void GenomePreviewGhostController::clear() {
    mEnabled = false;
    mSnapshot.valid = false;
    mSnapshot.geometryReady = false;
}

bool GenomePreviewGhostController::hasGhost() const {
    return mEnabled && mSnapshot.valid;
}

void GenomePreviewGhostController::restore(NativeSimulationWorldRenderer& synchronousRenderer) {
    synchronousRenderer.restoreDrawSnapshot(mSnapshot);
}
