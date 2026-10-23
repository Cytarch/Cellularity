#include "NativeShaders.h"

MarkerShader::MarkerShader(const NativeRenderAssets& assets, NativeGlApi& gl)
        : ShaderProgram(assets, gl, "marker") {
    aPosition = getAttrib("vPosition");
    aCenter = getAttrib("vCenter");
    uMVPMatrix = getUniform("uMVPMatrix");
}

void MarkerShader::draw(const std::array<float, 16>& mvpMatrix, float x,
        float y, float radius, float alpha) {
    static constexpr float vertices[12] = {
            1.1f, 1.1f, 1.1f, -1.1f, -1.1f, -1.1f,
            -1.1f, -1.1f, -1.1f, 1.1f, 1.1f, 1.1f
    };
    float centers[24]{};
    for (int i = 0; i < 6; i++) {
        centers[4 * i] = x;
        centers[4 * i + 1] = y;
        centers[4 * i + 2] = radius;
        centers[4 * i + 3] = alpha;
    }

    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    if (aPosition != -1) {
        mGl.enableVertexAttribArray(aPosition);
        mGl.vertexAttribPointerData(aPosition, 2, mGl.floatType(), false, 8, vertices);
    }
    if (aCenter != -1) {
        mGl.enableVertexAttribArray(aCenter);
        mGl.vertexAttribPointerData(aCenter, 4, mGl.floatType(), false, 16, centers);
    }
    mGl.drawArrays(mGl.triangles(), 0, 6);
    if (aPosition != -1)
        mGl.disableVertexAttribArray(aPosition);
    if (aCenter != -1)
        mGl.disableVertexAttribArray(aCenter);
}
