#include <array>
#include <cstdint>
#include <iostream>
#include <string>

#include "app/microscope/render/MicroscopeDebugHud.h"
#include "core/CellDataConstants.h"
#include "render/backend/vulkan/VulkanBasicCellFrame.h"
#include "render/text/PixelTextRenderer.h"
#include "tests/support/TestAssertions.h"

namespace {

using cellularity::render::makePreparedRenderFrameView;
using cellularity::render::PixelTextRenderer;
using cellularity::render::PreparedRenderFrame;
using cellularity::render::vulkan::buildVulkanBasicCellUploadPlan;
using cellularity::render::vulkan::makeVulkanBasicCellDemoFrame;
using cellularity::render::vulkan::makeVulkanBasicCellDemoPresentation;
using cellularity::render::vulkan::VulkanBasicCellProgram;
using cellularity::render::vulkan::vulkanBasicCellProgramForType;
using cellularity::render::vulkan::VulkanBasicCellUploadPlan;
using cellularity::render::vulkan::VulkanPreparedFrameUploadIdentity;
using cellularity::render::vulkan::VulkanWorldProgram;
using cellularity::tests::assertTrue;

using cellularity::microscope::MicroscopeDebugHud;
using cellularity::microscope::MicroscopeDebugHudTelemetry;

void testEveryCellTypeMapsToTheProductionBaseProgram() {
    const std::array<VulkanBasicCellProgram, CellTypes::length> expected = {
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
    for (int index = 0; index < CellTypes::length; ++index) {
        assertTrue(vulkanBasicCellProgramForType(index) == expected[index],
                "Vulkan base shader mapping differs at Cell type "
                        + std::to_string(index));
    }
}

void testDemoFrameProducesExactUploadRangesAndDrawOrder() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    VulkanBasicCellUploadPlan plan;
    std::string error;
    assertTrue(buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Demo PreparedRenderFrame was rejected: " + error);

    assertTrue(plan.identity
                    == VulkanPreparedFrameUploadIdentity::from(
                            makePreparedRenderFrameView(frame)),
            "Vulkan upload identity did not preserve frame revisions");
    assertTrue(plan.positions.size
                    == frame.cellTriangleCoords.size() * sizeof(float),
            "Position upload range changed");
    assertTrue(plan.textureCoordinates.size
                    == frame.cellTextureCoords.size() * sizeof(float),
            "Texture-coordinate upload range changed");
    assertTrue(plan.centers.size
                    == static_cast<std::uint64_t>(frame.cellCenterFloatCount)
                            * sizeof(float),
            "Center upload range changed");
    assertTrue(plan.colors.size
                    == frame.cellColors.size() * sizeof(float),
            "Color upload range changed");
    assertTrue(plan.indices.size
                    == frame.cellIndices.size() * sizeof(unsigned short),
            "Index upload range changed");
    assertTrue(plan.foodInstances.size
                            == frame.foodInstanceAttributes.size()
                                    * sizeof(float)
                    && plan.foodAttributes.size
                            == frame.foodVertexAttributes.size()
                                    * sizeof(float),
            "Food upload ranges changed");
    assertTrue(plan.deadInstances.size
                            == frame.deadCellInstanceAttributes.size()
                                    * sizeof(float)
                    && plan.deadAttributes.size
                            == frame.deadCellVertexAttributes.size()
                                    * sizeof(float),
            "Dead-splash upload ranges changed");
    assertTrue(plan.linkVertices.size
                            == frame.linkTriangleCoords.size()
                                    * sizeof(float)
                    && plan.linkPatternPositions.size
                            == frame.linkTextureCoords.size()
                                    * sizeof(float),
            "Link upload ranges changed");
    assertTrue(plan.markerInstances.size
                            == frame.markers.size() * 4U * sizeof(float)
                    && plan.markerInstanceCount == frame.markers.size(),
            "Marker upload range or instance count changed");
    assertTrue(plan.drawCount == CellTypes::length,
            "Demo frame must exercise every basic Cell bucket");
    assertTrue(frame.environment.dishRadius < 1.0
                    && frame.cellColors[3] > 0.005F
                    && frame.cellColors[3]
                            <= CellDataConstants::MAX_CELL_RADIUS,
            "Demo frame must use production world units so fixed-size Cell details remain visible");

    for (std::size_t index = 0; index < plan.drawCount; ++index) {
        const auto& draw = plan.draws[index];
        assertTrue(draw.cellTypeIndex == index,
                "Vulkan draw order no longer follows Cell bucket order");
        assertTrue(draw.program
                        == vulkanBasicCellProgramForType(
                                static_cast<int>(index)),
                "Vulkan draw selected the wrong base shader");
        assertTrue(draw.firstIndex == index * 6
                        && draw.indexCount == 6
                        && draw.vertexOffset
                                == static_cast<std::int32_t>(index * 4),
                "Vulkan draw bunch offsets changed");
    }

    assertTrue(plan.previewMode && plan.arrowDrawCount == 1
                    && plan.arrowDraws[0].firstIndex
                            == CellTypes::length * 6U
                    && plan.arrowDraws[0].indexCount == 6
                    && plan.arrowDraws[0].vertexOffset
                            == CellTypes::length * 4,
            "Demo frame no longer exercises the preview arrow bucket");
    assertTrue(plan.foodInstanceCount == 3
                    && plan.deadInstanceCount == 1
                    && plan.markerInstanceCount == 2
                    && plan.linkDrawCount == 1
                    && plan.linkDraws[0].vertexCount == 6,
            "Demo frame no longer exercises world instance/link passes");
    constexpr std::array<VulkanWorldProgram, 8> expectedOutlines = {
            VulkanWorldProgram::FlagellocyteOutline,
            VulkanWorldProgram::DevorocyteOutline,
            VulkanWorldProgram::GlueocyteOutline,
            VulkanWorldProgram::VirocyteOutline,
            VulkanWorldProgram::NitrocyteOutline,
            VulkanWorldProgram::StereocyteOutline,
            VulkanWorldProgram::SenseocyteOutline,
            VulkanWorldProgram::CiliocyteOutline,
    };
    assertTrue(plan.outlineDrawCount == expectedOutlines.size(),
            "Demo frame must exercise every production outline shader");
    for (std::size_t index = 0; index < expectedOutlines.size(); ++index) {
        assertTrue(plan.outlineDraws[index].program
                                == expectedOutlines[index]
                        && plan.outlineDraws[index].vertexCount > 0,
                "Vulkan outline draw order or shader mapping changed");
    }
}

void testMarkerRevisionInvalidatesOnlyPreparedGeometryIdentity() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    const auto original = VulkanPreparedFrameUploadIdentity::from(
            makePreparedRenderFrameView(frame));
    ++frame.layout.markerRevision;
    const auto changed = VulkanPreparedFrameUploadIdentity::from(
            makePreparedRenderFrameView(frame));

    assertTrue(original != changed,
            "Marker changes must invalidate the Vulkan geometry upload");
    assertTrue(original.geometryRevision == changed.geometryRevision
                    && original.instanceRevision == changed.instanceRevision,
            "Marker changes must retain unrelated geometry revisions");
}

void testSharedHudGeometryPreservesTheGlesPixelContract() {
    MicroscopeDebugHud hud;
    MicroscopeDebugHudTelemetry telemetry;
    telemetry.rateSampleSequence = 1;
    telemetry.actualRateValid = true;
    telemetry.actualCompletedUps = 2'345.0;
    telemetry.stageTimings.sequence = 1;
    telemetry.stageTimings.totalNanos = 220'000.0;
    telemetry.stageTimings.stageNanos.fill(10'000.0);

    const PixelTextRenderer& geometry =
            hud.prepareGeometry(1280, 720, telemetry);
    const std::uint64_t firstRevision = geometry.geometryRevision();
    assertTrue(geometry.hasCommittedGeometry()
                    && geometry.instanceCount() == 507
                    && geometry.packedInstances().size()
                            == 507U * PixelTextRenderer::kInstanceFloatCount,
            "Shared HUD geometry drifted from the GLES instance contract");
    assertTrue(PixelTextRenderer::kAtlasWidth == 40
                    && PixelTextRenderer::kAtlasHeight == 42
                    && PixelTextRenderer::glyphAtlas().size() == 40U * 42U,
            "Shared glyph atlas dimensions changed");

    const auto& unchanged = hud.prepareGeometry(1280, 720, telemetry);
    assertTrue(unchanged.geometryRevision() == firstRevision,
            "Unchanged HUD values rebuilt text geometry");

    ++telemetry.rateSampleSequence;
    telemetry.actualCompletedUps = 3'210.0;
    const auto& updated = hud.prepareGeometry(1280, 720, telemetry);
    assertTrue(updated.geometryRevision() == firstRevision + 1,
            "Changed HUD telemetry did not rebuild text geometry");
}

void testCameraOnlyPresentationDoesNotChangeUploadIdentity() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    const auto identity = VulkanPreparedFrameUploadIdentity::from(
            makePreparedRenderFrameView(frame));
    auto first = makeVulkanBasicCellDemoPresentation(1080, 2400);
    auto second = makeVulkanBasicCellDemoPresentation(2400, 1080);
    second.viewport.revision = first.viewport.revision + 1;

    assertTrue(first.viewport.mvpMatrix != second.viewport.mvpMatrix,
            "Camera test requires distinct presentation matrices");
    assertTrue(identity
                    == VulkanPreparedFrameUploadIdentity::from(
                            makePreparedRenderFrameView(frame)),
            "Presentation-only changes must not invalidate geometry uploads");
}

void testMalformedActiveRangeIsRejectedBeforeUpload() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    frame.cellDrawVertexCount += 1;
    VulkanBasicCellUploadPlan plan;
    std::string error;
    assertTrue(!buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Out-of-bounds PreparedRenderFrame was accepted");
    assertTrue(!error.empty(), "Rejected frame must provide a diagnostic");
}

void testMalformedDrawBunchIsRejectedBeforeUpload() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    frame.cellIndices[0] = static_cast<unsigned short>(
            frame.bunchSize[0][0]);
    VulkanBasicCellUploadPlan plan;
    std::string error;
    assertTrue(!buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Out-of-bounds local Cell index was accepted");

    frame = makeVulkanBasicCellDemoFrame();
    frame.bunchStart[1][0] += 1;
    error.clear();
    assertTrue(!buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Non-contiguous Cell draw bunch was accepted");
}

void testEmptyPublishedFrameProducesNoUploadOrDrawRanges() {
    PreparedRenderFrame frame;
    frame.valid = true;
    frame.geometryReady = true;
    VulkanBasicCellUploadPlan plan;
    std::string error;
    assertTrue(buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Empty published PreparedRenderFrame was rejected: " + error);
    assertTrue(plan.requiredBytes == 0 && plan.drawCount == 0
                    && plan.positions.size == 0
                    && plan.textureCoordinates.size == 0
                    && plan.centers.size == 0 && plan.colors.size == 0
                    && plan.indices.size == 0
                    && plan.foodInstances.size == 0
                    && plan.foodAttributes.size == 0
                    && plan.deadInstances.size == 0
                    && plan.deadAttributes.size == 0
                    && plan.linkVertices.size == 0
                    && plan.linkPatternPositions.size == 0
                    && plan.markerInstances.size == 0
                    && plan.markerInstanceCount == 0
                    && plan.arrowDrawCount == 0
                    && plan.outlineDrawCount == 0
                    && plan.linkDrawCount == 0,
            "Empty PreparedRenderFrame produced active Vulkan ranges");
}

void testEmptyGenomePreviewArrowBucketProducesNoDraw() {
    PreparedRenderFrame frame;
    frame.valid = true;
    frame.geometryReady = true;
    frame.environment.previewMode = true;
    // CellBoundaryMeshBuilder visits the Preview bucket even when no living
    // Cells remain and therefore retains one empty metadata bunch.
    frame.bunches[cellularity::render::kPreviewExtraCellTypeIndex] = 1;

    VulkanBasicCellUploadPlan plan;
    std::string error;
    assertTrue(buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Empty Genome Preview frame was rejected: " + error);
    assertTrue(plan.previewMode && plan.arrowDrawCount == 0
                    && plan.drawCount == 0 && plan.requiredBytes == 0,
            "Empty Genome Preview arrow bucket produced an active draw");
}

void testBaseGeometryWithoutOutlineGeometryIsAccepted() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    constexpr int baseVertexCount = CellTypes::length * 4;
    constexpr int baseIndexCount = CellTypes::length * 6;
    frame.environment.previewMode = false;
    frame.cellDrawVertexCount = baseVertexCount;
    frame.cellDrawIndexCount = baseIndexCount;
    frame.cellCenterFloatCount = baseVertexCount * 2;
    frame.cellTriangleCoords.resize(baseVertexCount * 2U);
    frame.cellTextureCoords.resize(baseVertexCount * 4U);
    frame.cellCenterCoords.resize(baseVertexCount * 2U);
    frame.cellColors.resize(baseVertexCount * 4U);
    frame.cellIndices.resize(baseIndexCount);
    for (int bucket = CellTypes::length;
            bucket < cellularity::render::kCellDataBucketCount; ++bucket) {
        frame.cellVertexCountBefore[bucket] = baseVertexCount;
        frame.cellVertexCount[bucket] = 0;
        frame.cellIndexCountBefore[bucket] = baseIndexCount;
        frame.cellIndexCount[bucket] = 0;
        frame.bunches[bucket] = 0;
    }

    VulkanBasicCellUploadPlan plan;
    std::string error;
    assertTrue(buildVulkanBasicCellUploadPlan(frame, plan, error),
            "A production frame without outline geometry was rejected: "
                    + error);
    assertTrue(plan.drawCount == CellTypes::length
                    && plan.arrowDrawCount == 0
                    && plan.outlineDrawCount == 0,
            "A frame without outline geometry produced secondary draws");
}

void testMalformedWorldRangesAreRejectedBeforeUpload() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    frame.foodInstanceCount += 1;
    VulkanBasicCellUploadPlan plan;
    std::string error;
    assertTrue(!buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Out-of-bounds Food instance range was accepted");

    frame = makeVulkanBasicCellDemoFrame();
    frame.linkTextureCoords.pop_back();
    error.clear();
    assertTrue(!buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Out-of-bounds link pattern range was accepted");

    frame = makeVulkanBasicCellDemoFrame();
    const int outlineBucket = CellTypes::length + 1
            + CellTypes::ordinal(CellType::FLAGELLOCYTE);
    frame.cellCenterFloatCount =
            frame.cellVertexCountBefore[outlineBucket] * 4;
    error.clear();
    assertTrue(!buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Out-of-bounds outline center range was accepted");
}

void testUnpublishedFrameIsRejected() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    frame.valid = false;
    VulkanBasicCellUploadPlan plan;
    std::string error;
    assertTrue(!buildVulkanBasicCellUploadPlan(frame, plan, error),
            "Unpublished PreparedRenderFrame was accepted");
}

} // namespace

int main() {
    testEveryCellTypeMapsToTheProductionBaseProgram();
    testDemoFrameProducesExactUploadRangesAndDrawOrder();
    testMarkerRevisionInvalidatesOnlyPreparedGeometryIdentity();
    testSharedHudGeometryPreservesTheGlesPixelContract();
    testCameraOnlyPresentationDoesNotChangeUploadIdentity();
    testMalformedActiveRangeIsRejectedBeforeUpload();
    testMalformedDrawBunchIsRejectedBeforeUpload();
    testEmptyPublishedFrameProducesNoUploadOrDrawRanges();
    testEmptyGenomePreviewArrowBucketProducesNoDraw();
    testBaseGeometryWithoutOutlineGeometryIsAccepted();
    testMalformedWorldRangesAreRejectedBeforeUpload();
    testUnpublishedFrameIsRejected();
    std::cout << "Vulkan basic-cell frame tests passed\n";
    return 0;
}
