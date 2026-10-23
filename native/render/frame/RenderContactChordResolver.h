#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/RenderContactChord.h"

namespace cellularity::tests {
struct NativeSimulationWorldRendererTestAccess;
} // namespace cellularity::tests

namespace cellularity::render {

struct RenderContactChordNormalizationProfile;
struct RenderSourceFrame;

// Resolves one source Cell's raw contact chords into the normalized view shared
// by Cell and Link geometry. This component owns only frame-local cache state;
// it does not build mesh vertices or read simulation state.
class RenderContactChordResolver final {
public:
    enum class Consumer : std::uint8_t {
        CellGeometry,
        LinkLookup,
    };

    struct View final {
        const RenderContactChord* data = nullptr;
        int count = 0;
    };

    struct Profile final {
        long long resolutionNanos = 0;
        long long cellPassResolutionNanos = 0;
        long long linkPassResolutionNanos = 0;
        long long normalizationNanos = 0;
        long long cellPassNormalizationNanos = 0;
        long long linkPassNormalizationNanos = 0;
        long long resolveRequests = 0;
        long long directSourceViews = 0;
        long long cacheHits = 0;
        long long cacheMisses = 0;
        long long uniqueNormalizedCells = 0;
        long long normalizedContactChordCount = 0;
        long long linkOnlyCacheMisses = 0;
    };

    RenderContactChordResolver() = default;

    RenderContactChordResolver(const RenderContactChordResolver&) = delete;
    RenderContactChordResolver&
    operator=(const RenderContactChordResolver&) = delete;
    RenderContactChordResolver(RenderContactChordResolver&&) = delete;
    RenderContactChordResolver& operator=(RenderContactChordResolver&&) = delete;

    void reset(std::uint64_t generationEntryCount, std::uint64_t offsetEntryCount,
            std::uint64_t countEntryCount,
            std::uint64_t maximumNormalizedContactChordCount);
    void beginFrame();
    View resolve(
            const RenderSourceFrame& source, int recordIndex, Consumer consumer,
            Profile* profile,
            RenderContactChordNormalizationProfile* normalizationProfile = nullptr);

    [[nodiscard]] std::size_t normalizedContactChordCount() const noexcept;

private:
    friend struct cellularity::tests::NativeSimulationWorldRendererTestAccess;

    std::vector<std::uint64_t> mGenerationByCell;
    std::vector<std::size_t> mOffsetByCell;
    std::vector<std::uint8_t> mCountByCell;
    std::vector<RenderContactChord> mNormalizedContactChords;
    std::size_t mNormalizedContactChordCount = 0;
    std::size_t mMaximumNormalizedContactChordCount = 0;
    std::uint64_t mGeneration = 0;
};

} // namespace cellularity::render
