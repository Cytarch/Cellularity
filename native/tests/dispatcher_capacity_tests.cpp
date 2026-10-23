#include <cstdlib>
#include <iostream>
#include <limits>

#include "execution/DispatcherCapacity.h"

namespace {

void expectEqual(std::size_t actual, std::size_t expected, const char* label) {
    if (actual == expected) {
        return;
    }
    std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using cellularity::execution::deriveSafeAdditionalThreadCount;
    const std::size_t pathological = std::numeric_limits<std::size_t>::max();
    expectEqual(deriveSafeAdditionalThreadCount(false, 64, 63, 7), 0,
            "threading unavailable");
    expectEqual(deriveSafeAdditionalThreadCount(true, 0, pathological, 7), 0,
            "unknown processor count");
    expectEqual(deriveSafeAdditionalThreadCount(true, 1, pathological, 7), 0,
            "single processor");
    expectEqual(deriveSafeAdditionalThreadCount(true, 2, pathological, 7), 1,
            "pathological Highway result is bounded by processors");
    expectEqual(deriveSafeAdditionalThreadCount(true, 20, pathological, 7), 7,
            "pathological Highway result is bounded by project cap");
    expectEqual(deriveSafeAdditionalThreadCount(true, 20, 3, 7), 3,
            "Highway lower bound is retained");

    cellularity::execution::DispatcherCapacity capacity;
    capacity.safeMaxAdditionalThreads = 4;
    expectEqual(cellularity::execution::clampAdditionalThreadCount(100, capacity),
            4, "request clamp");
    return 0;
}
