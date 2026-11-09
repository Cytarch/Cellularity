#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "app/NativeWorldContext.h"
#include "app/genome_preview/GenomePreviewViewState.h"
#include "core/GenomePool.h"
#include "render/backend/RenderFrameResult.h"
#include "render/frame/PreparedRenderFrame.h"

class NativeViewport;
class GenomePreviewCellsSelection;
class GenomePreviewGhostController;
class GenomePreviewRenderer;
class GenomePreviewTreeController;
class NativeSimulationWorldRenderer;

namespace cellularity::render {
class RenderSurfaceSession;
} // namespace cellularity::render

class GenomePreviewController {
public:
    /**
     * Where the preview's camera stands, when the caller has one.
     *
     * The file preview frames the organism itself, which is what a saved genome wants: the picture
     * that travels with a genome should show the whole creature. A preview somebody is looking at
     * and moving is the other case -- they have panned and zoomed, and the picture has to be taken
     * from where they put the camera rather than from where the renderer would have chosen.
     */
    struct Camera {
        double centerX = 0.0;
        double centerY = 0.0;
        float zoom = 1.0f;
    };

    explicit GenomePreviewController(int maxCellCount);
    ~GenomePreviewController();

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
    [[nodiscard]] std::vector<std::uint8_t> captureFilePreviewRgba(
            const cellularity::GenomeData& genome,
            double maximumSimulationTime,
            cellularity::render::RenderSurfaceSession& backend,
            const Camera* camera = nullptr);
    /**
     * Draws the world the session is actually holding into memory the caller owns.
     *
     * The file preview above grows its own world and frames it itself, which is right for a picture
     * that travels with a genome. This one draws the world the published content built and the
     * published view is looking at, so a camera somebody moved is the camera the picture comes
     * from. It is the same renderer and the same offscreen pass; only the world differs.
     *
     * Returns how many bytes were written, and reports through `wrote` whether a frame was
     * produced at all, so a caller can tell "nothing to draw yet" from "drawing failed".
     */
    [[nodiscard]] std::size_t drawIntoRgba(
            SimulationWorld& world,
            NativeViewport& viewport,
            cellularity::render::RenderSurfaceSession& backend,
            std::uint8_t* destination,
            std::size_t destinationSize,
            std::size_t rowStride,
            bool& wrote);

    int selectGeneAt(const SimulationWorld& world, double worldX, double worldY);
    int treeNodeCount() const;

private:
    NativeSimulationWorldRenderer& synchronousRenderer();
    GenomePreviewBaseMode baseMode() const;
    GenomePreviewViewState viewState() const;

    bool mGridEnabled = true;
    int mSelectedGeneIndex = -1;
    int mInitialRendererCellCapacity;
    // Lazily owned so ordinary microscope controllers pay no Preview memory cost.
    // This renderer is never shared with NativeRenderSession or frame exchange.
    std::unique_ptr<NativeSimulationWorldRenderer> mSynchronousRenderer;
    std::unique_ptr<GenomePreviewRenderer> mRenderer;
    std::unique_ptr<GenomePreviewCellsSelection> mCellsSelection;
    std::unique_ptr<GenomePreviewTreeController> mTree;
    std::unique_ptr<GenomePreviewGhostController> mGhost;
};
