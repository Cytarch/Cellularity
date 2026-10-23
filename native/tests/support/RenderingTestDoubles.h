#pragma once

#include "render/NativeRenderAssets.h"
#include "render/NativeRenderTextureLoader.h"
#include "render/gl/NativeGlApi.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cellularity::tests {

class FakeRenderAssets : public NativeRenderAssets {
public:
    mutable std::vector<std::string> textPaths;

    std::string readText(const std::string& path) const override {
        textPaths.push_back(path);
        return "// test-asset:" + path + "\nvoid main() {}";
    }

    std::vector<unsigned char> readBytes(const std::string& path) const override {
        return std::vector<unsigned char>(path.begin(), path.end());
    }
};

class FakeGlApi : public NativeGlApi {
public:
    int nextShader = 100;
    int nextProgram = 200;
    int usedProgram = 0;
    int uniformMatrixLocation = -1;
    int activeTextureValue = -1;
    int boundTexture = -1;
    int uniformTextureLocation = -1;
    int uniformTextureValue = -1;
    int drawElementsCount = 0;
    int drawElementsOffsetBytes = 0;
    int drawArraysCount = 0;
    int drawArraysInstancedCount = 0;
    int drawArraysInstancedInstances = 0;
    int nextBuffer = 1;
    int nextTexture = 300;
    int nextVertexArray = 400;
    int clearCount = 0;
    int lastClearMask = 0;
    std::array<int, 4> lastViewport{};
    std::array<float, 4> lastClearColor{};
    std::vector<int> enabledAttribs;
    std::vector<int> disabledAttribs;
    std::vector<std::string> shaderSources;
    std::unordered_map<std::string, int> attribs{
            {"vPosition", 1}, {"vTPosition", 2}, {"vCenter", 3},
            {"vColor", 4}, {"vLinkVertex", 5}, {"vLinkPatternPosition", 10},
            {"vInstance", 6}, {"color", 7}, {"aPosition", 8},
            {"aCenter", 9}, {"aRect", 20}, {"aGlyph", 21},
            {"aColor", 22}
    };
    std::unordered_map<std::string, int> uniforms{
            {"uMVPMatrix", 11}, {"tex1", 12}, {"chloroplastTexture", 12},
            {"uRadius", 13}, {"uDirection", 14}, {"uBackgroundColor", 15},
            {"uGridSize", 16}, {"uColor", 17}, {"uSelected", 18},
            {"uWorldRadius", 19}, {"uSceneTexture", 20}, {"uField", 21},
            {"uFieldExtra", 22}, {"uGlyphAtlas", 23}
    };

    struct PointerCall {
        int buffer = 0;
        int index = 0;
        int size = 0;
        int stride = 0;
        int offsetBytes = 0;
        int vertexArray = 0;
    };

    struct BufferDataCall {
        int target = 0;
        int buffer = 0;
        int sizeBytes = 0;
        bool hasData = false;
        int usage = 0;
    };

    struct BufferSubDataCall {
        int target = 0;
        int buffer = 0;
        int offsetBytes = 0;
        int sizeBytes = 0;
    };

    struct TextureParameterCall {
        int parameterName = 0;
        int value = 0;
    };

    struct VertexAttribDivisorCall {
        int index = 0;
        int divisor = 0;
        int vertexArray = 0;
    };

    struct DrawElementsCall {
        int count = 0;
        int offsetBytes = 0;
    };

    struct DrawEvent {
        std::string type;
        int count = 0;
        int instanceCount = 1;
        int program = 0;
        std::string programName;
        bool blendEnabled = false;
        int blendSourceFactor = 0;
        int blendDestinationFactor = 0;
        bool depthTestEnabled = false;
        int depthFunction = 0;
        bool depthWriteEnabled = true;
    };

    struct UniformMatrixCall {
        int location = -1;
        std::array<float, 16> value{};
    };

    struct Uniform3fCall {
        int location = -1;
        std::array<float, 3> value{};
    };

    struct Uniform1iCall {
        int location = -1;
        int value = 0;
    };

