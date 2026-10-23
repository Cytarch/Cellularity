#include "render/SimulationWorldGpuRenderer.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "core/RendererMemoryModel.h"
#include "render/gl/NativeGlApi.h"
#include "render/shaders/NativeShaders.h"

namespace cellularity::render {

static_assert(sizeof(RenderMarker) <= memory::kRenderMarkerBudgetBytes,
        "RendererMemoryModel must cover RenderMarker");

namespace {

int checkedUploadByteCount(int elementCount, int elementBytes,
        const char* message) {
    if (elementCount < 0 || elementBytes <= 0 || elementCount > std::numeric_limits<int>::max() / elementBytes) {
        throw std::length_error(message);
    }
    return elementCount * elementBytes;
}

int checkedIntProduct(int value, int factor, const char* message) {
    if (value < 0 || factor <= 0 || value > std::numeric_limits<int>::max() / factor) {
        throw std::length_error(message);
    }
    return value * factor;
}

} // namespace

bool SimulationWorldGpuRenderer::hasGlBuffers() const noexcept {
    return mCellCoordsVbo != 0;
}

void SimulationWorldGpuRenderer::createGlBuffers(NativeGlApi& gl) {
    if (hasGlBuffers()) {
        return;
    }
    invalidateUploadedFrame();
    int buffers[13] = {};
    gl.genBuffers(13, buffers);
    mCellCoordsVbo = buffers[0];
    mCellCentersVbo = buffers[1];
    mCellTexCoordsVbo = buffers[2];
    mCellColorsVbo = buffers[3];
    mCellIndicesIbo = buffers[4];
    mFoodLocalVbo = buffers[5];
    mFoodWorldVbo = buffers[6];
    mFoodAttributesVbo = buffers[7];
    mDeadLocalVbo = buffers[8];
    mDeadWorldVbo = buffers[9];
    mDeadAttributesVbo = buffers[10];
    mLinkCoordsVbo = buffers[11];
    mLinkTexCoordsVbo = buffers[12];
    uploadStaticQuad(gl, mFoodLocalVbo);
    uploadStaticQuad(gl, mDeadLocalVbo);
}

void SimulationWorldGpuRenderer::deleteGlBuffers(NativeGlApi& gl) {
    int buffers[13] = {mCellCoordsVbo, mCellCentersVbo, mCellTexCoordsVbo,
            mCellColorsVbo, mCellIndicesIbo, mFoodLocalVbo,
            mFoodWorldVbo, mFoodAttributesVbo, mDeadLocalVbo,
            mDeadWorldVbo, mDeadAttributesVbo, mLinkCoordsVbo,
            mLinkTexCoordsVbo};
    gl.deleteBuffers(13, buffers);
    abandonGlBuffers();
}

void SimulationWorldGpuRenderer::abandonGlBuffers() noexcept {
    mCellCoordsVbo = 0;
    mCellCentersVbo = 0;
    mCellTexCoordsVbo = 0;
    mCellColorsVbo = 0;
    mCellIndicesIbo = 0;
    mFoodLocalVbo = 0;
    mFoodWorldVbo = 0;
    mFoodAttributesVbo = 0;
    mDeadLocalVbo = 0;
    mDeadWorldVbo = 0;
    mDeadAttributesVbo = 0;
    mLinkCoordsVbo = 0;
    mLinkTexCoordsVbo = 0;
    invalidateUploadedFrame();
}

void SimulationWorldGpuRenderer::invalidateUploadedFrame() noexcept {
    mUploadedFrameValid = false;
    mUploadedFrame = {};
}

bool SimulationWorldGpuRenderer::isFrameUploaded(
        const PreparedRenderFrame& frame) const noexcept {
    return mUploadedFrameValid && mUploadedFrame.frameAddress == reinterpret_cast<std::uintptr_t>(&frame) && mUploadedFrame.layoutVersion == frame.layout.version && mUploadedFrame.worldEpoch == frame.identity.worldEpoch && mUploadedFrame.modeGeneration == frame.identity.modeGeneration && mUploadedFrame.captureSequence == frame.identity.captureSequence && mUploadedFrame.geometryRevision == frame.layout.geometryRevision && mUploadedFrame.instanceRevision == frame.layout.instanceRevision;
}

void SimulationWorldGpuRenderer::rememberUploadedFrame(
        const PreparedRenderFrame& frame) noexcept {
    mUploadedFrame.frameAddress = reinterpret_cast<std::uintptr_t>(&frame);
    mUploadedFrame.layoutVersion = frame.layout.version;
    mUploadedFrame.worldEpoch = frame.identity.worldEpoch;
    mUploadedFrame.modeGeneration = frame.identity.modeGeneration;
    mUploadedFrame.captureSequence = frame.identity.captureSequence;
    mUploadedFrame.geometryRevision = frame.layout.geometryRevision;
    mUploadedFrame.instanceRevision = frame.layout.instanceRevision;
    mUploadedFrameValid = true;
}

void SimulationWorldGpuRenderer::uploadStaticQuad(NativeGlApi& gl, int bufferId) {
    static constexpr float quad[12] = {1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    gl.bindBuffer(gl.arrayBuffer(), bufferId);
    gl.bufferData(gl.arrayBuffer(), 12 * FLOAT_BYTES, quad, gl.staticDraw());
    gl.bindBuffer(gl.arrayBuffer(), 0);
}

void SimulationWorldGpuRenderer::uploadFloatRange(NativeGlApi& gl, int bufferId,
        const std::vector<float>& source,
        int floatCount) {
    const int sizeBytes = checkedUploadByteCount(
            floatCount, FLOAT_BYTES,
            "SimulationWorldGpuRenderer float upload exceeds NativeGlApi range");
    if (static_cast<std::size_t>(floatCount) > source.size()) {
        throw std::length_error(
                "SimulationWorldGpuRenderer float upload exceeds source storage");
    }
    gl.bindBuffer(gl.arrayBuffer(), bufferId);
    gl.bufferData(gl.arrayBuffer(), sizeBytes, nullptr, gl.streamDraw());
    if (floatCount > 0) {
        gl.bufferSubData(gl.arrayBuffer(), 0, sizeBytes, source.data());
    }
}

void SimulationWorldGpuRenderer::uploadShortRange(
        NativeGlApi& gl, int bufferId, const std::vector<unsigned short>& source,
        int shortCount) {
    const int sizeBytes = checkedUploadByteCount(
            shortCount, SHORT_BYTES,
            "SimulationWorldGpuRenderer index upload exceeds NativeGlApi range");
    if (static_cast<std::size_t>(shortCount) > source.size()) {
        throw std::length_error(
                "SimulationWorldGpuRenderer index upload exceeds source storage");
    }
    gl.bindBuffer(gl.elementArrayBuffer(), bufferId);
    gl.bufferData(gl.elementArrayBuffer(), sizeBytes, nullptr, gl.streamDraw());
    if (shortCount > 0) {
        gl.bufferSubData(gl.elementArrayBuffer(), 0, sizeBytes, source.data());
    }
}

bool SimulationWorldGpuRenderer::uploadDynamicBuffers(
        NativeGlApi& gl, const PreparedRenderFrame& frame) {
    if (!frame.geometryReady) {
        throw std::logic_error(
                "SimulationWorldGpuRenderer cannot upload a frame before geometry is ready");
    }
    if (!hasGlBuffers()) {
        createGlBuffers(gl);
    }
    if (isFrameUploaded(frame)) {
        return false;
    }
    invalidateUploadedFrame();

    uploadFloatRange(
            gl, mCellCoordsVbo, frame.cellTriangleCoords,
            checkedIntProduct(frame.cellDrawVertexCount, 2,
                    "SimulationWorldGpuRenderer triangle upload count overflow"));
    uploadFloatRange(gl, mCellCentersVbo, frame.cellCenterCoords,
            frame.cellCenterFloatCount);
    uploadFloatRange(
            gl, mCellTexCoordsVbo, frame.cellTextureCoords,
            checkedIntProduct(frame.cellDrawVertexCount, 4,
                    "SimulationWorldGpuRenderer texture upload count overflow"));
    uploadFloatRange(
            gl, mCellColorsVbo, frame.cellColors,
            checkedIntProduct(frame.cellDrawVertexCount, 4,
                    "SimulationWorldGpuRenderer color upload count overflow"));
    uploadShortRange(gl, mCellIndicesIbo, frame.cellIndices,
            frame.cellDrawIndexCount);

    const int foodFloatCount =
            checkedIntProduct(frame.foodInstanceCount, 4,
                    "SimulationWorldGpuRenderer Food upload count overflow");
    uploadFloatRange(gl, mFoodWorldVbo, frame.foodInstanceAttributes,
            foodFloatCount);
    uploadFloatRange(gl, mFoodAttributesVbo, frame.foodVertexAttributes,
            foodFloatCount);

    const int deadFloatCount = checkedIntProduct(
            frame.deadCellInstanceCount, 4,
            "SimulationWorldGpuRenderer dead-splash upload count overflow");
    uploadFloatRange(gl, mDeadWorldVbo, frame.deadCellInstanceAttributes,
            deadFloatCount);
    uploadFloatRange(gl, mDeadAttributesVbo, frame.deadCellVertexAttributes,
            deadFloatCount);

    uploadFloatRange(
            gl, mLinkCoordsVbo, frame.linkTriangleCoords,
            checkedIntProduct(frame.drawLinksCount, 4,
                    "SimulationWorldGpuRenderer link upload count overflow"));
    uploadFloatRange(gl, mLinkTexCoordsVbo, frame.linkTextureCoords,
            frame.drawLinksCount);

    unbindBuffers(gl);
    rememberUploadedFrame(frame);
    return true;
}

void SimulationWorldGpuRenderer::unbindBuffers(NativeGlApi& gl) {
    gl.bindBuffer(gl.arrayBuffer(), 0);
    gl.bindBuffer(gl.elementArrayBuffer(), 0);
}

void SimulationWorldGpuRenderer::drawCells(
        NativeGlApi& gl,
        const std::array<CellShader*, CellTypes::length>& basicCellShaders,
        CellShader* arrowShader, int dotTextureHandle, bool isPreviewMode,
        const PreparedRenderFrame& frame) {
    drawCellsWithMatrix(gl, basicCellShaders, arrowShader, dotTextureHandle,
            isPreviewMode, frame, frame.viewport.mvpMatrix);
}

void SimulationWorldGpuRenderer::drawCellsWithMatrix(
        NativeGlApi& gl,
        const std::array<CellShader*, CellTypes::length>& basicCellShaders,
        CellShader* arrowShader, int dotTextureHandle, bool isPreviewMode,
        const PreparedRenderFrame& frame, const std::array<float, 16>& mvpMatrix) {
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length;
            cellTypeIndex++) {
        CellShader* shader = basicCellShaders[cellTypeIndex];
        if (shader == nullptr || frame.cellIndexCount[cellTypeIndex] == 0) {
            continue;
        }
        shader->begin(mvpMatrix, dotTextureHandle);
        for (int bunch = 0; bunch < frame.bunches[cellTypeIndex]; bunch++) {
            int start = frame.bunchStart[cellTypeIndex][bunch];
            shader->drawElementsVbo(
                    mCellCoordsVbo, start * 2 * FLOAT_BYTES, mCellTexCoordsVbo,
                    start * 4 * FLOAT_BYTES, mCellCentersVbo, start * 2 * FLOAT_BYTES,
                    mCellColorsVbo, start * 4 * FLOAT_BYTES, mCellIndicesIbo,
                    frame.bunchIndexStart[cellTypeIndex][bunch] * SHORT_BYTES,
                    frame.bunchIndexSize[cellTypeIndex][bunch], 2);
        }
        shader->end();
    }

    if (isPreviewMode && arrowShader != nullptr) {
        int dataIndex = PREVIEW_EXTRA_CELL_TYPE_INDEX;
        if (frame.cellIndexCount[dataIndex] > 0) {
            gl.enable(gl.blend());
            arrowShader->begin(mvpMatrix, dotTextureHandle);
            for (int bunch = 0; bunch < frame.bunches[dataIndex]; bunch++) {
                int start = frame.bunchStart[dataIndex][bunch];
                arrowShader->drawElementsVbo(
                        mCellCoordsVbo, start * 2 * FLOAT_BYTES, mCellTexCoordsVbo,
                        start * 4 * FLOAT_BYTES, mCellCentersVbo, start * 2 * FLOAT_BYTES,
                        mCellColorsVbo, start * 4 * FLOAT_BYTES, mCellIndicesIbo,
                        frame.bunchIndexStart[dataIndex][bunch] * SHORT_BYTES,
                        frame.bunchIndexSize[dataIndex][bunch], 2);
            }
            arrowShader->end();
        }
    }
}

void SimulationWorldGpuRenderer::drawCellOutlines(
        const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
        bool isPreviewMode, const PreparedRenderFrame& frame) {
    drawCellOutlinesWithMatrix(outlineCellShaders, isPreviewMode, frame,
            frame.viewport.mvpMatrix);
}

void SimulationWorldGpuRenderer::drawCellOutlinesWithMatrix(
        const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
        bool isPreviewMode, const PreparedRenderFrame& frame,
        const std::array<float, 16>& mvpMatrix) {
    int shift = CellTypes::length + (isPreviewMode ? 1 : 0);
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length;
            cellTypeIndex++) {
        CellShader* shader = outlineCellShaders[cellTypeIndex];
        int dataIndex = shift + cellTypeIndex;
        if (shader == nullptr || frame.cellVertexCount[dataIndex] == 0) {
            continue;
        }
        shader->begin(mvpMatrix, 0);
        int chunk = 0;
        while (frame.cellVertexCount[dataIndex] > chunk * DRAW_BUNCH) {
            int start = frame.cellVertexCountBefore[dataIndex] + chunk * DRAW_BUNCH;
            int num = std::min(frame.cellVertexCount[dataIndex] - chunk * DRAW_BUNCH,
                    DRAW_BUNCH);
            shader->drawArraysVbo(mCellCoordsVbo, start * 2 * FLOAT_BYTES,
                    mCellTexCoordsVbo, start * 4 * FLOAT_BYTES,
                    mCellCentersVbo, start * 4 * FLOAT_BYTES,
                    mCellColorsVbo, start * 4 * FLOAT_BYTES, num, 4);
            chunk++;
        }
        shader->end();
    }
}

