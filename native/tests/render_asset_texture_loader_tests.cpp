#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "render/AssetRenderTextureLoader.h"
#include "render/NativeRenderAssets.h"
#include "tests/support/TestAssertions.h"

namespace cellularity::tests {
namespace {

class DirectoryRenderAssets final : public NativeRenderAssets {
public:
    explicit DirectoryRenderAssets(std::filesystem::path root)
            : mRoot(std::move(root)) {}

    std::string readText(const std::string&) const override {
        throw std::logic_error("Text resources are not used in this test");
    }

    std::vector<unsigned char> readBytes(
            const std::string& path) const override {
        lastPath = path;
        std::ifstream input(mRoot / path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Could not open test render asset: " + path);
        }
        return {std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
    }

    mutable std::string lastPath;

private:
    std::filesystem::path mRoot;
};

class MemoryRenderAssets final : public NativeRenderAssets {
public:
    explicit MemoryRenderAssets(std::vector<unsigned char> bytes)
            : mBytes(std::move(bytes)) {}

    std::string readText(const std::string&) const override {
        throw std::logic_error("Text resources are not used in this test");
    }

    std::vector<unsigned char> readBytes(
            const std::string& path) const override {
        lastPath = path;
        return mBytes;
    }

    mutable std::string lastPath;

private:
    std::vector<unsigned char> mBytes;
};

void testTextureLoaderDecodesCanonicalRgbaPayload() {
    DirectoryRenderAssets assets(CELLULARITY_RENDER_ASSET_ROOT);
    AssetRenderTextureLoader loader(assets);

    const NativeRenderTexturePayload payload =
            loader.loadTexture("small_dots", false);

    assertTrue(assets.lastPath == "textures/small_dots.png",
            "Asset texture loader uses the canonical logical path");
    assertTrue(payload.width == 256 && payload.height == 256,
            "Asset texture loader preserves PNG dimensions");
    assertTrue(payload.pixelFormat == NativeRenderTexturePixelFormat::Rgba8
                    && payload.minFilter
                            == NativeRenderTextureFilter::Nearest
                    && payload.magFilter
                            == NativeRenderTextureFilter::Linear,
            "Asset texture loader publishes the backend-neutral pixel contract");
    assertTrue(payload.wrapS == NativeRenderTextureWrap::Repeat
                    && payload.wrapT == NativeRenderTextureWrap::Repeat,
            "Repeating renderer textures retain their sampler contract");
    assertTrue(payload.valid() && payload.pixels[0] == 0
                    && payload.pixels[1] == 0 && payload.pixels[2] == 0
                    && payload.pixels[3] == 255,
            "Asset texture loader preserves canonical small-dots RGBA bytes");
}

void testTextureLoaderAppliesClampSamplerContract() {
    DirectoryRenderAssets assets(CELLULARITY_RENDER_ASSET_ROOT);
    AssetRenderTextureLoader loader(assets);

    const NativeRenderTexturePayload payload =
            loader.loadTexture("gauss_bessel_integral", true);

    assertTrue(payload.wrapS == NativeRenderTextureWrap::ClampToEdge
                    && payload.wrapT
                            == NativeRenderTextureWrap::ClampToEdge,
            "Clamped renderer textures retain their sampler contract");
    assertTrue(payload.valid() && payload.width == 256 && payload.height == 256
                    && payload.pixels[0] == 255 && payload.pixels[1] == 0
                    && payload.pixels[2] == 0 && payload.pixels[3] == 255,
            "Asset texture loader decodes the canonical Bessel texture");
}

void testTextureLoaderRejectsUnsafeNamesAndInvalidPng() {
    MemoryRenderAssets validAssets({0x89, 0x50, 0x4e, 0x47});
    AssetRenderTextureLoader validLoader(validAssets);
    assertThrows([&] {
        validLoader.loadTexture("../small_dots", false);
    },
            "Asset texture loader rejects path traversal");

    MemoryRenderAssets invalidAssets({0x00, 0x01, 0x02});
    AssetRenderTextureLoader invalidLoader(invalidAssets);
    assertThrows([&] {
        invalidLoader.loadTexture("small_dots", false);
    },
            "Asset texture loader rejects malformed PNG data");
}

} // namespace
} // namespace cellularity::tests

int main() {
    cellularity::tests::testTextureLoaderDecodesCanonicalRgbaPayload();
    cellularity::tests::testTextureLoaderAppliesClampSamplerContract();
    cellularity::tests::testTextureLoaderRejectsUnsafeNamesAndInvalidPng();
    std::cout << "native render asset texture loader tests passed\n";
    return 0;
}
