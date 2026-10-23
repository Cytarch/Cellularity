#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "app/EnvironmentFieldPreviewRenderer.h"
#include "app/interaction/NativeViewport.h"
#include "core/CellType.h"
#include "core/Environment.h"
#include "render/SimulationWorldGpuRenderer.h"
#include "render/NativeCellRendererResources.h"
#include "render/NativeRendererResourcesController.h"
#include "render/backend/RenderSurfaceSession.h"
#include "render/backend/vulkan/VulkanBasicCellFrame.h"
#include "render/frame/PreparedRenderFrame.h"
#include "tests/support/RenderingTestDoubles.h"
#include "tests/support/TestAssertions.h"

namespace {

using cellularity::render::SimulationWorldGpuRenderer;
using cellularity::render::EnvironmentFieldPreviewCommand;
using cellularity::render::PreparedRenderFrame;
using cellularity::render::RenderMarker;
using cellularity::render::RenderPresentationState;
using cellularity::render::vulkan::buildVulkanBasicCellUploadPlan;
using cellularity::render::vulkan::makeVulkanBasicCellDemoFrame;
using cellularity::render::vulkan::vulkanBasicCellProgramForType;
using cellularity::render::vulkan::vulkanBasicCellProgramName;
using cellularity::render::vulkan::VulkanBasicCellUploadPlan;
using cellularity::render::vulkan::vulkanOutlineProgramForType;
using cellularity::render::vulkan::vulkanWorldProgramName;
using cellularity::tests::assertTrue;
using cellularity::tests::FakeGlApi;
using cellularity::tests::FakeRenderAssets;
using cellularity::tests::FakeTextureLoader;

constexpr std::array<float, 16> kIdentityMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
};

void configureIndexedBucket(PreparedRenderFrame& frame, int bucket,
        int vertexStart, int indexStart) {
    frame.cellVertexCountBefore[bucket] = vertexStart;
    frame.cellVertexCount[bucket] = 3;
    frame.cellIndexCountBefore[bucket] = indexStart;
    frame.cellIndexCount[bucket] = 3;
    frame.bunches[bucket] = 1;
    frame.bunchStart[bucket][0] = vertexStart;
    frame.bunchSize[bucket][0] = 3;
    frame.bunchIndexStart[bucket][0] = indexStart;
    frame.bunchIndexSize[bucket][0] = 3;
}

