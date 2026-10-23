#include "app/NativeGenomePreviewFacade.h"

#include <memory>

#include "app/genome_preview/GenomePreviewController.h"

NativeGenomePreviewFacade::NativeGenomePreviewFacade(int maxCellCount)
        : mPreview(std::make_unique<GenomePreviewController>(maxCellCount)) {
}

NativeGenomePreviewFacade::~NativeGenomePreviewFacade() = default;

void NativeGenomePreviewFacade::setGridEnabled(bool enabled) {
    mPreview->setGridEnabled(enabled);
}

void NativeGenomePreviewFacade::setSelectedGeneIndex(int selectedGeneIndex) {
    mPreview->setSelectedGeneIndex(selectedGeneIndex);
}

void NativeGenomePreviewFacade::setTreeEnabled(NativeWorldContext context, bool enabled) {
    mPreview->setTreeEnabled(context, enabled);
}

void NativeGenomePreviewFacade::resetTreeState(NativeWorldContext context) {
    mPreview->resetTreeState(context);
}

void NativeGenomePreviewFacade::beginTreeStep(NativeWorldContext context) {
    mPreview->beginTreeStep(context);
}

void NativeGenomePreviewFacade::recordTreeSplits(NativeWorldContext context) {
    mPreview->recordTreeSplits(context);
}

void NativeGenomePreviewFacade::captureGhost(
        NativeWorldContext context,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    mPreview->captureGhost(context, captureMetadata);
}

void NativeGenomePreviewFacade::clearGhost() {
    mPreview->clearGhost();
}

cellularity::render::RenderFrameResult NativeGenomePreviewFacade::draw(
        NativeWorldContext context,
        cellularity::render::RenderSurfaceSession& backend,
        cellularity::render::RenderFrameCaptureMetadata captureMetadata) {
    return mPreview->draw(context, backend, captureMetadata);
}

std::vector<std::uint8_t> NativeGenomePreviewFacade::captureFilePreviewRgba(
        const cellularity::GenomeData& genome,
        double maximumSimulationTime,
        cellularity::render::RenderSurfaceSession& backend) {
    return mPreview->captureFilePreviewRgba(
            genome, maximumSimulationTime, backend);
}

int NativeGenomePreviewFacade::selectGeneAt(const SimulationWorld& world, double worldX, double worldY) {
    return mPreview->selectGeneAt(world, worldX, worldY);
}

int NativeGenomePreviewFacade::treeNodeCount() const {
    return mPreview->treeNodeCount();
}
