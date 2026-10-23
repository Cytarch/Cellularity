#include "render/backend/gles/GlesRenderSurfaceSession.h"

#include <optional>
#include <stdexcept>
#include <utility>

#include "app/NativeRenderFacade.h"
#include "app/microscope/render/MicroscopeRenderer.h"
#include "render/SimulationWorldGpuRenderer.h"
#include "render/NativeCellRendererResources.h"
#include "render/gl/NativeGlApi.h"
#include "render/gl/OffscreenRgbaRenderTarget.h"

namespace cellularity::render::gles {
namespace {

using PreviousState = OffscreenRgbaRenderTarget::PreviousState;

struct OffscreenState final {
    std::optional<PreviousState> previous;
};

} // namespace

class GlesRenderSurfaceSession::Impl final {
public:
    explicit Impl(NativeGlApi& api)
            : gl(api) {}

    NativeGlApi& gl;
    SimulationWorldGpuRenderer microscopeGpuRenderer;
    SimulationWorldGpuRenderer genomePreviewGpuRenderer;
    NativeRenderFacade render;
    MicroscopeRenderer microscopeRenderer;
    OffscreenRgbaRenderTarget genomePreviewTarget;
    OffscreenRgbaRenderTarget substratePreviewTarget;
    OffscreenState genomePreviewOffscreen;
    OffscreenState substratePreviewOffscreen;

    OffscreenRgbaRenderTarget& target(OffscreenRenderTarget targetKind) {
        return targetKind == OffscreenRenderTarget::GenomePreview
                ? genomePreviewTarget
                : substratePreviewTarget;
    }

    OffscreenState& offscreenState(OffscreenRenderTarget targetKind) {
        return targetKind == OffscreenRenderTarget::GenomePreview
                ? genomePreviewOffscreen
                : substratePreviewOffscreen;
    }

    void cancel(OffscreenRenderTarget targetKind) noexcept {
        OffscreenState& state = offscreenState(targetKind);
        if (state.previous.has_value()) {
            target(targetKind).restore(gl, *state.previous);
            state.previous.reset();
        }
    }

    void clearOffscreenState() noexcept {
        genomePreviewOffscreen.previous.reset();
        substratePreviewOffscreen.previous.reset();
    }

