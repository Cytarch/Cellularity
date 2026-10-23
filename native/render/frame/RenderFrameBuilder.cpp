#include "render/frame/RenderFrameBuilder.h"

#include <algorithm>
#ifdef CELLULARITY_RENDER_PROFILE
#include <chrono>
#endif
#include <cmath>
#include <limits>
#include <stdexcept>

#include "core/CellDataConstants.h"
#include "core/RendererMemoryModel.h"
#include "render/frame/RenderTimelineClock.h"
#include "render/geometry/RenderContactChordNormalizer.h"

static_assert(sizeof(cellularity::render::RenderMarker) <= cellularity::memory::kRenderMarkerBudgetBytes,
        "RendererMemoryModel must cover RenderMarker");

namespace {

std::size_t checkedSize(std::uint64_t requested, std::size_t maximum,
        const char* message) {
    if (requested > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || requested > static_cast<std::uint64_t>(maximum)) {
        throw std::length_error(message);
    }
    return static_cast<std::size_t>(requested);
}

template <typename T>
std::size_t checkedVectorSize(const std::vector<T>& destination,
        std::uint64_t requested, const char* message) {
    return checkedSize(requested, destination.max_size(), message);
}

std::size_t checkedProduct(std::size_t value, std::size_t factor,
        std::size_t maximum, const char* message) {
    if (value != 0 && factor > maximum / value) {
        throw std::length_error(message);
    }
    return value * factor;
}

template <typename T>
void ensureReusableSize(std::vector<T>& destination, std::size_t required,
        std::size_t logicalMaximum, const char* message) {
    if (required > logicalMaximum || required > destination.max_size()) {
        throw std::length_error(message);
    }
    if (destination.size() < required) {
        destination.resize(required);
    }
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

#ifdef CELLULARITY_RENDER_PROFILE
namespace {

void copyContactChordProfile(
        cellularity::render::RenderFrameBuilder::Profile& destination,
        const cellularity::render::RenderContactChordResolver::Profile& source) {
    destination.contactChordResolutionNanos = source.resolutionNanos;
    destination.cellPassContactChordResolutionNanos =
            source.cellPassResolutionNanos;
    destination.linkPassContactChordResolutionNanos =
            source.linkPassResolutionNanos;
    destination.contactChordNormalizationNanos = source.normalizationNanos;
    destination.cellPassContactChordNormalizationNanos =
            source.cellPassNormalizationNanos;
    destination.linkPassContactChordNormalizationNanos =
            source.linkPassNormalizationNanos;
    destination.contactChordResolveRequests = source.resolveRequests;
    destination.contactChordDirectSourceViews = source.directSourceViews;
    destination.contactChordCacheHits = source.cacheHits;
    destination.contactChordCacheMisses = source.cacheMisses;
    destination.uniqueNormalizedCells = source.uniqueNormalizedCells;
    destination.normalizedContactChordCount = source.normalizedContactChordCount;
    destination.linkOnlyCacheMisses = source.linkOnlyCacheMisses;
}

void copyLinkProfile(
        cellularity::render::RenderFrameBuilder::Profile& destination,
        const cellularity::render::RenderLinkGeometryBuilder::Profile& source) {
    destination.linkTopologyEdgeCount = source.topologyEdgeCount;
    destination.linkTopologyBoundaryCount = source.topologyBoundaryCount;
    destination.linkCandidateEdgeCount = source.candidateEdgeCount;
    destination.linkBoundaryCandidateCount = source.candidateBoundaryCount;
    destination.linkContactCrossSectionCount = source.contactCrossSectionCount;
    destination.linkNormalFallbackCount = source.normalFallbackCount;
    destination.linkBoundaryFallbackCount = source.boundaryFallbackCount;
    destination.linkDiscardedContactCount = source.discardedContactCount;
    destination.degenerateLinkCount = source.degenerateLinkCount;
}

} // namespace

namespace {

void copyNormalizerProfile(
        cellularity::render::RenderFrameBuilder::Profile& destination,
        const cellularity::render::RenderContactChordNormalizationProfile& total,
        const cellularity::render::RenderContactChordNormalizationProfile& cellPass) {
    destination.normalizerFirstPassNanos = total.firstPassNanos;
    destination.normalizerSecondPassNanos = total.secondPassNanos;
    destination.normalizerSortNanos = total.sortNanos;
    destination.normalizerCalls = total.calls;
    destination.cellPassNormalizerCalls = cellPass.calls;
    destination.linkPassNormalizerCalls = total.calls - cellPass.calls;
    destination.normalizerSkippedCalls = total.skippedCalls;
    destination.normalizerContactChordCountSum = total.contactChordCountSum;
    destination.cellPassNormalizerContactChordCountSum =
            cellPass.contactChordCountSum;
    destination.linkPassNormalizerContactChordCountSum =
            total.contactChordCountSum - cellPass.contactChordCountSum;
    destination.normalizerFirstPassPairs = total.firstPassPairs;
    destination.normalizerFirstPassDiscardedSkips = total.firstPassDiscardedSkips;
    destination.normalizerFirstPassIntersections = total.firstPassIntersections;
    destination.normalizerFirstPassJoinMarks = total.firstPassJoinMarks;
    destination.normalizerFirstPassDiscardMarks = total.firstPassDiscardMarks;
    destination.normalizerSecondPassPairs = total.secondPassPairs;
    destination.normalizerSecondPassSelfSkips = total.secondPassSelfSkips;
    destination.normalizerSecondPassEndpointSkips = total.secondPassEndpointSkips;
    destination.normalizerSecondPassDiscardMarks = total.secondPassDiscardMarks;
    destination.normalizerSortComparisons = total.sortComparisons;
    destination.normalizerSortMoves = total.sortMoves;
}

} // namespace
#endif

cellularity::render::RenderFrameBuilder::RenderFrameBuilder(
        int maxCellCount, PreparedFrame& frame) {
    reset(maxCellCount, frame);
}

void cellularity::render::RenderFrameBuilder::reset(int maxCellCount,
        PreparedFrame& frame) {
    cellularity::memory::RendererInitialBufferLayout layout;
    if (!cellularity::memory::tryBuildRendererInitialBufferLayout(maxCellCount,
                layout)) {
        throw std::length_error("RenderFrameBuilder capacity is unsupported");
    }

    const std::size_t cellTriangleFloats = checkedVectorSize(
            frame.cellTriangleCoords, layout.cellTriangleFloatEntries,
            "RenderFrameBuilder initial triangle capacity is unsupported");
    const std::size_t cellTextureFloats = checkedVectorSize(
            frame.cellTextureCoords, layout.cellTextureFloatEntries,
            "RenderFrameBuilder initial texture capacity is unsupported");
    const std::size_t cellCenterFloats = checkedVectorSize(
            frame.cellCenterCoords, layout.cellCenterFloatEntries,
            "RenderFrameBuilder initial center capacity is unsupported");
    const std::size_t cellColorFloats = checkedVectorSize(
            frame.cellColors, layout.cellColorFloatEntries,
            "RenderFrameBuilder initial color capacity is unsupported");
    const std::size_t cellIndices = checkedVectorSize(
            frame.cellIndices, layout.cellIndexEntries,
            "RenderFrameBuilder initial index capacity is unsupported");
    const std::size_t foodInstanceFloats = checkedVectorSize(
            frame.foodInstanceAttributes, layout.foodInstanceFloatEntries,
            "RenderFrameBuilder initial food-instance capacity is unsupported");
    const std::size_t foodVertexFloats = checkedVectorSize(
            frame.foodVertexAttributes, layout.foodVertexFloatEntries,
            "RenderFrameBuilder initial food-vertex capacity is unsupported");
    const std::size_t deadInstanceFloats = checkedVectorSize(
            frame.deadCellInstanceAttributes, layout.deadInstanceFloatEntries,
            "RenderFrameBuilder initial dead-instance capacity is unsupported");
    const std::size_t deadVertexFloats = checkedVectorSize(
            frame.deadCellVertexAttributes, layout.deadVertexFloatEntries,
            "RenderFrameBuilder initial dead-vertex capacity is unsupported");
    const std::size_t linkTriangleFloats = checkedVectorSize(
            frame.linkTriangleCoords, layout.linkTriangleFloatEntries,
            "RenderFrameBuilder initial link-triangle capacity is unsupported");
    const std::size_t linkTextureFloats = checkedVectorSize(
            frame.linkTextureCoords, layout.linkTextureFloatEntries,
            "RenderFrameBuilder initial link-texture capacity is unsupported");

    const std::size_t maximumCellVertexCount = checkedSize(
            layout.maximumCellVertexCount, std::numeric_limits<std::size_t>::max(),
            "RenderFrameBuilder maximum vertex count is unsupported");
    const std::size_t maximumCellIndexCount = checkedVectorSize(
            frame.cellIndices, layout.maximumCellIndexCount,
            "RenderFrameBuilder maximum index count is unsupported");
    const std::size_t maximumCellCenterFloatCount = checkedVectorSize(
            frame.cellCenterCoords, layout.maximumCellCenterFloatEntries,
            "RenderFrameBuilder maximum center count is unsupported");
    const std::size_t maximumLinkVertexCount = checkedSize(
            layout.maximumLinkVertexCount, std::numeric_limits<std::size_t>::max(),
            "RenderFrameBuilder maximum link count is unsupported");
    const std::size_t maximumDrawableLinkCount = checkedSize(
            layout.maximumDrawableLinkCount, std::numeric_limits<std::size_t>::max(),
            "RenderFrameBuilder maximum candidate Link count is unsupported");

    mContactChordResolver.reset(layout.renderContactChordGenerationEntries,
            layout.renderContactChordOffsetEntries,
            layout.renderContactChordCountEntries,
            layout.maximumNormalizedContactChordEntries);
    mCellBoundaryMeshBuilder.reset(maximumCellVertexCount, maximumCellIndexCount);
    mLinkGeometryBuilder.reset(maximumLinkVertexCount);
    resetVectorStorage(frame.cellTriangleCoords, cellTriangleFloats, 0.0f);
    resetVectorStorage(frame.cellTextureCoords, cellTextureFloats, 0.0f);
    resetVectorStorage(frame.cellCenterCoords, cellCenterFloats, 0.0f);
    resetVectorStorage(frame.cellColors, cellColorFloats, 0.0f);
    resetVectorStorage(frame.cellIndices, cellIndices,
            static_cast<unsigned short>(0));
    resetVectorStorage(frame.foodInstanceAttributes, foodInstanceFloats, 0.0f);
    resetVectorStorage(frame.foodVertexAttributes, foodVertexFloats, 0.0f);
    resetVectorStorage(frame.deadCellInstanceAttributes, deadInstanceFloats,
            0.0f);
    resetVectorStorage(frame.deadCellVertexAttributes, deadVertexFloats, 0.0f);
    resetVectorStorage(frame.linkTriangleCoords, linkTriangleFloats, 0.0f);
    resetVectorStorage(frame.linkTextureCoords, linkTextureFloats, 0.0f);
    std::vector<RenderMarker>().swap(frame.markers);
    frame.valid = false;
    frame.geometryReady = false;
    frame.identity = {};
    frame.environment = {};
    frame.viewport = {};
    frame.layout = {};
    frame.interpolation = {};
    frame.diagnostics = {};
    mCellCapacity = maxCellCount;
    mMaximumCellVertexCount = maximumCellVertexCount;
    mMaximumCellCenterFloatCount = maximumCellCenterFloatCount;
    mMaximumDrawableLinkCount = maximumDrawableLinkCount;
    clearDrawCounts(frame);
    // A freshly reset slot represents a complete empty geometry frame,
    // preserving the existing ability to consume it before the first capture.
    const std::uint64_t emptySequence = nextCaptureSequence();
    frame.identity.captureSequence = emptySequence;
    frame.layout.geometryRevision = emptySequence;
    frame.layout.instanceRevision = emptySequence;
    frame.interpolation.sourceCaptureSequence = emptySequence;
    frame.interpolation.targetCaptureSequence = emptySequence;
    frame.geometryReady = true;
}

void cellularity::render::RenderFrameBuilder::clearDrawCounts(
        PreparedFrame& frame) {
    frame.valid = false;
    frame.geometryReady = false;
    frame.cellVertexCountBefore.fill(0);
    frame.cellVertexCount.fill(0);
    frame.cellIndexCountBefore.fill(0);
    frame.cellIndexCount.fill(0);
    frame.bunches.fill(0);
    frame.cellDrawVertexCount = 0;
    frame.cellDrawIndexCount = 0;
    frame.cellCenterFloatCount = 0;
    frame.foodInstanceCount = 0;
    frame.deadCellInstanceCount = 0;
    frame.drawLinksCount = 0;
}

void cellularity::render::RenderFrameBuilder::beginPreparedFrame(
        const SourceFrame& source, PreparedFrame& frame) {
    frame.valid = false;
    frame.geometryReady = false;
    frame.markers.assign(source.markers.begin(), source.markers.end());

    const std::uint64_t captureSequence = nextCaptureSequence();
    frame.identity.captureSequence = captureSequence;
    frame.identity.worldEpoch = source.capture.worldEpoch;
    frame.identity.modeGeneration = source.capture.modeGeneration;
    frame.identity.completedUpdateId = source.capture.completedUpdateId;
    frame.identity.captureStage = source.capture.stage;

    frame.environment = source.environment;

    frame.viewport = source.capture.viewport;
    frame.resolvedInput = source.capture.resolvedInput;
    frame.timeline = source.capture.timeline;

    frame.layout = {};
    frame.layout.geometryRevision = captureSequence;
    frame.layout.instanceRevision = captureSequence;
    frame.layout.markerRevision = captureSequence;

    frame.interpolation = {};
    frame.interpolation.sourceCaptureSequence = captureSequence;
    frame.interpolation.targetCaptureSequence = captureSequence;

    frame.diagnostics = {};
    frame.diagnostics.sourceCellCount = source.sourceCellCount;
    frame.diagnostics.sourceFoodCount = source.sourceFoodCount;
    frame.diagnostics.sourceDeadCellCount = source.sourceDeadSplashCount;
    frame.diagnostics.sourceTopologyEdgeCount = source.sourceTopologyEdgeCount;
}

void cellularity::render::RenderFrameBuilder::finishPreparedFrameGeometry(
        const SourceFrame& source, PreparedFrame& frame,
        std::uint64_t buildNanoseconds) {
    frame.diagnostics.buildNanoseconds = buildNanoseconds;
    frame.diagnostics.visibleCellCount =
            static_cast<std::uint64_t>(source.visibleCellCount);
    frame.diagnostics.normalizedContactChordCount = static_cast<std::uint64_t>(
            mContactChordResolver.normalizedContactChordCount());
    frame.timeline.framePreparedNanos = renderTimelineNowNanos();
    frame.geometryReady = true;
}

void cellularity::render::RenderFrameBuilder::validateSource(
        const SourceFrame& source) const {
    if (!source.valid || source.layoutVersion != kRenderSourceFrameLayoutVersion) {
        throw std::invalid_argument(
                "RenderFrameBuilder requires a valid compatible source frame");
    }
    if (source.sourceCellCount > static_cast<std::uint64_t>(mCellCapacity) || source.cells.size() > static_cast<std::size_t>(mCellCapacity)) {
        throw std::length_error(
                "RenderFrameBuilder source Cell range exceeds configured capacity");
    }
    if (source.candidateLinks.size() > mMaximumDrawableLinkCount) {
        throw std::length_error(
                "RenderFrameBuilder source Link range exceeds configured capacity");
    }
    constexpr std::uint64_t maximumAuxiliaryInstanceCount =
            std::numeric_limits<int>::max() / (4U * FLOAT_BYTES);
    if (source.sourceFoodCapacity > maximumAuxiliaryInstanceCount
            || source.sourceDeadSplashCapacity > maximumAuxiliaryInstanceCount
            || source.foods.size() > source.sourceFoodCapacity
            || source.deadSplashes.size() > source.sourceDeadSplashCapacity) {
        throw std::length_error(
                "RenderFrameBuilder source auxiliary range exceeds its capacity or upload bound");
    }
    if (source.markers.size() > static_cast<std::size_t>(mCellCapacity)) {
        throw std::length_error(
                "RenderFrameBuilder source marker range exceeds configured capacity");
    }
    for (const RenderMarker& marker : source.markers) {
        if (!isEntityCellRuntimeId(marker.selectedCellRuntimeId)) {
            throw std::invalid_argument(
                    "RenderFrameBuilder source marker has no stable Cell identity");
        }
    }
    for (const RenderSourceFood& food : source.foods) {
        if (!isValidFoodRuntimeId(food.runtimeId)) {
            throw std::invalid_argument(
                    "RenderFrameBuilder source Food has no stable identity");
        }
    }
    for (const RenderSourceDeadSplash& deadSplash : source.deadSplashes) {
        if (!isValidDeadSplashRuntimeId(deadSplash.runtimeId)) {
            throw std::invalid_argument(
                    "RenderFrameBuilder source dead splash has no stable identity");
        }
    }
    for (const auto& bucket : source.orderedCellRecordIndices) {
        for (const int recordIndex : bucket) {
            if (recordIndex < 0 || static_cast<std::size_t>(recordIndex) >= source.cells.size()) {
                throw std::out_of_range(
                        "RenderFrameBuilder source bucket contains an invalid record");
            }
        }
    }
}

int cellularity::render::RenderFrameBuilder::build(const SourceFrame& source,
        PreparedFrame& frame) {
    validateSource(source);
    beginPreparedFrame(source, frame);
    clearDrawCounts(frame);
    mContactChordResolver.beginFrame();
    mCellBoundaryMeshBuilder.build(source, frame, mContactChordResolver, nullptr,
            nullptr);
    mLinkGeometryBuilder.build(source, frame, mContactChordResolver, nullptr, nullptr, nullptr);
    copyToDrawCellOutlines(source, frame);
    copyToDrawFoods(source, frame);
    copyToDrawDeadCells(source, frame);
    finishPreparedFrameGeometry(source, frame, 0);
    return source.visibleCellCount;
}

cellularity::render::RenderFrameBuilder::Profile
cellularity::render::RenderFrameBuilder::profileBuild(const SourceFrame& source,
        PreparedFrame& frame) {
#ifndef CELLULARITY_RENDER_PROFILE
    build(source, frame);
    Profile profile;
    profile.sourceActiveStorageBytes = source.activeStorageBytes();
    profile.sourceRetainedStorageBytes = source.retainedStorageBytes();
    profile.sourceCellRecordCount = static_cast<int>(source.cells.size());
    profile.sourceActiveContactChordCount =
            static_cast<int>(source.activeContactChords.size());
    profile.sourceCandidateLinkCount =
            static_cast<int>(source.candidateLinks.size());
    profile.sourceFoodRecordCount = static_cast<int>(source.foods.size());
    profile.sourceDeadSplashRecordCount =
            static_cast<int>(source.deadSplashes.size());
    profile.sourceMarkerRecordCount = static_cast<int>(source.markers.size());
    profile.visibleCellCount = source.visibleCellCount;
    profile.cellDrawVertexCount = frame.cellDrawVertexCount;
    profile.cellDrawIndexCount = frame.cellDrawIndexCount;
    profile.cellCenterFloatCount = frame.cellCenterFloatCount;
    profile.foodInstanceCount = frame.foodInstanceCount;
    profile.deadCellInstanceCount = frame.deadCellInstanceCount;
    profile.drawLinksCount = frame.drawLinksCount;
    return profile;
#else
    validateSource(source);
    beginPreparedFrame(source, frame);
    using Clock = std::chrono::steady_clock;
    const auto totalStart = Clock::now();
    auto stageStart = totalStart;
    Profile profile;
    RenderContactChordResolver::Profile contactChordProfile;
    RenderLinkGeometryBuilder::Profile linkProfile;
    RenderContactChordNormalizationProfile* normalizationProfile = nullptr;

    clearDrawCounts(frame);
    const auto afterClear = Clock::now();
    profile.clearNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            afterClear - stageStart)
                                 .count();

    mContactChordResolver.beginFrame();
#ifdef CELLULARITY_RENDER_PROFILE
    RenderContactChordNormalizationProfile contactChordNormalizationProfile;
    RenderContactChordNormalizationProfile
            cellPassContactChordNormalizationProfile;
    normalizationProfile = &contactChordNormalizationProfile;
#endif
    stageStart = afterClear;
    mCellBoundaryMeshBuilder.build(source, frame, mContactChordResolver,
            &contactChordProfile, normalizationProfile);
    const auto afterCells = Clock::now();
    profile.cellsNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            afterCells - stageStart)
                                 .count();
#ifdef CELLULARITY_RENDER_PROFILE
    cellPassContactChordNormalizationProfile = contactChordNormalizationProfile;
#endif

