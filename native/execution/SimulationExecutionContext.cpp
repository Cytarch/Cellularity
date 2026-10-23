#include "execution/SimulationExecutionContext.h"

#include <array>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

#include "execution/FloatingPointEnvironment.h"
#include "hwy/contrib/thread_pool/thread_pool.h"
#include "platform/CurrentThreadName.h"

namespace cellularity::execution {
namespace {

class DispatchScope final {
public:
    explicit DispatchScope(std::atomic<bool>& active) : mActive(active) {
        bool expected = false;
        if (!mActive.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel)) {
            throw std::logic_error("Simulation dispatch cannot be re-entered");
        }
    }

    ~DispatchScope() {
        mActive.store(false, std::memory_order_release);
    }

    DispatchScope(const DispatchScope&) = delete;
    DispatchScope& operator=(const DispatchScope&) = delete;

private:
    std::atomic<bool>& mActive;
};

} // namespace

struct SimulationExecutionContext::Impl final {
    explicit Impl(std::size_t requested)
            : capacity(detectDispatcherCapacity()),
              requestedAdditionalThreads(requested),
              additionalThreads(clampAdditionalThreadCount(requested, capacity)) {}

    void requireOwnerThread() const {
        if (!ownerThreadBound || std::this_thread::get_id() != ownerThread) {
            throw std::logic_error(
                    "Parallel simulation dispatch requires its bound owner thread");
        }
    }

    void requireQuiescent() const {
        if (dispatchActive.load(std::memory_order_acquire)) {
            throw std::logic_error(
                    "Simulation execution context is active during a lifecycle change");
        }
    }

    void destroyPool() noexcept {
        if (pool == nullptr) {
            return;
        }
        pool.reset();
        ++poolDestructionCount;
        workerInitializationComplete = false;
        floatingPointEnvironmentValid = true;
    }

    void ensurePool() {
        requireOwnerThread();
        if (pool != nullptr || additionalThreads == 0) {
            return;
        }

        // Construction happens before any stage write and on the owner thread, so
        // workers inherit the intended simulation context. Failure propagates
        // before useful work begins; there is no replay over partially written
        // state.
        auto candidate = std::make_unique<hwy::ThreadPool>(additionalThreads);
        ++poolConstructionCount;

        const std::size_t workers = candidate->NumWorkers();
        if (workers != additionalThreads + 1 || workers > workerFloatingEnvironment.size()) {
            ++poolDestructionCount;
            throw std::runtime_error(
                    "Simulation worker pool materialized an invalid participant count");
        }

        workerFloatingEnvironment.fill(false);
        const FloatingPointEnvironment expectedFloatingPointEnvironment =
                ownerFloatingPointEnvironment;
        candidate->Run(
                0, workers,
                [&](std::uint64_t taskIndex, std::size_t workerIndex) noexcept {
                    if (workerIndex != 0) {
                        cellularity::platform::setCurrentThreadNameWithIndex("CellSimW",
                                workerIndex);
                    }
                    const bool mappingValid =
                            taskIndex == static_cast<std::uint64_t>(workerIndex);
                    workerFloatingEnvironment[workerIndex] =
                            mappingValid && applyFloatingPointEnvironment(expectedFloatingPointEnvironment);
                });

        floatingPointEnvironmentValid = true;
        for (std::size_t worker = 0; worker < workers; ++worker) {
            floatingPointEnvironmentValid =
                    floatingPointEnvironmentValid && workerFloatingEnvironment[worker];
        }
        if (!floatingPointEnvironmentValid) {
            ++poolDestructionCount;
            throw std::runtime_error(
                    "Simulation workers inherited an incompatible floating-point "
                    "environment");
        }

        workerInitializationComplete = true;
        pool = std::move(candidate);
    }

    DispatcherCapacity capacity;
    std::size_t requestedAdditionalThreads = 0;
    std::size_t additionalThreads = 0;
    std::thread::id ownerThread{};
    FloatingPointEnvironment ownerFloatingPointEnvironment{};
    bool ownerThreadBound = false;
    std::unique_ptr<hwy::ThreadPool> pool;
    std::atomic<bool> dispatchActive{false};
    std::array<bool, kProjectMaxAdditionalThreads + 1>
            workerFloatingEnvironment{};
    bool workerInitializationComplete = false;
    bool floatingPointEnvironmentValid = true;
    std::uint64_t poolConstructionCount = 0;
    std::uint64_t poolDestructionCount = 0;
    std::uint64_t dispatchCount = 0;
    std::uint64_t directDispatchCount = 0;
    std::uint64_t parallelDispatchCount = 0;
    std::uint64_t worldResetCount = 0;
};

SimulationExecutionContext::SimulationExecutionContext(
        std::size_t requestedAdditionalThreadCount)
        : mImpl(std::make_unique<Impl>(requestedAdditionalThreadCount)) {}

SimulationExecutionContext::~SimulationExecutionContext() = default;

