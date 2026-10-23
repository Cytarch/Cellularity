#pragma once

#include <cstdint>

class Environment;

namespace cellularity::memory {

struct RuntimeLimits {
    unsigned int addressBits;
    std::uint64_t absoluteSubstrateFileBytes;
    std::uint64_t operationalSubstrateFileBytes;
    std::uint64_t worldAllocationBudgetBytes;
};

struct WorldAllocationEstimate {
    std::uint64_t bytes;
    std::uint64_t budgetBytes;
    std::uint64_t rendererBudgetedBytes;
};

[[nodiscard]] RuntimeLimits
runtimeLimitsForAddressBits(unsigned int addressBits);
[[nodiscard]] RuntimeLimits
runtimeLimitsForDevice(unsigned int addressBits,
        std::uint64_t physicalMemoryBytes);
[[nodiscard]] RuntimeLimits currentRuntimeLimits();

/**
 * Returns a conservative peak estimate for one world and its bounded
 * microscope frame exchanges, including reusable high-water and Ghost
 * snapshot storage. Every multiply and addition is checked before it can wrap;
 * an unrepresentable configuration is rejected.
 */
[[nodiscard]] WorldAllocationEstimate
estimateWorldAllocation(const Environment& environment,
        unsigned int addressBits);
[[nodiscard]] WorldAllocationEstimate
estimateCurrentWorldAllocation(const Environment& environment);

void validateWorldAllocation(const Environment& environment);
[[nodiscard]] bool
isWorldAllocationSupported(const Environment& environment) noexcept;

void validateSubstrateFileSize(std::uint64_t fileSizeBytes,
        const RuntimeLimits& limits);

} // namespace cellularity::memory
