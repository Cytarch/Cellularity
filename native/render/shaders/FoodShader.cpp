#include "NativeShaders.h"

FoodShader::FoodShader(const NativeRenderAssets& assets, NativeGlApi& gl)
        : ShaderProgram(assets, gl, "food") {
    aPosition = getAttrib("vPosition");
    aInstance = getAttrib("vInstance");
    aColor = getAttrib("color");
    uMVPMatrix = getUniform("uMVPMatrix");
}

void FoodShader::begin(const std::array<float, 16>& mvpMatrix) {
    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    if (aPosition != -1)
        mGl.enableVertexAttribArray(aPosition);
    if (aInstance != -1)
        mGl.enableVertexAttribArray(aInstance);
    if (aColor != -1)
        mGl.enableVertexAttribArray(aColor);
}

void FoodShader::drawInstancedVbo(int positionBuffer, int instanceBuffer, int colorBuffer, int instanceCount) {
    if (aPosition != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), positionBuffer);
        mGl.vertexAttribPointerOffset(aPosition, 2, mGl.floatType(), false, 8, 0);
    }
    if (aInstance != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), instanceBuffer);
        mGl.vertexAttribPointerOffset(aInstance, 4, mGl.floatType(), false, 16, 0);
        mGl.vertexAttribDivisor(aInstance, 1);
    }
    if (aColor != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), colorBuffer);
        mGl.vertexAttribPointerOffset(aColor, 4, mGl.floatType(), false, 16, 0);
        mGl.vertexAttribDivisor(aColor, 1);
    }
    mGl.drawArraysInstanced(mGl.triangles(), 0, 6, instanceCount);
    if (aInstance != -1)
        mGl.vertexAttribDivisor(aInstance, 0);
    if (aColor != -1)
        mGl.vertexAttribDivisor(aColor, 0);
}

void FoodShader::end() {
    if (aPosition != -1)
        mGl.disableVertexAttribArray(aPosition);
    if (aInstance != -1)
        mGl.disableVertexAttribArray(aInstance);
    if (aColor != -1)
        mGl.disableVertexAttribArray(aColor);
}
