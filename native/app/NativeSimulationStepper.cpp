#include "app/NativeSimulationStepper.h"

#include "core/SimulationWorld.h"

void NativeSimulationStepper::update(
        SimulationWorld& world,
        bool isForRendering,
        double speed,
        cellularity::execution::SimulationExecutionContext* executionContext) {
    world.update(isForRendering, speed, executionContext);
    mCompletedUpdateCount++;
    mSplitUpdateInProgress = false;
}

void NativeSimulationStepper::update1(
        SimulationWorld& world,
        bool isForRendering,
        double speed,
        cellularity::execution::SimulationExecutionContext* executionContext) {
    world.update1(isForRendering, speed, executionContext);
    mSplitUpdateInProgress = true;
}

void NativeSimulationStepper::update2(
        SimulationWorld& world,
        bool forView,
        double speed,
        cellularity::execution::SimulationExecutionContext* executionContext) {
    world.update2(forView, speed, executionContext);
    if (mSplitUpdateInProgress) {
        mCompletedUpdateCount++;
        mSplitUpdateInProgress = false;
    }
}

std::uint64_t NativeSimulationStepper::completedUpdateCount() const {
    return mCompletedUpdateCount;
}

void NativeSimulationStepper::resetCompletedUpdateCount() {
    mCompletedUpdateCount = 0;
    mSplitUpdateInProgress = false;
}
