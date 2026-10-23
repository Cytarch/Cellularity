#pragma once

#ifdef __ANDROID__

#include <memory>

#include "render/backend/abi/VulkanBackendSessionFactory.h"

class AndroidRenderAssetsLease;

namespace cellularity::platform::android {

class AndroidVulkanBackendSessionFactory final
        : public cellularity::render::abi::VulkanBackendSessionFactory {
public:
    AndroidVulkanBackendSessionFactory(
            const CellularityVulkanSurfaceApiV1* api,
            std::shared_ptr<AndroidRenderAssetsLease> assetsLease) noexcept;

    [[nodiscard]] cellularity::render::abi::VulkanBackendSessionCreateResult
    createOnOwnerThread() noexcept override;

private:
    const CellularityVulkanSurfaceApiV1* mApi = nullptr;
    std::shared_ptr<AndroidRenderAssetsLease> mAssetsLease;
};

} // namespace cellularity::platform::android

#endif
