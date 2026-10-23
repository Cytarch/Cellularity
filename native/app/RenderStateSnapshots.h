#pragma once

#include <array>

#include "app/interaction/NativeViewport.h"
#include "render/frame/RenderFrameTypes.h"

namespace cellularity::app {

[[nodiscard]] inline render::RenderViewBounds toRenderBounds(
        const NativeViewport::ViewBounds& bounds) noexcept {
    return {bounds.minX, bounds.maxX, bounds.minY, bounds.maxY};
}

[[nodiscard]] inline std::array<float, 16> identityMatrix() noexcept {
    return {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
    };
}

[[nodiscard]] inline render::RenderViewportState snapshotViewport(
        const NativeViewport& viewport,
        const std::array<float, 16>& mvpMatrix,
        const render::RenderViewBounds& coverageBounds) {
    render::RenderViewportState state;
    state.revision = viewport.revision();
    state.coverageRevision = viewport.revision();
    state.visibleBounds = toRenderBounds(viewport.viewBounds(0.0f));
    state.coverageBounds = coverageBounds;
    state.mvpMatrix = mvpMatrix;
    state.cameraX = viewport.cameraX();
    state.cameraY = viewport.cameraY();
    state.zoom = viewport.zoom();
    state.viewWidth = viewport.viewWidth();
    state.viewHeight = viewport.viewHeight();
    state.surfaceWidth = viewport.surfaceWidth();
    state.surfaceHeight = viewport.surfaceHeight();
    return state;
}

[[nodiscard]] inline render::RenderViewportState snapshotViewport(
        const NativeViewport& viewport,
        const render::RenderViewBounds& coverageBounds) {
    const std::array<float, 16> identity = identityMatrix();
    const bool hasFiniteMatrixDimensions = viewport.isReady()
            && viewport.viewWidth() != 0.0f
            && viewport.viewHeight() != 0.0f;
    return snapshotViewport(
            viewport,
            hasFiniteMatrixDimensions
                    ? viewport.worldMatrix(identity)
                    : identity,
            coverageBounds);
}

[[nodiscard]] inline render::RenderPresentationState snapshotPresentation(
        const NativeViewport& viewport,
        const std::array<float, 16>& mvpMatrix,
        const std::array<float, 3>& backgroundColor) {
    render::RenderPresentationState state;
    const render::RenderViewBounds visibleBounds =
            toRenderBounds(viewport.viewBounds(0.0f));
    state.viewport = snapshotViewport(viewport, mvpMatrix, visibleBounds);
    state.backgroundColor = backgroundColor;
    return state;
}

} // namespace cellularity::app
