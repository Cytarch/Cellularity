#include "render/backend/abi/VulkanRenderSurfaceSessionAdapter.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "app/microscope/render/MicroscopeDebugHud.h"
#include "core/SimulationWorldProfile.h"
#include "render/backend/abi/VulkanBackendSession.h"
#include "render/frame/PreparedRenderFrame.h"

namespace cellularity::render::abi {
namespace {

static_assert(kCellTypeBucketCount
        == CELLULARITY_VULKAN_CELL_TYPE_BUCKET_COUNT_V1);
static_assert(kCellDataBucketCount
        == CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1);
static_assert(kMaximumDrawBunches
        == CELLULARITY_VULKAN_MAXIMUM_DRAW_BUNCHES_V1);
static_assert(SimulationWorldProfile::STAGE_COUNT
        == CELLULARITY_VULKAN_STAGE_TIMING_COUNT_V1);
static_assert(sizeof(int) == sizeof(std::int32_t));
static_assert(sizeof(unsigned short) == sizeof(std::uint16_t));
static_assert(std::is_standard_layout_v<RenderMarker>);
static_assert(sizeof(RenderMarker) == sizeof(CellularityVulkanRenderMarkerV1));
static_assert(alignof(RenderMarker)
        == alignof(CellularityVulkanRenderMarkerV1));
static_assert(offsetof(RenderMarker, x)
        == offsetof(CellularityVulkanRenderMarkerV1, x));
static_assert(offsetof(RenderMarker, y)
        == offsetof(CellularityVulkanRenderMarkerV1, y));
static_assert(offsetof(RenderMarker, radius)
        == offsetof(CellularityVulkanRenderMarkerV1, radius));
static_assert(offsetof(RenderMarker, alpha)
        == offsetof(CellularityVulkanRenderMarkerV1, alpha));
static_assert(offsetof(RenderMarker, selectedCellRuntimeId)
        == offsetof(CellularityVulkanRenderMarkerV1,
                selected_cell_runtime_id));

template <typename T>
[[nodiscard]] std::uint64_t elementCount(const std::vector<T>& values) {
    static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
    return static_cast<std::uint64_t>(values.size());
}

[[nodiscard]] CellularityVulkanFloatSpanV1 floatSpan(
        const std::vector<float>& values) noexcept {
    return {values.data(), static_cast<std::uint64_t>(values.size()),
            sizeof(float), alignof(float)};
}

[[nodiscard]] CellularityVulkanPresentationStateV1 presentationView(
        const RenderPresentationState& source) noexcept;

void copyBounds(const RenderViewBounds& source,
        CellularityVulkanViewBoundsV1& destination) noexcept {
    destination.min_x = source.minX;
    destination.max_x = source.maxX;
    destination.min_y = source.minY;
    destination.max_y = source.maxY;
}

void copyViewport(const RenderViewportState& source,
        CellularityVulkanViewportStateV1& destination) noexcept {
    destination.revision = source.revision;
    destination.coverage_revision = source.coverageRevision;
    copyBounds(source.visibleBounds, destination.visible_bounds);
    copyBounds(source.coverageBounds, destination.coverage_bounds);
    std::copy(source.mvpMatrix.begin(), source.mvpMatrix.end(),
            destination.mvp_matrix);
    destination.camera_x = source.cameraX;
    destination.camera_y = source.cameraY;
    destination.zoom = source.zoom;
    destination.view_width = source.viewWidth;
    destination.view_height = source.viewHeight;
    destination.surface_width = source.surfaceWidth;
    destination.surface_height = source.surfaceHeight;
}

CellularityVulkanPresentationStateV1 presentationView(
        const RenderPresentationState& source) noexcept {
    CellularityVulkanPresentationStateV1 destination{};
    destination.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    destination.struct_size = sizeof(destination);
    copyViewport(source.viewport, destination.viewport);
    std::copy(source.backgroundColor.begin(), source.backgroundColor.end(),
            destination.background_color);
    return destination;
}

[[nodiscard]] CellularityPreparedRenderFrameViewV1 preparedFrameView(
        const PreparedRenderFrame& source) {
    if (!source.valid || !source.geometryReady
            || source.layout.version != kPreparedRenderFrameLayoutVersion) {
        throw std::invalid_argument(
                "Vulkan requires a published compatible prepared frame");
    }
    if (source.cellDrawVertexCount < 0 || source.cellDrawIndexCount < 0
            || source.cellCenterFloatCount < 0
            || source.foodInstanceCount < 0
            || source.deadCellInstanceCount < 0
            || source.drawLinksCount < 0) {
        throw std::invalid_argument("Prepared frame has a negative draw range");
    }

    CellularityPreparedRenderFrameViewV1 destination{};
    destination.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    destination.struct_size = sizeof(destination);
    destination.source_identity = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(&source));
    destination.identity.capture_sequence = source.identity.captureSequence;
    destination.identity.world_epoch = source.identity.worldEpoch;
    destination.identity.mode_generation = source.identity.modeGeneration;
    destination.identity.completed_update_id = source.identity.completedUpdateId;
    destination.identity.capture_stage =
            static_cast<std::uint32_t>(source.identity.captureStage);
    destination.environment.revision = source.environment.revision;
    destination.environment.preview_mode = source.environment.previewMode ? 1U : 0U;
    destination.environment.light_angle = source.environment.lightAngle;
    destination.environment.light_range = source.environment.lightRange;
    destination.environment.light_intensity = source.environment.lightIntensity;
    destination.environment.dish_radius = source.environment.dishRadius;
    destination.environment.dish_inner_radius = static_cast<float>(source.environment.dishInnerRadius);
    std::copy(source.environment.backgroundColor.begin(),
            source.environment.backgroundColor.end(),
            destination.environment.background_color);
    copyViewport(source.viewport, destination.viewport);
    destination.resolved_input.last_resolved_input_sequence =
            source.resolvedInput.lastResolvedInputSequence;
    destination.resolved_input.applied_world_revision =
            source.resolvedInput.appliedWorldRevision;
    destination.timeline.capture_requested_nanos =
            source.timeline.captureRequestedNanos;
    destination.timeline.source_captured_nanos =
            source.timeline.sourceCapturedNanos;
    destination.timeline.frame_prepared_nanos =
            source.timeline.framePreparedNanos;
    destination.layout.version = source.layout.version;
    destination.layout.geometry_revision = source.layout.geometryRevision;
    destination.layout.instance_revision = source.layout.instanceRevision;
    destination.layout.marker_revision = source.layout.markerRevision;
    destination.interpolation.mode =
            static_cast<std::uint32_t>(source.interpolation.mode);
    destination.interpolation.source_capture_sequence =
            source.interpolation.sourceCaptureSequence;
    destination.interpolation.target_capture_sequence =
            source.interpolation.targetCaptureSequence;
    destination.interpolation.alpha = source.interpolation.alpha;
    destination.diagnostics.build_nanoseconds = source.diagnostics.buildNanoseconds;
    destination.diagnostics.source_cell_count = source.diagnostics.sourceCellCount;
    destination.diagnostics.source_food_count = source.diagnostics.sourceFoodCount;
    destination.diagnostics.source_dead_cell_count =
            source.diagnostics.sourceDeadCellCount;
    destination.diagnostics.source_topology_edge_count =
            source.diagnostics.sourceTopologyEdgeCount;
    destination.diagnostics.visible_cell_count =
            source.diagnostics.visibleCellCount;
    destination.diagnostics.normalized_contact_chord_count =
            source.diagnostics.normalizedContactChordCount;

