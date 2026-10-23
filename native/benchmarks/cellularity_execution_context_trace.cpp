#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__ANDROID__) || defined(__linux__)
#include <sys/prctl.h>
#else
#include <pthread.h>
#endif
#include <sys/syscall.h>
#include <unistd.h>

#include "execution/DispatcherCapacity.h"
#include "execution/SimulationExecutionContext.h"
#include "platform/CurrentThreadName.h"

namespace {

using cellularity::execution::SimulationExecutionContext;

constexpr std::size_t kMaxParticipants =
        cellularity::execution::kProjectMaxAdditionalThreads + 1;

struct Configuration final {
    std::size_t requestedAdditionalThreads = 2;
    std::size_t cycleCount = 2;
    std::chrono::milliseconds activeDuration{1500};
    std::string outputPath;
};

struct ParticipantRecord final {
    std::size_t taskIndex = 0;
    std::size_t workerIndex = 0;
    long threadId = 0;
    std::array<char, 16> name{};
    std::uint64_t checksum = 0;
    bool observed = false;
};

struct CycleRecord final {
    std::size_t cycleIndex = 0;
    std::size_t participantCount = 0;
    std::size_t processThreadsWhileMaterialized = 0;
    std::size_t processThreadsAfterRelease = 0;
    std::uint64_t dispatchesDuringCycle = 0;
    std::uint64_t poolConstructionCount = 0;
    std::uint64_t poolDestructionCount = 0;
    std::vector<ParticipantRecord> participants;
};

[[nodiscard]] std::size_t parseSize(std::string_view value,
        const char* option) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(option) + " requires a value");
    }
    errno = 0;
    char* end = nullptr;
    const std::string owned(value);
    const unsigned long long parsed = std::strtoull(owned.c_str(), &end, 10);
    if (errno != 0 || end == owned.c_str() || *end != '\0' || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(std::string(option) + " has an invalid value");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] Configuration parseArguments(int argc, char** argv) {
    Configuration result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--help") {
            std::cout
                    << "Usage: cellularity_execution_context_trace [options]\n"
                       "  --additional-threads N  requested additional workers (1..7)\n"
                       "  --cycles N               bind/materialize/release cycles "
                       "(1..8)\n"
                       "  --duration-ms N          active trace time per cycle "
                       "(100..10000)\n"
                       "  --output PATH            write JSON result to PATH\n";
            std::exit(0);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument(std::string(option) + " requires a value");
        }
        const std::string_view value(argv[++index]);
        if (option == "--additional-threads") {
            result.requestedAdditionalThreads = parseSize(value, option.data());
        } else if (option == "--cycles") {
            result.cycleCount = parseSize(value, option.data());
        } else if (option == "--duration-ms") {
            result.activeDuration =
                    std::chrono::milliseconds(parseSize(value, option.data()));
        } else if (option == "--output") {
            result.outputPath = value;
        } else {
            throw std::invalid_argument("Unknown option: " + std::string(option));
        }
    }

    if (result.requestedAdditionalThreads == 0 || result.requestedAdditionalThreads > cellularity::execution::kProjectMaxAdditionalThreads) {
        throw std::invalid_argument("--additional-threads must be in [1, 7]");
    }
    if (result.cycleCount == 0 || result.cycleCount > 8) {
        throw std::invalid_argument("--cycles must be in [1, 8]");
    }
    if (result.activeDuration < std::chrono::milliseconds(100) || result.activeDuration > std::chrono::milliseconds(10'000)) {
        throw std::invalid_argument("--duration-ms must be in [100, 10000]");
    }
    return result;
}

[[nodiscard]] std::size_t processThreadCount() noexcept {
    DIR* directory = opendir("/proc/self/task");
    if (directory == nullptr) {
        return 0;
    }
    std::size_t count = 0;
    while (dirent* entry = readdir(directory)) {
        const unsigned char first = static_cast<unsigned char>(entry->d_name[0]);
        if (std::isdigit(first) != 0) {
            ++count;
        }
    }
    (void)closedir(directory);
    return count;
}

