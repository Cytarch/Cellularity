#include "render/text/PixelTextRenderer.h"

#include "render/gl/NativeGlApi.h"
#include "render/shaders/NativeShaders.h"

namespace cellularity::render {

void PixelTextRenderer::draw(NativeGlApi& gl, PixelTextShader& shader,
        const Matrix& mvpMatrix) {
    if (!mHasCommittedGeometry || mInstanceCount <= 0) {
        return;
    }
    ensureGlyphAtlas(gl);
    uploadGeometry(gl);
    ensureVertexArray(gl, shader);
    shader.drawInstancedVertexArray(mvpMatrix, mVertexArray, mInstanceCount,
            mGlyphAtlasTexture);
}

void PixelTextRenderer::releaseGlResources(NativeGlApi& gl) {
    if (mVertexArray != 0) {
        gl.deleteVertexArrays(1, &mVertexArray);
    }
    if (mInstanceBuffer != 0) {
        gl.deleteBuffers(1, &mInstanceBuffer);
    }
    if (mGlyphAtlasTexture != 0) {
        gl.deleteTextures(1, &mGlyphAtlasTexture);
    }
    abandonGlResources();
}

void PixelTextRenderer::abandonGlResources() noexcept {
    mInstanceBuffer = 0;
    mVertexArray = 0;
    mGlyphAtlasTexture = 0;
    mGeometryUploadDirty = mHasCommittedGeometry && !mPackedInstances.empty();
}

void PixelTextRenderer::ensureGlyphAtlas(NativeGlApi& gl) {
    if (mGlyphAtlasTexture != 0) {
        return;
    }

    gl.genTextures(1, &mGlyphAtlasTexture);
    gl.bindTexture(gl.texture2d(), mGlyphAtlasTexture);
    gl.textureParameter(gl.texture2d(), gl.textureMinFilter(), gl.nearest());
    gl.textureParameter(gl.texture2d(), gl.textureMagFilter(), gl.nearest());
    gl.textureParameter(
            gl.texture2d(), gl.textureWrapS(), gl.clampToEdge());
    gl.textureParameter(
            gl.texture2d(), gl.textureWrapT(), gl.clampToEdge());
    const GlyphAtlas& atlas = glyphAtlas();
    gl.textureImage2d(gl.texture2d(), 0, gl.r8(), kAtlasWidth, kAtlasHeight,
            0, gl.red(), gl.unsignedByte(), atlas.data());
}

void PixelTextRenderer::ensureVertexArray(NativeGlApi& gl,
        PixelTextShader& shader) {
    if (mVertexArray != 0) {
        return;
    }

    gl.genVertexArrays(1, &mVertexArray);
    gl.bindVertexArray(mVertexArray);
    shader.configureVertexArray(mInstanceBuffer);
    gl.bindBuffer(gl.arrayBuffer(), 0);
    gl.bindVertexArray(0);
}

void PixelTextRenderer::uploadGeometry(NativeGlApi& gl) {
    if (mInstanceBuffer == 0) {
        gl.genBuffers(1, &mInstanceBuffer);
        mGeometryUploadDirty = true;
    }
    if (!mGeometryUploadDirty) {
        return;
    }

    const int sizeBytes =
            static_cast<int>(mPackedInstances.size() * sizeof(float));
    gl.bindBuffer(gl.arrayBuffer(), mInstanceBuffer);
    gl.bufferData(gl.arrayBuffer(), sizeBytes, mPackedInstances.data(),
            gl.streamDraw());
    gl.bindBuffer(gl.arrayBuffer(), 0);
    mGeometryUploadDirty = false;
}

} // namespace cellularity::render
