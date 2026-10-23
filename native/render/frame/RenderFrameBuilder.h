#pragma once

#include <cstddef>
#include <cstdint>

#include "render/frame/CellBoundaryMeshBuilder.h"
#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderContactChordResolver.h"
#include "render/frame/RenderLinkGeometryBuilder.h"
#include "render/frame/RenderSourceFrame.h"

class NativeSimulationWorldRenderer;

namespace cellularity::tests {
struct NativeSimulationWorldRendererTestAccess;
} // namespace cellularity::tests

namespace cellularity::render {

// CPU-only geometry preparation. The builder consumes one complete owned
// source frame and never reads SimulationWorld, chooses visibility, or changes the
// source ordering selected by capture.
class RenderFrameBuilder final {
public:
    static constexpr int PREVIEW_EXTRA_CELL_TYPE_INDEX =
            kPreviewExtraCellTypeIndex;
    static constexpr int CELL_TYPE_BUCKET_COUNT = kCellTypeBucketCount;
    static constexpr int CELL_DATA_BUCKET_COUNT = kCellDataBucketCount;
    static constexpr int MAX_BUNCHES = kMaximumDrawBunches;

    using PreparedFrame = PreparedRenderFrame;
    using SourceFrame = RenderSourceFrame;

    struct Profile {
        // Capture fields are populated by NativeSimulationWorldRenderer, which owns
        // the synchronous world-to-source step.
        long long sourceCaptureNanos = 0;
        long long contactChordCollectionNanos = 0;
        long long cellCaptureNanos = 0;
        long long linkCaptureNanos = 0;
        long long foodCaptureNanos = 0;
        long long deadSplashCaptureNanos = 0;
        long long markerCaptureNanos = 0;
        long long freshPhysicsContactCaptures = 0;
        long long staleGeometryContactFallbacks = 0;
        long long materializedContactChords = 0;
        long long foodGridCaptureCalls = 0;
        long long foodLinearCaptureCalls = 0;
        long long foodLinearMissingIndexFallbacks = 0;
        long long foodLinearCoverageFallbacks = 0;
        long long foodCandidateVisits = 0;
        long long foodIndexGenerationHits = 0;
        long long foodIndexGenerationMisses = 0;
        long long foodStaleIndexFallbacks = 0;
        long long foodStaleIndexMappingVisits = 0;
        long long cullingNanos = 0;
        long long clearNanos = 0;
        long long cellsNanos = 0;
        long long linksNanos = 0;
        long long contactChordResolutionNanos = 0;
        long long cellPassContactChordResolutionNanos = 0;
        long long linkPassContactChordResolutionNanos = 0;
        long long contactChordNormalizationNanos = 0;
        long long cellPassContactChordNormalizationNanos = 0;
        long long linkPassContactChordNormalizationNanos = 0;
        long long contactChordResolveRequests = 0;
        long long contactChordDirectSourceViews = 0;
        long long contactChordCacheHits = 0;
        long long contactChordCacheMisses = 0;
        long long uniqueNormalizedCells = 0;
        long long normalizedContactChordCount = 0;
        long long linkOnlyCacheMisses = 0;
        long long normalizerFirstPassNanos = 0;
        long long normalizerSecondPassNanos = 0;
        long long normalizerSortNanos = 0;
        long long normalizerCalls = 0;
        long long cellPassNormalizerCalls = 0;
        long long linkPassNormalizerCalls = 0;
        long long normalizerSkippedCalls = 0;
        long long normalizerContactChordCountSum = 0;
        long long cellPassNormalizerContactChordCountSum = 0;
        long long linkPassNormalizerContactChordCountSum = 0;
        long long normalizerFirstPassPairs = 0;
        long long normalizerFirstPassDiscardedSkips = 0;
        long long normalizerFirstPassIntersections = 0;
        long long normalizerFirstPassJoinMarks = 0;
        long long normalizerFirstPassDiscardMarks = 0;
        long long normalizerSecondPassPairs = 0;
        long long normalizerSecondPassSelfSkips = 0;
        long long normalizerSecondPassEndpointSkips = 0;
        long long normalizerSecondPassDiscardMarks = 0;
        long long normalizerSortComparisons = 0;
        long long normalizerSortMoves = 0;
        int linkTopologyEdgeCount = 0;
        int linkTopologyBoundaryCount = 0;
        int linkBoundaryVisits = 0;
        int linkBoundaryCandidateCount = 0;
        int linkBoundaryCulledCount = 0;
        int linkBoundaryDeadOwnerCount = 0;
        int linkContactCrossSectionCount = 0;
        int linkNormalFallbackCount = 0;
        int linkBoundaryFallbackCount = 0;
        int linkDiscardedContactCount = 0;
        int degenerateLinkCount = 0;
        int linkGridBucketCount = 0;
        int linkGridWordCount = 0;
        int linkOccupiedBucketCount = 0;
        int linkGridCellVisits = 0;
        int linkEndpointCellCount = 0;
        int linkAdjacencyVisits = 0;
        int linkCandidateEdgeCount = 0;
        int linkCandidateWordCount = 0;
        long long outlinesNanos = 0;
        long long foodsNanos = 0;
        long long deadCellsNanos = 0;
        long long totalNanos = 0;
        std::uint64_t sourceActiveStorageBytes = 0;
        std::uint64_t sourceRetainedStorageBytes = 0;
        int sourceCellRecordCount = 0;
        int sourceActiveContactChordCount = 0;
        int sourceCandidateLinkCount = 0;
        int sourceFoodRecordCount = 0;
        int sourceDeadSplashRecordCount = 0;
        int sourceMarkerRecordCount = 0;
        int visibleCellCount = 0;
        int cellDrawVertexCount = 0;
        int cellDrawIndexCount = 0;
        int cellCenterFloatCount = 0;
        int foodInstanceCount = 0;
        int deadCellInstanceCount = 0;
        int drawLinksCount = 0;
    };

