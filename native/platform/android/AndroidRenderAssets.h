#pragma once

#ifdef __ANDROID__

#include <android/asset_manager.h>

#include "render/NativeRenderAssets.h"

class AndroidRenderAssets : public NativeRenderAssets {
public:
    explicit AndroidRenderAssets(AAssetManager* assetManager);

    std::string readText(const std::string& path) const override;
    std::vector<unsigned char> readBytes(const std::string& path) const override;
    [[nodiscard]] AAssetManager* assetManager() const noexcept;

private:
    AAssetManager* mAssetManager = nullptr;
};

#endif
