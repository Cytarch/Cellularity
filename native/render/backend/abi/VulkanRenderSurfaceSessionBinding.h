#pragma once

#include <memory>

#include "app/render/RenderSurfaceSessionBinding.h"

namespace cellularity::render::abi {

class VulkanBackendSession;

class VulkanRenderSurfaceSessionBinding final
        : public RenderSurfaceSessionBinding {
public:
    explicit VulkanRenderSurfaceSessionBinding(
            std::shared_ptr<VulkanBackendSession> session);

    [[nodiscard]] RenderBackendKind kind() const noexcept override;
    [[nodiscard]] const void* identity() const noexcept override;
    [[nodiscard]] std::unique_ptr<RenderSurfaceSession>
    createControllerAdapter() const noexcept override;

private:
    std::shared_ptr<VulkanBackendSession> mSession;
};

} // namespace cellularity::render::abi
