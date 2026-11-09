#pragma once

// How a simulation controller comes into being, shared by the Android and desktop JNI
// bridges: a new plate from its allocation, or a stored plate from its path. Both bridges
// used to repeat this with small differences (the desktop ignored the dish's inner radius
// and filled in the creation time its own way); Android's behaviour is the reference.

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "app/NativeCellularityController.h"
#include "core/Environment.h"
#include "core/SimulationMemoryPolicy.h"
#include "core/SimulationWorld.h"
#include "formats/DecodedWorldValidation.h"
#include "formats/NativeSubstrateLoader.h"

namespace cellularity::app {

/** The environment a new plate is created with (Android's nativeCreate). */
[[nodiscard]] inline Environment newPlateEnvironment(bool previewMode, int maxCellCount,
        int maxFoodCount, double radius, double innerRadius) {
    Environment environment = previewMode
            ? Environment::setupPreviewSimulationEnvironment(maxCellCount)
            : Environment::constructDefaultEnvironment(maxCellCount, maxFoodCount, radius);
    if (!previewMode) {
        environment.mInnerRadius = innerRadius;
    }
    return environment;
}

/** A new plate's controller: validated allocation, creation time, runtime fields. */
[[nodiscard]] inline std::shared_ptr<NativeCellularityController> createNewPlateController(
        bool previewMode, const Environment& environment, std::int64_t createdAtUnixMillis) {
    cellularity::snapshot_codec::validateEnvironmentForAllocation(environment);
    if (createdAtUnixMillis <= 0) {
        throw std::invalid_argument("World creation time must be positive");
    }
    auto controller = std::make_shared<NativeCellularityController>(previewMode, environment);
    {
        auto access = controller->writeWorld();
        access.world().mCreatedAtUnixMillis = createdAtUnixMillis;
        cellularity::snapshot_codec::preserveEnvironmentRuntimeFields(
                access.world(), environment);
    }
    return controller;
}

/**
 * A stored plate's controller. The fallback environment supplies what a file that predates
 * recording its allocation lacks; a file with no creation time gets the fallback one.
 */
[[nodiscard]] inline std::shared_ptr<NativeCellularityController>
createStoredPlateController(const std::string& substratePath, bool previewMode,
        const Environment& fallbackEnvironment, std::int64_t fallbackCreatedAtUnixMillis) {
    NativeSubstrateLoadResult loaded =
            loadNativeSubstrateFromPath(substratePath, previewMode, fallbackEnvironment);
    if (loaded.world == nullptr) {
        throw std::runtime_error("The substrate could not be loaded");
    }
    if (loaded.world->mCreatedAtUnixMillis <= 0) {
        if (fallbackCreatedAtUnixMillis <= 0) {
            throw std::invalid_argument("Fallback substrate creation time must be positive");
        }
        loaded.world->mCreatedAtUnixMillis = fallbackCreatedAtUnixMillis;
    }
    return std::make_shared<NativeCellularityController>(std::move(loaded.world));
}

/** Peak bytes a new plate of this shape needs, or -1 when the device budget refuses it. */
[[nodiscard]] inline std::int64_t estimateNewPlateAllocationBytes(
        int maxCellCount, int maxFoodCount, double radius) noexcept {
    try {
        const Environment environment =
                Environment::constructDefaultEnvironment(maxCellCount, maxFoodCount, radius);
        const cellularity::memory::WorldAllocationEstimate estimate =
                cellularity::memory::estimateCurrentWorldAllocation(environment);
        return estimate.bytes <= estimate.budgetBytes
                ? static_cast<std::int64_t>(estimate.bytes)
                : -1;
    } catch (...) {
        return -1;
    }
}

} // namespace cellularity::app
