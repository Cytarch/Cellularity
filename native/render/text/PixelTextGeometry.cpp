#include "render/text/PixelTextRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cellularity::render {
namespace {

using Glyph = std::array<std::uint8_t, PixelTextRenderer::kGlyphHeight>;

constexpr int kLastTextGlyphSlot = 42;
constexpr float kFallbackGlyphSlot = 0.0F;
constexpr float kSolidGlyphSlot = 43.0F;

unsigned char normalizedGlyphByte(unsigned char byte) {
    if (byte >= 'a' && byte <= 'z') {
        return static_cast<unsigned char>(byte - 'a' + 'A');
    }
    return byte;
}

float glyphSlot(unsigned char byte) {
    byte = normalizedGlyphByte(byte);
    if (byte >= '0' && byte <= '9') {
        return static_cast<float>(1 + byte - '0');
    }
    if (byte >= 'A' && byte <= 'Z') {
        return static_cast<float>(11 + byte - 'A');
    }
    switch (byte) {
        case '.':
            return 37.0F;
        case ':':
            return 38.0F;
        case '/':
            return 39.0F;
        case '%':
            return 40.0F;
        case '-':
            return 41.0F;
        case '+':
            return 42.0F;
        default:
            return kFallbackGlyphSlot;
    }
}

unsigned char glyphByteForAtlasSlot(int slot) {
    if (slot == 0) {
        return '?';
    }
    if (slot >= 1 && slot <= 10) {
        return static_cast<unsigned char>('0' + slot - 1);
    }
    if (slot >= 11 && slot <= 36) {
        return static_cast<unsigned char>('A' + slot - 11);
    }
    switch (slot) {
        case 37:
            return '.';
        case 38:
            return ':';
        case 39:
            return '/';
        case 40:
            return '%';
        case 41:
            return '-';
        case 42:
            return '+';
        default:
            return '?';
    }
}

Glyph pixelGlyph(unsigned char byte) {
    switch (normalizedGlyphByte(byte)) {
        case '0':
            return {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001,
                    0b01110};
        case '1':
            return {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100,
                    0b01110};
        case '2':
            return {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000,
                    0b11111};
        case '3':
            return {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001,
                    0b11110};
        case '4':
            return {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010,
                    0b00010};
        case '5':
            return {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001,
                    0b11110};
        case '6':
            return {0b01110, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001,
                    0b01110};
        case '7':
            return {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000,
                    0b01000};
        case '8':
            return {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001,
                    0b01110};
        case '9':
            return {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001,
                    0b01110};
        case 'A':
            return {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001,
                    0b10001};
        case 'B':
            return {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001,
                    0b11110};
        case 'C':
            return {0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000,
                    0b01111};
        case 'D':
            return {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001,
                    0b11110};
        case 'E':
            return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000,
                    0b11111};
        case 'F':
            return {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000,
                    0b10000};
        case 'G':
            return {0b01111, 0b10000, 0b10000, 0b10111, 0b10001, 0b10001,
                    0b01111};
        case 'H':
            return {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001,
                    0b10001};
        case 'I':
            return {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100,
                    0b11111};
        case 'J':
            return {0b00111, 0b00010, 0b00010, 0b00010, 0b10010, 0b10010,
                    0b01100};
        case 'K':
            return {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010,
                    0b10001};
        case 'L':
            return {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000,
                    0b11111};
        case 'M':
            return {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001,
                    0b10001};
        case 'N':
            return {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001,
                    0b10001};
        case 'O':
            return {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001,
                    0b01110};
        case 'P':
            return {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000,
                    0b10000};
        case 'Q':
            return {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010,
                    0b01101};
        case 'R':
            return {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010,
                    0b10001};
        case 'S':
            return {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001,
                    0b11110};
        case 'T':
            return {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100,
                    0b00100};
        case 'U':
            return {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001,
                    0b01110};
        case 'V':
            return {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010,
                    0b00100};
        case 'W':
            return {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101,
                    0b01010};
        case 'X':
            return {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001,
                    0b10001};
        case 'Y':
            return {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100,
                    0b00100};
        case 'Z':
            return {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000,
                    0b11111};
        case '.':
            return {0, 0, 0, 0, 0, 0b00110, 0b00110};
        case ':':
            return {0, 0b00110, 0b00110, 0, 0b00110, 0b00110, 0};
        case '/':
            return {0b00001, 0b00010, 0b00010, 0b00100, 0b01000, 0b01000,
                    0b10000};
        case '%':
            return {0b11001, 0b11010, 0b00100, 0b01000, 0b10110, 0b00110,
                    0};
        case '-':
            return {0, 0, 0, 0b11111, 0, 0, 0};
        case '+':
            return {0, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0};
        default:
            return {0b01110, 0b10001, 0b00010, 0b00100, 0b00100, 0,
                    0b00100};
    }
}

PixelTextRenderer::GlyphAtlas buildGlyphAtlas() {
    PixelTextRenderer::GlyphAtlas pixels{};
    for (int slot = 0; slot <= kLastTextGlyphSlot; ++slot) {
        const Glyph rows = pixelGlyph(glyphByteForAtlasSlot(slot));
        const int atlasLeft =
                (slot % PixelTextRenderer::kAtlasColumns)
                * PixelTextRenderer::kGlyphWidth;
        const int atlasTop =
                (slot / PixelTextRenderer::kAtlasColumns)
                * PixelTextRenderer::kGlyphHeight;
        for (int row = 0; row < PixelTextRenderer::kGlyphHeight; ++row) {
            for (int column = 0; column < PixelTextRenderer::kGlyphWidth;
                    ++column) {
                const bool isLit =
                        (rows[static_cast<std::size_t>(row)]
                                & (1U << (PixelTextRenderer::kGlyphWidth - 1
                                           - column)))
                        != 0;
                const std::size_t pixelIndex =
                        static_cast<std::size_t>(atlasTop + row)
                                * PixelTextRenderer::kAtlasWidth
                        + static_cast<std::size_t>(atlasLeft + column);
                pixels[pixelIndex] = isLit ? 255 : 0;
            }
        }
    }
    return pixels;
}

} // namespace

