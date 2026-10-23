#pragma once

#include <cstddef>
#include <cstdint>

namespace cellularity::app::render_worker {

inline constexpr std::uint32_t kNativeRenderProfilingMagic = 0x31505256U;
inline constexpr std::uint16_t kNativeRenderProfilingVersion = 1;
inline constexpr std::size_t kNativeRenderProfilingSize = 184;

enum class ProfilingConfigurationPublishResult : std::uint8_t {
    Applied = 0,
    Duplicate = 1,
    InvalidGeneration = 2,
    Unavailable = 3,
};

enum class ProfilingSnapshotReadResult : std::uint8_t {
    Ready = 0,
    InvalidBuffer = 1,
    Unavailable = 2,
};

} // namespace cellularity::app::render_worker
