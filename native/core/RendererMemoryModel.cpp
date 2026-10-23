#include "core/RendererMemoryModel.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core/CellDataConstants.h"
#include "core/CellRuntimeId.h"
#include "core/CellType.h"
#include "core/RenderContactChord.h"

namespace cellularity::memory {
namespace {

constexpr std::uint64_t kInitialAverageContactCount = 7;
constexpr std::uint64_t kMaximumRenderContactChordsPerCell =
        static_cast<std::uint64_t>(CellDataConstants::MAX_COLLISIONS_PER_CELL) + 1U;
static_assert(kMaximumRenderContactChordsPerCell > static_cast<std::uint64_t>(CellDataConstants::MAX_NEIGHBORS));
constexpr std::uint64_t kCellSectorCount = 4;
constexpr std::uint64_t kPreviewCopyCount = 2;
constexpr std::uint64_t kMaximumOutlineVerticesPerCell = 6;
constexpr std::uint64_t kFloatsPerLinkVertex = 4;
constexpr std::uint64_t kVerticesPerLink = 6;
constexpr std::uint64_t kCapacityGrowthHeadroom = 2;
constexpr std::uint64_t kCellDataBucketCount =
        static_cast<std::uint64_t>(CellTypes::length * 2 + 1);
constexpr std::uint64_t kMaximumDrawBunches = 201;
constexpr std::uint64_t kRenderFrameExchangeSlotCount = 3;
constexpr std::uint64_t kSourceNonBucketVectorCount = 7;
constexpr std::uint64_t kPreparedVectorCount = 12;
constexpr std::uint64_t kCaptureAndBuilderScratchVectorCount = 21;
constexpr std::uint64_t kGhostVectorCount = 12;
// Covers value-only frame identity, viewport, publication watermarks,
// diagnostic timestamps, renderer scalars and ABI alignment that are not
// represented by the vector-control and draw-table terms below. Keep this
// deliberately conservative: it affects admission estimates only and does
// not allocate runtime storage.
constexpr std::uint64_t kEmbeddedScalarAndAlignmentHeadroom = 2048;

bool checkedAdd(std::uint64_t left, std::uint64_t right,
        std::uint64_t& output) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    output = left + right;
    return true;
}

bool checkedMultiply(std::uint64_t left, std::uint64_t right,
        std::uint64_t& output) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    output = left * right;
    return true;
}

bool addProduct(std::uint64_t& total, std::uint64_t count,
        std::uint64_t elementBytes) noexcept {
    std::uint64_t bytes = 0;
    return checkedMultiply(count, elementBytes, bytes) && checkedAdd(total, bytes, total);
}

bool multiply(std::uint64_t value, std::uint64_t factor,
        std::uint64_t& output) noexcept {
    return checkedMultiply(value, factor, output);
}

bool uploadedRangeFitsInt(std::uint64_t elementCount,
        std::uint64_t elementBytes) noexcept {
    std::uint64_t bytes = 0;
    return checkedMultiply(elementCount, elementBytes, bytes) && bytes <= static_cast<std::uint64_t>(std::numeric_limits<int>::max());
}

bool estimateEmbeddedState(std::uint64_t pointerBytes,
        std::uint64_t vectorCount,
        std::uint64_t preparedFrameCount,
        std::uint64_t additionalInlineIntCount,
        std::uint64_t& output) noexcept {
    std::uint64_t vectorControlBytes = 0;
    std::uint64_t bunchTableInts = 0;
    std::uint64_t inlineInts = 0;
    if (!checkedMultiply(vectorCount, 3U * pointerBytes, vectorControlBytes) || !checkedMultiply(4U * kCellDataBucketCount, kMaximumDrawBunches, bunchTableInts) || !checkedMultiply(bunchTableInts, preparedFrameCount, bunchTableInts) || !checkedAdd(bunchTableInts, additionalInlineIntCount, inlineInts) || !checkedMultiply(inlineInts, sizeof(int), inlineInts) || !checkedAdd(vectorControlBytes, inlineInts, output) || !checkedAdd(output, kEmbeddedScalarAndAlignmentHeadroom, output)) {
        return false;
    }
    return true;
}

} // namespace