    destination.markers = {
            reinterpret_cast<const CellularityVulkanRenderMarkerV1*>(
                    source.markers.data()),
            elementCount(source.markers), sizeof(RenderMarker),
            alignof(RenderMarker)
    };
    destination.cell_triangle_coords = floatSpan(source.cellTriangleCoords);
    destination.cell_texture_coords = floatSpan(source.cellTextureCoords);
    destination.cell_center_coords = floatSpan(source.cellCenterCoords);
    destination.cell_colors = floatSpan(source.cellColors);
    destination.cell_indices = {source.cellIndices.data(),
            elementCount(source.cellIndices), sizeof(std::uint16_t),
            alignof(std::uint16_t)};
    destination.food_instance_attributes =
            floatSpan(source.foodInstanceAttributes);
    destination.food_vertex_attributes =
            floatSpan(source.foodVertexAttributes);
    destination.dead_cell_instance_attributes =
            floatSpan(source.deadCellInstanceAttributes);
    destination.dead_cell_vertex_attributes =
            floatSpan(source.deadCellVertexAttributes);
    destination.link_triangle_coords = floatSpan(source.linkTriangleCoords);
    destination.link_texture_coords = floatSpan(source.linkTextureCoords);

    const auto intSpan = [](const auto& values) {
        return CellularityVulkanInt32SpanV1{
                reinterpret_cast<const std::int32_t*>(values.data()),
                static_cast<std::uint64_t>(values.size()),
                sizeof(std::int32_t), alignof(std::int32_t)
        };
    };
    destination.cell_vertex_count_before = intSpan(source.cellVertexCountBefore);
    destination.cell_vertex_count = intSpan(source.cellVertexCount);
    destination.cell_index_count_before = intSpan(source.cellIndexCountBefore);
    destination.cell_index_count = intSpan(source.cellIndexCount);
    destination.bunches = intSpan(source.bunches);