    RenderFrameBuilder(int maxCellCount, PreparedFrame& frame);

    RenderFrameBuilder(const RenderFrameBuilder&) = delete;
    RenderFrameBuilder& operator=(const RenderFrameBuilder&) = delete;
    RenderFrameBuilder(RenderFrameBuilder&&) = delete;
    RenderFrameBuilder& operator=(RenderFrameBuilder&&) = delete;

    void reset(int maxCellCount, PreparedFrame& frame);
    int build(const SourceFrame& source, PreparedFrame& frame);
    Profile profileBuild(const SourceFrame& source, PreparedFrame& frame);

    [[nodiscard]] int cellCapacity() const noexcept;

private:
    friend class ::NativeSimulationWorldRenderer;
    friend struct cellularity::tests::NativeSimulationWorldRendererTestAccess;

    static constexpr int FLOAT_BYTES = 4;

    RenderContactChordResolver mContactChordResolver;
    CellBoundaryMeshBuilder mCellBoundaryMeshBuilder;
    RenderLinkGeometryBuilder mLinkGeometryBuilder;
    int mCellCapacity = 0;
    std::size_t mMaximumCellVertexCount = 0;
    std::size_t mMaximumCellCenterFloatCount = 0;
    std::size_t mMaximumDrawableLinkCount = 0;
    std::uint64_t mCaptureSequence = 0;

    void clearDrawCounts(PreparedFrame& frame);
    void validateSource(const SourceFrame& source) const;
    void beginPreparedFrame(const SourceFrame& source, PreparedFrame& frame);
    void finishPreparedFrameGeometry(const SourceFrame& source,
            PreparedFrame& frame,
            std::uint64_t buildNanoseconds);
    void copyToDrawCellOutlines(const SourceFrame& source, PreparedFrame& frame);
    void copyToDrawFoods(const SourceFrame& source, PreparedFrame& frame);
    void copyToDrawDeadCells(const SourceFrame& source, PreparedFrame& frame);
    std::uint64_t nextCaptureSequence();
};

} // namespace cellularity::render
