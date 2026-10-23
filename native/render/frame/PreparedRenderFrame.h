#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "render/frame/RenderFrameTypes.h"

namespace cellularity::render {

// Producer-owned CPU frame payload. A writable instance has one exclusive
// owner. After it is marked valid and published, consumers may receive only a
// const view; the producer must not reuse the instance until every consumer
// has released it. Publication synchronization belongs to RenderFrameExchange,
// not to this data object.
//
// The payload owns its active arrays and fixed draw metadata. It deliberately
// contains no world/view references, GL handles, JNI state, or simulation-only
// data. Copying is disabled so ownership transfers cannot accidentally clone a
// full frame; fixed-slot owners may move an unpublished instance.
struct PreparedRenderFrame final {
    PreparedRenderFrame() = default;
    PreparedRenderFrame(const PreparedRenderFrame&) = delete;
    PreparedRenderFrame& operator=(const PreparedRenderFrame&) = delete;
    PreparedRenderFrame(PreparedRenderFrame&&) noexcept = default;
    PreparedRenderFrame& operator=(PreparedRenderFrame&&) noexcept = default;

    // geometryReady is producer-local state. valid becomes true only as the
    // final operation before publication. Async frames keep capture coverage in
    // this payload while the GL consumer applies its newer presentation state
    // separately; synchronous compatibility owners may still complete both at
    // once.
    bool valid = false;
    bool geometryReady = false;
    RenderFrameIdentity identity{};
    RenderEnvironmentState environment{};
    RenderViewportState viewport{};
    RenderResolvedInputState resolvedInput{};
    RenderFrameTimeline timeline{};
    RenderFrameLayoutMetadata layout{};
    RenderFrameInterpolationMetadata interpolation{};
    RenderFrameDiagnostics diagnostics{};
    std::vector<RenderMarker> markers;
    std::vector<float> cellTriangleCoords;
    std::vector<float> cellTextureCoords;
    std::vector<float> cellCenterCoords;
    std::vector<float> cellColors;
    std::vector<unsigned short> cellIndices;
    std::vector<float> foodInstanceAttributes;
    std::vector<float> foodVertexAttributes;
    std::vector<float> deadCellInstanceAttributes;
    std::vector<float> deadCellVertexAttributes;
    std::vector<float> linkTriangleCoords;
    std::vector<float> linkTextureCoords;
    std::array<int, kCellDataBucketCount> cellVertexCountBefore{};
    std::array<int, kCellDataBucketCount> cellVertexCount{};
    std::array<int, kCellDataBucketCount> cellIndexCountBefore{};
    std::array<int, kCellDataBucketCount> cellIndexCount{};
    std::array<std::array<int, kMaximumDrawBunches>, kCellDataBucketCount>
            bunchStart{};
    std::array<std::array<int, kMaximumDrawBunches>, kCellDataBucketCount>
            bunchSize{};
    std::array<std::array<int, kMaximumDrawBunches>, kCellDataBucketCount>
            bunchIndexStart{};
    std::array<std::array<int, kMaximumDrawBunches>, kCellDataBucketCount>
            bunchIndexSize{};
    std::array<int, kCellDataBucketCount> bunches{};
    int cellDrawVertexCount = 0;
    int cellDrawIndexCount = 0;
    int cellCenterFloatCount = 0;
    int foodInstanceCount = 0;
    int deadCellInstanceCount = 0;
    int drawLinksCount = 0;

    // Includes the inline frame object and only the logical ranges consumed by
    // upload/draw. Retained storage instead reports vector capacities. Both are
    // saturating diagnostics and never participate in rendering decisions.
    [[nodiscard]] std::uint64_t activeStorageBytes() const noexcept;
    [[nodiscard]] std::uint64_t retainedStorageBytes() const noexcept;
};

} // namespace cellularity::render