    void drawGenomePreviewContents(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const GenomePreviewDrawOptions& options) {
        NativeCellRendererResources& resources = render.requireResources();
        genomePreviewGpuRenderer.drawPreview(gl,
                resources.basicCellShaders(), resources.outlineCellShaders(),
                options.drawDirectionArrow ? resources.arrowShader() : nullptr,
                resources.deadSplashShader(), resources.linkShader(),
                options.drawMarkers ? &resources.markerShader() : nullptr,
                resources.dotTextureHandle(), resources.besselTextureHandle(),
                frame, presentation, options.drawLinks);
    }
};

GlesRenderSurfaceSession::GlesRenderSurfaceSession(NativeGlApi& gl)
        : mImpl(std::make_unique<Impl>(gl)) {}

GlesRenderSurfaceSession::~GlesRenderSurfaceSession() = default;

RenderBackendKind GlesRenderSurfaceSession::kind() const noexcept {
    return RenderBackendKind::OpenGlEs;
}

bool GlesRenderSurfaceSession::hasResources() const noexcept {
    return mImpl->render.hasResources();
}

bool GlesRenderSurfaceSession::hasGpuBuffers() const noexcept {
    return mImpl->microscopeGpuRenderer.hasGlBuffers()
            || mImpl->genomePreviewGpuRenderer.hasGlBuffers();
}

void GlesRenderSurfaceSession::attachSurface(bool previewMode,
        const NativeRenderAssets& assets,
        NativeRenderTextureLoader& textureLoader) {
    mImpl->cancel(OffscreenRenderTarget::GenomePreview);
    mImpl->cancel(OffscreenRenderTarget::SubstratePreview);
    mImpl->genomePreviewTarget.release(mImpl->gl);
    mImpl->microscopeRenderer.releaseGlResources(mImpl->gl);
    if (mImpl->genomePreviewGpuRenderer.hasGlBuffers()) {
        mImpl->genomePreviewGpuRenderer.deleteGlBuffers(mImpl->gl);
    }
    if (previewMode) {
        mImpl->genomePreviewGpuRenderer.createGlBuffers(mImpl->gl);
    }
    mImpl->render.loadResources(mImpl->microscopeGpuRenderer, previewMode,
            assets, mImpl->gl, textureLoader);
}

void GlesRenderSurfaceSession::resizeSurface(int width, int height) {
    if (width < 0 || height < 0) {
        throw std::invalid_argument("GLES surface dimensions cannot be negative");
    }
}

void GlesRenderSurfaceSession::detachSurface() {
    mImpl->cancel(OffscreenRenderTarget::GenomePreview);
    mImpl->cancel(OffscreenRenderTarget::SubstratePreview);
    mImpl->genomePreviewTarget.release(mImpl->gl);
    mImpl->substratePreviewTarget.release(mImpl->gl);
    mImpl->microscopeRenderer.releaseGlResources(mImpl->gl);
    if (mImpl->genomePreviewGpuRenderer.hasGlBuffers()) {
        mImpl->genomePreviewGpuRenderer.deleteGlBuffers(mImpl->gl);
    }
    mImpl->render.releaseResources(mImpl->microscopeGpuRenderer, mImpl->gl);
}

void GlesRenderSurfaceSession::abandonSurface() noexcept {
    mImpl->clearOffscreenState();
    mImpl->genomePreviewTarget.abandon();
    mImpl->substratePreviewTarget.abandon();
    mImpl->microscopeRenderer.abandonGlResources();
    mImpl->genomePreviewGpuRenderer.abandonGlBuffers();
    mImpl->render.abandonResources(mImpl->microscopeGpuRenderer);
}

RenderFrameResult GlesRenderSurfaceSession::presentMicroscope(
        const RenderPresentationState& presentation,
        const PreparedRenderFrame& frame,
        const cellularity::microscope::MicroscopeDebugHudTelemetry*
                debugHudTelemetry) {
    const bool completed = mImpl->microscopeRenderer.drawPreparedFrame(
            presentation, frame, mImpl->microscopeGpuRenderer,
            mImpl->render, mImpl->gl, debugHudTelemetry);
    return makeRenderFrameResult(
            completed ? RenderFrameOutcome::Presented
                      : RenderFrameOutcome::Failed,
            kPrimaryContentPresented);
}

RenderPassStatus GlesRenderSurfaceSession::drawMicroscopeOffscreenPass(
        const RenderPresentationState& presentation,
        const PreparedRenderFrame& frame) {
    return mImpl->microscopeRenderer.drawPreparedFrame(presentation, frame,
                   mImpl->microscopeGpuRenderer, mImpl->render, mImpl->gl,
                   nullptr)
            ? RenderPassStatus::Completed
            : RenderPassStatus::Failed;
}

RenderFrameResult GlesRenderSurfaceSession::clearSurface(
        const RenderViewportState& viewport,
        const std::array<float, 4>& color) {
    if (viewport.surfaceWidth <= 0 || viewport.surfaceHeight <= 0) {
        return makeRenderFrameResult(RenderFrameOutcome::SkippedNotReady);
    }
    mImpl->gl.viewport(0, 0, viewport.surfaceWidth, viewport.surfaceHeight);
    mImpl->gl.clearColor(color[0], color[1], color[2], color[3]);
    mImpl->gl.depthMask(true);
    mImpl->gl.clear(
            mImpl->gl.colorBufferBit() | mImpl->gl.depthBufferBit());
    return makeRenderFrameResult(
            RenderFrameOutcome::Presented, kPrimaryContentPresented);
}

RenderFrameResult GlesRenderSurfaceSession::drawEnvironmentFieldPreview(
        const EnvironmentFieldPreviewCommand& command) {
    const bool completed = mImpl->render.drawEnvironmentFieldPreview(
                                   command, mImpl->gl)
            != 0;
    return makeRenderFrameResult(
            completed ? RenderFrameOutcome::Presented
                      : RenderFrameOutcome::Failed,
            kPrimaryContentPresented);
}

void GlesRenderSurfaceSession::beginGenomePreviewFrame(int width, int height,
        bool fullyTransparentClear) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument(
                "Genome Preview surface dimensions must be positive");
    }
    mImpl->gl.viewport(0, 0, width, height);
    const float clearRgb = fullyTransparentClear ? 0.0f : 32.0f / 255.0f;
    mImpl->gl.clearColor(clearRgb, clearRgb, clearRgb, 0.0f);
    mImpl->gl.depthMask(true);
    mImpl->gl.clear(
            mImpl->gl.colorBufferBit() | mImpl->gl.depthBufferBit());
}

