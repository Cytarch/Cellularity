#pragma once

#include <cstddef>

namespace cellularity::execution {

// This is deliberately a small project limit, applied before constructing a
// Highway pool. It bounds the laboratory sweep without turning the detected
// processor count into a production performance policy.
inline constexpr std::size_t kProjectMaxAdditionalThreads = 7;

enum class ProcessorCountSource {
    Unavailable,
    Affinity,
    HardwareConcurrency,
};

struct DispatcherCapacity final {
    bool threadingSupported = false;
    ProcessorCountSource processorCountSource = ProcessorCountSource::Unavailable;
    std::size_t affinityProcessorCount = 0;
    unsigned int hardwareConcurrency = 0;
    std::size_t usableProcessorCount = 0;
    std::size_t processorMaxAdditionalThreads = 0;
    std::size_t highwayReportedMaxAdditionalThreads = 0;
    std::size_t projectMaxAdditionalThreads = kProjectMaxAdditionalThreads;
    std::size_t safeMaxAdditionalThreads = 0;
};

[[nodiscard]] DispatcherCapacity detectDispatcherCapacity();

[[nodiscard]] std::size_t
clampAdditionalThreadCount(std::size_t requestedAdditionalThreads,
        const DispatcherCapacity& capacity) noexcept;

[[nodiscard]] std::size_t deriveSafeAdditionalThreadCount(
        bool threadingSupported, std::size_t usableProcessorCount,
        std::size_t highwayReportedMaxAdditionalThreads,
        std::size_t projectMaxAdditionalThreads) noexcept;

[[nodiscard]] const char*
processorCountSourceName(ProcessorCountSource source) noexcept;

} // namespace cellularity::execution
