#include "render/AssetRenderTextureLoader.h"

#include <climits>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "render/NativeRenderAssets.h"

#define STBI_MAX_DIMENSIONS 4096
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

constexpr int kRgbaChannelCount = 4;

bool validTextureName(const std::string& name) noexcept {
    if (name.empty() || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    for (const char character : name) {
        const bool lowercaseLetter = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!lowercaseLetter && !digit && character != '_') {
            return false;
        }
    }
    return true;
}

std::string decodeFailure(const std::string& path) {
    const char* reason = stbi_failure_reason();
    return "Could not decode renderer texture " + path
            + (reason != nullptr ? ": " + std::string(reason) : std::string());
}

} // namespace

AssetRenderTextureLoader::AssetRenderTextureLoader(
        const NativeRenderAssets& assets)
        : mAssets(assets) {}

NativeRenderTexturePayload AssetRenderTextureLoader::loadTexture(
        const std::string& name, bool clampToEdge) {
    if (!validTextureName(name)) {
        throw std::invalid_argument("Invalid renderer texture name: " + name);
    }

    const std::string path = "textures/" + name + ".png";
    const std::vector<unsigned char> encoded = mAssets.readBytes(path);
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(INT_MAX)) {
        throw std::runtime_error("Renderer texture has an invalid size: " + path);
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(encoded.data(),
                    static_cast<int>(encoded.size()), &width, &height,
                    &sourceChannels, kRgbaChannelCount),
            &stbi_image_free);
    if (pixels == nullptr || width <= 0 || height <= 0
            || sourceChannels <= 0) {
        throw std::runtime_error(decodeFailure(path));
    }

    const std::uint64_t byteCount = static_cast<std::uint64_t>(width)
            * static_cast<std::uint64_t>(height) * kRgbaChannelCount;
    if (byteCount > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error(
                "Decoded renderer texture is too large: " + path);
    }

    NativeRenderTexturePayload result;
    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);
    result.pixelFormat = NativeRenderTexturePixelFormat::Rgba8;
    result.minFilter = NativeRenderTextureFilter::Nearest;
    result.magFilter = NativeRenderTextureFilter::Linear;
    result.wrapS = clampToEdge ? NativeRenderTextureWrap::ClampToEdge
                               : NativeRenderTextureWrap::Repeat;
    result.wrapT = result.wrapS;
    const std::size_t decodedSize = static_cast<std::size_t>(byteCount);
    result.pixels.assign(pixels.get(), pixels.get() + decodedSize);
    return result;
}