PreparedRenderFrame representativeFrame(bool previewMode) {
    PreparedRenderFrame frame;
    frame.valid = true;
    frame.geometryReady = true;
    frame.environment.previewMode = previewMode;
    frame.environment.lightAngle = 0.3;
    frame.environment.lightRange = 0.7;
    frame.environment.lightIntensity = 0.5;
    frame.environment.dishRadius = 2.0;
    frame.environment.backgroundColor = {0.2f, 0.3f, 0.4f};
    frame.viewport.mvpMatrix = kIdentityMatrix;
    frame.viewport.surfaceWidth = 320;
    frame.viewport.surfaceHeight = 240;
    frame.markers.push_back(RenderMarker{0.0f, 0.0f, 0.1f, 0.8f});
    frame.foodInstanceCount = 1;
    frame.deadCellInstanceCount = 1;
    frame.drawLinksCount = 3;

    int vertexCursor = 0;
    int indexCursor = 0;
    configureIndexedBucket(frame, CellTypes::ordinal(CellType::PHAGOCYTE),
            vertexCursor, indexCursor);
    vertexCursor += 3;
    indexCursor += 3;
    if (previewMode) {
        configureIndexedBucket(frame,
                SimulationWorldGpuRenderer::PREVIEW_EXTRA_CELL_TYPE_INDEX,
                vertexCursor, indexCursor);
        vertexCursor += 3;
        indexCursor += 3;
    }

    const int outlineShift = CellTypes::length + (previewMode ? 1 : 0);
    const int outlineBucket =
            outlineShift + CellTypes::ordinal(CellType::FLAGELLOCYTE);
    frame.cellVertexCountBefore[outlineBucket] = vertexCursor;
    frame.cellVertexCount[outlineBucket] = 3;
    vertexCursor += 3;

    frame.cellDrawVertexCount = vertexCursor;
    frame.cellDrawIndexCount = indexCursor;
    frame.cellCenterFloatCount = vertexCursor * 4;
    frame.cellTriangleCoords.resize(
            static_cast<std::size_t>(vertexCursor) * 2U, 0.0F);
    frame.cellTextureCoords.resize(
            static_cast<std::size_t>(vertexCursor) * 4U, 0.0F);
    frame.cellCenterCoords.resize(
            static_cast<std::size_t>(frame.cellCenterFloatCount), 0.0F);
    frame.cellColors.resize(
            static_cast<std::size_t>(vertexCursor) * 4U, 0.0F);
    frame.cellIndices = previewMode
            ? std::vector<unsigned short>{0, 1, 2, 0, 1, 2}
            : std::vector<unsigned short>{0, 1, 2};
    frame.foodInstanceAttributes = {0.0F, 0.0F, 0.1F, 0.0F};
    frame.foodVertexAttributes = {0.2F, 0.3F, 0.4F, 0.5F};
    frame.deadCellInstanceAttributes = {0.0F, 0.0F, 0.1F, 0.2F};
    frame.deadCellVertexAttributes = {0.2F, 0.3F, 0.4F, 0.5F};
    frame.linkTriangleCoords.resize(12U, 0.0F);
    frame.linkTextureCoords.resize(3U, 0.0F);
    return frame;
}

RenderPresentationState presentationFor(const PreparedRenderFrame& frame) {
    RenderPresentationState presentation;
    presentation.viewport = frame.viewport;
    presentation.backgroundColor = frame.environment.backgroundColor;
    return presentation;
}

void assertProgramOrder(const std::vector<FakeGlApi::DrawEvent>& events,
        const std::vector<std::string>& expected,
        const std::string& context) {
    assertTrue(events.size() == expected.size(),
            context + " draw count changed: expected="
                    + std::to_string(expected.size()) + ", actual="
                    + std::to_string(events.size()));
    for (std::size_t index = 0; index < expected.size(); ++index) {
        assertTrue(events[index].programName == expected[index],
                context + " pass " + std::to_string(index)
                        + " changed: expected=" + expected[index]
                        + ", actual=" + events[index].programName);
    }
}

void assertDrawState(const FakeGlApi& gl,
        const FakeGlApi::DrawEvent& event,
        bool blendEnabled,
        bool depthTestEnabled,
        int depthFunction,
        bool depthWriteEnabled,
        const std::string& context) {
    assertTrue(event.blendEnabled == blendEnabled,
            context + " blend enable changed");
    if (blendEnabled) {
        assertTrue(
                event.blendSourceFactor == gl.srcAlpha()
                        && event.blendDestinationFactor
                                == gl.oneMinusSrcAlpha(),
                context + " blend factors changed");
    }
    assertTrue(event.depthTestEnabled == depthTestEnabled,
            context + " depth-test enable changed");
    assertTrue(event.depthFunction == depthFunction,
            context + " depth function changed");
    assertTrue(event.depthWriteEnabled == depthWriteEnabled,
            context + " depth write changed");
}

