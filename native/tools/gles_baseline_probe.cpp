#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/NativeCellularityController.h"
#include "app/NativeRenderFacade.h"
#include "app/RenderStateSnapshots.h"
#include "app/interaction/NativeViewport.h"
#include "app/microscope/MicroscopeTemperatureMode.h"
#include "app/microscope/NativeMicroscopeRuntime.h"
#include "app/microscope/render/MicroscopeRenderer.h"
#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "formats/NativeSubstrateLoader.h"
#include "render/SimulationWorldGpuRenderer.h"
#include "render/NativeSimulationWorldRenderer.h"
#include "render/NativeRenderAssets.h"
#include "render/NativeRenderTextureLoader.h"
#include "render/backend/gles/GlesRenderSurfaceSession.h"
#include "render/gl/AndroidGles30Api.h"

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
using cellularity::render::SimulationWorldGpuRenderer;
using cellularity::render::PreparedRenderFrame;
using cellularity::render::RenderFrameCaptureMetadata;
using cellularity::render::RenderPresentationState;

constexpr int kFrameWidth = 2560;
constexpr int kFrameHeight = 1390;
constexpr int kDensityDpi = 180;
constexpr int kTextureWidth = 256;
constexpr int kTextureHeight = 256;
constexpr int kIncubationFrameCount = 10;
constexpr int kIncubationTargetUps = 1700;
constexpr int kIncubationRenderFps = 30;
constexpr float kDishFramingScale = 1.0125f;
constexpr std::array<float, 3> kBackgroundColor{0.9f, 0.75f, 0.65f};
constexpr std::size_t kFramePixelCount =
        static_cast<std::size_t>(kFrameWidth)
        * static_cast<std::size_t>(kFrameHeight);

[[nodiscard]] std::string hexadecimal(unsigned int value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << value;
    return output.str();
}

void requireGl(const std::string& operation) {
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        throw std::runtime_error(operation + " failed with GL error "
                + hexadecimal(error));
    }
}

[[nodiscard]] std::vector<std::uint8_t> readFile(
        const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Could not open renderer asset: "
                + path.string());
    }
    const std::streamoff size = input.tellg();
    constexpr std::streamoff kMaximumAssetBytes = 64 * 1024 * 1024;
    if (size < 0 || size > kMaximumAssetBytes) {
        throw std::runtime_error("Renderer asset has an invalid size: "
                + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        throw std::runtime_error("Could not read renderer asset: "
                + path.string());
    }
    return bytes;
}

void writeFile(const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create probe output: "
                + path.string());
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    if (!output) {
        throw std::runtime_error("Could not write probe output: "
                + path.string());
    }
}

class DirectoryRenderAssets final : public NativeRenderAssets {
public:
    explicit DirectoryRenderAssets(std::filesystem::path root)
            : mRoot(std::move(root)) {}

