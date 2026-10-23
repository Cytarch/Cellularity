#include "render/frame/RenderSourceFrame.h"

#include <limits>

#include "core/RendererMemoryModel.h"

static_assert(sizeof(cellularity::render::RenderSourceCell) <= cellularity::memory::kRenderSourceCellBudgetBytes,
        "RendererMemoryModel must cover RenderSourceCell");
static_assert(sizeof(cellularity::render::RenderSourceLink) <= cellularity::memory::kRenderSourceLinkBudgetBytes,
        "RendererMemoryModel must cover RenderSourceLink");
static_assert(sizeof(cellularity::render::RenderSourceFood) <= cellularity::memory::kRenderSourceFoodBudgetBytes,
        "RendererMemoryModel must cover RenderSourceFood");
static_assert(sizeof(cellularity::render::RenderSourceDeadSplash) <= cellularity::memory::kRenderSourceDeadSplashBudgetBytes,
        "RendererMemoryModel must cover RenderSourceDeadSplash");
static_assert(sizeof(cellularity::render::RenderMarker) <= cellularity::memory::kRenderMarkerBudgetBytes,
        "RendererMemoryModel must cover RenderMarker");

namespace cellularity::render {
namespace {

void addBytes(std::uint64_t& total, std::size_t count,
        std::size_t elementBytes) noexcept {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    if (count > maximum / elementBytes) {
        total = maximum;
        return;
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(count) * elementBytes;
    if (bytes > maximum - total) {
        total = maximum;
        return;
    }
    total += bytes;
}

template <typename T>
void addVectorSize(std::uint64_t& total,
        const std::vector<T>& values) noexcept {
    addBytes(total, values.size(), sizeof(T));
}

template <typename T>
void addVectorCapacity(std::uint64_t& total,
        const std::vector<T>& values) noexcept {
    addBytes(total, values.capacity(), sizeof(T));
}

} // namespace

std::uint64_t RenderSourceFrame::activeStorageBytes() const noexcept {
    std::uint64_t total = sizeof(RenderSourceFrame);
    for (const auto& bucket : orderedCellRecordIndices) {
        addVectorSize(total, bucket);
    }
    addVectorSize(total, cells);
    addVectorSize(total, activeContactChords);
    addVectorSize(total, activeContactChordNeighborRuntimeIds);
    // Normal and boundary records use the same payload and active range.
    addVectorSize(total, candidateLinks);
    addVectorSize(total, foods);
    addVectorSize(total, deadSplashes);
    addVectorSize(total, markers);
    return total;
}

std::uint64_t RenderSourceFrame::retainedStorageBytes() const noexcept {
    std::uint64_t total = sizeof(RenderSourceFrame);
    for (const auto& bucket : orderedCellRecordIndices) {
        addVectorCapacity(total, bucket);
    }
    addVectorCapacity(total, cells);
    addVectorCapacity(total, activeContactChords);
    addVectorCapacity(total, activeContactChordNeighborRuntimeIds);
    // Retained high-water storage includes both kinds of Link record.
    addVectorCapacity(total, candidateLinks);
    addVectorCapacity(total, foods);
    addVectorCapacity(total, deadSplashes);
    addVectorCapacity(total, markers);
    return total;
}

} // namespace cellularity::render
