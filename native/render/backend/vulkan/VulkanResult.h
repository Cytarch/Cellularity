#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace cellularity::render::vulkan {

[[nodiscard]] const char* resultName(VkResult result) noexcept;

[[nodiscard]] std::string resultDescription(
        const char* operation, VkResult result);

[[nodiscard]] std::string versionString(std::uint32_t version);

[[nodiscard]] const char* formatName(VkFormat format) noexcept;

} // namespace cellularity::render::vulkan
