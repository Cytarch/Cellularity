#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

class NativeGlApi;
class PixelTextShader;

namespace cellularity::render {

// Small screen-space renderer for the built-in 5x7 pixel font. Coordinates are
// expressed in physical surface pixels with a top-left origin. Glyphs and solid
// rectangles become compact instances, retaining layer order while drawing the
// complete overlay through one atlas-backed instanced call.
class PixelTextRenderer final {
public:
    using Color = std::array<float, 4>;
    using Matrix = std::array<float, 16>;
    using LayerIndex = std::size_t;

    static constexpr int kGlyphWidth = 5;
    static constexpr int kGlyphHeight = 7;
    static constexpr int kAtlasColumns = 8;
    static constexpr int kAtlasRows = 6;
    static constexpr int kAtlasWidth = kGlyphWidth * kAtlasColumns;
    static constexpr int kAtlasHeight = kGlyphHeight * kAtlasRows;
    static constexpr int kInstanceFloatCount = 9;
    using GlyphAtlas =
            std::array<std::uint8_t, kAtlasWidth * kAtlasHeight>;

    PixelTextRenderer() = default;
    PixelTextRenderer(const PixelTextRenderer&) = delete;
    PixelTextRenderer& operator=(const PixelTextRenderer&) = delete;
    PixelTextRenderer(PixelTextRenderer&&) = delete;
    PixelTextRenderer& operator=(PixelTextRenderer&&) = delete;

    void beginGeometry(int surfaceWidth, int surfaceHeight,
            std::size_t layerCount);
    void configureLayer(LayerIndex layer, const Color& color,
            std::size_t expectedPrimitiveCount = 0);
    void appendText(LayerIndex layer, std::string_view text, float originX,
            float originY, float pixelSize);
    void appendRectangle(LayerIndex layer, float left, float top, float width,
            float height);
    void commitGeometry();

    [[nodiscard]] static const GlyphAtlas& glyphAtlas() noexcept;
    [[nodiscard]] const std::vector<float>& packedInstances() const noexcept;
    [[nodiscard]] int instanceCount() const noexcept;
    [[nodiscard]] std::uint64_t geometryRevision() const noexcept;
    [[nodiscard]] bool hasCommittedGeometry() const noexcept;

    void draw(NativeGlApi& gl, PixelTextShader& shader,
            const Matrix& mvpMatrix);
    void releaseGlResources(NativeGlApi& gl);
    void abandonGlResources() noexcept;

private:
    struct Primitive final {
        float left = 0.0f;
        float top = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float glyphSlot = 0.0f;
    };

    struct Layer final {
        Color color{};
        std::vector<Primitive> primitives;
    };

    [[nodiscard]] Layer& buildingLayer(LayerIndex layer);
    void appendPrimitive(LayerIndex layer, float left, float top, float width,
            float height, float glyphSlot);
    void ensureGlyphAtlas(NativeGlApi& gl);
    void ensureVertexArray(NativeGlApi& gl, PixelTextShader& shader);
    void uploadGeometry(NativeGlApi& gl);

    int mSurfaceWidth = 0;
    int mSurfaceHeight = 0;
    std::vector<Layer> mLayers;
    std::vector<float> mPackedInstances;
    int mInstanceBuffer = 0;
    int mVertexArray = 0;
    int mGlyphAtlasTexture = 0;
    int mInstanceCount = 0;
    std::uint64_t mGeometryRevision = 0;
    bool mBuildingGeometry = false;
    bool mHasCommittedGeometry = false;
    bool mGeometryUploadDirty = false;
};

} // namespace cellularity::render
