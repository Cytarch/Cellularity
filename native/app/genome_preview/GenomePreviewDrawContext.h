#pragma once

#include "app/genome_preview/layers/GenomePreviewFrameRenderer.h"
#include "render/frame/PreparedRenderFrame.h"

class SimulationWorld;
class NativeSimulationWorldRenderer;
class NativeViewport;

namespace cellularity::render {
class RenderSurfaceSession;
} // namespace cellularity::render

struct GenomePreviewDrawContext {
    const SimulationWorld& world;
    const NativeViewport& viewport;
    NativeSimulationWorldRenderer& synchronousRenderer;
    cellularity::render::RenderSurfaceSession& backend;
    const GenomePreviewFrame& frame;
    cellularity::render::RenderFrameCaptureMetadata captureMetadata;
};
