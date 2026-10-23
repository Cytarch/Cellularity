#include "NativeShaders.h"

CellShader::CellShader(const NativeRenderAssets& assets, NativeGlApi& gl, const std::string& shader)
        : ShaderProgram(assets, gl, shader) {
    aPosition = getAttrib("vPosition");
    aTC = getAttrib("vTPosition");
    aCenter = getAttrib("vCenter");
    aColor = getAttrib("vColor");
    uMVPMatrix = getUniform("uMVPMatrix");
    uTex1 = getUniform("tex1");
    if (uTex1 == -1) {
        uTex1 = getUniform("chloroplastTexture");
    }
}

void CellShader::begin(const std::array<float, 16>& mvpMatrix, int textureId) {
    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    if (uTex1 != -1) {
        mGl.activeTexture(mGl.texture0());
        mGl.bindTexture(mGl.texture2d(), textureId);
        mGl.uniform1i(uTex1, 0);
    }
    if (aPosition != -1)
        mGl.enableVertexAttribArray(aPosition);
    if (aTC != -1)
        mGl.enableVertexAttribArray(aTC);
    if (aCenter != -1)
        mGl.enableVertexAttribArray(aCenter);
    if (aColor != -1)
        mGl.enableVertexAttribArray(aColor);
}

void CellShader::bindPointerOffsets(int positionBuffer, int positionOffsetBytes, int tcBuffer, int tcOffsetBytes,
        int centerBuffer, int centerOffsetBytes, int colorBuffer, int colorOffsetBytes,
        int centerSize) {
    if (aPosition != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), positionBuffer);
        mGl.vertexAttribPointerOffset(aPosition, 2, mGl.floatType(), false, 8, positionOffsetBytes);
    }
    if (aCenter != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), centerBuffer);
        mGl.vertexAttribPointerOffset(aCenter, centerSize, mGl.floatType(), false, centerSize * 4, centerOffsetBytes);
    }
    if (aTC != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), tcBuffer);
        mGl.vertexAttribPointerOffset(aTC, 4, mGl.floatType(), false, 16, tcOffsetBytes);
    }
    if (aColor != -1) {
        mGl.bindBuffer(mGl.arrayBuffer(), colorBuffer);
        mGl.vertexAttribPointerOffset(aColor, 4, mGl.floatType(), false, 16, colorOffsetBytes);
    }
}

void CellShader::drawElementsVbo(int positionBuffer, int positionOffsetBytes, int tcBuffer, int tcOffsetBytes,
        int centerBuffer, int centerOffsetBytes, int colorBuffer, int colorOffsetBytes,
        int indexBuffer, int indexOffsetBytes, int numIndices, int centerSize) {
    bindPointerOffsets(positionBuffer, positionOffsetBytes, tcBuffer, tcOffsetBytes, centerBuffer, centerOffsetBytes, colorBuffer, colorOffsetBytes, centerSize);
    mGl.bindBuffer(mGl.elementArrayBuffer(), indexBuffer);
    mGl.drawElementsOffset(mGl.triangles(), numIndices, mGl.unsignedShort(), indexOffsetBytes);
}

void CellShader::drawArraysVbo(int positionBuffer, int positionOffsetBytes, int tcBuffer, int tcOffsetBytes,
        int centerBuffer, int centerOffsetBytes, int colorBuffer, int colorOffsetBytes,
        int numVertices, int centerSize) {
    bindPointerOffsets(positionBuffer, positionOffsetBytes, tcBuffer, tcOffsetBytes, centerBuffer, centerOffsetBytes, colorBuffer, colorOffsetBytes, centerSize);
    mGl.drawArrays(mGl.triangles(), 0, numVertices);
}

void CellShader::end() {
    if (aPosition != -1)
        mGl.disableVertexAttribArray(aPosition);
    if (aTC != -1)
        mGl.disableVertexAttribArray(aTC);
    if (aCenter != -1)
        mGl.disableVertexAttribArray(aCenter);
    if (aColor != -1)
        mGl.disableVertexAttribArray(aColor);
}
