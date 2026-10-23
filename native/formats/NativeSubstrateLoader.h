#pragma once

#include <memory>
#include <string>

#include "core/SimulationWorld.h"
#include "core/Environment.h"
#include "formats/SubstrateFormatTypes.h"

struct NativeSubstrateLoadResult {
    std::unique_ptr<SimulationWorld> world;
    NativeSubstrateFormatFamily formatFamily =
            NativeSubstrateFormatFamily::Unknown;
    /** Product version for Java legacy, content-schema major for release CLTY. */
    int familyLocalVersion = 0;
};

NativeSubstrateLoadResult loadNativeSubstrateFromPath(
        const std::string& path,
        bool previewMode,
        const Environment& fallbackEnvironment);

/** Full path validation; CLTY bodies are streamed without allocating a world. */
void validateNativeSubstratePath(
        const std::string& path,
        bool previewMode,
        const Environment& fallbackEnvironment);
