#pragma once

#include <array>
#include <cstdint>

#include "core/CellRuntimeId.h"
#include "core/CellType.h"

namespace cellularity::render {

inline constexpr int kPreviewExtraCellTypeIndex = CellTypes::length;
inline constexpr int kCellTypeBucketCount = CellTypes::length + 1;
inline constexpr int kCellDataBucketCount = CellTypes::length * 2 + 1;
inline constexpr int kMaximumDrawBunches = 201;
inline constexpr std::uint32_t kPreparedRenderFrameLayoutVersion = 1;

struct RenderViewBounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
};

struct RenderMarker {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
    float alpha = 0.0f;
    CellRuntimeId selectedCellRuntimeId = kInvalidCellRuntimeId;
};

// A capture-time request identifies a Cell slot only while SimulationWorld is under
// its owner. Capture resolves it immediately to stable identity and owned
// marker values; the slot index never reaches a published source frame.
struct RenderMarkerCaptureRequest {
    int sourceCellIndex = -1;
    float radiusOffset = 0.0f;
    float alpha = 0.0f;
};

// Describes the exact simulation point represented by the frame. The two
// update phases are deliberately distinct: a frame captured between them must
// never be mistaken for one captured after a completed simulation update.
enum class RenderCaptureStage : std::uint8_t {
    Unspecified = 0,
    StableWorld = 1,
    AfterUpdatePhaseOne = 2,
    AfterCompletedUpdate = 3,
    GenomePreview = 4,
    GenomePreviewGhost = 5,
    SubstrateFilePreview = 6,
};

enum class RenderInterpolationMode : std::uint8_t {
    Disabled = 0,
};

struct RenderFrameIdentity {
    std::uint64_t captureSequence = 0;
    std::uint64_t worldEpoch = 0;
    // Monotonic generation of the microscope presentation mode. Keeping this
    // separate from the mode ordinal prevents an A -> B -> A transition from
    // accepting a late frame produced during the first A period.
    std::uint64_t modeGeneration = 0;
    std::uint64_t completedUpdateId = 0;
    RenderCaptureStage captureStage = RenderCaptureStage::Unspecified;
};

// Watermarks describe input already resolved by the simulation owner. They
// are deliberately separate from frame identity: telemetry must never alter
// deterministic rendering or persistent simulation state.
struct RenderResolvedInputState {
    std::uint64_t lastResolvedInputSequence = 0;
    std::uint64_t appliedWorldRevision = 0;
};

// Monotonic diagnostic timestamps. Values remain zero when the local
// CELLULARITY_RENDER_TIMELINE build option is disabled.
struct RenderFrameTimeline {
    std::int64_t captureRequestedNanos = 0;
    std::int64_t sourceCapturedNanos = 0;
    std::int64_t framePreparedNanos = 0;
};

struct RenderEnvironmentState {
    std::int64_t revision = 0;
    bool previewMode = false;
    double lightAngle = 0.0;
    double lightRange = 0.0;
    double lightIntensity = 0.0;
    double dishRadius = 0.0;
    double dishInnerRadius = 0.0;
    std::array<float, 3> backgroundColor{};
};

struct RenderViewportState {
    // revision describes the viewport used for presentation; coverageRevision
    // identifies the viewport for which geometry coverage was built.
    std::uint64_t revision = 0;
    std::uint64_t coverageRevision = 0;
    RenderViewBounds visibleBounds{};
    RenderViewBounds coverageBounds{};
    std::array<float, 16> mvpMatrix{};
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float zoom = 0.0f;
    float viewWidth = 0.0f;
    float viewHeight = 0.0f;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
};

// Simulation-point identity plus the complete immutable viewport snapshot used
// to select this source frame. A later presentation snapshot may provide a
// newer camera matrix, while coverageRevision/coverageBounds remain tied to
// this capture.
struct RenderFrameCaptureMetadata {
    std::uint64_t worldEpoch = 0;
    std::uint64_t modeGeneration = 0;
    std::uint64_t completedUpdateId = 0;
    RenderCaptureStage stage = RenderCaptureStage::Unspecified;
    std::int64_t environmentRevision = 0;
    RenderViewportState viewport{};
    RenderResolvedInputState resolvedInput{};
    RenderFrameTimeline timeline{};
};

// Presentation-only data can change without rebuilding simulation geometry.
// It owns values and never points at NativeViewport, SimulationWorld, JNI, or GLES.
struct RenderPresentationState {
    RenderViewportState viewport{};
    std::array<float, 3> backgroundColor{};
};

// Revisions are intentionally separate from draw counts. A future GPU-only
// consumer can use them to skip uploads without treating diagnostics as input.
struct RenderFrameLayoutMetadata {
    std::uint32_t version = kPreparedRenderFrameLayoutVersion;
    std::uint64_t geometryRevision = 0;
    std::uint64_t instanceRevision = 0;
    std::uint64_t markerRevision = 0;
};

struct RenderFrameInterpolationMetadata {
    RenderInterpolationMode mode = RenderInterpolationMode::Disabled;
    std::uint64_t sourceCaptureSequence = 0;
    std::uint64_t targetCaptureSequence = 0;
    float alpha = 1.0f;
};

// These values are observability only. Rendering must use the active ranges
// and fixed metadata below, never these duplicated counters.
struct RenderFrameDiagnostics {
    std::uint64_t buildNanoseconds = 0;
    std::uint64_t sourceCellCount = 0;
    std::uint64_t sourceFoodCount = 0;
    std::uint64_t sourceDeadCellCount = 0;
    std::uint64_t sourceTopologyEdgeCount = 0;
    std::uint64_t visibleCellCount = 0;
    std::uint64_t normalizedContactChordCount = 0;
    std::uint64_t degenerateLinkCount = 0;
};

} // namespace cellularity::render