    int currentArrayBuffer = 0;
    int currentElementArrayBuffer = 0;
    int currentVertexArray = 0;
    int drawArraysInstancedVertexArray = 0;
    std::vector<PointerCall> pointerCalls;
    std::vector<BufferDataCall> bufferDataCalls;
    std::vector<std::uint8_t> lastBufferDataBytes;
    std::vector<BufferSubDataCall> bufferSubDataCalls;
    std::vector<TextureParameterCall> textureParameterCalls;
    std::vector<VertexAttribDivisorCall> vertexAttribDivisorCalls;
    std::vector<DrawElementsCall> drawElementsCalls;
    std::vector<DrawEvent> drawEvents;
    std::vector<UniformMatrixCall> uniformMatrixCalls;
    std::vector<Uniform3fCall> uniform3fCalls;
    std::vector<Uniform1iCall> uniform1iCalls;
    std::vector<int> deletedBuffers;
    std::vector<int> deletedTextures;
    std::vector<int> generatedVertexArrays;
    std::vector<int> boundVertexArrays;
    std::vector<int> deletedVertexArrays;
    std::vector<std::uint8_t> textureImageBytes;
    int textureImageWidth = 0;
    int textureImageHeight = 0;
    int textureImageInternalFormat = 0;
    int textureImageFormat = 0;
    int textureImageBytesPerPixel = 0;
    int textureImageUploadCount = 0;
    std::vector<int> enabledCaps;
    std::vector<int> disabledCaps;
    std::unordered_map<int, std::string> shaderProgramNames;
    std::unordered_map<int, std::string> linkedProgramNames;
    bool blendEnabled = false;
    int blendSourceFactor = 0;
    int blendDestinationFactor = 0;
    bool depthTestEnabled = false;
    int depthFunction = 119;
    bool depthWriteEnabled = true;

    int vertexShader() const override {
        return 101;
    }

    int fragmentShader() const override {
        return 102;
    }

    int linkStatus() const override {
        return 103;
    }

    int compileStatus() const override {
        return 104;
    }

    int texture2d() const override {
        return 105;
    }

    int red() const override {
        return 132;
    }

    int r8() const override {
        return 133;
    }

    int rgba() const override {
        return 120;
    }

    int rgba8() const override {
        return 121;
    }

    int unsignedByte() const override {
        return 122;
    }

    int textureMinFilter() const override {
        return 123;
    }

    int textureMagFilter() const override {
        return 124;
    }

    int textureWrapS() const override {
        return 125;
    }

    int textureWrapT() const override {
        return 126;
    }

    int nearest() const override {
        return 127;
    }

    int clampToEdge() const override {
        return 128;
    }

    int linear() const override {
        return 129;
    }

    int repeat() const override {
        return 134;
    }

    int unpackAlignment() const override {
        return 135;
    }

    int texture0() const override {
        return 106;
    }

    int blend() const override {
        return 114;
    }

    int depthTest() const override {
        return 115;
    }

    int srcAlpha() const override {
        return 116;
    }

    int oneMinusSrcAlpha() const override {
        return 117;
    }

    int always() const override {
        return 118;
    }

    int less() const override {
        return 119;
    }

    int floatType() const override {
        return 107;
    }

    int triangles() const override {
        return 108;
    }

    int unsignedShort() const override {
        return 109;
    }

    int arrayBuffer() const override {
        return 110;
    }

    int elementArrayBuffer() const override {
        return 111;
    }

    int staticDraw() const override {
        return 112;
    }

    int streamDraw() const override {
        return 113;
    }

    int colorBufferBit() const override {
        return 130;
    }

    int depthBufferBit() const override {
        return 131;
    }

    void viewport(int x, int y, int width, int height) override {
        lastViewport = {x, y, width, height};
    }

    void clearColor(float red, float green, float blue, float alpha) override {
        lastClearColor = {red, green, blue, alpha};
    }

    void clear(int mask) override {
        ++clearCount;
        lastClearMask = mask;
    }

    int createShader(int) override {
        return nextShader++;
    }

    void shaderSource(int shader, const std::string& source) override {
        shaderSources.push_back(source);
        constexpr std::string_view marker = "// test-asset:shaders/";
        const std::size_t markerPosition = source.find(marker);
        const std::size_t extensionPosition = source.find(".glsl", markerPosition);
        if (markerPosition != std::string::npos && extensionPosition != std::string::npos) {
            const std::size_t nameStart = source.rfind('/', extensionPosition);
            if (nameStart != std::string::npos) {
                shaderProgramNames[shader] = source.substr(
                        nameStart + 1, extensionPosition - nameStart - 1);
            }
        }
    }

