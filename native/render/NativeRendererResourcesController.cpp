#include "render/NativeRendererResourcesController.h"

#include <stdexcept>

#include "render/SimulationWorldGpuRenderer.h"
#include "render/NativeCellRendererResources.h"
#include "render/NativeRenderAssets.h"
#include "render/NativeRenderTextureLoader.h"
#include "render/gl/NativeGlApi.h"

bool NativeRendererResourcesController::hasResources() const {
    return mResources != nullptr;
}

NativeCellRendererResources& NativeRendererResourcesController::require() {
    if (mResources == nullptr) {
        throw std::runtime_error("Native renderer resources are not loaded");
    }
    return *mResources;
}

void NativeRendererResourcesController::load(
        cellularity::render::SimulationWorldGpuRenderer& renderer,
        const NativeRenderAssets& assets, NativeGlApi& gl,
        NativeRenderTextureLoader& textureLoader, bool isPreviewMode) {
    if (renderer.hasGlBuffers()) {
        renderer.deleteGlBuffers(gl);
    }
    mResources = std::make_unique<NativeCellRendererResources>(
            assets, gl, textureLoader, isPreviewMode);
    renderer.createGlBuffers(gl);
}

void NativeRendererResourcesController::release(
        cellularity::render::SimulationWorldGpuRenderer& renderer, NativeGlApi& gl) {
    if (renderer.hasGlBuffers()) {
        renderer.deleteGlBuffers(gl);
    }
    mResources.reset();
}

void NativeRendererResourcesController::abandon(
        cellularity::render::SimulationWorldGpuRenderer& renderer) {
    renderer.abandonGlBuffers();
    if (mResources != nullptr) {
        mResources->abandonTextures();
    }
    mResources.reset();
}
