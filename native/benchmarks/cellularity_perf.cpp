#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "PerfJson.h"
#include "PerfOverlap.h"
#include "PerfScenarios.h"
#include "PerfSignal.h"
#include "ThroughputWorkloads.h"
#include "core/Environment.h"
#include "formats/NativeSubstrateLoader.h"

namespace {

constexpr const char* DEFAULT_SUBSTRATE_PATH = "scripts/native/fixtures/balansnolip.substrate";

void printUsage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program
              << " [substrate-path] <mode> [warmupSteps] [profileSteps] [mode-args...] "
                 "[json-output]\n"
              << "\n"
              << "Modes:\n"
              << "  throughput-workloads-json [output=-]\n"
              << "  throughput-synthetic-profile-json <workload> [warmupSteps=50] "
                 "[profileSteps=500] [output=-]\n"
              << "  throughput-synthetic-detail-json <workload> [warmupSteps=50] "
                 "[profileSteps=500] [output=-]\n"
              << "  throughput-synthetic-signal-json <workload> "
                 "[warmupSteps=50] [profileSteps=500] "
                 "[algorithm=global-edge|cell-owned-csr] [output=-]\n"
              << "  throughput-synthetic-signal-detail-json <workload> "
                 "[warmupSteps=50] [profileSteps=500] "
                 "[algorithm=global-edge|cell-owned-csr] [output=-]\n"
              << "  throughput-synthetic-signal-kernel-json <workload> "
                 "[warmupSteps=200] [measuredSteps=5000] "
                 "[algorithm=global-edge|cell-owned-csr] [output=-]\n"
              << "  throughput-synthetic-trace-json <workload> [steps=32] [output=-]\n"
              << "  throughput-synthetic-stationary-json <workload> [warmupRepeats=100] "
                 "[measuredRepeats=1000] [output=-]\n"
              << "  [substrate-path] throughput-trace-json [steps=32] [output=-]\n"
              << "  [substrate-path] throughput-stationary-json [warmupRepeats=100] "
                 "[measuredRepeats=1000] [output=-]\n"
              << "  profile-json [warmupSteps=200] [profileSteps=2000] [output=-]\n"
              << "  profile-algorithm-json [warmupSteps=200] [profileSteps=2000] "
                 "[algorithm=baseline|compact-record|compact-primary|incremental-bucketed-f64-source32] [output=-]\n"
              << "  profile-detail-json [warmupSteps=200] [profileSteps=2000] "
                 "[output=-]\n"
              << "  profile-signal-json [warmupSteps=200] [profileSteps=2000] "
                 "[algorithm=global-edge|cell-owned-csr] [output=-]\n"
              << "  profile-signal-detail-json [warmupSteps=200] "
                 "[profileSteps=2000] [algorithm=global-edge|cell-owned-csr] "
                 "[output=-]\n"
              << "  signal-kernel-json [warmupSteps=200] [measuredSteps=5000] "
                 "[algorithm=global-edge|cell-owned-csr] [output=-]\n"
              << "  profile-overlap-json [warmupSteps=200] [profileSteps=2000] "
                 "[output=-]\n"
              << "  compare-overlap-json [warmupSteps=200] [profileSteps=2000] "
                 "[candidate=baseline|tight-radius|row-max|simd-exact|scalar-y-prune|"
                 "scalar-tile|simd-tile|compact-record|compact-primary|incremental-bucketed-f64-source32] [output=-]\n"
              << "  compare-overlap-balanced-json [warmupSteps=200] "
                 "[profileSteps=2000, multiple of four] [algorithmA=baseline] "
                 "[algorithmB=compact-record] [output=-]\n"
              << "  overlap-scan-repeat-json [warmupSteps=200] [repeatCount=10000] "
                 "[candidate=baseline-fast|...] [output=-]\n"
              << "  overlap-scan-balanced-json [warmupSteps=200] [repeatCount=10000, "
                 "multiple of four] [algorithmA=baseline] "
                 "[algorithmB=compact-record] [output=-]\n"
              << "  profile-render-json [warmupSteps=100] [profileSteps=500] "
                 "[viewRadius=0] [output=-]\n"
              << "  default-random-cells-json [warmupSteps=200] [profileSteps=5000] "
                 "[maxCells=1000] [maxFoods=3500] [radius=1] [output=-]\n"
              << "  default-random-cells-detail-json [warmupSteps=200] "
                 "[profileSteps=5000] [maxCells=1000] [maxFoods=3500] [radius=1] "
                 "[output=-]\n"
              << "  evolution-two-cell-swimmer-json [maxSteps=200000] [maxCells=1000] "
                 "[maxFoods=3500] [radius=1] [radiation=0.09] [seed=1337] "
                 "[stableWindowSteps=500] [minStableTravel=0.05] [mutationCoef=0.10] "
                 "[foodSpeed=10.5] [foodSize=0.25] [output=-]\n"
              << "  evolution-seeded-cell-swimmer-json [maxSteps=200000] "
                 "[maxCells=1000] [maxFoods=3500] [radius=1] [radiation=0.03] "
                 "[seed=1337] [stableWindowSteps=500] [minStableTravel=0.05] "
                 "[mutationCoef=0.05] [foodSpeed=15] [foodSize=0.25] "
                 "[seedCellCount=100] [output=-]\n"
              << "  evolution-seeded-cell-balance-json [maxSteps=200000] "
                 "[maxCells=1000] [maxFoods=3500] [radius=1] [radiation=0.03] "
                 "[seed=1337] [stableWindowSteps=500] [minStableTravel=0.05] "
                 "[mutationCoef=0.05] [foodSpeed=15] [foodSize=0.20] "
                 "[seedCellCount=100] [idleMetabolismScale=1] "
                 "[flagellocyteCostScale=1] [phagocyteEatSpeedScale=1] [output=-]\n"
              << "  evolution-complexity-json [maxSteps=200000] [maxCells=1000] "
                 "[maxFoods=3500] [radius=1] [radiation=0.03] [seed=1337] "
                 "[mutationCoef=0.05] [cellTypeMutationWeightScale=8] [foodSpeed=15] "
                 "[foodSize=0.20] [foodLumpiness=10] [foodLumpSize=0.06] "
                 "[seedCellCount=100] [idleMetabolismScale=1] "
                 "[flagellocyteCostScale=1.5] [phagocyteEatSpeedScale=0.75] "
                 "[sampleIntervalSteps=100] [stableWindowSteps=2000] "
                 "[stableMinComponentSize=6] [output=-]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string firstArg = argc > 1 ? argv[1] : "";
        if (firstArg == "throughput-workloads-json") {
            std::string outputPath = argc > 2 ? argv[2] : "-";
            writePerfJsonReport({{"schemaVersion", 1}, {"workloads", throughputWorkloadNames()}}, outputPath);
            return 0;
        }
        if (firstArg == "throughput-synthetic-profile-json"
                || firstArg == "throughput-synthetic-detail-json") {
            if (argc < 3) {
                throw std::invalid_argument("Synthetic profile mode requires a workload name");
            }
            const std::string workload = argv[2];
            const int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 50;
            const int profileSteps = argc > 4 ? std::stoi(argv[4]) : 500;
            const std::string outputPath = argc > 5 ? argv[5] : "-";
            writePerfJsonReport(
                    runThroughputSyntheticProfileJson(
                            workload,
                            warmupSteps,
                            profileSteps,
                            firstArg == "throughput-synthetic-detail-json"),
                    outputPath);
            return 0;
        }
        if (firstArg == "throughput-synthetic-signal-json"
                || firstArg == "throughput-synthetic-signal-detail-json") {
            if (argc < 3) {
                throw std::invalid_argument(
                        "Synthetic signal profile mode requires a workload name");
            }
            const std::string workload = argv[2];
            const int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 50;
            const int profileSteps = argc > 4 ? std::stoi(argv[4]) : 500;
            const std::string algorithmName =
                    argc > 5 ? argv[5] : "global-edge";
            const std::string outputPath = argc > 6 ? argv[6] : "-";
            const CellSignalUpdateAlgorithm algorithm =
                    parseSignalUpdateAlgorithm(algorithmName);
            writePerfJsonReport(
                    runThroughputSyntheticSignalProfileJson(
                            workload,
                            warmupSteps,
                            profileSteps,
                            firstArg == "throughput-synthetic-signal-detail-json",
                            algorithm),
                    outputPath);
            return 0;
        }
        if (firstArg == "throughput-synthetic-signal-kernel-json") {
            if (argc < 3) {
                throw std::invalid_argument(
                        "Synthetic signal kernel mode requires a workload name");
            }
            const std::string workload = argv[2];
            const int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            const int measuredSteps = argc > 4 ? std::stoi(argv[4]) : 5000;
            const std::string algorithmName =
                    argc > 5 ? argv[5] : "global-edge";
            const std::string outputPath = argc > 6 ? argv[6] : "-";
            const CellSignalUpdateAlgorithm algorithm =
                    parseSignalUpdateAlgorithm(algorithmName);
            std::unique_ptr<SimulationWorld> world = createThroughputWorkload(workload);
            world->mCellData.setSignalUpdateAlgorithm(algorithm);
            PerfJson report = runSignalKernelBenchmarkJson(
                    *world, workload, 0, warmupSteps, measuredSteps);
            report["scenario"] = "simulation/synthetic_signal_kernel";
            report["workload"] = workload;
            writePerfJsonReport(report, outputPath);
            return 0;
        }
        if (firstArg == "throughput-synthetic-trace-json") {
            if (argc < 3) {
                throw std::invalid_argument("Synthetic trace mode requires a workload name");
            }
            const std::string workload = argv[2];
            const int steps = argc > 3 ? std::stoi(argv[3]) : 32;
            const std::string outputPath = argc > 4 ? argv[4] : "-";
            std::unique_ptr<SimulationWorld> world = createThroughputWorkload(workload);
            writePerfJsonReport(runThroughputOrderedTraceJson(*world, workload, steps), outputPath);
            return 0;
        }
        if (firstArg == "throughput-synthetic-stationary-json") {
            if (argc < 3) {
                throw std::invalid_argument("Synthetic stationary mode requires a workload name");
            }
            const std::string workload = argv[2];
            const int warmupRepeats = argc > 3 ? std::stoi(argv[3]) : 100;
            const int measuredRepeats = argc > 4 ? std::stoi(argv[4]) : 1000;
            const std::string outputPath = argc > 5 ? argv[5] : "-";
            std::unique_ptr<SimulationWorld> world = createThroughputWorkload(workload);
            writePerfJsonReport(
                    runThroughputStationaryOverlapJson(
                            *world, workload, warmupRepeats, measuredRepeats),
                    outputPath);
            return 0;
        }
        if (firstArg == "default-random-cells-json") {
            int warmupSteps = argc > 2 ? std::stoi(argv[2]) : 200;
            int profileSteps = argc > 3 ? std::stoi(argv[3]) : 5000;
            int maxCells = argc > 4 ? std::stoi(argv[4]) : 1000;
            int maxFoods = argc > 5 ? std::stoi(argv[5]) : 3500;
            double radius = argc > 6 ? std::stod(argv[6]) : 1.0;
            std::string outputPath = argc > 7 ? argv[7] : "-";
            writePerfJsonReport(runDefaultRandomCellsMaxUpsJson(maxCells, maxFoods, radius, warmupSteps, profileSteps),
                    outputPath);
            return 0;
        }
        if (firstArg == "default-random-cells-detail-json") {
            int warmupSteps = argc > 2 ? std::stoi(argv[2]) : 200;
            int profileSteps = argc > 3 ? std::stoi(argv[3]) : 5000;
            int maxCells = argc > 4 ? std::stoi(argv[4]) : 1000;
            int maxFoods = argc > 5 ? std::stoi(argv[5]) : 3500;
            double radius = argc > 6 ? std::stod(argv[6]) : 1.0;
            std::string outputPath = argc > 7 ? argv[7] : "-";
            writePerfJsonReport(runDefaultRandomCellsDetailJson(maxCells, maxFoods, radius, warmupSteps, profileSteps),
                    outputPath);
            return 0;
        }
        if (firstArg == "evolution-two-cell-swimmer-json") {
            int maxSteps = argc > 2 ? std::stoi(argv[2]) : 200000;
            int maxCells = argc > 3 ? std::stoi(argv[3]) : 1000;
            int maxFoods = argc > 4 ? std::stoi(argv[4]) : 3500;
            double radius = argc > 5 ? std::stod(argv[5]) : 1.0;
            double radiation = argc > 6 ? std::stod(argv[6]) : Environment::MAX_RADIATION;
            int64_t seed = argc > 7 ? std::stoll(argv[7]) : 1337;
            int stableWindowSteps = argc > 8 ? std::stoi(argv[8]) : 500;
            double minStableTravel = argc > 9 ? std::stod(argv[9]) : 0.05;
            double mutationCoef = argc > 10 ? std::stod(argv[10]) : 0.10;
            double foodSpeed = argc > 11 ? std::stod(argv[11]) : Environment::MAX_FOOD_SPEED * 0.7;
            double foodSize = argc > 12 ? std::stod(argv[12]) : 0.25;
            std::string outputPath = argc > 13 ? argv[13] : "-";
            writePerfJsonReport(runEvolutionTwoCellSwimmerJson(maxSteps, maxCells, maxFoods, radius, radiation, seed,
                                        stableWindowSteps, minStableTravel, mutationCoef,
                                        foodSpeed, foodSize),
                    outputPath);
            return 0;
        }
        if (firstArg == "evolution-seeded-cell-swimmer-json") {
            int maxSteps = argc > 2 ? std::stoi(argv[2]) : 200000;
            int maxCells = argc > 3 ? std::stoi(argv[3]) : 1000;
            int maxFoods = argc > 4 ? std::stoi(argv[4]) : 3500;
            double radius = argc > 5 ? std::stod(argv[5]) : 1.0;
            double radiation = argc > 6 ? std::stod(argv[6]) : 0.03;
            int64_t seed = argc > 7 ? std::stoll(argv[7]) : 1337;
            int stableWindowSteps = argc > 8 ? std::stoi(argv[8]) : 500;
            double minStableTravel = argc > 9 ? std::stod(argv[9]) : 0.05;
            double mutationCoef = argc > 10 ? std::stod(argv[10]) : 0.05;
            double foodSpeed = argc > 11 ? std::stod(argv[11]) : Environment::MAX_FOOD_SPEED;
            double foodSize = argc > 12 ? std::stod(argv[12]) : 0.25;
            int seedCellCount = argc > 13 ? std::stoi(argv[13]) : 100;
            std::string outputPath = argc > 14 ? argv[14] : "-";
            writePerfJsonReport(runEvolutionSeededCellTwoCellSwimmerJson(
                                        maxSteps, maxCells, maxFoods, radius, radiation, seed, stableWindowSteps,
                                        minStableTravel, mutationCoef, foodSpeed, foodSize, seedCellCount),
                    outputPath);
            return 0;
        }
        if (firstArg == "evolution-seeded-cell-balance-json") {
            int maxSteps = argc > 2 ? std::stoi(argv[2]) : 200000;
            int maxCells = argc > 3 ? std::stoi(argv[3]) : 1000;
            int maxFoods = argc > 4 ? std::stoi(argv[4]) : 3500;
            double radius = argc > 5 ? std::stod(argv[5]) : 1.0;
            double radiation = argc > 6 ? std::stod(argv[6]) : 0.03;
            int64_t seed = argc > 7 ? std::stoll(argv[7]) : 1337;
            int stableWindowSteps = argc > 8 ? std::stoi(argv[8]) : 500;
            double minStableTravel = argc > 9 ? std::stod(argv[9]) : 0.05;
            double mutationCoef = argc > 10 ? std::stod(argv[10]) : 0.05;
            double foodSpeed = argc > 11 ? std::stod(argv[11]) : Environment::MAX_FOOD_SPEED;
            double foodSize = argc > 12 ? std::stod(argv[12]) : 0.20;
            int seedCellCount = argc > 13 ? std::stoi(argv[13]) : 100;
            double idleMetabolismScale = argc > 14 ? std::stod(argv[14]) : 1.0;
            double flagellocyteCostScale = argc > 15 ? std::stod(argv[15]) : 1.0;
            double phagocyteEatSpeedScale = argc > 16 ? std::stod(argv[16]) : 1.0;
            std::string outputPath = argc > 17 ? argv[17] : "-";
            writePerfJsonReport(runEvolutionSeededCellBalanceJson(
                                        maxSteps, maxCells, maxFoods, radius, radiation, seed, stableWindowSteps,
                                        minStableTravel, mutationCoef, foodSpeed, foodSize, seedCellCount,
                                        idleMetabolismScale, flagellocyteCostScale, phagocyteEatSpeedScale),
                    outputPath);
            return 0;
        }
        if (firstArg == "evolution-complexity-json") {
            int maxSteps = argc > 2 ? std::stoi(argv[2]) : 200000;
            int maxCells = argc > 3 ? std::stoi(argv[3]) : 1000;
            int maxFoods = argc > 4 ? std::stoi(argv[4]) : 3500;
            double radius = argc > 5 ? std::stod(argv[5]) : 1.0;
            double radiation = argc > 6 ? std::stod(argv[6]) : 0.03;
            int64_t seed = argc > 7 ? std::stoll(argv[7]) : 1337;
            double mutationCoef = argc > 8 ? std::stod(argv[8]) : 0.05;
            double cellTypeMutationWeightScale = argc > 9 ? std::stod(argv[9]) : 8.0;
            double foodSpeed = argc > 10 ? std::stod(argv[10]) : Environment::MAX_FOOD_SPEED;
            double foodSize = argc > 11 ? std::stod(argv[11]) : 0.20;
            double foodLumpiness = argc > 12 ? std::stod(argv[12]) : 10.0;
            double foodLumpSize = argc > 13 ? std::stod(argv[13]) : 0.06;
            int seedCellCount = argc > 14 ? std::stoi(argv[14]) : 100;
            double idleMetabolismScale = argc > 15 ? std::stod(argv[15]) : 1.0;
            double flagellocyteCostScale = argc > 16 ? std::stod(argv[16]) : 1.5;
            double phagocyteEatSpeedScale = argc > 17 ? std::stod(argv[17]) : 0.75;
            int sampleIntervalSteps = argc > 18 ? std::stoi(argv[18]) : 100;
            int stableWindowSteps = argc > 19 ? std::stoi(argv[19]) : 2000;
            int stableMinComponentSize = argc > 20 ? std::stoi(argv[20]) : 6;
            std::string outputPath = argc > 21 ? argv[21] : "-";
            writePerfJsonReport(
                    runEvolutionComplexityJson(maxSteps, maxCells, maxFoods, radius, radiation, seed, mutationCoef,
                            cellTypeMutationWeightScale, foodSpeed, foodSize, foodLumpiness,
                            foodLumpSize, seedCellCount, idleMetabolismScale, flagellocyteCostScale,
                            phagocyteEatSpeedScale, sampleIntervalSteps, stableWindowSteps,
                            stableMinComponentSize),
                    outputPath);
            return 0;
        }

