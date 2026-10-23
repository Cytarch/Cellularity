#include "NativeShaders.h"

namespace {

constexpr int kInstanceStrideBytes = 9 * static_cast<int>(sizeof(float));
constexpr int kGlyphOffsetBytes = 4 * static_cast<int>(sizeof(float));
constexpr int kColorOffsetBytes = 5 * static_cast<int>(sizeof(float));

} // namespace

PixelTextShader::PixelTextShader(const NativeRenderAssets& assets,
        NativeGlApi& gl)
        : ShaderProgram(assets, gl, "pixel_text") {
    aRect = getAttrib("aRect");
    aGlyph = getAttrib("aGlyph");
    aColor = getAttrib("aColor");
    uMVPMatrix = getUniform("uMVPMatrix");
    uGlyphAtlas = getUniform("uGlyphAtlas");
}

void PixelTextShader::configureVertexArray(int instanceBuffer) {
    mGl.bindBuffer(mGl.arrayBuffer(), instanceBuffer);
    if (aRect != -1) {
        mGl.enableVertexAttribArray(aRect);
        mGl.vertexAttribPointerOffset(aRect, 4, mGl.floatType(), false,
                kInstanceStrideBytes, 0);
        mGl.vertexAttribDivisor(aRect, 1);
    }
    if (aGlyph != -1) {
        mGl.enableVertexAttribArray(aGlyph);
        mGl.vertexAttribPointerOffset(aGlyph, 1, mGl.floatType(), false,
                kInstanceStrideBytes, kGlyphOffsetBytes);
        mGl.vertexAttribDivisor(aGlyph, 1);
    }
    if (aColor != -1) {
        mGl.enableVertexAttribArray(aColor);
        mGl.vertexAttribPointerOffset(aColor, 4, mGl.floatType(), false,
                kInstanceStrideBytes, kColorOffsetBytes);
        mGl.vertexAttribDivisor(aColor, 1);
    }
}

void PixelTextShader::drawInstancedVertexArray(
        const std::array<float, 16>& mvpMatrix, int vertexArray,
        int instanceCount, int glyphAtlasTexture) {
    if (vertexArray == 0 || instanceCount <= 0 || glyphAtlasTexture == 0) {
        return;
    }

    use();
    mGl.uniformMatrix4fv(uMVPMatrix, 1, false, mvpMatrix.data());
    mGl.activeTexture(mGl.texture0());
    mGl.bindTexture(mGl.texture2d(), glyphAtlasTexture);
    mGl.uniform1i(uGlyphAtlas, 0);
    mGl.bindVertexArray(vertexArray);

    mGl.drawArraysInstanced(mGl.triangles(), 0, 6, instanceCount);
    mGl.bindVertexArray(0);
}
