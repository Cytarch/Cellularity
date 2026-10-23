#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "execution/DispatcherCapacity.h"
#include "hwy/contrib/thread_pool/thread_pool.h"

namespace allocation_probe {

std::atomic<bool> tracking{false};
std::atomic<std::uint64_t> allocationCount{0};

void recordAllocation() noexcept {
    if (tracking.load(std::memory_order_relaxed)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
    }
}

void* allocate(std::size_t size) {
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    recordAllocation();
    return memory;
}

void* allocateAligned(std::size_t size, std::size_t alignment) {
    void* memory = nullptr;
#if defined(_WIN32)
    memory = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
    if (posix_memalign(&memory, alignment, size == 0 ? 1 : size) != 0) {
        memory = nullptr;
    }
#endif
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    recordAllocation();
    return memory;
}

void freeAligned(void* memory) noexcept {
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

void begin() noexcept {
    allocationCount.store(0, std::memory_order_relaxed);
    tracking.store(true, std::memory_order_release);
}

std::uint64_t end() noexcept {
    tracking.store(false, std::memory_order_release);
    return allocationCount.load(std::memory_order_relaxed);
}

} // namespace allocation_probe

void* operator new(std::size_t size) {
    return allocation_probe::allocate(size);
}

void* operator new[](std::size_t size) {
    return allocation_probe::allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocation_probe::allocateAligned(size,
            static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocation_probe::allocateAligned(size,
            static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment,
        const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocateAligned(
                size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, std::align_val_t alignment,
        const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocateAligned(
                size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete(void* memory, std::align_val_t,
        const std::nothrow_t&) noexcept {
    allocation_probe::freeAligned(memory);
}

void operator delete[](void* memory, std::align_val_t,
        const std::nothrow_t&) noexcept {
    allocation_probe::freeAligned(memory);
}

namespace {

std::atomic<std::uint64_t> sink{0};

void verifyMode(std::size_t additionalThreads, hwy::PoolWaitMode mode) {
    hwy::ThreadPool pool(additionalThreads);
    if (mode == hwy::PoolWaitMode::kSpin) {
        pool.SetWaitMode(mode);
    }
    alignas(64) std::array<std::uint64_t, 128> results{};
    auto dispatch = [&] {
        pool.Run(0, results.size(),
                [&](std::uint64_t task, std::size_t worker) noexcept {
                    std::uint64_t value = task + worker + 1U;
                    for (int iteration = 0; iteration < 64; ++iteration) {
                        value = (value ^ (value >> 13U)) * 0x9E37'79B9'7F4A'7C15ULL;
                    }
                    results[static_cast<std::size_t>(task)] = value;
                });
    };

    int autotuneRuns = 0;
    do {
        dispatch();
        ++autotuneRuns;
    } while (pool.NumWorkers() > 1 && !pool.AutoTuneComplete() && autotuneRuns < 20'000);
    if (pool.NumWorkers() > 1 && !pool.AutoTuneComplete()) {
        std::cerr << "Highway autotune did not complete in allocation test\n";
        std::exit(1);
    }
    for (int warmup = 0; warmup < 32; ++warmup) {
        dispatch();
    }

    allocation_probe::begin();
    for (int repeat = 0; repeat < 1000; ++repeat) {
        dispatch();
    }
    const std::uint64_t allocations = allocation_probe::end();
    for (const std::uint64_t value : results) {
        sink.fetch_xor(value, std::memory_order_relaxed);
    }
    if (allocations != 0) {
        std::cerr << "Steady Highway dispatch allocated " << allocations
                  << " times in "
                  << (mode == hwy::PoolWaitMode::kBlock ? "block" : "spin")
                  << " mode\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    const auto capacity = cellularity::execution::detectDispatcherCapacity();
    const std::size_t additionalThreads =
            std::min<std::size_t>(2, capacity.safeMaxAdditionalThreads);
    verifyMode(additionalThreads, hwy::PoolWaitMode::kBlock);
    if (additionalThreads > 0) {
        verifyMode(additionalThreads, hwy::PoolWaitMode::kSpin);
    }
    (void)sink.load(std::memory_order_relaxed);
    return 0;
}
