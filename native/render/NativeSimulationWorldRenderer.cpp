#include "render/NativeSimulationWorldRenderer.h"

#include <algorithm>
#include <stdexcept>

#include "core/SimulationWorld.h"
#include "render/frame/RenderTimelineClock.h"

NativeSimulationWorldRenderer::NativeSimulationWorldRenderer(int maxCellCount)
        : mSourceCapture(maxCellCount, mSourceFrame),
          mFrameBuilder(maxCellCount, mPreparedFrame) {}

void NativeSimulationWorldRenderer::reset(int maxCellCount) {
    mSourceCapture.reset(maxCellCount, mSourceFrame);
    mFrameBuilder.reset(maxCellCount, mPreparedFrame);
}

int NativeSimulationWorldRenderer::copyToDraw(const SimulationWorld& world,
        const ViewBounds& viewBounds) {
    CaptureMetadata captureMetadata;
    captureMetadata.stage =
            world.mEnvironment.mIsPreviewMode
            ? cellularity::render::RenderCaptureStage::GenomePreview
            : cellularity::render::RenderCaptureStage::StableWorld;
    captureMetadata.viewport.visibleBounds = viewBounds;
    captureMetadata.viewport.coverageBounds = viewBounds;
    return copyToDraw(world, viewBounds, captureMetadata);
}

int NativeSimulationWorldRenderer::copyToDraw(
        const SimulationWorld& world, const ViewBounds& viewBounds,
        const CaptureMetadata& captureMetadata) {
    return copyToDraw(world, viewBounds, captureMetadata, nullptr, 0);
}

int NativeSimulationWorldRenderer::copyToDraw(
        const SimulationWorld& world, const ViewBounds& viewBounds,
        const CaptureMetadata& captureMetadata,
        const MarkerCaptureRequest* markerRequests,
        std::size_t markerRequestCount) {
    CaptureMetadata stampedMetadata = captureMetadata;
    if (stampedMetadata.timeline.captureRequestedNanos == 0) {
        stampedMetadata.timeline.captureRequestedNanos =
                cellularity::render::renderTimelineNowNanos();
    }
    mSourceCapture.capture(world, viewBounds, stampedMetadata, mSourceFrame,
            markerRequests, markerRequestCount);
    return mFrameBuilder.build(mSourceFrame, mPreparedFrame);
}

