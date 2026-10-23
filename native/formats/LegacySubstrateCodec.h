#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Environment.h"
#include "formats/SubstrateFormatTypes.h"

class SimulationWorld;

namespace cellularity::snapshot_codec {

struct LegacySubstrateLoadResult {
    std::unique_ptr<SimulationWorld> world;
    int productVersion = 0;
};

/** Frozen Java-format adapter that constructs the current runtime directly. */
LegacySubstrateLoadResult loadLegacySubstrateBytesDirect(
        std::vector<std::uint8_t> bytes,
        bool previewMode,
        const Environment& fallbackEnvironment);
LegacySubstrateLoadResult loadLegacySubstratePathDirect(
        const std::string& inputPath,
        bool previewMode,
        const Environment& fallbackEnvironment);

NativeSubstrateInspection inspectLegacySubstrateHeader(
        std::vector<std::uint8_t> bytes,
        bool previewMode,
        const Environment& fallbackEnvironment);
} // namespace cellularity::snapshot_codec
