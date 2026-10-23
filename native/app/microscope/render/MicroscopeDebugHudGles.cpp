#include "app/microscope/render/MicroscopeDebugHud.h"

#include <array>

#include "render/gl/NativeGlApi.h"
#include "render/shaders/NativeShaders.h"

namespace cellularity::microscope {
namespace {

constexpr std::array<float, 16> kIdentityMatrix = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F
};

} // namespace

void MicroscopeDebugHud::draw(
        NativeGlApi& gl, PixelTextShader& shader, int surfaceWidth,
        int surfaceHeight, const MicroscopeDebugHudTelemetry& telemetry) {
    const auto& geometry =
            prepareGeometry(surfaceWidth, surfaceHeight, telemetry);
    if (!geometry.hasCommittedGeometry() || geometry.instanceCount() <= 0) {
        return;
    }

    gl.bindBuffer(gl.elementArrayBuffer(), 0);
    gl.disable(gl.depthTest());
    gl.depthMask(false);
    gl.enable(gl.blend());
    gl.blendFunc(gl.srcAlpha(), gl.oneMinusSrcAlpha());
    mTextRenderer.draw(gl, shader, kIdentityMatrix);
    gl.bindBuffer(gl.arrayBuffer(), 0);
    gl.disable(gl.blend());
    gl.enable(gl.depthTest());
    gl.depthFunc(gl.less());
    gl.depthMask(false);
}

void MicroscopeDebugHud::releaseGlResources(NativeGlApi& gl) {
    mTextRenderer.releaseGlResources(gl);
}

void MicroscopeDebugHud::abandonGlResources() noexcept {
    mTextRenderer.abandonGlResources();
}

} // namespace cellularity::microscope
