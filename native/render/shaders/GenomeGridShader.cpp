#include "NativeShaders.h"

GenomeGridShader::GenomeGridShader(const NativeRenderAssets& assets, NativeGlApi& gl)
        : ShaderProgram(assets, gl, "genome_grid") {
    aPosition = getAttrib("aPosition");
    uMVPMatrix = getUniform("uMVPMatrix");
    uWorldRadius = getUniform("uWorldRadius");
    uGridSize = getUniform("uGridSize");
}

void GenomeGridShader::draw(const std::array<float, 16>& mvpMatrix,
        float minX,
        float maxX,
        float minY,
        float maxY,
        double worldRadius,
        int gridSize) {
    const float vertices[12] = {
            maxX, maxY, maxX, minY, minX, minY,
            minX, minY, minX, maxY, maxX, maxY
    };
    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    mGl.uniform1f(uWorldRadius, static_cast<float>(worldRadius));
    mGl.uniform1f(uGridSize, static_cast<float>(gridSize));
    if (aPosition != -1) {
        mGl.enableVertexAttribArray(aPosition);
        mGl.vertexAttribPointerData(aPosition, 2, mGl.floatType(), false, 8, vertices);
        mGl.drawArrays(mGl.triangles(), 0, 6);
        mGl.disableVertexAttribArray(aPosition);
    }
}