void PixelTextRenderer::beginGeometry(int surfaceWidth, int surfaceHeight,
        std::size_t layerCount) {
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        throw std::invalid_argument(
                "Pixel text geometry requires a positive surface size");
    }
    mSurfaceWidth = surfaceWidth;
    mSurfaceHeight = surfaceHeight;
    mLayers.resize(layerCount);
    for (Layer& layer : mLayers) {
        layer.primitives.clear();
    }
    mBuildingGeometry = true;
}

void PixelTextRenderer::configureLayer(LayerIndex layer, const Color& color,
        std::size_t expectedPrimitiveCount) {
    Layer& target = buildingLayer(layer);
    target.color = color;
    target.primitives.reserve(expectedPrimitiveCount);
}

void PixelTextRenderer::appendText(LayerIndex layer, std::string_view text,
        float originX, float originY, float pixelSize) {
    pixelSize = std::max(1.0F, std::round(pixelSize));
    originX = std::round(originX);
    originY = std::round(originY);
    for (std::size_t glyphIndex = 0; glyphIndex < text.size(); ++glyphIndex) {
        const unsigned char byte =
                static_cast<unsigned char>(text[glyphIndex]);
        if (byte == ' ') {
            continue;
        }
        const float glyphX = originX
                + static_cast<float>(glyphIndex) * pixelSize * 6.0F;
        appendPrimitive(layer, glyphX, originY, pixelSize * kGlyphWidth,
                pixelSize * kGlyphHeight, glyphSlot(byte));
    }
}

void PixelTextRenderer::appendRectangle(LayerIndex layer, float left,
        float top, float width, float height) {
    appendPrimitive(layer, left, top, width, height, kSolidGlyphSlot);
}

void PixelTextRenderer::appendPrimitive(LayerIndex layer, float left,
        float top, float width, float height, float glyphSlotValue) {
    buildingLayer(layer).primitives.push_back(
            {left, top, width, height, glyphSlotValue});
}

void PixelTextRenderer::commitGeometry() {
    if (!mBuildingGeometry) {
        throw std::logic_error(
                "Pixel text geometry must begin before it can be committed");
    }

    constexpr std::size_t kMaximumInstanceCount =
            static_cast<std::size_t>(std::numeric_limits<int>::max())
            / (kInstanceFloatCount * sizeof(float));
    std::size_t totalInstanceCount = 0;
    for (const Layer& layer : mLayers) {
        if (layer.primitives.size()
                > kMaximumInstanceCount - totalInstanceCount) {
            throw std::length_error(
                    "Pixel text geometry exceeds backend range");
        }
        totalInstanceCount += layer.primitives.size();
    }

    mPackedInstances.clear();
    mPackedInstances.reserve(totalInstanceCount * kInstanceFloatCount);
    const float inverseWidth = 1.0F / static_cast<float>(mSurfaceWidth);
    const float inverseHeight = 1.0F / static_cast<float>(mSurfaceHeight);
    for (const Layer& layer : mLayers) {
        for (const Primitive& primitive : layer.primitives) {
            mPackedInstances.push_back(
                    primitive.left * inverseWidth * 2.0F - 1.0F);
            mPackedInstances.push_back(
                    1.0F - primitive.top * inverseHeight * 2.0F);
            mPackedInstances.push_back(
                    primitive.width * inverseWidth * 2.0F);
            mPackedInstances.push_back(
                    -primitive.height * inverseHeight * 2.0F);
            mPackedInstances.push_back(primitive.glyphSlot);
            mPackedInstances.insert(mPackedInstances.end(),
                    layer.color.begin(), layer.color.end());
        }
    }

    mInstanceCount = static_cast<int>(totalInstanceCount);
    mGeometryRevision = mGeometryRevision
                    == std::numeric_limits<std::uint64_t>::max()
            ? 1
            : mGeometryRevision + 1;
    mBuildingGeometry = false;
    mHasCommittedGeometry = true;
    mGeometryUploadDirty = true;
}

const PixelTextRenderer::GlyphAtlas&
PixelTextRenderer::glyphAtlas() noexcept {
    static const GlyphAtlas atlas = buildGlyphAtlas();
    return atlas;
}

const std::vector<float>&
PixelTextRenderer::packedInstances() const noexcept {
    return mPackedInstances;
}

int PixelTextRenderer::instanceCount() const noexcept {
    return mInstanceCount;
}

std::uint64_t PixelTextRenderer::geometryRevision() const noexcept {
    return mGeometryRevision;
}

bool PixelTextRenderer::hasCommittedGeometry() const noexcept {
    return mHasCommittedGeometry;
}

PixelTextRenderer::Layer& PixelTextRenderer::buildingLayer(LayerIndex layer) {
    if (!mBuildingGeometry) {
        throw std::logic_error(
                "Pixel text geometry must begin before primitives are appended");
    }
    if (layer >= mLayers.size()) {
        throw std::out_of_range("Pixel text layer index is out of range");
    }
    return mLayers[layer];
}

} // namespace cellularity::render
