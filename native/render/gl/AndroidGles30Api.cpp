#include "render/gl/AndroidGles30Api.h"

#if defined(__ANDROID__) || defined(CELLULARITY_HOST_GLES_PROBE)

#include <GLES3/gl3.h>

#include <vector>

int AndroidGles30Api::vertexShader() const {
    return GL_VERTEX_SHADER;
}

int AndroidGles30Api::fragmentShader() const {
    return GL_FRAGMENT_SHADER;
}

int AndroidGles30Api::linkStatus() const {
    return GL_LINK_STATUS;
}

int AndroidGles30Api::compileStatus() const {
    return GL_COMPILE_STATUS;
}

int AndroidGles30Api::texture2d() const {
    return GL_TEXTURE_2D;
}

int AndroidGles30Api::framebuffer() const {
    return GL_FRAMEBUFFER;
}

int AndroidGles30Api::renderbuffer() const {
    return GL_RENDERBUFFER;
}

int AndroidGles30Api::colorAttachment0() const {
    return GL_COLOR_ATTACHMENT0;
}

int AndroidGles30Api::depthAttachment() const {
    return GL_DEPTH_ATTACHMENT;
}

int AndroidGles30Api::depthComponent16() const {
    return GL_DEPTH_COMPONENT16;
}

int AndroidGles30Api::framebufferComplete() const {
    return GL_FRAMEBUFFER_COMPLETE;
}

int AndroidGles30Api::framebufferBinding() const {
    return GL_FRAMEBUFFER_BINDING;
}

int AndroidGles30Api::viewportState() const {
    return GL_VIEWPORT;
}

int AndroidGles30Api::packAlignment() const {
    return GL_PACK_ALIGNMENT;
}

int AndroidGles30Api::unpackAlignment() const {
    return GL_UNPACK_ALIGNMENT;
}

int AndroidGles30Api::red() const {
    return GL_RED;
}

int AndroidGles30Api::r8() const {
    return GL_R8;
}

int AndroidGles30Api::rgba() const {
    return GL_RGBA;
}

int AndroidGles30Api::rgba8() const {
    return GL_RGBA8;
}

int AndroidGles30Api::unsignedByte() const {
    return GL_UNSIGNED_BYTE;
}

int AndroidGles30Api::textureMinFilter() const {
    return GL_TEXTURE_MIN_FILTER;
}

int AndroidGles30Api::textureMagFilter() const {
    return GL_TEXTURE_MAG_FILTER;
}

int AndroidGles30Api::textureWrapS() const {
    return GL_TEXTURE_WRAP_S;
}

int AndroidGles30Api::textureWrapT() const {
    return GL_TEXTURE_WRAP_T;
}

int AndroidGles30Api::linear() const {
    return GL_LINEAR;
}

int AndroidGles30Api::nearest() const {
    return GL_NEAREST;
}

int AndroidGles30Api::clampToEdge() const {
    return GL_CLAMP_TO_EDGE;
}

int AndroidGles30Api::repeat() const {
    return GL_REPEAT;
}

int AndroidGles30Api::blend() const {
    return GL_BLEND;
}

int AndroidGles30Api::depthTest() const {
    return GL_DEPTH_TEST;
}

int AndroidGles30Api::srcAlpha() const {
    return GL_SRC_ALPHA;
}

int AndroidGles30Api::oneMinusSrcAlpha() const {
    return GL_ONE_MINUS_SRC_ALPHA;
}

int AndroidGles30Api::always() const {
    return GL_ALWAYS;
}

int AndroidGles30Api::less() const {
    return GL_LESS;
}

int AndroidGles30Api::texture0() const {
    return GL_TEXTURE0;
}

int AndroidGles30Api::one() const {
    return GL_ONE;
}

int AndroidGles30Api::floatType() const {
    return GL_FLOAT;
}

int AndroidGles30Api::triangles() const {
    return GL_TRIANGLES;
}

int AndroidGles30Api::unsignedShort() const {
    return GL_UNSIGNED_SHORT;
}

int AndroidGles30Api::arrayBuffer() const {
    return GL_ARRAY_BUFFER;
}

