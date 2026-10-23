#pragma once

#include <cstdint>
#include <limits>

using OrganismId = std::uint64_t;

// Zero deliberately means "unknown", primarily for original Java saves. The
// loader never guesses identity from legacy topology. Known IDs identify one
// organism branch and change only when a division produces a detached branch.
inline constexpr OrganismId kUnknownOrganismId = 0;
// Organism IDs cross the JNI boundary as signed Java/Kotlin Long values.
inline constexpr OrganismId kMaximumOrganismId =
        static_cast<OrganismId>(std::numeric_limits<std::int64_t>::max());

[[nodiscard]] constexpr bool isKnownOrganismId(
        OrganismId organismId) noexcept {
    return organismId != kUnknownOrganismId
            && organismId <= kMaximumOrganismId;
}
