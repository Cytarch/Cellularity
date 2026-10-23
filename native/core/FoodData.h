#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/FoodRuntimeId.h"

class Environment;

class FoodData {
public:
    static constexpr float RADIUS = 0.007f;

    const int MAX_FOOD_COUNT;
    int mFoodCount;

    std::vector<float> mPositionX;
    std::vector<float> mPositionY;
    std::vector<float> mMass;
    std::vector<float> mVelocityX;
    std::vector<float> mVelocityY;
    std::vector<float> mLipidLayer;
    std::vector<int> mCellCollisionGridIndex;
    std::vector<uint8_t> mCellCollisionGridIndexValid;
    std::vector<std::uint64_t> mCellCollisionGridMappingGeneration;

    explicit FoodData(int maxFoodCount);

    void copy(int from, int to);
    void resizeActiveFoodCount(int newFoodCount);
    void replaceActiveFoodCount(int newFoodCount);
    void removeFood(int foodIndex);
    void invalidateCellCollisionGridMapping(int foodIndex) noexcept;
    void markFoodPositionChanged(int foodIndex) noexcept;
    void markSpatialStateChanged() noexcept;

    [[nodiscard]] std::uint64_t spatialGeneration() const noexcept {
        return mSpatialGeneration;
    }

    [[nodiscard]] FoodRuntimeId runtimeId(int foodIndex) const noexcept {
        assert(foodIndex >= 0 && foodIndex < mFoodCount);
        return mRuntimeIds[static_cast<std::size_t>(foodIndex)];
    }

    double getFoodMass() const;

    int addFood(double positionX, double positionY, double mass, float lipidLayer, const Environment& environment);
    void addRandomFood(double deltaTime, Environment& environment);

private:
    std::vector<FoodRuntimeId> mRuntimeIds;
    FoodRuntimeId mNextRuntimeId = 1;
    std::uint64_t mSpatialGeneration = 1;

    [[nodiscard]] FoodRuntimeId allocateRuntimeId();
    void ensureRuntimeIdsAvailable(std::size_t count) const;
    void advanceSpatialGeneration() noexcept;
};