    [[nodiscard]] std::string readText(
            const std::string& path) const override {
        const auto bytes = readBytes(path);
        return std::string(bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::vector<unsigned char> readBytes(
            const std::string& path) const override {
        return readFile(mRoot / path);
    }

private:
    std::filesystem::path mRoot;
};

class RawTextureLoader final : public NativeRenderTextureLoader {
public:
    explicit RawTextureLoader(std::filesystem::path root)
            : mRoot(std::move(root)) {}

    NativeRenderTexturePayload loadTexture(
            const std::string& name, bool clampToEdge) override {
        auto pixels = readFile(mRoot / (name + ".rgba8"));
        constexpr std::size_t kExpectedBytes =
                static_cast<std::size_t>(kTextureWidth)
                * static_cast<std::size_t>(kTextureHeight) * 4U;
        if (pixels.size() != kExpectedBytes) {
            throw std::runtime_error("Unexpected raw texture size for " + name);
        }

        NativeRenderTexturePayload payload;
        payload.width = kTextureWidth;
        payload.height = kTextureHeight;
        payload.wrapS = clampToEdge
                ? NativeRenderTextureWrap::ClampToEdge
                : NativeRenderTextureWrap::Repeat;
        payload.wrapT = payload.wrapS;
        payload.pixels = std::move(pixels);
        return payload;
    }

private:
    std::filesystem::path mRoot;
};

struct GlCommandCounters final {
    std::uint64_t bufferDataCalls = 0;
    std::uint64_t bufferDataBytes = 0;
    std::uint64_t bufferSubDataCalls = 0;
    std::uint64_t uploadedPayloadBytes = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t programBindCalls = 0;
    std::uint64_t drawProgramSwitches = 0;
};

class DiagnosticGlesApi final : public AndroidGles30Api {
public:
    void resetCommandCounters() noexcept {
        mCounters = {};
        mLastDrawProgram = 0;
    }

    [[nodiscard]] const GlCommandCounters& commandCounters() const noexcept {
        return mCounters;
    }

    void useProgram(int program) override {
        AndroidGles30Api::useProgram(program);
        mCurrentProgram = program;
        if (program != 0) {
            ++mCounters.programBindCalls;
        }
    }

    void bufferData(int target, int sizeBytes, const void* data,
            int usage) override {
        ++mCounters.bufferDataCalls;
        if (sizeBytes > 0) {
            mCounters.bufferDataBytes += static_cast<std::uint64_t>(sizeBytes);
        }
        AndroidGles30Api::bufferData(target, sizeBytes, data, usage);
    }

    void bufferSubData(int target, int offsetBytes, int sizeBytes,
            const void* data) override {
        ++mCounters.bufferSubDataCalls;
        if (sizeBytes > 0) {
            mCounters.uploadedPayloadBytes +=
                    static_cast<std::uint64_t>(sizeBytes);
        }
        AndroidGles30Api::bufferSubData(
                target, offsetBytes, sizeBytes, data);
    }

    void drawArrays(int mode, int first, int count) override {
        recordDraw();
        AndroidGles30Api::drawArrays(mode, first, count);
    }

    void drawArraysInstanced(
            int mode, int first, int count, int instanceCount) override {
        recordDraw();
        AndroidGles30Api::drawArraysInstanced(
                mode, first, count, instanceCount);
    }

    void drawElementsOffset(
            int mode, int count, int type, int offsetBytes) override {
        recordDraw();
        AndroidGles30Api::drawElementsOffset(
                mode, count, type, offsetBytes);
    }

private:
    void recordDraw() noexcept {
        ++mCounters.drawCalls;
        if (mCurrentProgram != mLastDrawProgram) {
            ++mCounters.drawProgramSwitches;
            mLastDrawProgram = mCurrentProgram;
        }
    }

    GlCommandCounters mCounters{};
    int mCurrentProgram = 0;
    int mLastDrawProgram = 0;
};

class EglPbufferContext final {
public:
    EglPbufferContext() {
        mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (mDisplay == EGL_NO_DISPLAY) {
            throw std::runtime_error("eglGetDisplay returned EGL_NO_DISPLAY");
        }
        if (eglInitialize(mDisplay, &mMajor, &mMinor) != EGL_TRUE) {
            throw std::runtime_error("eglInitialize failed with "
                    + hexadecimal(eglGetError()));
        }

        constexpr EGLint kConfigAttributes[] = {
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 16,
                EGL_NONE
        };
        EGLint configCount = 0;
        if (eglChooseConfig(mDisplay, kConfigAttributes, &mConfig, 1,
                    &configCount)
                        != EGL_TRUE
                || configCount != 1) {
            throw std::runtime_error(
                    "No RGBA8/depth16 GLES 3 pbuffer config is available");
        }
        if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
            throw std::runtime_error("eglBindAPI failed");
        }

        constexpr EGLint kContextAttributes[] = {
                EGL_CONTEXT_CLIENT_VERSION, 3,
                EGL_NONE
        };
        mContext = eglCreateContext(
                mDisplay, mConfig, EGL_NO_CONTEXT, kContextAttributes);
        if (mContext == EGL_NO_CONTEXT) {
            throw std::runtime_error("Could not create a GLES 3 context");
        }

        constexpr EGLint kSurfaceAttributes[] = {
                EGL_WIDTH, 16,
                EGL_HEIGHT, 16,
                EGL_NONE
        };
        mSurface = eglCreatePbufferSurface(
                mDisplay, mConfig, kSurfaceAttributes);
        if (mSurface == EGL_NO_SURFACE) {
            throw std::runtime_error("Could not create an EGL pbuffer");
        }
        if (eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)
                != EGL_TRUE) {
            throw std::runtime_error("eglMakeCurrent failed");
        }
    }

