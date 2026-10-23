#include "DispatcherLab.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "PerfJson.h"
#include "app/microscope/render/MicroscopeRenderFramePipeline.h"
#include "core/Environment.h"
#include "execution/DispatcherCapacity.h"
#include "formats/NativeSubstrateLoader.h"
#include "hwy/contrib/thread_pool/thread_pool.h"
#include "platform/CurrentThreadName.h"

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

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;
using cellularity::execution::DispatcherCapacity;
using cellularity::execution::kProjectMaxAdditionalThreads;

constexpr std::size_t kMemoryElementCount = 1U << 20U;
constexpr std::size_t kMaximumLogicalTasks = 128;
constexpr std::uint64_t kCalibrationMinimumUnits = kMaximumLogicalTasks * 64U;
constexpr std::uint64_t kCalibrationMaximumUnits = 1ULL << 28U;
constexpr std::uint64_t kLabSeed = 0xC00D'15A7'2026'0919ULL;
constexpr int kMaximumAutotuneRuns = 20'000;

std::atomic<std::uint64_t> gBenchmarkSink{0};

std::uint64_t nanosBetween(Clock::time_point begin,
        Clock::time_point end) noexcept {
    const auto elapsed =
            std::chrono::duration_cast<Nanoseconds>(end - begin).count();
    return elapsed <= 0 ? 0 : static_cast<std::uint64_t>(elapsed);
}

std::uint64_t parseUnsigned(std::string_view value, const char* label) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(label) + " cannot be empty");
    }
    std::size_t consumed = 0;
    const auto parsed = std::stoull(std::string(value), &consumed, 10);
    if (consumed != value.size()) {
        throw std::invalid_argument(std::string(label) + " must be an integer");
    }
    return parsed;
}

std::vector<std::size_t> parseSizeList(std::string_view value,
        const char* label) {
    std::vector<std::size_t> values;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t comma = value.find(',', begin);
        const std::size_t end =
                comma == std::string_view::npos ? value.size() : comma;
        const std::uint64_t parsed =
                parseUnsigned(value.substr(begin, end - begin), label);
        if (parsed > std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument(std::string(label) + " is too large");
        }
        values.push_back(static_cast<std::size_t>(parsed));
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    if (values.empty()) {
        throw std::invalid_argument(std::string(label) + " cannot be empty");
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

enum class WaitMode {
    Block,
    Spin
};
enum class WorkloadKind {
    NoOp,
    Arithmetic,
    SequentialMemory,
    ScatteredMemory
};
enum class InterferenceMode {
    None,
    ActivePreparation,
    MovingCamera
};

const char* waitModeName(WaitMode mode) noexcept {
    return mode == WaitMode::Block ? "block" : "spin";
}

hwy::PoolWaitMode highwayWaitMode(WaitMode mode) noexcept {
    return mode == WaitMode::Block ? hwy::PoolWaitMode::kBlock
                                   : hwy::PoolWaitMode::kSpin;
}

const char* workloadName(WorkloadKind kind) noexcept {
    switch (kind) {
        case WorkloadKind::NoOp:
            return "no-op";
        case WorkloadKind::Arithmetic:
            return "arithmetic";
        case WorkloadKind::SequentialMemory:
            return "sequential-memory";
        case WorkloadKind::ScatteredMemory:
            return "scattered-memory";
    }
    return "invalid";
}

const char* interferenceName(InterferenceMode mode) noexcept {
    switch (mode) {
        case InterferenceMode::None:
            return "none";
        case InterferenceMode::ActivePreparation:
            return "active-preparation";
        case InterferenceMode::MovingCamera:
            return "moving-camera";
    }
    return "invalid";
}

struct Options final {
    std::string outputPath = "-";
    std::string substratePath;
    int samples = 101;
    int decompositionSamples = 21;
    int caseWarmups = 8;
    int passes = 2;
    int lifecycleSamples = 21;
    bool quick = false;
    bool rawSamples = true;
    bool customAdditionalCounts = false;
    std::vector<std::size_t> additionalCounts;
    std::vector<std::size_t> logicalTaskCounts{1, 2, 4, 8, 16, 32, 128};
    std::vector<std::size_t> targetMicroseconds{25, 100, 400};
    std::vector<WaitMode> waitModes{WaitMode::Block, WaitMode::Spin};
    InterferenceMode interference = InterferenceMode::None;
};

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " [options]\n\n"
              << "  --output <path|->\n"
              << "  --samples <count>\n"
              << "  --decomposition-samples <count>\n"
              << "  --warmups <count>\n"
              << "  --passes <count>\n"
              << "  --lifecycle-samples <count>\n"
              << "  --mode <block|spin|both>\n"
              << "  --additional-counts <0,1,...>\n"
              << "  --task-counts <1,2,4,...>\n"
              << "  --target-us <25,100,400>\n"
              << "  --interference <none|active-preparation|moving-camera>\n"
              << "  --substrate <path>   Required for render interference\n"
              << "  --no-raw-samples\n"
              << "  --quick              Infrastructure smoke only\n";
}

Options parseOptions(int argc, char** argv) {
    Options options;
    auto requireValue = [&](int& index, const char* option) -> std::string_view {
        if (index + 1 >= argc) {
            throw std::invalid_argument(std::string(option) + " requires a value");
        }
        return argv[++index];
    };

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else if (argument == "--output") {
            options.outputPath = requireValue(index, "--output");
        } else if (argument == "--samples") {
            options.samples = static_cast<int>(
                    parseUnsigned(requireValue(index, "--samples"), "samples"));
        } else if (argument == "--decomposition-samples") {
            options.decompositionSamples = static_cast<int>(
                    parseUnsigned(requireValue(index, "--decomposition-samples"),
                            "decomposition samples"));
        } else if (argument == "--warmups") {
            options.caseWarmups = static_cast<int>(
                    parseUnsigned(requireValue(index, "--warmups"), "warmups"));
        } else if (argument == "--passes") {
            options.passes = static_cast<int>(
                    parseUnsigned(requireValue(index, "--passes"), "passes"));
        } else if (argument == "--lifecycle-samples") {
            options.lifecycleSamples = static_cast<int>(parseUnsigned(
                    requireValue(index, "--lifecycle-samples"), "lifecycle samples"));
        } else if (argument == "--mode") {
            const std::string_view value = requireValue(index, "--mode");
            if (value == "block") {
                options.waitModes = {WaitMode::Block};
            } else if (value == "spin") {
                options.waitModes = {WaitMode::Spin};
            } else if (value == "both") {
                options.waitModes = {WaitMode::Block, WaitMode::Spin};
            } else {
                throw std::invalid_argument("mode must be block, spin, or both");
            }
        } else if (argument == "--additional-counts") {
            options.additionalCounts = parseSizeList(
                    requireValue(index, "--additional-counts"), "additional counts");
            options.customAdditionalCounts = true;
        } else if (argument == "--task-counts") {
            options.logicalTaskCounts =
                    parseSizeList(requireValue(index, "--task-counts"), "task counts");
        } else if (argument == "--target-us") {
            options.targetMicroseconds = parseSizeList(
                    requireValue(index, "--target-us"), "target microseconds");
        } else if (argument == "--interference") {
            const std::string_view value = requireValue(index, "--interference");
            if (value == "none") {
                options.interference = InterferenceMode::None;
            } else if (value == "active-preparation") {
                options.interference = InterferenceMode::ActivePreparation;
            } else if (value == "moving-camera") {
                options.interference = InterferenceMode::MovingCamera;
            } else {
                throw std::invalid_argument(
                        "interference must be none, active-preparation, or moving-camera");
            }
        } else if (argument == "--substrate") {
            options.substratePath = requireValue(index, "--substrate");
        } else if (argument == "--no-raw-samples") {
            options.rawSamples = false;
        } else if (argument == "--quick") {
            options.quick = true;
            options.samples = 11;
            options.decompositionSamples = 5;
            options.caseWarmups = 2;
            options.passes = 1;
            options.lifecycleSamples = 3;
            options.targetMicroseconds = {100};
        } else {
            throw std::invalid_argument("Unknown option: " + std::string(argument));
        }
    }

    if (options.samples < 1 || options.decompositionSamples < 1 || options.caseWarmups < 0 || options.passes < 1 || options.lifecycleSamples < 1) {
        throw std::invalid_argument("All sample/pass counts must be positive");
    }
    if (*std::max_element(options.logicalTaskCounts.begin(),
                options.logicalTaskCounts.end())
                    > kMaximumLogicalTasks
            || options.logicalTaskCounts.front() == 0) {
        throw std::invalid_argument("task counts must be in [1, 128]");
    }
    if (options.targetMicroseconds.front() == 0) {
        throw std::invalid_argument("target durations must be positive");
    }
    if (options.interference != InterferenceMode::None && options.substratePath.empty()) {
        throw std::invalid_argument("render interference requires --substrate");
    }
    return options;
}

