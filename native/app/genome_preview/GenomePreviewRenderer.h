#pragma once

#include <memory>

#include "app/genome_preview/GenomePreviewViewState.h"
#include "render/backend/RenderFrameResult.h"
#include "render/frame/PreparedRenderFrame.h"

class SimulationWorld;
class GenomePreviewFrameRenderer;
class GenomePreviewGhostController;
class GenomePreviewRenderPipeline;
class GenomePreviewTreeController;
class NativeSimulationWorldRenderer;
class NativeViewport;

namespace cellularity::render {
class RenderSurfaceSession;
} // namespace cellularity::render

class GenomePreviewRenderer {
public:
    struct OffscreenResult final {
        cellularity::render::RenderPassStatus status =
                cellularity::render::RenderPassStatus::Failed;
        int visibleCellCount = 0;
    };

    GenomePreviewRenderer();
    ~GenomePreviewRenderer();

    [[nodiscard]] cellularity::render::RenderFrameResult draw(
            const SimulationWorld& world,
            const NativeViewport& viewport,
            NativeSimulationWorldRenderer& synchronousRenderer,
            cellularity::render::RenderSurfaceSession& backend,
            GenomePreviewTreeController& tree,
            GenomePreviewGhostController& ghost,
            const GenomePreviewViewState& viewState,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata,
            bool fullyTransparentClear = false);
    [[nodiscard]] OffscreenResult drawOffscreen(const SimulationWorld& world,
            const NativeViewport& viewport,
            NativeSimulationWorldRenderer& synchronousRenderer,
            cellularity::render::RenderSurfaceSession& backend,
            GenomePreviewTreeController& tree,
            GenomePreviewGhostController& ghost,
            const GenomePreviewViewState& viewState,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata,
            bool fullyTransparentClear);

private:
    std::unique_ptr<GenomePreviewFrameRenderer> mFrameRenderer;
    std::unique_ptr<GenomePreviewRenderPipeline> mPipeline;
};
