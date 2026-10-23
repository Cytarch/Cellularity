#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/CellType.h"
#include "render/frame/PreparedRenderFrame.h"
#include "render/frame/RenderFrameBuilder.h"
#include "render/frame/RenderSourceCapture.h"
#include "render/frame/RenderSourceFrame.h"

class SimulationWorld;

namespace cellularity::tests {
struct NativeSimulationWorldRendererTestAccess;
} // namespace cellularity::tests

class NativeSimulationWorldRenderer {
public:
    static constexpr int PREVIEW_EXTRA_CELL_TYPE_INDEX =
            cellularity::render::kPreviewExtraCellTypeIndex;
    static constexpr int CELL_TYPE_BUCKET_COUNT =
            cellularity::render::kCellTypeBucketCount;
    static constexpr int CELL_DATA_BUCKET_COUNT =
            cellularity::render::kCellDataBucketCount;
    static constexpr int MAX_BUNCHES = cellularity::render::kMaximumDrawBunches;

    using ViewBounds = cellularity::render::RenderViewBounds;
    using MarkerCaptureRequest =
            cellularity::render::RenderMarkerCaptureRequest;
    using CaptureMetadata = cellularity::render::RenderFrameCaptureMetadata;
    using PresentationState = cellularity::render::RenderPresentationState;
    using PreparedFrame = cellularity::render::PreparedRenderFrame;

    using CopyToDrawProfile = cellularity::render::RenderFrameBuilder::Profile;

    explicit NativeSimulationWorldRenderer(int maxCellCount);

    void reset(int maxCellCount);
    int copyToDraw(const SimulationWorld& world, const ViewBounds& viewBounds);
    int copyToDraw(const SimulationWorld& world,
            const ViewBounds& viewBounds,
            const CaptureMetadata& captureMetadata);
    int copyToDraw(const SimulationWorld& world,
            const ViewBounds& viewBounds,
            const CaptureMetadata& captureMetadata,
            const MarkerCaptureRequest* markerRequests,
            std::size_t markerRequestCount);
    CopyToDrawProfile profileCopyToDraw(const SimulationWorld& world, const ViewBounds& viewBounds);
    const PreparedFrame& preparedFrame() const;
    const PreparedFrame& publishPreparedFrame(
            const PresentationState& presentation);
    int visibleCellCount() const;
    int visibleCellCountForBucket(int cellTypeIndex) const;
    int visibleCellIndex(int cellTypeIndex, int localIndex) const;
    int cellDrawVertexCount() const;
    int cellDrawIndexCount() const;
    int cellCenterFloatCount() const;
    int cellVertexCountBefore(int dataIndex) const;
    int cellVertexCount(int dataIndex) const;
    int cellIndexCountBefore(int dataIndex) const;
    int cellIndexCount(int dataIndex) const;
    int foodInstanceCount() const;
    int deadCellInstanceCount() const;
    int drawLinksCount() const;
    const std::vector<float>& cellTriangleCoords() const;
    const std::vector<float>& cellTextureCoords() const;
    const std::vector<float>& cellCenterCoords() const;
    const std::vector<float>& cellColors() const;
    const std::vector<unsigned short>& cellIndices() const;
    const std::vector<float>& foodInstanceAttributes() const;
    const std::vector<float>& foodVertexAttributes() const;
    const std::vector<float>& deadCellInstanceAttributes() const;
    const std::vector<float>& deadCellVertexAttributes() const;
    const std::vector<float>& linkTriangleCoords() const;
    const std::vector<float>& linkTextureCoords() const;
    void captureDrawSnapshot(PreparedFrame& snapshot, bool ghostTint);
    void restoreDrawSnapshot(const PreparedFrame& snapshot);

private:
    friend struct cellularity::tests::NativeSimulationWorldRendererTestAccess;

    cellularity::render::RenderSourceFrame mSourceFrame;
    cellularity::render::RenderSourceCapture mSourceCapture;
    PreparedFrame mPreparedFrame;
    cellularity::render::RenderFrameBuilder mFrameBuilder;
};
