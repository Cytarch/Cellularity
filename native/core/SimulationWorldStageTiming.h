#pragma once

#include <array>
#include <cstdint>

#include "core/SimulationWorldProfile.h"

struct SimulationWorldStageTimingSnapshot final {
    std::uint64_t sequence = 0;
    std::array<double, SimulationWorldProfile::STAGE_COUNT> stageNanos{};
    double totalNanos = 0.0;
};