NativeSimulationWorldRenderer::CopyToDrawProfile
NativeSimulationWorldRenderer::profileCopyToDraw(const SimulationWorld& world,
        const ViewBounds& viewBounds) {
    CaptureMetadata captureMetadata;
    captureMetadata.stage =
            world.mEnvironment.mIsPreviewMode
            ? cellularity::render::RenderCaptureStage::GenomePreview
            : cellularity::render::RenderCaptureStage::StableWorld;
    captureMetadata.viewport.visibleBounds = viewBounds;
    captureMetadata.viewport.coverageBounds = viewBounds;
    const cellularity::render::RenderSourceCapture::Profile captureProfile =
            mSourceCapture.profileCapture(world, viewBounds, captureMetadata,
                    mSourceFrame);
    CopyToDrawProfile profile =
            mFrameBuilder.profileBuild(mSourceFrame, mPreparedFrame);
    profile.sourceCaptureNanos = captureProfile.totalNanos;
    profile.contactChordCollectionNanos =
            captureProfile.contactChordCollectionNanos;
    profile.cellCaptureNanos = captureProfile.cellCaptureNanos;
    profile.linkCaptureNanos = captureProfile.linkCaptureNanos;
    profile.foodCaptureNanos = captureProfile.foodCaptureNanos;
    profile.deadSplashCaptureNanos = captureProfile.deadSplashCaptureNanos;
    profile.markerCaptureNanos = captureProfile.markerCaptureNanos;
    profile.freshPhysicsContactCaptures =
            captureProfile.freshPhysicsContactCaptures;
    profile.staleGeometryContactFallbacks =
            captureProfile.staleGeometryContactFallbacks;
    profile.materializedContactChords = captureProfile.materializedContactChords;
    profile.foodGridCaptureCalls = captureProfile.foodGridCaptureCalls;
    profile.foodLinearCaptureCalls = captureProfile.foodLinearCaptureCalls;
    profile.foodLinearMissingIndexFallbacks =
            captureProfile.foodLinearMissingIndexFallbacks;
    profile.foodLinearCoverageFallbacks =
            captureProfile.foodLinearCoverageFallbacks;
    profile.foodCandidateVisits = captureProfile.foodCandidateVisits;
    profile.foodIndexGenerationHits = captureProfile.foodIndexGenerationHits;
    profile.foodIndexGenerationMisses = captureProfile.foodIndexGenerationMisses;
    profile.foodStaleIndexFallbacks = captureProfile.foodStaleIndexFallbacks;
    profile.foodStaleIndexMappingVisits =
            captureProfile.foodStaleIndexMappingVisits;
    profile.cullingNanos += captureProfile.cellCaptureNanos;
    profile.linksNanos += captureProfile.linkCaptureNanos;
    profile.foodsNanos += captureProfile.foodCaptureNanos;
    profile.deadCellsNanos += captureProfile.deadSplashCaptureNanos;
    profile.totalNanos += captureProfile.totalNanos;
    profile.linkTopologyEdgeCount = captureProfile.linkTopologyEdgeCount;
    profile.linkTopologyBoundaryCount = captureProfile.linkTopologyBoundaryCount;
    profile.linkBoundaryVisits = captureProfile.linkBoundaryVisits;
    profile.linkBoundaryCandidateCount = captureProfile.linkBoundaryCandidateCount;
    profile.linkBoundaryCulledCount = captureProfile.linkBoundaryCulledCount;
    profile.linkBoundaryDeadOwnerCount = captureProfile.linkBoundaryDeadOwnerCount;
    profile.linkGridBucketCount = captureProfile.linkGridBucketCount;
    profile.linkGridWordCount = captureProfile.linkGridWordCount;
    profile.linkOccupiedBucketCount = captureProfile.linkOccupiedBucketCount;
    profile.linkGridCellVisits = captureProfile.linkGridCellVisits;
    profile.linkEndpointCellCount = captureProfile.linkEndpointCellCount;
    profile.linkAdjacencyVisits = captureProfile.linkAdjacencyVisits;
    profile.linkCandidateEdgeCount = captureProfile.linkCandidateEdgeCount;
    profile.linkCandidateWordCount = captureProfile.linkCandidateWordCount;
    mPreparedFrame.diagnostics.buildNanoseconds =
            static_cast<std::uint64_t>(std::max<long long>(0, profile.totalNanos));
    return profile;
}

const NativeSimulationWorldRenderer::PreparedFrame&
NativeSimulationWorldRenderer::preparedFrame() const {
    return mPreparedFrame;
}

const NativeSimulationWorldRenderer::PreparedFrame&
NativeSimulationWorldRenderer::publishPreparedFrame(
        const PresentationState& presentation) {
    if (!mPreparedFrame.geometryReady) {
        throw std::logic_error("NativeSimulationWorldRenderer cannot publish a frame "
                               "before geometry is ready");
    }
    if (presentation.viewport.surfaceWidth < 0 || presentation.viewport.surfaceHeight < 0) {
        throw std::invalid_argument("NativeSimulationWorldRenderer received a negative "
                                    "presentation surface size");
    }

    mPreparedFrame.valid = false;
    const std::uint64_t coverageRevision =
            mPreparedFrame.viewport.coverageRevision;
    const ViewBounds coverageBounds = mPreparedFrame.viewport.coverageBounds;
    mPreparedFrame.viewport = presentation.viewport;
    mPreparedFrame.viewport.coverageRevision = coverageRevision;
    mPreparedFrame.viewport.coverageBounds = coverageBounds;
    mPreparedFrame.environment.backgroundColor = presentation.backgroundColor;
    mPreparedFrame.valid = true;
    return mPreparedFrame;
}

int NativeSimulationWorldRenderer::visibleCellCount() const {
    return mSourceFrame.visibleCellCount;
}

int NativeSimulationWorldRenderer::visibleCellCountForBucket(
        int cellTypeIndex) const {
    if (cellTypeIndex < 0 || cellTypeIndex >= CELL_TYPE_BUCKET_COUNT) {
        throw std::out_of_range(
                "NativeSimulationWorldRenderer Cell type bucket out of range");
    }
    return static_cast<int>(
            mSourceFrame
                    .orderedCellRecordIndices[static_cast<std::size_t>(cellTypeIndex)]
                    .size());
}

