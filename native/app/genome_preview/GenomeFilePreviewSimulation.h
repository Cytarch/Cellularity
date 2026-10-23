#pragma once

#include <memory>

#include "core/GenomePool.h"
#include "core/OrganismId.h"

class SimulationWorld;

namespace cellularity::genome_preview {

inline constexpr int kGenomeFilePreviewPixels = 512;
inline constexpr double kGenomeFilePreviewMaximumTime = 48.0;

enum class DevelopmentStopReason {
    OffspringProduced,
    CellLimitReached,
    TimeLimitReached,
    OrganismExtinct
};

struct DevelopedGenomePreview final {
    std::unique_ptr<SimulationWorld> world;
    OrganismId organismId = kUnknownOrganismId;
    DevelopmentStopReason stopReason =
            DevelopmentStopReason::TimeLimitReached;
    int completedUpdates = 0;
};

struct GenomePreviewFraming final {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float halfExtent = 0.1f;
    int visibleCellCount = 0;
};

/**
 * Develops one deterministic founder until it produces a detached descendant
 * organism or reaches a bounded preview limit. Only the selected organism is
 * retained in the returned render world.
 */
[[nodiscard]] DevelopedGenomePreview developGenomeForFilePreview(
        const GenomeData& genome,
        int maximumCellCount,
        double maximumSimulationTime = kGenomeFilePreviewMaximumTime);

/** Returns square framing that includes Cell bodies and type-specific outlines. */
[[nodiscard]] GenomePreviewFraming frameGenomeFilePreview(
        const SimulationWorld& world);

} // namespace cellularity::genome_preview
