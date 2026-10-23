#pragma once

#include <memory>

#include "render/backend/RenderBackendKind.h"

namespace cellularity::render {

class RenderSurfaceSession;

// Main-DSO-only factory for controller-owned forwarding adapters. The
// binding identity is process-local and is never exposed through JNI or the
// Vulkan backend ABI.
class RenderSurfaceSessionBinding {
public:
    virtual ~RenderSurfaceSessionBinding() = default;

    [[nodiscard]] virtual RenderBackendKind kind() const noexcept = 0;
    [[nodiscard]] virtual const void* identity() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<RenderSurfaceSession>
    createControllerAdapter() const noexcept = 0;
};

} // namespace cellularity::render
