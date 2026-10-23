#include "render/backend/abi/CellularityVulkanBackendAbi.h"

#include <android/native_window.h>
#include <jni.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "app/microscope/render/MicroscopeDebugHud.h"
#include "platform/android/AndroidRenderAssets.h"
#include "render/AssetRenderTextureLoader.h"
#include "render/backend/RenderFrameResult.h"
#include "render/backend/abi/VulkanBackendAbiError.h"
#include "render/backend/abi/VulkanBackendAbiValidation.h"
#include "render/backend/vulkan/AndroidVulkanClearFrameSession.h"
#include "render/backend/vulkan/VulkanBasicCellFrame.h"
#include "render/frame/PreparedRenderFrameView.h"

namespace {

using cellularity::render::EnvironmentFieldPreviewCommand;
using cellularity::render::GenomePreviewDrawOptions;
using cellularity::render::GenomePreviewGridCommand;
using cellularity::render::OffscreenRenderTarget;
using cellularity::render::PreparedFrameSpan;
using cellularity::render::PreparedRenderFrameView;
using cellularity::render::RenderCaptureStage;
using cellularity::render::RenderFrameOutcome;
using cellularity::render::RenderInterpolationMode;
using cellularity::render::RenderMarker;
using cellularity::render::RenderPassStatus;
using cellularity::render::RenderPresentationState;
using cellularity::render::RenderViewBounds;
using cellularity::render::RenderViewportState;
using cellularity::render::abi::error::initializeError;
using cellularity::render::abi::error::setError;
using cellularity::render::abi::validation::validInt32Matrix;
using cellularity::render::abi::validation::validInt32Span;
using cellularity::render::abi::validation::validSpan;
using cellularity::render::vulkan::AndroidVulkanClearFrameSession;
using cellularity::render::vulkan::kVulkanBasicCellProgramCount;
using cellularity::render::vulkan::kVulkanWorldProgramCount;
using cellularity::render::vulkan::VulkanBasicCellProgram;
using cellularity::render::vulkan::vulkanBasicCellProgramName;
using cellularity::render::vulkan::VulkanCellShaderSpirv;
using cellularity::render::vulkan::VulkanWorldProgram;
using cellularity::render::vulkan::vulkanWorldProgramName;

struct ApiContext final {
    std::atomic<std::uint32_t> leases{0};
};

ApiContext gApiContext;

[[nodiscard]] std::vector<std::uint32_t> readSpirv(
        const NativeRenderAssets& assets, const std::string& path,
        const std::string& label) {
    const std::vector<unsigned char> bytes = assets.readBytes(path);
    if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) {
        throw std::invalid_argument(label + " SPIR-V has an invalid byte length");
    }
    std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    if (words.front() != UINT32_C(0x07230203)) {
        throw std::invalid_argument(label + " asset is not SPIR-V");
    }
    return words;
}

template <std::size_t ProgramCount>
[[nodiscard]] std::array<VulkanCellShaderSpirv, ProgramCount> readShaderSpirv(
        const NativeRenderAssets& assets, const char* groupName,
        const char* (*programName)(std::size_t)) {
    std::array<VulkanCellShaderSpirv, ProgramCount> result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const std::string name = programName(index);
        const std::string prefix = "vulkan/shaders/" + name;
        const std::string label = std::string(groupName) + " " + name;
        result[index].vertex = readSpirv(
                assets, prefix + ".vert.spv", label + " vertex");
        result[index].fragment = readSpirv(
                assets, prefix + ".frag.spv", label + " fragment");
    }
    return result;
}

[[nodiscard]] const char* basicCellProgramName(std::size_t index) noexcept {
    return vulkanBasicCellProgramName(
            static_cast<VulkanBasicCellProgram>(index));
}

[[nodiscard]] const char* worldProgramName(std::size_t index) noexcept {
    return vulkanWorldProgramName(static_cast<VulkanWorldProgram>(index));
}

void copyBounds(const CellularityVulkanViewBoundsV1& source,
        RenderViewBounds& destination) noexcept {
    destination.minX = source.min_x;
    destination.maxX = source.max_x;
    destination.minY = source.min_y;
    destination.maxY = source.max_y;
}

void copyViewport(const CellularityVulkanViewportStateV1& source,
        RenderViewportState& destination) noexcept {
    destination.revision = source.revision;
    destination.coverageRevision = source.coverage_revision;
    copyBounds(source.visible_bounds, destination.visibleBounds);
    copyBounds(source.coverage_bounds, destination.coverageBounds);
    std::copy(std::begin(source.mvp_matrix), std::end(source.mvp_matrix),
            destination.mvpMatrix.begin());
    destination.cameraX = source.camera_x;
    destination.cameraY = source.camera_y;
    destination.zoom = source.zoom;
    destination.viewWidth = source.view_width;
    destination.viewHeight = source.view_height;
    destination.surfaceWidth = source.surface_width;
    destination.surfaceHeight = source.surface_height;
}

