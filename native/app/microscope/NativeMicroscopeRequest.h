#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "app/microscope/MicroscopeCommandMailbox.h"
#include "app/microscope/MicroscopeRuntimeStartConfiguration.h"

namespace cellularity::microscope {

enum class NativeMicroscopeRequestOperation : std::uint8_t {
    ResetWorld = 0,
    SaveSubstrate = 1,
    ApplyEnvironmentPatch = 2,
    InspectCell = 3,
    InspectEnvironment = 4,
    InspectStats = 5,
    InspectSelectedGenome = 6,
    InspectRuntimeConfiguration = 7,
};

// Value-only request storage shared by one non-UI caller and the native owner.
// The mailbox completion publishes the owner's writes to the waiting caller.
class NativeMicroscopeRequest final : public MicroscopeCommandPayload {
public:
    explicit NativeMicroscopeRequest(NativeMicroscopeRequestOperation operation)
            : operation(operation) {}

    NativeMicroscopeRequestOperation operation;
    std::vector<std::uint8_t> inputBytes;
    std::string path;
    int cellId = -1;

    std::vector<std::uint8_t> outputBytes;
    std::uint64_t outputToken = 0;
    bool hasOutput = false;
    std::uint64_t completedUpdateCount = 0;
    int cellCount = 0;
    double foodMass = 0.0;
    double simulationTime = 0.0;
    MicroscopeRuntimeStartConfiguration runtimeConfiguration{};
    std::exception_ptr failure;
};

} // namespace cellularity::microscope
