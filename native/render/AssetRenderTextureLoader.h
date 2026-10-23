#pragma once

#include "render/NativeRenderTextureLoader.h"

class NativeRenderAssets;

/** Decodes backend-neutral texture payloads from the canonical asset store. */
class AssetRenderTextureLoader final : public NativeRenderTextureLoader {
public:
    explicit AssetRenderTextureLoader(const NativeRenderAssets& assets);

    NativeRenderTexturePayload loadTexture(
            const std::string& name, bool clampToEdge) override;

private:
    const NativeRenderAssets& mAssets;
};
