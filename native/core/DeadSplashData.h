#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

#include "core/DeadSplashRuntimeId.h"

class DeadSplashData {
public:
    static const float RADIUS;
    static constexpr double MAX_AGE = 1.0;

    const int MAX_DEAD_SPLASH_COUNT;
    int mDeadSplashCount;

    std::vector<float> mPositionX;
    std::vector<float> mPositionY;
    std::vector<float> mRadius;
    std::vector<float> mAge;
    std::vector<std::array<float, 4>> mColor;

    explicit DeadSplashData(int maxDeadSplashCount);

    void copy(int from, int to);
    int addDeadSplash(float positionX,
            float positionY,
            float radius,
            const std::array<float, 4>& color);
    void resizeActiveDeadSplashCount(int newDeadSplashCount);
    void replaceActiveDeadSplashCount(int newDeadSplashCount);
    void removeDeadSplash(int deadSplashIndex);

    [[nodiscard]] DeadSplashRuntimeId runtimeId(
            int deadSplashIndex) const noexcept {
        assert(deadSplashIndex >= 0
                && deadSplashIndex < mDeadSplashCount);
        return mRuntimeIds[static_cast<std::size_t>(deadSplashIndex)];
    }

    void updateDeadSplashes(double deltaTime);

private:
    std::vector<DeadSplashRuntimeId> mRuntimeIds;
    DeadSplashRuntimeId mNextRuntimeId = 1;

    [[nodiscard]] DeadSplashRuntimeId allocateRuntimeId();
    void ensureRuntimeIdsAvailable(std::size_t count) const;
};
