#pragma once

#include <cstdint>
#include <vector>

class SimulationWorld;

/** Internal CLCS message used by the Android cell inspector, never a file. */
[[nodiscard]] std::vector<std::uint8_t> writeCellSnapshotWire(
        const SimulationWorld& world,
        int cellId);
