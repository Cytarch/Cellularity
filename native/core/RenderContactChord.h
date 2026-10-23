#pragma once

// A cell-local chord produced from a collision contact for render geometry.
// This is transient frame input, not a physical wall or topology edge.
struct RenderContactChord final {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    bool isDiscarded = false;
    bool joinsNextChord = false;
    int neighborCellIndex = 0;
};