void testProductionShaderCatalogContainsAllPrograms() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    NativeCellRendererResources resources(
            assets, gl, textureLoader, true);
    (void)resources;

    std::set<std::string> actual;
    for (const auto& [program, name] : gl.linkedProgramNames) {
        (void)program;
        actual.insert(name);
    }
    const std::set<std::string> expected{
            "arrow", "background", "buoyocyte", "ciliocyte_outline",
            "dead", "density_field", "devorocyte_outline",
            "flagellocyte_outline", "food", "gamete", "genome_grid",
            "glueocyte_outline", "keratinocyte", "link", "lipocyte",
            "marker", "myocyte", "neurocyte", "nitrocyte_outline",
            "phagocyte", "photocyte", "pixel_text", "secrocyte",
            "senseocyte_outline", "solid_color", "stemocyte",
            "stereocyte_outline", "virocyte_outline"
    };
    assertTrue(actual == expected,
            "Production resources no longer instantiate the 28-program GLES catalog");
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length;
            ++cellTypeIndex) {
        const CellTypeInfo info =
                CellTypes::info(CellTypes::fromOrdinal(cellTypeIndex));
        assertTrue(info.baseShader
                        == vulkanBasicCellProgramName(
                                vulkanBasicCellProgramForType(cellTypeIndex)),
                "GLES and Vulkan base-program mappings differ at Cell type "
                        + std::to_string(cellTypeIndex));
        if (!info.outlineShader.empty()) {
            assertTrue(info.outlineShader
                            == vulkanWorldProgramName(
                                    vulkanOutlineProgramForType(
                                            cellTypeIndex)),
                    "GLES and Vulkan outline-program mappings differ at Cell type "
                            + std::to_string(cellTypeIndex));
        }
    }
    assertTrue(std::string(vulkanWorldProgramName(
                       cellularity::render::vulkan::VulkanWorldProgram::Marker))
                            == "marker"
                    && std::string(vulkanWorldProgramName(
                               cellularity::render::vulkan::VulkanWorldProgram::PixelText))
                            == "pixel_text",
            "GLES and Vulkan overlay-program mappings differ");
}

void testDecodedTexturePayloadOwnsBackendCreationAndLossSemantics() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    {
        NativeCellRendererResources resources(
                assets, gl, textureLoader, false);
        assertTrue(textureLoader.requests.size() == 2
                        && textureLoader.requests[0].name == "small_dots"
                        && !textureLoader.requests[0].clampToEdge
                        && textureLoader.requests[1].name
                                == "gauss_bessel_integral"
                        && textureLoader.requests[1].clampToEdge,
                "GLES resources no longer consume the two canonical decoded textures");
        assertTrue(resources.dotTextureHandle() == 300
                        && resources.besselTextureHandle() == 301
                        && gl.textureImageUploadCount == 2,
                "GLES did not create one persistent object per decoded texture");
        const std::vector<FakeGlApi::TextureParameterCall> expectedParameters = {
                {gl.textureMinFilter(), gl.nearest()},
                {gl.textureMagFilter(), gl.linear()},
                {gl.textureWrapS(), gl.repeat()},
                {gl.textureWrapT(), gl.repeat()},
                {gl.textureMinFilter(), gl.nearest()},
                {gl.textureMagFilter(), gl.linear()},
                {gl.textureWrapS(), gl.clampToEdge()},
                {gl.textureWrapT(), gl.clampToEdge()},
        };
        const bool parametersMatch =
                gl.textureParameterCalls.size() == expectedParameters.size()
                && std::equal(gl.textureParameterCalls.begin(),
                        gl.textureParameterCalls.end(),
                        expectedParameters.begin(),
                        [](const auto& left, const auto& right) {
                            return left.parameterName == right.parameterName
                                    && left.value == right.value;
                        });
        assertTrue(parametersMatch,
                "GLES filtering or edge behavior differs from the decoded payload");
    }
    assertTrue(gl.deletedTextures == std::vector<int>({300, 301}),
            "Normal GLES resource release did not delete both textures");

    FakeGlApi abandonedGl;
    FakeTextureLoader abandonedTextureLoader;
    SimulationWorldGpuRenderer renderer;
    NativeRendererResourcesController controller;
    controller.load(renderer, assets, abandonedGl, abandonedTextureLoader,
            false);
    controller.abandon(renderer);
    assertTrue(abandonedGl.deletedTextures.empty(),
            "Context loss attempted to delete texture names through an invalid GL context");
}