int NativeSimulationWorldRenderer::visibleCellIndex(int cellTypeIndex,
        int localIndex) const {
    if (cellTypeIndex < 0 || cellTypeIndex >= CELL_TYPE_BUCKET_COUNT) {
        throw std::out_of_range(
                "NativeSimulationWorldRenderer Cell type bucket out of range");
    }
    const auto& bucket =
            mSourceFrame
                    .orderedCellRecordIndices[static_cast<std::size_t>(cellTypeIndex)];
    if (localIndex < 0 || static_cast<std::size_t>(localIndex) >= bucket.size()) {
        throw std::out_of_range(
                "NativeSimulationWorldRenderer visible Cell index out of range");
    }
    return mSourceFrame
            .cells[static_cast<std::size_t>(
                    bucket[static_cast<std::size_t>(localIndex)])]
            .sourceCellIndex;
}

int NativeSimulationWorldRenderer::cellDrawVertexCount() const {
    return mPreparedFrame.cellDrawVertexCount;
}

int NativeSimulationWorldRenderer::cellDrawIndexCount() const {
    return mPreparedFrame.cellDrawIndexCount;
}

int NativeSimulationWorldRenderer::cellCenterFloatCount() const {
    return mPreparedFrame.cellCenterFloatCount;
}

int NativeSimulationWorldRenderer::cellVertexCountBefore(int dataIndex) const {
    if (dataIndex < 0 || dataIndex >= CELL_DATA_BUCKET_COUNT) {
        throw std::out_of_range(
                "NativeSimulationWorldRenderer cell data bucket out of range");
    }
    return mPreparedFrame.cellVertexCountBefore[dataIndex];
}

int NativeSimulationWorldRenderer::cellVertexCount(int dataIndex) const {
    if (dataIndex < 0 || dataIndex >= CELL_DATA_BUCKET_COUNT) {
        throw std::out_of_range(
                "NativeSimulationWorldRenderer cell data bucket out of range");
    }
    return mPreparedFrame.cellVertexCount[dataIndex];
}

int NativeSimulationWorldRenderer::cellIndexCountBefore(int dataIndex) const {
    if (dataIndex < 0 || dataIndex >= CELL_DATA_BUCKET_COUNT) {
        throw std::out_of_range(
                "NativeSimulationWorldRenderer cell data bucket out of range");
    }
    return mPreparedFrame.cellIndexCountBefore[dataIndex];
}

int NativeSimulationWorldRenderer::cellIndexCount(int dataIndex) const {
    if (dataIndex < 0 || dataIndex >= CELL_DATA_BUCKET_COUNT) {
        throw std::out_of_range(
                "NativeSimulationWorldRenderer cell data bucket out of range");
    }
    return mPreparedFrame.cellIndexCount[dataIndex];
}

int NativeSimulationWorldRenderer::foodInstanceCount() const {
    return mPreparedFrame.foodInstanceCount;
}

int NativeSimulationWorldRenderer::deadCellInstanceCount() const {
    return mPreparedFrame.deadCellInstanceCount;
}

int NativeSimulationWorldRenderer::drawLinksCount() const {
    return mPreparedFrame.drawLinksCount;
}

const std::vector<float>& NativeSimulationWorldRenderer::cellTriangleCoords() const {
    return mPreparedFrame.cellTriangleCoords;
}

const std::vector<float>& NativeSimulationWorldRenderer::cellTextureCoords() const {
    return mPreparedFrame.cellTextureCoords;
}

const std::vector<float>& NativeSimulationWorldRenderer::cellCenterCoords() const {
    return mPreparedFrame.cellCenterCoords;
}

const std::vector<float>& NativeSimulationWorldRenderer::cellColors() const {
    return mPreparedFrame.cellColors;
}

const std::vector<unsigned short>&
NativeSimulationWorldRenderer::cellIndices() const {
    return mPreparedFrame.cellIndices;
}

const std::vector<float>&
NativeSimulationWorldRenderer::foodInstanceAttributes() const {
    return mPreparedFrame.foodInstanceAttributes;
}

const std::vector<float>&
NativeSimulationWorldRenderer::foodVertexAttributes() const {
    return mPreparedFrame.foodVertexAttributes;
}

const std::vector<float>&
NativeSimulationWorldRenderer::deadCellInstanceAttributes() const {
    return mPreparedFrame.deadCellInstanceAttributes;
}

const std::vector<float>&
NativeSimulationWorldRenderer::deadCellVertexAttributes() const {
    return mPreparedFrame.deadCellVertexAttributes;
}

const std::vector<float>& NativeSimulationWorldRenderer::linkTriangleCoords() const {
    return mPreparedFrame.linkTriangleCoords;
}