void GlesRenderSurfaceSession::drawGenomePreviewGrid(
        const GenomePreviewGridCommand& command) {
    mImpl->gl.disable(mImpl->gl.depthTest());
    mImpl->gl.depthMask(false);
    mImpl->gl.disable(mImpl->gl.blend());
    mImpl->render.requireResources().genomeGridShader().draw(command.matrix,
            command.minX, command.maxX, command.minY, command.maxY,
            command.worldRadius, command.gridSize);
    mImpl->gl.depthMask(true);
}

void GlesRenderSurfaceSession::drawGenomePreviewTree(
        const RenderPresentationState& presentation,
        const PreparedRenderFrame& frame,
        const std::array<float, 16>& matrix,
        const std::vector<float>& lineVertices,
        const GenomePreviewDrawOptions& options) {
    mImpl->genomePreviewGpuRenderer.uploadDynamicBuffers(mImpl->gl, frame);
    mImpl->gl.enable(mImpl->gl.blend());
    mImpl->gl.blendFunc(mImpl->gl.srcAlpha(), mImpl->gl.oneMinusSrcAlpha());
    mImpl->gl.disable(mImpl->gl.depthTest());
    mImpl->render.requireResources().solidColorShader().draw(
            matrix, lineVertices);
    mImpl->drawGenomePreviewContents(presentation, frame, options);
}

void GlesRenderSurfaceSession::drawGenomePreview(
        const RenderPresentationState& presentation,
        const PreparedRenderFrame& frame,
        const GenomePreviewDrawOptions& options) {
    mImpl->genomePreviewGpuRenderer.uploadDynamicBuffers(mImpl->gl, frame);
    mImpl->drawGenomePreviewContents(presentation, frame, options);
}

RenderFrameResult GlesRenderSurfaceSession::endGenomePreviewFrame() {
    // GLES executes Preview commands immediately in the caller-owned context.
    return makeRenderFrameResult(
            RenderFrameOutcome::Presented, kPrimaryContentPresented);
}

RenderPassStatus GlesRenderSurfaceSession::endGenomePreviewOffscreenPass() {
    // GLES executes the same commands immediately into the bound framebuffer.
    return RenderPassStatus::Completed;
}

void GlesRenderSurfaceSession::cancelGenomePreviewFrame() noexcept {
    // No deferred GLES Preview batch exists to discard.
}

void GlesRenderSurfaceSession::beginOffscreenRgba(
        OffscreenRenderTarget targetKind, int width, int height) {
    OffscreenState& state = mImpl->offscreenState(targetKind);
    if (state.previous.has_value()) {
        throw std::logic_error("Off-screen RGBA target is already active");
    }
    state.previous = mImpl->target(targetKind).bind(mImpl->gl, width, height);
}

std::vector<std::uint8_t> GlesRenderSurfaceSession::finishOffscreenRgba(
        OffscreenRenderTarget targetKind) {
    OffscreenState& state = mImpl->offscreenState(targetKind);
    if (!state.previous.has_value()) {
        throw std::logic_error("Off-screen RGBA target is not active");
    }
    try {
        std::vector<std::uint8_t> rgba =
                mImpl->target(targetKind).readTopLeftRgba(mImpl->gl);
        mImpl->target(targetKind).restore(mImpl->gl, *state.previous);
        state.previous.reset();
        return rgba;
    } catch (...) {
        mImpl->target(targetKind).restore(mImpl->gl, *state.previous);
        state.previous.reset();
        throw;
    }
}

void GlesRenderSurfaceSession::cancelOffscreenRgba(
        OffscreenRenderTarget targetKind) noexcept {
    mImpl->cancel(targetKind);
}

} // namespace cellularity::render::gles
