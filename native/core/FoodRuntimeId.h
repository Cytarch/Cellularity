#pragma once

#include <cstdint>
#include <limits>

// Runtime-only Food identity. It is interpreted together with a world epoch
// and is deliberately absent from every persistent substrate format.
using FoodRuntimeId = std::uint64_t;

inline constexpr FoodRuntimeId kInvalidFoodRuntimeId = 0;
// UINT64_MAX is reserved as an exhaustion sentinel so allocation cannot wrap
// into the invalid identity.
inline constexpr FoodRuntimeId kMaximumFoodRuntimeId =
        std::numeric_limits<FoodRuntimeId>::max() - 1;

[[nodiscard]] constexpr bool isValidFoodRuntimeId(
        FoodRuntimeId runtimeId) noexcept {
    return runtimeId != kInvalidFoodRuntimeId
            && runtimeId <= kMaximumFoodRuntimeId;
}
