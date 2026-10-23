#pragma once

#include <cstdint>

#include "core/CellSweepBroadphase.h"

class CellData;
class Environment;
class RenderContactChordBuffer;

namespace cellularity {
class SubstrateGeometry;
struct SubstrateBoundaryContact;
} // namespace cellularity

enum class RenderContactInput : std::uint8_t {
    FreshPhysicsContacts = 0,
    CurrentGeometry = 1,
};

// Builds transient render-contact chords without applying simulation policy.
// Fresh physics contacts are the normal hot path. CurrentGeometry is the
// render-only fallback after loading or direct world edits invalidate them.
class RenderContactCollector final {
public:
    RenderContactCollector();

    RenderContactCollector(const RenderContactCollector&) = delete;
    RenderContactCollector& operator=(const RenderContactCollector&) = delete;
    RenderContactCollector(RenderContactCollector&&) = delete;
    RenderContactCollector& operator=(RenderContactCollector&&) = delete;

    void collect(RenderContactInput input,
            const CellData& cellData,
            const Environment& environment,
            RenderContactChordBuffer& output);

private:
    void collectPhysicsContacts(const CellData& cellData,
            RenderContactChordBuffer& output) const;
    void collectCurrentGeometry(const CellData& cellData,
            RenderContactChordBuffer& output);
    static void collectBoundaryContacts(const CellData& cellData,
            const Environment& environment,
            RenderContactChordBuffer& output);
    static void appendCellPairChords(const CellData& cellData,
            RenderContactChordBuffer& output,
            int currentCellIndex,
            int collidedCellIndex,
            double contactChordLengthSquared,
            double distance,
            double normalX,
            double normalY);
    static void appendBoundaryChord(const CellData& cellData,
            RenderContactChordBuffer& output,
            int currentCellIndex,
            const cellularity::SubstrateGeometry& geometry,
            const cellularity::SubstrateBoundaryContact& contact);

    // This scratch is intentionally separate from SimulationWorld's physics
    // broadphase. A render-only collection must not influence a later
    // simulation scan through retained sort/layout state.
    CellSweepBroadphase mCurrentGeometryBroadphase;
};