    stageStart = Clock::now();
    mLinkGeometryBuilder.build(source, frame, mContactChordResolver,
            &contactChordProfile, normalizationProfile, &linkProfile);
    const auto afterLinks = Clock::now();
    profile.linksNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            afterLinks - stageStart)
                                 .count();
#ifdef CELLULARITY_RENDER_PROFILE
    copyNormalizerProfile(profile, contactChordNormalizationProfile,
            cellPassContactChordNormalizationProfile);
#endif
    copyContactChordProfile(profile, contactChordProfile);
    copyLinkProfile(profile, linkProfile);

    stageStart = Clock::now();
    copyToDrawCellOutlines(source, frame);
    const auto afterOutlines = Clock::now();
    profile.outlinesNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            afterOutlines - stageStart)
                                    .count();

    stageStart = afterOutlines;
    copyToDrawFoods(source, frame);
    const auto afterFoods = Clock::now();
    profile.foodsNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            afterFoods - stageStart)
                                 .count();

    stageStart = afterFoods;
    copyToDrawDeadCells(source, frame);
    const auto afterDeadCells = Clock::now();
    profile.deadCellsNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            afterDeadCells - stageStart)
                                     .count();
    profile.totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            afterDeadCells - totalStart)
                                 .count();
    profile.sourceActiveStorageBytes = source.activeStorageBytes();
    profile.sourceRetainedStorageBytes = source.retainedStorageBytes();
    profile.sourceCellRecordCount = static_cast<int>(source.cells.size());
    profile.sourceActiveContactChordCount =
            static_cast<int>(source.activeContactChords.size());
    profile.sourceCandidateLinkCount =
            static_cast<int>(source.candidateLinks.size());
    profile.sourceFoodRecordCount = static_cast<int>(source.foods.size());
    profile.sourceDeadSplashRecordCount =
            static_cast<int>(source.deadSplashes.size());
    profile.sourceMarkerRecordCount = static_cast<int>(source.markers.size());
    profile.visibleCellCount = source.visibleCellCount;
    profile.cellDrawVertexCount = frame.cellDrawVertexCount;
    profile.cellDrawIndexCount = frame.cellDrawIndexCount;
    profile.cellCenterFloatCount = frame.cellCenterFloatCount;
    profile.foodInstanceCount = frame.foodInstanceCount;
    profile.deadCellInstanceCount = frame.deadCellInstanceCount;
    profile.drawLinksCount = frame.drawLinksCount;
    finishPreparedFrameGeometry(
            source, frame,
            static_cast<std::uint64_t>(std::max<long long>(0, profile.totalNanos)));
    return profile;
