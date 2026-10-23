#include "app/genome_preview/GenomePreviewController.h"

#include <memory>
#include <stdexcept>

#include "app/genome_preview/GenomeFilePreviewSimulation.h"
#include "app/genome_preview/GenomePreviewRenderer.h"
#include "app/genome_preview/ghost/GenomePreviewGhostController.h"
#include "app/genome_preview/selection/GenomePreviewCellsSelection.h"
#include "app/genome_preview/tree/GenomePreviewTreeController.h"
#include "render/NativeSimulationWorldRenderer.h"
#include "render/backend/RenderSurfaceSession.h"

GenomePreviewController::GenomePreviewController(int maxCellCount)
        : mInitialRendererCellCapacity(maxCellCount),
          mRenderer(std::make_unique<GenomePreviewRenderer>()),
          mCellsSelection(std::make_unique<GenomePreviewCellsSelection>()),
          mTree(std::make_unique<GenomePreviewTreeController>()),
          mGhost(std::make_unique<GenomePreviewGhostController>()) {
}

GenomePreviewController::~GenomePreviewController() = default;

NativeSimulationWorldRenderer& GenomePreviewController::synchronousRenderer() {
    if (mSynchronousRenderer == nullptr) {
        mSynchronousRenderer =
                std::make_unique<NativeSimulationWorldRenderer>(mInitialRendererCellCapacity);
    }
    return *mSynchronousRenderer;
}

void GenomePreviewController::setGridEnabled(bool enabled) {
    mGridEnabled = enabled;
}

void GenomePreviewController::setSelectedGeneIndex(int selectedGeneIndex) {
    mSelectedGeneIndex = selectedGeneIndex;
}

void GenomePreviewController::setTreeEnabled(NativeWorldContext context, bool enabled) {
    mTree->setEnabled(enabled);
    context.viewport.setCameraBoundsEnabled(!enabled);
}

GenomePreviewBaseMode GenomePreviewController::baseMode() const {
    if (mTree->enabled()) {
        return GenomePreviewBaseMode::Tree;
    }
    return GenomePreviewBaseMode::Cells;
}

GenomePreviewViewState GenomePreviewController::viewState() const {
    GenomePreviewViewState state;
    state.baseMode = baseMode();
    state.gridEnabled = mGridEnabled;
    state.ghostEnabled = state.baseMode != GenomePreviewBaseMode::Tree
            && mGhost->hasGhost();
    state.selectedGeneIndex = mSelectedGeneIndex;
    return state;
}

void GenomePreviewController::resetTreeState(NativeWorldContext context) {
    mTree->reset(context.world);
}

void GenomePreviewController::beginTreeStep(NativeWorldContext context) {
    mTree->beginStep(context.world);
}

void GenomePreviewController::recordTreeSplits(NativeWorldContext context) {
    mTree->recordSplits(context.world);
}

void GenomePreviewController::captureGhost(
        NativeWorldContext context,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    mGhost->capture(context, synchronousRenderer(), baseMode(), captureMetadata);
}

void GenomePreviewController::clearGhost() {
    mGhost->clear();
}

cellularity::render::RenderFrameResult GenomePreviewController::draw(
        NativeWorldContext context,
        cellularity::render::RenderSurfaceSession& backend,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    NativeSimulationWorldRenderer& renderer = synchronousRenderer();
    return mRenderer->draw(context.world,
            context.viewport,
            renderer,
            backend,
            *mTree,
            *mGhost,
            viewState(),
            captureMetadata);
}

std::vector<std::uint8_t> GenomePreviewController::captureFilePreviewRgba(
        const cellularity::GenomeData& genome,
        double maximumSimulationTime,
        cellularity::render::RenderSurfaceSession& backend) {
    using namespace cellularity::genome_preview;

    DevelopedGenomePreview developed = developGenomeForFilePreview(
            genome, mInitialRendererCellCapacity, maximumSimulationTime);
    const GenomePreviewFraming framing =
            frameGenomeFilePreview(*developed.world);
    if (framing.visibleCellCount <= 0) {
        throw std::runtime_error(
                "Genome file preview produced no living Cells");
    }

    NativeViewport viewport(framing.halfExtent);
    viewport.setCameraBoundsEnabled(false);
    viewport.setSurfaceSize(
            kGenomeFilePreviewPixels, kGenomeFilePreviewPixels);
    viewport.setCamera(
            framing.centerX, framing.centerY, 1.0f / framing.halfExtent);

    GenomePreviewViewState fileViewState;
    fileViewState.baseMode = GenomePreviewBaseMode::Cells;
    fileViewState.gridEnabled = false;
    fileViewState.ghostEnabled = false;
    fileViewState.selectedGeneIndex = -1;

    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.stage = cellularity::render::RenderCaptureStage::GenomePreview;
    metadata.completedUpdateId =
            static_cast<std::uint64_t>(developed.completedUpdates);

    backend.beginOffscreenRgba(
            cellularity::render::OffscreenRenderTarget::GenomePreview,
            kGenomeFilePreviewPixels, kGenomeFilePreviewPixels);
    try {
        const GenomePreviewRenderer::OffscreenResult renderResult =
                mRenderer->drawOffscreen(*developed.world,
                        viewport,
                        synchronousRenderer(),
                        backend,
                        *mTree,
                        *mGhost,
                        fileViewState,
                        metadata,
                        true);
        if (renderResult.status
                != cellularity::render::RenderPassStatus::Completed) {
            throw std::runtime_error(
                    "Genome file preview render pass failed");
        }
        if (renderResult.visibleCellCount != framing.visibleCellCount) {
            throw std::runtime_error(
                    "Genome file preview framing omitted living Cells");
        }
        return backend.finishOffscreenRgba(
                cellularity::render::OffscreenRenderTarget::GenomePreview);
    } catch (...) {
        backend.cancelOffscreenRgba(
                cellularity::render::OffscreenRenderTarget::GenomePreview);
        throw;
    }
}

int GenomePreviewController::selectGeneAt(const SimulationWorld& world, double worldX, double worldY) {
    if (baseMode() == GenomePreviewBaseMode::Tree) {
        return mTree->selectGeneAt(world, worldX, worldY);
    }

    return mCellsSelection->selectGeneAt(world, worldX, worldY);
}

int GenomePreviewController::treeNodeCount() const {
    return mTree->nodeCount();
}