    const auto matrix = [](const auto& values) {
        static_assert(sizeof(values[0])
                == sizeof(std::int32_t) * kMaximumDrawBunches);
        return CellularityVulkanInt32MatrixViewV1{
                reinterpret_cast<const std::int32_t*>(values[0].data()),
                static_cast<std::uint32_t>(values.size()),
                static_cast<std::uint32_t>(values[0].size()),
                static_cast<std::uint32_t>(values[0].size()),
                sizeof(std::int32_t), alignof(std::int32_t), 0
        };
    };
    destination.bunch_start = matrix(source.bunchStart);
    destination.bunch_size = matrix(source.bunchSize);
    destination.bunch_index_start = matrix(source.bunchIndexStart);
    destination.bunch_index_size = matrix(source.bunchIndexSize);
    destination.cell_draw_vertex_count = source.cellDrawVertexCount;
    destination.cell_draw_index_count = source.cellDrawIndexCount;
    destination.cell_center_float_count = source.cellCenterFloatCount;
    destination.food_instance_count = source.foodInstanceCount;
    destination.dead_cell_instance_count = source.deadCellInstanceCount;
    destination.draw_links_count = source.drawLinksCount;
    return destination;
}

[[nodiscard]] CellularityVulkanDebugHudViewV1 debugHudView(
        const cellularity::microscope::MicroscopeDebugHudTelemetry& source) noexcept {
    CellularityVulkanDebugHudViewV1 destination{};
    destination.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    destination.struct_size = sizeof(destination);
    destination.rate_sample_sequence = source.rateSampleSequence;
    destination.actual_rate_valid = source.actualRateValid ? 1U : 0U;
    destination.actual_completed_ups = source.actualCompletedUps;
    destination.stage_timing_sequence = source.stageTimings.sequence;
    destination.stage_nanos = source.stageTimings.stageNanos.data();
    destination.stage_count = source.stageTimings.stageNanos.size();
    destination.stage_element_size = sizeof(double);
    destination.total_nanos = source.stageTimings.totalNanos;
    return destination;
}

[[nodiscard]] CellularityVulkanOffscreenTargetV1 offscreenTarget(
        OffscreenRenderTarget target) {
    switch (target) {
        case OffscreenRenderTarget::GenomePreview:
            return CELLULARITY_VULKAN_OFFSCREEN_GENOME_PREVIEW_V1;
        case OffscreenRenderTarget::SubstratePreview:
            return CELLULARITY_VULKAN_OFFSCREEN_SUBSTRATE_PREVIEW_V1;
    }
    throw std::invalid_argument("Unknown offscreen render target");
}

[[nodiscard]] std::size_t offscreenIndex(OffscreenRenderTarget target) {
    const auto index = static_cast<std::size_t>(target);
    if (index >= 2)
        throw std::invalid_argument("Unknown offscreen target");
    return index;
}

[[nodiscard]] RenderFrameResult frameResult(
        CellularityVulkanSurfaceOutcomeV1 outcome) {
    switch (outcome) {
        case CELLULARITY_VULKAN_SURFACE_PRESENTED_V1:
            return makeRenderFrameResult(RenderFrameOutcome::Presented,
                    kPrimaryContentPresented);
        case CELLULARITY_VULKAN_SURFACE_DEFERRED_V1:
            return makeRenderFrameResult(RenderFrameOutcome::Deferred);
        case CELLULARITY_VULKAN_SURFACE_SWAPCHAIN_RECREATED_V1:
            return makeRenderFrameResult(RenderFrameOutcome::SwapchainRecreated);
        case CELLULARITY_VULKAN_SURFACE_LOST_V1:
            return makeRenderFrameResult(RenderFrameOutcome::SurfaceLost);
        case CELLULARITY_VULKAN_SURFACE_SKIPPED_NOT_READY_V1:
            return makeRenderFrameResult(RenderFrameOutcome::SkippedNotReady);
        case CELLULARITY_VULKAN_SURFACE_FAILED_V1:
            return makeRenderFrameResult(RenderFrameOutcome::Failed);
        default:
            throw std::runtime_error(
                    "Vulkan backend returned an unknown surface outcome");
    }
}

void requireOperation(CellularityVulkanOperationStatusV1 status,
        const char* operation) {
    if (status == CELLULARITY_VULKAN_OPERATION_OK_V1)
        return;
    if (!isKnownVulkanOperationStatus(status)) {
        throw std::runtime_error(std::string(operation)
                + " returned an unknown ABI status");
    }
    throw std::runtime_error(std::string(operation) + " failed");
}

[[nodiscard]] RenderPassStatus passStatus(
        CellularityVulkanPassStatusV1 status) {
    if (status == CELLULARITY_VULKAN_PASS_COMPLETED_V1) {
        return RenderPassStatus::Completed;
    }
    if (status == CELLULARITY_VULKAN_PASS_FAILED_V1) {
        return RenderPassStatus::Failed;
    }
    throw std::runtime_error("Vulkan backend returned an unknown pass status");
}

} // namespace