void testSameImmutableBasicCellFrameProducesMatchingGlesAndVulkanCommands() {
    PreparedRenderFrame frame = makeVulkanBasicCellDemoFrame();
    const auto originalIdentity = frame.identity;
    const auto originalLayout = frame.layout;
    const auto originalPositions = frame.cellTriangleCoords;

    VulkanBasicCellUploadPlan vulkanPlan;
    std::string error;
    assertTrue(buildVulkanBasicCellUploadPlan(frame, vulkanPlan, error),
            "Vulkan rejected the shared A/B frame: " + error);

    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    NativeCellRendererResources resources(
            assets, gl, textureLoader, false);
    SimulationWorldGpuRenderer renderer;
    renderer.createGlBuffers(gl);
    renderer.drawCells(gl, resources.basicCellShaders(),
            resources.arrowShader(), resources.dotTextureHandle(), false,
            frame);

    assertTrue(gl.drawEvents.size() == vulkanPlan.drawCount,
            "GLES and Vulkan basic-cell command counts differ");
    assertTrue(gl.drawElementsCalls.size() == vulkanPlan.drawCount,
            "GLES did not issue one indexed command per Vulkan draw bunch");
    assertTrue(gl.pointerCalls.size() == vulkanPlan.drawCount * 4U,
            "GLES basic-cell attribute binding count changed");
    for (std::size_t index = 0; index < vulkanPlan.drawCount; ++index) {
        const auto& vulkanDraw = vulkanPlan.draws[index];
        const auto& glesDraw = gl.drawEvents[index];
        assertTrue(glesDraw.programName
                        == vulkanBasicCellProgramName(vulkanDraw.program),
                "GLES and Vulkan selected different basic-cell programs");
        assertTrue(glesDraw.count
                        == static_cast<int>(vulkanDraw.indexCount),
                "GLES and Vulkan indexed draw sizes differ");
        assertTrue(gl.drawElementsCalls[index].offsetBytes
                        == static_cast<int>(vulkanDraw.firstIndex
                                * sizeof(unsigned short)),
                "GLES and Vulkan index offsets differ");

        const std::size_t pointer = index * 4U;
        const int vertexOffset = vulkanDraw.vertexOffset;
        assertTrue(gl.pointerCalls[pointer].offsetBytes
                                == vertexOffset * 2 * static_cast<int>(sizeof(float))
                        && gl.pointerCalls[pointer + 1U].offsetBytes
                                == vertexOffset * 2 * static_cast<int>(sizeof(float))
                        && gl.pointerCalls[pointer + 2U].offsetBytes
                                == vertexOffset * 4 * static_cast<int>(sizeof(float))
                        && gl.pointerCalls[pointer + 3U].offsetBytes
                                == vertexOffset * 4 * static_cast<int>(sizeof(float)),
                "GLES and Vulkan vertex-stream offsets differ");
    }

    assertTrue(frame.identity.captureSequence
                            == originalIdentity.captureSequence
                    && frame.identity.worldEpoch == originalIdentity.worldEpoch
                    && frame.identity.modeGeneration
                            == originalIdentity.modeGeneration
                    && frame.layout.version == originalLayout.version
                    && frame.layout.geometryRevision
                            == originalLayout.geometryRevision
                    && frame.layout.instanceRevision
                            == originalLayout.instanceRevision
                    && frame.cellTriangleCoords == originalPositions,
            "A/B command capture mutated the shared PreparedRenderFrame");
}

