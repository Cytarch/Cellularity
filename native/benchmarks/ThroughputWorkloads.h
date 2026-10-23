#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PerfJson.h"
#include "core/SimulationWorld.h"

std::vector<std::string> throughputWorkloadNames();
std::unique_ptr<SimulationWorld> createThroughputWorkload(const std::string& name);

PerfJson runThroughputSyntheticProfileJson(const std::string& name,
        int warmupSteps,
        int profileSteps,
        bool detailed);

PerfJson runThroughputSyntheticSignalProfileJson(
        const std::string& name,
        int warmupSteps,
        int profileSteps,
        bool detailed,
        CellSignalUpdateAlgorithm algorithm);

PerfJson runThroughputOrderedTraceJson(SimulationWorld& world,
        const std::string& workload,
        int steps);

PerfJson runThroughputStationaryOverlapJson(SimulationWorld& world,
        const std::string& workload,
        int warmupRepeats,
        int measuredRepeats);
