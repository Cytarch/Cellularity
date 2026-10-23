#include "core/SimulationRandom.h"

#include <cmath>
#include <limits>

#include "verification/SemanticTrace.h"

SimulationRandom SimulationRandom::global;

SimulationRandom::SimulationRandom(int64_t seed) {
    setSeed(seed);
}

int64_t SimulationRandom::nextLong() {
    // uint64_t keeps the right shift logical and preserves the established sequence.
#if defined(CELLULARITY_SEMANTIC_TRACE)
    const uint64_t previousSeed = mSeed;
#endif
    mSeed ^= (mSeed << 21);
    mSeed ^= (mSeed >> 35);
    mSeed ^= (mSeed << 4);
    CELLULARITY_SEMANTIC_EVENT(
            cellularity::verification::TraceChannel::Apply,
            cellularity::verification::SemanticEventKind::RandomLong,
            cellularity::verification::traceU64(previousSeed),
            cellularity::verification::traceU64(mSeed));
    return static_cast<int64_t>(mSeed);
}

double SimulationRandom::nextUniformSymmetric() {
    auto minVal = static_cast<double>(std::numeric_limits<int64_t>::min());
    return -(static_cast<double>(nextLong()) / minVal);
}

double SimulationRandom::nextUniform() {
    return 0.5 * (nextUniformSymmetric() + 1.0);
}

double SimulationRandom::nextGaussian() {
    constexpr double PI = 3.14159265358979323846;
    double u1;
    do {
        u1 = nextUniform();
    } while (u1 <= std::numeric_limits<double>::min());

    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * PI * nextUniform());
}

int SimulationRandom::nextInt(int bound) {
    if (bound <= 0) {
        return 0;
    }
    int i = static_cast<int>(nextLong() % static_cast<int64_t>(bound));
    return i < 0 ? -i : i;
}

int SimulationRandom::nextPoisson(double lambda) {
    double L = std::exp(-lambda);
    double p = 1.0;
    int k = 0;

    do {
        k++;
        p *= nextUniform();
    } while (p > L);

    return k - 1;
}

int64_t SimulationRandom::getSeed() const {
    return static_cast<int64_t>(mSeed);
}

void SimulationRandom::setSeed(int64_t newSeed) {
    if (newSeed == 0) {
        newSeed = 1337;
    }
    mSeed = static_cast<uint64_t>(newSeed);
}
