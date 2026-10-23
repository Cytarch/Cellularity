#include "render/backend/abi/VulkanRenderSurfaceSessionBinding.h"

#include <utility>

#include "render/backend/abi/VulkanBackendSession.h"
#include "render/backend/abi/VulkanRenderSurfaceSessionAdapter.h"

namespace cellularity::render::abi {

VulkanRenderSurfaceSessionBinding::VulkanRenderSurfaceSessionBinding(
        std::shared_ptr<VulkanBackendSession> session)
        : mSession(std::move(session)) {
}

RenderBackendKind VulkanRenderSurfaceSessionBinding::kind() const noexcept {
    return RenderBackendKind::Vulkan;
}

const void* VulkanRenderSurfaceSessionBinding::identity() const noexcept {
    return mSession == nullptr ? nullptr : mSession->bindingIdentity();
}

std::unique_ptr<RenderSurfaceSession>
VulkanRenderSurfaceSessionBinding::createControllerAdapter() const noexcept {
    if (mSession == nullptr || identity() == nullptr) {
        return nullptr;
    }
    try {
        return std::make_unique<VulkanRenderSurfaceSessionAdapter>(mSession);
    } catch (...) {
        return nullptr;
    }
}

} // namespace cellularity::render::abi
