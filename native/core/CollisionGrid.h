#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

class CollisionGrid {
public:
    CollisionGrid(double worldRadius, int gridSize, int entryLimit, bool hasTypes);

    int convertWorldToGridPosition(double position) const {
        if (!std::isfinite(position)) {
            return -1;
        }
        if (position < -mWorldRadius) {
            return -1;
        }
        if (position > mWorldRadius) {
            return mStride;
        }
        const double gridPosition =
                (mWorldRadius + position) / (2.0 * mWorldRadius) * mGridSize + 1.0;
        if (!std::isfinite(gridPosition) || gridPosition < 0.0) {
            return -1;
        }
        if (gridPosition >= static_cast<double>(mStride)) {
            return mStride;
        }
        return static_cast<int>(gridPosition);
    }

    int indexAtWorldPosition(double x, double y) const {
        return indexAtGridPosition(convertWorldToGridPosition(x), convertWorldToGridPosition(y));
    }

    int indexAtGridPosition(int gridPositionX, int gridPositionY) const {
        if (!containsGridPosition(gridPositionX, gridPositionY)) {
            return -1;
        }
        return gridPositionX + gridPositionY * mStride;
    }

    int stride() const {
        return mStride;
    }

    double worldRadius() const {
        return mWorldRadius;
    }

    int gridSize() const {
        return mGridSize;
    }

    int countAt(int gridIndex) const {
        return mCounts[gridIndex];
    }

    int entryOffsetAt(int gridIndex) const {
        return gridIndex * mEntryLimit;
    }

    int indexAtOffset(int entryOffset) const {
        return mIndexes[entryOffset];
    }

    int touchedCount() const {
        return static_cast<int>(mTouchedIndexes.size());
    }

    int touchedIndexAt(int touchedOffset) const {
        return mTouchedIndexes[touchedOffset];
    }

    int16_t typeAtOffset(int entryOffset) const {
        if (!mHasTypes) {
            throw std::logic_error("CollisionGrid was created without entry types");
        }
        return mTypes[entryOffset];
    }

    int clampGridPosition(int gridPosition) const {
        if (gridPosition < 0) {
            return 0;
        }
        return gridPosition > mGridSize + 1 ? mGridSize + 1 : gridPosition;
    }

    bool containsGridPosition(int gridPositionX, int gridPositionY) const {
        return gridPositionX >= 0 && gridPositionX < mStride && gridPositionY >= 0 && gridPositionY < mStride;
    }

    bool containsIndex(int gridIndex) const {
        return gridIndex >= 0 && gridIndex < static_cast<int>(mCounts.size());
    }

    void clear();

    bool add(int gridIndex, int value) {
        if (!containsIndex(gridIndex)) {
            return false;
        }
        int count = mCounts[gridIndex];
        if (count >= mEntryLimit) {
            return false;
        }

        if (count == 0) {
            mTouchedIndexes.push_back(gridIndex);
        }
        mIndexes[entryOffsetAt(gridIndex) + count] = value;
        mCounts[gridIndex] = count + 1;
        return true;
    }

    bool add(int gridIndex, int value, int16_t type) {
        if (!mHasTypes) {
            throw std::logic_error("CollisionGrid was created without entry types");
        }

        if (!containsIndex(gridIndex)) {
            return false;
        }
        int count = mCounts[gridIndex];
        if (count >= mEntryLimit) {
            return false;
        }

        if (count == 0) {
            mTouchedIndexes.push_back(gridIndex);
        }
        int entryOffset = entryOffsetAt(gridIndex) + count;
        mIndexes[entryOffset] = value;
        mTypes[entryOffset] = type;
        mCounts[gridIndex] = count + 1;
        return true;
    }

private:
    double mWorldRadius;
    int mGridSize;
    int mEntryLimit;
    bool mHasTypes;
    int mStride;
    std::vector<int> mIndexes;
    std::vector<int16_t> mTypes;
    std::vector<int> mCounts;
    std::vector<int> mTouchedIndexes;
};