VulkanRenderSurfaceSessionAdapter::VulkanRenderSurfaceSessionAdapter(
        std::shared_ptr<VulkanBackendSession> session)
        : mSession(std::move(session)) {
    if (mSession == nullptr) {
        throw std::invalid_argument("Vulkan adapter requires a session");
    }
}

RenderBackendKind VulkanRenderSurfaceSessionAdapter::kind() const noexcept {
    return RenderBackendKind::Vulkan;
}

const void* VulkanRenderSurfaceSessionAdapter::bindingIdentity()
        const noexcept {
    return mSession->bindingIdentity();
}

bool VulkanRenderSurfaceSessionAdapter::hasResources() const noexcept {
    std::uint32_t attached = 0;
    const auto status = mSession->hasAttachedSurface(attached);
    return status == CELLULARITY_VULKAN_OPERATION_OK_V1 && attached == 1;
}

bool VulkanRenderSurfaceSessionAdapter::hasGpuBuffers() const noexcept {
    return false;
}

void VulkanRenderSurfaceSessionAdapter::attachSurface(
        bool, const NativeRenderAssets&, NativeRenderTextureLoader&) {
    // Physical Vulkan surface lifecycle is owned by the render worker.
}

void VulkanRenderSurfaceSessionAdapter::resizeSurface(int, int) {
    // See attachSurface(). Controller viewport notifications are independent.
}

void VulkanRenderSurfaceSessionAdapter::detachSurface() {
}

void VulkanRenderSurfaceSessionAdapter::abandonSurface() noexcept {
}

