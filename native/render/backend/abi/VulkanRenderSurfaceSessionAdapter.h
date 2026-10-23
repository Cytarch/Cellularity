#pragma once

#include <array>
#include <memory>
#include <utility>

#include "render/backend/RenderSurfaceSession.h"

namespace cellularity::render::abi {

class VulkanBackendSession;

// Main-library C++ adapter. Its vtable never crosses the DSO boundary; every
// backend call below is dispatched through the validated C API table owned by
// VulkanBackendSession.
class VulkanRenderSurfaceSessionAdapter final : public RenderSurfaceSession {
public:
    explicit VulkanRenderSurfaceSessionAdapter(
            std::shared_ptr<VulkanBackendSession> session);
    ~VulkanRenderSurfaceSessionAdapter() override = default;

    [[nodiscard]] RenderBackendKind kind() const noexcept override;
    [[nodiscard]] const void* bindingIdentity() const noexcept override;
    [[nodiscard]] bool hasResources() const noexcept override;
    [[nodiscard]] bool hasGpuBuffers() const noexcept override;

    void attachSurface(bool previewMode, const NativeRenderAssets& assets,
            NativeRenderTextureLoader& textureLoader) override;
    void resizeSurface(int width, int height) override;
    void detachSurface() override;
    void abandonSurface() noexcept override;

    [[nodiscard]] RenderFrameResult presentMicroscope(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry = nullptr) override;
    [[nodiscard]] RenderPassStatus drawMicroscopeOffscreenPass(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame) override;
    [[nodiscard]] RenderFrameResult clearSurface(
            const RenderViewportState& viewport,
            const std::array<float, 4>& color) override;
    [[nodiscard]] RenderFrameResult drawEnvironmentFieldPreview(
            const EnvironmentFieldPreviewCommand& command) override;
    void beginGenomePreviewFrame(int width, int height,
            bool fullyTransparentClear) override;
    void drawGenomePreviewGrid(
            const GenomePreviewGridCommand& command) override;
    void drawGenomePreviewTree(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const std::array<float, 16>& matrix,
            const std::vector<float>& lineVertices,
            const GenomePreviewDrawOptions& options) override;
    void drawGenomePreview(const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const GenomePreviewDrawOptions& options) override;
    [[nodiscard]] RenderFrameResult endGenomePreviewFrame() override;
    [[nodiscard]] RenderPassStatus endGenomePreviewOffscreenPass() override;
    void cancelGenomePreviewFrame() noexcept override;
    void beginOffscreenRgba(OffscreenRenderTarget target,
            int width, int height) override;
    [[nodiscard]] std::vector<std::uint8_t> finishOffscreenRgba(
            OffscreenRenderTarget target) override;
    void cancelOffscreenRgba(OffscreenRenderTarget target) noexcept override;

private:
    std::shared_ptr<VulkanBackendSession> mSession;
    std::array<std::pair<int, int>, 2> mOffscreenDimensions{};
};

} // namespace cellularity::render::abi
