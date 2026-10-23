#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/SimulationWorld.h"

namespace cellularity::behavior_baseline {

struct ExpectedState {
    int cells;
    int livingCells;
    int foods;
    int links;
    int mutations;
    int activeSignalValues;
    std::size_t snapshotBytes;
};

struct ScenarioDefinition {
    const char* name;
    int64_t seed;
    int updateCount;
    const char* expectedDigest;
    ExpectedState expectedState;
};

struct ScenarioResult {
    std::string digest;
    ExpectedState state{};
    bool finiteState = true;
};

const std::array<ScenarioDefinition, 6>& scenarios();
std::unique_ptr<SimulationWorld> createScenarioWorld(const ScenarioDefinition& scenario);
ScenarioResult runScenario(const ScenarioDefinition& scenario);

constexpr int64_t REPRESENTATIVE_PERFORMANCE_SEED = 620'241;
std::unique_ptr<SimulationWorld> createRepresentativePerformanceWorld();
ScenarioResult summarizeWorld(const SimulationWorld& world);

} // namespace cellularity::behavior_baseline
