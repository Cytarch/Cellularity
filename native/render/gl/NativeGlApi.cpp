#include "render/gl/NativeGlApi.h"

int NativeGlApi::vertexShader() const {
    return 0;
}

int NativeGlApi::fragmentShader() const {
    return 0;
}

int NativeGlApi::linkStatus() const {
    return 0;
}

int NativeGlApi::compileStatus() const {
    return 0;
}

int NativeGlApi::texture2d() const {
    return 0;
}

int NativeGlApi::framebuffer() const {
    return 0;
}

int NativeGlApi::renderbuffer() const {
    return 0;
}

int NativeGlApi::colorAttachment0() const {
    return 0;
}

int NativeGlApi::depthAttachment() const {
    return 0;
}

int NativeGlApi::depthComponent16() const {
    return 0;
}

int NativeGlApi::framebufferComplete() const {
    return 0;
}

int NativeGlApi::framebufferBinding() const {
    return 0;
}

int NativeGlApi::viewportState() const {
    return 0;
}

int NativeGlApi::packAlignment() const {
    return 0;
}

int NativeGlApi::unpackAlignment() const {
    return 0;
}

int NativeGlApi::red() const {
    return 0;
}

int NativeGlApi::r8() const {
    return 0;
}

int NativeGlApi::rgba() const {
    return 0;
}

int NativeGlApi::rgba8() const {
    return 0;
}

int NativeGlApi::unsignedByte() const {
    return 0;
}

int NativeGlApi::textureMinFilter() const {
    return 0;
}

int NativeGlApi::textureMagFilter() const {
    return 0;
}

int NativeGlApi::textureWrapS() const {
    return 0;
}

int NativeGlApi::textureWrapT() const {
    return 0;
}

int NativeGlApi::linear() const {
    return 0;
}

int NativeGlApi::nearest() const {
    return 0;
}

int NativeGlApi::clampToEdge() const {
    return 0;
}

int NativeGlApi::repeat() const {
    return 0;
}

int NativeGlApi::blend() const {
    return 0;
}

int NativeGlApi::depthTest() const {
    return 0;
}

int NativeGlApi::one() const {
    return 0;
}

int NativeGlApi::srcAlpha() const {
    return 0;
}

int NativeGlApi::oneMinusSrcAlpha() const {
    return 0;
}

int NativeGlApi::always() const {
    return 0;
}

int NativeGlApi::less() const {
    return 0;
}

int NativeGlApi::texture0() const {
    return 0;
}

int NativeGlApi::floatType() const {
    return 0;
}

int NativeGlApi::triangles() const {
    return 0;
}

int NativeGlApi::unsignedShort() const {
    return 0;
}

int NativeGlApi::arrayBuffer() const {
    return 0;
}

int NativeGlApi::elementArrayBuffer() const {
    return 0;
}

int NativeGlApi::staticDraw() const {
    return 0;
}

int NativeGlApi::streamDraw() const {
    return 0;
}

int NativeGlApi::colorBufferBit() const {
    return 0;
}

int NativeGlApi::depthBufferBit() const {
    return 0;
}

void NativeGlApi::viewport(int, int, int, int) {}

void NativeGlApi::clearColor(float, float, float, float) {}

void NativeGlApi::clear(int) {}

void NativeGlApi::enable(int) {}

void NativeGlApi::disable(int) {}

void NativeGlApi::blendFunc(int, int) {}

void NativeGlApi::depthMask(bool) {}

void NativeGlApi::depthFunc(int) {}

void NativeGlApi::getIntegers(int, int*) {}

void NativeGlApi::pixelStore(int, int) {}

int NativeGlApi::createShader(int) {
    return 0;
}

void NativeGlApi::shaderSource(int, const std::string&) {}

void NativeGlApi::compileShader(int) {}

int NativeGlApi::getShaderParameter(int, int) {
    return 0;
}

std::string NativeGlApi::getShaderInfoLog(int) {
    return {};
}

void NativeGlApi::deleteShader(int) {}

int NativeGlApi::createProgram() {
    return 0;
}

void NativeGlApi::attachShader(int, int) {}

void NativeGlApi::linkProgram(int) {}

int NativeGlApi::getProgramParameter(int, int) {
    return 0;
}

std::string NativeGlApi::getProgramInfoLog(int) {
    return {};
}

void NativeGlApi::deleteProgram(int) {}

void NativeGlApi::useProgram(int) {}

int NativeGlApi::getUniformLocation(int, const std::string&) {
    return -1;
}

int NativeGlApi::getAttribLocation(int, const std::string&) {
    return -1;
}

void NativeGlApi::activeTexture(int) {}

void NativeGlApi::genTextures(int, int*) {}

void NativeGlApi::bindTexture(int, int) {}

void NativeGlApi::textureParameter(int, int, int) {}

void NativeGlApi::textureImage2d(int, int, int, int, int, int, int, int,
        const void*) {}

void NativeGlApi::deleteTextures(int, const int*) {}

void NativeGlApi::uniform1i(int, int) {}

void NativeGlApi::uniform1f(int, float) {}

void NativeGlApi::uniform3f(int, float, float, float) {}

void NativeGlApi::uniform4f(int, float, float, float, float) {}

void NativeGlApi::uniformMatrix4fv(int, int, bool, const float*) {}

void NativeGlApi::enableVertexAttribArray(int) {}

void NativeGlApi::disableVertexAttribArray(int) {}

void NativeGlApi::vertexAttribPointerData(int, int, int, bool, int, const void*) {}

void NativeGlApi::vertexAttribPointerOffset(int, int, int, bool, int, int) {}

void NativeGlApi::drawArrays(int, int, int) {}

void NativeGlApi::drawArraysInstanced(int, int, int, int) {}

void NativeGlApi::drawElementsOffset(int, int, int, int) {}

void NativeGlApi::vertexAttribDivisor(int, int) {}

void NativeGlApi::genBuffers(int, int*) {}

void NativeGlApi::bindBuffer(int, int) {}

void NativeGlApi::bufferData(int, int, const void*, int) {}

void NativeGlApi::bufferSubData(int, int, int, const void*) {}

void NativeGlApi::deleteBuffers(int, const int*) {}

void NativeGlApi::genVertexArrays(int, int*) {}

void NativeGlApi::bindVertexArray(int) {}

void NativeGlApi::deleteVertexArrays(int, const int*) {}

void NativeGlApi::genFramebuffers(int, int*) {}

void NativeGlApi::bindFramebuffer(int, int) {}

void NativeGlApi::framebufferTexture2d(int, int, int, int, int) {}

int NativeGlApi::checkFramebufferStatus(int) {
    return 0;
}

void NativeGlApi::deleteFramebuffers(int, const int*) {}

void NativeGlApi::genRenderbuffers(int, int*) {}

void NativeGlApi::bindRenderbuffer(int, int) {}

void NativeGlApi::renderbufferStorage(int, int, int, int) {}

void NativeGlApi::framebufferRenderbuffer(int, int, int, int) {}

void NativeGlApi::deleteRenderbuffers(int, const int*) {}

void NativeGlApi::readPixels(int, int, int, int, int, int, void*) {}