[[nodiscard]] bool decodePresentation(
        const CellularityVulkanPresentationStateV1* source,
        RenderPresentationState& destination) noexcept {
    if (source == nullptr
            || source->abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || source->struct_size < sizeof(*source)
            || source->reserved_zero != 0) {
        return false;
    }
    copyViewport(source->viewport, destination.viewport);
    std::copy(std::begin(source->background_color),
            std::end(source->background_color),
            destination.backgroundColor.begin());
    return true;
}

[[nodiscard]] bool decodeFrame(
        const CellularityPreparedRenderFrameViewV1* source,
        PreparedRenderFrameView& destination) noexcept {
    static_assert(sizeof(RenderMarker)
            == sizeof(CellularityVulkanRenderMarkerV1));
    static_assert(alignof(RenderMarker)
            == alignof(CellularityVulkanRenderMarkerV1));
    static_assert(offsetof(RenderMarker, selectedCellRuntimeId)
            == offsetof(CellularityVulkanRenderMarkerV1,
                    selected_cell_runtime_id));
    if (source == nullptr
            || source->abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || source->struct_size < sizeof(*source)
            || source->reserved_zero != 0 || source->source_identity == 0
            || source->source_identity
                    > std::numeric_limits<std::uintptr_t>::max()
            || source->identity.capture_stage
                    > static_cast<std::uint32_t>(
                            RenderCaptureStage::SubstrateFilePreview)
            || source->identity.reserved_zero != 0
            || source->environment.preview_mode > 1
            || source->environment.reserved_zero != 0
            || !std::isfinite(source->environment.dish_inner_radius)
            || source->environment.dish_inner_radius < 0.0
            || (source->environment.dish_inner_radius > 0.0
                    && source->environment.dish_inner_radius >= source->environment.dish_radius)
            || source->layout.version
                    != cellularity::render::kPreparedRenderFrameLayoutVersion
            || source->layout.reserved_zero != 0
            || source->interpolation.mode
                    != static_cast<std::uint32_t>(
                            RenderInterpolationMode::Disabled)
            || source->interpolation.reserved_zero != 0
            || source->interpolation.reserved_tail_zero != 0
            || source->cell_draw_vertex_count < 0
            || source->cell_draw_index_count < 0
            || source->cell_center_float_count < 0
            || source->food_instance_count < 0
            || source->dead_cell_instance_count < 0
            || source->draw_links_count < 0) {
        return false;
    }

    const auto& markers = source->markers;
    const auto floatOk = [](const CellularityVulkanFloatSpanV1& span) {
        return validSpan(span.data, span.element_count, span.element_size,
                span.element_alignment);
    };
    if (!validSpan(markers.data, markers.element_count, markers.element_size,
                markers.element_alignment)
            || !floatOk(source->cell_triangle_coords)
            || !floatOk(source->cell_texture_coords)
            || !floatOk(source->cell_center_coords)
            || !floatOk(source->cell_colors)
            || !validSpan(source->cell_indices.data,
                    source->cell_indices.element_count,
                    source->cell_indices.element_size,
                    source->cell_indices.element_alignment)
            || !floatOk(source->food_instance_attributes)
            || !floatOk(source->food_vertex_attributes)
            || !floatOk(source->dead_cell_instance_attributes)
            || !floatOk(source->dead_cell_vertex_attributes)
            || !floatOk(source->link_triangle_coords)
            || !floatOk(source->link_texture_coords)
            || !validInt32Span(source->cell_vertex_count_before,
                    CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1)
            || !validInt32Span(source->cell_vertex_count,
                    CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1)
            || !validInt32Span(source->cell_index_count_before,
                    CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1)
            || !validInt32Span(source->cell_index_count,
                    CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1)
            || !validInt32Matrix(source->bunch_start)
            || !validInt32Matrix(source->bunch_size)
            || !validInt32Matrix(source->bunch_index_start)
            || !validInt32Matrix(source->bunch_index_size)
            || !validInt32Span(source->bunches,
                    CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1)) {
        return false;
    }

    destination.valid = true;
    destination.geometryReady = true;
    destination.sourceIdentity =
            static_cast<std::uintptr_t>(source->source_identity);
    destination.identity.captureSequence = source->identity.capture_sequence;
    destination.identity.worldEpoch = source->identity.world_epoch;
    destination.identity.modeGeneration = source->identity.mode_generation;
    destination.identity.completedUpdateId =
            source->identity.completed_update_id;
    destination.identity.captureStage =
            static_cast<RenderCaptureStage>(source->identity.capture_stage);
    destination.environment.revision = source->environment.revision;
    destination.environment.previewMode = source->environment.preview_mode != 0;
    destination.environment.lightAngle = source->environment.light_angle;
    destination.environment.lightRange = source->environment.light_range;
    destination.environment.lightIntensity = source->environment.light_intensity;
    destination.environment.dishRadius = source->environment.dish_radius;
    destination.environment.dishInnerRadius = source->environment.dish_inner_radius;
    std::copy(std::begin(source->environment.background_color),
            std::end(source->environment.background_color),
            destination.environment.backgroundColor.begin());
    copyViewport(source->viewport, destination.viewport);
    destination.resolvedInput.lastResolvedInputSequence =
            source->resolved_input.last_resolved_input_sequence;
    destination.resolvedInput.appliedWorldRevision =
            source->resolved_input.applied_world_revision;
    destination.timeline.captureRequestedNanos =
            source->timeline.capture_requested_nanos;
    destination.timeline.sourceCapturedNanos =
            source->timeline.source_captured_nanos;
    destination.timeline.framePreparedNanos =
            source->timeline.frame_prepared_nanos;
    destination.layout.version = source->layout.version;
    destination.layout.geometryRevision = source->layout.geometry_revision;
    destination.layout.instanceRevision = source->layout.instance_revision;
    destination.layout.markerRevision = source->layout.marker_revision;
    destination.interpolation.mode = RenderInterpolationMode::Disabled;
    destination.interpolation.sourceCaptureSequence =
            source->interpolation.source_capture_sequence;
    destination.interpolation.targetCaptureSequence =
            source->interpolation.target_capture_sequence;
    destination.interpolation.alpha = source->interpolation.alpha;
    destination.diagnostics.buildNanoseconds =
            source->diagnostics.build_nanoseconds;
    destination.diagnostics.sourceCellCount =
            source->diagnostics.source_cell_count;
    destination.diagnostics.sourceFoodCount =
            source->diagnostics.source_food_count;
    destination.diagnostics.sourceDeadCellCount =
            source->diagnostics.source_dead_cell_count;
    destination.diagnostics.sourceTopologyEdgeCount =
            source->diagnostics.source_topology_edge_count;
    destination.diagnostics.visibleCellCount =
            source->diagnostics.visible_cell_count;
    destination.diagnostics.normalizedContactChordCount =
            source->diagnostics.normalized_contact_chord_count;

    destination.markers = {
            reinterpret_cast<const RenderMarker*>(markers.data),
            static_cast<std::size_t>(markers.element_count)
    };
#define CELLULARITY_DECODE_FLOAT_SPAN(field, abi_field) \
    destination.field = {source->abi_field.data,        \
            static_cast<std::size_t>(source->abi_field.element_count)}
    CELLULARITY_DECODE_FLOAT_SPAN(cellTriangleCoords, cell_triangle_coords);
    CELLULARITY_DECODE_FLOAT_SPAN(cellTextureCoords, cell_texture_coords);
    CELLULARITY_DECODE_FLOAT_SPAN(cellCenterCoords, cell_center_coords);
    CELLULARITY_DECODE_FLOAT_SPAN(cellColors, cell_colors);
    destination.cellIndices = {source->cell_indices.data,
            static_cast<std::size_t>(source->cell_indices.element_count)};
    CELLULARITY_DECODE_FLOAT_SPAN(
            foodInstanceAttributes, food_instance_attributes);
    CELLULARITY_DECODE_FLOAT_SPAN(
            foodVertexAttributes, food_vertex_attributes);
    CELLULARITY_DECODE_FLOAT_SPAN(
            deadCellInstanceAttributes, dead_cell_instance_attributes);
    CELLULARITY_DECODE_FLOAT_SPAN(
            deadCellVertexAttributes, dead_cell_vertex_attributes);
    CELLULARITY_DECODE_FLOAT_SPAN(linkTriangleCoords, link_triangle_coords);
    CELLULARITY_DECODE_FLOAT_SPAN(linkTextureCoords, link_texture_coords);
#undef CELLULARITY_DECODE_FLOAT_SPAN
    destination.cellVertexCountBefore = {source->cell_vertex_count_before.data,
            static_cast<std::size_t>(
                    source->cell_vertex_count_before.element_count)};
    destination.cellVertexCount = {source->cell_vertex_count.data,
            static_cast<std::size_t>(source->cell_vertex_count.element_count)};
    destination.cellIndexCountBefore = {source->cell_index_count_before.data,
            static_cast<std::size_t>(
                    source->cell_index_count_before.element_count)};
    destination.cellIndexCount = {source->cell_index_count.data,
            static_cast<std::size_t>(source->cell_index_count.element_count)};
    destination.bunchStart = {source->bunch_start.data,
            source->bunch_start.rows, source->bunch_start.columns,
            source->bunch_start.row_stride_elements};
    destination.bunchSize = {source->bunch_size.data,
            source->bunch_size.rows, source->bunch_size.columns,
            source->bunch_size.row_stride_elements};
    destination.bunchIndexStart = {source->bunch_index_start.data,
            source->bunch_index_start.rows,
            source->bunch_index_start.columns,
            source->bunch_index_start.row_stride_elements};
    destination.bunchIndexSize = {source->bunch_index_size.data,
            source->bunch_index_size.rows,
            source->bunch_index_size.columns,
            source->bunch_index_size.row_stride_elements};
    destination.bunches = {source->bunches.data,
            static_cast<std::size_t>(source->bunches.element_count)};
    destination.cellDrawVertexCount = source->cell_draw_vertex_count;
    destination.cellDrawIndexCount = source->cell_draw_index_count;
    destination.cellCenterFloatCount = source->cell_center_float_count;
    destination.foodInstanceCount = source->food_instance_count;
    destination.deadCellInstanceCount = source->dead_cell_instance_count;
    destination.drawLinksCount = source->draw_links_count;
    return true;
}

