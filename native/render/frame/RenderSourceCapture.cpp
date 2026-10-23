#include "render/frame/RenderSourceCapture.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

#ifdef CELLULARITY_RENDER_PROFILE
#include <chrono>
#endif

#include "core/CellData.h"
#include "core/CellTopology.h"
#include "core/SimulationWorld.h"
#include "core/CollisionGrid.h"
#include "core/DeadSplashData.h"
#include "core/Environment.h"
#include "core/FoodData.h"
#include "core/Gene.h"
#include "core/RendererMemoryModel.h"
#include "render/frame/RenderContactLimits.h"
#include "render/frame/RenderTimelineClock.h"

namespace {

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

std::size_t checkedSize(std::uint64_t requested, const char* message) {
    if (requested > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error(message);
    }
    return static_cast<std::size_t>(requested);
}

} // namespace

cellularity::render::RenderSourceCapture::RenderSourceCapture(
        int maxCellCount, RenderSourceFrame& frame) {
    reset(maxCellCount, frame);
}

void cellularity::render::RenderSourceCapture::reset(int maxCellCount,
        RenderSourceFrame& frame) {
    cellularity::memory::RendererInitialBufferLayout layout;
    if (!cellularity::memory::tryBuildRendererInitialBufferLayout(maxCellCount,
                layout)) {
        throw std::length_error("RenderSourceCapture capacity is unsupported");
    }

    const std::size_t cellVisitCount =
            checkedSize(layout.cellVisitEntries,
                    "RenderSourceCapture visit capacity is unsupported");
    const std::size_t sourceRecordIndexCount =
            checkedSize(layout.sourceRecordIndexEntries,
                    "RenderSourceCapture record-index capacity is unsupported");
    const std::size_t sourceRecordGenerationCount = checkedSize(
            layout.sourceRecordGenerationEntries,
            "RenderSourceCapture record-generation capacity is unsupported");
    const std::size_t candidateWordCount =
            checkedSize(layout.linkCandidateWordEntries,
                    "RenderSourceCapture candidate capacity is unsupported");
    const std::size_t maximumRawContactChordCount =
            checkedSize(layout.maximumRawContactChordEntries,
                    "RenderSourceCapture contact-chord capacity is unsupported");
    const std::size_t maximumNormalEdgeCount =
            checkedSize(layout.maximumNormalEdgeCount,
                    "RenderSourceCapture normal edge capacity is unsupported");
    const std::size_t maximumCandidateLinkCount =
            checkedSize(layout.maximumDrawableLinkCount,
                    "RenderSourceCapture Link capacity is unsupported");

    resetVectorStorage(mCellVisitMark, cellVisitCount, 0);
    resetVectorStorage(mSourceRecordIndexByCell, sourceRecordIndexCount, -1);
    resetVectorStorage(mSourceRecordGenerationByCell, sourceRecordGenerationCount,
            0);
    resetVectorStorage(mLinkCandidateBits, candidateWordCount, std::uint64_t{0});
    std::vector<FoodSpatialCandidate>().swap(mFoodSpatialCandidates);
    mRenderContactChords.reset(maxCellCount);
    frame = RenderSourceFrame{};

    mCellCapacity = maxCellCount;
    mMaximumRawContactChordCount = maximumRawContactChordCount;
    mMaximumNormalEdgeCount = maximumNormalEdgeCount;
    mMaximumCandidateLinkCount = maximumCandidateLinkCount;
    mCellVisitGeneration = 1;
    mSourceRecordGeneration = 1;
}

int cellularity::render::RenderSourceCapture::capture(
        const SimulationWorld& world, const RenderViewBounds& viewBounds,
        const RenderFrameCaptureMetadata& captureMetadata, RenderSourceFrame& frame,
        const RenderMarkerCaptureRequest* markerRequests,
        std::size_t markerRequestCount) {
    return captureImpl(world, viewBounds, captureMetadata, frame, markerRequests,
            markerRequestCount, nullptr);
}

cellularity::render::RenderSourceCapture::Profile
cellularity::render::RenderSourceCapture::profileCapture(
        const SimulationWorld& world, const RenderViewBounds& viewBounds,
        const RenderFrameCaptureMetadata& captureMetadata, RenderSourceFrame& frame,
        const RenderMarkerCaptureRequest* markerRequests,
        std::size_t markerRequestCount) {
    Profile profile;
    captureImpl(world, viewBounds, captureMetadata, frame, markerRequests,
            markerRequestCount, &profile);
    return profile;
}

int cellularity::render::RenderSourceCapture::captureImpl(
        const SimulationWorld& world, const RenderViewBounds& viewBounds,
        const RenderFrameCaptureMetadata& captureMetadata, RenderSourceFrame& frame,
        const RenderMarkerCaptureRequest* markerRequests,
        std::size_t markerRequestCount, Profile* profile) {
#ifdef CELLULARITY_RENDER_PROFILE
    using Clock = std::chrono::steady_clock;
    const auto totalStart = Clock::now();
#else
    (void)profile;
#endif

    beginCapture(world, captureMetadata, frame);
#ifdef CELLULARITY_RENDER_PROFILE
    const auto contactChordCollectionStart = Clock::now();
#endif
    const RenderContactInput contactInput =
            world.cellContactCacheIsFresh()
            ? RenderContactInput::FreshPhysicsContacts
            : RenderContactInput::CurrentGeometry;
#ifdef CELLULARITY_RENDER_PROFILE
    if (profile != nullptr) {
        if (contactInput == RenderContactInput::FreshPhysicsContacts) {
            ++profile->freshPhysicsContactCaptures;
        } else {
            ++profile->staleGeometryContactFallbacks;
        }
    }
#endif
    mRenderContactCollector.collect(contactInput, world.mCellData,
            world.mEnvironment, mRenderContactChords);
#ifdef CELLULARITY_RENDER_PROFILE
    if (profile != nullptr) {
        profile->materializedContactChords =
                static_cast<long long>(mRenderContactChords.activeChordCount());
    }
    const auto cellsStart = Clock::now();
#endif
    const int visibleCount =
            world.mEnvironment.mIsPreviewMode
            ? captureVisibleCellsLinear(world, viewBounds, frame)
            : captureVisibleCells(world, viewBounds, frame);
#ifdef CELLULARITY_RENDER_PROFILE
    const auto linksStart = Clock::now();
#endif
    captureCandidateLinks(world, viewBounds, frame, profile);
#ifdef CELLULARITY_RENDER_PROFILE
    const auto foodsStart = Clock::now();
    captureFoods(world, viewBounds, frame, profile);
    const auto deadStart = Clock::now();
#else
    captureFoods(world, viewBounds, frame);
#endif
    captureDeadSplashes(world, viewBounds, frame);
#ifdef CELLULARITY_RENDER_PROFILE
    const auto markersStart = Clock::now();
#endif
    captureMarkers(world, markerRequests, markerRequestCount, frame);
#ifdef CELLULARITY_RENDER_PROFILE
    const auto captureEnd = Clock::now();
#endif

    if (frame.activeContactChords.size() != frame.activeContactChordNeighborRuntimeIds.size()) {
        throw std::logic_error("RenderSourceCapture produced mismatched "
                               "contact-chord identity ranges");
    }
    frame.capture.timeline.sourceCapturedNanos =
            cellularity::render::renderTimelineNowNanos();
    frame.valid = true;
#ifdef CELLULARITY_RENDER_PROFILE
    if (profile != nullptr) {
        profile->contactChordCollectionNanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                        cellsStart - contactChordCollectionStart)
                        .count();
        profile->cellCaptureNanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(linksStart - cellsStart)
                        .count();
        profile->linkCaptureNanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(foodsStart - linksStart)
                        .count();
        profile->foodCaptureNanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(deadStart - foodsStart)
                        .count();
        profile->deadSplashCaptureNanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(markersStart - deadStart)
                        .count();
        profile->markerCaptureNanos =
                std::chrono::duration_cast<std::chrono::nanoseconds>(captureEnd - markersStart)
                        .count();
        profile->totalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                captureEnd - totalStart)
                                      .count();
    }
