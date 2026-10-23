#pragma once

#include <android/native_window.h>

#include <string>

namespace cellularity::android::vulkan {

/**
 * Runs a synchronous, read-only Vulkan capability probe against [window].
 * Every temporary Vulkan object is destroyed before this function returns.
 */
[[nodiscard]] std::string probeCapabilities(ANativeWindow* window);

} // namespace cellularity::android::vulkan
