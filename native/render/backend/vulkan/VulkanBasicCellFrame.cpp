#include "render/backend/vulkan/VulkanBasicCellFrame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "core/CellDataConstants.h"

namespace cellularity::render::vulkan {
namespace {

constexpr std::uint64_t kFloatBytes = sizeof(float);
constexpr std::uint64_t kIndexBytes = sizeof(unsigned short);
constexpr std::uint64_t kStreamAlignment = 16;
constexpr int kDrawBunchVertexCount = 3 * (32767 / 3);
// Base-cell shaders express the nucleus and membrane widths in production
// world units. Keep the deterministic fixture in that same physical scale;
// scaling only the MVP would make those fixed-size details sub-pixel.
constexpr float kDemoDishRadius = 0.24F;
constexpr float kDemoCellRadius = 0.024F;
constexpr float kDemoHorizontalSpacing = 0.0675F;
constexpr float kDemoVerticalSpacing = 0.07125F;

static_assert(kDemoCellRadius
        <= static_cast<float>(CellDataConstants::MAX_CELL_RADIUS));

constexpr std::array<VulkanBasicCellProgram, CellTypes::length>
        kProgramByCellType = {
                VulkanBasicCellProgram::Phagocyte,
                VulkanBasicCellProgram::Phagocyte,
                VulkanBasicCellProgram::Photocyte,
                VulkanBasicCellProgram::Phagocyte,
                VulkanBasicCellProgram::Lipocyte,
                VulkanBasicCellProgram::Keratinocyte,
                VulkanBasicCellProgram::Buoyocyte,
                VulkanBasicCellProgram::Phagocyte,
                VulkanBasicCellProgram::Phagocyte,
                VulkanBasicCellProgram::Phagocyte,
                VulkanBasicCellProgram::Neurocyte,
                VulkanBasicCellProgram::Neurocyte,
                VulkanBasicCellProgram::Myocyte,
                VulkanBasicCellProgram::Neurocyte,
                VulkanBasicCellProgram::Secrocyte,
                VulkanBasicCellProgram::Stemocyte,
                VulkanBasicCellProgram::Gamete,
                VulkanBasicCellProgram::Phagocyte,
};

constexpr std::array<const char*, kVulkanBasicCellProgramCount>
        kProgramNames = {
                "buoyocyte",
                "gamete",
                "keratinocyte",
                "lipocyte",
                "myocyte",
                "neurocyte",
                "phagocyte",
                "photocyte",
                "secrocyte",
                "stemocyte",
};

constexpr std::array<const char*, kVulkanWorldProgramCount>
        kWorldProgramNames = {
                "arrow",
                "ciliocyte_outline",
                "dead",
                "devorocyte_outline",
                "flagellocyte_outline",
                "food",
                "glueocyte_outline",
                "link",
                "marker",
                "nitrocyte_outline",
                "pixel_text",
                "senseocyte_outline",
                "stereocyte_outline",
                "virocyte_outline",
                "genome_grid",
                "genome_tree",
};

constexpr std::array<std::array<float, 3>, CellTypes::length> kDemoColors = {{
        {0.82F, 0.34F, 0.28F},
        {0.92F, 0.50F, 0.24F},
        {0.36F, 0.76F, 0.30F},
        {0.72F, 0.24F, 0.36F},
        {0.65F, 0.48F, 0.30F},
        {0.70F, 0.62F, 0.42F},
        {0.34F, 0.68F, 0.86F},
        {0.66F, 0.72F, 0.80F},
        {0.58F, 0.34F, 0.76F},
        {0.72F, 0.62F, 0.24F},
        {0.34F, 0.58F, 0.88F},
        {0.26F, 0.72F, 0.72F},
        {0.82F, 0.30F, 0.46F},
        {0.46F, 0.40F, 0.88F},
        {0.28F, 0.76F, 0.58F},
        {0.78F, 0.38F, 0.68F},
        {0.86F, 0.68F, 0.30F},
        {0.34F, 0.82F, 0.62F},
}};

constexpr std::array<float, CellTypes::length> kOutlineRadiusByCellType = {
        0.0F,
        4.0F * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS),
        0.0F,
        0.3F * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS),
        0.0F,
        0.0F,
        0.0F,
        0.4F * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS),
        0.3F * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS),
        0.45F * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS),
        0.4F * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS),
        0.2F * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS),
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        0.15F * static_cast<float>(CellDataConstants::MAX_CELL_RADIUS),
};

bool checkedMultiply(std::uint64_t left, std::uint64_t right,
        std::uint64_t& result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

bool alignUp(std::uint64_t value, std::uint64_t alignment,
        std::uint64_t& result) noexcept {
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0) {
        result = value;
        return true;
    }
    const std::uint64_t increment = alignment - remainder;
    if (value > std::numeric_limits<std::uint64_t>::max() - increment) {
        return false;
    }
    result = value + increment;
    return true;
}

