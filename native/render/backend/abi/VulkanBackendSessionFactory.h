#pragma once

#include <memory>

#include "render/backend/abi/CellularityVulkanBackendAbi.h"

namespace cellularity::render::abi {

class VulkanBackendSession;

struct VulkanBackendSessionCreateResult final {
    std::shared_ptr<VulkanBackendSession> session;
    CellularityVulkanErrorV1 error{};

    [[nodiscard]] bool succeeded() const noexcept {
        return session != nullptr
                && error.code == CELLULARITY_VULKAN_ERROR_NONE_V1;
    }
};

class VulkanBackendSessionFactory {
public:
    virtual ~VulkanBackendSessionFactory() = default;

    [[nodiscard]] virtual VulkanBackendSessionCreateResult
    createOnOwnerThread() noexcept = 0;
};

} // namespace cellularity::render::abi
