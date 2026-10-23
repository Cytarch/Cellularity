#include <array>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

#include "render/backend/RenderFrameResult.h"
#include "render/frame/PreparedRenderFrame.h"

namespace {

using cellularity::render::PreparedRenderFrame;
using cellularity::render::RenderCaptureStage;
using cellularity::render::RenderInterpolationMode;

bool hasValidFrameResultWireContract() {
    using namespace cellularity::render;
    constexpr std::array outcomes{
            RenderFrameOutcome::Presented,
            RenderFrameOutcome::Deferred,
            RenderFrameOutcome::SwapchainRecreated,
            RenderFrameOutcome::SurfaceLost,
            RenderFrameOutcome::SkippedNotReady,
            RenderFrameOutcome::Failed,
    };
    for (const RenderFrameOutcome outcome : outcomes) {
        const std::uint32_t flags = outcome == RenderFrameOutcome::Presented
                ? kPrimaryContentPresented
                : 0;
        const RenderFrameResult input{outcome, flags, -1234567};
        const std::uint64_t wire = packRenderFrameResult(input);
        RenderFrameResult decoded;
        if (wire == kInvalidRenderFrameWire
                || !unpackRenderFrameResult(wire, decoded)
                || decoded.outcome != input.outcome
                || decoded.flags != input.flags
                || decoded.semanticValue != input.semanticValue) {
            return false;
        }
    }

    const RenderFrameResult fallback{RenderFrameOutcome::Presented,
            kFallbackClearPresented, 17};
    RenderFrameResult fallbackDecoded;
    if (!unpackRenderFrameResult(packRenderFrameResult(fallback),
                fallbackDecoded)
            || fallbackDecoded.flags != kFallbackClearPresented
            || fallbackDecoded.semanticValue != 17) {
        return false;
    }

    constexpr std::uint64_t golden =
            (std::uint64_t{0xFFFFFFF9U} << 32U) | 0x100U;
    const RenderFrameResult signedResult{RenderFrameOutcome::Presented,
            kPrimaryContentPresented, -7};
    return packRenderFrameResult(signedResult) == golden
            && packRenderFrameResultSigned(signedResult) == -30064770816LL;
}

bool rejectsInvalidFrameResultWireContract() {
    using namespace cellularity::render;
    constexpr std::array invalidResults{
            RenderFrameResult{RenderFrameOutcome::Presented, 0, 0},
            RenderFrameResult{RenderFrameOutcome::Presented,
                    kPrimaryContentPresented | kFallbackClearPresented, 0},
            RenderFrameResult{RenderFrameOutcome::Deferred,
                    kPrimaryContentPresented, 0},
            RenderFrameResult{static_cast<RenderFrameOutcome>(6), 0, 0},
            RenderFrameResult{RenderFrameOutcome::Failed, 4U, 0},
    };
    for (const RenderFrameResult& result : invalidResults) {
        if (packRenderFrameResult(result) != kInvalidRenderFrameWire) {
            return false;
        }
    }

    constexpr std::array invalidWires{
            kInvalidRenderFrameWire,
            std::uint64_t{6},
            std::uint64_t{0},
            std::uint64_t{0x300},
            std::uint64_t{0x101},
            std::uint64_t{0x10000} | 0x100U,
    };
    RenderFrameResult decoded;
    for (const std::uint64_t wire : invalidWires) {
        if (unpackRenderFrameResult(wire, decoded)) {
            return false;
        }
    }
    return true;
}

static_assert(!std::is_copy_constructible_v<PreparedRenderFrame>);
static_assert(!std::is_copy_assignable_v<PreparedRenderFrame>);
static_assert(std::is_nothrow_move_constructible_v<PreparedRenderFrame>);
static_assert(std::is_nothrow_move_assignable_v<PreparedRenderFrame>);

bool hasDefaultState(const PreparedRenderFrame& frame) {
    return !frame.valid && !frame.geometryReady && frame.identity.captureSequence == 0 && frame.identity.worldEpoch == 0 && frame.identity.modeGeneration == 0 && frame.identity.completedUpdateId == 0 && frame.identity.captureStage == RenderCaptureStage::Unspecified && frame.environment.revision == 0 && !frame.environment.previewMode && frame.viewport.revision == 0 && frame.viewport.coverageRevision == 0 && frame.viewport.surfaceWidth == 0 && frame.viewport.surfaceHeight == 0 && frame.resolvedInput.lastResolvedInputSequence == 0 && frame.resolvedInput.appliedWorldRevision == 0 && frame.timeline.captureRequestedNanos == 0 && frame.timeline.sourceCapturedNanos == 0 && frame.timeline.framePreparedNanos == 0 && frame.layout.version == cellularity::render::kPreparedRenderFrameLayoutVersion && frame.layout.geometryRevision == 0 && frame.layout.instanceRevision == 0 && frame.layout.markerRevision == 0 && frame.interpolation.mode == RenderInterpolationMode::Disabled && frame.interpolation.alpha == 1.0f && frame.diagnostics.buildNanoseconds == 0 && frame.markers.empty() && frame.cellTriangleCoords.empty() && frame.cellIndices.empty() && frame.cellDrawVertexCount == 0 && frame.cellDrawIndexCount == 0 && frame.foodInstanceCount == 0 && frame.deadCellInstanceCount == 0 && frame.drawLinksCount == 0;
}

} // namespace

