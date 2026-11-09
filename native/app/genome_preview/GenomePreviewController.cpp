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
        cellularity::render::RenderSurfaceSession& backend,
        const Camera* camera) {
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
    // A camera from the caller is a person's camera: it is used as it stands, even where it shows
    // nothing, because a preview that snapped back to the organism whenever the view moved off it
    // would fight the hand that moved it.
    if (camera != nullptr) {
        viewport.setCamera(camera->centerX, camera->centerY, camera->zoom);
    } else {
        viewport.setCamera(
                framing.centerX, framing.centerY, 1.0f / framing.halfExtent);
    }

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

std::size_t GenomePreviewController::drawIntoRgba(
        SimulationWorld& world,
        NativeViewport& viewport,
        cellularity::render::RenderSurfaceSession& backend,
        std::uint8_t* destination,
        std::size_t destinationSize,
        std::size_t rowStride,
        bool& wrote) {
    wrote = false;
    const int width = viewport.surfaceWidth();
    const int height = viewport.surfaceHeight();
    if (destination == nullptr || width <= 0 || height <= 0
            || rowStride < static_cast<std::size_t>(width) * 4U
            || destinationSize < rowStride * static_cast<std::size_t>(height)) {
        return 0;
    }

    cellularity::render::RenderFrameCaptureMetadata metadata;
    metadata.stage = cellularity::render::RenderCaptureStage::GenomePreview;

    backend.beginOffscreenRgba(
            cellularity::render::OffscreenRenderTarget::GenomePreview, width, height);
    try {
        const GenomePreviewRenderer::OffscreenResult renderResult =
                // The renderer and the view state are the preview's own: taking them from the
                // caller would let the picture and the camera that produced it come from
                // different places, which is exactly the drift this whole change exists to end.
                mRenderer->drawOffscreen(world,
                        viewport,
                        synchronousRenderer(),
                        backend,
                        *mTree,
                        *mGhost,
                        viewState(),
                        metadata,
                        true);
        if (renderResult.status
                != cellularity::render::RenderPassStatus::Completed) {
            backend.cancelOffscreenRgba(
                    cellularity::render::OffscreenRenderTarget::GenomePreview);
            return 0;
        }
        const std::vector<std::uint8_t> pixels = backend.finishOffscreenRgba(
                cellularity::render::OffscreenRenderTarget::GenomePreview);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4U;
        if (pixels.size() < rowStride * static_cast<std::size_t>(height)) {
            return 0;
        }
        for (int row = 0; row < height; ++row) {
            std::memcpy(destination + (static_cast<std::size_t>(row) * rowStride),
                    pixels.data() + (static_cast<std::size_t>(row) * rowStride),
                    rowBytes);
        }
        wrote = true;
        return rowStride * static_cast<std::size_t>(height);
    } catch (...) {
        backend.cancelOffscreenRgba(
                cellularity::render::OffscreenRenderTarget::GenomePreview);
        return 0;
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
