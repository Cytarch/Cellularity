#pragma once

#include <cstddef>

#include "render/frame/RenderContactChordResolver.h"

namespace cellularity::render {

struct PreparedRenderFrame;
struct RenderContactChordNormalizationProfile;
struct RenderSourceFrame;

// Builds only the base Cell perimeter/fan mesh and its bucket metadata from
// immutable render DTOs. Outline, Food, Link and GL work are out of scope.
class CellBoundaryMeshBuilder final {
public:
    void reset(std::size_t maximumVertexCount,
            std::size_t maximumIndexCount) noexcept;
    void
    build(const RenderSourceFrame& source, PreparedRenderFrame& frame,
            RenderContactChordResolver& contactChordResolver,
            RenderContactChordResolver::Profile* contactChordProfile,
            RenderContactChordNormalizationProfile* normalizationProfile) const;

private:
    std::size_t mMaximumVertexCount = 0;
    std::size_t mMaximumIndexCount = 0;
};

} // namespace cellularity::render
