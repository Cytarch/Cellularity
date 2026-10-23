#pragma once

#ifdef CELLULARITY_PROFILE

#include <chrono>

#include "core/SimulationWorldProfile.h"

namespace cellularity::profile {

// CoarseRegionTimer is intentionally limited to stage-sized regions. It must
// never be instantiated from a per-Cell, per-contact, or per-pair hot loop.
class CoarseRegionTimer final {
public:
    CoarseRegionTimer(SimulationWorldProfile* profile,
            long long SimulationWorldProfile::* target) noexcept
            : mProfile(profile), mTarget(target) {
        if (mProfile != nullptr) {
            mStartedAt = std::chrono::steady_clock::now();
        }
    }

    ~CoarseRegionTimer() {
        if (mProfile != nullptr) {
            mProfile->*mTarget +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - mStartedAt)
                            .count();
        }
    }

    CoarseRegionTimer(const CoarseRegionTimer&) = delete;
    CoarseRegionTimer& operator=(const CoarseRegionTimer&) = delete;

private:
    SimulationWorldProfile* mProfile;
    long long SimulationWorldProfile::* mTarget;
    std::chrono::steady_clock::time_point mStartedAt{};
};

} // namespace cellularity::profile

#define CELLULARITY_PROFILE_CONCAT_IMPL(left, right) left##right
#define CELLULARITY_PROFILE_CONCAT(left, right) CELLULARITY_PROFILE_CONCAT_IMPL(left, right)
#define CELLULARITY_PROFILE_COARSE_REGION(profile_ptr, field_name)                                             \
    ::cellularity::profile::CoarseRegionTimer CELLULARITY_PROFILE_CONCAT(cellularityProfileRegion_, __LINE__)( \
            (profile_ptr), &SimulationWorldProfile::field_name)

#else

#define CELLULARITY_PROFILE_COARSE_REGION(profile_ptr, field_name) ((void)0)

#endif
