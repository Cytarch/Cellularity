#pragma once

#include "app/NativeWorldContext.h"
#include "app/genome_preview/GenomePreviewViewState.h"
#include "render/frame/PreparedRenderFrame.h"

class NativeSimulationWorldRenderer;

class GenomePreviewGhostController {
public:
    void capture(NativeWorldContext context,
            NativeSimulationWorldRenderer& synchronousRenderer,
            GenomePreviewBaseMode baseMode,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata);
    void clear();
    bool hasGhost() const;
    void restore(NativeSimulationWorldRenderer& synchronousRenderer);

private:
    cellularity::render::PreparedRenderFrame mSnapshot;
    bool mEnabled = false;
};
