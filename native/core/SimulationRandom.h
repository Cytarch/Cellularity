#pragma once

#include <cstdint>

class SimulationRandom {
private:
    uint64_t mSeed = 1337;

public:
    // Shared by simulation operations that are not owned by an Environment instance.
    static SimulationRandom global;

    SimulationRandom() = default;
    explicit SimulationRandom(int64_t seed);

    int64_t nextLong();
    double nextUniformSymmetric();
    double nextUniform();
    double nextGaussian();
    int nextInt(int bound);
    int nextPoisson(double lambda);

    int64_t getSeed() const;
    void setSeed(int64_t newSeed);
};
