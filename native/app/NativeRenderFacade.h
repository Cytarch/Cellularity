#pragma once

#include <memory>

#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderFrameTypes.h"

class EnvironmentFieldPreviewRenderer;
class NativeCellRendererResources;
class NativeGlApi;
class NativeRenderAssets;
class NativeRendererResourcesController;
class NativeRenderTextureLoader;

namespace cellularity::render {
class SimulationWorldGpuRenderer;
struct EnvironmentFieldPreviewCommand;
} // namespace cellularity::render

class NativeRenderFacade {
public:
    NativeRenderFacade();
    ~NativeRenderFacade();

    bool hasResources() const;
    void loadResources(cellularity::render::SimulationWorldGpuRenderer& renderer,
            bool previewMode, const NativeRenderAssets& assets,
            NativeGlApi& gl, NativeRenderTextureLoader& textureLoader);
    void releaseResources(cellularity::render::SimulationWorldGpuRenderer& renderer,
            NativeGlApi& gl);
    void abandonResources(cellularity::render::SimulationWorldGpuRenderer& renderer);
    void
    uploadDynamicBuffers(cellularity::render::SimulationWorldGpuRenderer& renderer,
            NativeGlApi& gl,
            const cellularity::render::PreparedRenderFrame& frame);
    void draw(cellularity::render::SimulationWorldGpuRenderer& renderer,
            NativeGlApi& gl,
            const cellularity::render::PreparedRenderFrame& frame,
            const cellularity::render::RenderPresentationState& presentation);
    int drawEnvironmentFieldPreview(
            const cellularity::render::EnvironmentFieldPreviewCommand& command,
            NativeGlApi& gl);

    NativeCellRendererResources& requireResources();

private:
    std::unique_ptr<EnvironmentFieldPreviewRenderer>
            mEnvironmentFieldPreviewRenderer;
    std::unique_ptr<NativeRendererResourcesController> mResources;
};
