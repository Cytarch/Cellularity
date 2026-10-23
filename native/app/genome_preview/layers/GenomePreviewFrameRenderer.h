#pragma once

#include <array>

#include "app/interaction/NativeViewport.h"
#include "render/NativeSimulationWorldRenderer.h"

namespace cellularity::render {
class RenderSurfaceSession;
} // namespace cellularity::render

struct GenomePreviewFrame {
    std::array<float, 16> matrix{};
    NativeViewport::ViewBounds viewportBounds{};
    NativeSimulationWorldRenderer::ViewBounds viewBounds{};
};

class GenomePreviewFrameRenderer {
public:
    bool begin(const NativeViewport& viewport,
            cellularity::render::RenderSurfaceSession& backend,
            GenomePreviewFrame& frame,
            bool fullyTransparentClear = false);
};
