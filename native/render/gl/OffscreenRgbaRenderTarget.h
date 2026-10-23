#pragma once

#include <array>
#include <cstdint>
#include <vector>

class NativeGlApi;

namespace cellularity::render {

// Owns only an off-screen GLES target. Scene preparation and drawing remain in
// the production render pipeline; this class merely redirects its output and
// reads the completed RGBA frame back in top-left row order.
class OffscreenRgbaRenderTarget final {
public:
    struct PreviousState final {
        int framebuffer = 0;
        std::array<int, 4> viewport{};
        int packAlignment = 4;
    };

    OffscreenRgbaRenderTarget() = default;
    ~OffscreenRgbaRenderTarget() = default;

    OffscreenRgbaRenderTarget(const OffscreenRgbaRenderTarget&) = delete;
    OffscreenRgbaRenderTarget&
    operator=(const OffscreenRgbaRenderTarget&) = delete;

    [[nodiscard]] PreviousState bind(NativeGlApi& gl, int width, int height);
    [[nodiscard]] std::vector<std::uint8_t>
    readTopLeftRgba(NativeGlApi& gl) const;
    void restore(NativeGlApi& gl, const PreviousState& state) const noexcept;
    void release(NativeGlApi& gl) noexcept;
    void abandon() noexcept;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

private:
    void ensureStorage(NativeGlApi& gl, int width, int height);

    int mFramebuffer = 0;
    int mColorTexture = 0;
    int mDepthRenderbuffer = 0;
    int mWidth = 0;
    int mHeight = 0;
};

} // namespace cellularity::render
