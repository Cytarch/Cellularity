#include "app/genome_preview/GenomePreviewRenderer.h"

#include <memory>
#include <stdexcept>

#include "app/RenderStateSnapshots.h"
#include "app/genome_preview/GenomePreviewDrawContext.h"
#include "app/genome_preview/GenomePreviewRenderPipeline.h"
#include "app/genome_preview/ghost/GenomePreviewGhostController.h"
#include "app/genome_preview/layers/GenomePreviewFrameRenderer.h"
#include "app/genome_preview/tree/GenomePreviewTreeController.h"
#include "render/backend/RenderSurfaceSession.h"

GenomePreviewRenderer::GenomePreviewRenderer()
        : mFrameRenderer(std::make_unique<GenomePreviewFrameRenderer>()),
          mPipeline(std::make_unique<GenomePreviewRenderPipeline>()) {
}

GenomePreviewRenderer::~GenomePreviewRenderer() = default;

cellularity::render::RenderFrameResult GenomePreviewRenderer::draw(
        const SimulationWorld& world,
        const NativeViewport& viewport,
        NativeSimulationWorldRenderer& synchronousRenderer,
        cellularity::render::RenderSurfaceSession& backend,
        GenomePreviewTreeController& tree,
        GenomePreviewGhostController& ghost,
        const GenomePreviewViewState& viewState,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata,
        bool fullyTransparentClear) {
    GenomePreviewFrame frame;
    if (!mFrameRenderer->begin(
                viewport, backend, frame, fullyTransparentClear)) {
        return cellularity::render::makeRenderFrameResult(
                cellularity::render::RenderFrameOutcome::SkippedNotReady);
    }

    captureMetadata.viewport = cellularity::app::snapshotViewport(
            viewport, frame.matrix, frame.viewBounds);
    GenomePreviewDrawContext drawContext{
            world,
            viewport,
            synchronousRenderer,
            backend,
            frame,
            captureMetadata
    };
    try {
        const int visibleCount =
                mPipeline->draw(drawContext, tree, ghost, viewState);
        if (visibleCount < 0) {
            throw std::logic_error(
                    "Genome Preview visible Cell count cannot be negative");
        }
        const cellularity::render::RenderFrameResult backendResult =
                backend.endGenomePreviewFrame();
        if (!cellularity::render::isValidRenderFrameResult(backendResult)
                || backendResult.semanticValue != 0
                || (backendResult.outcome
                                == cellularity::render::RenderFrameOutcome::Presented
                        && backendResult.flags
                                != cellularity::render::kPrimaryContentPresented)) {
            throw std::logic_error(
                    "Genome Preview backend returned an invalid frame result");
        }
        return cellularity::render::withPresentedContent(
                backendResult,
                cellularity::render::kPrimaryContentPresented,
                visibleCount);
    } catch (...) {
        backend.cancelGenomePreviewFrame();
        throw;
    }
}

GenomePreviewRenderer::OffscreenResult GenomePreviewRenderer::drawOffscreen(
        const SimulationWorld& world,
        const NativeViewport& viewport,
        NativeSimulationWorldRenderer& synchronousRenderer,
        cellularity::render::RenderSurfaceSession& backend,
        GenomePreviewTreeController& tree,
        GenomePreviewGhostController& ghost,
        const GenomePreviewViewState& viewState,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata,
        bool fullyTransparentClear) {
    GenomePreviewFrame frame;
    if (!mFrameRenderer->begin(
                viewport, backend, frame, fullyTransparentClear)) {
        return {};
    }

    captureMetadata.viewport = cellularity::app::snapshotViewport(
            viewport, frame.matrix, frame.viewBounds);
    GenomePreviewDrawContext drawContext{
            world,
            viewport,
            synchronousRenderer,
            backend,
            frame,
            captureMetadata
    };
    try {
        const int visibleCount =
                mPipeline->draw(drawContext, tree, ghost, viewState);
        if (visibleCount < 0) {
            throw std::logic_error(
                    "Genome Preview visible Cell count cannot be negative");
        }
        return {backend.endGenomePreviewOffscreenPass(), visibleCount};
    } catch (...) {
        backend.cancelGenomePreviewFrame();
        throw;
    }
}