bool tryBuildRendererInitialBufferLayout(
        int maxCellCount, RendererInitialBufferLayout& output) noexcept {
    if (maxCellCount <= 0) {
        return false;
    }

    const std::uint64_t cells = static_cast<std::uint64_t>(maxCellCount);
    RendererInitialBufferLayout layout;
    layout.sourceOrderedCellEntriesPerBucket = cells;
    layout.cellVisitEntries = cells;
    layout.sourceRecordIndexEntries = cells;
    layout.sourceRecordGenerationEntries = cells;
    layout.rawContactChordFirstEntries = cells;
    layout.rawContactChordLastEntries = cells;
    layout.rawContactChordCountEntries = cells;
    layout.collectorBroadphaseEntries = cells;
    layout.renderContactChordGenerationEntries = cells;
    layout.renderContactChordOffsetEntries = cells;
    layout.renderContactChordCountEntries = cells;

    if (!multiply(cells,
                static_cast<std::uint64_t>(CellDataConstants::MAX_NEIGHBORS),
                layout.maximumDrawableLinkCount)) {
        return false;
    }
    // Normal edges consume two adjacency slots; boundary Links consume one.
    layout.maximumNormalEdgeCount = layout.maximumDrawableLinkCount / 2U;
    if (!checkedAdd(layout.maximumNormalEdgeCount, 63U, layout.linkCandidateWordEntries)) {
        return false;
    }
    layout.linkCandidateWordEntries /= 64U;

    std::uint64_t initialVertexUnits = 0;
    if (!multiply(cells, kInitialAverageContactCount, initialVertexUnits) || !multiply(initialVertexUnits, kCellSectorCount, initialVertexUnits) || !multiply(initialVertexUnits, kPreviewCopyCount, initialVertexUnits) || !multiply(initialVertexUnits, 2U, initialVertexUnits) || !multiply(initialVertexUnits, 2U, layout.cellTriangleFloatEntries) || !multiply(initialVertexUnits, 4U, layout.cellTextureFloatEntries) || !multiply(initialVertexUnits, 2U, layout.cellCenterFloatEntries) || !multiply(initialVertexUnits, 4U, layout.cellColorFloatEntries)) {
        return false;
    }
    std::uint64_t initialIndexUnits = 0;
    if (!multiply(cells, kInitialAverageContactCount, initialIndexUnits) || !multiply(initialIndexUnits, kCellSectorCount, initialIndexUnits) || !multiply(initialIndexUnits, kPreviewCopyCount, initialIndexUnits) || !multiply(initialIndexUnits, 3U, layout.cellIndexEntries)) {
        return false;
    }

    if (!multiply(cells, 4U, layout.foodInstanceFloatEntries)) {
        return false;
    }
    layout.foodVertexFloatEntries = layout.foodInstanceFloatEntries;

    std::uint64_t roundedDeadCells = 0;
    if (!checkedAdd(cells, 3U, roundedDeadCells)) {
        return false;
    }
    roundedDeadCells = (roundedDeadCells / 4U) * 4U;
    layout.deadInstanceFloatEntries = roundedDeadCells;
    layout.deadVertexFloatEntries = roundedDeadCells;

    std::uint64_t initialLinkUnits = 0;
    if (!multiply(cells, kInitialAverageContactCount, initialLinkUnits) || !multiply(initialLinkUnits, 3U * 4U, layout.linkTriangleFloatEntries) || !multiply(initialLinkUnits, 3U * 2U, layout.linkTextureFloatEntries)) {
        return false;
    }

    // A contact chord can contribute its first endpoint and four curve vertices.
    // The center contributes one more vertex. Preview draws the base and arrow
    // buckets; outlines are emitted only once.
    const std::uint64_t maximumBaseVerticesPerCell =
            1U + static_cast<std::uint64_t>(kMaximumRenderContactChordsPerCell) * 5U;
    std::uint64_t maximumBaseVertices = 0;
    std::uint64_t maximumOutlineVertices = 0;
    if (!multiply(cells, maximumBaseVerticesPerCell * kPreviewCopyCount,
                maximumBaseVertices)
            || !multiply(cells, kMaximumOutlineVerticesPerCell,
                    maximumOutlineVertices)
            || !checkedAdd(maximumBaseVertices, maximumOutlineVertices,
                    layout.maximumCellVertexCount)
            || !multiply(maximumBaseVertices, 3U, layout.maximumCellIndexCount)
            // Outline attributes address the shared center buffer with a
            // four-float stride, including the preceding base-geometry range.
            || !multiply(layout.maximumCellVertexCount, 4U,
                    layout.maximumCellCenterFloatEntries)
            || !multiply(cells,
                    kMaximumRenderContactChordsPerCell,
                    layout.maximumNormalizedContactChordEntries)
            || !multiply(layout.maximumDrawableLinkCount, kVerticesPerLink,
                    layout.maximumLinkVertexCount)) {
        return false;
    }
    layout.maximumRawContactChordEntries =
            layout.maximumNormalizedContactChordEntries;

    // The current upload boundary expresses sizes as int bytes. Reject a
    // capacity whose legal active ranges could overflow that boundary.
    std::uint64_t maximumCellTriangleFloats = 0;
    std::uint64_t maximumCellAttributeFloats = 0;
    std::uint64_t maximumLinkTriangleFloats = 0;
    if (!multiply(layout.maximumCellVertexCount, 2U, maximumCellTriangleFloats) || !multiply(layout.maximumCellVertexCount, 4U, maximumCellAttributeFloats) || !multiply(layout.maximumLinkVertexCount, kFloatsPerLinkVertex, maximumLinkTriangleFloats) || layout.maximumCellVertexCount > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) || layout.maximumCellIndexCount > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) || layout.maximumCellCenterFloatEntries > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) || layout.maximumNormalizedContactChordEntries > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) || layout.maximumLinkVertexCount > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) || !uploadedRangeFitsInt(maximumCellTriangleFloats, sizeof(float)) || !uploadedRangeFitsInt(maximumCellAttributeFloats, sizeof(float)) || !uploadedRangeFitsInt(layout.maximumCellCenterFloatEntries, sizeof(float)) || !uploadedRangeFitsInt(layout.maximumCellIndexCount, sizeof(unsigned short)) || !uploadedRangeFitsInt(maximumLinkTriangleFloats, sizeof(float)) || !uploadedRangeFitsInt(layout.maximumLinkVertexCount, sizeof(float))) {
        return false;
    }

    output = layout;
    return true;
}

