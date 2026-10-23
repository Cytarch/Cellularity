#include "render/frame/PreparedRenderFrame.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace cellularity::render {
namespace {

void addBytes(std::uint64_t& total, std::uint64_t count,
        std::size_t elementBytes) noexcept {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    if (count > maximum / elementBytes) {
        total = maximum;
        return;
    }
    const std::uint64_t bytes = count * elementBytes;
    total = bytes > maximum - total ? maximum : total + bytes;
}

void addActiveFloatRange(std::uint64_t& total, int count,
        std::uint64_t floatsPerEntry) noexcept {
    if (count < 0) {
        total = std::numeric_limits<std::uint64_t>::max();
        return;
    }
    const std::uint64_t entries = static_cast<std::uint64_t>(count);
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    if (entries > maximum / floatsPerEntry) {
        total = maximum;
        return;
    }
    addBytes(total, entries * floatsPerEntry, sizeof(float));
}

template <typename T>
void addVectorCapacity(std::uint64_t& total,
        const std::vector<T>& values) noexcept {
    addBytes(total, static_cast<std::uint64_t>(values.capacity()), sizeof(T));
}

} // namespace

std::uint64_t PreparedRenderFrame::activeStorageBytes() const noexcept {
    std::uint64_t total = sizeof(PreparedRenderFrame);
    addBytes(total, static_cast<std::uint64_t>(markers.size()),
            sizeof(RenderMarker));
    addActiveFloatRange(total, cellDrawVertexCount, 2);
    addActiveFloatRange(total, cellDrawVertexCount, 4);
    addActiveFloatRange(total, cellCenterFloatCount, 1);
    addActiveFloatRange(total, cellDrawVertexCount, 4);
    if (cellDrawIndexCount < 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    addBytes(total, static_cast<std::uint64_t>(cellDrawIndexCount),
            sizeof(unsigned short));
    addActiveFloatRange(total, foodInstanceCount, 4);
    addActiveFloatRange(total, foodInstanceCount, 4);
    addActiveFloatRange(total, deadCellInstanceCount, 4);
    addActiveFloatRange(total, deadCellInstanceCount, 4);
    addActiveFloatRange(total, drawLinksCount, 4);
    addActiveFloatRange(total, drawLinksCount, 1);
    return total;
}

std::uint64_t PreparedRenderFrame::retainedStorageBytes() const noexcept {
    std::uint64_t total = sizeof(PreparedRenderFrame);
    addVectorCapacity(total, markers);
    addVectorCapacity(total, cellTriangleCoords);
    addVectorCapacity(total, cellTextureCoords);
    addVectorCapacity(total, cellCenterCoords);
    addVectorCapacity(total, cellColors);
    addVectorCapacity(total, cellIndices);
    addVectorCapacity(total, foodInstanceAttributes);
    addVectorCapacity(total, foodVertexAttributes);
    addVectorCapacity(total, deadCellInstanceAttributes);
    addVectorCapacity(total, deadCellVertexAttributes);
    addVectorCapacity(total, linkTriangleCoords);
    addVectorCapacity(total, linkTextureCoords);
    return total;
}

} // namespace cellularity::render
