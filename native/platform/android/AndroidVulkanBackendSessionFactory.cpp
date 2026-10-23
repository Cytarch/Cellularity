#include "platform/android/AndroidVulkanBackendSessionFactory.h"

#ifdef __ANDROID__

#include <algorithm>
#include <cstring>
#include <exception>
#include <new>
#include <stdexcept>
#include <utility>

#include "platform/android/AndroidRenderAssetsLease.h"
#include "render/backend/abi/VulkanBackendSession.h"

namespace cellularity::platform::android {
namespace {

void initializeError(CellularityVulkanErrorV1& error,
        CellularityVulkanErrorCodeV1 code,
        const char* message) noexcept {
    error = {};
    error.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    error.struct_size = sizeof(error);
    error.code = code;
    if (message == nullptr) {
        return;
    }
    const std::size_t sourceLength = std::strlen(message);
    const std::size_t copied = std::min(
            sourceLength, sizeof(error.message) - 1U);
    std::memcpy(error.message, message, copied);
    error.message[copied] = '\0';
    error.message_length = static_cast<std::uint32_t>(copied);
    if (copied != sourceLength) {
        error.flags |= CELLULARITY_VULKAN_ERROR_MESSAGE_TRUNCATED_V1;
    }
}

} // namespace

AndroidVulkanBackendSessionFactory::AndroidVulkanBackendSessionFactory(
        const CellularityVulkanSurfaceApiV1* api,
        std::shared_ptr<AndroidRenderAssetsLease> assetsLease) noexcept
        : mApi(api),
          mAssetsLease(std::move(assetsLease)) {
}

cellularity::render::abi::VulkanBackendSessionCreateResult
AndroidVulkanBackendSessionFactory::createOnOwnerThread() noexcept {
    cellularity::render::abi::VulkanBackendSessionCreateResult result;
    initializeError(result.error, CELLULARITY_VULKAN_ERROR_NONE_V1, nullptr);
    try {
        if (mApi == nullptr || mAssetsLease == nullptr
                || mAssetsLease->closed()) {
            initializeError(result.error,
                    CELLULARITY_VULKAN_ERROR_ASSET_UNAVAILABLE_V1,
                    "Vulkan backend assets are unavailable");
            return result;
        }
        const AndroidRenderAssets assets = mAssetsLease->assets();
        result.session = cellularity::render::abi::VulkanBackendSession::create(
                mApi, assets.assetManager());
        return result;
    } catch (const std::bad_alloc&) {
        initializeError(result.error,
                CELLULARITY_VULKAN_ERROR_OUT_OF_MEMORY_V1,
                "Vulkan backend session allocation failed");
    } catch (const std::invalid_argument& failure) {
        initializeError(result.error,
                CELLULARITY_VULKAN_ERROR_INVALID_ARGUMENT_V1,
                failure.what());
    } catch (const std::exception& failure) {
        initializeError(result.error,
                CELLULARITY_VULKAN_ERROR_BACKEND_INITIALIZATION_FAILED_V1,
                failure.what());
    } catch (...) {
        initializeError(result.error,
                CELLULARITY_VULKAN_ERROR_INTERNAL_FAILURE_V1,
                "Unknown Vulkan backend session creation failure");
    }
    result.session.reset();
    return result;
}

} // namespace cellularity::platform::android

#endif
