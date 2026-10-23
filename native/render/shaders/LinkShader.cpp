#include "NativeShaders.h"

LinkShader::LinkShader(const NativeRenderAssets& assets, NativeGlApi& gl)
        : ShaderProgram(assets, gl, "link") {
    aLinkVertex = getAttrib("vLinkVertex");
    aLinkPatternPosition = getAttrib("vLinkPatternPosition");
    uMVPMatrix = getUniform("uMVPMatrix");
}

void LinkShader::begin(const std::array<float, 16>& mvpMatrix) {
    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    if (aLinkVertex != -1)
        mGl.enableVertexAttribArray(aLinkVertex);
    if (aLinkPatternPosition != -1)
        mGl.enableVertexAttribArray(aLinkPatternPosition);
}

void LinkShader::drawChunkVbo(int vertexBuffer, int vertexOffsetBytes,
        int patternPositionBuffer, int patternPositionOffsetBytes,
        int numVertices) {
    if (aLinkVertex != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), vertexBuffer);
        mGl.vertexAttribPointerOffset(aLinkVertex, 4, mGl.floatType(), false, 16, vertexOffsetBytes);
    }
    if (aLinkPatternPosition != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), patternPositionBuffer);
        mGl.vertexAttribPointerOffset(aLinkPatternPosition, 1, mGl.floatType(), false, 4, patternPositionOffsetBytes);
    }
    mGl.drawArrays(mGl.triangles(), 0, numVertices);
}

void LinkShader::end() {
    if (aLinkVertex != -1)
        mGl.disableVertexAttribArray(aLinkVertex);
    if (aLinkPatternPosition != -1)
        mGl.disableVertexAttribArray(aLinkPatternPosition);
}
