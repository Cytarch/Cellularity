#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "render/backend/RenderSurfaceSession.h"
#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderFrameExchange.h"
#include "render/frame/RenderFrameTypes.h"

class SimulationWorld;
class NativeRenderAssets;
class NativeRenderTextureLoader;
class NativeViewport;

namespace cellularity::app {
struct EnvironmentFieldPreviewState;
} // namespace cellularity::app

namespace cellularity::microscope {
class MicroscopeRenderFramePipeline;
struct MicroscopeDebugHudTelemetry;
struct MicroscopeRenderFrameExchangeTelemetry;
} // namespace cellularity::microscope

// Owns backend-neutral frame preparation and delegates surface lifecycle and
// immutable prepared-frame consumption through RenderSurfaceSession.
// CPU frame storage is created lazily so a dedicated Genome Preview controller
// does not also allocate an unused microscope exchange. Production source
// capture is submitted while a simulation-owner lease is active;
// source-to-prepared work belongs to the pipeline worker and GPU consumption
// stays behind the selected backend on its surface owner.
class NativeRenderSession {
public:
    static constexpr int SUBSTRATE_PREVIEW_PIXELS = 512;

    explicit NativeRenderSession(int maxCellCount,
            std::unique_ptr<cellularity::render::RenderSurfaceSession>
                    surfaceSession = nullptr);
    ~NativeRenderSession();

    [[nodiscard]] bool hasSurfaceSession() const noexcept;
    void installSurfaceSession(
            std::unique_ptr<cellularity::render::RenderSurfaceSession>
                    surfaceSession);
    [[nodiscard]] bool uninstallSurfaceSession(
            cellularity::render::RenderBackendKind backendKind,
            const void* bindingIdentity) noexcept;
    [[nodiscard]] cellularity::render::RenderBackendKind backendKind() const;
    cellularity::render::RenderSurfaceSession& surfaceSession();
    const cellularity::render::RenderSurfaceSession& surfaceSession() const;

    bool hasResources() const;
    bool hasBackendGpuBuffers() const;
    void initializeMicroscopePipeline();
    void loadResources(const SimulationWorld& world, const NativeRenderAssets& assets,
            NativeRenderTextureLoader& textureLoader);
    void loadResources(bool previewMode, const NativeRenderAssets& assets,
            NativeRenderTextureLoader& textureLoader);
    void resizeSurface(int width, int height);
    void releaseResources();
    void abandonResources();
    void invalidateWorldEpoch(std::uint64_t worldEpoch);
    void invalidateModeGeneration(std::uint64_t modeGeneration);
    [[nodiscard]] bool consumeUrgentMicroscopeCaptureRequest() noexcept;
    [[nodiscard]] std::array<std::uint64_t, 4>
    microscopePreparationFailureState() const noexcept;
    [[nodiscard]] cellularity::microscope::MicroscopeRenderFrameExchangeTelemetry
    microscopeFrameExchangeTelemetry() const noexcept;

    int captureMicroscopeFrame(
            const SimulationWorld& world, const NativeViewport& viewport, float margin,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata);
    int captureMicroscopeFrame(
            const SimulationWorld& world,
            const cellularity::render::RenderViewportState& viewport, float margin,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata);
    [[nodiscard]] std::uint64_t prepareSubstratePreviewFrame(
            const SimulationWorld& world,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata);
    [[nodiscard]] std::uint64_t tryPrepareSubstratePreviewFrame(
            const SimulationWorld& world,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata) noexcept;
    [[nodiscard]] std::vector<std::uint8_t> captureSubstratePreviewRgba(
            std::uint64_t token,
            const std::array<float, 3>& backgroundColor);
    void discardSubstratePreviewFrame(std::uint64_t token) noexcept;
    [[nodiscard]] std::optional<
            cellularity::render::EnvironmentFieldPreviewCommand>
    prepareEnvironmentFieldPreview(
            const cellularity::app::EnvironmentFieldPreviewState& state,
            const NativeViewport& viewport) const;
    [[nodiscard]] cellularity::render::RenderFrameResult
    drawEnvironmentFieldPreview(
            const cellularity::render::EnvironmentFieldPreviewCommand& command);
    [[nodiscard]] cellularity::render::RenderFrameResult drawMicroscope(
            const NativeViewport& viewport,
            const std::array<float, 3>& backgroundColor);
    [[nodiscard]] cellularity::render::RenderFrameResult drawMicroscope(
            const cellularity::render::RenderPresentationState& presentation,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry = nullptr);
    [[nodiscard]] cellularity::render::RenderFrameResult
    clearMicroscopeNoRender(const NativeViewport& viewport);
    [[nodiscard]] cellularity::render::RenderFrameResult
    clearMicroscopeNoRender(
            const cellularity::render::RenderViewportState& viewport);

    [[nodiscard]] int visibleCellCount() const noexcept;
    [[nodiscard]] const cellularity::render::PreparedRenderFrame*
    preparedFrame() const;

private:
    struct SubstratePreviewState;

    cellularity::microscope::MicroscopeRenderFramePipeline& framePipeline();
    int captureViewport(
            const SimulationWorld& world, const NativeViewport& viewport, float margin,
            cellularity::render::RenderFrameCaptureMetadata captureMetadata);

    int mMaxCellCount;
    std::unique_ptr<cellularity::microscope::MicroscopeRenderFramePipeline>
            mFramePipeline;
    std::unique_ptr<cellularity::render::RenderSurfaceSession> mSurfaceSession;
    std::unique_ptr<SubstratePreviewState> mSubstratePreview;
};