RenderFrameResult VulkanRenderSurfaceSessionAdapter::presentMicroscope(
        const RenderPresentationState& presentation,
        const PreparedRenderFrame& frame,
        const cellularity::microscope::MicroscopeDebugHudTelemetry* debugHud) {
    const auto abiPresentation = presentationView(presentation);
    const auto abiFrame = preparedFrameView(frame);
    CellularityVulkanDebugHudViewV1 abiHud{};
    const CellularityVulkanDebugHudViewV1* hud = nullptr;
    if (debugHud != nullptr) {
        abiHud = debugHudView(*debugHud);
        hud = &abiHud;
    }
    return frameResult(
            mSession->presentMicroscope(abiPresentation, abiFrame, hud));
}

RenderPassStatus
VulkanRenderSurfaceSessionAdapter::drawMicroscopeOffscreenPass(
        const RenderPresentationState& presentation,
        const PreparedRenderFrame& frame) {
    const auto abiPresentation = presentationView(presentation);
    const auto abiFrame = preparedFrameView(frame);
    return passStatus(mSession->drawMicroscopeOffscreenPass(
            abiPresentation, abiFrame));
}

RenderFrameResult VulkanRenderSurfaceSessionAdapter::clearSurface(
        const RenderViewportState& viewport,
        const std::array<float, 4>& color) {
    CellularityVulkanViewportStateV1 abiViewport{};
    copyViewport(viewport, abiViewport);
    return frameResult(mSession->clearSurface(abiViewport, color.data()));
}

RenderFrameResult
VulkanRenderSurfaceSessionAdapter::drawEnvironmentFieldPreview(
        const EnvironmentFieldPreviewCommand& command) {
    CellularityVulkanEnvironmentFieldPreviewCommandV1 abi{};
    abi.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    abi.struct_size = sizeof(abi);
    std::copy(command.matrix.begin(), command.matrix.end(), abi.matrix);
    abi.surface_width = command.surfaceWidth;
    abi.surface_height = command.surfaceHeight;
    abi.dish_radius = command.dishRadius;
    abi.field_type = command.fieldType;
    abi.vector_preview = command.vectorPreview ? 1U : 0U;
    abi.field_base = command.fieldBase;
    abi.field_strength = command.fieldStrength;
    abi.field_angle = command.fieldAngle;
    abi.field_offset = command.fieldOffset;
    abi.field_softness = command.fieldSoftness;
    abi.display_min = command.displayMin;
    abi.display_max = command.displayMax;
    abi.gravity_field_type = command.gravityFieldType;
    abi.gravity_field_angle = command.gravityFieldAngle;
    return frameResult(mSession->drawEnvironmentFieldPreview(abi));
}

void VulkanRenderSurfaceSessionAdapter::beginGenomePreviewFrame(
        int width, int height, bool fullyTransparentClear) {
    requireOperation(mSession->beginGenomeFrame(width, height,
                             fullyTransparentClear ? 1U : 0U),
            "begin genome frame");
}

void VulkanRenderSurfaceSessionAdapter::drawGenomePreviewGrid(
        const GenomePreviewGridCommand& command) {
    CellularityVulkanGenomeGridCommandV1 abi{};
    abi.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    abi.struct_size = sizeof(abi);
    std::copy(command.matrix.begin(), command.matrix.end(), abi.matrix);
    abi.min_x = command.minX;
    abi.max_x = command.maxX;
    abi.min_y = command.minY;
    abi.max_y = command.maxY;
    abi.world_radius = command.worldRadius;
    abi.grid_size = command.gridSize;
    requireOperation(mSession->drawGenomeGrid(abi), "draw genome grid");
}

