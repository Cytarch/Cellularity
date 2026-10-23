#include "core/CollisionGrid.h"

CollisionGrid::CollisionGrid(double worldRadius, int gridSize, int entryLimit, bool hasTypes)
        : mWorldRadius(worldRadius),
          mGridSize(gridSize),
          mEntryLimit(entryLimit),
          mHasTypes(hasTypes),
          mStride(gridSize + 2),
          mIndexes(mStride * mStride * entryLimit),
          mTypes(hasTypes ? mStride * mStride * entryLimit : 0),
          mCounts(mStride * mStride) {
}

void CollisionGrid::clear() {
    for (int gridIndex : mTouchedIndexes) {
        mCounts[gridIndex] = 0;
    }
    mTouchedIndexes.clear();
}