void testMicroscopePassOrderAndPipelineState() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    NativeCellRendererResources resources(
            assets, gl, textureLoader, false);
    SimulationWorldGpuRenderer renderer;
    renderer.createGlBuffers(gl);
    PreparedRenderFrame frame = representativeFrame(false);
    VulkanBasicCellUploadPlan vulkanPlan;
    std::string planError;
    assertTrue(buildVulkanBasicCellUploadPlan(
                       frame, vulkanPlan, planError),
            "Vulkan rejected the shared microscope A/B frame: " + planError);

    gl.drawEvents.clear();
    renderer.draw(gl, resources.basicCellShaders(),
            resources.outlineCellShaders(), resources.arrowShader(),
            resources.backgroundShader(), resources.foodShader(),
            resources.deadSplashShader(), resources.linkShader(),
            &resources.markerShader(), resources.dotTextureHandle(),
            resources.besselTextureHandle(), frame, presentationFor(frame));

    assertProgramOrder(gl.drawEvents,
            {"background", "marker", "food", "link", "dead",
                    "phagocyte", "flagellocyte_outline"},
            "Microscope");
    assertDrawState(gl, gl.drawEvents[0], false, false, gl.less(), false,
            "Microscope background");
    assertDrawState(gl, gl.drawEvents[1], true, false, gl.less(), false,
            "Microscope marker");
    assertDrawState(gl, gl.drawEvents[2], true, false, gl.less(), false,
            "Microscope Food");
    assertDrawState(gl, gl.drawEvents[3], true, false, gl.less(), false,
            "Microscope link");
    assertDrawState(gl, gl.drawEvents[4], true, true, gl.always(), false,
            "Microscope dead splash");
    assertDrawState(gl, gl.drawEvents[5], true, true, gl.always(), true,
            "Microscope Cell");
    assertDrawState(gl, gl.drawEvents[6], true, true, gl.less(), false,
            "Microscope outline");
    assertTrue(!gl.blendEnabled && gl.depthTestEnabled
                    && gl.depthFunction == gl.less()
                    && !gl.depthWriteEnabled,
            "Microscope final GLES state changed");
    assertTrue(vulkanPlan.foodInstanceCount
                            == static_cast<std::uint32_t>(
                                    gl.drawEvents[2].instanceCount)
                    && vulkanPlan.markerInstanceCount
                            == static_cast<std::uint32_t>(
                                    frame.markers.size())
                    && vulkanPlan.linkDrawCount == 1
                    && vulkanPlan.linkDraws[0].vertexCount
                            == static_cast<std::uint32_t>(
                                    gl.drawEvents[3].count)
                    && vulkanPlan.deadInstanceCount
                            == static_cast<std::uint32_t>(
                                    gl.drawEvents[4].instanceCount)
                    && vulkanPlan.drawCount == 1
                    && vulkanBasicCellProgramName(
                               vulkanPlan.draws[0].program)
                            == gl.drawEvents[5].programName
                    && vulkanPlan.draws[0].indexCount
                            == static_cast<std::uint32_t>(
                                    gl.drawEvents[5].count)
                    && vulkanPlan.outlineDrawCount == 1
                    && vulkanWorldProgramName(
                               vulkanPlan.outlineDraws[0].program)
                            == gl.drawEvents[6].programName
                    && vulkanPlan.outlineDraws[0].vertexCount
                            == static_cast<std::uint32_t>(
                                    gl.drawEvents[6].count),
            "GLES and Vulkan world-pass counts or shader selection differ");
}

void testGenomePreviewPassOrderAndPipelineState() {
    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    NativeCellRendererResources resources(
            assets, gl, textureLoader, true);
    SimulationWorldGpuRenderer renderer;
    renderer.createGlBuffers(gl);
    PreparedRenderFrame frame = representativeFrame(true);

    gl.drawEvents.clear();
    renderer.drawPreview(gl, resources.basicCellShaders(),
            resources.outlineCellShaders(), resources.arrowShader(),
            resources.deadSplashShader(), resources.linkShader(),
            &resources.markerShader(), resources.dotTextureHandle(),
            resources.besselTextureHandle(), frame, presentationFor(frame));

    assertProgramOrder(gl.drawEvents,
            {"marker", "dead", "phagocyte", "arrow", "link",
                    "flagellocyte_outline"},
            "Genome Preview");
    assertDrawState(gl, gl.drawEvents[0], true, false, gl.less(), false,
            "Genome Preview marker");
    assertDrawState(gl, gl.drawEvents[1], true, false, gl.always(), false,
            "Genome Preview dead splash");
    assertDrawState(gl, gl.drawEvents[2], false, true, gl.always(), true,
            "Genome Preview Cell");
    assertDrawState(gl, gl.drawEvents[3], true, true, gl.always(), true,
            "Genome Preview arrow");
    assertDrawState(gl, gl.drawEvents[4], true, true, gl.always(), true,
            "Genome Preview link");
    assertDrawState(gl, gl.drawEvents[5], true, true, gl.less(), false,
            "Genome Preview outline");
    assertTrue(gl.blendEnabled && gl.depthTestEnabled
                    && gl.depthFunction == gl.less()
                    && !gl.depthWriteEnabled,
            "Genome Preview final GLES state changed");
}