    ~EglPbufferContext() {
        if (mDisplay == EGL_NO_DISPLAY) {
            return;
        }
        eglMakeCurrent(
                mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mSurface != EGL_NO_SURFACE) {
            eglDestroySurface(mDisplay, mSurface);
        }
        if (mContext != EGL_NO_CONTEXT) {
            eglDestroyContext(mDisplay, mContext);
        }
        eglTerminate(mDisplay);
    }

    [[nodiscard]] int major() const noexcept {
        return mMajor;
    }

    [[nodiscard]] int minor() const noexcept {
        return mMinor;
    }

private:
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLConfig mConfig = nullptr;
    EGLContext mContext = EGL_NO_CONTEXT;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLint mMajor = 0;
    EGLint mMinor = 0;
};

[[nodiscard]] GLuint compileShader(
        GLenum type, const char* source, const std::string& label) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) {
        return shader;
    }
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Could not compile " + label + ": " + log);
}

[[nodiscard]] GLuint createDepthEncodingProgram() {
    constexpr const char* kVertexSource = R"GLSL(#version 300 es
precision highp float;
const vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);
void main() {
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)GLSL";
    constexpr const char* kFragmentSource = R"GLSL(#version 300 es
precision highp float;
precision highp int;
uniform highp sampler2D uDepth;
layout(location = 0) out highp vec4 encodedDepth;
void main() {
    highp float depth = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r;
    highp uint value = uint(floor(depth * 65535.0 + 0.5));
    encodedDepth = vec4(
        float(value & 255u) / 255.0,
        float((value >> 8u) & 255u) / 255.0,
        0.0,
        1.0
    );
}
)GLSL";

    const GLuint vertex =
            compileShader(GL_VERTEX_SHADER, kVertexSource, "depth vertex shader");
    const GLuint fragment = compileShader(
            GL_FRAGMENT_SHADER, kFragmentSource, "depth fragment shader");
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
        return program;
    }
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(1, length)), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("Could not link depth encoder: " + log);
}

class OffscreenTarget final {
public:
    OffscreenTarget() {
        GLint maximumTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
        requireGl("GL_MAX_TEXTURE_SIZE query");
        if (kFrameWidth > maximumTextureSize
                || kFrameHeight > maximumTextureSize) {
            throw std::runtime_error("Requested framebuffer exceeds GL_MAX_TEXTURE_SIZE");
        }

        glGenTextures(1, &mColorTexture);
        configureTexture(mColorTexture, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        glGenTextures(1, &mDepthTexture);
        configureTexture(mDepthTexture, GL_DEPTH_COMPONENT16,
                GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT);

        glGenFramebuffers(1, &mFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, mColorTexture, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D, mDepthTexture, 0);
        requireComplete("scene framebuffer");

        glGenTextures(1, &mEncodedDepthTexture);
        configureTexture(mEncodedDepthTexture, GL_RGBA8, GL_RGBA,
                GL_UNSIGNED_BYTE);
        glGenFramebuffers(1, &mEncodedDepthFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, mEncodedDepthFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, mEncodedDepthTexture, 0);
        requireComplete("depth encoding framebuffer");

        mDepthEncodingProgram = createDepthEncodingProgram();
        glGenVertexArrays(1, &mDepthEncodingVertexArray);
        requireGl("offscreen target creation");
    }

    ~OffscreenTarget() {
        if (mDepthEncodingVertexArray != 0) {
            glDeleteVertexArrays(1, &mDepthEncodingVertexArray);
        }
        if (mDepthEncodingProgram != 0) {
            glDeleteProgram(mDepthEncodingProgram);
        }
        if (mEncodedDepthFramebuffer != 0) {
            glDeleteFramebuffers(1, &mEncodedDepthFramebuffer);
        }
        if (mFramebuffer != 0) {
            glDeleteFramebuffers(1, &mFramebuffer);
        }
        const std::array<GLuint, 3> textures{
                mColorTexture, mDepthTexture, mEncodedDepthTexture
        };
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
    }

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    }

    [[nodiscard]] std::vector<std::uint8_t> readColor() const {
        bind();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        std::vector<std::uint8_t> pixels(kFramePixelCount * 4U);
        glFinish();
        glReadPixels(0, 0, kFrameWidth, kFrameHeight, GL_RGBA,
                GL_UNSIGNED_BYTE, pixels.data());
        requireGl("RGBA8 readback");
        return pixels;
    }

