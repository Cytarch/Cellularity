#pragma once

#include <array>

#include "core/CellDataConstants.h"
#include "core/RenderContactChord.h"

namespace cellularity::render {

struct RenderContactChordNormalizationProfile final {
    long long calls = 0;
    long long skippedCalls = 0;
    long long contactChordCountSum = 0;
    long long firstPassPairs = 0;
    long long firstPassDiscardedSkips = 0;
    long long firstPassIntersections = 0;
    long long firstPassJoinMarks = 0;
    long long firstPassDiscardMarks = 0;
    long long secondPassPairs = 0;
    long long secondPassSelfSkips = 0;
    long long secondPassEndpointSkips = 0;
    long long secondPassDiscardMarks = 0;
    long long sortComparisons = 0;
    long long sortMoves = 0;
    long long firstPassNanos = 0;
    long long secondPassNanos = 0;
    long long sortNanos = 0;
};

void clipAndOrderRenderContactChords(
        RenderContactChord* contactChords, int contactChordCount,
        RenderContactChordNormalizationProfile* profile = nullptr);

inline void clipAndOrderRenderContactChords(
        std::array<RenderContactChord, CellDataConstants::MAX_NEIGHBORS>& contactChords,
        int contactChordCount,
        RenderContactChordNormalizationProfile* profile = nullptr) {
    clipAndOrderRenderContactChords(contactChords.data(), contactChordCount,
            profile);
}

} // namespace cellularity::render
