#pragma once

#include <array>

#include "app/microscope/render/MicroscopeDebugHud.h"
#include "render/frame/RenderFrameTypes.h"

class NativeGlApi;
class NativeRenderFacade;
class NativeViewport;

namespace cellularity::microscope {
class MicroscopeRenderFramePipeline;
} // namespace cellularity::microscope

namespace cellularity::render {
class SimulationWorldGpuRenderer;
struct PreparedRenderFrame;
} // namespace cellularity::render

class MicroscopeRenderer {
public:
    bool draw(const NativeViewport& viewport,
            cellularity::microscope::MicroscopeRenderFramePipeline& frames,
            cellularity::render::SimulationWorldGpuRenderer& gpuRenderer,
            NativeRenderFacade& render, NativeGlApi& gl,
            const std::array<float, 3>& backgroundColor,
            bool uploadDynamicBuffers,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry = nullptr);
    bool draw(const cellularity::render::RenderPresentationState& presentation,
            cellularity::microscope::MicroscopeRenderFramePipeline& frames,
            cellularity::render::SimulationWorldGpuRenderer& gpuRenderer,
            NativeRenderFacade& render, NativeGlApi& gl,
            bool uploadDynamicBuffers,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry = nullptr);
    bool drawPreparedFrame(
            const cellularity::render::RenderPresentationState& presentation,
            const cellularity::render::PreparedRenderFrame& frame,
            cellularity::render::SimulationWorldGpuRenderer& gpuRenderer,
            NativeRenderFacade& render, NativeGlApi& gl,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry = nullptr);
    void releaseGlResources(NativeGlApi& gl);
    void abandonGlResources() noexcept;

private:
#ifdef CELLULARITY_PERFORMANCE_HUD
    cellularity::microscope::MicroscopeDebugHud mDebugHud;
#endif
};