#endif
    return visibleCount;
}

void cellularity::render::RenderSourceCapture::beginCapture(
        const SimulationWorld& world, const RenderFrameCaptureMetadata& captureMetadata,
        RenderSourceFrame& frame) {
    // Invalidate the reusable slot before any validation or mutation. A failed
    // capture can never leave the previous source frame publishable.
    frame.valid = false;
    const CellData& cellData = world.mCellData;
    if (cellData.mCellCount < 0 || cellData.mCellCount > mCellCapacity) {
        throw std::length_error(
                "RenderSourceCapture active Cell range exceeds configured capacity");
    }
    const int normalEdgeCount = cellData.mTopology.edgeCount();
    const int boundaryLinkCount = cellData.mTopology.boundaryCount();
    if (normalEdgeCount < 0 || boundaryLinkCount < 0
            || static_cast<std::size_t>(normalEdgeCount) > mMaximumNormalEdgeCount
            || static_cast<std::size_t>(boundaryLinkCount) > mMaximumCandidateLinkCount - 2U * static_cast<std::size_t>(normalEdgeCount)) {
        // A normal edge consumes two adjacency slots; a boundary Link consumes one.
        throw std::length_error(
                "RenderSourceCapture topology exceeds configured adjacency capacity");
    }
    if (world.mFoodData.MAX_FOOD_COUNT < 0 || world.mDeadSplashData.MAX_DEAD_SPLASH_COUNT < 0 || world.mFoodData.mFoodCount < 0 || world.mFoodData.mFoodCount > world.mFoodData.MAX_FOOD_COUNT || world.mDeadSplashData.mDeadSplashCount < 0 || world.mDeadSplashData.mDeadSplashCount > world.mDeadSplashData.MAX_DEAD_SPLASH_COUNT) {
        throw std::length_error(
                "RenderSourceCapture received an invalid auxiliary range");
    }

    frame.layoutVersion = kRenderSourceFrameLayoutVersion;
    frame.captureSequence = nextCaptureSequence();
    frame.capture = captureMetadata;
    frame.environment = {};
    frame.environment.revision = captureMetadata.environmentRevision;
    frame.environment.previewMode = world.mEnvironment.mIsPreviewMode;
    frame.environment.lightAngle = world.mEnvironment.mLightAngle;
    frame.environment.lightRange = world.mEnvironment.mLightRange;
    frame.environment.lightIntensity = world.mEnvironment.mLightIntensity;
    frame.environment.dishRadius = world.mEnvironment.geometry().radius();
    frame.environment.dishInnerRadius = world.mEnvironment.geometry().innerRadius();
    frame.sourceCellCount = static_cast<std::uint64_t>(cellData.mCellCount);
    frame.sourceFoodCount =
            static_cast<std::uint64_t>(world.mFoodData.mFoodCount);
    frame.sourceDeadSplashCount =
            static_cast<std::uint64_t>(world.mDeadSplashData.mDeadSplashCount);
    frame.sourceTopologyEdgeCount =
            static_cast<std::uint64_t>(cellData.mTopology.edgeCount());
    frame.sourceBoundaryLinkCount =
            static_cast<std::uint64_t>(cellData.mTopology.boundaryCount());
    frame.sourceFoodCapacity =
            static_cast<std::uint64_t>(world.mFoodData.MAX_FOOD_COUNT);
    frame.sourceDeadSplashCapacity =
            static_cast<std::uint64_t>(world.mDeadSplashData.MAX_DEAD_SPLASH_COUNT);
    frame.visibleCellCount = 0;
    for (auto& bucket : frame.orderedCellRecordIndices) {
        bucket.clear();
    }
    frame.cells.clear();
    frame.activeContactChords.clear();
    frame.activeContactChordNeighborRuntimeIds.clear();
    frame.candidateLinks.clear();
    frame.foods.clear();
    frame.deadSplashes.clear();
    frame.markers.clear();
    nextSourceRecordGeneration();
}

