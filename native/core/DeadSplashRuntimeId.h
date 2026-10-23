#pragma once

#include <cstdint>
#include <limits>

// Runtime-only dead-splash identity. It is interpreted together with a world
// epoch and is deliberately absent from every persistent substrate format.
using DeadSplashRuntimeId = std::uint64_t;

inline constexpr DeadSplashRuntimeId kInvalidDeadSplashRuntimeId = 0;
// UINT64_MAX is reserved as an exhaustion sentinel so allocation cannot wrap
// into the invalid identity.
inline constexpr DeadSplashRuntimeId kMaximumDeadSplashRuntimeId =
        std::numeric_limits<DeadSplashRuntimeId>::max() - 1;

[[nodiscard]] constexpr bool isValidDeadSplashRuntimeId(
        DeadSplashRuntimeId runtimeId) noexcept {
    return runtimeId != kInvalidDeadSplashRuntimeId
            && runtimeId <= kMaximumDeadSplashRuntimeId;
}
