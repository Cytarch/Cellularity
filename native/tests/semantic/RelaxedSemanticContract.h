#pragma once

#include <functional>
#include <memory>
#include <string>

#include "core/SimulationWorld.h"

namespace cellularity::semantic_tests::level2 {

struct NumericalTolerance {
    double absolute = 0.0;
    double relative = 0.0;
};

struct ContractConfiguration {
    int shortHorizonUpdates = 32;
    int longRunUpdates = 1024;
    int visualCheckpointInterval = 128;
    NumericalTolerance doubleTolerance{1.0e-10, 1.0e-8};
    NumericalTolerance floatTolerance{2.0e-6, 2.0e-5};
    NumericalTolerance visualTolerance{2.0e-5, 2.0e-4};
};

using WorldFactory = std::function<std::unique_ptr<SimulationWorld>()>;
using WorldConfigurator = std::function<void(SimulationWorld&)>;

// Verifies one candidate against an independently constructed reference world.
// Discrete traces/state remain exact. Only numeric state produced from the
// same transition inputs is compared with the fixed tolerances above.
void verifyCandidate(const std::string& candidateName,
        const WorldFactory& factory,
        const WorldConfigurator& configureReference,
        const WorldConfigurator& configureCandidate,
        const ContractConfiguration& configuration = {});

// Guards the projection used by Level 2: changing only floating-point fields
// must not affect a discrete trace, while changing an integer field must.
void verifyDiscreteTraceProjection();

// Exercises the source comparator with independent mutations of every Link
// identity, lookup index and numeric field, including boundary records.
void verifyRenderSourceLinkComparison();

} // namespace cellularity::semantic_tests::level2
