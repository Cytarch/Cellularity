#include "app/genome_preview/layers/GenomePreviewFrameRenderer.h"

#include "render/backend/RenderSurfaceSession.h"

bool GenomePreviewFrameRenderer::begin(const NativeViewport& viewport,
        cellularity::render::RenderSurfaceSession& backend,
        GenomePreviewFrame& frame,
        bool fullyTransparentClear) {
    if (!viewport.isReady() || viewport.viewWidth() == 0.0f || viewport.viewHeight() == 0.0f) {
        return false;
    }

    std::array<float, 16> identity{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
    };
    frame.matrix = viewport.worldMatrix(identity);
    frame.viewportBounds = viewport.viewBounds(0.0f);
    frame.viewBounds = {
            frame.viewportBounds.minX,
            frame.viewportBounds.maxX,
            frame.viewportBounds.minY,
            frame.viewportBounds.maxY
    };

    backend.beginGenomePreviewFrame(viewport.surfaceWidth(),
            viewport.surfaceHeight(), fullyTransparentClear);
    return true;
}