int main() {
    if (!hasValidFrameResultWireContract()
            || !rejectsInvalidFrameResultWireContract()) {
        return 1;
    }
    PreparedRenderFrame writable;
    if (!hasDefaultState(writable)) {
        return 1;
    }

    writable.cellTriangleCoords = {1.0f, 2.0f};
    writable.cellIndices = {0};
    writable.cellDrawVertexCount = 1;
    writable.cellDrawIndexCount = 1;
    writable.geometryReady = true;
    writable.identity.captureSequence = 17;
    writable.identity.worldEpoch = 3;
    writable.identity.modeGeneration = 7;
    writable.identity.completedUpdateId = 41;
    writable.identity.captureStage = RenderCaptureStage::AfterCompletedUpdate;
    writable.environment.revision = 9;
    writable.environment.lightIntensity = 0.75;
    writable.environment.backgroundColor = {0.1f, 0.2f, 0.3f};
    writable.viewport.revision = 12;
    writable.viewport.coverageRevision = 10;
    writable.viewport.coverageBounds = {-2.0f, 2.0f, -1.0f, 1.0f};
    writable.viewport.mvpMatrix[0] = 2.0f;
    writable.resolvedInput.lastResolvedInputSequence = 23;
    writable.resolvedInput.appliedWorldRevision = 5;
    writable.timeline.captureRequestedNanos = 100;
    writable.timeline.sourceCapturedNanos = 200;
    writable.timeline.framePreparedNanos = 300;
    writable.layout.geometryRevision = 17;
    writable.layout.instanceRevision = 17;
    writable.layout.markerRevision = 4;
    writable.interpolation.sourceCaptureSequence = 17;
    writable.interpolation.targetCaptureSequence = 17;
    writable.diagnostics.visibleCellCount = 1;
    writable.markers.push_back({0.25f, -0.5f, 0.1f, 0.8f});
    writable.valid = true;

    const PreparedRenderFrame& published = writable;
    if (!published.valid || published.cellTriangleCoords.size() != 2 || published.cellIndices.size() != 1 || published.cellDrawVertexCount != 1 || published.cellDrawIndexCount != 1 || published.identity.captureSequence != 17 || published.identity.worldEpoch != 3 || published.identity.modeGeneration != 7 || published.identity.completedUpdateId != 41 || published.identity.captureStage != RenderCaptureStage::AfterCompletedUpdate || published.viewport.revision != 12 || published.viewport.coverageBounds.maxX != 2.0f || published.resolvedInput.lastResolvedInputSequence != 23 || published.resolvedInput.appliedWorldRevision != 5 || published.timeline.captureRequestedNanos != 100 || published.timeline.sourceCapturedNanos != 200 || published.timeline.framePreparedNanos != 300 || published.environment.backgroundColor[2] != 0.3f || published.layout.geometryRevision != 17 || published.interpolation.alpha != 1.0f || published.diagnostics.visibleCellCount != 1 || published.markers.size() != 1) {
        return 1;
    }

    PreparedRenderFrame returned = std::move(writable);
    if (!returned.valid || returned.cellTriangleCoords[0] != 1.0f || returned.cellTriangleCoords[1] != 2.0f || returned.markers[0].alpha != 0.8f || returned.identity.captureSequence != 17 || returned.resolvedInput.lastResolvedInputSequence != 23 || returned.timeline.framePreparedNanos != 300) {
        return 1;
    }

    std::cout << "render frame wire and ownership contract tests passed\n";
    return 0;
}
