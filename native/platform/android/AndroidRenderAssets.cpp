#include "platform/android/AndroidRenderAssets.h"

#ifdef __ANDROID__

#include <algorithm>
#include <climits>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {

struct AssetCloser final {
    void operator()(AAsset* asset) const noexcept {
        if (asset != nullptr) {
            AAsset_close(asset);
        }
    }
};

} // namespace

AndroidRenderAssets::AndroidRenderAssets(AAssetManager* assetManager)
        : mAssetManager(assetManager) {
}

AAssetManager* AndroidRenderAssets::assetManager() const noexcept {
    return mAssetManager;
}

std::string AndroidRenderAssets::readText(const std::string& path) const {
    std::vector<unsigned char> bytes = readBytes(path);
    return std::string(bytes.begin(), bytes.end());
}

std::vector<unsigned char> AndroidRenderAssets::readBytes(const std::string& path) const {
    if (mAssetManager == nullptr) {
        throw std::runtime_error("AndroidRenderAssets requires a non-null AAssetManager");
    }

    std::unique_ptr<AAsset, AssetCloser> asset(
            AAssetManager_open(
                    mAssetManager, path.c_str(), AASSET_MODE_BUFFER));
    if (asset == nullptr) {
        throw std::runtime_error("Render resource not found: " + path);
    }

    const off64_t length = AAsset_getLength64(asset.get());
    if (length < 0
            || static_cast<std::uint64_t>(length)
                    > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Render resource is too large: " + path);
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const int request = static_cast<int>(std::min<std::size_t>(
                remaining, static_cast<std::size_t>(INT_MAX)));
        const int read = AAsset_read(asset.get(), bytes.data() + offset,
                request);
        if (read <= 0) {
            throw std::runtime_error(
                    "Could not read render resource: " + path);
        }
        offset += static_cast<std::size_t>(read);
    }
    return bytes;
}

#endif