struct Distribution final {
    double minimum = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
};

double percentile(const std::vector<std::uint64_t>& sorted, double fraction) {
    if (sorted.empty()) {
        return 0.0;
    }
    const double position = fraction * static_cast<double>(sorted.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(position);
    const std::size_t upper = std::min(lower + 1, sorted.size() - 1);
    const double alpha = position - static_cast<double>(lower);
    return static_cast<double>(sorted[lower]) * (1.0 - alpha) + static_cast<double>(sorted[upper]) * alpha;
}

Distribution summarize(const std::vector<std::uint64_t>& samples) {
    if (samples.empty()) {
        return {};
    }
    std::vector<std::uint64_t> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const long double sum = std::accumulate(sorted.begin(), sorted.end(),
            static_cast<long double>(0));
    return {static_cast<double>(sorted.front()),
            percentile(sorted, 0.50),
            percentile(sorted, 0.95),
            percentile(sorted, 0.99),
            static_cast<double>(sorted.back()),
            static_cast<double>(sum / sorted.size())};
}

PerfJson distributionJson(const std::vector<std::uint64_t>& samples,
        bool includeRaw) {
    const Distribution stats = summarize(samples);
    PerfJson result = {{"sampleCount", samples.size()},
            {"minNs", stats.minimum},
            {"p50Ns", stats.p50},
            {"p95Ns", stats.p95},
            {"p99Ns", stats.p99},
            {"maxNs", stats.maximum},
            {"rangeNs", stats.maximum - stats.minimum},
            {"meanNs", stats.mean}};
    if (includeRaw) {
        result["rawNs"] = samples;
    }
    return result;
}

PerfJson signedDistributionJson(const std::vector<double>& samples,
        bool includeRaw) {
    if (samples.empty()) {
        return {{"sampleCount", 0}};
    }
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    auto at = [&](double fraction) {
        const double position = fraction * static_cast<double>(sorted.size() - 1);
        const std::size_t lower = static_cast<std::size_t>(position);
        const std::size_t upper = std::min(lower + 1, sorted.size() - 1);
        const double alpha = position - static_cast<double>(lower);
        return sorted[lower] * (1.0 - alpha) + sorted[upper] * alpha;
    };
    const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    PerfJson result = {{"sampleCount", samples.size()},
            {"min", sorted.front()},
            {"p50", at(0.50)},
            {"p95", at(0.95)},
            {"p99", at(0.99)},
            {"max", sorted.back()},
            {"range", sorted.back() - sorted.front()},
            {"mean", sum / static_cast<double>(sorted.size())}};
    if (includeRaw) {
        result["raw"] = samples;
    }
    return result;
}

std::uint64_t rotateLeft(std::uint64_t value, unsigned int shift) noexcept {
    return (value << shift) | (value >> (64U - shift));
}

struct alignas(64) TaskResult final {
    std::uint64_t value = 0;
};

struct alignas(64) WorkerObservation final {
    std::uint64_t taskCount = 0;
    std::uint64_t firstStartNs = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastEndNs = 0;
};

struct TaskObservation final {
    std::uint64_t startNs = 0;
    std::uint64_t endNs = 0;
};

class WorkloadStorage final {
public:
    WorkloadStorage()
            : mMemory(kMemoryElementCount), mScatteredIndex(kMemoryElementCount),
              mTaskResults(kMaximumLogicalTasks),
              mTaskObservations(kMaximumLogicalTasks),
              mWorkerObservations(kProjectMaxAdditionalThreads + 1U) {
        std::uint64_t state = kLabSeed;
        for (std::size_t index = 0; index < kMemoryElementCount; ++index) {
            state ^= state >> 12U;
            state ^= state << 25U;
            state ^= state >> 27U;
            const std::uint32_t bits = static_cast<std::uint32_t>(state);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            if (!std::isfinite(value)) {
                value = static_cast<float>((bits & 0xFFFFU) + 1U) / 65536.0f;
            }
            mMemory[index] = value;
            mScatteredIndex[index] = static_cast<std::uint32_t>(index);
        }
        std::mt19937 generator(0xC001D00DU);
        std::shuffle(mScatteredIndex.begin(), mScatteredIndex.end(), generator);
    }

    void resetObservations() noexcept {
        for (WorkerObservation& worker : mWorkerObservations) {
            worker.taskCount = 0;
            worker.firstStartNs = std::numeric_limits<std::uint64_t>::max();
            worker.lastEndNs = 0;
        }
        for (TaskObservation& task : mTaskObservations) {
            task = {};
        }
    }

    void execute(WorkloadKind kind, std::uint64_t totalUnits,
            std::size_t logicalTasks, std::uint64_t task, std::size_t worker,
            std::uint64_t seed) noexcept {
        ++mWorkerObservations[worker].taskCount;
        const std::uint64_t begin = totalUnits * task / logicalTasks;
        const std::uint64_t end = totalUnits * (task + 1U) / logicalTasks;
        std::uint64_t accumulator = seed ^ (task * 0x9E37'79B9'7F4A'7C15ULL);

        switch (kind) {
            case WorkloadKind::NoOp:
                accumulator ^= task + 0xA5A5'A5A5U;
                break;
            case WorkloadKind::Arithmetic:
                for (std::uint64_t index = begin; index < end; ++index) {
                    accumulator ^= index + 0x9E37'79B9U;
                    accumulator *= 0xBF58'476D'1CE4'E5B9ULL;
                    accumulator = rotateLeft(accumulator, 17U);
                    accumulator += 0x94D0'49BB'1331'11EBULL;
                }
                break;
            case WorkloadKind::SequentialMemory:
                for (std::uint64_t index = begin; index < end; ++index) {
                    const std::size_t location =
                            static_cast<std::size_t>(index + seed) & (kMemoryElementCount - 1U);
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &mMemory[location], sizeof(bits));
                    accumulator ^= static_cast<std::uint64_t>(bits) + index;
                    accumulator = rotateLeft(accumulator, 11U) * 0x9E37'79B1U;
                }
                break;
            case WorkloadKind::ScatteredMemory:
                for (std::uint64_t index = begin; index < end; ++index) {
                    const std::size_t order =
                            static_cast<std::size_t>(index + seed) & (kMemoryElementCount - 1U);
                    const std::size_t location = mScatteredIndex[order];
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &mMemory[location], sizeof(bits));
                    accumulator ^= static_cast<std::uint64_t>(bits) + location;
                    accumulator = rotateLeft(accumulator, 13U) * 0x85EB'CA77U;
                }
                break;
        }
        mTaskResults[static_cast<std::size_t>(task)].value = accumulator;
    }

    std::uint64_t reduce(std::size_t logicalTasks) const noexcept {
        std::uint64_t result = 0xCBF2'9CE4'8422'2325ULL;
        for (std::size_t task = 0; task < logicalTasks; ++task) {
            result ^= mTaskResults[task].value;
            result *= 0x0000'0100'0000'01B3ULL;
        }
        return result;
    }

    TaskObservation& taskObservation(std::size_t task) noexcept {
        return mTaskObservations[task];
    }

    WorkerObservation& workerObservation(std::size_t worker) noexcept {
        return mWorkerObservations[worker];
    }

    const std::vector<TaskObservation>& taskObservations() const noexcept {
        return mTaskObservations;
    }

    const std::vector<WorkerObservation>& workerObservations() const noexcept {
        return mWorkerObservations;
    }

private:
    std::vector<float> mMemory;
    std::vector<std::uint32_t> mScatteredIndex;
    std::vector<TaskResult> mTaskResults;
    std::vector<TaskObservation> mTaskObservations;
    std::vector<WorkerObservation> mWorkerObservations;
};

struct ProcessStatus final {
    bool available = false;
    std::uint64_t virtualPeakKiB = 0;
    std::uint64_t virtualSizeKiB = 0;
    std::uint64_t residentHighWaterKiB = 0;
    std::uint64_t residentSizeKiB = 0;
    std::uint64_t threadCount = 0;
};

ProcessStatus readProcessStatus() noexcept {
    ProcessStatus result;
    try {
        std::ifstream status("/proc/self/status");
        if (!status) {
            return result;
        }

        auto parseValue = [](const std::string& line, std::uint64_t& destination) {
            std::istringstream fields(line.substr(line.find(':') + 1U));
            fields >> destination;
        };
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("VmPeak:", 0) == 0) {
                parseValue(line, result.virtualPeakKiB);
            } else if (line.rfind("VmSize:", 0) == 0) {
                parseValue(line, result.virtualSizeKiB);
            } else if (line.rfind("VmHWM:", 0) == 0) {
                parseValue(line, result.residentHighWaterKiB);
            } else if (line.rfind("VmRSS:", 0) == 0) {
                parseValue(line, result.residentSizeKiB);
            } else if (line.rfind("Threads:", 0) == 0) {
                parseValue(line, result.threadCount);
            }
        }
        result.available = true;
    } catch (...) {
        return {};
    }
    return result;
}

