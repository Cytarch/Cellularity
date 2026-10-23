#pragma once

#include <cstddef>
#include <cstdint>

#include "render/frame/PreparedRenderFrame.h"

namespace cellularity::render {

// A synchronous, non-owning view over prepared-frame storage. It is the
// backend-neutral shape used by the optional backend ABI: the producer keeps
// the owned PreparedRenderFrame alive for the complete call and no geometry
// is repacked or copied at the library boundary.
template <typename T>
class PreparedFrameSpan final {
public:
    constexpr PreparedFrameSpan() noexcept = default;

    constexpr PreparedFrameSpan(const T* data, std::size_t size) noexcept
            : mData(data), mSize(size) {}

    [[nodiscard]] constexpr const T* data() const noexcept {
        return mData;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return mSize;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return mSize == 0;
    }

    [[nodiscard]] constexpr const T* begin() const noexcept {
        return mData;
    }

    [[nodiscard]] constexpr const T* end() const noexcept {
        return mSize == 0 ? mData : mData + mSize;
    }

    [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept {
        return mData[index];
    }

private:
    const T* mData = nullptr;
    std::size_t mSize = 0;
};

template <typename T>
class PreparedFrameMatrixView final {
public:
    constexpr PreparedFrameMatrixView() noexcept = default;

    constexpr PreparedFrameMatrixView(const T* data, std::size_t rows,
            std::size_t columns, std::size_t rowStride) noexcept
            : mData(data),
              mRows(rows),
              mColumns(columns),
              mRowStride(rowStride) {}

    [[nodiscard]] constexpr const T* operator[](std::size_t row) const noexcept {
        return mData + row * mRowStride;
    }

    [[nodiscard]] constexpr const T* data() const noexcept {
        return mData;
    }

    [[nodiscard]] constexpr std::size_t rows() const noexcept {
        return mRows;
    }

    [[nodiscard]] constexpr std::size_t columns() const noexcept {
        return mColumns;
    }

