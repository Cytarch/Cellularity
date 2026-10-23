#pragma once

#include <cstdint>

class SimulationWorld;

namespace cellularity::execution {
class SimulationExecutionContext;
} // namespace cellularity::execution

class NativeSimulationStepper {
public:
    void update(
            SimulationWorld& world, bool isForRendering, double speed,
            cellularity::execution::SimulationExecutionContext* executionContext = nullptr);
    void update1(
            SimulationWorld& world, bool isForRendering, double speed,
            cellularity::execution::SimulationExecutionContext* executionContext = nullptr);
    void update2(
            SimulationWorld& world, bool forView, double speed,
            cellularity::execution::SimulationExecutionContext* executionContext = nullptr);

    std::uint64_t completedUpdateCount() const;
    void resetCompletedUpdateCount();

private:
    std::uint64_t mCompletedUpdateCount = 0;
    bool mSplitUpdateInProgress = false;
};
