#pragma once

#include <cstddef>
#include <string>

class NativeGlApi {
public:
    virtual ~NativeGlApi() = default;

    virtual int vertexShader() const;
    virtual int fragmentShader() const;
    virtual int linkStatus() const;
    virtual int compileStatus() const;
    virtual int texture2d() const;
    virtual int framebuffer() const;
    virtual int renderbuffer() const;
    virtual int colorAttachment0() const;
    virtual int depthAttachment() const;
    virtual int depthComponent16() const;
    virtual int framebufferComplete() const;
    virtual int framebufferBinding() const;
    virtual int viewportState() const;
    virtual int packAlignment() const;
    virtual int unpackAlignment() const;
    virtual int red() const;
    virtual int r8() const;
    virtual int rgba() const;
    virtual int rgba8() const;
    virtual int unsignedByte() const;
    virtual int textureMinFilter() const;
    virtual int textureMagFilter() const;
    virtual int textureWrapS() const;
    virtual int textureWrapT() const;
    virtual int linear() const;
    virtual int nearest() const;
    virtual int clampToEdge() const;
    virtual int repeat() const;
    virtual int blend() const;
    virtual int depthTest() const;
    virtual int one() const;
    virtual int srcAlpha() const;
    virtual int oneMinusSrcAlpha() const;
    virtual int always() const;
    virtual int less() const;
    virtual int texture0() const;
    virtual int floatType() const;
    virtual int triangles() const;
    virtual int unsignedShort() const;
    virtual int arrayBuffer() const;
    virtual int elementArrayBuffer() const;
    virtual int staticDraw() const;
    virtual int streamDraw() const;
    virtual int colorBufferBit() const;
    virtual int depthBufferBit() const;

    virtual void viewport(int x, int y, int width, int height);
    virtual void clearColor(float red, float green, float blue, float alpha);
    virtual void clear(int mask);
    virtual void enable(int capability);
    virtual void disable(int capability);
    virtual void blendFunc(int sourceFactor, int destinationFactor);
    virtual void depthMask(bool flag);
    virtual void depthFunc(int function);
    virtual void getIntegers(int parameterName, int* values);
    virtual void pixelStore(int parameterName, int value);

    virtual int createShader(int type);
    virtual void shaderSource(int shader, const std::string& source);
    virtual void compileShader(int shader);
    virtual int getShaderParameter(int shader, int parameterName);
    virtual std::string getShaderInfoLog(int shader);
    virtual void deleteShader(int shader);

    virtual int createProgram();
    virtual void attachShader(int program, int shader);
    virtual void linkProgram(int program);
    virtual int getProgramParameter(int program, int parameterName);
    virtual std::string getProgramInfoLog(int program);
    virtual void deleteProgram(int program);
    virtual void useProgram(int program);

    virtual int getUniformLocation(int program, const std::string& name);
    virtual int getAttribLocation(int program, const std::string& name);

    virtual void activeTexture(int texture);
    virtual void genTextures(int count, int* textures);
    virtual void bindTexture(int target, int texture);
    virtual void textureParameter(int target, int parameterName, int value);
    virtual void textureImage2d(int target, int level, int internalFormat,
            int width, int height, int border, int format,
            int type, const void* pixels);
    virtual void deleteTextures(int count, const int* textures);
    virtual void uniform1i(int location, int value);
    virtual void uniform1f(int location, float value);
    virtual void uniform3f(int location, float value0, float value1, float value2);
    virtual void uniform4f(int location, float value0, float value1, float value2, float value3);
    virtual void uniformMatrix4fv(int location, int count, bool transpose, const float* value);
    virtual void enableVertexAttribArray(int index);
    virtual void disableVertexAttribArray(int index);
    virtual void vertexAttribPointerData(int index, int size, int type, bool normalized, int stride, const void* data);
    virtual void vertexAttribPointerOffset(int index, int size, int type, bool normalized, int stride, int offsetBytes);
    virtual void drawArrays(int mode, int first, int count);
    virtual void drawArraysInstanced(int mode, int first, int count, int instanceCount);
    virtual void drawElementsOffset(int mode, int count, int type, int offsetBytes);
    virtual void vertexAttribDivisor(int index, int divisor);

    virtual void genBuffers(int count, int* buffers);
    virtual void bindBuffer(int target, int buffer);
    virtual void bufferData(int target, int sizeBytes, const void* data, int usage);
    virtual void bufferSubData(int target, int offsetBytes, int sizeBytes, const void* data);
    virtual void deleteBuffers(int count, const int* buffers);

    virtual void genVertexArrays(int count, int* vertexArrays);
    virtual void bindVertexArray(int vertexArray);
    virtual void deleteVertexArrays(int count, const int* vertexArrays);

    virtual void genFramebuffers(int count, int* framebuffers);
    virtual void bindFramebuffer(int target, int framebuffer);
    virtual void framebufferTexture2d(int target, int attachment,
            int textureTarget, int texture,
            int level);
    virtual int checkFramebufferStatus(int target);
    virtual void deleteFramebuffers(int count, const int* framebuffers);

    virtual void genRenderbuffers(int count, int* renderbuffers);
    virtual void bindRenderbuffer(int target, int renderbuffer);
    virtual void renderbufferStorage(int target, int internalFormat,
            int width, int height);
    virtual void framebufferRenderbuffer(int target, int attachment,
            int renderbufferTarget,
            int renderbuffer);
    virtual void deleteRenderbuffers(int count, const int* renderbuffers);

    virtual void readPixels(int x, int y, int width, int height, int format,
            int type, void* pixels);
};
