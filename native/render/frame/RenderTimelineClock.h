#pragma once

#include <cstdint>

#ifdef CELLULARITY_RENDER_TIMELINE
#include <chrono>
#endif

namespace cellularity::render {

// Timeline collection is a local diagnostic facility. Ordinary builds keep
// the value contract but execute no clock reads on the render path.
[[nodiscard]] inline std::int64_t renderTimelineNowNanos() noexcept {
#ifdef CELLULARITY_RENDER_TIMELINE
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
#else
    return 0;
#endif
}

} // namespace cellularity::render