int cellularity::render::RenderSourceCapture::ensureSourceCell(
        const SimulationWorld& world, int cellIndex, bool includeContactChords,
        RenderSourceFrame& frame) {
    const CellData& cellData = world.mCellData;
    if (cellIndex < 0 || cellIndex >= cellData.mCellCount) {
        throw std::out_of_range(
                "RenderSourceCapture received an invalid Cell index");
    }
    const std::size_t sourceIndex = static_cast<std::size_t>(cellIndex);
    int recordIndex = -1;
    if (mSourceRecordGenerationByCell[sourceIndex] == mSourceRecordGeneration) {
        recordIndex = mSourceRecordIndexByCell[sourceIndex];
    } else {
        if (frame.cells.size() >= static_cast<std::size_t>(mCellCapacity)) {
            throw std::length_error(
                    "RenderSourceCapture compact Cell range exceeds capacity");
        }

        const cellularity::GeneView gene = cellData.getActiveGeneView(cellIndex);
        RenderSourceCell record;
        record.runtimeId = cellData.runtimeId(cellIndex);
        if (!isEntityCellRuntimeId(record.runtimeId)) {
            throw std::logic_error(
                    "RenderSourceCapture received a Cell without a runtime identity");
        }
        record.sourceCellIndex = cellIndex;
        record.cellType = gene.cellType();
        record.positionX = static_cast<float>(cellData.mPositionX[sourceIndex]);
        record.positionY = static_cast<float>(cellData.mPositionY[sourceIndex]);
        record.radius = static_cast<float>(cellData.mRadius[sourceIndex]);
        record.angle = static_cast<float>(cellData.mAngle[sourceIndex]);
        record.red =
                cellData.mColor[sourceIndex][0] + cellData.mToxin[sourceIndex] * 0.1f;
        record.green =
                cellData.mColor[sourceIndex][1] + cellData.mToxication[sourceIndex];
        record.blue = cellData.mColor[sourceIndex][2];
        const bool mirrored = cellData.mIsMirrored[sourceIndex] != 0;
        record.mirrored = mirrored;

        switch (gene.cellType()) {
            case CellType::LIPOCYTE:
                record.baseAux = cellData.mLipids[sourceIndex] / CellData::MAX_LIPIDS;
                break;
            case CellType::SECROCYTE:
                record.baseAux = cellData.mSwimAnimationTime[sourceIndex];
                break;
            case CellType::PHOTOCYTE:
            case CellType::GAMETE:
                record.baseAux4 =
                        static_cast<float>(cellData.mAngleCytoplasm[sourceIndex]);
                break;
            case CellType::NEUROCYTE:
            case CellType::STEREOCYTE:
            case CellType::SENSEOCYTE:
                record.baseAux = cellData.mNewSignalSubstance[sourceIndex][0] * CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT_INVERSE;
                record.baseAux4 = cellData.mNewSignalSubstance[sourceIndex][1] * CellData::MAX_SIGNAL_SUBSTANCE_OUTPUT_INVERSE;
                break;
            case CellType::MYOCYTE:
                record.baseAux = -cellData.mMyocyteContraction[sourceIndex];
                record.baseAux4 = cellData.mMyocyteBending[sourceIndex];
                break;
            case CellType::STEMOCYTE: {
                const float first = std::max(
                        0.0f,
                        cellData.getValue(cellIndex, Gene::STEM_THRESHOLD_1_VALUE_INDEX, 0) + 0.5f);
                const float second = std::max(
                        0.0f,
                        cellData.getValue(cellIndex, Gene::STEM_THRESHOLD_2_VALUE_INDEX, 0) + 0.5f);
                record.baseAux = first / (1.0f + second * second) * static_cast<float>(std::acos(-1.0));
                record.baseAux4 =
                        second / (1.0f + first * first) * static_cast<float>(std::acos(-1.0));
                break;
            }
            default:
                break;
        }

        record.previewAux =
                record.angle + gene.splitAngle() * (mirrored ? -1.0f : 1.0f);
        record.previewAux4 =
                static_cast<float>(record.radius * 2.0 * (0.5 - gene.splitRatio()));

        if (gene.cellType() == CellType::GLUEOCYTE) {
            record.outlineAux1 = static_cast<float>(cellData.hairX[sourceIndex]);
            record.outlineAux2 = static_cast<float>(cellData.hairY[sourceIndex]);
        }
        if (gene.cellType() == CellType::CILIOCYTE) {
            if (mirrored) {
                record.outlineAux1 = cellData.mSwimAnimationTime2[sourceIndex];
                record.outlineAux2 = cellData.mSwimAnimationTime[sourceIndex];
            } else {
                record.outlineAux1 = cellData.mSwimAnimationTime[sourceIndex];
                record.outlineAux2 = cellData.mSwimAnimationTime2[sourceIndex];
            }
        }
        record.outlineAux3 =
                static_cast<float>(cellData.mHairRotation[sourceIndex]);
        if (gene.cellType() == CellType::SECROCYTE) {
            record.outlineAux3 = cellData.mSwimAnimationTime[sourceIndex];
        }
        record.outlineAnimation = cellData.mSwimAnimationTime[sourceIndex];

        recordIndex = static_cast<int>(frame.cells.size());
        frame.cells.push_back(record);
        mSourceRecordIndexByCell[sourceIndex] = recordIndex;
        mSourceRecordGenerationByCell[sourceIndex] = mSourceRecordGeneration;
    }

    assert(recordIndex >= 0);
    assert(static_cast<std::size_t>(recordIndex) < frame.cells.size());
    RenderSourceCell& record = frame.cells[static_cast<std::size_t>(recordIndex)];
    if (includeContactChords && !record.hasContactChordPayload) {
        captureSourceCellContactChords(world, cellIndex, record, frame);
    }
    return recordIndex;
}

void cellularity::render::RenderSourceCapture::captureSourceCellContactChords(
        const SimulationWorld& world, int cellIndex, RenderSourceCell& record,
        RenderSourceFrame& frame) {
    const CellData& cellData = world.mCellData;
    const RenderContactChordBuffer::ActiveRange sourceContactChords =
            mRenderContactChords.activeChords(cellIndex);
    const int contactChordCount = sourceContactChords.size();
    if (contactChordCount < 0 || contactChordCount > kMaximumRenderContactChordsPerCell) {
        throw std::length_error(
                "RenderSourceCapture received an invalid active contact-chord count");
    }
    const std::size_t activeContactChordCount =
            static_cast<std::size_t>(contactChordCount);
    if (frame.activeContactChords.size() > mMaximumRawContactChordCount || activeContactChordCount > mMaximumRawContactChordCount - frame.activeContactChords.size()) {
        throw std::length_error(
                "RenderSourceCapture active contact-chord range exceeds capacity");
    }

    if (frame.activeContactChords.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error(
                "RenderSourceCapture contact-chord offset exceeds its representation");
    }
    record.contactChordOffset =
            static_cast<std::uint32_t>(frame.activeContactChords.size());
    if (activeContactChordCount != 0) {
        frame.activeContactChords.insert(
                frame.activeContactChords.end(), sourceContactChords.begin(),
                sourceContactChords.end());
        for (int contactChordIndex = 0; contactChordIndex < contactChordCount;
                ++contactChordIndex) {
            const int neighborCellIndex =
                    sourceContactChords[contactChordIndex]
                            .neighborCellIndex;
            CellRuntimeId neighborRuntimeId = kInvalidCellRuntimeId;
            if (neighborCellIndex == CellData::BOUNDARY_CONNECTION_INDEX) {
                neighborRuntimeId = kBoundaryCellRuntimeId;
            } else if (neighborCellIndex >= 0 && neighborCellIndex < cellData.mCellCount) {
                neighborRuntimeId = cellData.runtimeId(neighborCellIndex);
                if (!isEntityCellRuntimeId(neighborRuntimeId)) {
                    throw std::logic_error("RenderSourceCapture contact-chord neighbor "
                                           "has no runtime identity");
                }
            }
            frame.activeContactChordNeighborRuntimeIds.push_back(neighborRuntimeId);
        }
    }
    record.contactChordCount = static_cast<std::uint8_t>(contactChordCount);
    record.hasContactChordPayload = true;
}