    [[nodiscard]] std::vector<std::uint8_t> readDepth16LittleEndian() const {
        const GLboolean ditherWasEnabled = glIsEnabled(GL_DITHER);
        glBindFramebuffer(GL_FRAMEBUFFER, mEncodedDepthFramebuffer);
        glViewport(0, 0, kFrameWidth, kFrameHeight);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_DITHER);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(mDepthEncodingProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mDepthTexture);
        glUniform1i(glGetUniformLocation(mDepthEncodingProgram, "uDepth"), 0);
        glBindVertexArray(mDepthEncodingVertexArray);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        std::vector<std::uint8_t> rgba(kFramePixelCount * 4U);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glFinish();
        glReadPixels(0, 0, kFrameWidth, kFrameHeight, GL_RGBA,
                GL_UNSIGNED_BYTE, rgba.data());
        requireGl("depth16 encoding readback");

        std::vector<std::uint8_t> depth(kFramePixelCount * 2U);
        for (std::size_t pixel = 0; pixel < kFramePixelCount; ++pixel) {
            depth[pixel * 2U] = rgba[pixel * 4U];
            depth[pixel * 2U + 1U] = rgba[pixel * 4U + 1U];
        }
        glBindVertexArray(0);
        glUseProgram(0);
        if (ditherWasEnabled == GL_TRUE) {
            glEnable(GL_DITHER);
        }
        return depth;
    }

private:
    static void configureTexture(GLuint texture, GLint internalFormat,
            GLenum format, GLenum type) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, kFrameWidth,
                kFrameHeight, 0, format, type, nullptr);
    }

    static void requireComplete(const std::string& name) {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)
                != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Incomplete " + name);
        }
    }

    GLuint mColorTexture = 0;
    GLuint mDepthTexture = 0;
    GLuint mEncodedDepthTexture = 0;
    GLuint mFramebuffer = 0;
    GLuint mEncodedDepthFramebuffer = 0;
    GLuint mDepthEncodingProgram = 0;
    GLuint mDepthEncodingVertexArray = 0;
};

[[nodiscard]] std::string glString(GLenum name) {
    const auto* value = glGetString(name);
    return value == nullptr
            ? std::string{}
            : reinterpret_cast<const char*>(value);
}

[[nodiscard]] std::string frameName(int frameIndex, int updates) {
    std::ostringstream output;
    output << "frame-" << std::setfill('0') << std::setw(2) << frameIndex
           << "-update-" << std::setw(6) << updates;
    return output.str();
}

