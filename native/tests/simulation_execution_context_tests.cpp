#include "execution/FloatingPointEnvironment.h"
#include "execution/SimulationExecutionContext.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfenv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace {

using cellularity::execution::applyFloatingPointEnvironment;
using cellularity::execution::captureFloatingPointEnvironment;
using cellularity::execution::FloatingPointEnvironment;
using cellularity::execution::SimulationExecutionContext;

void assertTrue(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t processThreadCount() {
#if defined(__linux__)
    std::size_t count = 0;
    for (const auto& entry :
            std::filesystem::directory_iterator("/proc/self/task")) {
        (void)entry;
        ++count;
    }
    return count;
#endif
    return 0;
}

std::string processThreadNames() {
    std::string result;
#if defined(__linux__)
    for (const auto& entry :
            std::filesystem::directory_iterator("/proc/self/task")) {
        std::ifstream nameFile(entry.path() / "comm");
        std::string name;
        std::getline(nameFile, name);
        if (!result.empty()) {
            result += ',';
        }
        result += entry.path().filename().string() + ':' + name;
    }
#endif
    return result;
}

bool waitForProcessThreadCount(std::size_t expected) {
    for (int attempt = 0; attempt < 1'000; ++attempt) {
        if (processThreadCount() == expected) {
            return true;
        }
        // pthread_join has already completed. Some Linux kernels retain the procfs
        // task entry for a very short grace period, so poll that observable rather
        // than mistaking delayed procfs cleanup for a live worker leak.
        std::this_thread::yield();
    }
    return false;
}

std::size_t quiescentProcessThreadCount() {
    std::size_t minimum = processThreadCount();
    for (int attempt = 0; attempt < 1'000; ++attempt) {
        minimum = std::min(minimum, processThreadCount());
        std::this_thread::yield();
    }
    return minimum;
}

void readCurrentThreadName(std::array<char, 16>& name) noexcept {
    name[0] = '\0';
#if defined(__linux__)
    (void)pthread_getname_np(pthread_self(), name.data(), name.size());
#endif
}

void testSerialFallbackRequiresNoBindingOrPool() {
    SimulationExecutionContext context;
    std::array<int, 9> calls{};
    auto task = [&](std::size_t taskIndex, std::size_t workerIndex) noexcept {
        if (workerIndex == 0 && taskIndex < calls.size()) {
            ++calls[taskIndex];
        }
    };

    context.runTasks(calls.size(), task);
    for (int callCount : calls) {
        assertTrue(callCount == 1,
                "Direct serial dispatch must execute every task exactly once");
    }
    const auto snapshot = context.snapshot();
    assertTrue(snapshot.participantCount == 1 && !snapshot.ownerThreadBound && !snapshot.poolMaterialized && snapshot.dispatchCount == 1 && snapshot.directDispatchCount == 1 && snapshot.parallelDispatchCount == 0,
            "Default execution context must remain a pool-free serial path");
}

void testLazyMaterializationAndWorkerContract() {
    SimulationExecutionContext context(1);
    const std::size_t additionalThreads = context.additionalThreadCount();
    const auto constructed = context.snapshot();
    assertTrue(!constructed.ownerThreadBound && !constructed.poolMaterialized && constructed.poolConstructionCount == 0,
            "Context construction must not create or bind worker threads");

    if (additionalThreads != 0) {
        bool rejectedBeforeBinding = false;
        auto unboundTask = [](std::size_t, std::size_t) noexcept {};
        try {
            context.runParticipantShards(unboundTask);
        } catch (const std::logic_error&) {
            rejectedBeforeBinding = true;
        }
        assertTrue(rejectedBeforeBinding && !context.snapshot().poolMaterialized,
                "Parallel dispatch cannot implicitly bind an arbitrary caller");
        rejectedBeforeBinding = false;
        try {
            context.runTasks(1, unboundTask);
        } catch (const std::logic_error&) {
            rejectedBeforeBinding = true;
        }
        assertTrue(rejectedBeforeBinding && context.snapshot().dispatchCount == 0,
                "A short direct stage cannot bypass configured owner binding");
    }

    context.bindToCurrentThread();
    const auto bound = context.snapshot();
    assertTrue(bound.ownerThreadBound && !bound.poolMaterialized,
            "Binding the simulation owner must remain thread-lazy");

    const std::size_t boundAdditionalThreads = context.additionalThreadCount();
    if (boundAdditionalThreads == 0) {
        return;
    }

    constexpr std::size_t maxParticipants =
            cellularity::execution::kProjectMaxAdditionalThreads + 1;
    std::array<int, maxParticipants> calls{};
    std::array<std::size_t, maxParticipants> workers{};
    std::array<std::thread::id, maxParticipants> threadIds{};
    std::array<std::array<char, 16>, maxParticipants> names{};
    const std::thread::id ownerThread = std::this_thread::get_id();
    auto task = [&](std::size_t taskIndex, std::size_t workerIndex) noexcept {
        ++calls[taskIndex];
        workers[taskIndex] = workerIndex;
        threadIds[taskIndex] = std::this_thread::get_id();
        readCurrentThreadName(names[taskIndex]);
    };

    context.runParticipantShards(task);
    const auto materialized = context.snapshot();
    assertTrue(materialized.poolMaterialized && materialized.workerInitializationComplete && materialized.floatingPointEnvironmentValid && materialized.poolConstructionCount == 1 && materialized.parallelDispatchCount == 1,
            "First parallel dispatch must initialize one valid persistent "
            "pool");
    assertTrue(calls[0] == 1 && workers[0] == 0 && threadIds[0] == ownerThread,
            "Simulation owner thread must execute shard zero");
    for (std::size_t worker = 1; worker < context.participantCount(); ++worker) {
        const std::string expected = "CellSimW" + std::to_string(worker);
        assertTrue(calls[worker] == 1 && workers[worker] == worker && threadIds[worker] != ownerThread,
                "Each additional worker must execute its fixed shard once");
#if defined(__linux__)
        assertTrue(std::strcmp(names[worker].data(), expected.c_str()) == 0,
                "Simulation worker has an unexpected trace name");
#endif
    }

    auto secondTask = [](std::size_t, std::size_t) noexcept {};
    context.runParticipantShards(secondTask);
    const auto reused = context.snapshot();
    assertTrue(reused.poolConstructionCount == 1 && reused.parallelDispatchCount == 2,
            "Steady dispatch must reuse the same worker pool");
}

void testParallelDispatchRejectsAnotherOwnerThread() {
    SimulationExecutionContext context(1);
    if (context.additionalThreadCount() == 0) {
        return;
    }
    context.bindToCurrentThread();
    auto task = [](std::size_t, std::size_t) noexcept {};
    context.runParticipantShards(task);

    std::atomic<bool> rejected{false};
    std::thread intruder([&] {
        try {
            context.runTasks(1, task);
        } catch (const std::logic_error&) {
            rejected.store(true, std::memory_order_release);
        }
    });
    intruder.join();
    assertTrue(rejected.load(std::memory_order_acquire),
            "A second dispatch owner thread must be rejected before work");
    assertTrue(context.snapshot().parallelDispatchCount == 1,
            "Rejected dispatch must not enter the worker pool");
}

void testWorkersInheritRoundingAndDenormalPolicy() {
    const FloatingPointEnvironment original = captureFloatingPointEnvironment();
    assertTrue(original.valid,
            "Test process must expose a valid floating-point environment");

    struct RestoreEnvironment final {
        FloatingPointEnvironment value;

        ~RestoreEnvironment() {
            (void)applyFloatingPointEnvironment(value);
        }
    } restore{original};

    const int alternateRounding =
            original.roundingMode == FE_DOWNWARD ? FE_TONEAREST : FE_DOWNWARD;
    assertTrue(std::fesetround(alternateRounding) == 0,
            "Test could not select an alternate rounding mode");
    FloatingPointEnvironment requested = captureFloatingPointEnvironment();
#if defined(__SSE2__) || defined(_M_X64)
    requested.architectureControl ^= (1ULL << 15U);
#elif defined(__aarch64__) || defined(__arm__)
    requested.architectureControl ^= (1ULL << 24U);
#endif
    assertTrue(applyFloatingPointEnvironment(requested),
            "Test could not select an alternate denormal policy");
    const FloatingPointEnvironment expected = captureFloatingPointEnvironment();

    SimulationExecutionContext context(1);
    context.bindToCurrentThread();
    constexpr std::size_t maxParticipants =
            cellularity::execution::kProjectMaxAdditionalThreads + 1;
    std::array<FloatingPointEnvironment, maxParticipants> observed{};
    auto task = [&](std::size_t taskIndex, std::size_t) noexcept {
        observed[taskIndex] = captureFloatingPointEnvironment();
    };
    context.runParticipantShards(task);
    for (std::size_t participant = 0; participant < context.participantCount();
            ++participant) {
        assertTrue(observed[participant].valid && observed[participant].roundingMode == expected.roundingMode && observed[participant].hasArchitectureControl == expected.hasArchitectureControl && observed[participant].architectureControl == expected.architectureControl,
                "A simulation participant inherited different floating-point "
                "control policy");
    }
    context.releaseFromCurrentThread();
}

void testResetReuseReconfigurationAndRelease() {
    SimulationExecutionContext context(1);
    context.bindToCurrentThread();
    std::atomic<bool> activeChangeRejected{false};
    auto guardedTask = [&](std::size_t taskIndex, std::size_t) noexcept {
        if (taskIndex != 0) {
            return;
        }
        try {
            context.setRequestedAdditionalThreadCount(0);
        } catch (const std::logic_error&) {
            activeChangeRejected.store(true, std::memory_order_release);
        }
    };
    context.runParticipantShards(guardedTask);
    assertTrue(activeChangeRejected.load(std::memory_order_acquire),
            "Worker capacity cannot change while a dispatch is active");
    auto task = [](std::size_t, std::size_t) noexcept {};
    const auto initial = context.snapshot();

    context.resetForWorldReplacement();
    const auto reset = context.snapshot();
    assertTrue(reset.worldResetCount == 1 && reset.poolConstructionCount == initial.poolConstructionCount && reset.poolMaterialized == initial.poolMaterialized,
            "World replacement must retain a compatible session pool");

    context.setRequestedAdditionalThreadCount(0);
    const auto serial = context.snapshot();
    assertTrue(serial.additionalThreadCount == 0 && !serial.poolMaterialized && serial.poolDestructionCount == initial.poolConstructionCount,
            "Quiescent capacity change must join the old pool");
    context.runParticipantShards(task);
    assertTrue(context.snapshot().directDispatchCount == 1,
            "Zero-thread reconfiguration must use direct serial dispatch");

    context.setRequestedAdditionalThreadCount(1);
    assertTrue(!context.snapshot().poolMaterialized,
            "Reconfiguration must not eagerly recreate workers");
    context.runParticipantShards(task);
    const auto recreated = context.snapshot();
    if (recreated.additionalThreadCount != 0) {
        assertTrue(recreated.poolConstructionCount == initial.poolConstructionCount + 1 && recreated.poolMaterialized,
                "Next parallel dispatch must lazily create the replacement");
    }

    context.releaseFromCurrentThread();
    const auto released = context.snapshot();
    assertTrue(!released.ownerThreadBound && !released.poolMaterialized && released.poolConstructionCount == released.poolDestructionCount,
            "Runtime release must join all materialized workers");

    context.bindToCurrentThread();
    const auto rebound = context.snapshot();
    assertTrue(rebound.ownerThreadBound && !rebound.poolMaterialized && rebound.worldResetCount == released.worldResetCount,
            "Runtime restart must rebind lazily and retain the world-reset "
            "generation");
    context.runParticipantShards(task);
    context.releaseFromCurrentThread();
    const auto restartedRelease = context.snapshot();
    assertTrue(!restartedRelease.ownerThreadBound && !restartedRelease.poolMaterialized && restartedRelease.poolConstructionCount == restartedRelease.poolDestructionCount,
            "Runtime restart must release every recreated worker");
}

void testRepeatedSessionLifecycleReturnsThreadCountToBaseline() {
    // Establish sanitizer/runtime helper threads before taking the baseline and
    // let procfs retire the warm-up worker's task entry.
    {
        SimulationExecutionContext warmup(1);
        warmup.bindToCurrentThread();
        auto task = [](std::size_t, std::size_t) noexcept {};
        warmup.runParticipantShards(task);
        warmup.releaseFromCurrentThread();
    }
    const std::size_t baselineThreads = quiescentProcessThreadCount();
    for (int repetition = 0; repetition < 32; ++repetition) {
        SimulationExecutionContext context(1);
        context.bindToCurrentThread();
        auto task = [](std::size_t, std::size_t) noexcept {};
        context.runParticipantShards(task);
        context.releaseFromCurrentThread();
        const auto released = context.snapshot();
        assertTrue(released.poolConstructionCount == released.poolDestructionCount,
                "Repeated runtime lifecycle leaked a materialized pool");
#if defined(__linux__)
        if (baselineThreads != 0) {
            if (!waitForProcessThreadCount(baselineThreads)) {
                const std::size_t currentThreads = processThreadCount();
                throw std::runtime_error(
                        "Repeated runtime lifecycle changed OS thread count from " + std::to_string(baselineThreads) + " to " + std::to_string(currentThreads) + " (" + processThreadNames() + ")");
            }
        }
#endif
    }
}

} // namespace

int main() {
    try {
        testSerialFallbackRequiresNoBindingOrPool();
        testLazyMaterializationAndWorkerContract();
        testParallelDispatchRejectsAnotherOwnerThread();
        testWorkersInheritRoundingAndDenormalPolicy();
        testResetReuseReconfigurationAndRelease();
        testRepeatedSessionLifecycleReturnsThreadCountToBaseline();
        std::cout << "simulation execution context tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "simulation execution context tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