void testEnvironmentPreviewPassOrderAndInheritedBlendState() {
    Environment environment =
            Environment::constructDefaultEnvironment(4, 4, 2.0);
    environment.mDensityFieldType = Environment::DENSITY_FIELD_RADIAL;
    NativeViewport viewport(2.0f);
    viewport.setSurfaceSize(320, 240);
    viewport.setCamera(0.0f, 0.0f, 1.0f);

    FakeRenderAssets assets;
    FakeGlApi gl;
    FakeTextureLoader textureLoader;
    NativeCellRendererResources resources(
            assets, gl, textureLoader, false);
    EnvironmentFieldPreviewRenderer renderer;
    EnvironmentFieldPreviewCommand command;
    command.matrix = viewport.worldMatrix(kIdentityMatrix);
    command.surfaceWidth = viewport.surfaceWidth();
    command.surfaceHeight = viewport.surfaceHeight();
    command.dishRadius = environment.mRadius;
    command.fieldType = environment.mDensityFieldType;
    command.fieldBase = environment.mDensityFieldBase;
    command.fieldStrength = environment.mDensityFieldStrength;
    command.fieldAngle = environment.mDensityFieldAngle;
    command.fieldOffset = environment.mDensityFieldOffset;
    command.fieldSoftness = environment.mDensityFieldSoftness;
    command.displayMin = -3.3;
    command.displayMax = 3.3;
    command.gravityFieldType = environment.mGravityFieldType;
    command.gravityFieldAngle = environment.mGravityFieldAngle;

    gl.drawEvents.clear();
    assertTrue(renderer.draw(command, gl, resources) == 1,
            "Environment preview rejected a valid immutable command");
    assertProgramOrder(gl.drawEvents,
            {"background", "density_field", "marker"},
            "Environment field preview");
    assertDrawState(gl, gl.drawEvents[0], false, false, gl.less(), true,
            "First environment background");
    assertDrawState(gl, gl.drawEvents[1], true, false, gl.less(), false,
            "Environment density field");
    assertDrawState(gl, gl.drawEvents[2], true, false, gl.less(), false,
            "Environment radial marker");

    gl.drawEvents.clear();
    assertTrue(renderer.draw(command, gl, resources) == 1,
            "Repeated environment preview rejected a valid command");
    assertProgramOrder(gl.drawEvents,
            {"background", "density_field", "marker"},
            "Repeated environment field preview");
    assertDrawState(gl, gl.drawEvents[0], true, false, gl.less(), true,
            "Repeated environment background inherited blend");
    assertTrue(gl.blendEnabled && !gl.depthTestEnabled
                    && gl.depthWriteEnabled,
            "Environment preview final GLES state changed");
}

} // namespace

int main() {
    testProductionShaderCatalogContainsAllPrograms();
    testDecodedTexturePayloadOwnsBackendCreationAndLossSemantics();
    testSameImmutableBasicCellFrameProducesMatchingGlesAndVulkanCommands();
    testMicroscopePassOrderAndPipelineState();
    testGenomePreviewPassOrderAndPipelineState();
    testEnvironmentPreviewPassOrderAndInheritedBlendState();
    std::cout << "GLES render contract tests passed\n";
    return 0;
}
