#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
    SubstratePreview = 1,
    // A frame of the live microscope scene that the caller is about to draw
    // itself, for a host whose presentation layer owns the picture instead of a
    // window's swapchain. It is the same scene the Substrate Preview target
    // draws -- same prepared frame, same record path -- but the pixels belong to
    // a running session rather than to a saved document, so the target is
    // separate: a preview capture and a live frame can be asked for at
    // different moments and must never share one another's storage.
    LiveFrame = 2
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
            const PreparedRenderFrame& frame,
            const cellularity::microscope::MicroscopeDebugHudTelemetry*
                    debugHudTelemetry = nullptr) = 0;
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

    // The same transaction as finishOffscreenRgba, but the image is written
    // straight into caller-owned storage with the caller's own row stride.
    //
    // This exists for a host that draws every frame itself: it owns one buffer
    // for the life of the session, and the pixels must land in it once, without
    // an intermediate image that is allocated, zero-filled and copied per frame.
    // The destination must hold at least height * rowStride bytes, rowStride
    // must be at least width * 4, and width/height must be the extent the target
    // was begun with. Returns the number of bytes written, or 0 when nothing was
    // written.
    //
    // The default implementation is the honest fallback for backends with no
    // direct route -- it takes the finished image and copies it row by row --
    // and it is what the test doubles use. Vulkan overrides it and copies from
    // its mapped readback buffer, so no second image is ever built.
    [[nodiscard]] virtual std::size_t finishOffscreenRgbaInto(
            OffscreenRenderTarget target, int width, int height,
            std::uint8_t* destination, std::size_t destinationSize,
            std::size_t rowStride) {
        if (destination == nullptr || width <= 0 || height <= 0) {
            return 0;
        }
        const std::size_t packedRowBytes =
                static_cast<std::size_t>(width) * 4U;
        if (rowStride < packedRowBytes
                || static_cast<std::size_t>(height)
                        > destinationSize / rowStride) {
            return 0;
        }
        std::vector<std::uint8_t> packed = finishOffscreenRgba(target);
        if (packed.size() < packedRowBytes * static_cast<std::size_t>(height)) {
            return 0;
        }
        for (int row = 0; row < height; ++row) {
            std::memcpy(destination + static_cast<std::size_t>(row) * rowStride,
                    packed.data()
                            + static_cast<std::size_t>(row) * packedRowBytes,
                    packedRowBytes);
        }
        return packedRowBytes * static_cast<std::size_t>(height);
    }
};

} // namespace cellularity::render
