#include "NativeShaders.h"

DensityFieldShader::DensityFieldShader(const NativeRenderAssets& assets, NativeGlApi& gl)
        : ShaderProgram(assets, gl, "density_field") {
    aPosition = getAttrib("aPosition");
    uMVPMatrix = getUniform("uMVPMatrix");
    uRadius = getUniform("uRadius");
    uField = getUniform("uField");
    uFieldExtra = getUniform("uFieldExtra");
    uDisplayRange = getUniform("uDisplayRange");
}

void DensityFieldShader::draw(const std::array<float, 16>& mvpMatrix,
        int fieldType,
        double base,
        double strength,
        double angle,
        double offset,
        double softness,
        double radius,
        double displayMin,
        double displayMax) {
    static constexpr float vertices[12] = {
            1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f
    };
    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    mGl.uniform1f(uRadius, static_cast<float>(radius));
    mGl.uniform4f(uField,
            static_cast<float>(fieldType),
            static_cast<float>(base),
            static_cast<float>(strength),
            static_cast<float>(angle));
    mGl.uniform4f(uFieldExtra,
            static_cast<float>(offset),
            static_cast<float>(softness),
            0.0f,
            0.0f);
    mGl.uniform4f(uDisplayRange, static_cast<float>(displayMin), static_cast<float>(displayMax), 0.0f, 0.0f);
    if (aPosition != -1) {
        mGl.enableVertexAttribArray(aPosition);
        mGl.vertexAttribPointerData(aPosition, 2, mGl.floatType(), false, 8, vertices);
        mGl.drawArrays(mGl.triangles(), 0, 6);
        mGl.disableVertexAttribArray(aPosition);
    }
}
