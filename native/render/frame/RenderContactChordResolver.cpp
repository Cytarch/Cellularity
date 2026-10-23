#include "render/frame/RenderContactChordResolver.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <limits>
#include <stdexcept>

#include "core/CellDataConstants.h"
#include "render/frame/RenderContactLimits.h"
#include "render/frame/RenderSourceFrame.h"
#include "render/geometry/RenderContactChordNormalizer.h"

static_assert(cellularity::render::kMaximumRenderContactChordsPerCell <= static_cast<int>(std::numeric_limits<std::uint8_t>::max()),
        "RenderContactChordResolver counts must fit in uint8_t metadata");

namespace {

template <typename T>
std::size_t checkedVectorSize(const std::vector<T>& destination,
        std::uint64_t requested, const char* message) {
    if (requested > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || requested > static_cast<std::uint64_t>(destination.max_size())) {
        throw std::length_error(message);
    }
    return static_cast<std::size_t>(requested);
}

template <typename T>
void resetVectorStorage(std::vector<T>& destination, std::size_t required,
        const T& initialValue) {
    if (destination.capacity() == required) {
        destination.assign(required, initialValue);
        return;
    }
    std::vector<T> replacement(required, initialValue);
    destination.swap(replacement);
}

} // namespace

void cellularity::render::RenderContactChordResolver::reset(
        std::uint64_t generationEntryCount, std::uint64_t offsetEntryCount,
        std::uint64_t countEntryCount,
        std::uint64_t maximumNormalizedContactChordCount) {
    const std::size_t generationEntries = checkedVectorSize(
            mGenerationByCell, generationEntryCount,
            "RenderContactChordResolver generation capacity is unsupported");
    const std::size_t offsetEntries = checkedVectorSize(
            mOffsetByCell, offsetEntryCount,
            "RenderContactChordResolver offset capacity is unsupported");
    const std::size_t countEntries = checkedVectorSize(
            mCountByCell, countEntryCount,
            "RenderContactChordResolver count capacity is unsupported");
    const std::size_t maximumNormalizedCount = checkedVectorSize(
            mNormalizedContactChords, maximumNormalizedContactChordCount,
            "RenderContactChordResolver normalized capacity is unsupported");

    resetVectorStorage(mGenerationByCell, generationEntries, std::uint64_t{0});
    resetVectorStorage(mOffsetByCell, offsetEntries, std::size_t{0});
    resetVectorStorage(mCountByCell, countEntries, std::uint8_t{0});
    std::vector<RenderContactChord>().swap(mNormalizedContactChords);
    mNormalizedContactChordCount = 0;
    mMaximumNormalizedContactChordCount = maximumNormalizedCount;
    mGeneration = 0;
}

void cellularity::render::RenderContactChordResolver::beginFrame() {
    if (mGeneration == std::numeric_limits<std::uint64_t>::max()) {
        std::fill(mGenerationByCell.begin(), mGenerationByCell.end(), 0);
        mGeneration = 1;
    } else {
        ++mGeneration;
    }
    mNormalizedContactChordCount = 0;
}

cellularity::render::RenderContactChordResolver::View
cellularity::render::RenderContactChordResolver::resolve(
        const RenderSourceFrame& source, int recordIndex, Consumer consumer,
        Profile* profile,
        RenderContactChordNormalizationProfile* normalizationProfile) {
    (void)consumer;
    (void)profile;
    assert(recordIndex >= 0);
    assert(static_cast<std::size_t>(recordIndex) < source.cells.size());
    const std::size_t cacheIndex = static_cast<std::size_t>(recordIndex);
    assert(cacheIndex < mGenerationByCell.size());
    assert(cacheIndex < mOffsetByCell.size());
    assert(cacheIndex < mCountByCell.size());
    assert(mGeneration != 0);

#ifdef CELLULARITY_RENDER_PROFILE
    using Clock = std::chrono::steady_clock;
    Clock::time_point resolutionStart;
    if (profile != nullptr) {
        resolutionStart = Clock::now();
        profile->resolveRequests++;
    }
    auto finishResolutionProfile = [&]() {
        if (profile == nullptr) {
            return;
        }
        const long long resolutionNanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - resolutionStart)
                        .count();
        profile->resolutionNanos += resolutionNanos;
        if (consumer == Consumer::CellGeometry) {
            profile->cellPassResolutionNanos += resolutionNanos;
        } else {
            profile->linkPassResolutionNanos += resolutionNanos;
        }
    };
