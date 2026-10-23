#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "execution/DispatcherCapacity.h"

namespace cellularity::execution {

// Runtime-only state of the simulation execution substrate. It is owned by a
// simulation session, never by SimulationWorld, and therefore is neither serialized
// nor copied when world contents are replaced.
class SimulationExecutionContext final {
public:
    using ErasedTask = void (*)(void* state, std::size_t taskIndex,
            std::size_t workerIndex) noexcept;

    struct Snapshot final {
        std::size_t requestedAdditionalThreadCount = 0;
        std::size_t additionalThreadCount = 0;
        std::size_t participantCount = 1;
        bool ownerThreadBound = false;
        bool poolMaterialized = false;
        bool workerInitializationComplete = false;
        bool floatingPointEnvironmentValid = true;
        std::uint64_t poolConstructionCount = 0;
        std::uint64_t poolDestructionCount = 0;
        std::uint64_t dispatchCount = 0;
        std::uint64_t directDispatchCount = 0;
        std::uint64_t parallelDispatchCount = 0;
        std::uint64_t worldResetCount = 0;
    };

    // Zero is the intentional production default until a measured execution
    // policy (C-03/C-04) selects a parallel backend for a real stage.
    explicit SimulationExecutionContext(
            std::size_t requestedAdditionalThreadCount = 0);
    ~SimulationExecutionContext();

    SimulationExecutionContext(const SimulationExecutionContext&) = delete;
    SimulationExecutionContext&
    operator=(const SimulationExecutionContext&) = delete;
    SimulationExecutionContext(SimulationExecutionContext&&) = delete;
    SimulationExecutionContext& operator=(SimulationExecutionContext&&) = delete;

    // Called on the authoritative simulation thread before it can dispatch any
    // parallel stage. Binding does not create worker threads.
    void bindToCurrentThread();

    // Safe only between stages/updates. Reconfiguration destroys a materialized
    // pool and the next parallel dispatch creates the replacement lazily.
    void
    setRequestedAdditionalThreadCount(std::size_t requestedAdditionalThreadCount);

    [[nodiscard]] std::size_t requestedAdditionalThreadCount() const noexcept;
    [[nodiscard]] std::size_t additionalThreadCount() const noexcept;
    [[nodiscard]] std::size_t participantCount() const noexcept;
    [[nodiscard]] const DispatcherCapacity& capacity() const noexcept;

    // One task per participant. Highway maps task/worker 0 to the calling
    // simulation thread, which therefore always owns shard 0.
    template <class Task>
    void runParticipantShards(Task& task) {
        static_assert(std::is_nothrow_invocable_v<Task&, std::size_t, std::size_t>,
                "Simulation worker tasks must be noexcept");
        runErased(participantCount(), &task, &invoke<Task>);
    }

    template <class Task>
    void runTasks(std::size_t taskCount, Task& task) {
        static_assert(std::is_nothrow_invocable_v<Task&, std::size_t, std::size_t>,
                "Simulation worker tasks must be noexcept");
        runErased(taskCount, &task, &invoke<Task>);
    }

    // World replacement invalidates future stage scratch but deliberately keeps
    // a compatible materialized pool. Current C-01 has no stage scratch yet;
    // the generation counter makes this lifecycle boundary explicit for C-03.
    void resetForWorldReplacement();

    // Called by the simulation thread when its runtime stops. Blocking workers
    // are joined and ownership is cleared; configuration remains for restart.
    void releaseFromCurrentThread() noexcept;

    [[nodiscard]] Snapshot snapshot() const noexcept;

private:
    struct Impl;

    template <class Task>
    static void invoke(void* state, std::size_t taskIndex,
            std::size_t workerIndex) noexcept {
        (*static_cast<Task*>(state))(taskIndex, workerIndex);
    }

    void runErased(std::size_t taskCount, void* state, ErasedTask task);

    std::unique_ptr<Impl> mImpl;
};

} // namespace cellularity::execution
