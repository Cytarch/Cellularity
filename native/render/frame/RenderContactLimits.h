#pragma once

#include "core/CellDataConstants.h"

namespace cellularity::render {

// Topology links remain governed by CellDataConstants::MAX_NEIGHBORS. Render
// geometry has to represent every retained physics contact independently and
// may also need one dish-boundary constraint.
inline constexpr int kMaximumCellContactChordsPerCell =
        CellDataConstants::MAX_COLLISIONS_PER_CELL;
inline constexpr int kMaximumBoundaryContactChordsPerCell = 1;
inline constexpr int kMaximumRenderContactChordsPerCell =
        kMaximumCellContactChordsPerCell + kMaximumBoundaryContactChordsPerCell;

static_assert(kMaximumRenderContactChordsPerCell > CellDataConstants::MAX_NEIGHBORS,
        "Render-contact capacity must not alias topology capacity");

} // namespace cellularity::render