int AndroidGles30Api::elementArrayBuffer() const {
    return GL_ELEMENT_ARRAY_BUFFER;
}

int AndroidGles30Api::staticDraw() const {
    return GL_STATIC_DRAW;
}

int AndroidGles30Api::streamDraw() const {
    return GL_STREAM_DRAW;
}

int AndroidGles30Api::colorBufferBit() const {
    return GL_COLOR_BUFFER_BIT;
}

int AndroidGles30Api::depthBufferBit() const {
    return GL_DEPTH_BUFFER_BIT;
}

void AndroidGles30Api::viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

void AndroidGles30Api::clearColor(float red, float green, float blue, float alpha) {
    glClearColor(red, green, blue, alpha);
}

void AndroidGles30Api::clear(int mask) {
    glClear(static_cast<GLbitfield>(mask));
}

void AndroidGles30Api::enable(int capability) {
    glEnable(static_cast<GLenum>(capability));
}

void AndroidGles30Api::disable(int capability) {
    glDisable(static_cast<GLenum>(capability));
}

void AndroidGles30Api::blendFunc(int sourceFactor, int destinationFactor) {
    glBlendFunc(static_cast<GLenum>(sourceFactor), static_cast<GLenum>(destinationFactor));
}

void AndroidGles30Api::depthMask(bool flag) {
    glDepthMask(flag ? GL_TRUE : GL_FALSE);
}

void AndroidGles30Api::depthFunc(int function) {
    glDepthFunc(static_cast<GLenum>(function));
}

void AndroidGles30Api::getIntegers(int parameterName, int* values) {
    glGetIntegerv(static_cast<GLenum>(parameterName), reinterpret_cast<GLint*>(values));
}

void AndroidGles30Api::pixelStore(int parameterName, int value) {
    glPixelStorei(static_cast<GLenum>(parameterName), value);
}

int AndroidGles30Api::createShader(int type) {
    return static_cast<int>(glCreateShader(static_cast<GLenum>(type)));
}

void AndroidGles30Api::shaderSource(int shader, const std::string& source) {
    const char* rawSource = source.c_str();
    glShaderSource(static_cast<GLuint>(shader), 1, &rawSource, nullptr);
}

void AndroidGles30Api::compileShader(int shader) {
    glCompileShader(static_cast<GLuint>(shader));
}

int AndroidGles30Api::getShaderParameter(int shader, int parameterName) {
    GLint value = 0;
    glGetShaderiv(static_cast<GLuint>(shader), static_cast<GLenum>(parameterName), &value);
    return value;
}

std::string AndroidGles30Api::getShaderInfoLog(int shader) {
    GLint length = 0;
    glGetShaderiv(static_cast<GLuint>(shader), GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return {};
    }
    std::vector<char> log(static_cast<size_t>(length));
    glGetShaderInfoLog(static_cast<GLuint>(shader), length, nullptr, log.data());
    return std::string(log.data());
}

void AndroidGles30Api::deleteShader(int shader) {
    glDeleteShader(static_cast<GLuint>(shader));
}

int AndroidGles30Api::createProgram() {
    return static_cast<int>(glCreateProgram());
}

void AndroidGles30Api::attachShader(int program, int shader) {
    glAttachShader(static_cast<GLuint>(program), static_cast<GLuint>(shader));
}

void AndroidGles30Api::linkProgram(int program) {
    glLinkProgram(static_cast<GLuint>(program));
}

int AndroidGles30Api::getProgramParameter(int program, int parameterName) {
    GLint value = 0;
    glGetProgramiv(static_cast<GLuint>(program), static_cast<GLenum>(parameterName), &value);
    return value;
}

std::string AndroidGles30Api::getProgramInfoLog(int program) {
    GLint length = 0;
    glGetProgramiv(static_cast<GLuint>(program), GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return {};
    }
    std::vector<char> log(static_cast<size_t>(length));
    glGetProgramInfoLog(static_cast<GLuint>(program), length, nullptr, log.data());
    return std::string(log.data());
}