bool appendRange(std::uint64_t size, std::uint64_t alignment,
        std::uint64_t& cursor, VulkanBasicCellStreamRange& range) noexcept {
    if (!alignUp(cursor, alignment, range.offset)
            || range.offset > std::numeric_limits<std::uint64_t>::max() - size) {
        return false;
    }
    range.size = size;
    cursor = range.offset + size;
    return true;
}

bool checkedVectorRange(std::uint64_t elementCount, std::uint64_t elementBytes,
        std::size_t availableElements, std::uint64_t& sizeBytes) noexcept {
    return elementCount <= availableElements
            && checkedMultiply(elementCount, elementBytes, sizeBytes);
}

std::array<float, 16> demoMvp(int surfaceWidth, int surfaceHeight) {
    const float width = static_cast<float>(std::max(surfaceWidth, 1));
    const float height = static_cast<float>(std::max(surfaceHeight, 1));
    const float minimumSide = std::min(width, height);
    return {
            minimumSide / (kDemoDishRadius * width),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            minimumSide / (kDemoDishRadius * height),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            1.0F,
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            1.0F,
    };
}

} // namespace

VulkanPreparedFrameUploadIdentity VulkanPreparedFrameUploadIdentity::from(
        const PreparedRenderFrameView& frame) noexcept {
    return {
            frame.sourceIdentity,
            frame.layout.version,
            frame.identity.worldEpoch,
            frame.identity.modeGeneration,
            frame.identity.captureSequence,
            frame.layout.geometryRevision,
            frame.layout.instanceRevision,
            frame.layout.markerRevision,
    };
}

const char* vulkanBasicCellProgramName(
        VulkanBasicCellProgram program) noexcept {
    const std::size_t index = static_cast<std::size_t>(program);
    return index < kProgramNames.size() ? kProgramNames[index] : "unknown";
}

VulkanBasicCellProgram vulkanBasicCellProgramForType(int cellTypeIndex) {
    if (cellTypeIndex < 0 || cellTypeIndex >= CellTypes::length) {
        throw std::out_of_range("Vulkan basic-cell type index is out of range");
    }
    return kProgramByCellType[static_cast<std::size_t>(cellTypeIndex)];
}

const char* vulkanWorldProgramName(VulkanWorldProgram program) noexcept {
    const std::size_t index = static_cast<std::size_t>(program);
    return index < kWorldProgramNames.size() ? kWorldProgramNames[index]
                                             : "unknown";
}

VulkanWorldProgram vulkanOutlineProgramForType(int cellTypeIndex) {
    if (cellTypeIndex < 0 || cellTypeIndex >= CellTypes::length) {
        throw std::out_of_range("Vulkan outline type index is out of range");
    }
    switch (CellTypes::fromOrdinal(cellTypeIndex)) {
        case CellType::FLAGELLOCYTE:
            return VulkanWorldProgram::FlagellocyteOutline;
        case CellType::DEVOROCYTE:
            return VulkanWorldProgram::DevorocyteOutline;
        case CellType::GLUEOCYTE:
            return VulkanWorldProgram::GlueocyteOutline;
        case CellType::VIROCYTE:
            return VulkanWorldProgram::VirocyteOutline;
        case CellType::NITROCYTE:
            return VulkanWorldProgram::NitrocyteOutline;
        case CellType::STEREOCYTE:
            return VulkanWorldProgram::StereocyteOutline;
        case CellType::SENSEOCYTE:
            return VulkanWorldProgram::SenseocyteOutline;
        case CellType::CILIOCYTE:
            return VulkanWorldProgram::CiliocyteOutline;
        default:
            throw std::invalid_argument(
                    "Cell type does not have an outline shader");
    }
}

