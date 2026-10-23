#include "app/microscope/render/MicroscopeRenderer.h"

#include "app/NativeRenderFacade.h"
#include "app/RenderStateSnapshots.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/render/MicroscopeRenderFramePipeline.h"
#include "render/SimulationWorldGpuRenderer.h"
#include "render/NativeCellRendererResources.h"
#include "render/gl/NativeGlApi.h"

bool MicroscopeRenderer::draw(
        const NativeViewport& viewport,
        cellularity::microscope::MicroscopeRenderFramePipeline& frames,
        cellularity::render::SimulationWorldGpuRenderer& gpuRenderer,
        NativeRenderFacade& render, NativeGlApi& gl,
        const std::array<float, 3>& backgroundColor, bool uploadDynamicBuffers,
        const cellularity::microscope::MicroscopeDebugHudTelemetry*
                debugHudTelemetry) {
    if (!viewport.isReady() || viewport.viewWidth() == 0.0f || viewport.viewHeight() == 0.0f) {
        return false;
    }

    const std::array<float, 16> matrix =
            viewport.worldMatrix(cellularity::app::identityMatrix());

    const auto presentation =
            cellularity::app::snapshotPresentation(viewport, matrix, backgroundColor);
    return draw(presentation, frames, gpuRenderer, render, gl,
            uploadDynamicBuffers, debugHudTelemetry);
}

bool MicroscopeRenderer::draw(
        const cellularity::render::RenderPresentationState& presentation,
        cellularity::microscope::MicroscopeRenderFramePipeline& frames,
        cellularity::render::SimulationWorldGpuRenderer& gpuRenderer,
        NativeRenderFacade& render, NativeGlApi& gl, bool uploadDynamicBuffers,
        const cellularity::microscope::MicroscopeDebugHudTelemetry*
                debugHudTelemetry) {
    const auto& viewport = presentation.viewport;
    if (viewport.surfaceWidth <= 0 || viewport.surfaceHeight <= 0 || viewport.viewWidth == 0.0f || viewport.viewHeight == 0.0f) {
        return false;
    }

    const auto* frame = frames.presentLatestPreparedFrame(presentation);
    if (frame == nullptr) {
        // GLSurfaceView swaps after every callback. A missing first frame must
        // still produce a complete, defined presentation.
        gl.viewport(0, 0, viewport.surfaceWidth, viewport.surfaceHeight);
        gl.clearColor(0.31372549019f, 0.31372549019f, 0.31372549019f, 1.0f);
        gl.depthMask(true);
        gl.clear(gl.colorBufferBit() | gl.depthBufferBit());
        return false;
    }
    // Async completion is independent from the old capture-side upload flag.
    // The GPU consumer's frame identity makes this a no-op for repeated draws.
    (void)uploadDynamicBuffers;
    return drawPreparedFrame(frames.latestPresentation(), *frame, gpuRenderer,
            render, gl,
            debugHudTelemetry);
}

bool MicroscopeRenderer::drawPreparedFrame(
        const cellularity::render::RenderPresentationState& presentation,
        const cellularity::render::PreparedRenderFrame& frame,
        cellularity::render::SimulationWorldGpuRenderer& gpuRenderer,
        NativeRenderFacade& render, NativeGlApi& gl,
        const cellularity::microscope::MicroscopeDebugHudTelemetry*
                debugHudTelemetry) {
    const auto& viewport = presentation.viewport;
    if (viewport.surfaceWidth <= 0 || viewport.surfaceHeight <= 0 || viewport.viewWidth == 0.0f || viewport.viewHeight == 0.0f || !frame.valid || !frame.geometryReady) {
        return false;
    }

    // Screen presentation and persisted previews deliberately share this exact
    // clear, upload and production draw path. The caller chooses only the target
    // framebuffer and immutable presentation matrix.
    gl.viewport(0, 0, viewport.surfaceWidth, viewport.surfaceHeight);
    gl.clearColor(0.31372549019f, 0.31372549019f, 0.31372549019f, 1.0f);
    gl.depthMask(true);
    gl.clear(gl.colorBufferBit() | gl.depthBufferBit());

    render.uploadDynamicBuffers(gpuRenderer, gl, frame);
    render.draw(gpuRenderer, gl, frame, presentation);
#ifdef CELLULARITY_PERFORMANCE_HUD
    if (debugHudTelemetry != nullptr) {
        mDebugHud.draw(gl, render.requireResources().pixelTextShader(),
                viewport.surfaceWidth, viewport.surfaceHeight,
                *debugHudTelemetry);
    }
#else
    (void)debugHudTelemetry;
#endif
    return true;
}

void MicroscopeRenderer::releaseGlResources(NativeGlApi& gl) {
#ifdef CELLULARITY_PERFORMANCE_HUD
    mDebugHud.releaseGlResources(gl);
#else
    (void)gl;
#endif
}

void MicroscopeRenderer::abandonGlResources() noexcept {
#ifdef CELLULARITY_PERFORMANCE_HUD
    mDebugHud.abandonGlResources();
#endif
}