#endif
}

int cellularity::render::RenderFrameBuilder::cellCapacity() const noexcept {
    return mCellCapacity;
}

std::uint64_t cellularity::render::RenderFrameBuilder::nextCaptureSequence() {
    if (mCaptureSequence == std::numeric_limits<std::uint64_t>::max()) {
        mCaptureSequence = 1;
    } else {
        ++mCaptureSequence;
    }
    return mCaptureSequence;
}

void cellularity::render::RenderFrameBuilder::copyToDrawCellOutlines(
        const SourceFrame& source, PreparedFrame& frame) {
    int shift = CellTypes::length + (source.environment.previewMode ? 1 : 0);
    int size =
            frame.cellVertexCountBefore[shift - 1] + frame.cellVertexCount[shift - 1];
    int baseVertexCount = size;
    int centerFloatCount = baseVertexCount * 2;
    const float inverseMaxCellRadius =
            static_cast<float>(1.0 / CellDataConstants::MAX_CELL_RADIUS);
    auto outlineWritableVertexLimit = [&]() {
        return std::min(
                {mMaximumCellVertexCount, frame.cellTriangleCoords.size() / 2U,
                        frame.cellTextureCoords.size() / 4U,
                        frame.cellCenterCoords.size() / 4U, frame.cellColors.size() / 4U});
    };
    std::size_t writableVertexCount = outlineWritableVertexLimit();
    auto ensureOutlineVertexCapacity = [&](std::size_t requiredVertices) {
        if (requiredVertices > mMaximumCellVertexCount) {
            throw std::length_error(
                    "RenderFrameBuilder outline vertex output exceeds its bound");
        }
        const std::size_t requiredTriangleFloats = requiredVertices * 2U;
        const std::size_t requiredAttributeFloats = requiredVertices * 4U;
        if (frame.cellTriangleCoords.size() < requiredTriangleFloats) {
            frame.cellTriangleCoords.resize(requiredTriangleFloats);
        }
        if (frame.cellTextureCoords.size() < requiredAttributeFloats) {
            frame.cellTextureCoords.resize(requiredAttributeFloats);
        }
        if (frame.cellCenterCoords.size() < requiredAttributeFloats) {
            frame.cellCenterCoords.resize(requiredAttributeFloats);
        }
        if (frame.cellColors.size() < requiredAttributeFloats) {
            frame.cellColors.resize(requiredAttributeFloats);
        }
        writableVertexCount = outlineWritableVertexLimit();
    };
    auto ensureOutlineAppendCapacity = [&](std::size_t additionalVertices) {
        const std::size_t currentVertexCount = static_cast<std::size_t>(size);
        if (currentVertexCount > writableVertexCount || additionalVertices > writableVertexCount - currentVertexCount) {
            if (currentVertexCount > mMaximumCellVertexCount || additionalVertices > mMaximumCellVertexCount - currentVertexCount) {
                throw std::length_error(
                        "RenderFrameBuilder outline vertex output exceeds its bound");
            }
            ensureOutlineVertexCapacity(currentVertexCount + additionalVertices);
        }
    };

    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length;
            cellTypeIndex++) {
        CellType cellType = CellTypes::fromOrdinal(cellTypeIndex);
        CellTypeInfo cellTypeInfo = CellTypes::info(cellType);
        int dataIndex = shift + cellTypeIndex;
        frame.cellVertexCountBefore[dataIndex] = size;

        if (cellTypeInfo.outlineShader.empty()) {
            frame.cellVertexCount[dataIndex] = 0;
            continue;
        }

        if (cellType == CellType::FLAGELLOCYTE) {
            float x = 1.0f / (cellTypeInfo.outlineRadius * inverseMaxCellRadius);
            float y = static_cast<float>(std::sqrt(1.0f - x * x));

            const auto& sourceBucket =
                    source.orderedCellRecordIndices[static_cast<std::size_t>(
                            cellTypeIndex)];
            for (const int recordIndex : sourceBucket) {
                const RenderSourceCell& cell =
                        source.cells[static_cast<std::size_t>(recordIndex)];

                const float red = cell.red;
                const float green = cell.green;
                const float blue = cell.blue;

                const float cellAngle = cell.angle;
                const float cellRadius = cell.radius;
                const float cellPositionX = cell.positionX;
                const float cellPositionY = cell.positionY;
                const float swimAnimationTime = cell.outlineAnimation;

                float sin = static_cast<float>(std::sin(cellAngle) * cellRadius);
                float cos = static_cast<float>(std::cos(cellAngle) * cellRadius);
                ensureOutlineAppendCapacity(3U);

                auto writeFlagellocyteVertex = [&](int vertex, float localX,
                                                       float localY, float textureX,
                                                       float textureY) {
                    int target = size + vertex;
                    frame.cellTriangleCoords[2 * target] = localX;
                    frame.cellTriangleCoords[2 * target + 1] = localY;
                    frame.cellTextureCoords[4 * target] = textureX;
                    frame.cellTextureCoords[4 * target + 1] = textureY;
                    frame.cellTextureCoords[4 * target + 2] = swimAnimationTime;
                    frame.cellTextureCoords[4 * target + 3] = 0.0f;
                    frame.cellColors[4 * target] = red;
                    frame.cellColors[4 * target + 1] = green;
                    frame.cellColors[4 * target + 2] = blue;
                    frame.cellColors[4 * target + 3] = cellRadius;
                    frame.cellCenterCoords[4 * target] = cellPositionX;
                    frame.cellCenterCoords[4 * target + 1] = cellPositionY;
                    frame.cellCenterCoords[4 * target + 2] = 0.0f;
                    frame.cellCenterCoords[4 * target + 3] = 0.0f;
                    centerFloatCount = std::max(centerFloatCount, (target + 1) * 4);
                };

                writeFlagellocyteVertex(0, -sin * y - cos * x, cos * y - sin * x, x, y);
                writeFlagellocyteVertex(1, sin * y - cos * x, -cos * y - sin * x, x,
                        -y);
                writeFlagellocyteVertex(
                        2, -cellTypeInfo.outlineRadius * cos * inverseMaxCellRadius,
                        -cellTypeInfo.outlineRadius * sin * inverseMaxCellRadius,
                        cellTypeInfo.outlineRadius * inverseMaxCellRadius, 0.0f);
                size += 3;
            }
        } else {
            const auto& sourceBucket =
                    source.orderedCellRecordIndices[static_cast<std::size_t>(
                            cellTypeIndex)];
            for (const int recordIndex : sourceBucket) {
                const RenderSourceCell& cell =
                        source.cells[static_cast<std::size_t>(recordIndex)];

                const float red = cell.red;
                const float green = cell.green;
                const float blue = cell.blue;

                const float cellAngle = cell.angle;
                const float cellRadius = cell.radius;
                const float cellPositionX = cell.positionX;
                const float cellPositionY = cell.positionY;

                const float aux1 = cell.outlineAux1;
                const float aux2 = cell.outlineAux2;

                float cos = static_cast<float>(
                        std::cos(cellAngle) * (cellRadius + cellTypeInfo.outlineRadius));
                float sin = static_cast<float>(
                        std::sin(cellAngle) * (cellRadius + cellTypeInfo.outlineRadius));

                const float aux3 = cell.outlineAux3;

                ensureOutlineAppendCapacity(6U);

                auto writeOutlineVertex = [&](int vertex, float localX, float localY,
                                                  float textureX, float textureY) {
                    int target = size + vertex;
                    frame.cellTriangleCoords[2 * target] = localX;
                    frame.cellTriangleCoords[2 * target + 1] = localY;
                    frame.cellTextureCoords[4 * target] = textureX;
                    frame.cellTextureCoords[4 * target + 1] = textureY;
                    frame.cellTextureCoords[4 * target + 2] = aux1;
                    frame.cellTextureCoords[4 * target + 3] = aux2;
                    frame.cellColors[4 * target] = red;
                    frame.cellColors[4 * target + 1] = green;
                    frame.cellColors[4 * target + 2] = blue;
                    frame.cellColors[4 * target + 3] = cellRadius;
                    frame.cellCenterCoords[4 * target] = cellPositionX;
                    frame.cellCenterCoords[4 * target + 1] = cellPositionY;
                    frame.cellCenterCoords[4 * target + 2] = aux3;
                    frame.cellCenterCoords[4 * target + 3] = cellAngle;
                    centerFloatCount = std::max(centerFloatCount, (target + 1) * 4);
                };

                writeOutlineVertex(0, -cos - sin, -sin + cos, -1.0f, 1.0f);
                writeOutlineVertex(1, cos + sin, sin - cos, 1.0f, -1.0f);
                writeOutlineVertex(2, cos - sin, sin + cos, 1.0f, 1.0f);
                writeOutlineVertex(3, -cos - sin, -sin + cos, -1.0f, 1.0f);
                writeOutlineVertex(4, -cos + sin, -sin - cos, -1.0f, -1.0f);
                writeOutlineVertex(5, cos + sin, sin - cos, 1.0f, -1.0f);
                size += 6;
            }
        }

        frame.cellVertexCount[dataIndex] =
                size - frame.cellVertexCountBefore[dataIndex];
    }

    frame.cellDrawVertexCount = size;
    frame.cellCenterFloatCount = centerFloatCount;
}