void SimulationWorldGpuRenderer::drawLinks(LinkShader& linkShader,
        const PreparedRenderFrame& frame) {
    drawLinksWithMatrix(linkShader, frame, frame.viewport.mvpMatrix);
}

void SimulationWorldGpuRenderer::drawLinksWithMatrix(
        LinkShader& linkShader, const PreparedRenderFrame& frame,
        const std::array<float, 16>& mvpMatrix) {
    linkShader.begin(mvpMatrix);
    int chunk = 0;
    while (frame.drawLinksCount > chunk * DRAW_BUNCH) {
        int start = chunk * DRAW_BUNCH;
        int num = std::min(frame.drawLinksCount - start, DRAW_BUNCH);
        linkShader.drawChunkVbo(mLinkCoordsVbo, start * 4 * FLOAT_BYTES,
                mLinkTexCoordsVbo, start * FLOAT_BYTES, num);
        chunk++;
    }
    linkShader.end();
}

void SimulationWorldGpuRenderer::drawFoods(FoodShader& foodShader,
        const PreparedRenderFrame& frame) {
    drawFoodsWithMatrix(foodShader, frame, frame.viewport.mvpMatrix);
}

void SimulationWorldGpuRenderer::drawFoodsWithMatrix(
        FoodShader& foodShader, const PreparedRenderFrame& frame,
        const std::array<float, 16>& mvpMatrix) {
    foodShader.begin(mvpMatrix);
    if (frame.foodInstanceCount > 0) {
        foodShader.drawInstancedVbo(mFoodLocalVbo, mFoodWorldVbo,
                mFoodAttributesVbo, frame.foodInstanceCount);
    }
    foodShader.end();
}