[[nodiscard]] bool decodeHud(const CellularityVulkanDebugHudViewV1* source,
        cellularity::microscope::MicroscopeDebugHudTelemetry& destination) noexcept {
    if (source == nullptr)
        return false;
    if (source->abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || source->struct_size < sizeof(*source)
            || source->actual_rate_valid > 1 || source->reserved_zero != 0
            || source->stage_count
                    != CELLULARITY_VULKAN_STAGE_TIMING_COUNT_V1
            || !validSpan(source->stage_nanos, source->stage_count,
                    source->stage_element_size, alignof(double))) {
        return false;
    }
    destination.rateSampleSequence = source->rate_sample_sequence;
    destination.actualRateValid = source->actual_rate_valid != 0;
    destination.actualCompletedUps = source->actual_completed_ups;
    destination.stageTimings.sequence = source->stage_timing_sequence;
    std::copy(source->stage_nanos, source->stage_nanos + source->stage_count,
            destination.stageTimings.stageNanos.begin());
    destination.stageTimings.totalNanos = source->total_nanos;
    return true;
}

[[nodiscard]] CellularityVulkanSurfaceOutcomeV1 encodeOutcome(
        const cellularity::render::RenderFrameResult& result) noexcept {
    switch (result.outcome) {
        case RenderFrameOutcome::Presented:
            return CELLULARITY_VULKAN_SURFACE_PRESENTED_V1;
        case RenderFrameOutcome::Deferred:
            return CELLULARITY_VULKAN_SURFACE_DEFERRED_V1;
        case RenderFrameOutcome::SwapchainRecreated:
            return CELLULARITY_VULKAN_SURFACE_SWAPCHAIN_RECREATED_V1;
        case RenderFrameOutcome::SurfaceLost:
            return CELLULARITY_VULKAN_SURFACE_LOST_V1;
        case RenderFrameOutcome::SkippedNotReady:
            return CELLULARITY_VULKAN_SURFACE_SKIPPED_NOT_READY_V1;
        case RenderFrameOutcome::Failed:
            return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    }
    return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
}