void cellularity::render::RenderFrameBuilder::copyToDrawFoods(
        const SourceFrame& source, PreparedFrame& frame) {
    const std::size_t maximumFoodFloatCount = std::min(
            frame.foodInstanceAttributes.max_size(),
            static_cast<std::size_t>(std::numeric_limits<int>::max() / FLOAT_BYTES));
    const std::size_t requiredFoodFloats = checkedProduct(
            checkedSize(source.sourceFoodCapacity, maximumFoodFloatCount / 4U,
                    "RenderFrameBuilder Food capacity exceeds its bound"),
            4U, maximumFoodFloatCount,
            "RenderFrameBuilder Food output exceeds its bound");
    ensureReusableSize(frame.foodInstanceAttributes, requiredFoodFloats,
            maximumFoodFloatCount,
            "RenderFrameBuilder Food output exceeds its bound");
    ensureReusableSize(
            frame.foodVertexAttributes, requiredFoodFloats,
            std::min(frame.foodVertexAttributes.max_size(), maximumFoodFloatCount),
            "RenderFrameBuilder Food output exceeds its bound");

    for (std::size_t instanceIndex = 0; instanceIndex < source.foods.size();
            ++instanceIndex) {
        const std::size_t offset = instanceIndex * 4U;
        std::copy_n(source.foods[instanceIndex].instance.data(), 4U,
                frame.foodInstanceAttributes.data() + offset);
        std::copy_n(source.foods[instanceIndex].vertex.data(), 4U,
                frame.foodVertexAttributes.data() + offset);
    }
    frame.foodInstanceCount = static_cast<int>(source.foods.size());
}

