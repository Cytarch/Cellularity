#pragma once

#include <string>

#include "PerfJson.h"
#include "core/SimulationWorld.h"

PerfJson runFullUpdateProfileJson(SimulationWorld& world,
        const std::string& inputPath,
        int substrateFormatVersion,
        int warmupSteps,
        int profileSteps);

PerfJson runStageBreakdownProfileJson(SimulationWorld& world,
        const std::string& inputPath,
        int substrateFormatVersion,
        int warmupSteps,
        int profileSteps);

PerfJson runSignalKernelBenchmarkJson(SimulationWorld& world,
        const std::string& inputPath,
        int substrateFormatVersion,
        int warmupSteps,
        int measuredSteps);

PerfJson runRenderCopyToDrawProfileJson(SimulationWorld& world,
        const std::string& inputPath,
        int substrateFormatVersion,
        int warmupSteps,
        int profileSteps,
        double viewRadius);

PerfJson runDefaultRandomCellsMaxUpsJson(int maxCells,
        int maxFoods,
        double radius,
        int warmupSteps,
        int profileSteps);

PerfJson runDefaultRandomCellsDetailJson(int maxCells,
        int maxFoods,
        double radius,
        int warmupSteps,
        int profileSteps);

PerfJson runEvolutionTwoCellSwimmerJson(int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        int stableWindowSteps,
        double minStableTravel,
        double mutationCoef,
        double foodSpeed,
        double foodSize);

PerfJson runEvolutionSeededCellTwoCellSwimmerJson(int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        int stableWindowSteps,
        double minStableTravel,
        double mutationCoef,
        double foodSpeed,
        double foodSize,
        int seedCellCount);

PerfJson runEvolutionSeededCellBalanceJson(int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        int stableWindowSteps,
        double minStableTravel,
        double mutationCoef,
        double foodSpeed,
        double foodSize,
        int seedCellCount,
        double idleMetabolismScale,
        double flagellocyteCostScale,
        double phagocyteEatSpeedScale);

PerfJson runEvolutionComplexityJson(int maxSteps,
        int maxCells,
        int maxFoods,
        double radius,
        double radiation,
        int64_t seed,
        double mutationCoef,
        double cellTypeMutationWeightScale,
        double foodSpeed,
        double foodSize,
        double foodLumpiness,
        double foodLumpSize,
        int seedCellCount,
        double idleMetabolismScale,
        double flagellocyteCostScale,
        double phagocyteEatSpeedScale,
        int sampleIntervalSteps,
        int stableWindowSteps,
        int stableMinComponentSize);