int cellularity::render::RenderSourceCapture::captureVisibleCells(
        const SimulationWorld& world, const RenderViewBounds& viewBounds,
        RenderSourceFrame& frame) {
    constexpr float maxRadius =
            static_cast<float>(4.0 * CellData::MAX_CELL_RADIUS);
    const float cullLeft = viewBounds.minX - maxRadius;
    const float cullRight = viewBounds.maxX + maxRadius;
    const float cullBottom = viewBounds.minY - maxRadius;
    const float cullTop = viewBounds.maxY + maxRadius;

    const int visitGeneration = nextCellVisitGeneration();
    const CollisionGrid& grid = *world.mCellCollisionGrid;
    const int minGridX =
            grid.clampGridPosition(grid.convertWorldToGridPosition(cullLeft));
    const int maxGridX =
            grid.clampGridPosition(grid.convertWorldToGridPosition(cullRight));
    const int minGridY =
            grid.clampGridPosition(grid.convertWorldToGridPosition(cullBottom));
    const int maxGridY =
            grid.clampGridPosition(grid.convertWorldToGridPosition(cullTop));
    const int gridStride = grid.stride();

    const CellData& cellData = world.mCellData;
    auto captureVisibleCell = [&](int cellIndex) {
        if (cellIndex < 0 || cellIndex >= cellData.mCellCount) {
            return;
        }
        const std::size_t sourceIndex = static_cast<std::size_t>(cellIndex);
        if (mCellVisitMark[sourceIndex] == visitGeneration) {
            return;
        }
        mCellVisitMark[sourceIndex] = visitGeneration;
        if (cellData.mIsDead[sourceIndex]) {
            return;
        }
        if (cellData.mPositionX[sourceIndex] < cullLeft || cellData.mPositionX[sourceIndex] > cullRight || cellData.mPositionY[sourceIndex] < cullBottom || cellData.mPositionY[sourceIndex] > cullTop) {
            return;
        }

        const int recordIndex = ensureSourceCell(world, cellIndex, true, frame);
        const int typeIndex = CellTypes::ordinal(
                frame.cells[static_cast<std::size_t>(recordIndex)].cellType);
        auto& bucket =
                frame.orderedCellRecordIndices[static_cast<std::size_t>(typeIndex)];
        if (bucket.size() >= static_cast<std::size_t>(mCellCapacity)) {
            throw std::length_error(
                    "RenderSourceCapture visible bucket exceeds capacity");
        }
        bucket.push_back(recordIndex);
        ++frame.visibleCellCount;
    };

    for (int gridY = minGridY; gridY <= maxGridY; ++gridY) {
        const int rowStart = gridY * gridStride;
        for (int gridX = minGridX; gridX <= maxGridX; ++gridX) {
            const int gridIndex = rowStart + gridX;
            const int gridCount = grid.countAt(gridIndex);
            const int entryOffset = grid.entryOffsetAt(gridIndex);
            for (int localIndex = 0; localIndex < gridCount; ++localIndex) {
                captureVisibleCell(grid.indexAtOffset(entryOffset + localIndex));
            }
            for (int cellIndex = world.mCellRenderOverflowGridHead[gridIndex];
                    cellIndex != -1;
                    cellIndex =
                            world.mCellRenderOverflowGridNext[static_cast<std::size_t>(
                                    cellIndex)]) {
                captureVisibleCell(cellIndex);
            }
        }
    }
    return frame.visibleCellCount;
}

int cellularity::render::RenderSourceCapture::captureVisibleCellsLinear(
        const SimulationWorld& world, const RenderViewBounds& viewBounds,
        RenderSourceFrame& frame) {
    constexpr float maxRadius =
            static_cast<float>(4.0 * CellData::MAX_CELL_RADIUS);
    const float cullLeft = viewBounds.minX - maxRadius;
    const float cullRight = viewBounds.maxX + maxRadius;
    const float cullBottom = viewBounds.minY - maxRadius;
    const float cullTop = viewBounds.maxY + maxRadius;

    const CellData& cellData = world.mCellData;
    for (int cellIndex = 0; cellIndex < cellData.mCellCount; ++cellIndex) {
        const std::size_t sourceIndex = static_cast<std::size_t>(cellIndex);
        if (cellData.mIsDead[sourceIndex]) {
            continue;
        }
        if (cellData.mPositionX[sourceIndex] < cullLeft || cellData.mPositionX[sourceIndex] > cullRight || cellData.mPositionY[sourceIndex] < cullBottom || cellData.mPositionY[sourceIndex] > cullTop) {
            continue;
        }
        const int recordIndex = ensureSourceCell(world, cellIndex, true, frame);
        const int typeIndex = CellTypes::ordinal(
                frame.cells[static_cast<std::size_t>(recordIndex)].cellType);
        frame.orderedCellRecordIndices[static_cast<std::size_t>(typeIndex)]
                .push_back(recordIndex);
        ++frame.visibleCellCount;
    }

    if (world.mEnvironment.mIsPreviewMode) {
        auto& previewBucket =
                frame.orderedCellRecordIndices[kPreviewExtraCellTypeIndex];
        for (int cellIndex = 0; cellIndex < cellData.mCellCount; ++cellIndex) {
            const int recordIndex = ensureSourceCell(world, cellIndex, true, frame);
            frame.cells[static_cast<std::size_t>(recordIndex)].previewArrow = true;
            previewBucket.push_back(recordIndex);
        }
    }
    return frame.visibleCellCount;
}

void cellularity::render::RenderSourceCapture::captureCandidateLinks(
        const SimulationWorld& world, const RenderViewBounds& viewBounds,
        RenderSourceFrame& frame, Profile* profile) {
    const float margin = static_cast<float>(CellData::MAX_CELL_RADIUS * 2.0);
    const RenderViewBounds captureBounds{viewBounds.minX - margin, viewBounds.maxX + margin,
            viewBounds.minY - margin, viewBounds.maxY + margin};
    captureNormalCandidateLinks(world, captureBounds, frame, profile);
    captureBoundaryCandidateLinks(world, captureBounds, frame, profile);
}

