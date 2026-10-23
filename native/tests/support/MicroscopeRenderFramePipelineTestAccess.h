#pragma once

#include <chrono>
#include <cstdint>

#include "app/microscope/render/MicroscopeRenderFramePipeline.h"

namespace cellularity::tests {

struct MicroscopeRenderFramePipelineTestAccess final {
    static bool pausePreparation(
            microscope::MicroscopeRenderFramePipeline& pipeline, bool paused,
            std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        return pipeline.setPreparationPausedForTesting(paused, timeout);
    }

    static bool waitUntilSourcePrepared(
            microscope::MicroscopeRenderFramePipeline& pipeline,
            std::uint64_t sourceSequence,
            std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        return pipeline.waitUntilSourcePreparedForTesting(sourceSequence, timeout);
    }

    static void
    setPauseAfterBuild(microscope::MicroscopeRenderFramePipeline& pipeline,
            bool paused) {
        pipeline.setPauseAfterBuildForTesting(paused);
    }

    static bool waitUntilPausedAfterBuild(
            const microscope::MicroscopeRenderFramePipeline& pipeline,
            std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        return pipeline.waitUntilPausedAfterBuildForTesting(timeout);
    }

    static void
    failNextPreparation(microscope::MicroscopeRenderFramePipeline& pipeline,
            microscope::RenderPreparationFailureKind kind) {
        pipeline.failNextPreparationForTesting(kind);
    }
};

} // namespace cellularity::tests