    void compileShader(int) override {}

    int getShaderParameter(int, int) override {
        return 1;
    }

    void deleteShader(int) override {}

    int createProgram() override {
        return nextProgram++;
    }

    void attachShader(int program, int shader) override {
        const auto found = shaderProgramNames.find(shader);
        if (found != shaderProgramNames.end()) {
            linkedProgramNames[program] = found->second;
        }
    }

    void linkProgram(int) override {}

    int getProgramParameter(int, int) override {
        return 1;
    }

    void deleteProgram(int) override {}

    void useProgram(int program) override {
        usedProgram = program;
    }

    int getUniformLocation(int, const std::string& name) override {
        auto it = uniforms.find(name);
        return it == uniforms.end() ? -1 : it->second;
    }

    int getAttribLocation(int, const std::string& name) override {
        auto it = attribs.find(name);
        return it == attribs.end() ? -1 : it->second;
    }

    void activeTexture(int texture) override {
        activeTextureValue = texture;
    }

    void genTextures(int count, int* textures) override {
        for (int index = 0; index < count; ++index) {
            textures[index] = nextTexture++;
        }
    }

    void bindTexture(int, int texture) override {
        boundTexture = texture;
    }

    void textureParameter(int, int parameterName, int value) override {
        textureParameterCalls.push_back({parameterName, value});
    }

    void textureImage2d(int, int, int internalFormat, int width, int height, int,
            int format, int, const void* pixels) override {
        textureImageWidth = width;
        textureImageHeight = height;
        textureImageInternalFormat = internalFormat;
        textureImageFormat = format;
        textureImageBytesPerPixel = format == red() ? 1 : 4;
        ++textureImageUploadCount;
        const auto* bytes = static_cast<const std::uint8_t*>(pixels);
        const std::size_t byteCount =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(textureImageBytesPerPixel);
        if (bytes == nullptr) {
            textureImageBytes.clear();
        } else {
            textureImageBytes.assign(bytes, bytes + byteCount);
        }
    }

    void deleteTextures(int count, const int* textures) override {
        for (int index = 0; index < count; ++index) {
            deletedTextures.push_back(textures[index]);
        }
    }

    void uniform1i(int location, int value) override {
        uniformTextureLocation = location;
        uniformTextureValue = value;
        uniform1iCalls.push_back({location, value});
    }

    void uniformMatrix4fv(int location, int, bool, const float* value) override {
        uniformMatrixLocation = location;
        UniformMatrixCall call;
        call.location = location;
        std::copy_n(value, call.value.size(), call.value.begin());
        uniformMatrixCalls.push_back(call);
    }

    void uniform3f(int location, float value0, float value1,
            float value2) override {
        uniform3fCalls.push_back({location, {value0, value1, value2}});
    }

    void enable(int capability) override {
        enabledCaps.push_back(capability);
        if (capability == blend()) {
            blendEnabled = true;
        } else if (capability == depthTest()) {
            depthTestEnabled = true;
        }
    }

    void disable(int capability) override {
        disabledCaps.push_back(capability);
        if (capability == blend()) {
            blendEnabled = false;
        } else if (capability == depthTest()) {
            depthTestEnabled = false;
        }
    }

    void blendFunc(int sourceFactor, int destinationFactor) override {
        blendSourceFactor = sourceFactor;
        blendDestinationFactor = destinationFactor;
    }

    void depthMask(bool flag) override {
        depthWriteEnabled = flag;
    }

    void depthFunc(int function) override {
        depthFunction = function;
    }

    void enableVertexAttribArray(int index) override {
        enabledAttribs.push_back(index);
    }

    void disableVertexAttribArray(int index) override {
        disabledAttribs.push_back(index);
    }

    void bindBuffer(int target, int buffer) override {
        if (target == arrayBuffer()) {
            currentArrayBuffer = buffer;
        } else if (target == elementArrayBuffer()) {
            currentElementArrayBuffer = buffer;
        }
    }