const std::vector<float>& NativeSimulationWorldRenderer::linkTextureCoords() const {
    return mPreparedFrame.linkTextureCoords;
}

void NativeSimulationWorldRenderer::captureDrawSnapshot(PreparedFrame& snapshot,
        bool ghostTint) {
    snapshot.valid = false;
    snapshot.geometryReady = false;
    if (!mPreparedFrame.geometryReady) {
        return;
    }

    snapshot.identity = mPreparedFrame.identity;
    snapshot.identity.captureSequence = mFrameBuilder.nextCaptureSequence();
    if (ghostTint) {
        snapshot.identity.captureStage =
                cellularity::render::RenderCaptureStage::GenomePreviewGhost;
    }
    snapshot.environment = mPreparedFrame.environment;
    snapshot.viewport = mPreparedFrame.viewport;
    snapshot.layout = mPreparedFrame.layout;
    snapshot.layout.geometryRevision = snapshot.identity.captureSequence;
    snapshot.layout.instanceRevision = snapshot.identity.captureSequence;
    snapshot.interpolation = {};
    snapshot.interpolation.sourceCaptureSequence =
            snapshot.identity.captureSequence;
    snapshot.interpolation.targetCaptureSequence =
            snapshot.identity.captureSequence;
    snapshot.diagnostics = mPreparedFrame.diagnostics;
    snapshot.markers.assign(mPreparedFrame.markers.begin(),
            mPreparedFrame.markers.end());
    snapshot.cellTriangleCoords.assign(mPreparedFrame.cellTriangleCoords.begin(),
            mPreparedFrame.cellTriangleCoords.begin() + mPreparedFrame.cellDrawVertexCount * 2);
    snapshot.cellTextureCoords.assign(mPreparedFrame.cellTextureCoords.begin(),
            mPreparedFrame.cellTextureCoords.begin() + mPreparedFrame.cellDrawVertexCount * 4);
    snapshot.cellCenterCoords.assign(mPreparedFrame.cellCenterCoords.begin(),
            mPreparedFrame.cellCenterCoords.begin() + mPreparedFrame.cellCenterFloatCount);
    snapshot.cellColors.assign(mPreparedFrame.cellColors.begin(),
            mPreparedFrame.cellColors.begin() + mPreparedFrame.cellDrawVertexCount * 4);
    if (ghostTint) {
        for (int vertexIndex = 0; vertexIndex < mPreparedFrame.cellDrawVertexCount;
                vertexIndex++) {
            snapshot.cellColors[4 * vertexIndex] = 0.18f;
            snapshot.cellColors[4 * vertexIndex + 1] = 0.22f;
            snapshot.cellColors[4 * vertexIndex + 2] = 0.26f;
        }
    }
    snapshot.cellIndices.assign(mPreparedFrame.cellIndices.begin(),
            mPreparedFrame.cellIndices.begin() + mPreparedFrame.cellDrawIndexCount);
    snapshot.foodInstanceAttributes.assign(
            mPreparedFrame.foodInstanceAttributes.begin(),
            mPreparedFrame.foodInstanceAttributes.begin() + mPreparedFrame.foodInstanceCount * 4);
    snapshot.foodVertexAttributes.assign(
            mPreparedFrame.foodVertexAttributes.begin(),
            mPreparedFrame.foodVertexAttributes.begin() + mPreparedFrame.foodInstanceCount * 4);
    snapshot.deadCellInstanceAttributes.assign(
            mPreparedFrame.deadCellInstanceAttributes.begin(),
            mPreparedFrame.deadCellInstanceAttributes.begin() + mPreparedFrame.deadCellInstanceCount * 4);
    snapshot.deadCellVertexAttributes.assign(
            mPreparedFrame.deadCellVertexAttributes.begin(),
            mPreparedFrame.deadCellVertexAttributes.begin() + mPreparedFrame.deadCellInstanceCount * 4);
    snapshot.linkTriangleCoords.assign(mPreparedFrame.linkTriangleCoords.begin(),
            mPreparedFrame.linkTriangleCoords.begin() + mPreparedFrame.drawLinksCount * 4);
    snapshot.linkTextureCoords.assign(mPreparedFrame.linkTextureCoords.begin(),
            mPreparedFrame.linkTextureCoords.begin() + mPreparedFrame.drawLinksCount);
    snapshot.cellVertexCountBefore = mPreparedFrame.cellVertexCountBefore;
    snapshot.cellVertexCount = mPreparedFrame.cellVertexCount;
    snapshot.cellIndexCountBefore = mPreparedFrame.cellIndexCountBefore;
    snapshot.cellIndexCount = mPreparedFrame.cellIndexCount;
    snapshot.bunchStart = mPreparedFrame.bunchStart;
    snapshot.bunchSize = mPreparedFrame.bunchSize;
    snapshot.bunchIndexStart = mPreparedFrame.bunchIndexStart;
    snapshot.bunchIndexSize = mPreparedFrame.bunchIndexSize;
    snapshot.bunches = mPreparedFrame.bunches;
    snapshot.cellDrawVertexCount = mPreparedFrame.cellDrawVertexCount;
    snapshot.cellDrawIndexCount = mPreparedFrame.cellDrawIndexCount;
    snapshot.cellCenterFloatCount = mPreparedFrame.cellCenterFloatCount;
    snapshot.foodInstanceCount = mPreparedFrame.foodInstanceCount;
    snapshot.deadCellInstanceCount = mPreparedFrame.deadCellInstanceCount;
    snapshot.drawLinksCount = mPreparedFrame.drawLinksCount;
    snapshot.geometryReady = true;
    snapshot.valid = true;
}