    [[nodiscard]] constexpr std::size_t rowStride() const noexcept {
        return mRowStride;
    }

private:
    const T* mData = nullptr;
    std::size_t mRows = 0;
    std::size_t mColumns = 0;
    std::size_t mRowStride = 0;
};

struct PreparedRenderFrameView final {
    bool valid = false;
    bool geometryReady = false;
    std::uintptr_t sourceIdentity = 0;
    RenderFrameIdentity identity{};
    RenderEnvironmentState environment{};
    RenderViewportState viewport{};
    RenderResolvedInputState resolvedInput{};
    RenderFrameTimeline timeline{};
    RenderFrameLayoutMetadata layout{};
    RenderFrameInterpolationMetadata interpolation{};
    RenderFrameDiagnostics diagnostics{};
    PreparedFrameSpan<RenderMarker> markers{};
    PreparedFrameSpan<float> cellTriangleCoords{};
    PreparedFrameSpan<float> cellTextureCoords{};
    PreparedFrameSpan<float> cellCenterCoords{};
    PreparedFrameSpan<float> cellColors{};
    PreparedFrameSpan<unsigned short> cellIndices{};
    PreparedFrameSpan<float> foodInstanceAttributes{};
    PreparedFrameSpan<float> foodVertexAttributes{};
    PreparedFrameSpan<float> deadCellInstanceAttributes{};
    PreparedFrameSpan<float> deadCellVertexAttributes{};
    PreparedFrameSpan<float> linkTriangleCoords{};
    PreparedFrameSpan<float> linkTextureCoords{};
    PreparedFrameSpan<int> cellVertexCountBefore{};
    PreparedFrameSpan<int> cellVertexCount{};
    PreparedFrameSpan<int> cellIndexCountBefore{};
    PreparedFrameSpan<int> cellIndexCount{};
    PreparedFrameMatrixView<int> bunchStart{};
    PreparedFrameMatrixView<int> bunchSize{};
    PreparedFrameMatrixView<int> bunchIndexStart{};
    PreparedFrameMatrixView<int> bunchIndexSize{};
    PreparedFrameSpan<int> bunches{};
    int cellDrawVertexCount = 0;
    int cellDrawIndexCount = 0;
    int cellCenterFloatCount = 0;
    int foodInstanceCount = 0;
    int deadCellInstanceCount = 0;
    int drawLinksCount = 0;
};

[[nodiscard]] inline PreparedRenderFrameView makePreparedRenderFrameView(
        const PreparedRenderFrame& frame) noexcept {
    static_assert(sizeof(int) == sizeof(std::int32_t));
    static_assert(sizeof(unsigned short) == sizeof(std::uint16_t));
    static_assert(sizeof(frame.bunchStart[0])
            == sizeof(int) * kMaximumDrawBunches);

    PreparedRenderFrameView view;
    view.valid = frame.valid;
    view.geometryReady = frame.geometryReady;
    view.sourceIdentity = reinterpret_cast<std::uintptr_t>(&frame);
    view.identity = frame.identity;
    view.environment = frame.environment;
    view.viewport = frame.viewport;
    view.resolvedInput = frame.resolvedInput;
    view.timeline = frame.timeline;
    view.layout = frame.layout;
    view.interpolation = frame.interpolation;
    view.diagnostics = frame.diagnostics;
    view.markers = {frame.markers.data(), frame.markers.size()};
    view.cellTriangleCoords = {
            frame.cellTriangleCoords.data(), frame.cellTriangleCoords.size()
    };
    view.cellTextureCoords = {
            frame.cellTextureCoords.data(), frame.cellTextureCoords.size()
    };
    view.cellCenterCoords = {
            frame.cellCenterCoords.data(), frame.cellCenterCoords.size()
    };
    view.cellColors = {frame.cellColors.data(), frame.cellColors.size()};
    view.cellIndices = {frame.cellIndices.data(), frame.cellIndices.size()};
    view.foodInstanceAttributes = {frame.foodInstanceAttributes.data(),
            frame.foodInstanceAttributes.size()};
    view.foodVertexAttributes = {frame.foodVertexAttributes.data(),
            frame.foodVertexAttributes.size()};
    view.deadCellInstanceAttributes = {frame.deadCellInstanceAttributes.data(),
            frame.deadCellInstanceAttributes.size()};
    view.deadCellVertexAttributes = {frame.deadCellVertexAttributes.data(),
            frame.deadCellVertexAttributes.size()};
    view.linkTriangleCoords = {
            frame.linkTriangleCoords.data(), frame.linkTriangleCoords.size()
    };
    view.linkTextureCoords = {
            frame.linkTextureCoords.data(), frame.linkTextureCoords.size()
    };
    view.cellVertexCountBefore = {
            frame.cellVertexCountBefore.data(), frame.cellVertexCountBefore.size()
    };
    view.cellVertexCount = {
            frame.cellVertexCount.data(), frame.cellVertexCount.size()
    };
    view.cellIndexCountBefore = {
            frame.cellIndexCountBefore.data(), frame.cellIndexCountBefore.size()
    };
    view.cellIndexCount = {
            frame.cellIndexCount.data(), frame.cellIndexCount.size()
    };
    view.bunchStart = {frame.bunchStart[0].data(), frame.bunchStart.size(),
            frame.bunchStart[0].size(), frame.bunchStart[0].size()};
    view.bunchSize = {frame.bunchSize[0].data(), frame.bunchSize.size(),
            frame.bunchSize[0].size(), frame.bunchSize[0].size()};
    view.bunchIndexStart = {frame.bunchIndexStart[0].data(),
            frame.bunchIndexStart.size(), frame.bunchIndexStart[0].size(),
            frame.bunchIndexStart[0].size()};
    view.bunchIndexSize = {frame.bunchIndexSize[0].data(),
            frame.bunchIndexSize.size(), frame.bunchIndexSize[0].size(),
            frame.bunchIndexSize[0].size()};
    view.bunches = {frame.bunches.data(), frame.bunches.size()};
    view.cellDrawVertexCount = frame.cellDrawVertexCount;
    view.cellDrawIndexCount = frame.cellDrawIndexCount;
    view.cellCenterFloatCount = frame.cellCenterFloatCount;
    view.foodInstanceCount = frame.foodInstanceCount;
    view.deadCellInstanceCount = frame.deadCellInstanceCount;
    view.drawLinksCount = frame.drawLinksCount;
    return view;
}

} // namespace cellularity::render