void cellularity::render::RenderSourceCapture::captureNormalCandidateLinks(
        const SimulationWorld& world, const RenderViewBounds& captureBounds,
        RenderSourceFrame& frame, Profile* profile) {
    const float cullLeft = captureBounds.minX;
    const float cullRight = captureBounds.maxX;
    const float cullBottom = captureBounds.minY;
    const float cullTop = captureBounds.maxY;
    const CellData& cellData = world.mCellData;
    const CellTopology& topology = cellData.mTopology;
    const CellTopology::RenderEdgeView topologyEdges = topology.renderEdgeView();
    const int* const topologyLinkCounts = topology.cellLinkCounts();
    const int topologyEdgeCount = topologyEdges.count;
    if (profile != nullptr) {
        profile->linkTopologyEdgeCount = topologyEdgeCount;
    }
    if (topologyEdgeCount == 0) {
        return;
    }

    const CollisionGrid& grid = *world.mCellCollisionGrid;
    auto lowerGridPosition = [&](float bound) {
        if (!std::isfinite(bound)) {
            return std::signbit(bound) ? 0 : grid.gridSize() + 1;
        }
        return grid.clampGridPosition(grid.convertWorldToGridPosition(
                std::nextafter(bound, -std::numeric_limits<float>::infinity())));
    };
    auto upperGridPosition = [&](float bound) {
        if (!std::isfinite(bound)) {
            return std::signbit(bound) ? 0 : grid.gridSize() + 1;
        }
        return grid.clampGridPosition(grid.convertWorldToGridPosition(
                std::nextafter(bound, std::numeric_limits<float>::infinity())));
    };
    const int minGridX = lowerGridPosition(cullLeft);
    const int maxGridX = upperGridPosition(cullRight);
    const int minGridY = lowerGridPosition(cullBottom);
    const int maxGridY = upperGridPosition(cullTop);
    const int gridStride = grid.stride();
    const std::size_t candidateWordCount =
            (static_cast<std::size_t>(topologyEdgeCount) + 63U) / 64U;
    if (candidateWordCount > mLinkCandidateBits.size()) {
        throw std::length_error(
                "RenderSourceCapture Link candidate capacity exceeded");
    }
    std::fill_n(mLinkCandidateBits.begin(), candidateWordCount, std::uint64_t{0});

    if (profile != nullptr) {
        profile->linkGridBucketCount =
                (maxGridX - minGridX + 1) * (maxGridY - minGridY + 1);
    }

    auto collectCellCandidates = [&](int cellIndex) {
        if (profile != nullptr) {
            ++profile->linkGridCellVisits;
        }
        const float cellPositionX = static_cast<float>(
                cellData.mPositionX[static_cast<std::size_t>(cellIndex)]);
        const float cellPositionY = static_cast<float>(
                cellData.mPositionY[static_cast<std::size_t>(cellIndex)]);
        const bool endpointInView =
                cellPositionX >= cullLeft && cellPositionX <= cullRight && cellPositionY >= cullBottom && cellPositionY <= cullTop;
        if (!endpointInView) {
            return;
        }
        if (profile != nullptr) {
            ++profile->linkEndpointCellCount;
        }
        const int linkCount = topologyLinkCounts[cellIndex];
        for (int slotIndex = 0; slotIndex < linkCount; ++slotIndex) {
            if (profile != nullptr) {
                ++profile->linkAdjacencyVisits;
            }
            const int edgeIndex =
                    topology.incidentEdgeIndexAtValidSlot(cellIndex, slotIndex);
            if (edgeIndex < 0) {
                continue;
            }
            const std::size_t wordIndex = static_cast<std::size_t>(edgeIndex) / 64U;
            const std::uint64_t edgeBit =
                    std::uint64_t{1} << (static_cast<unsigned int>(edgeIndex) & 63U);
            mLinkCandidateBits[wordIndex] |= edgeBit;
        }
    };

    for (int gridY = minGridY; gridY <= maxGridY; ++gridY) {
        const int rowStart = gridY * gridStride;
        const int firstGridIndex = rowStart + minGridX;
        const int lastGridIndex = rowStart + maxGridX;
        const std::size_t firstWordIndex =
                static_cast<std::size_t>(firstGridIndex) / 64U;
        const std::size_t lastWordIndex =
                static_cast<std::size_t>(lastGridIndex) / 64U;
        for (std::size_t wordIndex = firstWordIndex; wordIndex <= lastWordIndex;
                ++wordIndex) {
            if (profile != nullptr) {
                ++profile->linkGridWordCount;
            }
            std::uint64_t occupied = world.mCellRenderGridOccupied[wordIndex];
            if (wordIndex == firstWordIndex) {
                occupied &= ~std::uint64_t{0}
                        << (static_cast<unsigned int>(firstGridIndex) & 63U);
            }
            if (wordIndex == lastWordIndex) {
                occupied &= ~std::uint64_t{0} >> (63U - (static_cast<unsigned int>(lastGridIndex) & 63U));
            }
            while (occupied != 0) {
#if defined(__clang__) || defined(__GNUC__)
                const int gridBitIndex = __builtin_ctzll(occupied);
#else
                int gridBitIndex = 0;
                for (std::uint64_t bits = occupied; (bits & 1U) == 0; bits >>= 1U) {
                    ++gridBitIndex;
                }
#endif
                const int gridIndex = static_cast<int>(wordIndex * 64U) + gridBitIndex;
                if (profile != nullptr) {
                    ++profile->linkOccupiedBucketCount;
                }
                const int primaryCount = grid.countAt(gridIndex);
                const int primaryOffset = grid.entryOffsetAt(gridIndex);
                for (int localIndex = 0; localIndex < primaryCount; ++localIndex) {
                    collectCellCandidates(grid.indexAtOffset(primaryOffset + localIndex));
                }
                for (int cellIndex = world.mCellRenderOverflowGridHead[gridIndex];
                        cellIndex != -1;
                        cellIndex =
                                world.mCellRenderOverflowGridNext[static_cast<std::size_t>(
                                        cellIndex)]) {
                    collectCellCandidates(cellIndex);
                }
                occupied &= occupied - 1U;
            }
        }
    }

    for (std::size_t wordIndex = 0; wordIndex < candidateWordCount; ++wordIndex) {
        std::uint64_t candidates = mLinkCandidateBits[wordIndex];
        mLinkCandidateBits[wordIndex] = 0;
        if (profile != nullptr && candidates != 0) {
            ++profile->linkCandidateWordCount;
        }
        while (candidates != 0) {
#if defined(__clang__) || defined(__GNUC__)
            const int bitIndex = __builtin_ctzll(candidates);
#else
            int bitIndex = 0;
            for (std::uint64_t bits = candidates; (bits & 1U) == 0; bits >>= 1U) {
                ++bitIndex;
            }
#endif
            const int edgeIndex = static_cast<int>(wordIndex * 64U) + bitIndex;
            candidates &= candidates - 1U;
            const int cellA = topologyEdges.cellA[edgeIndex];
            const int cellB = topologyEdges.cellB[edgeIndex];
            if (cellA < 0 || cellA >= cellData.mCellCount || cellB < 0
                    || cellB >= cellData.mCellCount) {
                throw std::logic_error(
                        "RenderSourceCapture received an invalid Link endpoint");
            }
            if (cellData.mIsDead[static_cast<std::size_t>(cellA)]
                    || cellData.mIsDead[static_cast<std::size_t>(cellB)]) {
                continue;
            }
            // Rendering owns the Link from its higher-index Cell, independently of topology orientation.
            const bool ownerIsA = cellA > cellB;
            const int renderOwner = ownerIsA ? cellA : cellB;
            const int renderTarget = ownerIsA ? cellB : cellA;
            const std::size_t ownerIndex = static_cast<std::size_t>(renderOwner);
            const std::size_t targetIndex = static_cast<std::size_t>(renderTarget);
            if (frame.candidateLinks.size() >= mMaximumCandidateLinkCount) {
                throw std::length_error(
                        "RenderSourceCapture candidate Link range exceeds capacity");
            }
            frame.candidateLinks.emplace_back();
            RenderSourceLink& link = frame.candidateLinks.back();
            link.runtimeId = topologyEdges.runtimeId[edgeIndex];
            if (!isValidTopologyEdgeRuntimeId(link.runtimeId)) {
                throw std::logic_error(
                        "RenderSourceCapture received a Link without a runtime identity");
            }
            link.sourceLinkIndex = edgeIndex;
            link.sourceCellBIndex = renderTarget;
            link.sourceCellARecordIndex = ensureSourceCell(world, renderOwner, true, frame);
            link.cellARuntimeId = cellData.runtimeId(renderOwner);
            link.cellBRuntimeId = cellData.runtimeId(renderTarget);
            if (!isEntityCellRuntimeId(link.cellARuntimeId)
                    || !isEntityCellRuntimeId(link.cellBRuntimeId)) {
                throw std::logic_error("RenderSourceCapture received a Link endpoint "
                                       "without a runtime identity");
            }
            link.cellAPositionX = static_cast<float>(
                    cellData.mPositionX[ownerIndex]);
            link.cellAPositionY = static_cast<float>(
                    cellData.mPositionY[ownerIndex]);
            link.cellBPositionX = static_cast<float>(
                    cellData.mPositionX[targetIndex]);
            link.cellBPositionY = static_cast<float>(
                    cellData.mPositionY[targetIndex]);
            const double axisX = cellData.mPositionX[targetIndex] - cellData.mPositionX[ownerIndex];
            const double axisY = cellData.mPositionY[targetIndex] - cellData.mPositionY[ownerIndex];
            const double distance = std::sqrt(axisX * axisX + axisY * axisY);
            if (std::isfinite(distance) && distance > 0.0) {
                const double radiusA = cellData.mRadius[ownerIndex];
                const double radiusB = cellData.mRadius[targetIndex];
                const float ratio = static_cast<float>(
                        (radiusA + 0.5 * (distance - radiusA - radiusB)) / distance);
                if (std::isfinite(ratio)) {
                    link.fallbackCenterRatio = ratio;
                }
            }
            link.renderStartOffsetX = ownerIsA ? topologyEdges.renderStartOffsetAX[edgeIndex] : topologyEdges.renderEndOffsetAX[edgeIndex];
            link.renderStartOffsetY = ownerIsA ? topologyEdges.renderStartOffsetAY[edgeIndex] : topologyEdges.renderEndOffsetAY[edgeIndex];
            link.renderEndOffsetX = ownerIsA ? topologyEdges.renderEndOffsetAX[edgeIndex] : topologyEdges.renderStartOffsetAX[edgeIndex];
            link.renderEndOffsetY = ownerIsA ? topologyEdges.renderEndOffsetAY[edgeIndex] : topologyEdges.renderStartOffsetAY[edgeIndex];
            if (profile != nullptr) {
                ++profile->linkCandidateEdgeCount;
            }
        }
    }
}