void cellularity::render::RenderFrameBuilder::copyToDrawDeadCells(
        const SourceFrame& source, PreparedFrame& frame) {
    const std::size_t maximumDeadFloatCount = std::min(
            frame.deadCellInstanceAttributes.max_size(),
            static_cast<std::size_t>(std::numeric_limits<int>::max() / FLOAT_BYTES));
    const std::size_t requiredDeadFloats = checkedProduct(
            checkedSize(source.sourceDeadSplashCapacity, maximumDeadFloatCount / 4U,
                    "RenderFrameBuilder dead-splash capacity exceeds its bound"),
            4U, maximumDeadFloatCount,
            "RenderFrameBuilder dead-splash output exceeds its bound");
    ensureReusableSize(frame.deadCellInstanceAttributes, requiredDeadFloats,
            maximumDeadFloatCount,
            "RenderFrameBuilder dead-splash output exceeds its bound");
    ensureReusableSize(frame.deadCellVertexAttributes, requiredDeadFloats,
            std::min(frame.deadCellVertexAttributes.max_size(),
                    maximumDeadFloatCount),
            "RenderFrameBuilder dead-splash output exceeds its bound");
    for (std::size_t instanceIndex = 0;
            instanceIndex < source.deadSplashes.size(); ++instanceIndex) {
        const std::size_t offset = instanceIndex * 4U;
        std::copy_n(source.deadSplashes[instanceIndex].instance.data(), 4U,
                frame.deadCellInstanceAttributes.data() + offset);
        std::copy_n(source.deadSplashes[instanceIndex].vertex.data(), 4U,
                frame.deadCellVertexAttributes.data() + offset);
    }
    frame.deadCellInstanceCount = static_cast<int>(source.deadSplashes.size());
}
