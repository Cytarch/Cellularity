#include "NativeShaders.h"

#include <cmath>

BackgroundShader::BackgroundShader(const NativeRenderAssets& assets, NativeGlApi& gl)
        : ShaderProgram(assets, gl, "background") {
    aPosition = getAttrib("aPosition");
    uMVPMatrix = getUniform("uMVPMatrix");
    uRadius = getUniform("uRadius");
    uDirection = getUniform("uDirection");
    uBackgroundColor = getUniform("uBackgroundColor");
    uInnerRadiusSquared = getUniform("uInnerRadiusSquared");
}

void BackgroundShader::draw(const std::array<float, 16>& mvpMatrix, double lightAngle, double lightRange,
        double lightIntensity, double radius, const std::array<float, 3>& backgroundColor, double innerRadius) {
    static constexpr float vertices[12] = {
            1.1f, 1.1f, 1.1f, -1.1f, -1.1f, -1.1f,
            -1.1f, -1.1f, -1.1f, 1.1f, 1.1f, 1.1f
    };
    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    mGl.uniform4f(uDirection, static_cast<float>(std::cos(lightAngle)), static_cast<float>(std::sin(lightAngle)),
            static_cast<float>(lightRange), static_cast<float>(lightIntensity));
    mGl.uniform1f(uRadius, static_cast<float>(radius));
    // Match the packed Vulkan ABI's GPU radius precision in both backends.
    const double innerRatio = static_cast<float>(innerRadius) / radius;
    mGl.uniform1f(uInnerRadiusSquared, static_cast<float>(innerRatio * innerRatio));
    mGl.uniform3f(uBackgroundColor, backgroundColor[0], backgroundColor[1], backgroundColor[2]);
    if (aPosition != -1) {
        mGl.enableVertexAttribArray(aPosition);
        mGl.vertexAttribPointerData(aPosition, 2, mGl.floatType(), false, 8, vertices);
        mGl.drawArrays(mGl.triangles(), 0, 6);
        mGl.disableVertexAttribArray(aPosition);
    }
}
