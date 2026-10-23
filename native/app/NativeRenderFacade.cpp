#include "app/NativeRenderFacade.h"

#include <memory>

#include "app/EnvironmentFieldPreviewRenderer.h"
#include "render/SimulationWorldGpuRenderer.h"
#include "render/NativeCellRendererResources.h"
#include "render/NativeRenderAssets.h"
#include "render/NativeRenderTextureLoader.h"
#include "render/NativeRendererResourcesController.h"
#include "render/backend/RenderSurfaceSession.h"
#include "render/gl/NativeGlApi.h"

NativeRenderFacade::NativeRenderFacade()
        : mEnvironmentFieldPreviewRenderer(
                  std::make_unique<EnvironmentFieldPreviewRenderer>()),
          mResources(std::make_unique<NativeRendererResourcesController>()) {}

NativeRenderFacade::~NativeRenderFacade() = default;

bool NativeRenderFacade::hasResources() const {
    return mResources->hasResources();
}

void NativeRenderFacade::loadResources(
        cellularity::render::SimulationWorldGpuRenderer& renderer, bool previewMode,
        const NativeRenderAssets& assets, NativeGlApi& gl,
        NativeRenderTextureLoader& textureLoader) {
    mResources->load(renderer, assets, gl, textureLoader, previewMode);
}

void NativeRenderFacade::releaseResources(
        cellularity::render::SimulationWorldGpuRenderer& renderer, NativeGlApi& gl) {
    mResources->release(renderer, gl);
}

void NativeRenderFacade::abandonResources(
        cellularity::render::SimulationWorldGpuRenderer& renderer) {
    mResources->abandon(renderer);
}

void NativeRenderFacade::uploadDynamicBuffers(
        cellularity::render::SimulationWorldGpuRenderer& renderer, NativeGlApi& gl,
        const cellularity::render::PreparedRenderFrame& frame) {
    renderer.uploadDynamicBuffers(gl, frame);
}

void NativeRenderFacade::draw(
        cellularity::render::SimulationWorldGpuRenderer& renderer, NativeGlApi& gl,
        const cellularity::render::PreparedRenderFrame& frame,
        const cellularity::render::RenderPresentationState& presentation) {
    NativeCellRendererResources& resources = requireResources();
    if (frame.environment.previewMode) {
        renderer.drawPreview(gl, resources.basicCellShaders(),
                resources.outlineCellShaders(),
                resources.arrowShader(), resources.deadSplashShader(),
                resources.linkShader(), &resources.markerShader(),
                resources.dotTextureHandle(),
                resources.besselTextureHandle(), frame, presentation);
    } else {
        renderer.draw(gl, resources.basicCellShaders(),
                resources.outlineCellShaders(), resources.arrowShader(),
                resources.backgroundShader(), resources.foodShader(),
                resources.deadSplashShader(), resources.linkShader(),
                &resources.markerShader(), resources.dotTextureHandle(),
                resources.besselTextureHandle(), frame, presentation);
    }
}

int NativeRenderFacade::drawEnvironmentFieldPreview(
        const cellularity::render::EnvironmentFieldPreviewCommand& command,
        NativeGlApi& gl) {
    return mEnvironmentFieldPreviewRenderer->draw(
            command, gl, requireResources());
}

NativeCellRendererResources& NativeRenderFacade::requireResources() {
    return mResources->require();
}