PerfJson processStatusJson(const ProcessStatus& status) {
    if (!status.available) {
        return {{"available", false}};
    }
    return {
            {"available", true},
            {"virtualPeakKiB", status.virtualPeakKiB},
            {"virtualSizeKiB", status.virtualSizeKiB},
            {"residentHighWaterKiB", status.residentHighWaterKiB},
            {"residentSizeKiB", status.residentSizeKiB},
            {"threadCount", status.threadCount},
    };
}

PerfJson processStatusDeltaJson(const ProcessStatus& after,
        const ProcessStatus& before) {
    if (!after.available || !before.available) {
        return {{"available", false}};
    }
    auto delta = [](std::uint64_t newer, std::uint64_t older) {
        return static_cast<std::int64_t>(newer) - static_cast<std::int64_t>(older);
    };
    return {
            {"available", true},
            {"virtualPeakKiB", delta(after.virtualPeakKiB, before.virtualPeakKiB)},
            {"virtualSizeKiB", delta(after.virtualSizeKiB, before.virtualSizeKiB)},
            {"residentHighWaterKiB",
                    delta(after.residentHighWaterKiB, before.residentHighWaterKiB)},
            {"residentSizeKiB", delta(after.residentSizeKiB, before.residentSizeKiB)},
            {"threadCount", delta(after.threadCount, before.threadCount)},
    };
}

struct TimedRun final {
    std::uint64_t runNs = 0;
    std::uint64_t reductionNs = 0;
    std::uint64_t totalNs = 0;
    std::uint64_t checksum = 0;
    std::size_t observedWorkers = 0;
};

TimedRun runDirect(WorkloadStorage& storage, WorkloadKind kind,
        std::uint64_t totalUnits, std::size_t logicalTasks,
        std::uint64_t seed) {
    storage.resetObservations();
    const auto runStarted = Clock::now();
    for (std::size_t task = 0; task < logicalTasks; ++task) {
        storage.execute(kind, totalUnits, logicalTasks, task, 0, seed);
    }
    const auto runFinished = Clock::now();
    const auto reductionStarted = Clock::now();
    const std::uint64_t checksum = storage.reduce(logicalTasks);
    const auto reductionFinished = Clock::now();
    gBenchmarkSink.fetch_xor(checksum, std::memory_order_relaxed);
    return {nanosBetween(runStarted, runFinished),
            nanosBetween(reductionStarted, reductionFinished),
            nanosBetween(runStarted, reductionFinished), checksum, 1};
}

TimedRun runPool(hwy::ThreadPool& pool, WorkloadStorage& storage,
        WorkloadKind kind, std::uint64_t totalUnits,
        std::size_t logicalTasks, std::uint64_t seed) {
    storage.resetObservations();
    const auto runStarted = Clock::now();
    pool.Run(
            0, logicalTasks, [&](std::uint64_t task, std::size_t worker) noexcept {
                storage.execute(kind, totalUnits, logicalTasks, task, worker, seed);
            });
    const auto runFinished = Clock::now();
    const auto reductionStarted = Clock::now();
    const std::uint64_t checksum = storage.reduce(logicalTasks);
    const auto reductionFinished = Clock::now();
    std::size_t observedWorkers = 0;
    for (std::size_t worker = 0; worker < pool.NumWorkers(); ++worker) {
        observedWorkers += storage.workerObservations()[worker].taskCount != 0;
    }
    gBenchmarkSink.fetch_xor(checksum, std::memory_order_relaxed);
    return {nanosBetween(runStarted, runFinished),
            nanosBetween(reductionStarted, reductionFinished),
            nanosBetween(runStarted, reductionFinished), checksum,
            observedWorkers};
}

struct DecomposedRun final {
    std::uint64_t totalRunNs = 0;
    std::uint64_t publishToMainTaskNs = 0;
    bool mainTaskObserved = false;
    std::uint64_t publishToFirstWorkerTaskNs = 0;
    bool workerTaskObserved = false;
    std::uint64_t usefulTaskEnvelopeNs = 0;
    std::uint64_t summedTaskNs = 0;
    std::uint64_t longestTaskNs = 0;
    std::uint64_t barrierTailNs = 0;
    std::size_t observedWorkers = 0;
    std::uint64_t checksum = 0;
};

DecomposedRun runPoolDecomposed(hwy::ThreadPool& pool, WorkloadStorage& storage,
        WorkloadKind kind, std::uint64_t totalUnits,
        std::size_t logicalTasks, std::uint64_t seed) {
    storage.resetObservations();
    const auto runStarted = Clock::now();
    pool.Run(
            0, logicalTasks, [&](std::uint64_t task, std::size_t worker) noexcept {
                const auto taskStarted = Clock::now();
                const std::uint64_t startNs = nanosBetween(runStarted, taskStarted);
                TaskObservation& taskObservation =
                        storage.taskObservation(static_cast<std::size_t>(task));
                taskObservation.startNs = startNs;
                WorkerObservation& workerObservation =
                        storage.workerObservation(worker);
                workerObservation.firstStartNs =
                        std::min(workerObservation.firstStartNs, startNs);
                storage.execute(kind, totalUnits, logicalTasks, task, worker, seed);
                const auto taskFinished = Clock::now();
                const std::uint64_t endNs = nanosBetween(runStarted, taskFinished);
                taskObservation.endNs = endNs;
                workerObservation.lastEndNs =
                        std::max(workerObservation.lastEndNs, endNs);
            });
    const auto runFinished = Clock::now();
    const std::uint64_t checksum = storage.reduce(logicalTasks);
    gBenchmarkSink.fetch_xor(checksum, std::memory_order_relaxed);

    std::uint64_t firstTask = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastTask = 0;
    std::uint64_t summedTask = 0;
    std::uint64_t longestTask = 0;
    for (std::size_t task = 0; task < logicalTasks; ++task) {
        const TaskObservation& observation = storage.taskObservations()[task];
        firstTask = std::min(firstTask, observation.startNs);
        lastTask = std::max(lastTask, observation.endNs);
        const std::uint64_t taskNs = observation.endNs - observation.startNs;
        summedTask += taskNs;
        longestTask = std::max(longestTask, taskNs);
    }

    std::size_t observedWorkers = 0;
    std::uint64_t firstWorkerTask = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t worker = 0; worker < pool.NumWorkers(); ++worker) {
        const WorkerObservation& observation = storage.workerObservations()[worker];
        if (observation.taskCount == 0) {
            continue;
        }
        ++observedWorkers;
        if (worker != 0) {
            firstWorkerTask = std::min(firstWorkerTask, observation.firstStartNs);
        }
    }

    const std::uint64_t totalRunNs = nanosBetween(runStarted, runFinished);
    const bool workerObserved =
            firstWorkerTask != std::numeric_limits<std::uint64_t>::max();
    const bool mainObserved = storage.workerObservations()[0].firstStartNs != std::numeric_limits<std::uint64_t>::max();
    return {totalRunNs,
            mainObserved ? storage.workerObservations()[0].firstStartNs : 0,
            mainObserved,
            workerObserved ? firstWorkerTask : 0,
            workerObserved,
            lastTask >= firstTask ? lastTask - firstTask : 0,
            summedTask,
            longestTask,
            totalRunNs >= lastTask ? totalRunNs - lastTask : 0,
            observedWorkers,
            checksum};
}

struct Calibration final {
    WorkloadKind kind = WorkloadKind::NoOp;
    std::size_t targetMicroseconds = 0;
    std::uint64_t totalUnits = 0;
    double measuredP50Ns = 0.0;
};

std::uint64_t roundedCalibrationUnits(long double value) {
    const long double bounded =
            std::clamp(value, static_cast<long double>(kCalibrationMinimumUnits),
                    static_cast<long double>(kCalibrationMaximumUnits));
    const std::uint64_t units = static_cast<std::uint64_t>(bounded);
    return std::max(kCalibrationMinimumUnits, (units + 63U) & ~std::uint64_t{63});
}

Calibration calibrateWorkload(WorkloadStorage& storage, WorkloadKind kind,
        std::size_t targetMicroseconds) {
    const long double targetNs =
            static_cast<long double>(targetMicroseconds) * 1000.0L;
    std::uint64_t units = kCalibrationMinimumUnits;
    auto measure = [&] {
        std::vector<std::uint64_t> samples;
        samples.reserve(9);
        for (int sample = 0; sample < 9; ++sample) {
            samples.push_back(runDirect(storage, kind, units, 1,
                    kLabSeed + static_cast<std::uint64_t>(sample))
                            .runNs);
        }
        return summarize(samples);
    };

    for (int iteration = 0; iteration < 10; ++iteration) {
        const Distribution measured = measure();
        if (measured.p50 <= 0.0) {
            units = std::min(kCalibrationMaximumUnits, units * 4U);
            continue;
        }
        const long double ratio = std::clamp(targetNs / measured.p50, 0.25L, 4.0L);
        const std::uint64_t adjusted = roundedCalibrationUnits(units * ratio);
        const long double relativeError =
                std::abs(static_cast<long double>(measured.p50) - targetNs) / targetNs;
        if (relativeError <= 0.08L || adjusted == units) {
            break;
        }
        units = adjusted;
    }
    const Distribution measured = measure();
    return {kind, targetMicroseconds, units, measured.p50};
}

