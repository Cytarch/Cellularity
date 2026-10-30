#pragma once

#include "app/genome_preview/GenomePreviewController.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "app/NativeWorldContext.h"
#include "core/GenomePool.h"
#include "render/backend/RenderFrameResult.h"
#include "render/frame/PreparedRenderFrame.h"

class GenomePreviewController;

namespace cellularity::render {
class RenderSurfaceSession;
} // namespace cellularity::render

class NativeGenomePreviewFacade {
public:
    explicit NativeGenomePreviewFacade(int maxCellCount);
    ~NativeGenomePreviewFacade();

    void setGridEnabled(bool enabled);
    void setSelectedGeneIndex(int selectedGeneIndex);
    void setTreeEnabled(NativeWorldContext context, bool enabled);
    void resetTreeState(NativeWorldContext context);
    void beginTreeStep(NativeWorldContext context);
    void recordTreeSplits(NativeWorldContext context);
    void captureGhost(
            NativeWorldContext context,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata);
    void clearGhost();
    [[nodiscard]] cellularity::render::RenderFrameResult draw(
            NativeWorldContext context,
            cellularity::render::RenderSurfaceSession& backend,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata);
    /**
     * Draws the world this facade is holding into memory the caller owns.
     *
     * `draw` presents that world into the surface the session is attached to; this is the same
     * frame into a buffer instead, which is what a host needs when the preview is one layer of its
     * own scene rather than a window of its own. The camera is the preview's, because the preview
     * is what the published view state moved.
     */
    [[nodiscard]] std::size_t drawIntoRgba(
            NativeWorldContext context,
            cellularity::render::RenderSurfaceSession& backend,
            std::uint8_t* destination,
            std::size_t destinationSize,
            std::size_t rowStride,
            bool& wrote);

    [[nodiscard]] std::vector<std::uint8_t> captureFilePreviewRgba(
            const cellularity::GenomeData& genome,
            double maximumSimulationTime,
            cellularity::render::RenderSurfaceSession& backend,
            const GenomePreviewController::Camera* camera = nullptr);
    int selectGeneAt(const SimulationWorld& world, double worldX, double worldY);
    int treeNodeCount() const;

private:
    std::unique_ptr<GenomePreviewController> mPreview;
};