bool buildVulkanBasicCellUploadPlan(const PreparedRenderFrameView& frame,
        VulkanBasicCellUploadPlan& output, std::string& error) {
    output = {};
    error.clear();
    if (!frame.valid || !frame.geometryReady) {
        error = "PreparedRenderFrame is not published and geometry-ready";
        return false;
    }
    if (frame.layout.version != kPreparedRenderFrameLayoutVersion) {
        error = "PreparedRenderFrame layout version is unsupported";
        return false;
    }
    if (frame.cellDrawVertexCount < 0 || frame.cellDrawIndexCount < 0
            || frame.cellCenterFloatCount < 0
            || frame.foodInstanceCount < 0
            || frame.deadCellInstanceCount < 0
            || frame.drawLinksCount < 0) {
        error = "PreparedRenderFrame contains a negative world range";
        return false;
    }

    const auto vertexCount = static_cast<std::uint64_t>(
            frame.cellDrawVertexCount);
    const auto indexCount = static_cast<std::uint64_t>(
            frame.cellDrawIndexCount);
    const auto centerFloatCount = static_cast<std::uint64_t>(
            frame.cellCenterFloatCount);
    std::uint64_t positionFloatCount = 0;
    std::uint64_t attributeFloatCount = 0;
    if (!checkedMultiply(vertexCount, 2, positionFloatCount)
            || !checkedMultiply(vertexCount, 4, attributeFloatCount)) {
        error = "PreparedRenderFrame basic-cell element count overflows";
        return false;
    }

    std::uint64_t positionBytes = 0;
    std::uint64_t textureBytes = 0;
    std::uint64_t centerBytes = 0;
    std::uint64_t colorBytes = 0;
    std::uint64_t indexBytes = 0;
    std::uint64_t foodBytes = 0;
    std::uint64_t deadBytes = 0;
    std::uint64_t linkVertexBytes = 0;
    std::uint64_t linkPatternBytes = 0;
    std::uint64_t markerBytes = 0;
    std::uint64_t foodFloatCount = 0;
    std::uint64_t deadFloatCount = 0;
    std::uint64_t linkVertexFloatCount = 0;
    std::uint64_t markerFloatCount = 0;
    const auto foodInstanceCount =
            static_cast<std::uint64_t>(frame.foodInstanceCount);
    const auto deadInstanceCount =
            static_cast<std::uint64_t>(frame.deadCellInstanceCount);
    const auto linkVertexCount =
            static_cast<std::uint64_t>(frame.drawLinksCount);
    if (!checkedMultiply(foodInstanceCount, 4, foodFloatCount)
            || !checkedMultiply(deadInstanceCount, 4, deadFloatCount)
            || !checkedMultiply(linkVertexCount, 4,
                    linkVertexFloatCount)
            || !checkedMultiply(frame.markers.size(), 4,
                    markerFloatCount)
            || !checkedMultiply(markerFloatCount, kFloatBytes,
                    markerBytes)) {
        error = "PreparedRenderFrame world element count overflows";
        return false;
    }
    if (!checkedVectorRange(positionFloatCount, kFloatBytes,
                frame.cellTriangleCoords.size(), positionBytes)
            || !checkedVectorRange(attributeFloatCount, kFloatBytes,
                    frame.cellTextureCoords.size(), textureBytes)
            || !checkedVectorRange(centerFloatCount, kFloatBytes,
                    frame.cellCenterCoords.size(), centerBytes)
            || centerFloatCount < positionFloatCount
            || !checkedVectorRange(attributeFloatCount, kFloatBytes,
                    frame.cellColors.size(), colorBytes)
            || !checkedVectorRange(indexCount, kIndexBytes,
                    frame.cellIndices.size(), indexBytes)
            || !checkedVectorRange(foodFloatCount, kFloatBytes,
                    frame.foodInstanceAttributes.size(), foodBytes)
            || !checkedVectorRange(foodFloatCount, kFloatBytes,
                    frame.foodVertexAttributes.size(), foodBytes)
            || !checkedVectorRange(deadFloatCount, kFloatBytes,
                    frame.deadCellInstanceAttributes.size(), deadBytes)
            || !checkedVectorRange(deadFloatCount, kFloatBytes,
                    frame.deadCellVertexAttributes.size(), deadBytes)
            || !checkedVectorRange(linkVertexFloatCount, kFloatBytes,
                    frame.linkTriangleCoords.size(), linkVertexBytes)
            || !checkedVectorRange(linkVertexCount, kFloatBytes,
                    frame.linkTextureCoords.size(), linkPatternBytes)) {
        error = "PreparedRenderFrame world range exceeds owned storage";
        return false;
    }

    std::uint64_t cursor = 0;
    if (!appendRange(positionBytes, kStreamAlignment, cursor, output.positions)
            || !appendRange(textureBytes, kStreamAlignment, cursor,
                    output.textureCoordinates)
            || !appendRange(centerBytes, kStreamAlignment, cursor,
                    output.centers)
            || !appendRange(colorBytes, kStreamAlignment, cursor,
                    output.colors)
            || !appendRange(indexBytes, kIndexBytes, cursor, output.indices)
            || !appendRange(foodBytes, kStreamAlignment, cursor,
                    output.foodInstances)
            || !appendRange(foodBytes, kStreamAlignment, cursor,
                    output.foodAttributes)
            || !appendRange(deadBytes, kStreamAlignment, cursor,
                    output.deadInstances)
            || !appendRange(deadBytes, kStreamAlignment, cursor,
                    output.deadAttributes)
            || !appendRange(linkVertexBytes, kStreamAlignment, cursor,
                    output.linkVertices)
            || !appendRange(linkPatternBytes, kStreamAlignment, cursor,
                    output.linkPatternPositions)
            || !appendRange(markerBytes, kStreamAlignment, cursor,
                    output.markerInstances)
            || !alignUp(cursor, kStreamAlignment, output.requiredBytes)) {
        error = "PreparedRenderFrame Vulkan world upload layout overflows";
        return false;
    }

    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length;
            ++cellTypeIndex) {
        const int bucketVertexStart =
                frame.cellVertexCountBefore[cellTypeIndex];
        const int bucketVertexCount = frame.cellVertexCount[cellTypeIndex];
        const int bucketIndexStart =
                frame.cellIndexCountBefore[cellTypeIndex];
        const int bucketIndexCount = frame.cellIndexCount[cellTypeIndex];
        const int bunchCount = frame.bunches[cellTypeIndex];
        if (bucketVertexStart < 0 || bucketVertexCount < 0
                || bucketIndexStart < 0 || bucketIndexCount < 0
                || bucketVertexStart > frame.cellDrawVertexCount
                || bucketVertexCount
                        > frame.cellDrawVertexCount - bucketVertexStart
                || bucketIndexStart > frame.cellDrawIndexCount
                || bucketIndexCount
                        > frame.cellDrawIndexCount - bucketIndexStart
                || bunchCount < 0
                || bunchCount > kMaximumDrawBunches) {
            error = "PreparedRenderFrame basic-cell bucket metadata is invalid";
            return false;
        }
        if (bucketIndexCount == 0) {
            if (bucketVertexCount != 0) {
                error = "PreparedRenderFrame basic-cell bucket has vertices without indices";
                return false;
            }
            continue;
        }
        if (bucketVertexCount == 0 || bunchCount == 0) {
            error = "PreparedRenderFrame indexed bucket has no drawable geometry";
            return false;
        }

        int expectedVertexStart = bucketVertexStart;
        int expectedIndexStart = bucketIndexStart;
        for (int bunch = 0; bunch < bunchCount; ++bunch) {
            const int vertexStart = frame.bunchStart[cellTypeIndex][bunch];
            const int bunchVertexCount =
                    frame.bunchSize[cellTypeIndex][bunch];
            const int firstIndex = frame.bunchIndexStart[cellTypeIndex][bunch];
            const int bunchIndexCount =
                    frame.bunchIndexSize[cellTypeIndex][bunch];
            if (vertexStart != expectedVertexStart
                    || firstIndex != expectedIndexStart
                    || bunchVertexCount <= 0 || bunchIndexCount <= 0
                    || bunchVertexCount
                            > bucketVertexStart + bucketVertexCount - vertexStart
                    || bunchIndexCount
                            > bucketIndexStart + bucketIndexCount - firstIndex) {
                error = "PreparedRenderFrame draw bunch exceeds active ranges";
                return false;
            }
            for (int index = 0; index < bunchIndexCount; ++index) {
                const auto localVertex = frame.cellIndices[static_cast<std::size_t>(firstIndex + index)];
                if (localVertex
                        >= static_cast<unsigned int>(bunchVertexCount)) {
                    error = "PreparedRenderFrame draw bunch contains an invalid local index";
                    return false;
                }
            }
            if (output.drawCount >= output.draws.size()) {
                error = "PreparedRenderFrame contains too many draw bunches";
                return false;
            }
            output.draws[output.drawCount++] = {
                    vulkanBasicCellProgramForType(cellTypeIndex),
                    static_cast<std::uint32_t>(cellTypeIndex),
                    static_cast<std::uint32_t>(firstIndex),
                    static_cast<std::uint32_t>(bunchIndexCount),
                    vertexStart,
            };
            expectedVertexStart += bunchVertexCount;
            expectedIndexStart += bunchIndexCount;
        }
        if (expectedVertexStart != bucketVertexStart + bucketVertexCount
                || expectedIndexStart
                        != bucketIndexStart + bucketIndexCount) {
            error = "PreparedRenderFrame draw bunches do not exactly cover their bucket";
            return false;
        }
    }

    if (frame.environment.previewMode) {
        const int arrowBucket = kPreviewExtraCellTypeIndex;
        const int arrowVertexStart = frame.cellVertexCountBefore[arrowBucket];
        const int arrowVertexCount = frame.cellVertexCount[arrowBucket];
        const int arrowIndexStart = frame.cellIndexCountBefore[arrowBucket];
        const int arrowIndexCount = frame.cellIndexCount[arrowBucket];
        const int arrowBunchCount = frame.bunches[arrowBucket];
        if (arrowVertexStart < 0 || arrowVertexCount < 0
                || arrowIndexStart < 0 || arrowIndexCount < 0
                || arrowVertexStart > frame.cellDrawVertexCount
                || arrowVertexCount
                        > frame.cellDrawVertexCount - arrowVertexStart
                || arrowIndexStart > frame.cellDrawIndexCount
                || arrowIndexCount
                        > frame.cellDrawIndexCount - arrowIndexStart
                || arrowBunchCount < 0
                || arrowBunchCount > kMaximumDrawBunches) {
            error = "PreparedRenderFrame arrow bucket metadata is invalid";
            return false;
        }
        if ((arrowIndexCount == 0) != (arrowVertexCount == 0)
                || (arrowIndexCount > 0 && arrowBunchCount == 0)) {
            error = "PreparedRenderFrame arrow bucket is not drawable";
            return false;
        }
        // The shared mesh builder retains one empty metadata bunch for an
        // empty visited bucket. Match the base-cell contract: without active
        // vertex/index ranges there is no draw to validate or submit.
        if (arrowIndexCount > 0) {
            int expectedArrowVertexStart = arrowVertexStart;
            int expectedArrowIndexStart = arrowIndexStart;
            for (int bunch = 0; bunch < arrowBunchCount; ++bunch) {
                const int vertexStart = frame.bunchStart[arrowBucket][bunch];
                const int bunchVertexCount = frame.bunchSize[arrowBucket][bunch];
                const int firstIndex = frame.bunchIndexStart[arrowBucket][bunch];
                const int bunchIndexCount =
                        frame.bunchIndexSize[arrowBucket][bunch];
                if (vertexStart != expectedArrowVertexStart
                        || firstIndex != expectedArrowIndexStart
                        || bunchVertexCount <= 0 || bunchIndexCount <= 0
                        || bunchVertexCount
                                > arrowVertexStart + arrowVertexCount - vertexStart
                        || bunchIndexCount
                                > arrowIndexStart + arrowIndexCount - firstIndex) {
                    error = "PreparedRenderFrame arrow bunch exceeds active ranges";
                    return false;
                }
                for (int index = 0; index < bunchIndexCount; ++index) {
                    const auto localVertex =
                            frame.cellIndices[static_cast<std::size_t>(
                                    firstIndex + index)];
                    if (localVertex
                            >= static_cast<unsigned int>(bunchVertexCount)) {
                        error = "PreparedRenderFrame arrow bunch contains an invalid local index";
                        return false;
                    }
                }
                output.arrowDraws[output.arrowDrawCount++] = {
                        static_cast<std::uint32_t>(firstIndex),
                        static_cast<std::uint32_t>(bunchIndexCount),
                        vertexStart,
                };
                expectedArrowVertexStart += bunchVertexCount;
                expectedArrowIndexStart += bunchIndexCount;
            }
            if (expectedArrowVertexStart != arrowVertexStart + arrowVertexCount
                    || expectedArrowIndexStart
                            != arrowIndexStart + arrowIndexCount) {
                error = "PreparedRenderFrame arrow bunches do not cover their bucket";
                return false;
            }
        }
    }

    const int outlineShift = CellTypes::length
            + (frame.environment.previewMode ? 1 : 0);
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length;
            ++cellTypeIndex) {
        const int bucket = outlineShift + cellTypeIndex;
        const int firstVertex = frame.cellVertexCountBefore[bucket];
        const int vertexCountForType = frame.cellVertexCount[bucket];
        const bool hasOutline = kOutlineRadiusByCellType[static_cast<std::size_t>(cellTypeIndex)]
                > 0.0F;
        if (firstVertex < 0 || vertexCountForType < 0
                || firstVertex > frame.cellDrawVertexCount
                || vertexCountForType
                        > frame.cellDrawVertexCount - firstVertex) {
            error = "PreparedRenderFrame outline bucket metadata is invalid";
            return false;
        }
        if (!hasOutline) {
            if (vertexCountForType != 0) {
                error = "PreparedRenderFrame has geometry for a missing outline shader";
                return false;
            }
            continue;
        }
        if (vertexCountForType == 0) {
            continue;
        }
        if (static_cast<std::uint64_t>(firstVertex + vertexCountForType)
                        * 4U
                > centerFloatCount) {
            error = "PreparedRenderFrame outline center range is invalid";
            return false;
        }
        int consumed = 0;
        while (consumed < vertexCountForType) {
            if (output.outlineDrawCount >= output.outlineDraws.size()) {
                error = "PreparedRenderFrame contains too many outline chunks";
                return false;
            }
            const int count = std::min(
                    vertexCountForType - consumed, kDrawBunchVertexCount);
            output.outlineDraws[output.outlineDrawCount++] = {
                    vulkanOutlineProgramForType(cellTypeIndex),
                    static_cast<std::uint32_t>(cellTypeIndex),
                    static_cast<std::uint32_t>(firstVertex + consumed),
                    static_cast<std::uint32_t>(count),
            };
            consumed += count;
        }
    }

    int linkFirstVertex = 0;
    while (linkFirstVertex < frame.drawLinksCount) {
        if (output.linkDrawCount >= output.linkDraws.size()) {
            error = "PreparedRenderFrame contains too many link chunks";
            return false;
        }
        const int count = std::min(
                frame.drawLinksCount - linkFirstVertex,
                kDrawBunchVertexCount);
        output.linkDraws[output.linkDrawCount++] = {
                static_cast<std::uint32_t>(linkFirstVertex),
                static_cast<std::uint32_t>(count),
        };
        linkFirstVertex += count;
    }

    if (foodInstanceCount > std::numeric_limits<std::uint32_t>::max()
            || deadInstanceCount > std::numeric_limits<std::uint32_t>::max()
            || frame.markers.size()
                    > std::numeric_limits<std::uint32_t>::max()) {
        error = "PreparedRenderFrame instance count exceeds Vulkan limits";
        return false;
    }
    output.foodInstanceCount =
            static_cast<std::uint32_t>(foodInstanceCount);
    output.deadInstanceCount =
            static_cast<std::uint32_t>(deadInstanceCount);
    output.markerInstanceCount =
            static_cast<std::uint32_t>(frame.markers.size());
    output.previewMode = frame.environment.previewMode;

    output.identity = VulkanPreparedFrameUploadIdentity::from(frame);
    return true;
}

