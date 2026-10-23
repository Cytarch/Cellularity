#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/CellRuntimeId.h"
#include "core/CellType.h"
#include "core/DeadSplashRuntimeId.h"
#include "core/FoodRuntimeId.h"
#include "core/RenderContactChord.h"
#include "core/TopologyEdgeRuntimeId.h"
#include "render/frame/RenderFrameTypes.h"

namespace cellularity::render {

inline constexpr std::uint32_t kRenderSourceFrameLayoutVersion = 5;

// Compact values needed to build one Cell in either its ordinary or Preview
// arrow bucket. runtimeId is the cross-frame key inside one world epoch;
// sourceCellIndex is only a frame-local compatibility reference.
struct RenderSourceCell final {
    CellRuntimeId runtimeId = kInvalidCellRuntimeId;
    int sourceCellIndex = -1;
    CellType cellType = CellType::PHAGOCYTE;
    std::uint32_t contactChordOffset = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float radius = 0.0f;
    float angle = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float baseAux = 0.0f;
    float baseAux4 = 0.0f;
    float previewAux = 0.0f;
    float previewAux4 = 0.0f;
    float outlineAux1 = 0.0f;
    float outlineAux2 = 0.0f;
    float outlineAux3 = 0.0f;
    float outlineAnimation = 0.0f;
    std::uint8_t contactChordCount = 0;
    bool hasContactChordPayload = false;
    bool mirrored = false;
    bool previewArrow = false;
};

// Normal and boundary candidates share one compact payload. Cell A owns the
// contact-chord lookup; Cell B is the other endpoint or a saved boundary anchor.
// sourceLinkIndex and lookup indices belong only to this immutable frame.
struct RenderSourceLink final {
    TopologyEdgeRuntimeId runtimeId = kInvalidTopologyEdgeRuntimeId;
    CellRuntimeId cellARuntimeId = kInvalidCellRuntimeId;
    CellRuntimeId cellBRuntimeId = kInvalidCellRuntimeId;
    int sourceLinkIndex = -1;
    int sourceCellBIndex = -1;
    int sourceCellARecordIndex = -1;
    float cellAPositionX = 0.0f;
    float cellAPositionY = 0.0f;
    float cellBPositionX = 0.0f;
    float cellBPositionY = 0.0f;
    // Captured once from the endpoint radii; deliberately not clamped to [0, 1].
    float fallbackCenterRatio = 0.5f;
    float renderStartOffsetX = 0.0f;
    float renderStartOffsetY = 0.0f;
    float renderEndOffsetX = 0.0f;
    float renderEndOffsetY = 0.0f;

    [[nodiscard]] constexpr bool isBoundary() const noexcept {
        return cellBRuntimeId == kBoundaryCellRuntimeId;
    }
};

struct RenderSourceFood final {
    FoodRuntimeId runtimeId = kInvalidFoodRuntimeId;
    std::array<float, 4> instance{};
    std::array<float, 4> vertex{};
};

struct RenderSourceDeadSplash final {
    DeadSplashRuntimeId runtimeId = kInvalidDeadSplashRuntimeId;
    std::array<float, 4> instance{};
    std::array<float, 4> vertex{};
};

// One capture slot. All vectors own their data and contain
// only active ordered records. valid is committed last; consumers receive a
// const view and never retain aliases into SimulationWorld.
struct RenderSourceFrame final {
    RenderSourceFrame() = default;
    RenderSourceFrame(const RenderSourceFrame&) = delete;
    RenderSourceFrame& operator=(const RenderSourceFrame&) = delete;
    RenderSourceFrame(RenderSourceFrame&&) noexcept = default;
    RenderSourceFrame& operator=(RenderSourceFrame&&) noexcept = default;

    bool valid = false;
    std::uint32_t layoutVersion = kRenderSourceFrameLayoutVersion;
    std::uint64_t captureSequence = 0;
    RenderFrameCaptureMetadata capture{};
    RenderEnvironmentState environment{};
    std::uint64_t sourceCellCount = 0;
    std::uint64_t sourceFoodCount = 0;
    std::uint64_t sourceDeadSplashCount = 0;
    std::uint64_t sourceTopologyEdgeCount = 0;
    std::uint64_t sourceBoundaryLinkCount = 0;
    std::uint64_t sourceFoodCapacity = 0;
    std::uint64_t sourceDeadSplashCapacity = 0;
    int visibleCellCount = 0;
    std::array<std::vector<int>, kCellTypeBucketCount> orderedCellRecordIndices;
    std::vector<RenderSourceCell> cells;
    // The two contact-chord vectors have identical active ranges.
    // RenderContactChord::neighborCellIndex remains a frame-local compatibility
    // lookup key; this parallel runtime identity is the authoritative cross-frame
    // neighbor key.
    std::vector<RenderContactChord> activeContactChords;
    std::vector<CellRuntimeId> activeContactChordNeighborRuntimeIds;
    std::vector<RenderSourceLink> candidateLinks;
    std::vector<RenderSourceFood> foods;
    std::vector<RenderSourceDeadSplash> deadSplashes;
    std::vector<RenderMarker> markers;

    [[nodiscard]] std::uint64_t activeStorageBytes() const noexcept;
    [[nodiscard]] std::uint64_t retainedStorageBytes() const noexcept;
};

} // namespace cellularity::render