#endif

    const RenderSourceCell& cell = source.cells[cacheIndex];
    assert(cell.hasContactChordPayload);
    const int contactChordCount = static_cast<int>(cell.contactChordCount);
    assert(contactChordCount >= 0);
    assert(contactChordCount <= kMaximumRenderContactChordsPerCell);
    assert(cell.contactChordOffset <= source.activeContactChords.size());
    assert(static_cast<std::size_t>(contactChordCount) <= source.activeContactChords.size() - cell.contactChordOffset);
    assert(cell.contactChordOffset <= source.activeContactChordNeighborRuntimeIds.size());
    assert(static_cast<std::size_t>(contactChordCount) <= source.activeContactChordNeighborRuntimeIds.size() - cell.contactChordOffset);
    const RenderContactChord* const sourceContactChords =
            contactChordCount == 0
            ? nullptr
            : source.activeContactChords.data() + cell.contactChordOffset;
    if (contactChordCount <= 1) {
#ifdef CELLULARITY_RENDER_PROFILE
        if (profile != nullptr) {
            profile->directSourceViews++;
        }
        finishResolutionProfile();
#endif
        return {sourceContactChords, contactChordCount};
    }

    if (mGenerationByCell[cacheIndex] == mGeneration) {
        const std::size_t offset = mOffsetByCell[cacheIndex];
        const int cachedCount = static_cast<int>(mCountByCell[cacheIndex]);
        assert(cachedCount == contactChordCount);
        assert(offset <= mNormalizedContactChordCount);
        assert(static_cast<std::size_t>(cachedCount) <= mNormalizedContactChordCount - offset);
#ifdef CELLULARITY_RENDER_PROFILE
        if (profile != nullptr) {
            profile->cacheHits++;
        }
        finishResolutionProfile();
#endif
        return {mNormalizedContactChords.data() + offset, cachedCount};
    }

    const std::size_t activeContactChordCount =
            static_cast<std::size_t>(contactChordCount);
    if (mNormalizedContactChordCount > mMaximumNormalizedContactChordCount || activeContactChordCount > mMaximumNormalizedContactChordCount - mNormalizedContactChordCount) {
        throw std::length_error(
                "RenderContactChordResolver normalized cache exceeds its bound");
    }
    const std::size_t offset = mNormalizedContactChordCount;
    const std::size_t requiredContactChordCount =
            offset + activeContactChordCount;
    if (mNormalizedContactChords.size() < requiredContactChordCount) {
        mNormalizedContactChords.resize(requiredContactChordCount);
    }
    RenderContactChord* const targetContactChords =
            mNormalizedContactChords.data() + offset;
    std::copy_n(sourceContactChords, contactChordCount, targetContactChords);
#ifdef CELLULARITY_RENDER_PROFILE
    Clock::time_point normalizationStart;
    if (profile != nullptr) {
        normalizationStart = Clock::now();
    }
#endif
    clipAndOrderRenderContactChords(targetContactChords, contactChordCount,
            normalizationProfile);
#ifdef CELLULARITY_RENDER_PROFILE
    if (profile != nullptr) {
        const long long normalizationNanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - normalizationStart)
                        .count();
        profile->normalizationNanos += normalizationNanos;
        if (consumer == Consumer::CellGeometry) {
            profile->cellPassNormalizationNanos += normalizationNanos;
        } else {
            profile->linkPassNormalizationNanos += normalizationNanos;
        }
    }
#endif
    mNormalizedContactChordCount = requiredContactChordCount;
    mOffsetByCell[cacheIndex] = offset;
    mCountByCell[cacheIndex] = static_cast<std::uint8_t>(contactChordCount);
    mGenerationByCell[cacheIndex] = mGeneration;

#ifdef CELLULARITY_RENDER_PROFILE
    if (profile != nullptr) {
        profile->cacheMisses++;
        profile->uniqueNormalizedCells++;
        profile->normalizedContactChordCount += contactChordCount;
        if (consumer == Consumer::LinkLookup) {
            profile->linkOnlyCacheMisses++;
        }
    }
    finishResolutionProfile();
#endif
    return {targetContactChords, contactChordCount};
}

std::size_t
cellularity::render::RenderContactChordResolver::normalizedContactChordCount()
        const noexcept {
    return mNormalizedContactChordCount;
}
