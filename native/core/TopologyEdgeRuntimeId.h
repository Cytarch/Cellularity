#pragma once

#include <cstdint>
#include <limits>

// Runtime-only identity of a normal CellTopology edge. It is interpreted
// together with the world epoch and is deliberately absent from persistent
// substrate formats.
using TopologyEdgeRuntimeId = std::uint64_t;

inline constexpr TopologyEdgeRuntimeId kInvalidTopologyEdgeRuntimeId = 0;
// UINT64_MAX remains an allocation-exhaustion sentinel so incrementing the
// monotonic counter can never wrap into the invalid identity.
inline constexpr TopologyEdgeRuntimeId kMaximumTopologyEdgeRuntimeId =
        std::numeric_limits<TopologyEdgeRuntimeId>::max() - 1;

[[nodiscard]] constexpr bool isValidTopologyEdgeRuntimeId(
        TopologyEdgeRuntimeId runtimeId) noexcept {
    return runtimeId != kInvalidTopologyEdgeRuntimeId
            && runtimeId <= kMaximumTopologyEdgeRuntimeId;
}