[[nodiscard]] std::size_t
waitForThreadCountAtMost(std::size_t expected) noexcept {
    std::size_t observed = processThreadCount();
    for (int attempt = 0; attempt < 1000 && observed > expected; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        observed = processThreadCount();
    }
    return observed;
}

void initializeThreadRuntimeForBaseline() {
    // TSan starts one process-wide helper lazily on the first std::thread. Do
    // that before taking the lifecycle baseline so it cannot be mistaken for a
    // leaked simulation worker. Ordinary and Android builds need no warm-up.
#if defined(__SANITIZE_THREAD__)
    std::thread warmup([] {});
    warmup.join();
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
    std::thread warmup([] {});
    warmup.join();
#endif
#endif
}

[[nodiscard]] long currentThreadId() noexcept {
    return static_cast<long>(syscall(SYS_gettid));
}

[[nodiscard]] std::array<char, 16> currentThreadName() noexcept {
    std::array<char, 16> name{};
#if defined(__ANDROID__) || defined(__linux__)
    if (prctl(PR_GET_NAME, name.data(), 0, 0, 0) != 0) {
        name[0] = '\0';
    }
#else
    if (pthread_getname_np(pthread_self(), name.data(), name.size()) != 0) {
        name[0] = '\0';
    }
#endif
    return name;
}

void appendError(std::vector<std::string>& errors, bool condition,
        std::string message) {
    if (!condition) {
        errors.push_back(std::move(message));
    }
}

void writeJsonString(std::ostream& output, std::string_view value) {
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (character < 0x20U) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    output << "\\u00" << kHex[(character >> 4U) & 0x0FU]
                           << kHex[character & 0x0FU];
                } else {
                    output << static_cast<char>(character);
                }
                break;
        }
    }
    output << '"';
}

void writeSnapshot(std::ostream& output,
        const SimulationExecutionContext::Snapshot& snapshot) {
    output << "{\"requestedAdditionalThreadCount\":"
           << snapshot.requestedAdditionalThreadCount
           << ",\"additionalThreadCount\":" << snapshot.additionalThreadCount
           << ",\"participantCount\":" << snapshot.participantCount
           << ",\"ownerThreadBound\":"
           << (snapshot.ownerThreadBound ? "true" : "false")
           << ",\"poolMaterialized\":"
           << (snapshot.poolMaterialized ? "true" : "false")
           << ",\"workerInitializationComplete\":"
           << (snapshot.workerInitializationComplete ? "true" : "false")
           << ",\"floatingPointEnvironmentValid\":"
           << (snapshot.floatingPointEnvironmentValid ? "true" : "false")
           << ",\"poolConstructionCount\":" << snapshot.poolConstructionCount
           << ",\"poolDestructionCount\":" << snapshot.poolDestructionCount
           << ",\"dispatchCount\":" << snapshot.dispatchCount
           << ",\"directDispatchCount\":" << snapshot.directDispatchCount
           << ",\"parallelDispatchCount\":" << snapshot.parallelDispatchCount
           << ",\"worldResetCount\":" << snapshot.worldResetCount << '}';
}

