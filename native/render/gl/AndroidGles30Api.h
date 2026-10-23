#pragma once

#if defined(__ANDROID__) || defined(CELLULARITY_HOST_GLES_PROBE)

#include "render/gl/NativeGlApi.h"

class AndroidGles30Api : public NativeGlApi {
public:
    int vertexShader() const override;
    int fragmentShader() const override;
    int linkStatus() const override;
    int compileStatus() const override;
    int texture2d() const override;
    int framebuffer() const override;
    int renderbuffer() const override;
    int colorAttachment0() const override;
    int depthAttachment() const override;
    int depthComponent16() const override;
    int framebufferComplete() const override;
    int framebufferBinding() const override;
    int viewportState() const override;
    int packAlignment() const override;
    int unpackAlignment() const override;
    int red() const override;
    int r8() const override;
    int rgba() const override;
    int rgba8() const override;
    int unsignedByte() const override;
    int textureMinFilter() const override;
    int textureMagFilter() const override;
    int textureWrapS() const override;
    int textureWrapT() const override;
    int linear() const override;
    int nearest() const override;
    int clampToEdge() const override;
    int repeat() const override;
    int blend() const override;
    int depthTest() const override;
    int one() const override;
    int srcAlpha() const override;
    int oneMinusSrcAlpha() const override;
    int always() const override;
    int less() const override;
    int texture0() const override;
    int floatType() const override;
    int triangles() const override;
    int unsignedShort() const override;
    int arrayBuffer() const override;
    int elementArrayBuffer() const override;
    int staticDraw() const override;
    int streamDraw() const override;
    int colorBufferBit() const override;
    int depthBufferBit() const override;

    void viewport(int x, int y, int width, int height) override;
    void clearColor(float red, float green, float blue, float alpha) override;
    void clear(int mask) override;
    void enable(int capability) override;
    void disable(int capability) override;
    void blendFunc(int sourceFactor, int destinationFactor) override;
    void depthMask(bool flag) override;
    void depthFunc(int function) override;
    void getIntegers(int parameterName, int* values) override;
    void pixelStore(int parameterName, int value) override;

    int createShader(int type) override;
    void shaderSource(int shader, const std::string& source) override;
    void compileShader(int shader) override;
    int getShaderParameter(int shader, int parameterName) override;
    std::string getShaderInfoLog(int shader) override;
    void deleteShader(int shader) override;

    int createProgram() override;
    void attachShader(int program, int shader) override;
    void linkProgram(int program) override;
    int getProgramParameter(int program, int parameterName) override;
    std::string getProgramInfoLog(int program) override;
    void deleteProgram(int program) override;
    void useProgram(int program) override;

    int getUniformLocation(int program, const std::string& name) override;
    int getAttribLocation(int program, const std::string& name) override;

    void activeTexture(int texture) override;
    void genTextures(int count, int* textures) override;
    void bindTexture(int target, int texture) override;
    void textureParameter(int target, int parameterName, int value) override;
    void textureImage2d(int target, int level, int internalFormat,
            int width, int height, int border, int format,
            int type, const void* pixels) override;
    void deleteTextures(int count, const int* textures) override;
    void uniform1i(int location, int value) override;
    void uniform1f(int location, float value) override;
    void uniform3f(int location, float value0, float value1, float value2) override;
    void uniform4f(int location, float value0, float value1, float value2, float value3) override;
    void uniformMatrix4fv(int location, int count, bool transpose, const float* value) override;
    void enableVertexAttribArray(int index) override;
    void disableVertexAttribArray(int index) override;
    void vertexAttribPointerData(int index, int size, int type, bool normalized, int stride, const void* data) override;
    void vertexAttribPointerOffset(int index, int size, int type, bool normalized, int stride, int offsetBytes) override;
    void drawArrays(int mode, int first, int count) override;
    void drawArraysInstanced(int mode, int first, int count, int instanceCount) override;
    void drawElementsOffset(int mode, int count, int type, int offsetBytes) override;
    void vertexAttribDivisor(int index, int divisor) override;

    void genBuffers(int count, int* buffers) override;
    void bindBuffer(int target, int buffer) override;
    void bufferData(int target, int sizeBytes, const void* data, int usage) override;
    void bufferSubData(int target, int offsetBytes, int sizeBytes, const void* data) override;
    void deleteBuffers(int count, const int* buffers) override;

    void genVertexArrays(int count, int* vertexArrays) override;
    void bindVertexArray(int vertexArray) override;
    void deleteVertexArrays(int count, const int* vertexArrays) override;

    void genFramebuffers(int count, int* framebuffers) override;
    void bindFramebuffer(int target, int framebuffer) override;
    void framebufferTexture2d(int target, int attachment, int textureTarget,
            int texture, int level) override;
    int checkFramebufferStatus(int target) override;
    void deleteFramebuffers(int count, const int* framebuffers) override;

    void genRenderbuffers(int count, int* renderbuffers) override;
    void bindRenderbuffer(int target, int renderbuffer) override;
    void renderbufferStorage(int target, int internalFormat, int width,
            int height) override;
    void framebufferRenderbuffer(int target, int attachment,
            int renderbufferTarget,
            int renderbuffer) override;
    void deleteRenderbuffers(int count, const int* renderbuffers) override;

    void readPixels(int x, int y, int width, int height, int format, int type,
            void* pixels) override;
};

#endif