void SimulationWorldGpuRenderer::drawDeadCells(DeadSplashShader& deadSplashShader,
        int besselTextureHandle,
        const PreparedRenderFrame& frame) {
    drawDeadCellsWithMatrix(deadSplashShader, besselTextureHandle, frame,
            frame.viewport.mvpMatrix);
}

void SimulationWorldGpuRenderer::drawDeadCellsWithMatrix(
        DeadSplashShader& deadSplashShader, int besselTextureHandle,
        const PreparedRenderFrame& frame, const std::array<float, 16>& mvpMatrix) {
    deadSplashShader.begin(mvpMatrix, besselTextureHandle);
    if (frame.deadCellInstanceCount > 0) {
        deadSplashShader.drawInstancedVbo(mDeadLocalVbo, mDeadWorldVbo,
                mDeadAttributesVbo,
                frame.deadCellInstanceCount);
    }
    deadSplashShader.end();
}

void SimulationWorldGpuRenderer::drawMarkers(NativeGlApi& gl,
        MarkerShader* markerShader,
        const std::vector<RenderMarker>& markers,
        const std::array<float, 16>& mvpMatrix) {
    if (markerShader == nullptr || markers.empty()) {
        return;
    }

    gl.disable(gl.depthTest());
    gl.enable(gl.blend());
    gl.blendFunc(gl.srcAlpha(), gl.oneMinusSrcAlpha());
    for (const RenderMarker& marker : markers) {
        markerShader->draw(mvpMatrix, marker.x, marker.y, marker.radius,
                marker.alpha);
    }
}

