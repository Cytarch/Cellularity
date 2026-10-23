#include "core/FoodData.h"

#include <algorithm>
#include <stdexcept>

#include "core/Environment.h"

FoodData::FoodData(int maxFoodCount)
        : MAX_FOOD_COUNT(maxFoodCount),
          mFoodCount(0),
          mPositionX(maxFoodCount, 0.0f),
          mPositionY(maxFoodCount, 0.0f),
          mMass(maxFoodCount, 0.0f),
          mVelocityX(maxFoodCount, 0.0f),
          mVelocityY(maxFoodCount, 0.0f),
          mLipidLayer(maxFoodCount, 0.0f),
          mCellCollisionGridIndex(maxFoodCount, -1),
          mCellCollisionGridIndexValid(maxFoodCount, false),
          mCellCollisionGridMappingGeneration(maxFoodCount, 0),
          mRuntimeIds(maxFoodCount, kInvalidFoodRuntimeId) {
}

void FoodData::copy(int from, int to) {
    mRuntimeIds[to] = mRuntimeIds[from];
    mPositionX[to] = mPositionX[from];
    mPositionY[to] = mPositionY[from];
    mMass[to] = mMass[from];
    mVelocityX[to] = mVelocityX[from];
    mVelocityY[to] = mVelocityY[from];
    mLipidLayer[to] = mLipidLayer[from];
    mCellCollisionGridIndex[to] = mCellCollisionGridIndex[from];
    mCellCollisionGridIndexValid[to] = mCellCollisionGridIndexValid[from];
    mCellCollisionGridMappingGeneration[to] =
            mCellCollisionGridMappingGeneration[from];
}

void FoodData::resizeActiveFoodCount(int newFoodCount) {
    if (newFoodCount < 0 || newFoodCount > MAX_FOOD_COUNT) {
        throw std::length_error("FoodData active Food count is out of range");
    }

    if (newFoodCount == mFoodCount) {
        return;
    }

    if (newFoodCount > mFoodCount) {
        const std::size_t addedCount = static_cast<std::size_t>(
                newFoodCount - mFoodCount);
        ensureRuntimeIdsAvailable(addedCount);
        for (int foodIndex = mFoodCount;
                foodIndex < newFoodCount;
                ++foodIndex) {
            mRuntimeIds[static_cast<std::size_t>(foodIndex)] =
                    allocateRuntimeId();
            mCellCollisionGridIndex[foodIndex] = -1;
            mCellCollisionGridIndexValid[foodIndex] = false;
            mCellCollisionGridMappingGeneration[foodIndex] = 0;
        }
    } else {
        std::fill(mRuntimeIds.begin() + newFoodCount,
                mRuntimeIds.begin() + mFoodCount,
                kInvalidFoodRuntimeId);
    }
    mFoodCount = newFoodCount;
    advanceSpatialGeneration();
}

void FoodData::replaceActiveFoodCount(int newFoodCount) {
    if (newFoodCount < 0 || newFoodCount > MAX_FOOD_COUNT) {
        throw std::length_error("FoodData replacement Food count is out of range");
    }

    ensureRuntimeIdsAvailable(static_cast<std::size_t>(newFoodCount));
    const int previousFoodCount = mFoodCount;
    for (int foodIndex = 0; foodIndex < newFoodCount; ++foodIndex) {
        mRuntimeIds[static_cast<std::size_t>(foodIndex)] = allocateRuntimeId();
        mCellCollisionGridIndex[foodIndex] = -1;
        mCellCollisionGridIndexValid[foodIndex] = false;
        mCellCollisionGridMappingGeneration[foodIndex] = 0;
    }
    if (newFoodCount < previousFoodCount) {
        std::fill(mRuntimeIds.begin() + newFoodCount,
                mRuntimeIds.begin() + previousFoodCount,
                kInvalidFoodRuntimeId);
    }
    mFoodCount = newFoodCount;
    advanceSpatialGeneration();
}

void FoodData::removeFood(int foodIndex) {
    if (foodIndex < 0 || foodIndex >= mFoodCount) {
        throw std::out_of_range("FoodData cannot remove an inactive Food slot");
    }
    const int lastFoodIndex = mFoodCount - 1;
    if (foodIndex != lastFoodIndex) {
        copy(lastFoodIndex, foodIndex);
    }
    --mFoodCount;
    mRuntimeIds[static_cast<std::size_t>(mFoodCount)] = kInvalidFoodRuntimeId;
    mCellCollisionGridIndex[mFoodCount] = -1;
    mCellCollisionGridIndexValid[mFoodCount] = false;
    mCellCollisionGridMappingGeneration[mFoodCount] = 0;
    advanceSpatialGeneration();
}