void writeResult(std::ostream& output, const Configuration& configuration,
        std::size_t baselineThreadCount,
        const SimulationExecutionContext::Snapshot& finalSnapshot,
        const std::vector<CycleRecord>& cycles,
        const std::vector<std::string>& errors) {
    output << "{\n  \"schemaVersion\": 1,\n"
           << "  \"scenario\": \"c01/execution-context-device-trace\",\n"
           << "  \"passed\": " << (errors.empty() ? "true" : "false")
           << ",\n  \"requestedAdditionalThreadCount\": "
           << configuration.requestedAdditionalThreads
           << ",\n  \"requestedCycleCount\": " << configuration.cycleCount
           << ",\n  \"activeDurationMsPerCycle\": "
           << configuration.activeDuration.count()
           << ",\n  \"baselineProcessThreadCount\": " << baselineThreadCount
           << ",\n  \"finalSnapshot\": ";
    writeSnapshot(output, finalSnapshot);
    output << ",\n  \"cycles\": [";
    for (std::size_t cycleIndex = 0; cycleIndex < cycles.size(); ++cycleIndex) {
        const CycleRecord& cycle = cycles[cycleIndex];
        output << (cycleIndex == 0 ? "\n" : ",\n")
               << "    {\"cycleIndex\":" << cycle.cycleIndex
               << ",\"participantCount\":" << cycle.participantCount
               << ",\"processThreadsWhileMaterialized\":"
               << cycle.processThreadsWhileMaterialized
               << ",\"processThreadsAfterRelease\":"
               << cycle.processThreadsAfterRelease
               << ",\"dispatchesDuringCycle\":" << cycle.dispatchesDuringCycle
               << ",\"poolConstructionCount\":" << cycle.poolConstructionCount
               << ",\"poolDestructionCount\":" << cycle.poolDestructionCount
               << ",\"participants\":[";
        for (std::size_t participantIndex = 0;
                participantIndex < cycle.participants.size(); ++participantIndex) {
            const ParticipantRecord& participant =
                    cycle.participants[participantIndex];
            output << (participantIndex == 0 ? "" : ",")
                   << "{\"taskIndex\":" << participant.taskIndex
                   << ",\"workerIndex\":" << participant.workerIndex
                   << ",\"threadId\":" << participant.threadId << ",\"name\":";
            writeJsonString(output, participant.name.data());
            output << ",\"checksum\":" << participant.checksum
                   << ",\"observed\":" << (participant.observed ? "true" : "false")
                   << '}';
        }
        output << "]}";
    }
    output << "\n  ],\n  \"errors\": [";
    for (std::size_t errorIndex = 0; errorIndex < errors.size(); ++errorIndex) {
        output << (errorIndex == 0 ? "" : ",");
        writeJsonString(output, errors[errorIndex]);
    }
    output << "]\n}\n";
}

