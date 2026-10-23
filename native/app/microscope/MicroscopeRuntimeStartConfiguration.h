#pragma once

namespace cellularity::microscope {

// Resolved owner state used to start a replacement runtime. Every field is
// applied before the owner thread is created, so a paused replacement cannot
// advance the world between startup and a later control command.
struct MicroscopeRuntimeStartConfiguration final {
    int temperatureMode = 0;
    int toolOrdinal = 0;
    float slowMotionSpeed = 0.05F;
    double incubateTargetUps = 1700.0;
    bool lifecyclePaused = false;
    bool performanceHudEnabled = false;
};

} // namespace cellularity::microscope
