#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

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

// Compiler identification for the report. MSVC does not define __VERSION__.
#if defined(__VERSION__)
#define CELLULARITY_COMPILER_VERSION __VERSION__
#elif defined(_MSC_FULL_VER)
#define CELLULARITY_STRINGIZE_IMPL(x) #x
#define CELLULARITY_STRINGIZE(x) CELLULARITY_STRINGIZE_IMPL(x)
#define CELLULARITY_COMPILER_VERSION \
    "MSVC " CELLULARITY_STRINGIZE(_MSC_FULL_VER)
#else
#define CELLULARITY_COMPILER_VERSION "unknown"
#endif

// How peak RSS was obtained, so the report stays honest about its source.
#if defined(_WIN32)
#define CELLULARITY_PEAK_RSS_SOURCE \
    "GetProcessMemoryInfo(PeakWorkingSetSize)"
#else
#define CELLULARITY_PEAK_RSS_SOURCE "getrusage(RUSAGE_SELF).ru_maxrss"
#endif

// Peak resident set size in KiB, or -1 when the platform cannot report it.
// POSIX reads getrusage(RUSAGE_SELF); on Windows the closest equivalent is the
// process memory counters, which report a peak working set in bytes.
long long peakRssKiB() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (::GetProcessMemoryInfo(
                ::GetCurrentProcess(), &counters, sizeof(counters))
            == 0) {
        return -1;
    }
    return static_cast<long long>(counters.PeakWorkingSetSize / 1024);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
#if defined(__APPLE__)
    return static_cast<long long>(usage.ru_maxrss / 1024);
#else
    return static_cast<long long>(usage.ru_maxrss);
#endif
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
                {"metadata", {{"buildType", CELLULARITY_BUILD_TYPE}, {"system", CELLULARITY_SYSTEM_NAME}, {"processor", CELLULARITY_SYSTEM_PROCESSOR}, {"compiler", compilerName()}, {"compilerVersion", CELLULARITY_COMPILER_VERSION}, {"cplusplus", __cplusplus}, {"hardwareThreads", std::thread::hardware_concurrency()}, {"timer", "std::chrono::steady_clock"}, {"peakRssSource", CELLULARITY_PEAK_RSS_SOURCE}, {"peakRssUnit", "KiB"}, {"sourceRevision", CELLULARITY_SOURCE_REVISION}, {"sourceDirty", CELLULARITY_SOURCE_DIRTY}, {"simulationBroadphase", "sweep"}}}
        };
        writePerfJsonReport(report, outputPath);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return 1;
    }
}
