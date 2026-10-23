#pragma once

#include <cstdint>

namespace cellularity::execution {

// Only floating-point control policy belongs here. Sticky exception/status
// flags are outputs of arithmetic and are intentionally not copied between
// threads.
struct FloatingPointEnvironment final {
    int roundingMode = -1;
    std::uint64_t architectureControl = 0;
    bool hasArchitectureControl = false;
    bool valid = false;
};

[[nodiscard]] FloatingPointEnvironment
captureFloatingPointEnvironment() noexcept;

// Applies the captured rounding/denormal policy and verifies the result.
[[nodiscard]] bool applyFloatingPointEnvironment(
        const FloatingPointEnvironment& environment) noexcept;

[[nodiscard]] bool floatingPointEnvironmentMatches(
        const FloatingPointEnvironment& expected) noexcept;

} // namespace cellularity::execution
