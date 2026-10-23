#pragma once

#include <cstddef>

#include "render/frame/RenderContactChordResolver.h"

namespace cellularity::render {

struct PreparedRenderFrame;
struct RenderSourceFrame;

// Builds Link endpoint triangles and texture coordinates from immutable
// render DTOs.
// The builder owns no simulation or graphics-API state.
class RenderLinkGeometryBuilder final {
public:
    struct Profile final {
        int topologyEdgeCount = 0;
        int topologyBoundaryCount = 0;
        int candidateEdgeCount = 0;
        int candidateBoundaryCount = 0;
        int contactCrossSectionCount = 0;
        int normalFallbackCount = 0;
        int boundaryFallbackCount = 0;
        int discardedContactCount = 0;
        int degenerateLinkCount = 0;
    };

    void reset(std::size_t maximumVertexCount) noexcept;
    void build(const RenderSourceFrame& source, PreparedRenderFrame& frame,
            RenderContactChordResolver& contactChordResolver,
            RenderContactChordResolver::Profile* contactChordProfile,
            RenderContactChordNormalizationProfile* normalizationProfile,
            Profile* profile) const;

private:
    std::size_t mMaximumVertexCount = 0;
};

} // namespace cellularity::render