void AndroidGles30Api::deleteProgram(int program) {
    glDeleteProgram(static_cast<GLuint>(program));
}

void AndroidGles30Api::useProgram(int program) {
    glUseProgram(static_cast<GLuint>(program));
}

int AndroidGles30Api::getUniformLocation(int program, const std::string& name) {
    return glGetUniformLocation(static_cast<GLuint>(program), name.c_str());
}

int AndroidGles30Api::getAttribLocation(int program, const std::string& name) {
    return glGetAttribLocation(static_cast<GLuint>(program), name.c_str());
}

void AndroidGles30Api::activeTexture(int texture) {
    glActiveTexture(static_cast<GLenum>(texture));
}

void AndroidGles30Api::genTextures(int count, int* textures) {
    glGenTextures(count, reinterpret_cast<GLuint*>(textures));
}

void AndroidGles30Api::bindTexture(int target, int texture) {
    glBindTexture(static_cast<GLenum>(target), static_cast<GLuint>(texture));
}

void AndroidGles30Api::textureParameter(int target, int parameterName, int value) {
    glTexParameteri(static_cast<GLenum>(target), static_cast<GLenum>(parameterName), value);
}

void AndroidGles30Api::textureImage2d(int target, int level, int internalFormat,
        int width, int height, int border,
        int format, int type, const void* pixels) {
    glTexImage2D(static_cast<GLenum>(target), level, internalFormat, width,
            height, border, static_cast<GLenum>(format),
            static_cast<GLenum>(type), pixels);
}

void AndroidGles30Api::deleteTextures(int count, const int* textures) {
    glDeleteTextures(count, reinterpret_cast<const GLuint*>(textures));
}

void AndroidGles30Api::uniform1i(int location, int value) {
    glUniform1i(location, value);
}

void AndroidGles30Api::uniform1f(int location, float value) {
    glUniform1f(location, value);
}

void AndroidGles30Api::uniform3f(int location, float value0, float value1, float value2) {
    glUniform3f(location, value0, value1, value2);
}

void AndroidGles30Api::uniform4f(int location, float value0, float value1, float value2, float value3) {
    glUniform4f(location, value0, value1, value2, value3);
}

void AndroidGles30Api::uniformMatrix4fv(int location, int count, bool transpose, const float* value) {
    glUniformMatrix4fv(location, count, transpose ? GL_TRUE : GL_FALSE, value);
}

void AndroidGles30Api::enableVertexAttribArray(int index) {
    glEnableVertexAttribArray(static_cast<GLuint>(index));
}

void AndroidGles30Api::disableVertexAttribArray(int index) {
    glDisableVertexAttribArray(static_cast<GLuint>(index));
}

void AndroidGles30Api::vertexAttribPointerData(int index, int size, int type, bool normalized, int stride, const void* data) {
    glVertexAttribPointer(static_cast<GLuint>(index), size, static_cast<GLenum>(type), normalized ? GL_TRUE : GL_FALSE, stride, data);
}

void AndroidGles30Api::vertexAttribPointerOffset(int index, int size, int type, bool normalized, int stride, int offsetBytes) {
    glVertexAttribPointer(static_cast<GLuint>(index), size, static_cast<GLenum>(type), normalized ? GL_TRUE : GL_FALSE,
            stride, reinterpret_cast<const void*>(static_cast<intptr_t>(offsetBytes)));
}

void AndroidGles30Api::drawArrays(int mode, int first, int count) {
    glDrawArrays(static_cast<GLenum>(mode), first, count);
}

void AndroidGles30Api::drawArraysInstanced(int mode, int first, int count, int instanceCount) {
    glDrawArraysInstanced(static_cast<GLenum>(mode), first, count, instanceCount);
}

void AndroidGles30Api::drawElementsOffset(int mode, int count, int type, int offsetBytes) {
    glDrawElements(static_cast<GLenum>(mode), count, static_cast<GLenum>(type),
            reinterpret_cast<const void*>(static_cast<intptr_t>(offsetBytes)));
}

