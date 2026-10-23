#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "render/backend/RenderSurfaceSession.h"

namespace cellularity::tests {

class FakeRenderSurfaceSession final
        : public render::RenderSurfaceSession {
public:
    render::RenderBackendKind backendKind = render::RenderBackendKind::OpenGlEs;
    bool resources = false;
    bool gpuBuffers = false;
    render::RenderFrameResult frameResult = render::makeRenderFrameResult(
            render::RenderFrameOutcome::Presented,
            render::kPrimaryContentPresented);
    render::RenderPassStatus offscreenPassResult =
            render::RenderPassStatus::Completed;
    bool throwOnGenomeDraw = false;
    int attachCount = 0;
    int resizeCount = 0;
    int detachCount = 0;
    int abandonCount = 0;
    int presentCount = 0;
    int clearCount = 0;
    int environmentPreviewCount = 0;
    int genomeFrameCount = 0;
    int genomeGridCount = 0;
    int genomeTreeLineCount = 0;
    int genomeDrawCount = 0;
    int genomeEndCount = 0;
    int genomeCancelCount = 0;
    int offscreenBeginCount = 0;
    int offscreenFinishCount = 0;
    int offscreenCancelCount = 0;
    int lastWidth = 0;
    int lastHeight = 0;
    bool lastPreviewMode = false;
    bool lastTransparentClear = false;
    render::OffscreenRenderTarget lastOffscreenTarget =
            render::OffscreenRenderTarget::GenomePreview;
    render::RenderPresentationState lastPresentation{};
    render::RenderViewportState lastClearViewport{};
    render::EnvironmentFieldPreviewCommand lastEnvironmentPreview{};
    std::array<float, 4> lastClearColor{};
    const render::PreparedRenderFrame* lastPresentedFrame = nullptr;
    std::vector<std::uint8_t> offscreenPixels{1, 2, 3, 4};
    const void* identity = nullptr;

    [[nodiscard]] render::RenderBackendKind kind() const noexcept override {
        return backendKind;
    }

    [[nodiscard]] const void* bindingIdentity() const noexcept override {
        return identity;
    }

    [[nodiscard]] bool hasResources() const noexcept override {
        return resources;
    }

    [[nodiscard]] bool hasGpuBuffers() const noexcept override {
        return gpuBuffers;
    }

    void attachSurface(bool previewMode, const NativeRenderAssets&,
            NativeRenderTextureLoader&) override {
        ++attachCount;
        lastPreviewMode = previewMode;
        resources = true;
        gpuBuffers = true;
    }

    void resizeSurface(int width, int height) override {
        ++resizeCount;
        lastWidth = width;
        lastHeight = height;
    }

    void detachSurface() override {
        ++detachCount;
        resources = false;
        gpuBuffers = false;
    }

    void abandonSurface() noexcept override {
        ++abandonCount;
        resources = false;
        gpuBuffers = false;
    }

    render::RenderFrameResult presentMicroscope(
            const render::RenderPresentationState& presentation,
            const render::PreparedRenderFrame& frame,
            const microscope::MicroscopeDebugHudTelemetry*) override {
        ++presentCount;
        lastPresentation = presentation;
        lastPresentedFrame = &frame;
        return frameResult;
    }

    render::RenderPassStatus drawMicroscopeOffscreenPass(
            const render::RenderPresentationState& presentation,
            const render::PreparedRenderFrame& frame) override {
        ++presentCount;
        lastPresentation = presentation;
        lastPresentedFrame = &frame;
        return offscreenPassResult;
    }

    render::RenderFrameResult clearSurface(
            const render::RenderViewportState& viewport,
            const std::array<float, 4>& color) override {
        ++clearCount;
        lastClearViewport = viewport;
        lastClearColor = color;
        return frameResult;
    }

    render::RenderFrameResult drawEnvironmentFieldPreview(
            const render::EnvironmentFieldPreviewCommand& command) override {
        ++environmentPreviewCount;
        lastEnvironmentPreview = command;
        return frameResult;
    }

    void beginGenomePreviewFrame(int width, int height,
            bool fullyTransparentClear) override {
        ++genomeFrameCount;
        lastWidth = width;
        lastHeight = height;
        lastTransparentClear = fullyTransparentClear;
    }

    void drawGenomePreviewGrid(
            const render::GenomePreviewGridCommand&) override {
        ++genomeGridCount;
    }

    void drawGenomePreviewTree(
            const render::RenderPresentationState& presentation,
            const render::PreparedRenderFrame& frame,
            const std::array<float, 16>&,
            const std::vector<float>&,
            const render::GenomePreviewDrawOptions&) override {
        ++genomeTreeLineCount;
        ++genomeDrawCount;
        lastPresentation = presentation;
        lastPresentedFrame = &frame;
    }

    void drawGenomePreview(const render::RenderPresentationState& presentation,
            const render::PreparedRenderFrame& frame,
            const render::GenomePreviewDrawOptions&) override {
        if (throwOnGenomeDraw) {
            throw std::runtime_error("Injected Genome Preview draw failure");
        }
        ++genomeDrawCount;
        lastPresentation = presentation;
        lastPresentedFrame = &frame;
    }

    render::RenderFrameResult endGenomePreviewFrame() override {
        ++genomeEndCount;
        return frameResult;
    }

    render::RenderPassStatus endGenomePreviewOffscreenPass() override {
        ++genomeEndCount;
        return offscreenPassResult;
    }

    void cancelGenomePreviewFrame() noexcept override {
        ++genomeCancelCount;
    }

    void beginOffscreenRgba(render::OffscreenRenderTarget target,
            int width, int height) override {
        ++offscreenBeginCount;
        lastOffscreenTarget = target;
        lastWidth = width;
        lastHeight = height;
    }

    [[nodiscard]] std::vector<std::uint8_t>
    finishOffscreenRgba(render::OffscreenRenderTarget target) override {
        ++offscreenFinishCount;
        lastOffscreenTarget = target;
        return offscreenPixels;
    }

    void cancelOffscreenRgba(
            render::OffscreenRenderTarget target) noexcept override {
        ++offscreenCancelCount;
        lastOffscreenTarget = target;
    }
};

} // namespace cellularity::tests