[[nodiscard]] bool decodeTarget(CellularityVulkanOffscreenTargetV1 source,
        OffscreenRenderTarget& destination) noexcept {
    if (source == CELLULARITY_VULKAN_OFFSCREEN_GENOME_PREVIEW_V1) {
        destination = OffscreenRenderTarget::GenomePreview;
        return true;
    }
    if (source == CELLULARITY_VULKAN_OFFSCREEN_SUBSTRATE_PREVIEW_V1) {
        destination = OffscreenRenderTarget::SubstratePreview;
        return true;
    }
    return false;
}

} // namespace

struct CellularityVulkanSessionOpaque final {
    explicit CellularityVulkanSessionOpaque(
            std::unique_ptr<AndroidVulkanClearFrameSession> value)
            : implementation(std::move(value)) {}

    std::unique_ptr<AndroidVulkanClearFrameSession> implementation;
    std::string diagnosticsSnapshot;
    std::uint64_t diagnosticsRevision = 0;
    std::uint64_t nextDiagnosticsRevision = 1;
    bool diagnosticsActive = false;
};

namespace {

void retainApi(void* context) noexcept {
    if (context != &gApiContext)
        return;
    gApiContext.leases.fetch_add(1, std::memory_order_relaxed);
}

void releaseApi(void* context) noexcept {
    if (context != &gApiContext)
        return;
    std::uint32_t current = gApiContext.leases.load(std::memory_order_relaxed);
    while (current != 0
            && !gApiContext.leases.compare_exchange_weak(current, current - 1,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

CellularityVulkanSessionHandle createSession(
        const CellularityVulkanSessionCreateInfoV1* info,
        CellularityVulkanErrorV1* error) noexcept {
    initializeError(error);
    if (error == nullptr || info == nullptr
            || info->abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || info->struct_size
                    < CELLULARITY_VULKAN_SESSION_CREATE_INFO_V1_REQUIRED_SIZE
            || info->asset_manager == nullptr || info->flags != 0
            || info->reserved_zero != 0) {
        setError(error, CELLULARITY_VULKAN_ERROR_INVALID_ARGUMENT_V1,
                "Invalid Vulkan session creation request");
        return nullptr;
    }
    try {
        AndroidRenderAssets assets(info->asset_manager);
        AssetRenderTextureLoader textureLoader(assets);
        auto implementation = std::make_unique<AndroidVulkanClearFrameSession>(
                readSpirv(assets, "vulkan/shaders/background.vert.spv",
                        "Background vertex"),
                readSpirv(assets, "vulkan/shaders/background.frag.spv",
                        "Background fragment"),
                readSpirv(assets, "vulkan/shaders/density_field.vert.spv",
                        "Density-field vertex"),
                readSpirv(assets, "vulkan/shaders/density_field.frag.spv",
                        "Density-field fragment"),
                readShaderSpirv<kVulkanBasicCellProgramCount>(
                        assets, "Basic-cell", &basicCellProgramName),
                readShaderSpirv<kVulkanWorldProgramCount>(
                        assets, "World", &worldProgramName),
                textureLoader.loadTexture("small_dots", false),
                textureLoader.loadTexture("gauss_bessel_integral", true));
        return new CellularityVulkanSessionOpaque(std::move(implementation));
    } catch (const std::bad_alloc&) {
        setError(error, CELLULARITY_VULKAN_ERROR_OUT_OF_MEMORY_V1,
                "Out of memory while creating Vulkan backend session");
    } catch (const std::invalid_argument& exception) {
        setError(error, CELLULARITY_VULKAN_ERROR_ASSET_UNAVAILABLE_V1,
                exception.what());
    } catch (const std::exception& exception) {
        setError(error,
                CELLULARITY_VULKAN_ERROR_BACKEND_INITIALIZATION_FAILED_V1,
                exception.what());
    } catch (...) {
        setError(error, CELLULARITY_VULKAN_ERROR_INTERNAL_FAILURE_V1,
                "Unexpected Vulkan backend session creation failure");
    }
    return nullptr;
}

void destroySession(CellularityVulkanSessionHandle session) noexcept {
    try {
        delete session;
    } catch (...) {
    }
}

CellularityVulkanOperationStatusV1 hasAttachedSurface(
        CellularityVulkanSessionHandle session,
        std::uint32_t* outAttached) noexcept {
    if (outAttached != nullptr)
        *outAttached = 0;
    if (session == nullptr || outAttached == nullptr) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        *outAttached = session->implementation->hasAttachedSurface() ? 1U : 0U;
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 attachWindow(
        CellularityVulkanSessionHandle session, ANativeWindow* window,
        std::int32_t width, std::int32_t height) noexcept {
    if (session == nullptr || window == nullptr || width < 0 || height < 0) {
        if (window != nullptr)
            ANativeWindow_release(window);
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        return session->implementation->attachWindow(window, width, height)
                ? CELLULARITY_VULKAN_OPERATION_OK_V1
                : CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    } catch (...) {
        // attachWindow owns and releases the reference on every implementation
        // path after entry.
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 resizeSurface(
        CellularityVulkanSessionHandle session,
        std::int32_t width, std::int32_t height) noexcept {
    if (session == nullptr || width < 0 || height < 0) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        session->implementation->resizeSurface(width, height);
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 detachSurface(
        CellularityVulkanSessionHandle session) noexcept {
    if (session == nullptr) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        session->implementation->detachSurface();
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanSurfaceOutcomeV1 presentMicroscope(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanPresentationStateV1* presentation,
        const CellularityPreparedRenderFrameViewV1* frame,
        const CellularityVulkanDebugHudViewV1* debugHud) noexcept {
    if (session == nullptr)
        return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    try {
        RenderPresentationState decodedPresentation;
        PreparedRenderFrameView decodedFrame;
        if (!decodePresentation(presentation, decodedPresentation)
                || !decodeFrame(frame, decodedFrame)) {
            return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
        }
        cellularity::microscope::MicroscopeDebugHudTelemetry decodedHud;
        const auto* hud = debugHud != nullptr
                        && decodeHud(debugHud, decodedHud)
                ? &decodedHud
                : nullptr;
        if (debugHud != nullptr && hud == nullptr) {
            return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
        }
        return encodeOutcome(session->implementation->renderFrame(
                decodedFrame, decodedPresentation, hud));
    } catch (...) {
        return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    }
}

CellularityVulkanSurfaceOutcomeV1 clearSurface(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanViewportStateV1* viewport,
        const float color[4]) noexcept {
    if (session == nullptr || viewport == nullptr || color == nullptr) {
        return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    }
    try {
        RenderViewportState decoded;
        copyViewport(*viewport, decoded);
        std::array<float, 4> decodedColor{};
        std::copy(color, color + decodedColor.size(), decodedColor.begin());
        return encodeOutcome(
                session->implementation->renderClear(decoded, decodedColor));
    } catch (...) {
        return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    }
}

CellularityVulkanSurfaceOutcomeV1 drawEnvironmentFieldPreview(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanEnvironmentFieldPreviewCommandV1* source) noexcept {
    if (session == nullptr || source == nullptr
            || source->abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || source->struct_size < sizeof(*source)
            || source->vector_preview > 1 || source->reserved_zero != 0) {
        return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    }
    try {
        EnvironmentFieldPreviewCommand command;
        std::copy(std::begin(source->matrix), std::end(source->matrix),
                command.matrix.begin());
        command.surfaceWidth = source->surface_width;
        command.surfaceHeight = source->surface_height;
        command.dishRadius = source->dish_radius;
        command.fieldType = source->field_type;
        command.fieldBase = source->field_base;
        command.fieldStrength = source->field_strength;
        command.fieldAngle = source->field_angle;
        command.fieldOffset = source->field_offset;
        command.fieldSoftness = source->field_softness;
        command.displayMin = source->display_min;
        command.displayMax = source->display_max;
        command.vectorPreview = source->vector_preview != 0;
        command.gravityFieldType = source->gravity_field_type;
        command.gravityFieldAngle = source->gravity_field_angle;
        return encodeOutcome(session->implementation
                        ->renderEnvironmentFieldPreview(command));
    } catch (...) {
        return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 beginGenomeFrame(
        CellularityVulkanSessionHandle session,
        std::int32_t width, std::int32_t height,
        std::uint32_t transparent) noexcept {
    if (session == nullptr || width < 0 || height < 0 || transparent > 1) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        session->implementation->beginGenomePreviewFrame(
                width, height, transparent != 0);
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 drawGenomeGrid(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanGenomeGridCommandV1* source) noexcept {
    if (session == nullptr || source == nullptr
            || source->abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || source->struct_size < sizeof(*source)
            || source->reserved_zero != 0) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        GenomePreviewGridCommand command;
        std::copy(std::begin(source->matrix), std::end(source->matrix),
                command.matrix.begin());
        command.minX = source->min_x;
        command.maxX = source->max_x;
        command.minY = source->min_y;
        command.maxY = source->max_y;
        command.worldRadius = source->world_radius;
        command.gridSize = source->grid_size;
        session->implementation->drawGenomePreviewGrid(command);
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

[[nodiscard]] bool decodeGenomeOptions(
        const CellularityVulkanGenomeDrawOptionsV1* source,
        GenomePreviewDrawOptions& destination) noexcept {
    if (source == nullptr
            || source->abi_version != CELLULARITY_VULKAN_ABI_VERSION_V1
            || source->struct_size < sizeof(*source)
            || source->draw_direction_arrow > 1 || source->draw_markers > 1
            || source->draw_links > 1 || source->reserved_zero != 0) {
        return false;
    }
    destination.drawDirectionArrow = source->draw_direction_arrow != 0;
    destination.drawMarkers = source->draw_markers != 0;
    destination.drawLinks = source->draw_links != 0;
    return true;
}

CellularityVulkanOperationStatusV1 drawGenomeTree(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanPresentationStateV1* presentation,
        const CellularityPreparedRenderFrameViewV1* frame,
        const float matrix[16], CellularityVulkanFloatSpanV1 lineVertices,
        const CellularityVulkanGenomeDrawOptionsV1* options) noexcept {
    if (session == nullptr || matrix == nullptr
            || !validSpan(lineVertices.data, lineVertices.element_count,
                    lineVertices.element_size,
                    lineVertices.element_alignment)) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        RenderPresentationState decodedPresentation;
        PreparedRenderFrameView decodedFrame;
        GenomePreviewDrawOptions decodedOptions;
        if (!decodePresentation(presentation, decodedPresentation)
                || !decodeFrame(frame, decodedFrame)
                || !decodeGenomeOptions(options, decodedOptions)) {
            return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
        }
        std::array<float, 16> decodedMatrix{};
        std::copy(matrix, matrix + decodedMatrix.size(), decodedMatrix.begin());
        session->implementation->drawGenomePreviewTree(decodedPresentation,
                decodedFrame, decodedMatrix,
                {lineVertices.data,
                        static_cast<std::size_t>(lineVertices.element_count)},
                decodedOptions);
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 drawGenomeCells(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanPresentationStateV1* presentation,
        const CellularityPreparedRenderFrameViewV1* frame,
        const CellularityVulkanGenomeDrawOptionsV1* options) noexcept {
    if (session == nullptr) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        RenderPresentationState decodedPresentation;
        PreparedRenderFrameView decodedFrame;
        GenomePreviewDrawOptions decodedOptions;
        if (!decodePresentation(presentation, decodedPresentation)
                || !decodeFrame(frame, decodedFrame)
                || !decodeGenomeOptions(options, decodedOptions)) {
            return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
        }
        session->implementation->drawGenomePreview(
                decodedPresentation, decodedFrame, decodedOptions);
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanSurfaceOutcomeV1 endGenomeSurfaceFrame(
        CellularityVulkanSessionHandle session) noexcept {
    if (session == nullptr)
        return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    try {
        return encodeOutcome(
                session->implementation->endGenomePreviewFrame());
    } catch (...) {
        return CELLULARITY_VULKAN_SURFACE_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 cancelGenomeFrame(
        CellularityVulkanSessionHandle session) noexcept {
    if (session == nullptr) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        session->implementation->cancelGenomePreviewFrame();
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 beginOffscreenRgba(
        CellularityVulkanSessionHandle session,
        CellularityVulkanOffscreenTargetV1 target,
        std::int32_t width, std::int32_t height) noexcept {
    OffscreenRenderTarget decoded;
    if (session == nullptr || !decodeTarget(target, decoded)
            || width <= 0 || height <= 0) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        session->implementation->beginOffscreenRgba(decoded, width, height);
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanPassStatusV1 drawMicroscopeOffscreenPass(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanPresentationStateV1* presentation,
        const CellularityPreparedRenderFrameViewV1* frame) noexcept {
    if (session == nullptr)
        return CELLULARITY_VULKAN_PASS_FAILED_V1;
    try {
        RenderPresentationState decodedPresentation;
        PreparedRenderFrameView decodedFrame;
        if (!decodePresentation(presentation, decodedPresentation)
                || !decodeFrame(frame, decodedFrame)) {
            return CELLULARITY_VULKAN_PASS_FAILED_V1;
        }
        return session->implementation->drawMicroscopeOffscreenPass(
                       decodedFrame, decodedPresentation)
                        == RenderPassStatus::Completed
                ? CELLULARITY_VULKAN_PASS_COMPLETED_V1
                : CELLULARITY_VULKAN_PASS_FAILED_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_PASS_FAILED_V1;
    }
}

CellularityVulkanPassStatusV1 endGenomeOffscreenPass(
        CellularityVulkanSessionHandle session) noexcept {
    if (session == nullptr)
        return CELLULARITY_VULKAN_PASS_FAILED_V1;
    try {
        return session->implementation->endGenomePreviewOffscreenPass()
                        == RenderPassStatus::Completed
                ? CELLULARITY_VULKAN_PASS_COMPLETED_V1
                : CELLULARITY_VULKAN_PASS_FAILED_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_PASS_FAILED_V1;
    }
}

CellularityVulkanPassStatusV1 finishOffscreenRgba(
        CellularityVulkanSessionHandle session,
        CellularityVulkanOffscreenTargetV1 target,
        std::uint8_t* destination, std::uint64_t destinationSize,
        std::uint64_t* outBytesWritten) noexcept {
    if (outBytesWritten != nullptr)
        *outBytesWritten = 0;
    OffscreenRenderTarget decoded;
    if (session == nullptr || !decodeTarget(target, decoded)
            || destination == nullptr || outBytesWritten == nullptr
            || destinationSize > std::numeric_limits<std::size_t>::max()) {
        return CELLULARITY_VULKAN_PASS_FAILED_V1;
    }
    try {
        const std::vector<std::uint8_t> bytes =
                session->implementation->finishOffscreenRgba(decoded);
        if (bytes.size() != destinationSize) {
            return CELLULARITY_VULKAN_PASS_FAILED_V1;
        }
        if (!bytes.empty())
            std::memcpy(destination, bytes.data(), bytes.size());
        *outBytesWritten = bytes.size();
        return CELLULARITY_VULKAN_PASS_COMPLETED_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_PASS_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 cancelOffscreenRgba(
        CellularityVulkanSessionHandle session,
        CellularityVulkanOffscreenTargetV1 target) noexcept {
    OffscreenRenderTarget decoded;
    if (session == nullptr || !decodeTarget(target, decoded)) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        session->implementation->cancelOffscreenRgba(decoded);
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanOperationStatusV1 setProfilingEnabled(
        CellularityVulkanSessionHandle session, std::uint32_t enabled) noexcept {
    if (session == nullptr || enabled > 1) {
        return CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1;
    }
    try {
        session->implementation->setPerformanceProfilingEnabled(enabled != 0);
        return CELLULARITY_VULKAN_OPERATION_OK_V1;
    } catch (...) {
        return CELLULARITY_VULKAN_OPERATION_FAILED_V1;
    }
}

CellularityVulkanDiagnosticsStatusV1 getDiagnosticsSize(
        CellularityVulkanSessionHandle session, std::uint64_t* outRevision,
        std::uint64_t* outTotalBytes) noexcept {
    if (outRevision != nullptr)
        *outRevision = 0;
    if (outTotalBytes != nullptr)
        *outTotalBytes = 0;
    if (session == nullptr || outRevision == nullptr || outTotalBytes == nullptr) {
        return CELLULARITY_VULKAN_DIAGNOSTICS_INVALID_ARGUMENT_V1;
    }
    if (session->diagnosticsActive) {
        return CELLULARITY_VULKAN_DIAGNOSTICS_BUSY_V1;
    }
    if (session->nextDiagnosticsRevision == 0) {
        return CELLULARITY_VULKAN_DIAGNOSTICS_REVISION_EXHAUSTED_V1;
    }
    try {
        session->diagnosticsSnapshot = session->implementation->diagnostics();
        session->diagnosticsRevision = session->nextDiagnosticsRevision;
        if (session->nextDiagnosticsRevision
                == std::numeric_limits<std::uint64_t>::max()) {
            session->nextDiagnosticsRevision = 0;
        } else {
            ++session->nextDiagnosticsRevision;
        }
        session->diagnosticsActive = true;
        *outRevision = session->diagnosticsRevision;
        *outTotalBytes = session->diagnosticsSnapshot.size();
        return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
    } catch (const std::bad_alloc&) {
        session->diagnosticsSnapshot.clear();
        return CELLULARITY_VULKAN_DIAGNOSTICS_OUT_OF_MEMORY_V1;
    } catch (...) {
        session->diagnosticsSnapshot.clear();
        return CELLULARITY_VULKAN_DIAGNOSTICS_FAILED_V1;
    }
}

CellularityVulkanDiagnosticsStatusV1 readDiagnosticsChunk(
        CellularityVulkanSessionHandle session, std::uint64_t revision,
        std::uint64_t offset, std::uint8_t* destination,
        std::uint32_t destinationSize,
        std::uint32_t* outBytesWritten) noexcept {
    if (outBytesWritten != nullptr)
        *outBytesWritten = 0;
    if (session == nullptr || revision == 0 || destination == nullptr
            || outBytesWritten == nullptr || destinationSize == 0
            || destinationSize
                    > CELLULARITY_VULKAN_DIAGNOSTICS_CHUNK_BYTES_V1) {
        return CELLULARITY_VULKAN_DIAGNOSTICS_INVALID_ARGUMENT_V1;
    }
    if (!session->diagnosticsActive
            || session->diagnosticsRevision != revision) {
        return CELLULARITY_VULKAN_DIAGNOSTICS_STALE_REVISION_V1;
    }
    if (offset > session->diagnosticsSnapshot.size()) {
        return CELLULARITY_VULKAN_DIAGNOSTICS_INVALID_ARGUMENT_V1;
    }
    const std::size_t available = session->diagnosticsSnapshot.size()
            - static_cast<std::size_t>(offset);
    const std::size_t count = std::min<std::size_t>(available, destinationSize);
    if (count != 0) {
        std::memcpy(destination,
                session->diagnosticsSnapshot.data()
                        + static_cast<std::size_t>(offset),
                count);
    }
    *outBytesWritten = static_cast<std::uint32_t>(count);
    return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
}

CellularityVulkanDiagnosticsStatusV1 releaseDiagnosticsSnapshot(
        CellularityVulkanSessionHandle session,
        std::uint64_t revision) noexcept {
    if (session == nullptr || revision == 0) {
        return CELLULARITY_VULKAN_DIAGNOSTICS_INVALID_ARGUMENT_V1;
    }
    if (!session->diagnosticsActive
            || session->diagnosticsRevision != revision) {
        return CELLULARITY_VULKAN_DIAGNOSTICS_STALE_REVISION_V1;
    }
    session->diagnosticsActive = false;
    session->diagnosticsRevision = 0;
    session->diagnosticsSnapshot.clear();
    return CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1;
}

[[nodiscard]] CellularityVulkanSurfaceApiV1 makeApi() noexcept {
    CellularityVulkanSurfaceApiV1 api{};
    api.abi_version = CELLULARITY_VULKAN_ABI_VERSION_V1;
    api.struct_size = sizeof(api);
    api.api_context = &gApiContext;
    api.retain_api = &retainApi;
    api.release_api = &releaseApi;
    api.create_session = &createSession;
    api.destroy_session = &destroySession;
    api.has_attached_surface = &hasAttachedSurface;
    api.attach_window = &attachWindow;
    api.resize_surface = &resizeSurface;
    api.detach_surface = &detachSurface;
    api.present_microscope = &presentMicroscope;
    api.clear_surface = &clearSurface;
    api.draw_environment_field_preview = &drawEnvironmentFieldPreview;
    api.begin_genome_frame = &beginGenomeFrame;
    api.draw_genome_grid = &drawGenomeGrid;
    api.draw_genome_tree = &drawGenomeTree;
    api.draw_genome_cells = &drawGenomeCells;
    api.end_genome_surface_frame = &endGenomeSurfaceFrame;
    api.cancel_genome_frame = &cancelGenomeFrame;
    api.begin_offscreen_rgba = &beginOffscreenRgba;
    api.draw_microscope_offscreen_pass = &drawMicroscopeOffscreenPass;
    api.end_genome_offscreen_pass = &endGenomeOffscreenPass;
    api.finish_offscreen_rgba = &finishOffscreenRgba;
    api.cancel_offscreen_rgba = &cancelOffscreenRgba;
    api.set_profiling_enabled = &setProfilingEnabled;
    api.get_diagnostics_size = &getDiagnosticsSize;
    api.read_diagnostics_chunk = &readDiagnosticsChunk;
    api.release_diagnostics_snapshot = &releaseDiagnosticsSnapshot;
    return api;
}

} // namespace

extern "C" __attribute__((visibility("default")))
const CellularityVulkanSurfaceApiV1*
cellularity_vulkan_get_surface_api_v1(void) {
    static const CellularityVulkanSurfaceApiV1 api = makeApi();
    return &api;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_cytarch_cellularity_platform_render_vulkan_NativeVulkanBackendLoader_nativeBackendApiV1Address(
        JNIEnv*, jclass) {
    static_assert(sizeof(std::uintptr_t) <= sizeof(jlong));
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(
            cellularity_vulkan_get_surface_api_v1()));
}