int run(const Configuration& configuration) {
    cellularity::platform::setCurrentThreadName("CellularitySim");
    initializeThreadRuntimeForBaseline();
    const long ownerThreadId = currentThreadId();
    const std::size_t baselineThreadCount = processThreadCount();
    std::vector<std::string> errors;
    std::vector<CycleRecord> cycles;
    SimulationExecutionContext context(configuration.requestedAdditionalThreads);

    for (std::size_t cycleIndex = 0; cycleIndex < configuration.cycleCount;
            ++cycleIndex) {
        CycleRecord cycle;
        cycle.cycleIndex = cycleIndex;
        try {
            context.bindToCurrentThread();
            const std::size_t participants = context.participantCount();
            cycle.participantCount = participants;
            appendError(errors, participants > 1,
                    "Device capacity clamped the requested worker count to zero");
            appendError(errors, participants <= kMaxParticipants,
                    "Participant count exceeds the project capacity");
            if (participants <= 1 || participants > kMaxParticipants) {
                context.releaseFromCurrentThread();
                cycles.push_back(std::move(cycle));
                break;
            }

            std::array<ParticipantRecord, kMaxParticipants> records{};
            for (std::size_t participant = 0; participant < participants;
                    ++participant) {
                records[participant].taskIndex = participant;
            }
            auto task = [&](std::size_t taskIndex, std::size_t workerIndex) noexcept {
                ParticipantRecord& record = records[taskIndex];
                if (!record.observed) {
                    record.workerIndex = workerIndex;
                    record.threadId = currentThreadId();
                    record.name = currentThreadName();
                    record.observed = true;
                }
                std::uint64_t value =
                        record.checksum ^ (0x9e3779b97f4a7c15ULL + taskIndex);
                for (std::uint64_t iteration = 0; iteration < 4096; ++iteration) {
                    value ^= value >> 12U;
                    value ^= value << 25U;
                    value ^= value >> 27U;
                    value *= 0x2545f4914f6cdd1dULL;
                }
                record.checksum = value;
            };

            const std::uint64_t dispatchBefore = context.snapshot().dispatchCount;
            context.runParticipantShards(task);
            const auto materialized = context.snapshot();
            cycle.processThreadsWhileMaterialized = processThreadCount();
            appendError(errors, materialized.poolMaterialized,
                    "First parallel dispatch did not materialize the pool");
            appendError(errors, materialized.workerInitializationComplete,
                    "Worker initialization did not complete");
            appendError(errors, materialized.floatingPointEnvironmentValid,
                    "Worker floating-point environment is invalid");
            appendError(
                    errors,
                    cycle.processThreadsWhileMaterialized >= baselineThreadCount + materialized.additionalThreadCount,
                    "Materialized pool did not expose the expected process threads");

            if (cycleIndex == 0) {
                const std::uint64_t constructionBeforeReset =
                        materialized.poolConstructionCount;
                context.resetForWorldReplacement();
                const auto reset = context.snapshot();
                appendError(errors,
                        reset.worldResetCount == 1 && reset.poolMaterialized && reset.poolConstructionCount == constructionBeforeReset,
                        "World replacement did not retain the compatible pool");
            }

            const auto deadline =
                    std::chrono::steady_clock::now() + configuration.activeDuration;
            while (std::chrono::steady_clock::now() < deadline) {
                context.runParticipantShards(task);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            cycle.dispatchesDuringCycle =
                    context.snapshot().dispatchCount - dispatchBefore;

            for (std::size_t participant = 0; participant < participants;
                    ++participant) {
                const ParticipantRecord& record = records[participant];
                const std::string expectedName =
                        participant == 0 ? "CellularitySim"
                                         : "CellSimW" + std::to_string(participant);
                appendError(errors, record.observed,
                        "A participant did not execute its owner shard");
                appendError(errors, record.workerIndex == participant,
                        "Task-to-worker mapping changed for an owner shard");
                appendError(errors, record.name.data() == expectedName,
                        "Unexpected thread name for participant " + std::to_string(participant) + ": " + record.name.data());
                appendError(errors,
                        participant != 0 || record.threadId == ownerThreadId,
                        "Shard zero did not execute on the owner thread");
                appendError(errors,
                        participant == 0 || record.threadId != ownerThreadId,
                        "An additional shard executed on the owner thread");
                cycle.participants.push_back(record);
            }

            context.releaseFromCurrentThread();
            const auto released = context.snapshot();
            cycle.poolConstructionCount = released.poolConstructionCount;
            cycle.poolDestructionCount = released.poolDestructionCount;
            cycle.processThreadsAfterRelease =
                    waitForThreadCountAtMost(baselineThreadCount);
            appendError(errors,
                    !released.ownerThreadBound && !released.poolMaterialized,
                    "Release retained an owner binding or materialized pool");
            appendError(errors,
                    released.poolConstructionCount == released.poolDestructionCount,
                    "Release did not destroy every constructed pool");
            appendError(errors,
                    cycle.processThreadsAfterRelease <= baselineThreadCount,
                    "Release did not return process thread count to baseline");
        } catch (const std::exception& error) {
            errors.push_back("Cycle " + std::to_string(cycleIndex) + ": " + error.what());
            if (context.snapshot().ownerThreadBound) {
                context.releaseFromCurrentThread();
            }
            cycle.processThreadsAfterRelease =
                    waitForThreadCountAtMost(baselineThreadCount);
            cycles.push_back(std::move(cycle));
            break;
        }
        cycles.push_back(std::move(cycle));
    }

    const auto finalSnapshot = context.snapshot();
    appendError(
            errors,
            finalSnapshot.poolConstructionCount == configuration.cycleCount && finalSnapshot.poolDestructionCount == configuration.cycleCount,
            "Restart cycles did not construct and destroy exactly one pool each");

    if (configuration.outputPath.empty()) {
        writeResult(std::cout, configuration, baselineThreadCount, finalSnapshot,
                cycles, errors);
    } else {
        std::ofstream output(configuration.outputPath,
                std::ios::out | std::ios::trunc);
        if (!output) {
            std::cerr << "Unable to open output: " << configuration.outputPath
                      << '\n';
            return 2;
        }
        writeResult(output, configuration, baselineThreadCount, finalSnapshot,
                cycles, errors);
    }
    return errors.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(parseArguments(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "execution-context trace check failed: " << error.what()
                  << '\n';
        return 2;
    }
}
