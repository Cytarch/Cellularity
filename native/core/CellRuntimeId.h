#pragma once

#include <cstdint>
#include <limits>

// Runtime-only Cell identity. It is paired with a world epoch across frames
// and is deliberately absent from every persistent substrate format.
using CellRuntimeId = std::uint64_t;

inline constexpr CellRuntimeId kInvalidCellRuntimeId = 0;
// Boundary contact chords have no Cell entity but need a stable, unambiguous
// key in the same compact neighbor-identity stream.
inline constexpr CellRuntimeId kBoundaryCellRuntimeId =
        std::numeric_limits<CellRuntimeId>::max();

[[nodiscard]] constexpr bool
isEntityCellRuntimeId(CellRuntimeId runtimeId) noexcept {
    return runtimeId != kInvalidCellRuntimeId && runtimeId != kBoundaryCellRuntimeId;
}
