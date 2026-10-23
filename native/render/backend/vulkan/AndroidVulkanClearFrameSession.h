#pragma once

#include <android/native_window.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/microscope/render/MicroscopeDebugHud.h"
#include "render/backend/RenderSurfaceSession.h"
#include "render/backend/vulkan/VulkanBasicCellRenderer.h"
#include "render/frame/PreparedRenderFrameView.h"

namespace cellularity::render::vulkan {

using ClearFrameResult = RenderFrameOutcome;

/**
 * Thread-confined Vulkan 1.0 microscope-world test session.
 *
 * attachWindow always consumes one ANativeWindow reference, including on
 * failure. Instance/device resources survive detachSurface. Completed
 * off-screen capture targets also survive; Android surface, swapchain and
 * swapchain-depth resources do not.
 */
class AndroidVulkanClearFrameSession final {
public:
    AndroidVulkanClearFrameSession(
            std::vector<std::uint32_t> backgroundVertexSpirv,
            std::vector<std::uint32_t> backgroundFragmentSpirv,
            std::vector<std::uint32_t> densityFieldVertexSpirv,
            std::vector<std::uint32_t> densityFieldFragmentSpirv,
            std::array<VulkanCellShaderSpirv,
                    kVulkanBasicCellProgramCount>
                    cellShaderSpirv,
            std::array<VulkanCellShaderSpirv, kVulkanWorldProgramCount>
                    worldShaderSpirv,
            NativeRenderTexturePayload dotTexture,
            NativeRenderTexturePayload besselTexture);
    ~AndroidVulkanClearFrameSession();

    AndroidVulkanClearFrameSession(
            const AndroidVulkanClearFrameSession&) = delete;
    AndroidVulkanClearFrameSession& operator=(
            const AndroidVulkanClearFrameSession&) = delete;

    [[nodiscard]] bool attachWindow(
            ANativeWindow* window, int width, int height);
    void resizeSurface(int width, int height);
    void detachSurface();
    void setPerformanceProfilingEnabled(bool enabled);
    [[nodiscard]] RenderFrameResult renderFrame();
    [[nodiscard]] RenderFrameResult renderFrame(bool performanceHudEnabled);
    [[nodiscard]] RenderFrameResult renderFrame(
            const PreparedRenderFrameView& frame,
            const RenderPresentationState& presentation);
    [[nodiscard]] RenderFrameResult renderFrame(
            const PreparedRenderFrameView& frame,
            const RenderPresentationState& presentation,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry);
    [[nodiscard]] RenderPassStatus drawMicroscopeOffscreenPass(
            const PreparedRenderFrameView& frame,
            const RenderPresentationState& presentation);
    [[nodiscard]] RenderFrameResult renderClear(
            const RenderViewportState& viewport,
            const std::array<float, 4>& color);
    [[nodiscard]] RenderFrameResult renderEnvironmentFieldPreview(
            const EnvironmentFieldPreviewCommand& command);
    void beginOffscreenRgba(
            OffscreenRenderTarget target, int width, int height);
    [[nodiscard]] std::vector<std::uint8_t> finishOffscreenRgba(
            OffscreenRenderTarget target);
    void cancelOffscreenRgba(OffscreenRenderTarget target) noexcept;
    void beginGenomePreviewFrame(
            int width, int height, bool fullyTransparentClear);
    void drawGenomePreviewGrid(const GenomePreviewGridCommand& command);
    void drawGenomePreviewTree(
            const RenderPresentationState& presentation,
            const PreparedRenderFrameView& frame,
            const std::array<float, 16>& matrix,
            PreparedFrameSpan<float> lineVertices,
            const GenomePreviewDrawOptions& options);
    void drawGenomePreview(const RenderPresentationState& presentation,
            const PreparedRenderFrameView& frame,
            const GenomePreviewDrawOptions& options);
    [[nodiscard]] RenderFrameResult endGenomePreviewFrame();
    [[nodiscard]] RenderPassStatus endGenomePreviewOffscreenPass();
    void cancelGenomePreviewFrame() noexcept;
    [[nodiscard]] bool hasAttachedSurface() const noexcept;

    [[nodiscard]] std::string diagnostics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace cellularity::render::vulkan