void FoodData::invalidateCellCollisionGridMapping(int foodIndex) noexcept {
    assert(foodIndex >= 0 && foodIndex < mFoodCount);
    mCellCollisionGridIndexValid[foodIndex] = false;
    mCellCollisionGridMappingGeneration[foodIndex] = 0;
}

void FoodData::markFoodPositionChanged(int foodIndex) noexcept {
    invalidateCellCollisionGridMapping(foodIndex);
    advanceSpatialGeneration();
}

void FoodData::markSpatialStateChanged() noexcept {
    advanceSpatialGeneration();
}

void FoodData::advanceSpatialGeneration() noexcept {
    ++mSpatialGeneration;
    if (mSpatialGeneration == 0) {
        mSpatialGeneration = 1;
    }
}

FoodRuntimeId FoodData::allocateRuntimeId() {
    ensureRuntimeIdsAvailable(1);
    return mNextRuntimeId++;
}

void FoodData::ensureRuntimeIdsAvailable(std::size_t count) const {
    const FoodRuntimeId available =
            kMaximumFoodRuntimeId - mNextRuntimeId + 1;
    if (count > available) {
        throw std::overflow_error("FoodData runtime Food identity space is exhausted");
    }
}

double FoodData::getFoodMass() const {
    double food = 0.0;
    for (int i = 0; i < mFoodCount; i++) {
        food += mMass[i];
    }
    return food;
}

int FoodData::addFood(double positionX, double positionY, double mass, float lipidLayer, const Environment& environment) {
    if (mFoodCount >= MAX_FOOD_COUNT) {
        return -1;
    }

    if (environment.geometry().isOutsideOrOnBoundary(positionX, positionY)) {
        return -1;
    }

    const int foodIndex = mFoodCount;
    mRuntimeIds[static_cast<std::size_t>(foodIndex)] = allocateRuntimeId();
    mPositionX[foodIndex] = static_cast<float>(positionX);
    mPositionY[foodIndex] = static_cast<float>(positionY);
    mMass[foodIndex] = static_cast<float>(mass);
    mVelocityX[foodIndex] = 0.0f;
    mVelocityY[foodIndex] = 0.0f;
    mLipidLayer[foodIndex] = lipidLayer;
    mCellCollisionGridIndex[foodIndex] = -1;
    mCellCollisionGridIndexValid[foodIndex] = false;
    mCellCollisionGridMappingGeneration[foodIndex] = 0;
    ++mFoodCount;
    advanceSpatialGeneration();
    return foodIndex;
}

void FoodData::addRandomFood(double deltaTime, Environment& environment) {
    const auto geometry = environment.geometry();
    double spawnIntensity = environment.mFoodSpeed * geometry.radius() * geometry.radius()
            / environment.mFoodSize / (1.0 + environment.mFoodLumpiness) * deltaTime;
    if (geometry.isRing()) {
        spawnIntensity *= 1.0 - (geometry.innerRadius() / geometry.radius()) * (geometry.innerRadius() / geometry.radius());
    }

    int clustersCount = environment.mRandom.nextPoisson(spawnIntensity);

    for (int clusterIndex = 0; clusterIndex < clustersCount; clusterIndex++) {
        const auto clusterCenter = geometry.samplePoint(environment.mRandom);
        const double clusterCenterX = clusterCenter[0];
        const double clusterCenterY = clusterCenter[1];

        int foodCountInCluster = 1;
        if (environment.mFoodLumpiness > 0.0) {
            foodCountInCluster = environment.mRandom.nextPoisson(1.0 + environment.mFoodLumpiness);
        }

        for (int j = 0; j < foodCountInCluster; j++) {
            double offsetX = environment.mRandom.nextGaussian() * environment.mFoodLumpSize;
            double offsetY = environment.mRandom.nextGaussian() * environment.mFoodLumpSize;
            double foodX = clusterCenterX + offsetX;
            double foodY = clusterCenterY + offsetY;
            addFood(foodX,
                    foodY,
                    environment.mFoodSize,
                    static_cast<float>(environment.getFoodLipidLayer(foodX, foodY)),
                    environment);
        }
    }
}
