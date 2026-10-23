#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/resource.h>

#include "PerfJson.h"
#include "tests/baseline/BehaviorBaseline.h"

namespace {

using Clock = std::chrono::steady_clock;

#ifndef CELLULARITY_BUILD_TYPE
#define CELLULARITY_BUILD_TYPE "unknown"
#endif

#ifndef CELLULARITY_SYSTEM_NAME
#define CELLULARITY_SYSTEM_NAME "unknown"
#endif

#ifndef CELLULARITY_SYSTEM_PROCESSOR
#define CELLULARITY_SYSTEM_PROCESSOR "unknown"
#endif

#ifndef CELLULARITY_SOURCE_REVISION
#define CELLULARITY_SOURCE_REVISION "unavailable"
#endif

#ifndef CELLULARITY_SOURCE_DIRTY
#define CELLULARITY_SOURCE_DIRTY "unknown"
#endif

long long peakRssKiB() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
#if defined(__APPLE__)
    return static_cast<long long>(usage.ru_maxrss / 1024);
#else
    return static_cast<long long>(usage.ru_maxrss);
#endif
}

void runUpdates(SimulationWorld& world, int updateCount) {
    for (int update = 0; update < updateCount; update++) {
        world.update(false, 1.0);
    }
}

const char* compilerName() {
#if defined(__clang__)
    return "Clang";
#elif defined(__GNUC__)
    return "GCC";
#else
    return "unknown";
#endif
}

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " [warmupUpdates=80] [measuredUpdates=500] [output=-]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        const int warmupUpdates = argc > 1 ? std::stoi(argv[1]) : 80;
        const int measuredUpdates = argc > 2 ? std::stoi(argv[2]) : 500;
        const std::string outputPath = argc > 3 ? argv[3] : "-";
        if (warmupUpdates < 0 || measuredUpdates <= 0) {
            throw std::invalid_argument("Update counts must be non-negative and measuredUpdates must be positive");
        }

        std::unique_ptr<SimulationWorld> world =
                cellularity::behavior_baseline::createRepresentativePerformanceWorld();
        const auto initial = cellularity::behavior_baseline::summarizeWorld(*world);
        runUpdates(*world, warmupUpdates);
        const auto measuredStart = cellularity::behavior_baseline::summarizeWorld(*world);

        const auto startedAt = Clock::now();
        runUpdates(*world, measuredUpdates);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - startedAt);
        const auto final = cellularity::behavior_baseline::summarizeWorld(*world);
        const double wallTimeMs = static_cast<double>(elapsed.count()) / 1'000'000.0;

        PerfJson report = {
                {"schemaVersion", 1},
                {"benchmark", "simulation/representative_world"},
                {"behaviorBaselineVersion", 1},
                {"comparisonPolicy", "signal_only_no_hard_threshold"},
                {"scenarioRevision", 1},
                {"seed", cellularity::behavior_baseline::REPRESENTATIVE_PERFORMANCE_SEED},
                {"warmupUpdates", warmupUpdates},
                {"measuredUpdates", measuredUpdates},
                {"wallTimeMs", wallTimeMs},
                {"updatesPerSecond", measuredUpdates / (wallTimeMs / 1000.0)},
                {"peakRssKiB", peakRssKiB()},
                {"scenario", {{"radius", 1.5}, {"cellCapacity", 256}, {"foodCapacity", 1600}, {"initialCellGrid", "14x14"}, {"initialFoodGrid", "30x30"}}},
                {"state", {{"initialDigest", initial.digest}, {"measuredStartDigest", measuredStart.digest}, {"finalDigest", final.digest}, {"initialCells", initial.state.cells}, {"initialFoods", initial.state.foods}, {"finalCells", final.state.cells}, {"finalFoods", final.state.foods}, {"finalLinks", final.state.links}, {"finalMutations", final.state.mutations}, {"finiteState", final.finiteState}}},
                {"metadata", {{"buildType", CELLULARITY_BUILD_TYPE}, {"system", CELLULARITY_SYSTEM_NAME}, {"processor", CELLULARITY_SYSTEM_PROCESSOR}, {"compiler", compilerName()}, {"compilerVersion", __VERSION__}, {"cplusplus", __cplusplus}, {"hardwareThreads", std::thread::hardware_concurrency()}, {"timer", "std::chrono::steady_clock"}, {"peakRssSource", "getrusage(RUSAGE_SELF).ru_maxrss"}, {"peakRssUnit", "KiB"}, {"sourceRevision", CELLULARITY_SOURCE_REVISION}, {"sourceDirty", CELLULARITY_SOURCE_DIRTY}, {"simulationBroadphase", "sweep"}}}
        };
        writePerfJsonReport(report, outputPath);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return 1;
    }
}