[[nodiscard]] int deadCellCount(const SimulationWorld& world) {
    int count = 0;
    for (int cell = 0; cell < world.mCellData.mCellCount; ++cell) {
        if (world.mCellData.mIsDead[cell]) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] Json commandCountersJson(const GlCommandCounters& counters) {
    return {
            {"bufferDataCalls", counters.bufferDataCalls},
            {"bufferDataBytes", counters.bufferDataBytes},
            {"bufferSubDataCalls", counters.bufferSubDataCalls},
            {"uploadedPayloadBytes", counters.uploadedPayloadBytes},
            {"drawCalls", counters.drawCalls},
            {"programBindCalls", counters.programBindCalls},
            {"drawProgramSwitches", counters.drawProgramSwitches}
    };
}

[[nodiscard]] double percentile(
        const std::vector<double>& sortedValues, double quantile) {
    if (sortedValues.empty()) {
        return 0.0;
    }
    const double position = quantile
            * static_cast<double>(sortedValues.size() - 1U);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return sortedValues[lower]
            + (sortedValues[upper] - sortedValues[lower]) * fraction;
}

[[nodiscard]] Json timingSummary(std::vector<double> samplesMicroseconds) {
    std::sort(samplesMicroseconds.begin(), samplesMicroseconds.end());
    double sum = 0.0;
    for (const double sample : samplesMicroseconds) {
        sum += sample;
    }
    return {
            {"samples", samplesMicroseconds.size()},
            {"unit", "microseconds"},
            {"p50", percentile(samplesMicroseconds, 0.50)},
            {"p95", percentile(samplesMicroseconds, 0.95)},
            {"p99", percentile(samplesMicroseconds, 0.99)},
            {"mean", samplesMicroseconds.empty() ? 0.0 : sum / static_cast<double>(samplesMicroseconds.size())}
    };
}

[[nodiscard]] RenderFrameCaptureMetadata captureMetadata(
        const NativeViewport& viewport,
        const NativeSimulationWorldRenderer::ViewBounds& bounds,
        std::uint64_t completedUpdates) {
    RenderFrameCaptureMetadata metadata;
    metadata.worldEpoch = 1;
    metadata.modeGeneration = 1;
    metadata.completedUpdateId = completedUpdates;
    metadata.stage = cellularity::render::RenderCaptureStage::StableWorld;
    metadata.viewport = cellularity::app::snapshotViewport(viewport, bounds);
    return metadata;
}

[[nodiscard]] RenderPresentationState presentation(
        const NativeViewport& viewport,
        const NativeSimulationWorldRenderer::ViewBounds& bounds) {
    RenderPresentationState state;
    state.viewport = cellularity::app::snapshotViewport(viewport, bounds);
    state.backgroundColor = kBackgroundColor;
    return state;
}

[[nodiscard]] NativeSimulationWorldRenderer::ViewBounds viewBounds(
        const NativeViewport& viewport) {
    const auto bounds = viewport.viewBounds(0.0f);
    return {bounds.minX, bounds.maxX, bounds.minY, bounds.maxY};
}

[[nodiscard]] Json frameDiagnostics(const SimulationWorld& world,
        const PreparedRenderFrame& frame, int index, int updates,
        const std::string& name, const GlCommandCounters& counters) {
    const int deadCells = deadCellCount(world);
    return {
            {"index", index},
            {"name", name},
            {"cumulativeUpdates", updates},
            {"worldTime", world.mTime},
            {"cellCount", world.mCellData.mCellCount},
            {"aliveCellCount", world.mCellData.mCellCount - deadCells},
            {"deadCellCount", deadCells},
            {"foodCount", world.mFoodData.mFoodCount},
            {"deadSplashCount", world.mDeadSplashData.mDeadSplashCount},
            {"topologyEdgeCount", world.mCellData.mTopology.edgeCount()},
            {"topologyBoundaryCount", world.mCellData.mTopology.boundaryCount()},
            {"visibleCellCount", frame.diagnostics.visibleCellCount},
            {"cellDrawVertexCount", frame.cellDrawVertexCount},
            {"cellDrawIndexCount", frame.cellDrawIndexCount},
            {"foodInstanceCount", frame.foodInstanceCount},
            {"deadCellInstanceCount", frame.deadCellInstanceCount},
            {"drawLinkVertexCount", frame.drawLinksCount},
            {"commands", commandCountersJson(counters)},
            {"colorRaw", "raw/" + name + ".rgba8"},
            {"depthRaw", "raw/" + name + ".depth16le"}
    };
}

template <typename Prepare>
[[nodiscard]] Json benchmarkDraws(int samples, OffscreenTarget& target,
        DiagnosticGlesApi& gl, MicroscopeRenderer& microscope,
        NativeRenderFacade& facade, SimulationWorldGpuRenderer& gpuRenderer,
        Prepare&& prepare) {
    std::vector<double> durations;
    durations.reserve(static_cast<std::size_t>(samples));
    GlCommandCounters representative{};
    for (int sample = 0; sample < samples; ++sample) {
        const auto prepared = prepare(sample);
        target.bind();
        glFinish();
        gl.resetCommandCounters();
        const auto startedAt = Clock::now();
        if (!microscope.drawPreparedFrame(prepared.presentation,
                    *prepared.frame, gpuRenderer, facade, gl)) {
            throw std::runtime_error("Production microscope draw was rejected");
        }
        const auto stoppedAt = Clock::now();
        if (sample == 0) {
            representative = gl.commandCounters();
        }
        glFinish();
        durations.push_back(
                std::chrono::duration<double, std::micro>(stoppedAt - startedAt)
                        .count());
    }
    return {
            {"cpuSubmissionTime", timingSummary(std::move(durations))},
            {"representativeCommands", commandCountersJson(representative)},
            {"queuePolicy", "glFinish-before-sample; completion outside timed interval"}
    };
}

struct PreparedDraw final {
    const PreparedRenderFrame* frame = nullptr;
    RenderPresentationState presentation{};
};

[[nodiscard]] Json benchmarkRuntimeThroughput(
        const std::filesystem::path& substratePath,
        const std::filesystem::path& assetRoot,
        DiagnosticGlesApi& gl,
        RawTextureLoader& textureLoader,
        OffscreenTarget& target,
        cellularity::microscope::MicroscopeTemperatureMode mode,
        bool renderFrames) {
    Environment fallback =
            Environment::constructDefaultEnvironment(1000, 1000, 100.0);
    NativeSubstrateLoadResult loaded = loadNativeSubstrateFromPath(
            substratePath.string(), false, fallback);
    if (loaded.world == nullptr) {
        throw std::runtime_error(
                "Runtime throughput fixture loader returned no world");
    }

    const float dishRadius =
            static_cast<float>(loaded.world->mEnvironment.mRadius);
    if (!std::isfinite(dishRadius) || dishRadius <= 0.0f) {
        throw std::runtime_error(
                "Runtime throughput fixture has an invalid dish radius");
    }

    NativeCellularityController controller(std::move(loaded.world));
    controller.installRenderSurfaceSession(
            std::make_unique<cellularity::render::gles::GlesRenderSurfaceSession>(
                    gl));
    const float framingRadius = dishRadius * kDishFramingScale;
    controller.setViewportSurfaceSize(kFrameWidth, kFrameHeight);
    controller.setViewportCamera(0.0f, 0.0f, 1.0f / framingRadius);
    controller.setMicroscopeTemperatureMode(
            cellularity::microscope::temperatureModeOrdinal(mode));
    DirectoryRenderAssets assets(assetRoot);
    if (renderFrames) {
        controller.loadRendererResources(assets, textureLoader);
    }
    controller.startMicroscopeRuntime();

    constexpr auto kWarmup = std::chrono::milliseconds(750);
    constexpr auto kMeasurement = std::chrono::milliseconds(1500);
    constexpr auto kPresentationInterval =
            std::chrono::nanoseconds(16'666'667);
    std::uint64_t drawAttempts = 0;
    const auto runWindow = [&](Clock::duration duration) {
        const auto deadline = Clock::now() + duration;
        auto nextPresentation = Clock::now();
        while (Clock::now() < deadline) {
            if (!renderFrames) {
                std::this_thread::sleep_until(deadline);
                break;
            }
            target.bind();
            controller.drawMicroscope();
            glFinish();
            ++drawAttempts;
            nextPresentation += kPresentationInterval;
            std::this_thread::sleep_until(
                    std::min(nextPresentation, deadline));
        }
    };

    runWindow(kWarmup);
    const auto beforeSnapshot = controller.microscopeRuntimeSnapshot();
    const auto startedAt = Clock::now();
    const std::uint64_t drawsBefore = drawAttempts;
    runWindow(kMeasurement);
    const auto stoppedAt = Clock::now();
    const auto snapshot = controller.microscopeRuntimeSnapshot();
    controller.stopMicroscopeRuntime();
    if (renderFrames) {
        controller.releaseRendererResources();
    }
    if (snapshot.failed
            || snapshot.completedUpdateCount
                    < beforeSnapshot.completedUpdateCount
            || !snapshot.rateTelemetry.actualRateValid) {
        throw std::runtime_error("Runtime throughput probe failed");
    }

    const double elapsedSeconds =
            std::chrono::duration<double>(stoppedAt - startedAt).count();
    const std::uint64_t completedUpdates = snapshot.completedUpdateCount
            - beforeSnapshot.completedUpdateCount;
    return {
            {"temperatureMode",
                    cellularity::microscope::temperatureModeOrdinal(mode)},
            {"renderFrames", renderFrames},
            {"measurementSeconds", elapsedSeconds},
            {"completedUpdates", completedUpdates},
            {"snapshotDeltaUps",
                    elapsedSeconds > 0.0
                            ? static_cast<double>(completedUpdates)
                                    / elapsedSeconds
                            : 0.0},
            {"measuredUps",
                    snapshot.rateTelemetry.actualCompletedUps},
            {"rateWindowSeconds",
                    std::chrono::duration<double>(
                            snapshot.rateTelemetry.actualRateWindow)
                            .count()},
            {"drawAttempts", drawAttempts - drawsBefore},
            {"publishedSourceFrames",
                    snapshot.rateTelemetry.publishedSourceFrameCount},
            {"publishedPreparedFrames",
                    snapshot.rateTelemetry.publishedPreparedFrameCount},
            {"scope",
                    "local host scheduler and GLES driver; not a universal device constant"}
    };
}

[[nodiscard]] Json runProbe(const std::filesystem::path& assetRoot,
        const std::filesystem::path& rawTextureRoot,
        const std::filesystem::path& substratePath,
        const std::filesystem::path& outputDirectory, int samples) {
    std::filesystem::create_directories(outputDirectory / "raw");

    EglPbufferContext context;
    DiagnosticGlesApi gl;
    DirectoryRenderAssets assets(assetRoot);
    RawTextureLoader textureLoader(rawTextureRoot);
    OffscreenTarget target;
    NativeRenderFacade facade;
    SimulationWorldGpuRenderer gpuRenderer;
    MicroscopeRenderer microscope;
    facade.loadResources(gpuRenderer, false, assets, gl, textureLoader);

    Environment fallback =
            Environment::constructDefaultEnvironment(1000, 1000, 100.0);
    NativeSubstrateLoadResult loaded = loadNativeSubstrateFromPath(
            substratePath.string(), false, fallback);
    if (loaded.world == nullptr) {
        throw std::runtime_error("Substrate loader returned no world");
    }
    SimulationWorld& world = *loaded.world;
    const float dishRadius = static_cast<float>(world.mEnvironment.mRadius);
    if (!std::isfinite(dishRadius) || dishRadius <= 0.0f) {
        throw std::runtime_error("Loaded substrate has an invalid dish radius");
    }

    const float framingRadius = dishRadius * kDishFramingScale;
    NativeViewport viewport(framingRadius);
    viewport.setCameraBoundsEnabled(true);
    viewport.setSurfaceSize(kFrameWidth, kFrameHeight);
    viewport.setCamera(0.0f, 0.0f, 1.0f / framingRadius);
    if (!viewport.isReady()) {
        throw std::runtime_error("Could not configure the full-dish viewport");
    }
    const auto bounds = viewBounds(viewport);
    NativeSimulationWorldRenderer frameBuilder(world.mCellData.MAX_CELL_COUNT);

    Json frames = Json::array();
    int completedUpdates = 0;
    for (int frameIndex = 0; frameIndex < kIncubationFrameCount; ++frameIndex) {
        const int targetUpdates =
                frameIndex * kIncubationTargetUps / kIncubationRenderFps;
        while (completedUpdates < targetUpdates) {
            world.update(false, 1.0);
            ++completedUpdates;
        }

        frameBuilder.copyToDraw(world, bounds,
                captureMetadata(viewport, bounds,
                        static_cast<std::uint64_t>(completedUpdates)));
        const auto framePresentation = presentation(viewport, bounds);
        const auto& frame =
                frameBuilder.publishPreparedFrame(framePresentation);
        target.bind();
        gl.resetCommandCounters();
        if (!microscope.drawPreparedFrame(framePresentation, frame, gpuRenderer,
                    facade, gl)) {
            throw std::runtime_error("Production microscope draw was rejected");
        }
        requireGl("incubation frame draw");

        const std::string name = frameName(frameIndex, targetUpdates);
        writeFile(outputDirectory / "raw" / (name + ".rgba8"),
                target.readColor());
        writeFile(outputDirectory / "raw" / (name + ".depth16le"),
                target.readDepth16LittleEndian());
        frames.push_back(frameDiagnostics(world, frame, frameIndex,
                targetUpdates, name, gl.commandCounters()));
    }

    frameBuilder.copyToDraw(world, bounds,
            captureMetadata(viewport, bounds,
                    static_cast<std::uint64_t>(completedUpdates)));
    const auto fixedPresentation = presentation(viewport, bounds);
    const auto& fixedFrame =
            frameBuilder.publishPreparedFrame(fixedPresentation);
    target.bind();
    if (!microscope.drawPreparedFrame(fixedPresentation, fixedFrame,
                gpuRenderer, facade, gl)) {
        throw std::runtime_error("Could not prime the fixed prepared frame");
    }
    glFinish();

    const Json idle = benchmarkDraws(samples, target, gl, microscope, facade,
            gpuRenderer, [&](int) {
                return PreparedDraw{&fixedFrame, fixedPresentation};
            });

    const Json moving = benchmarkDraws(samples, target, gl, microscope, facade,
            gpuRenderer, [&](int sample) {
                RenderPresentationState movingPresentation = fixedPresentation;
                const float shift = static_cast<float>((sample % 31) - 15)
                        * 0.0005f;
                movingPresentation.viewport.cameraX = shift;
                movingPresentation.viewport.mvpMatrix[12] = shift;
                ++movingPresentation.viewport.revision;
                return PreparedDraw{&fixedFrame, movingPresentation};
            });

    const Json changed = benchmarkDraws(samples, target, gl, microscope, facade,
            gpuRenderer, [&](int sample) {
                frameBuilder.copyToDraw(world, bounds,
                        captureMetadata(viewport, bounds,
                                static_cast<std::uint64_t>(completedUpdates + sample + 1)));
                return PreparedDraw{
                        &frameBuilder.publishPreparedFrame(fixedPresentation),
                        fixedPresentation
                };
            });

    facade.releaseResources(gpuRenderer, gl);
    requireGl("renderer resource release");

    const Json observeWithRendering = benchmarkRuntimeThroughput(substratePath,
            assetRoot, gl, textureLoader, target,
            cellularity::microscope::MicroscopeTemperatureMode::Observe, true);
    const Json observeWithoutRendering = benchmarkRuntimeThroughput(substratePath,
            assetRoot, gl, textureLoader, target,
            cellularity::microscope::MicroscopeTemperatureMode::Observe, false);
    const Json incubateWithRendering = benchmarkRuntimeThroughput(substratePath,
            assetRoot, gl, textureLoader, target,
            cellularity::microscope::MicroscopeTemperatureMode::Incubate, true);
    const Json incubateWithoutRendering = benchmarkRuntimeThroughput(substratePath,
            assetRoot, gl, textureLoader, target,
            cellularity::microscope::MicroscopeTemperatureMode::IncubateNoRender,
            false);
    requireGl("runtime throughput diagnostics");

    return {
            {"schemaVersion", 1},
            {"format", "cellularity-host-gles-baseline-probe-v1"},
            {"backend", "OpenGL ES"},
            {"eglVersion", std::to_string(context.major()) + "." + std::to_string(context.minor())},
            {"glVendor", glString(GL_VENDOR)},
            {"glRenderer", glString(GL_RENDERER)},
            {"glVersion", glString(GL_VERSION)},
            {"glslVersion", glString(GL_SHADING_LANGUAGE_VERSION)},
            {"frame", {{"width", kFrameWidth}, {"height", kFrameHeight}, {"densityDpi", kDensityDpi}, {"color", "RGBA8"}, {"depth", "DEPTH_COMPONENT16"}, {"rawOrigin", "bottom-left"}}},
            {"input", {{"path", substratePath.string()}, {"formatFamily", static_cast<int>(loaded.formatFamily)}, {"familyLocalVersion", loaded.familyLocalVersion}}},
            {"framing", {{"dishRadius", world.mEnvironment.mRadius}, {"framingScale", kDishFramingScale}, {"cameraZoom", viewport.zoom()}, {"viewWidth", viewport.viewWidth()}, {"viewHeight", viewport.viewHeight()}, {"outerWallVisible", true}}},
            {"incubation", {{"targetUpdatesPerSecond", kIncubationTargetUps}, {"nominalRenderFramesPerSecond", kIncubationRenderFps}, {"frameCount", kIncubationFrameCount}, {"updateCall", "SimulationWorld::update(false, 1.0)"}}},
            {"frames", std::move(frames)},
            {"diagnostics", {{"scope", "local host GLES driver; not a universal device constant"}, {"idlePresentation", idle}, {"movingCameraPresentation", moving}, {"changedPreparedFrame", changed}, {"runtimeThroughput", {{"observeWithRendering", observeWithRendering}, {"observeWithoutRendering", observeWithoutRendering}, {"incubateWithRendering", incubateWithRendering}, {"incubateWithoutRendering", incubateWithoutRendering}}}}}
    };
}

void printUsage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <asset-root> <raw-texture-root> <substrate> <output-directory>"
                 " [timing-samples=120]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 5 || argc > 6) {
        printUsage(argv[0]);
        return 2;
    }
    try {
        const int samples = argc == 6 ? std::stoi(argv[5]) : 120;
        if (samples < 1 || samples > 10'000) {
            throw std::invalid_argument("Timing sample count must be 1..10000");
        }
        const std::filesystem::path outputDirectory(argv[4]);
        Json report = runProbe(argv[1], argv[2], argv[3], outputDirectory,
                samples);
        std::ofstream output(outputDirectory / "probe.json",
                std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Could not create probe.json");
        }
        output << std::setw(2) << report << '\n';
        if (!output) {
            throw std::runtime_error("Could not write probe.json");
        }
        std::cout << "GLES baseline probe completed: "
                  << (outputDirectory / "probe.json") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GLES baseline probe failed: " << error.what() << '\n';
        return 1;
    }
}
