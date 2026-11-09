#include "host/win32/Win32HostCamera.h"

#include "app/interaction/NativeViewport.h"

namespace cellularity::host::win32 {

std::array<float, 16> projectionMatrixFor(const NativeViewport& viewport) {
    const float halfWidth = viewport.viewWidth() * 0.5F;
    const float halfHeight = viewport.viewHeight() * 0.5F;
    if (!(halfWidth > 0.0F) || !(halfHeight > 0.0F)) {
        return {1.0F, 0.0F, 0.0F, 0.0F,
                0.0F, 1.0F, 0.0F, 0.0F,
                0.0F, 0.0F, 1.0F, 0.0F,
                0.0F, 0.0F, 0.0F, 1.0F};
    }
    return {
            1.0F / halfWidth, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F / halfHeight, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            -viewport.cameraX() / halfWidth, -viewport.cameraY() / halfHeight,
            0.0F, 1.0F};
}

} // namespace cellularity::host::win32
