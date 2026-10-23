#include "PerfState.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "formats/release/ReleaseSubstrateCodec.h"

PerfJson capturePerfWorldState(const SimulationWorld& world) {
    int livingCells = 0;
    int mutations = 0;
    int activeSignalValues = 0;
    bool finiteState = true;

    for (int cellIndex = 0; cellIndex < world.mCellData.mCellCount; cellIndex++) {
        if (!world.mCellData.mIsDead[cellIndex]) {
            livingCells++;
        }
        mutations += world.mCellData.mMutationCount[cellIndex];
        for (int signalIndex = 0; signalIndex < CellData::SIGNAL_SUBSTANCE_COUNT; signalIndex++) {
            if (world.mCellData.mSignalSubstance[cellIndex][signalIndex] != 0.0f) {
                activeSignalValues++;
            }
        }
        finiteState = finiteState
                && std::isfinite(world.mCellData.mPositionX[cellIndex])
                && std::isfinite(world.mCellData.mPositionY[cellIndex])
                && std::isfinite(world.mCellData.mVelocityX[cellIndex])
                && std::isfinite(world.mCellData.mVelocityY[cellIndex])
                && std::isfinite(world.mCellData.mMass[cellIndex])
                && std::isfinite(world.mCellData.mNitrates[cellIndex]);
    }

    const auto snapshot =
            cellularity::save_format::release::encodeReleaseSubstrate(world);
    return {
            {"digest", cellularity::save_format::release::substrateSemanticDigest(world)},
            {"snapshotBytes", snapshot.bytes.size()},
            {"cells", world.mCellData.mCellCount},
            {"livingCells", livingCells},
            {"food", world.mFoodData.mFoodCount},
            {"links", world.mCellData.mTopology.edgeCount() + world.mCellData.mTopology.boundaryCount()},
            {"mutations", mutations},
            {"activeSignalValues", activeSignalValues},
            {"finite", finiteState}
    };
}