void cellularity::render::RenderSourceCapture::captureBoundaryCandidateLinks(
        const SimulationWorld& world, const RenderViewBounds& captureBounds,
        RenderSourceFrame& frame, Profile* profile) {
    const CellData& cellData = world.mCellData;
    const CellTopology& topology = cellData.mTopology;
    const CellTopology::BoundaryView boundaries = topology.boundaryView();
    if (profile != nullptr) {
        profile->linkTopologyBoundaryCount = boundaries.count;
    }
    for (int boundaryIndex = 0; boundaryIndex < boundaries.count; ++boundaryIndex) {
        if (profile != nullptr) {
            ++profile->linkBoundaryVisits;
        }
        const int owner = boundaries.cell[boundaryIndex];
        if (owner < 0 || owner >= cellData.mCellCount) {
            throw std::logic_error("RenderSourceCapture received an invalid boundary Link owner");
        }
        const std::size_t ownerIndex = static_cast<std::size_t>(owner);
        const double positionX = cellData.mPositionX[ownerIndex];
        const double positionY = cellData.mPositionY[ownerIndex];
        // Compare original positions to expanded bounds before narrowing the DTO coordinates.
        if (positionX < captureBounds.minX || positionX > captureBounds.maxX
                || positionY < captureBounds.minY || positionY > captureBounds.maxY) {
            if (profile != nullptr) {
                ++profile->linkBoundaryCulledCount;
            }
            continue;
        }
        if (cellData.mIsDead[ownerIndex]) {
            if (profile != nullptr) {
                ++profile->linkBoundaryDeadOwnerCount;
            }
            continue;
        }
        if (frame.candidateLinks.size() >= mMaximumCandidateLinkCount) {
            throw std::length_error("RenderSourceCapture candidate Link range exceeds capacity");
        }
        frame.candidateLinks.emplace_back();
        RenderSourceLink& link = frame.candidateLinks.back();
        link.sourceLinkIndex = boundaryIndex;
        link.sourceCellBIndex = CellData::BOUNDARY_CONNECTION_INDEX;
        link.sourceCellARecordIndex = ensureSourceCell(world, owner, true, frame);
        const RenderSourceCell& sourceCell = frame.cells[static_cast<std::size_t>(link.sourceCellARecordIndex)];
        link.cellARuntimeId = sourceCell.runtimeId;
        link.cellBRuntimeId = kBoundaryCellRuntimeId;
        link.cellAPositionX = sourceCell.positionX;
        link.cellAPositionY = sourceCell.positionY;
        link.cellBPositionX = static_cast<float>(boundaries.anchorX[boundaryIndex]);
        link.cellBPositionY = static_cast<float>(boundaries.anchorY[boundaryIndex]);
        link.renderStartOffsetX = topology.boundaryRenderStartOffsetX(boundaryIndex);
        link.renderStartOffsetY = topology.boundaryRenderStartOffsetY(boundaryIndex);
        // The default invalid edge ID, midpoint ratio and zero end offsets belong to boundary records.
        if (profile != nullptr) {
            ++profile->linkBoundaryCandidateCount;
        }
    }
}

