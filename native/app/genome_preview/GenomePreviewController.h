#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "app/NativeWorldContext.h"
#include "app/genome_preview/GenomePreviewViewState.h"
#include "core/GenomePool.h"
#include "render/backend/RenderFrameResult.h"
#include "render/frame/PreparedRenderFrame.h"

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
            cellularity::render::RenderSurfaceSession& backend);
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
