#pragma once

#include <cstdint>

namespace cellularity::microscope {

enum class MicroscopeTemperatureMode : int {
    Freeze = 0,
    Observe = 1,
    Incubate = 2,
    Slow = 3,
    IncubateNoRender = 4,
};

[[nodiscard]] constexpr int
temperatureModeOrdinal(MicroscopeTemperatureMode mode) noexcept {
    return static_cast<int>(mode);
}

inline constexpr double kDisplayPacedSimulationTargetUps = 60.0;
inline constexpr std::int64_t kIncubateCaptureIntervalNanos = 33'333'333LL;

} // namespace cellularity::microscope
