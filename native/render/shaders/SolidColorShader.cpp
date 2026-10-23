#include "NativeShaders.h"

SolidColorShader::SolidColorShader(const NativeRenderAssets& assets, NativeGlApi& gl)
        : ShaderProgram(assets, gl, "solid_color") {
    aPosition = getAttrib("aPosition");
    uMVPMatrix = getUniform("uMVPMatrix");
    uColor = getUniform("uColor");
}

void SolidColorShader::draw(const std::array<float, 16>& mvpMatrix, const std::vector<float>& vertices) {
    draw(mvpMatrix, vertices, {0.41f, 0.80f, 0.29f, 1.0f});
}

void SolidColorShader::draw(const std::array<float, 16>& mvpMatrix,
        const std::vector<float>& vertices,
        const std::array<float, 4>& color) {
    if (vertices.empty()) {
        return;
    }
    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    mGl.uniform4f(uColor, color[0], color[1], color[2], color[3]);
    if (aPosition != -1) {
        mGl.enableVertexAttribArray(aPosition);
        mGl.vertexAttribPointerData(aPosition, 2, mGl.floatType(), false, 8, vertices.data());
        mGl.drawArrays(mGl.triangles(), 0, static_cast<int>(vertices.size() / 2));
        mGl.disableVertexAttribArray(aPosition);
    }
}

void SolidColorShader::drawVbo(const std::array<float, 16>& mvpMatrix,
        int vertexBuffer,
        int vertexOffsetBytes,
        int vertexCount,
        const std::array<float, 4>& color) {
    if (vertexBuffer == 0 || vertexCount <= 0) {
        return;
    }
    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    mGl.uniform4f(uColor, color[0], color[1], color[2], color[3]);
    if (aPosition != -1) {
        mGl.enableVertexAttribArray(aPosition);
        mGl.bindBuffer(mGl.arrayBuffer(), vertexBuffer);
        mGl.vertexAttribPointerOffset(aPosition, 2, mGl.floatType(), false, 8,
                vertexOffsetBytes);
        mGl.drawArrays(mGl.triangles(), 0, vertexCount);
        mGl.disableVertexAttribArray(aPosition);
    }
}