void SimulationWorldGpuRenderer::draw(
        NativeGlApi& gl,
        const std::array<CellShader*, CellTypes::length>& basicCellShaders,
        const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
        CellShader* arrowShader, BackgroundShader& backgroundShader,
        FoodShader& foodShader, DeadSplashShader& deadSplashShader,
        LinkShader& linkShader, MarkerShader* markerShader, int dotTextureHandle,
        int besselTextureHandle,
        const PreparedRenderFrame& frame,
        const RenderPresentationState& presentation) {
    if (!frame.valid || !frame.geometryReady) {
        throw std::logic_error(
                "SimulationWorldGpuRenderer cannot draw an unpublished frame");
    }
    const RenderEnvironmentState& environment = frame.environment;
    const auto& mvpMatrix = presentation.viewport.mvpMatrix;

    gl.disable(gl.depthTest());
    gl.depthMask(false);
    gl.disable(gl.blend());
    backgroundShader.draw(mvpMatrix, environment.lightAngle,
            environment.lightRange, environment.lightIntensity,
            environment.dishRadius, presentation.backgroundColor, environment.dishInnerRadius);

    drawMarkers(gl, markerShader, frame.markers, mvpMatrix);

    gl.blendFunc(gl.srcAlpha(), gl.oneMinusSrcAlpha());
    gl.enable(gl.blend());
    drawFoodsWithMatrix(foodShader, frame, mvpMatrix);
    drawLinksWithMatrix(linkShader, frame, mvpMatrix);
    gl.enable(gl.depthTest());
    gl.depthFunc(gl.always());
    drawDeadCellsWithMatrix(deadSplashShader, besselTextureHandle, frame,
            mvpMatrix);
    gl.depthMask(true);
    drawCellsWithMatrix(gl, basicCellShaders, arrowShader, dotTextureHandle,
            false, frame, mvpMatrix);
    gl.enable(gl.depthTest());
    gl.depthFunc(gl.less());
    gl.depthMask(false);
    drawCellOutlinesWithMatrix(outlineCellShaders, false, frame, mvpMatrix);
    gl.disable(gl.blend());
    unbindBuffers(gl);
}