        std::string path = argc > 1 ? argv[1] : DEFAULT_SUBSTRATE_PATH;
        std::string mode = argc > 2 ? argv[2] : "profile-detail-json";

        Environment fallbackEnvironment = Environment::constructDefaultEnvironment(1000, 1000, 100.0);
        NativeSubstrateLoadResult substrate = loadNativeSubstrateFromPath(path, false, fallbackEnvironment);
        SimulationWorld& world = *substrate.world;

        if (mode == "throughput-trace-json") {
            const int steps = argc > 3 ? std::stoi(argv[3]) : 32;
            const std::string outputPath = argc > 4 ? argv[4] : "-";
            writePerfJsonReport(runThroughputOrderedTraceJson(world, path, steps), outputPath);
            return 0;
        }
        if (mode == "throughput-stationary-json") {
            const int warmupRepeats = argc > 3 ? std::stoi(argv[3]) : 100;
            const int measuredRepeats = argc > 4 ? std::stoi(argv[4]) : 1000;
            const std::string outputPath = argc > 5 ? argv[5] : "-";
            writePerfJsonReport(
                    runThroughputStationaryOverlapJson(
                            world, path, warmupRepeats, measuredRepeats),
                    outputPath);
            return 0;
        }

        if (mode == "profile-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            int profileSteps = argc > 4 ? std::stoi(argv[4]) : 2000;
            std::string outputPath = argc > 5 ? argv[5] : "-";
            writePerfJsonReport(
                    runFullUpdateProfileJson(world, path, substrate.familyLocalVersion, warmupSteps, profileSteps),
                    outputPath);
            return 0;
        }
        if (mode == "profile-algorithm-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            int profileSteps = argc > 4 ? std::stoi(argv[4]) : 2000;
            std::string algorithmName = argc > 5 ? argv[5] : "baseline";
            std::string outputPath = argc > 6 ? argv[6] : "-";
            const CellOverlapScanAlgorithm algorithm = parseOverlapScanAlgorithm(algorithmName);
            world.setCellOverlapScanAlgorithm(algorithm);
            PerfJson report = runFullUpdateProfileJson(world, path, substrate.familyLocalVersion, warmupSteps, profileSteps);
            report["overlapBroadphaseAlgorithm"] = overlapScanAlgorithmName(algorithm);
            writePerfJsonReport(report, outputPath);
            return 0;
        }
        if (mode == "profile-detail-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            int profileSteps = argc > 4 ? std::stoi(argv[4]) : 2000;
            std::string outputPath = argc > 5 ? argv[5] : "-";
            writePerfJsonReport(
                    runStageBreakdownProfileJson(world, path, substrate.familyLocalVersion, warmupSteps, profileSteps),
                    outputPath);
            return 0;
        }
        if (mode == "profile-signal-json"
                || mode == "profile-signal-detail-json") {
            const int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            const int profileSteps = argc > 4 ? std::stoi(argv[4]) : 2000;
            const std::string algorithmName =
                    argc > 5 ? argv[5] : "global-edge";
            const std::string outputPath = argc > 6 ? argv[6] : "-";
            const CellSignalUpdateAlgorithm algorithm =
                    parseSignalUpdateAlgorithm(algorithmName);
            world.mCellData.setSignalUpdateAlgorithm(algorithm);
            PerfJson report = mode == "profile-signal-detail-json"
                    ? runStageBreakdownProfileJson(
                              world,
                              path,
                              substrate.familyLocalVersion,
                              warmupSteps,
                              profileSteps)
                    : runFullUpdateProfileJson(
                              world,
                              path,
                              substrate.familyLocalVersion,
                              warmupSteps,
                              profileSteps);
            report["signalUpdateAlgorithm"] =
                    signalUpdateAlgorithmName(algorithm);
            writePerfJsonReport(report, outputPath);
            return 0;
        }
        if (mode == "signal-kernel-json") {
            const int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            const int measuredSteps = argc > 4 ? std::stoi(argv[4]) : 5000;
            const std::string algorithmName =
                    argc > 5 ? argv[5] : "global-edge";
            const std::string outputPath = argc > 6 ? argv[6] : "-";
            const CellSignalUpdateAlgorithm algorithm =
                    parseSignalUpdateAlgorithm(algorithmName);
            world.mCellData.setSignalUpdateAlgorithm(algorithm);
            writePerfJsonReport(
                    runSignalKernelBenchmarkJson(
                            world,
                            path,
                            substrate.familyLocalVersion,
                            warmupSteps,
                            measuredSteps),
                    outputPath);
            return 0;
        }
        if (mode == "profile-overlap-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            int profileSteps = argc > 4 ? std::stoi(argv[4]) : 2000;
            std::string outputPath = argc > 5 ? argv[5] : "-";
            writePerfJsonReport(
                    runOverlapDetailProfileJson(world, path, substrate.familyLocalVersion, warmupSteps, profileSteps),
                    outputPath);
            return 0;
        }
        if (mode == "compare-overlap-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            int profileSteps = argc > 4 ? std::stoi(argv[4]) : 2000;
            std::string candidateAlgorithm = argc > 5 ? argv[5] : "tight-radius";
            std::string outputPath = argc > 6 ? argv[6] : "-";
            writePerfJsonReport(runOverlapAlgorithmCompareJson(world, path, substrate.familyLocalVersion, warmupSteps,
                                        profileSteps, candidateAlgorithm),
                    outputPath);
            return 0;
        }
        if (mode == "compare-overlap-balanced-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            int profileSteps = argc > 4 ? std::stoi(argv[4]) : 2000;
            std::string algorithmA = argc > 5 ? argv[5] : "baseline";
            std::string algorithmB = argc > 6 ? argv[6] : "compact-record";
            std::string outputPath = argc > 7 ? argv[7] : "-";
            writePerfJsonReport(
                    runBalancedOverlapAlgorithmCompareJson(world, path, substrate.familyLocalVersion, warmupSteps,
                            profileSteps, algorithmA, algorithmB),
                    outputPath);
            return 0;
        }
        if (mode == "overlap-scan-repeat-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            int repeatCount = argc > 4 ? std::stoi(argv[4]) : 10000;
            std::string candidateAlgorithm = argc > 5 ? argv[5] : "baseline-fast";
            std::string outputPath = argc > 6 ? argv[6] : "-";
            writePerfJsonReport(runOverlapScanRepeatJson(world, path, substrate.familyLocalVersion, warmupSteps, repeatCount,
                                        candidateAlgorithm),
                    outputPath);
            return 0;
        }
        if (mode == "overlap-scan-balanced-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 200;
            int repeatCount = argc > 4 ? std::stoi(argv[4]) : 10000;
            std::string algorithmA = argc > 5 ? argv[5] : "baseline";
            std::string algorithmB = argc > 6 ? argv[6] : "compact-record";
            std::string outputPath = argc > 7 ? argv[7] : "-";
            writePerfJsonReport(runBalancedOverlapScanRepeatJson(world, path, substrate.familyLocalVersion, warmupSteps,
                                        repeatCount, algorithmA, algorithmB),
                    outputPath);
            return 0;
        }
        if (mode == "profile-render-json") {
            int warmupSteps = argc > 3 ? std::stoi(argv[3]) : 100;
            int profileSteps = argc > 4 ? std::stoi(argv[4]) : 500;
            int argIndex = 5;
            double viewRadius = argc > argIndex ? std::stod(argv[argIndex]) : 0.0;
            std::string outputPath = argc > argIndex + 1 ? argv[argIndex + 1] : "-";
            writePerfJsonReport(runRenderCopyToDrawProfileJson(world, path, substrate.familyLocalVersion, warmupSteps,
                                        profileSteps, viewRadius),
                    outputPath);
            return 0;
        }

        printUsage(argv[0]);
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << "Error: " << exception.what() << '\n';
        return 1;
    }
}
