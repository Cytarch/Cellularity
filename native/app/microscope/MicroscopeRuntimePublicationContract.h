#pragma once

#include <cstdint>

namespace cellularity::microscope {

inline constexpr std::int64_t kPublicationIntervalNanos = 50'000'000LL;
inline constexpr std::int64_t kPublicationIntervalMillis =
        kPublicationIntervalNanos / 1'000'000LL;

static_assert(kPublicationIntervalNanos > 0);
static_assert(kPublicationIntervalNanos % 1'000'000LL == 0);

} // namespace cellularity::microscope
