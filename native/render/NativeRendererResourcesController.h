#pragma once

#include <memory>

class NativeCellRendererResources;
class NativeGlApi;
class NativeRenderAssets;
class NativeRenderTextureLoader;

namespace cellularity::render {
class SimulationWorldGpuRenderer;
} // namespace cellularity::render

class NativeRendererResourcesController {
public:
    bool hasResources() const;
    NativeCellRendererResources& require();

    void load(cellularity::render::SimulationWorldGpuRenderer& renderer,
            const NativeRenderAssets& assets, NativeGlApi& gl,
            NativeRenderTextureLoader& textureLoader, bool isPreviewMode);
    void release(cellularity::render::SimulationWorldGpuRenderer& renderer,
            NativeGlApi& gl);
    void abandon(cellularity::render::SimulationWorldGpuRenderer& renderer);

private:
    std::unique_ptr<NativeCellRendererResources> mResources;
};