void VulkanRenderSurfaceSessionAdapter::drawGenomePreviewTree(
        const RenderPresentationState& presentation,
        const PreparedRenderFrame& frame,
        const std::array<float, 16>& matrix,
        const std::vector<float>& lineVertices,
        const GenomePreviewDrawOptions& options) {
    const auto abiPresentation = presentationView(presentation);
    const auto abiFrame = preparedFrameView(frame);
    CellularityVulkanGenomeDrawOptionsV1 abiOptions{
            CELLULARITY_VULKAN_ABI_VERSION_V1,
            sizeof(CellularityVulkanGenomeDrawOptionsV1),
            options.drawDirectionArrow ? 1U : 0U,
            options.drawMarkers ? 1U : 0U,
            options.drawLinks ? 1U : 0U,
            0
    };
    requireOperation(mSession->drawGenomeTree(abiPresentation, abiFrame,
                             matrix.data(), floatSpan(lineVertices), abiOptions),
            "draw genome tree");
}

void VulkanRenderSurfaceSessionAdapter::drawGenomePreview(
        const RenderPresentationState& presentation,
        const PreparedRenderFrame& frame,
        const GenomePreviewDrawOptions& options) {
    const auto abiPresentation = presentationView(presentation);
    const auto abiFrame = preparedFrameView(frame);
    CellularityVulkanGenomeDrawOptionsV1 abiOptions{
            CELLULARITY_VULKAN_ABI_VERSION_V1,
            sizeof(CellularityVulkanGenomeDrawOptionsV1),
            options.drawDirectionArrow ? 1U : 0U,
            options.drawMarkers ? 1U : 0U,
            options.drawLinks ? 1U : 0U,
            0
    };
    requireOperation(mSession->drawGenomeCells(
                             abiPresentation, abiFrame, abiOptions),
            "draw genome cells");
}

RenderFrameResult VulkanRenderSurfaceSessionAdapter::endGenomePreviewFrame() {
    return frameResult(mSession->endGenomeSurfaceFrame());
}

RenderPassStatus
VulkanRenderSurfaceSessionAdapter::endGenomePreviewOffscreenPass() {
    return passStatus(mSession->endGenomeOffscreenPass());
}

void VulkanRenderSurfaceSessionAdapter::cancelGenomePreviewFrame() noexcept {
    (void)mSession->cancelGenomeFrame();
}

void VulkanRenderSurfaceSessionAdapter::beginOffscreenRgba(
        OffscreenRenderTarget target, int width, int height) {
    requireOperation(mSession->beginOffscreenRgba(
                             offscreenTarget(target), width, height),
            "begin offscreen RGBA");
    mOffscreenDimensions[offscreenIndex(target)] = {width, height};
}

std::vector<std::uint8_t>
VulkanRenderSurfaceSessionAdapter::finishOffscreenRgba(
        OffscreenRenderTarget target) {
    const auto [width, height] = mOffscreenDimensions[offscreenIndex(target)];
    if (width <= 0 || height <= 0) {
        throw std::logic_error("Offscreen RGBA target has no active dimensions");
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width)
            * static_cast<std::uint64_t>(height);
    if (pixels > std::numeric_limits<std::size_t>::max() / 4U) {
        throw std::overflow_error("Offscreen RGBA size overflows");
    }
    std::vector<std::uint8_t> result(static_cast<std::size_t>(pixels * 4U));
    std::uint64_t written = 0;
    const auto status = mSession->finishOffscreenRgba(offscreenTarget(target),
            result.data(), result.size(), written);
    if (passStatus(status) != RenderPassStatus::Completed
            || written != result.size()) {
        throw std::runtime_error("Vulkan offscreen RGBA readback failed");
    }
    mOffscreenDimensions[offscreenIndex(target)] = {};
    return result;
}

void VulkanRenderSurfaceSessionAdapter::cancelOffscreenRgba(
        OffscreenRenderTarget target) noexcept {
    CellularityVulkanOffscreenTargetV1 abiTarget =
            CELLULARITY_VULKAN_OFFSCREEN_GENOME_PREVIEW_V1;
    std::size_t index = 0;
    if (target == OffscreenRenderTarget::SubstratePreview) {
        abiTarget = CELLULARITY_VULKAN_OFFSCREEN_SUBSTRATE_PREVIEW_V1;
        index = 1;
    }
    mOffscreenDimensions[index] = {};
    (void)mSession->cancelOffscreenRgba(abiTarget);
}

} // namespace cellularity::render::abi
