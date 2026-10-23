#include "AndroidJniRenderBridge.h"

#ifdef __ANDROID__

#include <memory>
#include <stdexcept>

#include "app/NativeCellularityController.h"
#include "platform/android/AndroidRenderAssets.h"
#include "platform/android/AndroidRenderAssetsLease.h"
#include "render/AssetRenderTextureLoader.h"
#include "render/backend/gles/GlesRenderSurfaceSession.h"

AndroidGles30Api& androidGl() {
    static AndroidGles30Api gl;
    return gl;
}

void loadAndroidRendererResources(
        NativeCellularityController& controller,
        const AndroidRenderAssetsLease& assetsLease) {
    AndroidRenderAssets assets = assetsLease.assets();
    AssetRenderTextureLoader textureLoader(assets);
    if (!controller.hasRenderSurfaceSession()) {
        controller.installRenderSurfaceSession(
                std::make_unique<
                        cellularity::render::gles::GlesRenderSurfaceSession>(
                        androidGl()));
    } else if (controller.renderBackendKind()
            != cellularity::render::RenderBackendKind::OpenGlEs) {
        throw std::logic_error(
                "Android GLES surface cannot attach a different render backend");
    }
    controller.loadRendererResources(assets, textureLoader);
}

#endif
