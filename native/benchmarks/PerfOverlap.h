#pragma once

#include <string>

#include "PerfJson.h"
#include "core/SimulationWorld.h"

CellOverlapScanAlgorithm parseOverlapScanAlgorithm(const std::string& name);
const char* overlapScanAlgorithmName(CellOverlapScanAlgorithm algorithm) noexcept;

PerfJson runOverlapDetailProfileJson(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int profileSteps);

PerfJson runOverlapAlgorithmCompareJson(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int profileSteps, const std::string& candidateAlgorithm);

PerfJson runBalancedOverlapAlgorithmCompareJson(SimulationWorld& world, const std::string& inputPath,
        int substrateFormatVersion, int warmupSteps, int profileSteps,
        const std::string& algorithmA, const std::string& algorithmB);

PerfJson runOverlapScanRepeatJson(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int repeatCount, const std::string& candidateAlgorithm);

PerfJson runBalancedOverlapScanRepeatJson(SimulationWorld& world, const std::string& inputPath, int substrateFormatVersion,
        int warmupSteps, int repeatCount, const std::string& algorithmA,
        const std::string& algorithmB);