void AndroidGles30Api::vertexAttribDivisor(int index, int divisor) {
    glVertexAttribDivisor(static_cast<GLuint>(index), static_cast<GLuint>(divisor));
}

void AndroidGles30Api::genBuffers(int count, int* buffers) {
    glGenBuffers(count, reinterpret_cast<GLuint*>(buffers));
}

void AndroidGles30Api::bindBuffer(int target, int buffer) {
    glBindBuffer(static_cast<GLenum>(target), static_cast<GLuint>(buffer));
}

void AndroidGles30Api::bufferData(int target, int sizeBytes, const void* data, int usage) {
    glBufferData(static_cast<GLenum>(target), sizeBytes, data, static_cast<GLenum>(usage));
}

void AndroidGles30Api::bufferSubData(int target, int offsetBytes, int sizeBytes, const void* data) {
    glBufferSubData(static_cast<GLenum>(target), offsetBytes, sizeBytes, data);
}

void AndroidGles30Api::deleteBuffers(int count, const int* buffers) {
    glDeleteBuffers(count, reinterpret_cast<const GLuint*>(buffers));
}

void AndroidGles30Api::genVertexArrays(int count, int* vertexArrays) {
    glGenVertexArrays(count, reinterpret_cast<GLuint*>(vertexArrays));
}

void AndroidGles30Api::bindVertexArray(int vertexArray) {
    glBindVertexArray(static_cast<GLuint>(vertexArray));
}

void AndroidGles30Api::deleteVertexArrays(int count, const int* vertexArrays) {
    glDeleteVertexArrays(count, reinterpret_cast<const GLuint*>(vertexArrays));
}

void AndroidGles30Api::genFramebuffers(int count, int* framebuffers) {
    glGenFramebuffers(count, reinterpret_cast<GLuint*>(framebuffers));
}

void AndroidGles30Api::bindFramebuffer(int target, int framebuffer) {
    glBindFramebuffer(static_cast<GLenum>(target), static_cast<GLuint>(framebuffer));
}

void AndroidGles30Api::framebufferTexture2d(int target, int attachment,
        int textureTarget, int texture,
        int level) {
    glFramebufferTexture2D(static_cast<GLenum>(target),
            static_cast<GLenum>(attachment),
            static_cast<GLenum>(textureTarget),
            static_cast<GLuint>(texture), level);
}

int AndroidGles30Api::checkFramebufferStatus(int target) {
    return static_cast<int>(glCheckFramebufferStatus(static_cast<GLenum>(target)));
}

void AndroidGles30Api::deleteFramebuffers(int count, const int* framebuffers) {
    glDeleteFramebuffers(count, reinterpret_cast<const GLuint*>(framebuffers));
}

void AndroidGles30Api::genRenderbuffers(int count, int* renderbuffers) {
    glGenRenderbuffers(count, reinterpret_cast<GLuint*>(renderbuffers));
}

void AndroidGles30Api::bindRenderbuffer(int target, int renderbuffer) {
    glBindRenderbuffer(static_cast<GLenum>(target), static_cast<GLuint>(renderbuffer));
}

void AndroidGles30Api::renderbufferStorage(int target, int internalFormat,
        int width, int height) {
    glRenderbufferStorage(static_cast<GLenum>(target),
            static_cast<GLenum>(internalFormat), width, height);
}

void AndroidGles30Api::framebufferRenderbuffer(int target, int attachment,
        int renderbufferTarget,
        int renderbuffer) {
    glFramebufferRenderbuffer(static_cast<GLenum>(target),
            static_cast<GLenum>(attachment),
            static_cast<GLenum>(renderbufferTarget),
            static_cast<GLuint>(renderbuffer));
}

void AndroidGles30Api::deleteRenderbuffers(int count, const int* renderbuffers) {
    glDeleteRenderbuffers(count, reinterpret_cast<const GLuint*>(renderbuffers));
}

void AndroidGles30Api::readPixels(int x, int y, int width, int height,
        int format, int type, void* pixels) {
    glReadPixels(x, y, width, height, static_cast<GLenum>(format),
            static_cast<GLenum>(type), pixels);
}

#endif
