#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "render/frame/RenderContactChordBuffer.h"
#include "render/frame/RenderContactCollector.h"
#include "render/frame/RenderSourceFrame.h"

class SimulationWorld;

namespace cellularity::tests {
struct NativeSimulationWorldRendererTestAccess;
} // namespace cellularity::tests

namespace cellularity::render {

// Reads SimulationWorld only during a bounded synchronous capture and publishes an
// owned RenderSourceFrame. Raw render contacts are collected into private
// sparse scratch here; mesh preparation remains downstream.
class RenderSourceCapture final {
public:
    struct Profile {
        long long totalNanos = 0;
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
        int linkTopologyEdgeCount = 0;
        int linkTopologyBoundaryCount = 0;
        int linkBoundaryVisits = 0;
        int linkBoundaryCandidateCount = 0;
        int linkBoundaryCulledCount = 0;
        int linkBoundaryDeadOwnerCount = 0;
        int linkGridBucketCount = 0;
        int linkGridWordCount = 0;
        int linkOccupiedBucketCount = 0;
        int linkGridCellVisits = 0;
        int linkEndpointCellCount = 0;
        int linkAdjacencyVisits = 0;
        int linkCandidateEdgeCount = 0;
        int linkCandidateWordCount = 0;
    };

    RenderSourceCapture(int maxCellCount, RenderSourceFrame& frame);

    RenderSourceCapture(const RenderSourceCapture&) = delete;
    RenderSourceCapture& operator=(const RenderSourceCapture&) = delete;
    RenderSourceCapture(RenderSourceCapture&&) = delete;
    RenderSourceCapture& operator=(RenderSourceCapture&&) = delete;

    void reset(int maxCellCount, RenderSourceFrame& frame);
    int capture(const SimulationWorld& world, const RenderViewBounds& viewBounds,
            const RenderFrameCaptureMetadata& captureMetadata,
            RenderSourceFrame& frame,
            const RenderMarkerCaptureRequest* markerRequests = nullptr,
            std::size_t markerRequestCount = 0);
    Profile
    profileCapture(const SimulationWorld& world, const RenderViewBounds& viewBounds,
            const RenderFrameCaptureMetadata& captureMetadata,
            RenderSourceFrame& frame,
            const RenderMarkerCaptureRequest* markerRequests = nullptr,
            std::size_t markerRequestCount = 0);

    [[nodiscard]] int cellCapacity() const noexcept;

private:
    friend struct cellularity::tests::NativeSimulationWorldRendererTestAccess;

    struct FoodSpatialCandidate final {
        int gridIndex = -1;
        int foodIndex = -1;
    };

    int mCellCapacity = 0;
    std::size_t mMaximumRawContactChordCount = 0;
    std::size_t mMaximumNormalEdgeCount = 0;
    std::size_t mMaximumCandidateLinkCount = 0;
    RenderContactCollector mRenderContactCollector;
    RenderContactChordBuffer mRenderContactChords;
    std::vector<int> mCellVisitMark;
    int mCellVisitGeneration = 1;
    std::vector<int> mSourceRecordIndexByCell;
    std::vector<int> mSourceRecordGenerationByCell;
    int mSourceRecordGeneration = 1;
    std::vector<std::uint64_t> mLinkCandidateBits;
    std::vector<FoodSpatialCandidate> mFoodSpatialCandidates;
    std::uint64_t mCaptureSequence = 0;

    int captureImpl(const SimulationWorld& world, const RenderViewBounds& viewBounds,
            const RenderFrameCaptureMetadata& captureMetadata,
            RenderSourceFrame& frame,
            const RenderMarkerCaptureRequest* markerRequests,
            std::size_t markerRequestCount, Profile* profile);
    void beginCapture(const SimulationWorld& world,
            const RenderFrameCaptureMetadata& captureMetadata,
            RenderSourceFrame& frame);
    int ensureSourceCell(const SimulationWorld& world, int cellIndex,
            bool includeContactChords, RenderSourceFrame& frame);
    void captureSourceCellContactChords(const SimulationWorld& world, int cellIndex,
            RenderSourceCell& record,
            RenderSourceFrame& frame);
    int captureVisibleCells(const SimulationWorld& world,
            const RenderViewBounds& viewBounds,
            RenderSourceFrame& frame);
    int captureVisibleCellsLinear(const SimulationWorld& world,
            const RenderViewBounds& viewBounds,
            RenderSourceFrame& frame);
    void captureCandidateLinks(const SimulationWorld& world,
            const RenderViewBounds& viewBounds,
            RenderSourceFrame& frame, Profile* profile);
    void captureNormalCandidateLinks(const SimulationWorld& world,
            const RenderViewBounds& captureBounds,
            RenderSourceFrame& frame, Profile* profile);
    void captureBoundaryCandidateLinks(const SimulationWorld& world,
            const RenderViewBounds& captureBounds,
            RenderSourceFrame& frame, Profile* profile);
#ifdef CELLULARITY_RENDER_PROFILE
    void captureFoods(const SimulationWorld& world, const RenderViewBounds& viewBounds,
            RenderSourceFrame& frame, Profile* profile);
#else
    void captureFoods(const SimulationWorld& world, const RenderViewBounds& viewBounds,
            RenderSourceFrame& frame);
#endif
    void captureDeadSplashes(const SimulationWorld& world,
            const RenderViewBounds& viewBounds,
            RenderSourceFrame& frame);
    void captureMarkers(const SimulationWorld& world,
            const RenderMarkerCaptureRequest* markerRequests,
            std::size_t markerRequestCount, RenderSourceFrame& frame);
    std::uint64_t nextCaptureSequence();
    int nextCellVisitGeneration();
    int nextSourceRecordGeneration();
};

} // namespace cellularity::render
