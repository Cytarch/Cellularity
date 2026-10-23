#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

enum class NativeRenderTexturePixelFormat : std::uint8_t {
    Rgba8 = 0,
    R8 = 1,
};

enum class NativeRenderTextureFilter : std::uint8_t {
    Nearest = 0,
    Linear = 1,
};

enum class NativeRenderTextureWrap : std::uint8_t {
    Repeat = 0,
    ClampToEdge = 1,
};

struct NativeRenderTexturePayload final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    NativeRenderTexturePixelFormat pixelFormat =
            NativeRenderTexturePixelFormat::Rgba8;
    NativeRenderTextureFilter minFilter =
            NativeRenderTextureFilter::Nearest;
    NativeRenderTextureFilter magFilter =
            NativeRenderTextureFilter::Linear;
    NativeRenderTextureWrap wrapS = NativeRenderTextureWrap::Repeat;
    NativeRenderTextureWrap wrapT = NativeRenderTextureWrap::Repeat;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] std::size_t bytesPerPixel() const noexcept {
        return pixelFormat == NativeRenderTexturePixelFormat::R8 ? 1U : 4U;
    }

    [[nodiscard]] bool valid() const noexcept {
        if (width == 0 || height == 0
                || (pixelFormat != NativeRenderTexturePixelFormat::Rgba8
                        && pixelFormat
                                != NativeRenderTexturePixelFormat::R8)
                || (minFilter != NativeRenderTextureFilter::Nearest
                        && minFilter != NativeRenderTextureFilter::Linear)
                || (magFilter != NativeRenderTextureFilter::Nearest
                        && magFilter != NativeRenderTextureFilter::Linear)
                || (wrapS != NativeRenderTextureWrap::Repeat
                        && wrapS != NativeRenderTextureWrap::ClampToEdge)
                || (wrapT != NativeRenderTextureWrap::Repeat
                        && wrapT != NativeRenderTextureWrap::ClampToEdge)) {
            return false;
        }
        const std::uint64_t pixelCount = static_cast<std::uint64_t>(width)
                * static_cast<std::uint64_t>(height);
        const std::size_t componentCount = bytesPerPixel();
        if (pixelCount
                > std::numeric_limits<std::uint64_t>::max()
                        / componentCount) {
            return false;
        }
        const std::uint64_t expected = pixelCount * componentCount;
        return expected == pixels.size();
    }
};

class NativeRenderTextureLoader {
public:
    virtual ~NativeRenderTextureLoader() = default;
    virtual NativeRenderTexturePayload loadTexture(
            const std::string& name, bool clampToEdge) = 0;
};
