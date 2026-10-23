#pragma once

#include "app/interaction/NativeViewport.h"
#include "core/SimulationWorld.h"

// A short-lived mutable view used only while simulation-owner access is held.
// Rendering state is deliberately not part of the world/interaction context.
struct NativeWorldContext {
    SimulationWorld& world;
    NativeViewport& viewport;
};