PreparedRenderFrame makeVulkanBasicCellDemoFrame() {
    PreparedRenderFrame frame;
    frame.identity.captureSequence = 1;
    frame.identity.worldEpoch = 1;
    frame.identity.modeGeneration = 1;
    frame.identity.completedUpdateId = 1;
    frame.identity.captureStage = RenderCaptureStage::StableWorld;
    frame.environment.revision = 1;
    frame.environment.lightAngle = 0.0;
    frame.environment.lightRange = 0.55;
    frame.environment.lightIntensity = 0.35;
    frame.environment.dishRadius = kDemoDishRadius;
    frame.environment.backgroundColor = {0.73F, 0.70F, 0.95F};
    frame.layout.version = kPreparedRenderFrameLayoutVersion;
    frame.layout.geometryRevision = 1;
    frame.layout.instanceRevision = 1;
    frame.layout.markerRevision = 1;

    constexpr int kVerticesPerCell = 4;
    constexpr int kIndicesPerCell = 6;
    constexpr int kColumns = 6;
    frame.cellTriangleCoords.reserve(
            CellTypes::length * kVerticesPerCell * 2);
    frame.cellTextureCoords.reserve(
            CellTypes::length * kVerticesPerCell * 4);
    frame.cellCenterCoords.reserve(
            CellTypes::length * kVerticesPerCell * 2);
    frame.cellColors.reserve(CellTypes::length * kVerticesPerCell * 4);
    frame.cellIndices.reserve(CellTypes::length * kIndicesPerCell);

    constexpr std::array<std::array<float, 2>, kVerticesPerCell> kCorners = {{
            {kDemoCellRadius, kDemoCellRadius},
            {kDemoCellRadius, -kDemoCellRadius},
            {-kDemoCellRadius, -kDemoCellRadius},
            {-kDemoCellRadius, kDemoCellRadius},
    }};
    constexpr std::array<unsigned short, kIndicesPerCell> kQuadIndices = {
            0,
            1,
            2,
            0,
            2,
            3,
    };

    int vertexCursor = 0;
    int indexCursor = 0;
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length;
            ++cellTypeIndex) {
        const int column = cellTypeIndex % kColumns;
        const int row = cellTypeIndex / kColumns;
        const float centerX =
                (static_cast<float>(column) - 2.5F)
                * kDemoHorizontalSpacing;
        const float centerY =
                (1.0F - static_cast<float>(row)) * kDemoVerticalSpacing;
        const float angle = 0.31F * static_cast<float>(cellTypeIndex);
        float aux = 0.35F;
        float aux4 = angle + 1.1F;
        if (cellTypeIndex == CellTypes::ordinal(CellType::LIPOCYTE)) {
            aux = 0.78F;
        } else if (cellTypeIndex
                == CellTypes::ordinal(CellType::MYOCYTE)) {
            aux = 0.55F;
            aux4 = 0.30F;
        } else if (cellTypeIndex
                        == CellTypes::ordinal(CellType::NEUROCYTE)
                || cellTypeIndex
                        == CellTypes::ordinal(CellType::STEREOCYTE)
                || cellTypeIndex
                        == CellTypes::ordinal(CellType::SENSEOCYTE)) {
            aux = 0.82F;
            aux4 = -0.68F;
        } else if (cellTypeIndex
                == CellTypes::ordinal(CellType::SECROCYTE)) {
            aux = 4.0F;
        } else if (cellTypeIndex
                == CellTypes::ordinal(CellType::STEMOCYTE)) {
            aux = 0.38F;
            aux4 = 0.46F;
        }

        frame.cellVertexCountBefore[cellTypeIndex] = vertexCursor;
        frame.cellVertexCount[cellTypeIndex] = kVerticesPerCell;
        frame.cellIndexCountBefore[cellTypeIndex] = indexCursor;
        frame.cellIndexCount[cellTypeIndex] = kIndicesPerCell;
        frame.bunches[cellTypeIndex] = 1;
        frame.bunchStart[cellTypeIndex][0] = vertexCursor;
        frame.bunchSize[cellTypeIndex][0] = kVerticesPerCell;
        frame.bunchIndexStart[cellTypeIndex][0] = indexCursor;
        frame.bunchIndexSize[cellTypeIndex][0] = kIndicesPerCell;

        for (const auto& corner : kCorners) {
            frame.cellTriangleCoords.push_back(corner[0]);
            frame.cellTriangleCoords.push_back(corner[1]);
            frame.cellTextureCoords.push_back(aux);
            frame.cellTextureCoords.push_back(0.0F);
            frame.cellTextureCoords.push_back(angle);
            frame.cellTextureCoords.push_back(aux4);
            frame.cellCenterCoords.push_back(centerX);
            frame.cellCenterCoords.push_back(centerY);
            frame.cellColors.push_back(kDemoColors[cellTypeIndex][0]);
            frame.cellColors.push_back(kDemoColors[cellTypeIndex][1]);
            frame.cellColors.push_back(kDemoColors[cellTypeIndex][2]);
            frame.cellColors.push_back(kDemoCellRadius);
        }
        frame.cellIndices.insert(frame.cellIndices.end(),
                kQuadIndices.begin(), kQuadIndices.end());
        vertexCursor += kVerticesPerCell;
        indexCursor += kIndicesPerCell;
    }

    frame.environment.previewMode = true;
    const int arrowBucket = kPreviewExtraCellTypeIndex;
    frame.cellVertexCountBefore[arrowBucket] = vertexCursor;
    frame.cellVertexCount[arrowBucket] = kVerticesPerCell;
    frame.cellIndexCountBefore[arrowBucket] = indexCursor;
    frame.cellIndexCount[arrowBucket] = kIndicesPerCell;
    frame.bunches[arrowBucket] = 1;
    frame.bunchStart[arrowBucket][0] = vertexCursor;
    frame.bunchSize[arrowBucket][0] = kVerticesPerCell;
    frame.bunchIndexStart[arrowBucket][0] = indexCursor;
    frame.bunchIndexSize[arrowBucket][0] = kIndicesPerCell;
    constexpr float kArrowRadius = 0.032F;
    for (const auto& corner : kCorners) {
        frame.cellTriangleCoords.push_back(
                corner[0] * (kArrowRadius / kDemoCellRadius));
        frame.cellTriangleCoords.push_back(
                corner[1] * (kArrowRadius / kDemoCellRadius));
        frame.cellTextureCoords.push_back(0.35F);
        frame.cellTextureCoords.push_back(0.0F);
        frame.cellTextureCoords.push_back(0.0F);
        frame.cellTextureCoords.push_back(0.0F);
        frame.cellCenterCoords.push_back(0.16F);
        frame.cellCenterCoords.push_back(-0.16F);
        frame.cellColors.push_back(0.25F);
        frame.cellColors.push_back(0.75F);
        frame.cellColors.push_back(0.95F);
        frame.cellColors.push_back(kArrowRadius);
    }
    frame.cellIndices.insert(frame.cellIndices.end(),
            kQuadIndices.begin(), kQuadIndices.end());
    vertexCursor += kVerticesPerCell;
    indexCursor += kIndicesPerCell;

    const int outlineShift = CellTypes::length + 1;
    int centerFloatCount = vertexCursor * 2;
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length;
            ++cellTypeIndex) {
        const float outlineRadius = kOutlineRadiusByCellType[static_cast<std::size_t>(cellTypeIndex)];
        const int bucket = outlineShift + cellTypeIndex;
        frame.cellVertexCountBefore[bucket] = vertexCursor;
        if (outlineRadius <= 0.0F) {
            continue;
        }

        const int column = cellTypeIndex % kColumns;
        const int row = cellTypeIndex / kColumns;
        const float centerX =
                (static_cast<float>(column) - 2.5F)
                * kDemoHorizontalSpacing;
        const float centerY =
                (1.0F - static_cast<float>(row)) * kDemoVerticalSpacing;
        const float angle = 0.31F * static_cast<float>(cellTypeIndex);
        const bool flagellocyte = CellTypes::fromOrdinal(cellTypeIndex)
                == CellType::FLAGELLOCYTE;
        const int outlineVertexCount = flagellocyte ? 3 : 6;
        std::array<std::array<float, 4>, 6> vertices{};
        if (flagellocyte) {
            const float maximumRadius = static_cast<float>(
                    CellDataConstants::MAX_CELL_RADIUS);
            const float textureX = maximumRadius / outlineRadius;
            const float textureY = std::sqrt(
                    std::max(0.0F, 1.0F - textureX * textureX));
            const float cellSin = std::sin(angle) * kDemoCellRadius;
            const float cellCos = std::cos(angle) * kDemoCellRadius;
            vertices[0] = {
                    -cellSin * textureY - cellCos * textureX,
                    cellCos * textureY - cellSin * textureX,
                    textureX,
                    textureY,
            };
            vertices[1] = {
                    cellSin * textureY - cellCos * textureX,
                    -cellCos * textureY - cellSin * textureX,
                    textureX,
                    -textureY,
            };
            vertices[2] = {
                    -outlineRadius * cellCos / maximumRadius,
                    -outlineRadius * cellSin / maximumRadius,
                    outlineRadius / maximumRadius,
                    0.0F,
            };
        } else {
            const float extent = kDemoCellRadius + outlineRadius;
            const float cellCos = std::cos(angle) * extent;
            const float cellSin = std::sin(angle) * extent;
            vertices = {{
                    {-cellCos - cellSin, -cellSin + cellCos, -1.0F, 1.0F},
                    {cellCos + cellSin, cellSin - cellCos, 1.0F, -1.0F},
                    {cellCos - cellSin, cellSin + cellCos, 1.0F, 1.0F},
                    {-cellCos - cellSin, -cellSin + cellCos, -1.0F, 1.0F},
                    {-cellCos + cellSin, -cellSin - cellCos, -1.0F, -1.0F},
                    {cellCos + cellSin, cellSin - cellCos, 1.0F, -1.0F},
            }};
        }
        frame.cellVertexCount[bucket] = outlineVertexCount;
        const std::size_t requiredCenterFloats =
                static_cast<std::size_t>(vertexCursor + outlineVertexCount)
                * 4U;
        frame.cellCenterCoords.resize(
                std::max(frame.cellCenterCoords.size(), requiredCenterFloats),
                0.0F);
        for (int vertex = 0; vertex < outlineVertexCount; ++vertex) {
            const auto& outlineVertex = vertices[static_cast<std::size_t>(
                    vertex)];
            frame.cellTriangleCoords.push_back(outlineVertex[0]);
            frame.cellTriangleCoords.push_back(outlineVertex[1]);
            frame.cellTextureCoords.push_back(outlineVertex[2]);
            frame.cellTextureCoords.push_back(outlineVertex[3]);
            frame.cellTextureCoords.push_back(0.25F);
            frame.cellTextureCoords.push_back(0.0F);
            frame.cellColors.push_back(kDemoColors[cellTypeIndex][0]);
            frame.cellColors.push_back(kDemoColors[cellTypeIndex][1]);
            frame.cellColors.push_back(kDemoColors[cellTypeIndex][2]);
            frame.cellColors.push_back(kDemoCellRadius);
            const std::size_t centerOffset =
                    static_cast<std::size_t>(vertexCursor + vertex) * 4U;
            frame.cellCenterCoords[centerOffset] = centerX;
            frame.cellCenterCoords[centerOffset + 1U] = centerY;
            frame.cellCenterCoords[centerOffset + 2U] = 0.0F;
            frame.cellCenterCoords[centerOffset + 3U] = angle;
        }
        vertexCursor += outlineVertexCount;
        centerFloatCount = std::max(centerFloatCount, vertexCursor * 4);
    }

    frame.markers = {
            {-2.5F * kDemoHorizontalSpacing, -kDemoVerticalSpacing,
                    kDemoCellRadius + 0.01F, 1.0F},
            {2.5F * kDemoHorizontalSpacing, -kDemoVerticalSpacing,
                    kDemoCellRadius + 0.01F, 0.3F},
    };

    frame.foodInstanceAttributes = {
            -0.16F,
            -0.16F,
            0.014F,
            0.0F,
            -0.12F,
            -0.18F,
            0.010F,
            0.0F,
            -0.08F,
            -0.15F,
            0.008F,
            0.0F,
    };
    frame.foodVertexAttributes = {
            0.30F,
            0.68F,
            0.20F,
            0.92F,
            0.74F,
            0.42F,
            0.18F,
            0.88F,
            0.92F,
            0.70F,
            0.24F,
            0.84F,
    };
    frame.foodInstanceCount = 3;

    frame.deadCellInstanceAttributes = {
            0.10F,
            -0.17F,
            0.035F,
            0.75F,
    };
    frame.deadCellVertexAttributes = {
            0.55F,
            0.20F,
            0.18F,
            kDemoCellRadius,
    };
    frame.deadCellInstanceCount = 1;

    frame.linkTriangleCoords = {
            -0.105F,
            0.035F,
            0.0F,
            1.0F,
            -0.045F,
            0.028F,
            -1.0F,
            0.0F,
            -0.045F,
            0.042F,
            1.0F,
            0.0F,
            -0.045F,
            0.028F,
            -1.0F,
            0.0F,
            0.015F,
            0.035F,
            0.0F,
            1.0F,
            -0.045F,
            0.042F,
            1.0F,
            0.0F,
    };
    frame.linkTextureCoords = {0.0F, -1.0F, 1.0F, -1.0F, 0.0F, 1.0F};
    frame.drawLinksCount = 6;

    frame.cellDrawVertexCount = vertexCursor;
    frame.cellDrawIndexCount = indexCursor;
    frame.cellCenterFloatCount = centerFloatCount;
    frame.viewport = makeVulkanBasicCellDemoPresentation(1, 1).viewport;
    frame.geometryReady = true;
    frame.valid = true;
    return frame;
}

RenderPresentationState makeVulkanBasicCellDemoPresentation(
        int surfaceWidth, int surfaceHeight) {
    RenderPresentationState presentation;
    presentation.viewport.revision = 1;
    presentation.viewport.coverageRevision = 1;
    presentation.viewport.surfaceWidth = std::max(surfaceWidth, 0);
    presentation.viewport.surfaceHeight = std::max(surfaceHeight, 0);
    presentation.viewport.viewWidth = static_cast<float>(
            std::max(surfaceWidth, 0));
    presentation.viewport.viewHeight = static_cast<float>(
            std::max(surfaceHeight, 0));
    presentation.viewport.zoom = 1.0F / kDemoDishRadius;
    presentation.viewport.mvpMatrix = demoMvp(surfaceWidth, surfaceHeight);
    presentation.backgroundColor = {0.73F, 0.70F, 0.95F};
    return presentation;
}

} // namespace cellularity::render::vulkan