void SimulationWorldGpuRenderer::drawPreview(
        NativeGlApi& gl,
        const std::array<CellShader*, CellTypes::length>& basicCellShaders,
        const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
        CellShader* arrowShader, DeadSplashShader& deadSplashShader,
        LinkShader& linkShader, MarkerShader* markerShader, int dotTextureHandle,
        int besselTextureHandle, const PreparedRenderFrame& frame,
        bool drawLinksEnabled) {
    RenderPresentationState presentation;
    presentation.viewport = frame.viewport;
    presentation.backgroundColor = frame.environment.backgroundColor;
    drawPreview(gl, basicCellShaders, outlineCellShaders, arrowShader,
            deadSplashShader, linkShader, markerShader, dotTextureHandle,
            besselTextureHandle, frame, presentation, drawLinksEnabled);
}

void SimulationWorldGpuRenderer::drawPreview(
        NativeGlApi& gl,
        const std::array<CellShader*, CellTypes::length>& basicCellShaders,
        const std::array<CellShader*, CellTypes::length>& outlineCellShaders,
        CellShader* arrowShader, DeadSplashShader& deadSplashShader,
        LinkShader& linkShader, MarkerShader* markerShader, int dotTextureHandle,
        int besselTextureHandle, const PreparedRenderFrame& frame,
        const RenderPresentationState& presentation, bool drawLinksEnabled) {
    if (!frame.valid || !frame.geometryReady) {
        throw std::logic_error(
                "SimulationWorldGpuRenderer cannot draw an unpublished preview frame");
    }
    const auto& mvpMatrix = presentation.viewport.mvpMatrix;
    gl.disable(gl.depthTest());
    gl.depthMask(false);
    drawMarkers(gl, markerShader, frame.markers, mvpMatrix);
    gl.enable(gl.blend());
    gl.blendFunc(gl.srcAlpha(), gl.oneMinusSrcAlpha());
    gl.depthFunc(gl.always());
    drawDeadCellsWithMatrix(deadSplashShader, besselTextureHandle, frame,
            mvpMatrix);
    gl.enable(gl.depthTest());
    gl.disable(gl.blend());
    gl.depthMask(true);
    drawCellsWithMatrix(gl, basicCellShaders, arrowShader, dotTextureHandle, true,
            frame, mvpMatrix);
    if (drawLinksEnabled) {
        gl.enable(gl.blend());
        gl.blendFunc(gl.srcAlpha(), gl.oneMinusSrcAlpha());
        drawLinksWithMatrix(linkShader, frame, mvpMatrix);
        gl.disable(gl.blend());
    }
    gl.enable(gl.depthTest());
    gl.depthFunc(gl.less());
    gl.enable(gl.blend());
    gl.depthMask(false);
    drawCellOutlinesWithMatrix(outlineCellShaders, true, frame, mvpMatrix);
    unbindBuffers(gl);
}

} // namespace cellularity::render