bool tryEstimateRendererMemory(int maxCellCount, int maxFoodCount,
        unsigned int addressBits,
        RendererMemoryEstimate& output) noexcept {
    if (maxFoodCount < 0 || addressBits == 0 || addressBits > 64) {
        return false;
    }

    RendererInitialBufferLayout layout;
    if (!tryBuildRendererInitialBufferLayout(maxCellCount, layout)) {
        return false;
    }

    RendererMemoryEstimate estimate;
    const std::uint64_t pointerBytes = addressBits <= 32 ? 4U : 8U;
    const std::uint64_t bucketCount =
            static_cast<std::uint64_t>(CellTypes::length + 1);
    const std::uint64_t cells = static_cast<std::uint64_t>(maxCellCount);
    const std::uint64_t foods = static_cast<std::uint64_t>(maxFoodCount);

    // Every prepared slot owns four per-data-bucket count arrays plus active
    // bunch counts. Capture/builder scratch is single-owner even though frame
    // payload storage is tripled.
    const std::uint64_t rendererVectorCount =
            kRenderFrameExchangeSlotCount * (bucketCount + kSourceNonBucketVectorCount + kPreparedVectorCount) + kCaptureAndBuilderScratchVectorCount;
    const std::uint64_t rendererInlineInts =
            kRenderFrameExchangeSlotCount * 5U * kCellDataBucketCount;
    const std::uint64_t ghostInlineInts = 5U * kCellDataBucketCount;
    if (!estimateEmbeddedState(pointerBytes, rendererVectorCount,
                kRenderFrameExchangeSlotCount, rendererInlineInts,
                estimate.rendererEmbeddedStateBytes)
            || !estimateEmbeddedState(pointerBytes, kGhostVectorCount, 1U,
                    ghostInlineInts,
                    estimate.ghostSnapshotEmbeddedStateBytes)) {
        return false;
    }

    std::uint64_t fixedMetadataBytes = 0;
    if (!addProduct(fixedMetadataBytes, layout.cellVisitEntries, sizeof(int)) || !addProduct(fixedMetadataBytes, layout.sourceRecordIndexEntries, sizeof(int)) || !addProduct(fixedMetadataBytes, layout.sourceRecordGenerationEntries, sizeof(int)) || !addProduct(fixedMetadataBytes, layout.rawContactChordFirstEntries, sizeof(std::uint32_t)) || !addProduct(fixedMetadataBytes, layout.rawContactChordLastEntries, sizeof(std::uint32_t)) || !addProduct(fixedMetadataBytes, layout.rawContactChordCountEntries, sizeof(std::uint8_t)) || !addProduct(fixedMetadataBytes, layout.collectorBroadphaseEntries, 4U * sizeof(int) + 4U * sizeof(double)) || !addProduct(fixedMetadataBytes, layout.renderContactChordGenerationEntries, sizeof(std::uint64_t)) || !addProduct(fixedMetadataBytes, layout.renderContactChordOffsetEntries, pointerBytes) || !addProduct(fixedMetadataBytes, layout.renderContactChordCountEntries, sizeof(std::uint8_t)) || !addProduct(fixedMetadataBytes, layout.linkCandidateWordEntries, sizeof(std::uint64_t))) {
        return false;
    }

    auto addInitialReusablePayload = [&](std::uint64_t& total) {
        return addProduct(total, layout.cellTriangleFloatEntries, sizeof(float)) && addProduct(total, layout.cellTextureFloatEntries, sizeof(float)) && addProduct(total, layout.cellCenterFloatEntries, sizeof(float)) && addProduct(total, layout.cellColorFloatEntries, sizeof(float)) && addProduct(total, layout.cellIndexEntries, sizeof(unsigned short)) && addProduct(total, layout.foodInstanceFloatEntries, sizeof(float)) && addProduct(total, layout.foodVertexFloatEntries, sizeof(float)) && addProduct(total, layout.deadInstanceFloatEntries, sizeof(float)) && addProduct(total, layout.deadVertexFloatEntries, sizeof(float)) && addProduct(total, layout.linkTriangleFloatEntries, sizeof(float)) && addProduct(total, layout.linkTextureFloatEntries, sizeof(float));
    };

    std::uint64_t initialReusableBytes = 0;
    if (!addInitialReusablePayload(initialReusableBytes) || !checkedAdd(fixedMetadataBytes, initialReusableBytes, estimate.initialPayloadBytes) || !checkedAdd(estimate.initialPayloadBytes, estimate.rendererEmbeddedStateBytes, estimate.initialPayloadBytes)) {
        return false;
    }

    std::uint64_t maximumCellTriangleFloats = 0;
    std::uint64_t maximumCellAttributeFloats = 0;
    std::uint64_t maximumLinkTriangleFloats = 0;
    std::uint64_t maximumFoodFloats = 0;
    std::uint64_t maximumDeadFloats = 0;
    const std::uint64_t deadCount = (cells + 3U) / 4U;
    if (!multiply(layout.maximumCellVertexCount, 2U, maximumCellTriangleFloats) || !multiply(layout.maximumCellVertexCount, 4U, maximumCellAttributeFloats) || !multiply(layout.maximumLinkVertexCount, kFloatsPerLinkVertex, maximumLinkTriangleFloats) || !multiply(std::max(cells, foods), 4U, maximumFoodFloats) || !multiply(deadCount, 4U, maximumDeadFloats) || !uploadedRangeFitsInt(maximumFoodFloats, sizeof(float)) || !uploadedRangeFitsInt(maximumDeadFloats, sizeof(float))) {
        return false;
    }

    std::uint64_t allSourceOrderedEntries = 0;
    std::uint64_t maximumSourceVectorBytes = 0;
    if (!multiply(layout.sourceOrderedCellEntriesPerBucket, bucketCount,
                allSourceOrderedEntries)
            || !addProduct(maximumSourceVectorBytes, allSourceOrderedEntries,
                    sizeof(int))
            || !addProduct(maximumSourceVectorBytes, cells,
                    kRenderSourceCellBudgetBytes)
            || !addProduct(maximumSourceVectorBytes,
                    layout.maximumRawContactChordEntries,
                    sizeof(RenderContactChord))
            || !addProduct(maximumSourceVectorBytes,
                    layout.maximumRawContactChordEntries,
                    sizeof(CellRuntimeId))
            || !addProduct(maximumSourceVectorBytes, layout.maximumDrawableLinkCount,
                    kRenderSourceLinkBudgetBytes)
            || !addProduct(maximumSourceVectorBytes, foods,
                    kRenderSourceFoodBudgetBytes)
            || !addProduct(maximumSourceVectorBytes, deadCount,
                    kRenderSourceDeadSplashBudgetBytes)
            || !addProduct(maximumSourceVectorBytes, cells, kRenderMarkerBudgetBytes)) {
        return false;
    }

    std::uint64_t maximumPreparedVectorBytes = 0;
    if (!addProduct(maximumPreparedVectorBytes, cells,
                kRenderMarkerBudgetBytes)
            || !addProduct(maximumPreparedVectorBytes, maximumCellTriangleFloats,
                    sizeof(float))
            || !addProduct(maximumPreparedVectorBytes, maximumCellAttributeFloats,
                    sizeof(float))
            || !addProduct(maximumPreparedVectorBytes,
                    layout.maximumCellCenterFloatEntries, sizeof(float))
            || !addProduct(maximumPreparedVectorBytes, maximumCellAttributeFloats,
                    sizeof(float))
            || !addProduct(maximumPreparedVectorBytes, layout.maximumCellIndexCount,
                    sizeof(unsigned short))
            || !addProduct(maximumPreparedVectorBytes, maximumFoodFloats,
                    sizeof(float))
            || !addProduct(maximumPreparedVectorBytes, maximumFoodFloats,
                    sizeof(float))
            || !addProduct(maximumPreparedVectorBytes, maximumDeadFloats,
                    sizeof(float))
            || !addProduct(maximumPreparedVectorBytes, maximumDeadFloats,
                    sizeof(float))
            || !addProduct(maximumPreparedVectorBytes, maximumLinkTriangleFloats,
                    sizeof(float))
            || !addProduct(maximumPreparedVectorBytes, layout.maximumLinkVertexCount,
                    sizeof(float))) {
        return false;
    }

    std::uint64_t oneFrameSlotPayloadBytes = 0;
    std::uint64_t allFrameSlotPayloadBytes = 0;
    std::uint64_t builderScratchBytes = 0;
    if (!checkedAdd(maximumSourceVectorBytes, maximumPreparedVectorBytes,
                oneFrameSlotPayloadBytes)
            || !multiply(oneFrameSlotPayloadBytes, kRenderFrameExchangeSlotCount,
                    allFrameSlotPayloadBytes)
            || !addProduct(builderScratchBytes,
                    layout.maximumNormalizedContactChordEntries,
                    sizeof(RenderContactChord))
            || !addProduct(builderScratchBytes,
                    layout.maximumRawContactChordEntries,
                    kSparseRenderContactChordEntryBudgetBytes)
            || !addProduct(builderScratchBytes, foods, 2U * sizeof(int)) || !checkedAdd(fixedMetadataBytes, builderScratchBytes, estimate.maximumReusablePayloadBytes) || !checkedAdd(estimate.maximumReusablePayloadBytes, allFrameSlotPayloadBytes, estimate.maximumReusablePayloadBytes) || !checkedAdd(estimate.maximumReusablePayloadBytes, estimate.rendererEmbeddedStateBytes, estimate.maximumReusablePayloadBytes)) {
        return false;
    }

    // Ghost stores active draw ranges, not culling/cache metadata.
    if (!addProduct(estimate.maximumGhostSnapshotPayloadBytes, cells,
                kRenderMarkerBudgetBytes)
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes,
                    maximumCellTriangleFloats, sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes,
                    maximumCellAttributeFloats, sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes,
                    layout.maximumCellCenterFloatEntries, sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes,
                    maximumCellAttributeFloats, sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes,
                    layout.maximumCellIndexCount, sizeof(unsigned short))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes, maximumFoodFloats,
                    sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes, maximumFoodFloats,
                    sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes, maximumDeadFloats,
                    sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes, maximumDeadFloats,
                    sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes,
                    maximumLinkTriangleFloats, sizeof(float))
            || !addProduct(estimate.maximumGhostSnapshotPayloadBytes,
                    layout.maximumLinkVertexCount, sizeof(float))
            || !checkedAdd(estimate.maximumGhostSnapshotPayloadBytes,
                    estimate.ghostSnapshotEmbeddedStateBytes,
                    estimate.maximumGhostSnapshotPayloadBytes)) {
        return false;
    }

    std::uint64_t reusableWithGrowthHeadroom = 0;
    std::uint64_t ghostWithGrowthHeadroom = 0;
    if (!multiply(estimate.maximumReusablePayloadBytes, kCapacityGrowthHeadroom,
                reusableWithGrowthHeadroom)
            || !multiply(estimate.maximumGhostSnapshotPayloadBytes,
                    kCapacityGrowthHeadroom, ghostWithGrowthHeadroom)
            || !checkedAdd(reusableWithGrowthHeadroom, ghostWithGrowthHeadroom,
                    estimate.budgetedBytes)) {
        return false;
    }
    estimate.reusableFrameSlotCount = kRenderFrameExchangeSlotCount;
    output = estimate;
    return true;
}

} // namespace cellularity::memory
