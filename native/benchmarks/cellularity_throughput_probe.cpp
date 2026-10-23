#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "PerfJson.h"
#include "PerfState.h"
#include "core/Environment.h"
#include "formats/NativeSubstrateLoader.h"

namespace {

const char* algorithmName(CellOverlapScanAlgorithm algorithm) {
    switch (algorithm) {
        case CellOverlapScanAlgorithm::COMPACT_RECORD_SWEEP:
            return "compact-record-sweep";
        case CellOverlapScanAlgorithm::COMPACT_PRIMARY_SWEEP:
            return "compact-primary-sweep";
        case CellOverlapScanAlgorithm::INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP:
            return "incremental-bucketed-f64-source32-sweep";
        case CellOverlapScanAlgorithm::BASELINE_SWEEP:
            return "baseline-sweep";
        default:
            return "unexpected-runtime-algorithm";
    }
}

void printUsage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <substrate> [warmup-steps=200] [measured-steps=5000] [output=-]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        printUsage(argv[0]);
        return 2;
    }
    try {
        const std::string inputPath = argv[1];
        const int warmupSteps = argc > 2 ? std::stoi(argv[2]) : 200;
        const int measuredSteps = argc > 3 ? std::stoi(argv[3]) : 5000;
        const std::string outputPath = argc > 4 ? argv[4] : "-";
        if (warmupSteps < 0 || measuredSteps < 1) {
            throw std::invalid_argument("Invalid throughput probe step count");
        }

        Environment fallback = Environment::constructDefaultEnvironment(1000, 1000, 100.0);
        NativeSubstrateLoadResult substrate = loadNativeSubstrateFromPath(inputPath, false, fallback);
        SimulationWorld& world = *substrate.world;
        const PerfJson initialState = capturePerfWorldState(world);
        for (int step = 0; step < warmupSteps; step++) {
            world.update(false, 1.0);
        }
        const PerfJson measuredStartState = capturePerfWorldState(world);
        const auto startedAt = std::chrono::steady_clock::now();
        for (int step = 0; step < measuredSteps; step++) {
            world.update(false, 1.0);
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - startedAt);
        const double totalMs = static_cast<double>(elapsed.count()) / 1'000'000.0;
        const PerfJson finalState = capturePerfWorldState(world);
        writePerfJsonReport(
                {
                        {"schemaVersion", 2},
                        {"scenario", "simulation/android_native_full_update"},
                        {"inputPath", inputPath},
                        {"substrateFormatVersion", substrate.familyLocalVersion},
                        {"overlapBroadphaseAlgorithm", algorithmName(world.cellOverlapScanAlgorithm())},
                        {"warmupSteps", warmupSteps},
                        {"profileSteps", measuredSteps},
                        {"maxCells", world.mEnvironment.maxCellCount},
                        {"maxFoods", world.mEnvironment.maxFoodCount},
                        {"environmentRadius", world.mEnvironment.mRadius},
                        {"cells", world.mCellData.mCellCount},
                        {"food", world.mFoodData.mFoodCount},
                        {"totalMs", totalMs},
                        {"avgStepMs", totalMs / measuredSteps},
                        {"stepsPerSecond", measuredSteps / (totalMs / 1000.0)},
                        {"state", {
                                          {"initial", initialState},
                                          {"measuredStart", measuredStartState},
                                          {"final", finalState},
                                  }},
                },
                outputPath);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "throughput probe failed: " << error.what() << '\n';
        return 1;
    }
}