void cellularity::render::RenderSourceCapture::captureFoods(
        const SimulationWorld& world, const RenderViewBounds& viewBounds,
        RenderSourceFrame& frame
#ifdef CELLULARITY_RENDER_PROFILE
        ,
        Profile* profile
#endif
) {
    const float cullLeft = viewBounds.minX - FoodData::RADIUS;
    const float cullRight = viewBounds.maxX + FoodData::RADIUS;
    const float cullBottom = viewBounds.minY - FoodData::RADIUS;
    const float cullTop = viewBounds.maxY + FoodData::RADIUS;
    const FoodData& foodData = world.mFoodData;
    if (foodData.MAX_FOOD_COUNT < 0) {
        throw std::length_error(
                "RenderSourceCapture received a negative Food capacity");
    }

    auto appendFood = [&](int foodIndex) {
#ifdef CELLULARITY_RENDER_PROFILE
        if (profile != nullptr) {
            ++profile->foodCandidateVisits;
        }
#endif
        if (foodIndex < 0 || foodIndex >= foodData.mFoodCount) {
            return;
        }
        const std::size_t sourceIndex = static_cast<std::size_t>(foodIndex);
        const float x = foodData.mPositionX[sourceIndex];
        const float y = foodData.mPositionY[sourceIndex];
        if (x < cullLeft || x > cullRight || y < cullBottom || y > cullTop) {
            return;
        }
        if (frame.foods.size() >= static_cast<std::size_t>(foodData.MAX_FOOD_COUNT)) {
            throw std::length_error(
                    "RenderSourceCapture Food range exceeds capacity");
        }

        const float scaledRadius = static_cast<float>(
                FoodData::RADIUS * std::sqrt(foodData.mMass[sourceIndex] / Environment::MAX_FOOD_SIZE));
        const float lipidLayer = foodData.mLipidLayer[sourceIndex];
        float colorR = 0.6f;
        float colorG = 0.4f;
        float colorB = 0.2f;
        if (lipidLayer > 0.0f) {
            colorR = 0.8f + 0.2f * lipidLayer;
            colorG = 0.4f * (1.0f - lipidLayer);
            colorB = 0.2f * (1.0f - lipidLayer);
        }
        frame.foods.emplace_back();
        RenderSourceFood& record = frame.foods.back();
        record.runtimeId = foodData.runtimeId(foodIndex);
        if (!isValidFoodRuntimeId(record.runtimeId)) {
            throw std::logic_error(
                    "RenderSourceCapture received Food without a runtime identity");
        }
        record.instance = {x, y, scaledRadius, 0.0f};
        record.vertex = {colorR, colorG, colorB, 0.4f};
    };

    const bool hasGridStorage = !world.mFoodSpatialGridHead.empty();
    const bool gridIndexFresh = world.foodSpatialIndexIsFresh();
#ifdef CELLULARITY_RENDER_PROFILE
    if (profile != nullptr) {
        if (gridIndexFresh) {
            ++profile->foodIndexGenerationHits;
        } else {
            ++profile->foodIndexGenerationMisses;
        }
    }
#endif
    bool useGridScan = hasGridStorage;
    if (useGridScan) {
        const CollisionGrid& grid = *world.mCellCollisionGrid;
        const int minGridX =
                grid.clampGridPosition(grid.convertWorldToGridPosition(cullLeft));
        const int maxGridX =
                grid.clampGridPosition(grid.convertWorldToGridPosition(cullRight));
        const int minGridY =
                grid.clampGridPosition(grid.convertWorldToGridPosition(cullBottom));
        const int maxGridY =
                grid.clampGridPosition(grid.convertWorldToGridPosition(cullTop));
        const int gridStride = grid.stride();
        const int coveredGridCells =
                (maxGridX - minGridX + 1) * (maxGridY - minGridY + 1);
        const int totalGridCells = gridStride * gridStride;
        useGridScan = coveredGridCells * 4 < totalGridCells;
#ifdef CELLULARITY_RENDER_PROFILE
        if (!useGridScan && profile != nullptr) {
            ++profile->foodLinearCoverageFallbacks;
        }
#endif

        if (useGridScan && gridIndexFresh) {
#ifdef CELLULARITY_RENDER_PROFILE
            if (profile != nullptr) {
                ++profile->foodGridCaptureCalls;
            }
#endif
            for (int gridY = minGridY; gridY <= maxGridY; ++gridY) {
                const int rowStart = gridY * gridStride;
                for (int gridX = minGridX; gridX <= maxGridX; ++gridX) {
                    const int gridIndex = rowStart + gridX;
                    if (gridIndex < 0 || gridIndex >= static_cast<int>(world.mFoodSpatialGridHead.size())) {
                        continue;
                    }
                    for (int foodIndex = world.mFoodSpatialGridHead[gridIndex];
                            foodIndex != -1;
                            foodIndex = world.mFoodSpatialGridNext[static_cast<std::size_t>(
                                    foodIndex)]) {
                        appendFood(foodIndex);
                    }
                }
            }
        } else if (useGridScan) {
#ifdef CELLULARITY_RENDER_PROFILE
            if (profile != nullptr) {
                ++profile->foodStaleIndexFallbacks;
            }
#endif
            mFoodSpatialCandidates.clear();
            if (mFoodSpatialCandidates.capacity()
                    < static_cast<std::size_t>(foodData.mFoodCount)) {
                mFoodSpatialCandidates.reserve(
                        static_cast<std::size_t>(foodData.mFoodCount));
            }
            for (int foodIndex = 0; foodIndex < foodData.mFoodCount; ++foodIndex) {
#ifdef CELLULARITY_RENDER_PROFILE
                if (profile != nullptr) {
                    ++profile->foodStaleIndexMappingVisits;
                }
#endif
                const int gridIndex = grid.indexAtWorldPosition(
                        foodData.mPositionX[foodIndex], foodData.mPositionY[foodIndex]);
                if (!grid.containsIndex(gridIndex)) {
                    continue;
                }
                const int gridX = gridIndex % gridStride;
                const int gridY = gridIndex / gridStride;
                if (gridX < minGridX || gridX > maxGridX || gridY < minGridY || gridY > maxGridY) {
                    continue;
                }
                mFoodSpatialCandidates.push_back({gridIndex, foodIndex});
            }
            std::sort(mFoodSpatialCandidates.begin(), mFoodSpatialCandidates.end(),
                    [](const FoodSpatialCandidate& left,
                            const FoodSpatialCandidate& right) {
                        return left.gridIndex < right.gridIndex || (left.gridIndex == right.gridIndex && left.foodIndex > right.foodIndex);
                    });
            for (const FoodSpatialCandidate& candidate : mFoodSpatialCandidates) {
                appendFood(candidate.foodIndex);
            }
        }
    }
    if (!useGridScan) {
#ifdef CELLULARITY_RENDER_PROFILE
        if (profile != nullptr) {
            ++profile->foodLinearCaptureCalls;
            if (!hasGridStorage) {
                ++profile->foodLinearMissingIndexFallbacks;
            }
        }
#endif
        for (int foodIndex = 0; foodIndex < foodData.mFoodCount; ++foodIndex) {
            appendFood(foodIndex);
        }
    }
}

