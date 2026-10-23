#pragma once

#include <cstdint>

namespace cellularity::render {

enum class RenderBackendKind : std::uint8_t {
    OpenGlEs = 0,
    Vulkan = 1
};

} // namespace cellularity::render