void SimulationExecutionContext::bindToCurrentThread() {
    mImpl->requireQuiescent();
    const std::thread::id current = std::this_thread::get_id();
    if (mImpl->ownerThreadBound) {
        if (mImpl->ownerThread != current) {
            throw std::logic_error(
                    "Simulation execution context is already bound to another thread");
        }
        return;
    }
    // The owner may have been constructed on a UI/JNI thread whose affinity
    // mask differs from the authoritative simulation thread. Re-detect here so
    // neither capacity nor the later worker pool inherits that creator context.
    // An unbound context cannot retain a materialized pool.
    mImpl->capacity = detectDispatcherCapacity();
    mImpl->additionalThreads = clampAdditionalThreadCount(
            mImpl->requestedAdditionalThreads, mImpl->capacity);
    mImpl->ownerThread = current;
    mImpl->ownerFloatingPointEnvironment = captureFloatingPointEnvironment();
    if (!mImpl->ownerFloatingPointEnvironment.valid) {
        throw std::runtime_error(
                "Unable to inspect the simulation floating-point environment");
    }
    mImpl->ownerThreadBound = true;
}

void SimulationExecutionContext::setRequestedAdditionalThreadCount(
        std::size_t requestedAdditionalThreadCount) {
    mImpl->requireQuiescent();
    if (mImpl->ownerThreadBound) {
        mImpl->requireOwnerThread();
    }
    const std::size_t clamped = clampAdditionalThreadCount(
            requestedAdditionalThreadCount, mImpl->capacity);
    mImpl->requestedAdditionalThreads = requestedAdditionalThreadCount;
    if (mImpl->additionalThreads == clamped) {
        return;
    }
    mImpl->destroyPool();
    mImpl->additionalThreads = clamped;
}

std::size_t
SimulationExecutionContext::requestedAdditionalThreadCount() const noexcept {
    return mImpl->requestedAdditionalThreads;
}

std::size_t SimulationExecutionContext::additionalThreadCount() const noexcept {
    return mImpl->additionalThreads;
}

std::size_t SimulationExecutionContext::participantCount() const noexcept {
    return mImpl->additionalThreads + 1;
}

const DispatcherCapacity&
SimulationExecutionContext::capacity() const noexcept {
    return mImpl->capacity;
}

void SimulationExecutionContext::runErased(std::size_t taskCount, void* state,
        ErasedTask task) {
    if (taskCount == 0) {
        return;
    }
    if (state == nullptr || task == nullptr) {
        throw std::invalid_argument("Simulation dispatch requires a task state");
    }
    if (mImpl->ownerThreadBound) {
        mImpl->requireOwnerThread();
    } else if (mImpl->additionalThreads != 0) {
        // A context configured for workers must establish one authoritative
        // dispatch owner even when a particular stage happens to contain one
        // direct task. Otherwise that short stage could run from an arbitrary
        // caller before the first pool-backed dispatch.
        throw std::logic_error(
                "Parallel simulation dispatch requires an explicit owner binding");
    }

    DispatchScope dispatch(mImpl->dispatchActive);
    ++mImpl->dispatchCount;
    if (mImpl->additionalThreads == 0 || taskCount == 1) {
        ++mImpl->directDispatchCount;
        for (std::size_t taskIndex = 0; taskIndex < taskCount; ++taskIndex) {
            task(state, taskIndex, 0);
        }
        return;
    }

    mImpl->ensurePool();
    ++mImpl->parallelDispatchCount;
    mImpl->pool->Run(
            0, static_cast<std::uint64_t>(taskCount),
            [state, task](std::uint64_t taskIndex, std::size_t workerIndex) noexcept {
                task(state, static_cast<std::size_t>(taskIndex), workerIndex);
            });
}

void SimulationExecutionContext::resetForWorldReplacement() {
    mImpl->requireQuiescent();
    if (mImpl->ownerThreadBound) {
        mImpl->requireOwnerThread();
    }
    ++mImpl->worldResetCount;
}

void SimulationExecutionContext::releaseFromCurrentThread() noexcept {
    if (mImpl->dispatchActive.load(std::memory_order_acquire)) {
        std::terminate();
    }
    if (mImpl->ownerThreadBound && mImpl->ownerThread != std::this_thread::get_id()) {
        std::terminate();
    }
    mImpl->destroyPool();
    mImpl->ownerThread = std::thread::id{};
    mImpl->ownerThreadBound = false;
}

SimulationExecutionContext::Snapshot
SimulationExecutionContext::snapshot() const noexcept {
    Snapshot result;
    result.requestedAdditionalThreadCount = mImpl->requestedAdditionalThreads;
    result.additionalThreadCount = mImpl->additionalThreads;
    result.participantCount = mImpl->additionalThreads + 1;
    result.ownerThreadBound = mImpl->ownerThreadBound;
    result.poolMaterialized = mImpl->pool != nullptr;
    result.workerInitializationComplete = mImpl->workerInitializationComplete;
    result.floatingPointEnvironmentValid = mImpl->floatingPointEnvironmentValid;
    result.poolConstructionCount = mImpl->poolConstructionCount;
    result.poolDestructionCount = mImpl->poolDestructionCount;
    result.dispatchCount = mImpl->dispatchCount;
    result.directDispatchCount = mImpl->directDispatchCount;
    result.parallelDispatchCount = mImpl->parallelDispatchCount;
    result.worldResetCount = mImpl->worldResetCount;
    return result;
}

} // namespace cellularity::execution
