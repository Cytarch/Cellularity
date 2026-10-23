#include "core/DeadSplashData.h"

#include <algorithm>
#include <stdexcept>

#include "core/CellData.h"

const float DeadSplashData::RADIUS = static_cast<float>(CellData::MAX_CELL_RADIUS * 2.1037);

DeadSplashData::DeadSplashData(int maxDeadSplashCount) : MAX_DEAD_SPLASH_COUNT(maxDeadSplashCount),
                                                         mDeadSplashCount(0),
                                                         mPositionX(maxDeadSplashCount, 0.0f),
                                                         mPositionY(maxDeadSplashCount, 0.0f),
                                                         mRadius(maxDeadSplashCount, 0.0f),
                                                         mAge(maxDeadSplashCount, 0.0f),
                                                         mColor(maxDeadSplashCount, std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}),
                                                         mRuntimeIds(maxDeadSplashCount, kInvalidDeadSplashRuntimeId) {
}

void DeadSplashData::copy(int from, int to) {
    mRuntimeIds[to] = mRuntimeIds[from];
    mPositionX[to] = mPositionX[from];
    mPositionY[to] = mPositionY[from];
    mRadius[to] = mRadius[from];
    mAge[to] = mAge[from];
    mColor[to] = mColor[from];
}

int DeadSplashData::addDeadSplash(
        float positionX,
        float positionY,
        float radius,
        const std::array<float, 4>& color) {
    if (mDeadSplashCount >= MAX_DEAD_SPLASH_COUNT) {
        return -1;
    }
    const int deadSplashIndex = mDeadSplashCount;
    mRuntimeIds[static_cast<std::size_t>(deadSplashIndex)] =
            allocateRuntimeId();
    mPositionX[deadSplashIndex] = positionX;
    mPositionY[deadSplashIndex] = positionY;
    mRadius[deadSplashIndex] = radius;
    mAge[deadSplashIndex] = 0.0f;
    mColor[deadSplashIndex] = color;
    ++mDeadSplashCount;
    return deadSplashIndex;
}

void DeadSplashData::resizeActiveDeadSplashCount(int newDeadSplashCount) {
    if (newDeadSplashCount < 0
            || newDeadSplashCount > MAX_DEAD_SPLASH_COUNT) {
        throw std::length_error(
                "DeadSplashData active splash count is out of range");
    }

    if (newDeadSplashCount > mDeadSplashCount) {
        const std::size_t addedCount = static_cast<std::size_t>(
                newDeadSplashCount - mDeadSplashCount);
        ensureRuntimeIdsAvailable(addedCount);
        for (int deadSplashIndex = mDeadSplashCount;
                deadSplashIndex < newDeadSplashCount;
                ++deadSplashIndex) {
            mRuntimeIds[static_cast<std::size_t>(deadSplashIndex)] =
                    allocateRuntimeId();
        }
    } else {
        std::fill(mRuntimeIds.begin() + newDeadSplashCount,
                mRuntimeIds.begin() + mDeadSplashCount,
                kInvalidDeadSplashRuntimeId);
    }
    mDeadSplashCount = newDeadSplashCount;
}

void DeadSplashData::replaceActiveDeadSplashCount(int newDeadSplashCount) {
    if (newDeadSplashCount < 0
            || newDeadSplashCount > MAX_DEAD_SPLASH_COUNT) {
        throw std::length_error(
                "DeadSplashData replacement splash count is out of range");
    }

    ensureRuntimeIdsAvailable(static_cast<std::size_t>(newDeadSplashCount));
    const int previousDeadSplashCount = mDeadSplashCount;
    for (int deadSplashIndex = 0;
            deadSplashIndex < newDeadSplashCount;
            ++deadSplashIndex) {
        mRuntimeIds[static_cast<std::size_t>(deadSplashIndex)] =
                allocateRuntimeId();
    }
    if (newDeadSplashCount < previousDeadSplashCount) {
        std::fill(mRuntimeIds.begin() + newDeadSplashCount,
                mRuntimeIds.begin() + previousDeadSplashCount,
                kInvalidDeadSplashRuntimeId);
    }
    mDeadSplashCount = newDeadSplashCount;
}

void DeadSplashData::removeDeadSplash(int deadSplashIndex) {
    if (deadSplashIndex < 0 || deadSplashIndex >= mDeadSplashCount) {
        throw std::out_of_range(
                "DeadSplashData cannot remove an inactive splash slot");
    }
    const int lastDeadSplashIndex = mDeadSplashCount - 1;
    if (deadSplashIndex != lastDeadSplashIndex) {
        copy(lastDeadSplashIndex, deadSplashIndex);
    }
    --mDeadSplashCount;
    mRuntimeIds[static_cast<std::size_t>(mDeadSplashCount)] =
            kInvalidDeadSplashRuntimeId;
}

DeadSplashRuntimeId DeadSplashData::allocateRuntimeId() {
    ensureRuntimeIdsAvailable(1);
    return mNextRuntimeId++;
}

void DeadSplashData::ensureRuntimeIdsAvailable(std::size_t count) const {
    const DeadSplashRuntimeId available =
            kMaximumDeadSplashRuntimeId - mNextRuntimeId + 1;
    if (count > available) {
        throw std::overflow_error(
                "DeadSplashData runtime identity space is exhausted");
    }
}

void DeadSplashData::updateDeadSplashes(double deltaTime) {
    for (int deadCellIndex = 0; deadCellIndex < mDeadSplashCount; deadCellIndex++) {
        mAge[deadCellIndex] += static_cast<float>(0.5 * deltaTime);
        double s = mRadius[deadCellIndex] / CellData::MAX_CELL_RADIUS;
        if (mAge[deadCellIndex] > MAX_AGE * s * s) {
            removeDeadSplash(deadCellIndex);
            deadCellIndex--;
        }
    }
}