void NativeSimulationWorldRenderer::restoreDrawSnapshot(
        const PreparedFrame& snapshot) {
    if (!snapshot.valid) {
        mFrameBuilder.clearDrawCounts(mPreparedFrame);
        mPreparedFrame.markers.clear();
        mPreparedFrame.geometryReady = true;
        return;
    }
    mPreparedFrame.valid = false;
    mPreparedFrame.geometryReady = false;
    mPreparedFrame.identity = snapshot.identity;
    mPreparedFrame.environment = snapshot.environment;
    mPreparedFrame.viewport = snapshot.viewport;
    mPreparedFrame.layout = snapshot.layout;
    mPreparedFrame.interpolation = snapshot.interpolation;
    mPreparedFrame.diagnostics = snapshot.diagnostics;
    mPreparedFrame.markers.assign(snapshot.markers.begin(),
            snapshot.markers.end());
    mPreparedFrame.cellTriangleCoords = snapshot.cellTriangleCoords;
    mPreparedFrame.cellTextureCoords = snapshot.cellTextureCoords;
    mPreparedFrame.cellCenterCoords = snapshot.cellCenterCoords;
    mPreparedFrame.cellColors = snapshot.cellColors;
    mPreparedFrame.cellIndices = snapshot.cellIndices;
    mPreparedFrame.foodInstanceAttributes = snapshot.foodInstanceAttributes;
    mPreparedFrame.foodVertexAttributes = snapshot.foodVertexAttributes;
    mPreparedFrame.deadCellInstanceAttributes =
            snapshot.deadCellInstanceAttributes;
    mPreparedFrame.deadCellVertexAttributes = snapshot.deadCellVertexAttributes;
    mPreparedFrame.linkTriangleCoords = snapshot.linkTriangleCoords;
    mPreparedFrame.linkTextureCoords = snapshot.linkTextureCoords;
    mPreparedFrame.cellVertexCountBefore = snapshot.cellVertexCountBefore;
    mPreparedFrame.cellVertexCount = snapshot.cellVertexCount;
    mPreparedFrame.cellIndexCountBefore = snapshot.cellIndexCountBefore;
    mPreparedFrame.cellIndexCount = snapshot.cellIndexCount;
    mPreparedFrame.bunchStart = snapshot.bunchStart;
    mPreparedFrame.bunchSize = snapshot.bunchSize;
    mPreparedFrame.bunchIndexStart = snapshot.bunchIndexStart;
    mPreparedFrame.bunchIndexSize = snapshot.bunchIndexSize;
    mPreparedFrame.bunches = snapshot.bunches;
    mPreparedFrame.cellDrawVertexCount = snapshot.cellDrawVertexCount;
    mPreparedFrame.cellDrawIndexCount = snapshot.cellDrawIndexCount;
    mPreparedFrame.cellCenterFloatCount = snapshot.cellCenterFloatCount;
    mPreparedFrame.foodInstanceCount = snapshot.foodInstanceCount;
    mPreparedFrame.deadCellInstanceCount = snapshot.deadCellInstanceCount;
    mPreparedFrame.drawLinksCount = snapshot.drawLinksCount;
    mPreparedFrame.geometryReady = true;
}
