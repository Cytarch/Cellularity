#pragma once

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
    [[nodiscard]] std::vector<std::uint8_t> captureFilePreviewRgba(
            const cellularity::GenomeData& genome,
            double maximumSimulationTime,
            cellularity::render::RenderSurfaceSession& backend);
    int selectGeneAt(const SimulationWorld& world, double worldX, double worldY);
    int treeNodeCount() const;

private:
    std::unique_ptr<GenomePreviewController> mPreview;
};