std::vector<Calibration>
calibrateWorkloads(WorkloadStorage& storage,
        const std::vector<std::size_t>& targetMicroseconds) {
    std::vector<Calibration> result;
    result.reserve(targetMicroseconds.size() * 3U);
    for (WorkloadKind kind :
            {WorkloadKind::Arithmetic, WorkloadKind::SequentialMemory,
                    WorkloadKind::ScatteredMemory}) {
        for (const std::size_t target : targetMicroseconds) {
            result.push_back(calibrateWorkload(storage, kind, target));
        }
    }
    return result;
}

struct CaseSpec final {
    WorkloadKind kind = WorkloadKind::NoOp;
    std::size_t targetMicroseconds = 0;
    std::uint64_t totalUnits = 0;
    std::size_t logicalTasks = 1;
};

std::vector<CaseSpec>
makeCaseSpecs(const Options& options,
        const std::vector<Calibration>& calibrations) {
    std::vector<CaseSpec> noOp;
    std::vector<CaseSpec> work;
    noOp.reserve(options.logicalTaskCounts.size() * (calibrations.size() + 1U));
    work.reserve(options.logicalTaskCounts.size() * calibrations.size());
    for (const std::size_t tasks : options.logicalTaskCounts) {
        noOp.push_back({WorkloadKind::NoOp, 0, 0, tasks});
    }
    for (const Calibration& calibration : calibrations) {
        for (const std::size_t tasks : options.logicalTaskCounts) {
            work.push_back({calibration.kind, calibration.targetMicroseconds,
                    calibration.totalUnits, tasks});
        }
    }
    std::mt19937 generator(0xC00'2026U);
    std::shuffle(work.begin(), work.end(), generator);
    noOp.insert(noOp.end(), work.begin(), work.end());
    return noOp;
}

class RenderInterference final {
public:
    RenderInterference(InterferenceMode mode, const std::string& substratePath)
            : mMode(mode) {
        if (mMode == InterferenceMode::None) {
            return;
        }
        Environment fallback =
                Environment::constructDefaultEnvironment(1000, 1000, 100.0);
        NativeSubstrateLoadResult loaded =
                loadNativeSubstrateFromPath(substratePath, false, fallback);
        mWorld = std::move(loaded.world);
        mFormatVersion = loaded.familyLocalVersion;
        mPipeline = std::make_unique<
                cellularity::microscope::MicroscopeRenderFramePipeline>(
                mWorld->mEnvironment.maxCellCount);
        mThread = std::thread([this] {
            run();
        });

        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (mCompletedCaptures.load(std::memory_order_acquire) < 2 && !mFailed.load(std::memory_order_acquire) && Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (mFailed.load(std::memory_order_acquire) || mCompletedCaptures.load(std::memory_order_acquire) < 2) {
            stop();
            throw std::runtime_error(
                    "Render interference failed to complete its warm-up captures");
        }
    }

    ~RenderInterference() {
        stop();
    }

    RenderInterference(const RenderInterference&) = delete;
    RenderInterference& operator=(const RenderInterference&) = delete;

    PerfJson snapshot() const {
        PerfJson result = {
                {"mode", interferenceName(mMode)},
                {"substrateFormatVersion", mFormatVersion},
                {"submittedCaptures", mSubmittedCaptures.load()},
                {"completedCaptures", mCompletedCaptures.load()},
                {"failed", mFailed.load()},
                {"cadence", mMode == InterferenceMode::MovingCamera ? "moving viewport at up to 60 Hz" : "continuous capture-and-prepare saturation"},
        };
        if (mPipeline) {
            const auto failure = mPipeline->failure();
            result["pipelineFailureKind"] = static_cast<int>(failure.kind);
            result["pipelineFailureStage"] = static_cast<int>(failure.stage);
        }
        return result;
    }

private:
    void stop() noexcept {
        mStop.store(true, std::memory_order_release);
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    void run() noexcept {
        cellularity::platform::setCurrentThreadName("DispatcherFeed");
        try {
            const float radius = static_cast<float>(mWorld->mEnvironment.mRadius);
            std::uint64_t revision = 1;
            auto nextCapture = Clock::now();
            while (!mStop.load(std::memory_order_acquire)) {
                const float phase = static_cast<float>(revision % 240U) / 240.0f;
                const float cameraX =
                        mMode == InterferenceMode::MovingCamera
                        ? std::sin(phase * 6.28318530718f) * radius * 0.2f
                        : 0.0f;
                const float cameraY =
                        mMode == InterferenceMode::MovingCamera
                        ? std::cos(phase * 6.28318530718f) * radius * 0.2f
                        : 0.0f;
                const float halfWidth = radius * 0.55f;
                const cellularity::render::RenderViewBounds view{
                        cameraX - halfWidth, cameraX + halfWidth,
                        cameraY - halfWidth * 1390.0f / 2560.0f,
                        cameraY + halfWidth * 1390.0f / 2560.0f
                };
                cellularity::render::RenderFrameCaptureMetadata metadata;
                metadata.worldEpoch = 1;
                metadata.modeGeneration = 1;
                metadata.completedUpdateId = revision;
                metadata.stage =
                        cellularity::render::RenderCaptureStage::AfterCompletedUpdate;
                metadata.environmentRevision = 1;
                metadata.viewport.revision = revision;
                metadata.viewport.coverageRevision = revision;
                metadata.viewport.visibleBounds = view;
                metadata.viewport.coverageBounds = view;
                metadata.viewport.cameraX = cameraX;
                metadata.viewport.cameraY = cameraY;
                metadata.viewport.zoom = 1.0f;
                metadata.viewport.viewWidth = halfWidth * 2.0f;
                metadata.viewport.viewHeight = halfWidth * 2.0f * 1390.0f / 2560.0f;
                metadata.viewport.surfaceWidth = 2560;
                metadata.viewport.surfaceHeight = 1390;
                mSubmittedCaptures.fetch_add(1, std::memory_order_relaxed);
                (void)mPipeline->captureAndPrepare(*mWorld, view, metadata);
                (void)mPipeline->acquireLatestPreparedFrame();
                mCompletedCaptures.fetch_add(1, std::memory_order_release);
                ++revision;

                if (mMode == InterferenceMode::MovingCamera) {
                    nextCapture += std::chrono::nanoseconds(1'000'000'000LL / 60);
                    std::this_thread::sleep_until(nextCapture);
                    if (nextCapture + std::chrono::seconds(1) < Clock::now()) {
                        nextCapture = Clock::now();
                    }
                }
            }
        } catch (...) {
            mFailed.store(true, std::memory_order_release);
        }
    }

    InterferenceMode mMode;
    std::unique_ptr<SimulationWorld> mWorld;
    std::unique_ptr<cellularity::microscope::MicroscopeRenderFramePipeline>
            mPipeline;
    int mFormatVersion = 0;
    std::atomic<bool> mStop{false};
    std::atomic<bool> mFailed{false};
    std::atomic<std::uint64_t> mSubmittedCaptures{0};
    std::atomic<std::uint64_t> mCompletedCaptures{0};
    std::thread mThread;
};

struct SampleSeries final {
    std::vector<std::uint64_t> runNs;
    std::vector<std::uint64_t> reductionNs;
    std::vector<std::uint64_t> totalNs;
    std::vector<std::uint64_t> observedWorkers;
};

struct NoOpReference final {
    double directRunP50Ns = 0.0;
    double poolRunP50Ns = 0.0;
};

struct DecisionRecord final {
    int pass = 0;
    std::size_t additionalThreads = 0;
    WaitMode waitMode = WaitMode::Block;
    WorkloadKind workload = WorkloadKind::NoOp;
    std::size_t targetMicroseconds = 0;
    std::size_t logicalTasks = 0;
    double directTotalP50Ns = 0.0;
    double poolTotalP50Ns = 0.0;
    double pairedSpeedupP50Percent = 0.0;
    double acceptanceRatio = 0.0;
    bool acceptancePassed = false;
};

struct CaseRunResult final {
    PerfJson json;
    NoOpReference noOp;
    DecisionRecord decision;
};

void reserveSeries(SampleSeries& series, std::size_t count) {
    series.runNs.reserve(count);
    series.reductionNs.reserve(count);
    series.totalNs.reserve(count);
    series.observedWorkers.reserve(count);
}

void appendRun(SampleSeries& series, const TimedRun& run) {
    series.runNs.push_back(run.runNs);
    series.reductionNs.push_back(run.reductionNs);
    series.totalNs.push_back(run.totalNs);
    series.observedWorkers.push_back(run.observedWorkers);
}

PerfJson sampleSeriesJson(const SampleSeries& series, bool includeRaw) {
    return {{"run", distributionJson(series.runNs, includeRaw)},
            {"reduction", distributionJson(series.reductionNs, includeRaw)},
            {"total", distributionJson(series.totalNs, includeRaw)},
            {"observedWorkerCount",
                    distributionJson(series.observedWorkers, includeRaw)}};
}

CaseRunResult runCase(const Options& options, int pass,
        std::size_t additionalThreads, WaitMode waitMode,
        hwy::ThreadPool& pool, WorkloadStorage& storage,
        const CaseSpec& spec,
        const NoOpReference& noOpReference) {
    for (int warmup = 0; warmup < options.caseWarmups; ++warmup) {
        const std::uint64_t seed =
                kLabSeed + static_cast<std::uint64_t>(pass * 10'000 + warmup);
        const TimedRun direct =
                runDirect(storage, spec.kind, spec.totalUnits, spec.logicalTasks, seed);
        const TimedRun parallel = runPool(pool, storage, spec.kind, spec.totalUnits,
                spec.logicalTasks, seed);
        if (direct.checksum != parallel.checksum) {
            throw std::runtime_error("Dispatcher warm-up checksum mismatch");
        }
    }

    SampleSeries directSamples;
    SampleSeries poolSamples;
    reserveSeries(directSamples, static_cast<std::size_t>(options.samples));
    reserveSeries(poolSamples, static_cast<std::size_t>(options.samples));
    std::vector<double> pairedSpeedupPercent;
    pairedSpeedupPercent.reserve(static_cast<std::size_t>(options.samples));

    for (int sample = 0; sample < options.samples; ++sample) {
        const std::uint64_t seed = kLabSeed + static_cast<std::uint64_t>(pass) * 1'000'000ULL + static_cast<std::uint64_t>(sample) * 17U + static_cast<std::uint64_t>(spec.logicalTasks);
        TimedRun direct;
        TimedRun parallel;
        if ((sample & 1) == 0) {
            direct = runDirect(storage, spec.kind, spec.totalUnits, spec.logicalTasks,
                    seed);
            parallel = runPool(pool, storage, spec.kind, spec.totalUnits,
                    spec.logicalTasks, seed);
        } else {
            parallel = runPool(pool, storage, spec.kind, spec.totalUnits,
                    spec.logicalTasks, seed);
            direct = runDirect(storage, spec.kind, spec.totalUnits, spec.logicalTasks,
                    seed);
        }
        if (direct.checksum != parallel.checksum) {
            throw std::runtime_error("Dispatcher measured checksum mismatch");
        }
        appendRun(directSamples, direct);
        appendRun(poolSamples, parallel);
        pairedSpeedupPercent.push_back(
                parallel.totalNs == 0 ? 0.0
                                      : (static_cast<double>(direct.totalNs) / static_cast<double>(parallel.totalNs) - 1.0) * 100.0);
    }

    std::vector<std::uint64_t> decompositionTotal;
    std::vector<std::uint64_t> publishToMain;
    std::vector<std::uint64_t> publishToWorker;
    std::vector<std::uint64_t> usefulEnvelope;
    std::vector<std::uint64_t> summedTask;
    std::vector<std::uint64_t> longestTask;
    std::vector<std::uint64_t> barrierTail;
    std::vector<std::uint64_t> decompositionWorkers;
    std::size_t mainDispatchSamples = 0;
    std::size_t workerWakeSamples = 0;
    for (auto* samples :
            {&decompositionTotal, &publishToMain, &publishToWorker, &usefulEnvelope,
                    &summedTask, &longestTask, &barrierTail, &decompositionWorkers}) {
        samples->reserve(static_cast<std::size_t>(options.decompositionSamples));
    }
    for (int sample = 0; sample < options.decompositionSamples; ++sample) {
        const std::uint64_t seed =
                kLabSeed + 0xD3C0'0000ULL + static_cast<std::uint64_t>(sample);
        const TimedRun reference =
                runDirect(storage, spec.kind, spec.totalUnits, spec.logicalTasks, seed);
        const DecomposedRun decomposed = runPoolDecomposed(
                pool, storage, spec.kind, spec.totalUnits, spec.logicalTasks, seed);
        if (reference.checksum != decomposed.checksum) {
            throw std::runtime_error("Dispatcher decomposition checksum mismatch");
        }
        decompositionTotal.push_back(decomposed.totalRunNs);
        if (decomposed.mainTaskObserved) {
            publishToMain.push_back(decomposed.publishToMainTaskNs);
            ++mainDispatchSamples;
        }
        if (decomposed.workerTaskObserved) {
            publishToWorker.push_back(decomposed.publishToFirstWorkerTaskNs);
            ++workerWakeSamples;
        }
        usefulEnvelope.push_back(decomposed.usefulTaskEnvelopeNs);
        summedTask.push_back(decomposed.summedTaskNs);
        longestTask.push_back(decomposed.longestTaskNs);
        barrierTail.push_back(decomposed.barrierTailNs);
        decompositionWorkers.push_back(decomposed.observedWorkers);
    }

    const Distribution directRun = summarize(directSamples.runNs);
    const Distribution directTotal = summarize(directSamples.totalNs);
    const Distribution poolRun = summarize(poolSamples.runNs);
    const Distribution poolReduction = summarize(poolSamples.reductionNs);
    const Distribution poolTotal = summarize(poolSamples.totalNs);
    const PerfJson speedupJson =
            signedDistributionJson(pairedSpeedupPercent, options.rawSamples);
    const double pairedSpeedupP50 = speedupJson.value("p50", 0.0);

    NoOpReference effectiveNoOp = noOpReference;
    if (spec.kind == WorkloadKind::NoOp) {
        effectiveNoOp.directRunP50Ns = directRun.p50;
        effectiveNoOp.poolRunP50Ns = poolRun.p50;
    }

    PerfJson acceptance;
    double acceptanceRatio = 0.0;
    bool acceptancePassed = false;
    if (spec.kind == WorkloadKind::NoOp) {
        acceptance = {{"applicable", false},
                {"reason", "no-op establishes dispatch/barrier cost"}};
    } else {
        const double serialUsefulNs =
                std::max(0.0, directRun.p50 - effectiveNoOp.directRunP50Ns);
        const double parallelUsefulNs =
                std::max(0.0, poolRun.p50 - effectiveNoOp.poolRunP50Ns);
        const double usefulWorkSavingsNs = serialUsefulNs - parallelUsefulNs;
        const double dispatchBarrierNs = std::max(
                0.0, effectiveNoOp.poolRunP50Ns - effectiveNoOp.directRunP50Ns);
        const double requiredOverheadNs = dispatchBarrierNs + poolReduction.p50;
        acceptanceRatio = requiredOverheadNs > 0.0
                ? usefulWorkSavingsNs / requiredOverheadNs
                : (usefulWorkSavingsNs > 0.0
                                  ? std::numeric_limits<double>::infinity()
                                  : 0.0);
        acceptancePassed = additionalThreads > 0 && usefulWorkSavingsNs > 0.0 && acceptanceRatio >= 2.0 && poolTotal.p50 < directTotal.p50;
        acceptance = {
                {"applicable", additionalThreads > 0},
                {"passed", acceptancePassed},
                {"serialUsefulEstimateNs", serialUsefulNs},
                {"parallelUsefulEstimateNs", parallelUsefulNs},
                {"expectedUsefulWorkSavingsNs", usefulWorkSavingsNs},
                {"dispatchAndBarrierCostNs", dispatchBarrierNs},
                {"reductionCostNs", poolReduction.p50},
                {"totalRequiredOverheadNs", requiredOverheadNs},
                {"savingsToOverheadRatio", std::isfinite(acceptanceRatio) ? PerfJson(acceptanceRatio) : PerfJson("infinite")},
                {"requiredRatio", 2.0},
                {"method",
                        "non-instrumented work minus same-task no-op; conservative full "
                        "parallel reduction cost"},
        };
    }

    PerfJson target = nullptr;
    if (spec.kind != WorkloadKind::NoOp) {
        target = spec.targetMicroseconds;
    }
    PerfJson decomposition = {
            {"instrumented", true},
            {"totalRun", distributionJson(decompositionTotal, options.rawSamples)},
            {"publishToMainTask",
                    distributionJson(publishToMain, options.rawSamples)},
            {"mainDispatchObservedSamples", mainDispatchSamples},
            {"publishToFirstWorkerTask",
                    distributionJson(publishToWorker, options.rawSamples)},
            {"workerWakeObservedSamples", workerWakeSamples},
            {"usefulTaskEnvelope",
                    distributionJson(usefulEnvelope, options.rawSamples)},
            {"summedTaskTime", distributionJson(summedTask, options.rawSamples)},
            {"longestTask", distributionJson(longestTask, options.rawSamples)},
            {"completionBarrierTail",
                    distributionJson(barrierTail, options.rawSamples)},
            {"observedWorkerCount",
                    distributionJson(decompositionWorkers, options.rawSamples)},
            {"interpretation",
                    "Highway public API exposes one synchronous Run boundary. "
                    "publishToFirstWorkerTask therefore combines publication and wake; "
                    "completionBarrierTail is time after the final instrumented task and "
                    "includes barrier return. Timestamp overhead is excluded from the "
                    "acceptance calculation."},
    };

    PerfJson result = {
            {"workload", workloadName(spec.kind)},
            {"targetMicroseconds", target},
            {"totalWorkUnits", spec.totalUnits},
            {"logicalTaskCount", spec.logicalTasks},
            {"direct", sampleSeriesJson(directSamples, options.rawSamples)},
            {"highway", sampleSeriesJson(poolSamples, options.rawSamples)},
            {"pairedTotalSpeedupPercent", speedupJson},
            {"decomposition", std::move(decomposition)},
            {"acceptance", std::move(acceptance)},
            {"checksumMatched", true},
            {"serialMeasurementContext",
                    waitMode == WaitMode::Spin
                            ? "serial call runs while bounded lab spin workers are resident"
                            : "serial call runs while block-mode pool is resident"},
    };

    DecisionRecord decision{pass,
            additionalThreads,
            waitMode,
            spec.kind,
            spec.targetMicroseconds,
            spec.logicalTasks,
            directTotal.p50,
            poolTotal.p50,
            pairedSpeedupP50,
            acceptanceRatio,
            acceptancePassed};
    return {std::move(result), effectiveNoOp, decision};
}

struct AutotuneResult final {
    bool applicable = false;
    bool completed = false;
    int runCount = 0;
    std::uint64_t firstRunNs = 0;
    std::uint64_t totalNs = 0;
    std::string selectedConfig;
};

AutotuneResult autotunePool(hwy::ThreadPool& pool, WorkloadStorage& storage,
        const Calibration& calibration) {
    AutotuneResult result;
    result.applicable = pool.NumWorkers() > 1;
    const std::size_t tasks = std::max<std::size_t>(32, pool.NumWorkers() * 4U);
    const auto autotuneStarted = Clock::now();
    auto runOne = [&](int runIndex) {
        const auto started = Clock::now();
        pool.Run(0, tasks, [&](std::uint64_t task, std::size_t worker) noexcept {
            storage.execute(calibration.kind, calibration.totalUnits, tasks, task,
                    worker, kLabSeed + static_cast<std::uint64_t>(runIndex));
        });
        const auto finished = Clock::now();
        const std::uint64_t checksum = storage.reduce(tasks);
        gBenchmarkSink.fetch_xor(checksum, std::memory_order_relaxed);
        return nanosBetween(started, finished);
    };

    result.firstRunNs = runOne(0);
    result.runCount = 1;
    if (result.applicable) {
        while (!pool.AutoTuneComplete() && result.runCount < kMaximumAutotuneRuns) {
            (void)runOne(result.runCount);
            ++result.runCount;
        }
        result.completed = pool.AutoTuneComplete();
        if (!result.completed) {
            throw std::runtime_error("Highway thread-pool autotune did not converge");
        }
    } else {
        // Highway intentionally bypasses its autotuner for a zero-worker pool.
        result.completed = false;
    }
    result.totalNs = nanosBetween(autotuneStarted, Clock::now());
    result.selectedConfig = pool.config().ToString();
    return result;
}

struct PoolInstanceResult final {
    PerfJson json;
    std::vector<DecisionRecord> decisions;
};

PoolInstanceResult runPoolInstance(const Options& options, int pass,
        std::size_t additionalThreads,
        WaitMode waitMode,
        const std::vector<CaseSpec>& caseSpecs,
        const Calibration& autotuneCalibration,
        WorkloadStorage& storage) {
    const auto constructionStarted = Clock::now();
    auto pool = std::make_unique<hwy::ThreadPool>(additionalThreads);
    const std::uint64_t constructionNs =
            nanosBetween(constructionStarted, Clock::now());
    if (pool->NumWorkers() != additionalThreads + 1U) {
        throw std::runtime_error("Highway constructed an unexpected worker count");
    }

    std::uint64_t waitModeTransitionNs = 0;
    if (waitMode == WaitMode::Spin) {
        const auto transitionStarted = Clock::now();
        pool->SetWaitMode(highwayWaitMode(waitMode));
        waitModeTransitionNs = nanosBetween(transitionStarted, Clock::now());
    }

    const AutotuneResult autotune =
            autotunePool(*pool, storage, autotuneCalibration);
    PerfJson cases = PerfJson::array();
    std::vector<DecisionRecord> decisions;
    decisions.reserve(caseSpecs.size());
    std::vector<NoOpReference> noOpReferences(kMaximumLogicalTasks + 1U);
    for (const CaseSpec& spec : caseSpecs) {
        const NoOpReference reference = noOpReferences[spec.logicalTasks];
        CaseRunResult result = runCase(options, pass, additionalThreads, waitMode,
                *pool, storage, spec, reference);
        if (spec.kind == WorkloadKind::NoOp) {
            noOpReferences[spec.logicalTasks] = result.noOp;
        }
        cases.push_back(std::move(result.json));
        decisions.push_back(result.decision);
    }

    const std::string finalConfig = pool->config().ToString();
    const auto teardownStarted = Clock::now();
    pool.reset();
    const std::uint64_t teardownNs = nanosBetween(teardownStarted, Clock::now());

    PerfJson result = {
            {"pass", pass},
            {"additionalThreadCount", additionalThreads},
            {"participantCount", additionalThreads + 1U},
            {"waitMode", waitModeName(waitMode)},
            {"boundedSpinAblation", waitMode == WaitMode::Spin},
            {"lifecycle",
                    {{"constructionNs", constructionNs},
                            {"waitModeTransitionNs", waitModeTransitionNs},
                            {"firstRunNs", autotune.firstRunNs},
                            {"autotuneApplicable", autotune.applicable},
                            {"autotuneCompleted", autotune.completed},
                            {"autotuneRunCount", autotune.runCount},
                            {"autotuneTotalNs", autotune.totalNs},
                            {"selectedConfig", autotune.selectedConfig},
                            {"finalConfig", finalConfig},
                            {"teardownNs", teardownNs}}},
            {"cases", std::move(cases)},
    };
    return {std::move(result), std::move(decisions)};
}

PerfJson runLifecycleMatrix(const Options& options,
        const std::vector<std::size_t>& additionalCounts,
        WorkloadStorage& storage) {
    PerfJson matrix = PerfJson::array();
    for (const std::size_t additionalThreads : additionalCounts) {
        for (const WaitMode waitMode : options.waitModes) {
            std::vector<std::uint64_t> construction;
            std::vector<std::uint64_t> modeTransition;
            std::vector<std::uint64_t> firstRun;
            std::vector<std::uint64_t> teardown;
            const std::size_t tasks =
                    std::max<std::size_t>(2, additionalThreads + 1U);
            for (int sample = 0; sample < options.lifecycleSamples; ++sample) {
                const auto constructionStarted = Clock::now();
                auto pool = std::make_unique<hwy::ThreadPool>(additionalThreads);
                construction.push_back(nanosBetween(constructionStarted, Clock::now()));

                const auto transitionStarted = Clock::now();
                if (waitMode == WaitMode::Spin) {
                    pool->SetWaitMode(highwayWaitMode(waitMode));
                }
                modeTransition.push_back(nanosBetween(transitionStarted, Clock::now()));

                storage.resetObservations();
                const auto firstRunStarted = Clock::now();
                pool->Run(
                        0, tasks, [&](std::uint64_t task, std::size_t worker) noexcept {
                            storage.execute(WorkloadKind::NoOp, 0, tasks, task, worker,
                                    kLabSeed + static_cast<std::uint64_t>(sample));
                        });
                firstRun.push_back(nanosBetween(firstRunStarted, Clock::now()));
                gBenchmarkSink.fetch_xor(storage.reduce(tasks),
                        std::memory_order_relaxed);

                const auto teardownStarted = Clock::now();
                pool.reset();
                teardown.push_back(nanosBetween(teardownStarted, Clock::now()));
            }
            matrix.push_back({
                    {"additionalThreadCount", additionalThreads},
                    {"participantCount", additionalThreads + 1U},
                    {"waitMode", waitModeName(waitMode)},
                    {"sampleCount", options.lifecycleSamples},
                    {"construction", distributionJson(construction, options.rawSamples)},
                    {"waitModeTransition",
                            distributionJson(modeTransition, options.rawSamples)},
                    {"firstRun", distributionJson(firstRun, options.rawSamples)},
                    {"teardown", distributionJson(teardown, options.rawSamples)},
            });
        }
    }
    return matrix;
}

PerfJson runPoolMemoryMatrix(const std::vector<std::size_t>& additionalCounts,
        WorkloadStorage& storage) {
    PerfJson matrix = PerfJson::array();
    for (const std::size_t additionalThreads : additionalCounts) {
        const ProcessStatus beforeConstruction = readProcessStatus();
        ProcessStatus afterConstruction;
        ProcessStatus afterFirstRun;
        {
            auto pool = std::make_unique<hwy::ThreadPool>(additionalThreads);
            afterConstruction = readProcessStatus();

            const std::size_t tasks =
                    std::max<std::size_t>(2, additionalThreads + 1U);
            storage.resetObservations();
            pool->Run(0, tasks, [&](std::uint64_t task, std::size_t worker) noexcept {
                storage.execute(WorkloadKind::NoOp, 0, tasks, task, worker,
                        kLabSeed + additionalThreads);
            });
            gBenchmarkSink.fetch_xor(storage.reduce(tasks),
                    std::memory_order_relaxed);
            afterFirstRun = readProcessStatus();
        }
        const ProcessStatus afterTeardown = readProcessStatus();

        matrix.push_back({
                {"additionalThreadCount", additionalThreads},
                {"participantCount", additionalThreads + 1U},
                {"waitMode", "block"},
                {"measurementScope",
                        "process /proc/self/status; deltas include Highway worker stack, TLS "
                        "and internal pool storage"},
                {"beforeConstruction", processStatusJson(beforeConstruction)},
                {"afterConstruction", processStatusJson(afterConstruction)},
                {"afterFirstRun", processStatusJson(afterFirstRun)},
                {"afterTeardown", processStatusJson(afterTeardown)},
                {"constructionDelta",
                        processStatusDeltaJson(afterConstruction, beforeConstruction)},
                {"firstRunDelta",
                        processStatusDeltaJson(afterFirstRun, beforeConstruction)},
                {"teardownResidual",
                        processStatusDeltaJson(afterTeardown, beforeConstruction)},
        });
    }
    return matrix;
}

double medianDouble(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2U;
    return values.size() % 2U == 0 ? (values[middle - 1U] + values[middle]) * 0.5
                                   : values[middle];
}

std::vector<double> representativeSpeedups(
        const std::vector<DecisionRecord>& decisions, std::size_t additionalThreads,
        std::size_t targetMicroseconds, std::size_t logicalTasks) {
    std::vector<double> result;
    for (const DecisionRecord& decision : decisions) {
        if (decision.waitMode == WaitMode::Block && decision.additionalThreads == additionalThreads && decision.workload == WorkloadKind::Arithmetic && decision.targetMicroseconds == targetMicroseconds && decision.logicalTasks == logicalTasks) {
            result.push_back(decision.pairedSpeedupP50Percent);
        }
    }
    return result;
}

PerfJson extensionDecisionJson(const Options& options,
        const DispatcherCapacity& capacity,
        const std::vector<DecisionRecord>& decisions,
        bool& extend) {
    extend = false;
    PerfJson result = {
            {"eligible", !options.customAdditionalCounts && capacity.safeMaxAdditionalThreads > 4},
            {"extended", false},
            {"criterion",
                    "block-mode arithmetic at the longest calibrated duration and 128 "
                    "logical tasks; participant 5 must improve normalized paired median "
                    "over participant 4"},
    };
    if (options.customAdditionalCounts || capacity.safeMaxAdditionalThreads <= 4) {
        result["reason"] =
                options.customAdditionalCounts
                ? "explicit additional-count list disables auto-extension"
                : "no safe counts above four additional threads";
        return result;
    }

    const std::size_t target = options.targetMicroseconds.back();
    const std::size_t tasks = options.logicalTaskCounts.back();
    const std::vector<double> participant4 =
            representativeSpeedups(decisions, 3, target, tasks);
    const std::vector<double> participant5 =
            representativeSpeedups(decisions, 4, target, tasks);
    if (participant4.empty() || participant5.empty()) {
        result["reason"] = "mandatory participant 4/5 evidence unavailable";
        return result;
    }
    const double speedup4 = medianDouble(participant4);
    const double speedup5 = medianDouble(participant5);
    const double marginal = speedup5 - speedup4;
    extend = marginal > 0.0;
    result["participant4PairedSpeedupP50Percent"] = speedup4;
    result["participant5PairedSpeedupP50Percent"] = speedup5;
    result["marginalSpeedupPoints"] = marginal;
    result["extended"] = extend;
    result["reason"] = extend ? "positive measured marginal efficiency"
                              : "upper mandatory point did not improve";
    return result;
}

PerfJson recommendationJson(const std::vector<DecisionRecord>& decisions,
        InterferenceMode interference) {
    struct Candidate final {
        std::size_t additionalThreads = 0;
        std::vector<double> speedups;
        std::size_t acceptedCases = 0;
        std::size_t consideredCases = 0;
    };

    std::vector<Candidate> candidates;
    for (const DecisionRecord& decision : decisions) {
        if (decision.waitMode != WaitMode::Block || decision.additionalThreads == 0 || decision.workload == WorkloadKind::NoOp || decision.targetMicroseconds < 100 || decision.logicalTasks < 32) {
            continue;
        }
        auto found = std::find_if(
                candidates.begin(), candidates.end(), [&](const Candidate& candidate) {
                    return candidate.additionalThreads == decision.additionalThreads;
                });
        if (found == candidates.end()) {
            Candidate candidate;
            candidate.additionalThreads = decision.additionalThreads;
            candidates.push_back(std::move(candidate));
            found = std::prev(candidates.end());
        }
        ++found->consideredCases;
        if (decision.acceptancePassed) {
            ++found->acceptedCases;
            found->speedups.push_back(decision.pairedSpeedupP50Percent);
        }
    }

    PerfJson evaluated = PerfJson::array();
    std::size_t selectedAdditionalThreads = 0;
    double selectedScore = 0.0;
    for (Candidate& candidate : candidates) {
        const double score = medianDouble(candidate.speedups);
        const double passFraction =
                candidate.consideredCases == 0
                ? 0.0
                : static_cast<double>(candidate.acceptedCases) / static_cast<double>(candidate.consideredCases);
        evaluated.push_back({{"additionalThreadCount", candidate.additionalThreads},
                {"participantCount", candidate.additionalThreads + 1U},
                {"consideredCases", candidate.consideredCases},
                {"acceptedCases", candidate.acceptedCases},
                {"acceptedFraction", passFraction},
                {"acceptedCaseMedianSpeedupPercent", score}});
        // Requiring at least half of the medium/long cases avoids selecting a
        // configuration because of one cache-friendly outlier.
        if (passFraction >= 0.5 && score > selectedScore) {
            selectedScore = score;
            selectedAdditionalThreads = candidate.additionalThreads;
        }
    }

    return {
            {"scope", "measurement-local; never keyed by CPU model"},
            {"interference", interferenceName(interference)},
            {"parallelBackend",
                    selectedAdditionalThreads == 0 ? "none" : "highway-persistent-pool"},
            {"waitMode", "block"},
            {"additionalThreadCount", selectedAdditionalThreads},
            {"participantCount", selectedAdditionalThreads + 1U},
            {"directSerialFallback", true},
            {"dynamicPoolRecreationPerUpdate", false},
            {"selectionRule",
                    "highest median speedup among configurations passing the 2x overhead "
                    "gate in at least half of >=100 us, >=32-task cases"},
            {"selectedScorePercent", selectedScore},
            {"candidates", std::move(evaluated)},
    };
}

PerfJson capacityJson(const DispatcherCapacity& capacity) {
    return {
            {"threadingSupported", capacity.threadingSupported},
            {"processorCountSource", cellularity::execution::processorCountSourceName(capacity.processorCountSource)},
            {"affinityProcessorCount", capacity.affinityProcessorCount},
            {"hardwareConcurrency", capacity.hardwareConcurrency},
            {"usableProcessorCount", capacity.usableProcessorCount},
            {"processorMaxAdditionalThreads", capacity.processorMaxAdditionalThreads},
            {"highwayReportedMaxAdditionalThreads",
                    capacity.highwayReportedMaxAdditionalThreads},
            {"projectMaxAdditionalThreads", capacity.projectMaxAdditionalThreads},
            {"safeMaxAdditionalThreads", capacity.safeMaxAdditionalThreads},
            {"unknownCountPolicy", "zero additional threads"},
            {"maxThreadsUnderflowGuarded", true},
            {"processorCountIsPerformancePolicy", false},
    };
}

PerfJson calibrationJson(const std::vector<Calibration>& calibrations) {
    PerfJson result = PerfJson::array();
    for (const Calibration& calibration : calibrations) {
        result.push_back({{"workload", workloadName(calibration.kind)},
                {"targetMicroseconds", calibration.targetMicroseconds},
                {"totalWorkUnits", calibration.totalUnits},
                {"measuredP50Ns", calibration.measuredP50Ns}});
    }
    return result;
}

std::vector<std::size_t>
defaultAdditionalCounts(const DispatcherCapacity& capacity) {
    const std::size_t maximum =
            std::min<std::size_t>(4, capacity.safeMaxAdditionalThreads);
    std::vector<std::size_t> result(maximum + 1U);
    std::iota(result.begin(), result.end(), 0U);
    return result;
}

void validateAdditionalCounts(const std::vector<std::size_t>& counts,
        const DispatcherCapacity& capacity) {
    if (counts.empty() || counts.front() != 0) {
        throw std::invalid_argument(
                "additional counts must include zero for the Highway serial fallback");
    }
    if (counts.back() > capacity.safeMaxAdditionalThreads) {
        throw std::invalid_argument(
                "requested additional thread count exceeds the safe capacity");
    }
}

const Calibration&
selectAutotuneCalibration(const std::vector<Calibration>& calibrations) {
    const auto found = std::find_if(
            calibrations.begin(), calibrations.end(), [](const Calibration& value) {
                return value.kind == WorkloadKind::Arithmetic && value.targetMicroseconds >= 100;
            });
    return found == calibrations.end() ? calibrations.front() : *found;
}

void appendSweep(const Options& options,
        const std::vector<std::size_t>& additionalCounts,
        const std::vector<CaseSpec>& caseSpecs,
        const Calibration& autotuneCalibration,
        WorkloadStorage& storage, PerfJson& instances,
        std::vector<DecisionRecord>& decisions) {
    for (int pass = 0; pass < options.passes; ++pass) {
        std::vector<std::size_t> countOrder = additionalCounts;
        std::vector<WaitMode> modeOrder = options.waitModes;
        if ((pass & 1) != 0) {
            std::reverse(countOrder.begin(), countOrder.end());
            std::reverse(modeOrder.begin(), modeOrder.end());
        }
        for (const std::size_t additionalThreads : countOrder) {
            for (const WaitMode mode : modeOrder) {
                PoolInstanceResult instance =
                        runPoolInstance(options, pass, additionalThreads, mode, caseSpecs,
                                autotuneCalibration, storage);
                instances.push_back(std::move(instance.json));
                decisions.insert(decisions.end(), instance.decisions.begin(),
                        instance.decisions.end());
            }
        }
    }
}

PerfJson buildMetadata() {
    return {
            {"buildType", CELLULARITY_BUILD_TYPE},
            {"systemName", CELLULARITY_SYSTEM_NAME},
            {"systemProcessor", CELLULARITY_SYSTEM_PROCESSOR},
            {"compiler",
#if defined(__clang__)
                    "Clang"
#elif defined(__GNUC__)
                    "GCC"
#else
                    "unknown"
#endif
            },
            {"compilerVersion", __VERSION__},
            {"sourceRevision", CELLULARITY_SOURCE_REVISION},
            {"sourceDirty", CELLULARITY_SOURCE_DIRTY},
            {"pointerBits", sizeof(void*) * 8U},
            {"steadyClockIsSteady", Clock::is_steady},
            {"steadyClockPeriodNumerator", Clock::period::num},
            {"steadyClockPeriodDenominator", Clock::period::den},
    };
}

PerfJson runLaboratory(const Options& options) {
    const DispatcherCapacity capacity =
            cellularity::execution::detectDispatcherCapacity();
    std::vector<std::size_t> counts = options.customAdditionalCounts
            ? options.additionalCounts
            : defaultAdditionalCounts(capacity);
    validateAdditionalCounts(counts, capacity);

    WorkloadStorage storage;
    const std::vector<Calibration> calibrations =
            calibrateWorkloads(storage, options.targetMicroseconds);
    const std::vector<CaseSpec> caseSpecs = makeCaseSpecs(options, calibrations);
    const Calibration& autotuneCalibration =
            selectAutotuneCalibration(calibrations);

    RenderInterference interference(options.interference, options.substratePath);
    PerfJson instances = PerfJson::array();
    std::vector<DecisionRecord> decisions;
    appendSweep(options, counts, caseSpecs, autotuneCalibration, storage,
            instances, decisions);

    bool extend = false;
    PerfJson extension =
            extensionDecisionJson(options, capacity, decisions, extend);
    if (extend) {
        std::vector<std::size_t> extendedCounts;
        for (std::size_t count = 5; count <= capacity.safeMaxAdditionalThreads;
                ++count) {
            extendedCounts.push_back(count);
        }
        appendSweep(options, extendedCounts, caseSpecs, autotuneCalibration,
                storage, instances, decisions);
        counts.insert(counts.end(), extendedCounts.begin(), extendedCounts.end());
    }

    PerfJson lifecycle = runLifecycleMatrix(options, counts, storage);
    PerfJson poolMemory = runPoolMemoryMatrix(counts, storage);
    const PerfJson interferenceSnapshot = interference.snapshot();

    PerfJson waitModes = PerfJson::array();
    for (const WaitMode mode : options.waitModes) {
        waitModes.push_back(waitModeName(mode));
    }
    return {
            {"schemaVersion", 1},
            {"scenario", "execution/dispatcher-laboratory"},
            {"quickSmoke", options.quick},
            {"productionSimulationChanged", false},
            {"backendCandidates",
                    {"serial-direct", "highway-persistent-thread-pool"}},
            {"projectOwnedPoolImplemented", false},
            {"projectOwnedPoolReason",
                    "Highway is evaluated first; no concrete failed requirement has been "
                    "established"},
            {"metadata", buildMetadata()},
            {"capacity", capacityJson(capacity)},
            {"configuration",
                    {{"samplesPerCase", options.samples},
                            {"decompositionSamplesPerCase", options.decompositionSamples},
                            {"caseWarmups", options.caseWarmups},
                            {"passes", options.passes},
                            {"lifecycleSamples", options.lifecycleSamples},
                            {"additionalThreadCounts", counts},
                            {"logicalTaskCounts", options.logicalTaskCounts},
                            {"targetMicroseconds", options.targetMicroseconds},
                            {"waitModes", waitModes},
                            {"defaultWaitMode", "block"},
                            {"pairOrdering", "alternating direct-first / pool-first"},
                            {"passOrdering",
                                    "additional-count and wait-mode order reverses each pass"},
                            {"workCaseOrdering",
                                    "deterministically shuffled after no-op references"},
                            {"calibrationIncludedInSteadySamples", false},
                            {"autotuneIncludedInSteadySamples", false},
                            {"spinPolicy",
                                    "lab-only bounded lifetime ablation; not a production default"},
                            {"rawSamplesIncluded", options.rawSamples}}},
            {"calibration", calibrationJson(calibrations)},
            {"minimumSweepComplete",
                    capacity.safeMaxAdditionalThreads >= 4 && std::find(counts.begin(), counts.end(), 4) != counts.end()},
            {"extensionDecision", std::move(extension)},
            {"poolInstances", std::move(instances)},
            {"coldLifecycle", std::move(lifecycle)},
            {"poolMemoryFootprint", std::move(poolMemory)},
            {"interference", interferenceSnapshot},
            {"recommendation", recommendationJson(decisions, options.interference)},
            {"measurementDefinitions",
                    {{"steadyRun",
                             "non-instrumented synchronous call from immediately before Run to "
                             "its return"},
                            {"publishDispatch",
                                    "instrumented latency from Run entry to main task entry"},
                            {"workerWake",
                                    "instrumented latency from Run entry to first non-main worker task; "
                                    "includes publication because Highway exposes no split boundary"},
                            {"usefulWork",
                                    "instrumented envelope and summed task time; acceptance instead uses "
                                    "non-instrumented work-minus-no-op estimates"},
                            {"barrier",
                                    "instrumented tail from final task completion to Run return"},
                            {"reduction",
                                    "separately timed deterministic task-index-order checksum reduction"},
                            {"autotune",
                                    "first run and all runs until AutoTuneComplete; excluded from every "
                                    "steady case"},
                            {"lifecycle",
                                    "constructor, optional wait-mode transition, first run, destructor"},
                            {"poolMemory",
                                    "process status before construction, after construction, after first "
                                    "dispatch and after teardown; outside every steady interval"}}},
            {"benchmarkSink", gBenchmarkSink.load(std::memory_order_relaxed)},
    };
}

} // namespace

int runDispatcherLab(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        writePerfJsonReport(runLaboratory(options), options.outputPath);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "dispatcher laboratory failed: " << error.what() << '\n';
        return 1;
    }
}