    void vertexAttribPointerOffset(int index, int size, int, bool, int stride,
            int offsetBytes) override {
        pointerCalls.push_back({currentArrayBuffer, index, size, stride,
                offsetBytes, currentVertexArray});
    }

    void drawElementsOffset(int, int count, int, int offsetBytes) override {
        drawElementsCount = count;
        drawElementsOffsetBytes = offsetBytes;
        drawElementsCalls.push_back({count, offsetBytes});
        recordDraw("elements", count, 1);
    }

    void drawArrays(int, int, int count) override {
        drawArraysCount = count;
        recordDraw("arrays", count, 1);
    }

    void drawArraysInstanced(int, int, int count, int instanceCount) override {
        drawArraysInstancedCount = count;
        drawArraysInstancedInstances = instanceCount;
        drawArraysInstancedVertexArray = currentVertexArray;
        recordDraw("arrays_instanced", count, instanceCount);
    }

    void vertexAttribDivisor(int index, int divisor) override {
        vertexAttribDivisorCalls.push_back({index, divisor, currentVertexArray});
    }

    void genBuffers(int count, int* buffers) override {
        for (int i = 0; i < count; i++) {
            buffers[i] = nextBuffer++;
        }
    }

    void bufferData(int target, int sizeBytes, const void* data,
            int usage) override {
        int buffer = target == arrayBuffer() ? currentArrayBuffer
                                             : currentElementArrayBuffer;
        bufferDataCalls.push_back(
                {target, buffer, sizeBytes, data != nullptr, usage});
        if (data == nullptr || sizeBytes <= 0) {
            lastBufferDataBytes.clear();
        } else {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            lastBufferDataBytes.assign(bytes, bytes + sizeBytes);
        }
    }

    void bufferSubData(int target, int offsetBytes, int sizeBytes,
            const void*) override {
        int buffer = target == arrayBuffer() ? currentArrayBuffer
                                             : currentElementArrayBuffer;
        bufferSubDataCalls.push_back({target, buffer, offsetBytes, sizeBytes});
    }

    void deleteBuffers(int count, const int* buffers) override {
        for (int i = 0; i < count; i++) {
            deletedBuffers.push_back(buffers[i]);
        }
    }

    void genVertexArrays(int count, int* vertexArrays) override {
        for (int index = 0; index < count; ++index) {
            vertexArrays[index] = nextVertexArray++;
            generatedVertexArrays.push_back(vertexArrays[index]);
        }
    }

    void bindVertexArray(int vertexArray) override {
        currentVertexArray = vertexArray;
        boundVertexArrays.push_back(vertexArray);
    }

    void deleteVertexArrays(int count, const int* vertexArrays) override {
        for (int index = 0; index < count; ++index) {
            deletedVertexArrays.push_back(vertexArrays[index]);
        }
    }

private:
    void recordDraw(const std::string& type, int count, int instanceCount) {
        DrawEvent event;
        event.type = type;
        event.count = count;
        event.instanceCount = instanceCount;
        event.program = usedProgram;
        const auto programName = linkedProgramNames.find(usedProgram);
        if (programName != linkedProgramNames.end()) {
            event.programName = programName->second;
        }
        event.blendEnabled = blendEnabled;
        event.blendSourceFactor = blendSourceFactor;
        event.blendDestinationFactor = blendDestinationFactor;
        event.depthTestEnabled = depthTestEnabled;
        event.depthFunction = depthFunction;
        event.depthWriteEnabled = depthWriteEnabled;
        drawEvents.push_back(std::move(event));
    }
};

class FakeTextureLoader : public NativeRenderTextureLoader {
public:
    struct TextureRequest {
        std::string name;
        bool clampToEdge = false;
    };

    std::vector<TextureRequest> requests;

    NativeRenderTexturePayload loadTexture(
            const std::string& name, bool clampToEdge) override {
        requests.push_back({name, clampToEdge});
        NativeRenderTexturePayload payload;
        payload.width = 1;
        payload.height = 1;
        payload.wrapS = clampToEdge
                ? NativeRenderTextureWrap::ClampToEdge
                : NativeRenderTextureWrap::Repeat;
        payload.wrapT = payload.wrapS;
        payload.pixels = {255, 255, 255, 255};
        return payload;
    }
};

} // namespace cellularity::tests
