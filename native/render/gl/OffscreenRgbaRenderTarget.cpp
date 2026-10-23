#include "render/gl/OffscreenRgbaRenderTarget.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include "render/gl/NativeGlApi.h"

namespace cellularity::render {
namespace {

constexpr std::size_t kRgbaChannels = 4;

[[nodiscard]] std::size_t checkedPixelBytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument(
                "Off-screen render target dimensions must be positive");
    }
    const auto unsignedWidth = static_cast<std::size_t>(width);
    const auto unsignedHeight = static_cast<std::size_t>(height);
    if (unsignedWidth > std::numeric_limits<std::size_t>::max() / unsignedHeight / kRgbaChannels) {
        throw std::length_error("Off-screen RGBA target exceeds address space");
    }
    return unsignedWidth * unsignedHeight * kRgbaChannels;
}

} // namespace

OffscreenRgbaRenderTarget::PreviousState
OffscreenRgbaRenderTarget::bind(NativeGlApi& gl, int width, int height) {
    PreviousState state;
    gl.getIntegers(gl.framebufferBinding(), &state.framebuffer);
    gl.getIntegers(gl.viewportState(), state.viewport.data());
    gl.getIntegers(gl.packAlignment(), &state.packAlignment);

    try {
        ensureStorage(gl, width, height);
        gl.bindFramebuffer(gl.framebuffer(), mFramebuffer);
        gl.viewport(0, 0, mWidth, mHeight);
        gl.pixelStore(gl.packAlignment(), 1);
    } catch (...) {
        restore(gl, state);
        throw;
    }
    return state;
}

std::vector<std::uint8_t>
OffscreenRgbaRenderTarget::readTopLeftRgba(NativeGlApi& gl) const {
    if (mFramebuffer == 0 || mWidth <= 0 || mHeight <= 0) {
        throw std::logic_error("Off-screen render target is not initialized");
    }
    const std::size_t byteCount = checkedPixelBytes(mWidth, mHeight);
    std::vector<std::uint8_t> pixels(byteCount);
    gl.readPixels(0, 0, mWidth, mHeight, gl.rgba(), gl.unsignedByte(),
            pixels.data());

    const std::size_t rowBytes =
            static_cast<std::size_t>(mWidth) * kRgbaChannels;
    for (int top = 0, bottom = mHeight - 1; top < bottom; ++top, --bottom) {
        auto topBegin = pixels.begin() + static_cast<std::ptrdiff_t>(top) * static_cast<std::ptrdiff_t>(rowBytes);
        auto bottomBegin = pixels.begin() + static_cast<std::ptrdiff_t>(bottom) * static_cast<std::ptrdiff_t>(rowBytes);
        std::swap_ranges(topBegin, topBegin + static_cast<std::ptrdiff_t>(rowBytes),
                bottomBegin);
    }
    return pixels;
}

void OffscreenRgbaRenderTarget::restore(
        NativeGlApi& gl, const PreviousState& state) const noexcept {
    gl.bindFramebuffer(gl.framebuffer(), state.framebuffer);
    gl.viewport(state.viewport[0], state.viewport[1], state.viewport[2],
            state.viewport[3]);
    gl.pixelStore(gl.packAlignment(), state.packAlignment);
}

void OffscreenRgbaRenderTarget::release(NativeGlApi& gl) noexcept {
    if (mFramebuffer != 0) {
        gl.deleteFramebuffers(1, &mFramebuffer);
    }
    if (mDepthRenderbuffer != 0) {
        gl.deleteRenderbuffers(1, &mDepthRenderbuffer);
    }
    if (mColorTexture != 0) {
        gl.deleteTextures(1, &mColorTexture);
    }
    abandon();
}

void OffscreenRgbaRenderTarget::abandon() noexcept {
    mFramebuffer = 0;
    mColorTexture = 0;
    mDepthRenderbuffer = 0;
    mWidth = 0;
    mHeight = 0;
}

int OffscreenRgbaRenderTarget::width() const noexcept {
    return mWidth;
}

int OffscreenRgbaRenderTarget::height() const noexcept {
    return mHeight;
}

void OffscreenRgbaRenderTarget::ensureStorage(NativeGlApi& gl, int width,
        int height) {
    (void)checkedPixelBytes(width, height);
    if (mFramebuffer != 0 && mWidth == width && mHeight == height) {
        return;
    }
    release(gl);

    gl.genTextures(1, &mColorTexture);
    gl.bindTexture(gl.texture2d(), mColorTexture);
    gl.textureParameter(gl.texture2d(), gl.textureMinFilter(), gl.linear());
    gl.textureParameter(gl.texture2d(), gl.textureMagFilter(), gl.linear());
    gl.textureParameter(gl.texture2d(), gl.textureWrapS(), gl.clampToEdge());
    gl.textureParameter(gl.texture2d(), gl.textureWrapT(), gl.clampToEdge());
    gl.textureImage2d(gl.texture2d(), 0, gl.rgba8(), width, height, 0, gl.rgba(),
            gl.unsignedByte(), nullptr);

    gl.genRenderbuffers(1, &mDepthRenderbuffer);
    gl.bindRenderbuffer(gl.renderbuffer(), mDepthRenderbuffer);
    gl.renderbufferStorage(gl.renderbuffer(), gl.depthComponent16(), width,
            height);

    gl.genFramebuffers(1, &mFramebuffer);
    gl.bindFramebuffer(gl.framebuffer(), mFramebuffer);
    gl.framebufferTexture2d(gl.framebuffer(), gl.colorAttachment0(),
            gl.texture2d(), mColorTexture, 0);
    gl.framebufferRenderbuffer(gl.framebuffer(), gl.depthAttachment(),
            gl.renderbuffer(), mDepthRenderbuffer);
    if (gl.checkFramebufferStatus(gl.framebuffer()) != gl.framebufferComplete()) {
        release(gl);
        throw std::runtime_error("Could not create complete off-screen framebuffer");
    }

    gl.bindTexture(gl.texture2d(), 0);
    gl.bindRenderbuffer(gl.renderbuffer(), 0);
    mWidth = width;
    mHeight = height;
}

} // namespace cellularity::render
