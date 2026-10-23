#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/CellType.h"
#include "render/frame/PreparedRenderFrame.h"

class BackgroundShader;
class CellShader;
class DeadSplashShader;
class FoodShader;
class LinkShader;
class MarkerShader;
class NativeGlApi;

namespace cellularity::render {

// GL-thread-owned consumer for immutable prepared frames. This object owns
// only GLES resources and upload identity; it never owns or reads simulation
// state. Creation, deletion, upload and draw calls belong to the thread that
// owns the active GL context. Abandon only forgets stale handles and is safe
// after that context has already been lost.
class SimulationWorldGpuRenderer final {
public:
    static constexpr int PREVIEW_EXTRA_CELL_TYPE_INDEX =
            kPreviewExtraCellTypeIndex;

    SimulationWorldGpuRenderer() = default;
    ~SimulationWorldGpuRenderer() = default;

    SimulationWorldGpuRenderer(const SimulationWorldGpuRenderer&) = delete;
    SimulationWorldGpuRenderer& operator=(const SimulationWorldGpuRenderer&) = delete;
    SimulationWorldGpuRenderer(SimulationWorldGpuRenderer&&) = delete;
    SimulationWorldGpuRenderer& operator=(SimulationWorldGpuRenderer&&) = delete;

    [[nodiscard]] bool hasGlBuffers() const noexcept;
    void createGlBuffers(NativeGlApi& gl);
    void deleteGlBuffers(NativeGlApi& gl);
    void abandonGlBuffers() noexcept;

    // Returns true only when active CPU ranges were uploaded. An unchanged
    // frame identity can be presented repeatedly without rewriting its VBOs.
    bool uploadDynamicBuffers(NativeGlApi& gl, const PreparedRenderFrame& frame);

    void
    drawCells(NativeGlApi& gl,
            const std::array<CellShader*, CellTypes::length>& basicCellShaders,
            CellShader* arrowShader, int dotTextureHandle, bool isPreviewMode,
            const PreparedRenderFrame& frame);
    void drawCellOutlines(
            const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
            bool isPreviewMode, const PreparedRenderFrame& frame);
    void drawLinks(LinkShader& linkShader, const PreparedRenderFrame& frame);
    void drawFoods(FoodShader& foodShader, const PreparedRenderFrame& frame);
    void drawDeadCells(DeadSplashShader& deadSplashShader,
            int besselTextureHandle, const PreparedRenderFrame& frame);
    void
    draw(NativeGlApi& gl,
            const std::array<CellShader*, CellTypes::length>& basicCellShaders,
            const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
            CellShader* arrowShader, BackgroundShader& backgroundShader,
            FoodShader& foodShader, DeadSplashShader& deadSplashShader,
            LinkShader& linkShader, MarkerShader* markerShader, int dotTextureHandle,
            int besselTextureHandle, const PreparedRenderFrame& frame,
            const RenderPresentationState& presentation);
    void drawPreview(
            NativeGlApi& gl,
            const std::array<CellShader*, CellTypes::length>& basicCellShaders,
            const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
            CellShader* arrowShader, DeadSplashShader& deadSplashShader,
            LinkShader& linkShader, MarkerShader* markerShader, int dotTextureHandle,
            int besselTextureHandle, const PreparedRenderFrame& frame,
            bool drawLinksEnabled = true);
    void drawPreview(
            NativeGlApi& gl,
            const std::array<CellShader*, CellTypes::length>& basicCellShaders,
            const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
            CellShader* arrowShader, DeadSplashShader& deadSplashShader,
            LinkShader& linkShader, MarkerShader* markerShader, int dotTextureHandle,
            int besselTextureHandle, const PreparedRenderFrame& frame,
            const RenderPresentationState& presentation,
            bool drawLinksEnabled = true);

private:
    static constexpr int DRAW_BUNCH = 3 * (32767 / 3);
    static constexpr int FLOAT_BYTES = 4;
    static constexpr int SHORT_BYTES = 2;

    struct UploadedFrameIdentity {
        std::uintptr_t frameAddress = 0;
        std::uint32_t layoutVersion = 0;
        std::uint64_t worldEpoch = 0;
        std::uint64_t modeGeneration = 0;
        std::uint64_t captureSequence = 0;
        std::uint64_t geometryRevision = 0;
        std::uint64_t instanceRevision = 0;
    };

    int mCellCoordsVbo = 0;
    int mCellCentersVbo = 0;
    int mCellTexCoordsVbo = 0;
    int mCellColorsVbo = 0;
    int mCellIndicesIbo = 0;
    int mFoodLocalVbo = 0;
    int mFoodWorldVbo = 0;
    int mFoodAttributesVbo = 0;
    int mDeadLocalVbo = 0;
    int mDeadWorldVbo = 0;
    int mDeadAttributesVbo = 0;
    int mLinkCoordsVbo = 0;
    int mLinkTexCoordsVbo = 0;
    bool mUploadedFrameValid = false;
    UploadedFrameIdentity mUploadedFrame{};

    void invalidateUploadedFrame() noexcept;
    [[nodiscard]] bool
    isFrameUploaded(const PreparedRenderFrame& frame) const noexcept;
    void rememberUploadedFrame(const PreparedRenderFrame& frame) noexcept;
    void uploadStaticQuad(NativeGlApi& gl, int bufferId);
    void uploadFloatRange(NativeGlApi& gl, int bufferId,
            const std::vector<float>& source, int floatCount);
    void uploadShortRange(NativeGlApi& gl, int bufferId,
            const std::vector<unsigned short>& source,
            int shortCount);
    void unbindBuffers(NativeGlApi& gl);
    void drawMarkers(NativeGlApi& gl, MarkerShader* markerShader,
            const std::vector<RenderMarker>& markers,
            const std::array<float, 16>& mvpMatrix);
    void drawCellsWithMatrix(
            NativeGlApi& gl,
            const std::array<CellShader*, CellTypes::length>& basicCellShaders,
            CellShader* arrowShader, int dotTextureHandle, bool isPreviewMode,
            const PreparedRenderFrame& frame, const std::array<float, 16>& mvpMatrix);
    void drawCellOutlinesWithMatrix(
            const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
            bool isPreviewMode, const PreparedRenderFrame& frame,
            const std::array<float, 16>& mvpMatrix);
    void drawLinksWithMatrix(LinkShader& linkShader,
            const PreparedRenderFrame& frame,
            const std::array<float, 16>& mvpMatrix);
    void drawFoodsWithMatrix(FoodShader& foodShader,
            const PreparedRenderFrame& frame,
            const std::array<float, 16>& mvpMatrix);
    void drawDeadCellsWithMatrix(DeadSplashShader& deadSplashShader,
            int besselTextureHandle,
            const PreparedRenderFrame& frame,
            const std::array<float, 16>& mvpMatrix);
};

} // namespace cellularity::render
