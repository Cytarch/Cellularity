#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "render/backend/RenderBackendKind.h"
#include "render/backend/RenderFrameResult.h"
#include "render/frame/RenderFrameTypes.h"

class NativeRenderAssets;
class NativeRenderTextureLoader;

namespace cellularity::microscope {
struct MicroscopeDebugHudTelemetry;
} // namespace cellularity::microscope

namespace cellularity::render {

struct PreparedRenderFrame;

enum class OffscreenRenderTarget : std::uint8_t {
    GenomePreview = 0,
    SubstratePreview = 1
};

struct GenomePreviewGridCommand final {
    std::array<float, 16> matrix{};
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    double worldRadius = 0.0;
    int gridSize = 0;
};

struct GenomePreviewDrawOptions final {
    bool drawDirectionArrow = false;
    bool drawMarkers = true;
    bool drawLinks = true;
};

struct EnvironmentFieldPreviewCommand final {
    std::array<float, 16> matrix{};
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    double dishRadius = 0.0;
    int fieldType = 0;
    double fieldBase = 0.0;
    double fieldStrength = 0.0;
    double fieldAngle = 0.0;
    double fieldOffset = 0.0;
    double fieldSoftness = 0.0;
    double displayMin = 0.0;
    double displayMax = 0.0;
    bool vectorPreview = false;
    int gravityFieldType = 0;
    double gravityFieldAngle = 0.0;
};

// High-level contract between backend-neutral render orchestration and one
// surface implementation. Backend command APIs, GPU handles, shaders and GPU
// resource objects must never cross this boundary. NativeRenderTextureLoader
// remains a temporary attach-time adapter until the decoded texture payload
// planned for V-09 replaces it.
class RenderSurfaceSession {
public:
    virtual ~RenderSurfaceSession() = default;

    [[nodiscard]] virtual RenderBackendKind kind() const noexcept = 0;

    // Optional process-local identity used to reject stale detach callbacks
    // when Android replaces one backend session with another. It is never
    // persisted or exposed as a graphics API handle.
    [[nodiscard]] virtual const void* bindingIdentity() const noexcept {
        return nullptr;
    }

    [[nodiscard]] virtual bool hasResources() const noexcept = 0;
    [[nodiscard]] virtual bool hasGpuBuffers() const noexcept = 0;

    virtual void attachSurface(bool previewMode,
            const NativeRenderAssets& assets,
            NativeRenderTextureLoader& textureLoader) = 0;
    virtual void resizeSurface(int width, int height) = 0;
    virtual void detachSurface() = 0;
    virtual void abandonSurface() noexcept = 0;

    [[nodiscard]] virtual RenderFrameResult presentMicroscope(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry = nullptr) = 0;
    [[nodiscard]] virtual RenderPassStatus drawMicroscopeOffscreenPass(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame) = 0;
    [[nodiscard]] virtual RenderFrameResult clearSurface(
            const RenderViewportState& viewport,
            const std::array<float, 4>& color) = 0;

    [[nodiscard]] virtual RenderFrameResult drawEnvironmentFieldPreview(
            const EnvironmentFieldPreviewCommand& command) = 0;
    virtual void beginGenomePreviewFrame(int width, int height,
            bool fullyTransparentClear) = 0;
    virtual void drawGenomePreviewGrid(
            const GenomePreviewGridCommand& command) = 0;
    virtual void drawGenomePreviewTree(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const std::array<float, 16>& matrix,
            const std::vector<float>& lineVertices,
            const GenomePreviewDrawOptions& options) = 0;
    virtual void drawGenomePreview(
            const RenderPresentationState& presentation,
            const PreparedRenderFrame& frame,
            const GenomePreviewDrawOptions& options) = 0;
    // Completes the one logical Preview frame started by
    // beginGenomePreviewFrame(). Backends such as Vulkan use this explicit
    // boundary to submit grid, ghost and live/tree layers in one present.
    [[nodiscard]] virtual RenderFrameResult endGenomePreviewFrame() = 0;
    [[nodiscard]] virtual RenderPassStatus
    endGenomePreviewOffscreenPass() = 0;
    virtual void cancelGenomePreviewFrame() noexcept = 0;

    // Starts an explicit save-preview transaction. Production draw calls made
    // before finish target backend-owned off-screen storage; ordinary frames
    // never perform a readback. finish returns tightly packed, top-left RGBA8.
    virtual void beginOffscreenRgba(OffscreenRenderTarget target,
            int width, int height) = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t>
    finishOffscreenRgba(OffscreenRenderTarget target) = 0;
    virtual void cancelOffscreenRgba(
            OffscreenRenderTarget target) noexcept = 0;
};

} // namespace cellularity::render