void cellularity::render::RenderSourceCapture::captureDeadSplashes(
        const SimulationWorld& world, const RenderViewBounds& viewBounds,
        RenderSourceFrame& frame) {
    const float cullLeft = viewBounds.minX - DeadSplashData::RADIUS;
    const float cullRight = viewBounds.maxX + DeadSplashData::RADIUS;
    const float cullBottom = viewBounds.minY - DeadSplashData::RADIUS;
    const float cullTop = viewBounds.maxY + DeadSplashData::RADIUS;
    const DeadSplashData& deadData = world.mDeadSplashData;
    if (deadData.MAX_DEAD_SPLASH_COUNT < 0) {
        throw std::length_error(
                "RenderSourceCapture received a negative dead-splash capacity");
    }
    for (int deadIndex = 0; deadIndex < deadData.mDeadSplashCount; ++deadIndex) {
        const std::size_t sourceIndex = static_cast<std::size_t>(deadIndex);
        const float x = deadData.mPositionX[sourceIndex];
        const float y = deadData.mPositionY[sourceIndex];
        if (x < cullLeft || x > cullRight || y < cullBottom || y > cullTop) {
            continue;
        }
        if (frame.deadSplashes.size() >= static_cast<std::size_t>(deadData.MAX_DEAD_SPLASH_COUNT)) {
            throw std::length_error(
                    "RenderSourceCapture dead-splash range exceeds capacity");
        }
        const float radius = deadData.mRadius[sourceIndex];
        frame.deadSplashes.emplace_back();
        RenderSourceDeadSplash& record = frame.deadSplashes.back();
        record.runtimeId = deadData.runtimeId(deadIndex);
        if (!isValidDeadSplashRuntimeId(record.runtimeId)) {
            throw std::logic_error("RenderSourceCapture received a dead splash "
                                   "without a runtime identity");
        }
        record.instance = {
                x, y,
                DeadSplashData::RADIUS * (radius / static_cast<float>(CellData::MAX_CELL_RADIUS)),
                std::sqrt(deadData.mAge[sourceIndex])
        };
        record.vertex = {deadData.mColor[sourceIndex][0],
                deadData.mColor[sourceIndex][1],
                deadData.mColor[sourceIndex][2], radius};
    }
}

void cellularity::render::RenderSourceCapture::captureMarkers(
        const SimulationWorld& world, const RenderMarkerCaptureRequest* markerRequests,
        std::size_t markerRequestCount, RenderSourceFrame& frame) {
    if (markerRequestCount > static_cast<std::size_t>(mCellCapacity)) {
        throw std::length_error(
                "RenderSourceCapture marker range exceeds Cell capacity");
    }
    if (markerRequestCount != 0 && markerRequests == nullptr) {
        throw std::invalid_argument(
                "RenderSourceCapture received a null marker request range");
    }
    if (frame.markers.capacity() < markerRequestCount) {
        frame.markers.reserve(markerRequestCount);
    }

    const CellData& cellData = world.mCellData;
    for (std::size_t markerIndex = 0; markerIndex < markerRequestCount;
            ++markerIndex) {
        const RenderMarkerCaptureRequest& request = markerRequests[markerIndex];
        if (request.sourceCellIndex < 0 || request.sourceCellIndex >= cellData.mCellCount) {
            throw std::out_of_range(
                    "RenderSourceCapture marker references an invalid Cell slot");
        }
        const std::size_t cellIndex =
                static_cast<std::size_t>(request.sourceCellIndex);
        if (cellData.mIsDead[cellIndex]) {
            continue;
        }

        RenderMarker marker;
        marker.selectedCellRuntimeId = cellData.runtimeId(request.sourceCellIndex);
        if (!isEntityCellRuntimeId(marker.selectedCellRuntimeId)) {
            throw std::logic_error(
                    "RenderSourceCapture marker Cell has no runtime identity");
        }
        marker.x = static_cast<float>(cellData.mPositionX[cellIndex]);
        marker.y = static_cast<float>(cellData.mPositionY[cellIndex]);
        marker.radius =
                static_cast<float>(cellData.mRadius[cellIndex] + request.radiusOffset);
        marker.alpha = request.alpha;
        frame.markers.push_back(marker);
    }
}

std::uint64_t cellularity::render::RenderSourceCapture::nextCaptureSequence() {
    if (mCaptureSequence == std::numeric_limits<std::uint64_t>::max()) {
        mCaptureSequence = 1;
    } else {
        ++mCaptureSequence;
    }
    return mCaptureSequence;
}

int cellularity::render::RenderSourceCapture::nextCellVisitGeneration() {
    if (mCellVisitGeneration == std::numeric_limits<int>::max()) {
        std::fill(mCellVisitMark.begin(), mCellVisitMark.end(), 0);
        mCellVisitGeneration = 1;
    } else {
        ++mCellVisitGeneration;
    }
    return mCellVisitGeneration;
}

int cellularity::render::RenderSourceCapture::nextSourceRecordGeneration() {
    if (mSourceRecordGeneration == std::numeric_limits<int>::max()) {
        std::fill(mSourceRecordGenerationByCell.begin(),
                mSourceRecordGenerationByCell.end(), 0);
        mSourceRecordGeneration = 1;
    } else {
        ++mSourceRecordGeneration;
    }
    return mSourceRecordGeneration;
}

int cellularity::render::RenderSourceCapture::cellCapacity() const noexcept {
    return mCellCapacity;
}
