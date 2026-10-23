#ifndef CELLULARITY_VULKAN_BACKEND_ABI_H
#define CELLULARITY_VULKAN_BACKEND_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define CELLULARITY_VULKAN_BACKEND_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define CELLULARITY_VULKAN_BACKEND_EXPORT \
    __attribute__((visibility("default")))
#else
#define CELLULARITY_VULKAN_BACKEND_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct AAssetManager;
struct ANativeWindow;
struct CellularityVulkanSessionOpaque;

typedef struct CellularityVulkanSessionOpaque*
        CellularityVulkanSessionHandle;

#define CELLULARITY_VULKAN_ABI_VERSION_V1 UINT32_C(1)
#define CELLULARITY_VULKAN_CELL_TYPE_BUCKET_COUNT_V1 UINT32_C(19)
#define CELLULARITY_VULKAN_CELL_DATA_BUCKET_COUNT_V1 UINT32_C(37)
#define CELLULARITY_VULKAN_MAXIMUM_DRAW_BUNCHES_V1 UINT32_C(201)
#define CELLULARITY_VULKAN_STAGE_TIMING_COUNT_V1 UINT32_C(22)
#define CELLULARITY_VULKAN_DIAGNOSTICS_CHUNK_BYTES_V1 UINT32_C(65536)

typedef uint32_t CellularityVulkanOperationStatusV1;
#define CELLULARITY_VULKAN_OPERATION_OK_V1 UINT32_C(0)
#define CELLULARITY_VULKAN_OPERATION_INVALID_ARGUMENT_V1 UINT32_C(1)
#define CELLULARITY_VULKAN_OPERATION_UNSUPPORTED_V1 UINT32_C(2)
#define CELLULARITY_VULKAN_OPERATION_FAILED_V1 UINT32_C(3)

typedef uint32_t CellularityVulkanSurfaceOutcomeV1;
#define CELLULARITY_VULKAN_SURFACE_PRESENTED_V1 UINT32_C(0)
#define CELLULARITY_VULKAN_SURFACE_DEFERRED_V1 UINT32_C(1)
#define CELLULARITY_VULKAN_SURFACE_SWAPCHAIN_RECREATED_V1 UINT32_C(2)
#define CELLULARITY_VULKAN_SURFACE_LOST_V1 UINT32_C(3)
#define CELLULARITY_VULKAN_SURFACE_SKIPPED_NOT_READY_V1 UINT32_C(4)
#define CELLULARITY_VULKAN_SURFACE_FAILED_V1 UINT32_C(5)

typedef uint32_t CellularityVulkanPassStatusV1;
#define CELLULARITY_VULKAN_PASS_COMPLETED_V1 UINT32_C(0)
#define CELLULARITY_VULKAN_PASS_FAILED_V1 UINT32_C(1)

typedef uint32_t CellularityVulkanDiagnosticsStatusV1;
#define CELLULARITY_VULKAN_DIAGNOSTICS_OK_V1 UINT32_C(0)
#define CELLULARITY_VULKAN_DIAGNOSTICS_BUSY_V1 UINT32_C(1)
#define CELLULARITY_VULKAN_DIAGNOSTICS_STALE_REVISION_V1 UINT32_C(2)
#define CELLULARITY_VULKAN_DIAGNOSTICS_INVALID_ARGUMENT_V1 UINT32_C(3)
#define CELLULARITY_VULKAN_DIAGNOSTICS_OUT_OF_MEMORY_V1 UINT32_C(4)
#define CELLULARITY_VULKAN_DIAGNOSTICS_FAILED_V1 UINT32_C(5)
#define CELLULARITY_VULKAN_DIAGNOSTICS_REVISION_EXHAUSTED_V1 UINT32_C(6)

typedef uint32_t CellularityVulkanErrorCodeV1;
#define CELLULARITY_VULKAN_ERROR_NONE_V1 UINT32_C(0)
#define CELLULARITY_VULKAN_ERROR_INVALID_ARGUMENT_V1 UINT32_C(1)
#define CELLULARITY_VULKAN_ERROR_ASSET_UNAVAILABLE_V1 UINT32_C(2)
#define CELLULARITY_VULKAN_ERROR_OUT_OF_MEMORY_V1 UINT32_C(3)
#define CELLULARITY_VULKAN_ERROR_BACKEND_INITIALIZATION_FAILED_V1 UINT32_C(4)
#define CELLULARITY_VULKAN_ERROR_INTERNAL_FAILURE_V1 UINT32_C(5)
#define CELLULARITY_VULKAN_ERROR_MESSAGE_TRUNCATED_V1 UINT32_C(1)

typedef uint32_t CellularityVulkanOffscreenTargetV1;
#define CELLULARITY_VULKAN_OFFSCREEN_GENOME_PREVIEW_V1 UINT32_C(0)
#define CELLULARITY_VULKAN_OFFSCREEN_SUBSTRATE_PREVIEW_V1 UINT32_C(1)

typedef struct CellularityVulkanSessionCreateInfoV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    struct AAssetManager* asset_manager;
    uint32_t flags;
    uint32_t reserved_zero;
} CellularityVulkanSessionCreateInfoV1;

#define CELLULARITY_VULKAN_SESSION_CREATE_INFO_V1_REQUIRED_SIZE    \
    (offsetof(CellularityVulkanSessionCreateInfoV1, reserved_zero) \
            + sizeof(uint32_t))

typedef struct CellularityVulkanErrorV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    CellularityVulkanErrorCodeV1 code;
    uint32_t flags;
    int32_t backend_code;
    uint32_t message_length;
    char message[512];
} CellularityVulkanErrorV1;

typedef struct CellularityVulkanFloatSpanV1 {
    const float* data;
    uint64_t element_count;
    uint32_t element_size;
    uint32_t element_alignment;
} CellularityVulkanFloatSpanV1;

typedef struct CellularityVulkanUint16SpanV1 {
    const uint16_t* data;
    uint64_t element_count;
    uint32_t element_size;
    uint32_t element_alignment;
} CellularityVulkanUint16SpanV1;

typedef struct CellularityVulkanInt32SpanV1 {
    const int32_t* data;
    uint64_t element_count;
    uint32_t element_size;
    uint32_t element_alignment;
} CellularityVulkanInt32SpanV1;

typedef struct CellularityVulkanInt32MatrixViewV1 {
    const int32_t* data;
    uint32_t rows;
    uint32_t columns;
    uint32_t row_stride_elements;
    uint32_t element_size;
    uint32_t element_alignment;
    uint32_t reserved_zero;
} CellularityVulkanInt32MatrixViewV1;

typedef struct CellularityVulkanRenderMarkerV1 {
    float x;
    float y;
    float radius;
    float alpha;
    uint64_t selected_cell_runtime_id;
} CellularityVulkanRenderMarkerV1;

typedef struct CellularityVulkanMarkerSpanV1 {
    const CellularityVulkanRenderMarkerV1* data;
    uint64_t element_count;
    uint32_t element_size;
    uint32_t element_alignment;
} CellularityVulkanMarkerSpanV1;

typedef struct CellularityVulkanViewBoundsV1 {
    float min_x;
    float max_x;
    float min_y;
    float max_y;
} CellularityVulkanViewBoundsV1;

typedef struct CellularityVulkanViewportStateV1 {
    uint64_t revision;
    uint64_t coverage_revision;
    CellularityVulkanViewBoundsV1 visible_bounds;
    CellularityVulkanViewBoundsV1 coverage_bounds;
    float mvp_matrix[16];
    float camera_x;
    float camera_y;
    float zoom;
    float view_width;
    float view_height;
    int32_t surface_width;
    int32_t surface_height;
} CellularityVulkanViewportStateV1;

typedef struct CellularityVulkanPresentationStateV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    CellularityVulkanViewportStateV1 viewport;
    float background_color[3];
    uint32_t reserved_zero;
} CellularityVulkanPresentationStateV1;

typedef struct CellularityVulkanFrameIdentityV1 {
    uint64_t capture_sequence;
    uint64_t world_epoch;
    uint64_t mode_generation;
    uint64_t completed_update_id;
    uint32_t capture_stage;
    uint32_t reserved_zero;
} CellularityVulkanFrameIdentityV1;

typedef struct CellularityVulkanEnvironmentStateV1 {
    int64_t revision;
    uint32_t preview_mode;
    uint32_t reserved_zero;
    double light_angle;
    double light_range;
    double light_intensity;
    double dish_radius;
    float background_color[3];
    // Uses the former reserved float without shifting any ABI fields. Older
    // backends accept circular frames and reject nonzero values safely.
    float dish_inner_radius;
} CellularityVulkanEnvironmentStateV1;

typedef struct CellularityVulkanResolvedInputStateV1 {
    uint64_t last_resolved_input_sequence;
    uint64_t applied_world_revision;
} CellularityVulkanResolvedInputStateV1;

typedef struct CellularityVulkanFrameTimelineV1 {
    int64_t capture_requested_nanos;
    int64_t source_captured_nanos;
    int64_t frame_prepared_nanos;
} CellularityVulkanFrameTimelineV1;

typedef struct CellularityVulkanFrameLayoutMetadataV1 {
    uint32_t version;
    uint32_t reserved_zero;
    uint64_t geometry_revision;
    uint64_t instance_revision;
    uint64_t marker_revision;
} CellularityVulkanFrameLayoutMetadataV1;

typedef struct CellularityVulkanFrameInterpolationMetadataV1 {
    uint32_t mode;
    uint32_t reserved_zero;
    uint64_t source_capture_sequence;
    uint64_t target_capture_sequence;
    float alpha;
    uint32_t reserved_tail_zero;
} CellularityVulkanFrameInterpolationMetadataV1;

typedef struct CellularityVulkanFrameDiagnosticsV1 {
    uint64_t build_nanoseconds;
    uint64_t source_cell_count;
    uint64_t source_food_count;
    uint64_t source_dead_cell_count;
    uint64_t source_topology_edge_count;
    uint64_t visible_cell_count;
    uint64_t normalized_contact_chord_count;
} CellularityVulkanFrameDiagnosticsV1;

typedef struct CellularityPreparedRenderFrameViewV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t source_identity;
    CellularityVulkanFrameIdentityV1 identity;
    CellularityVulkanEnvironmentStateV1 environment;
    CellularityVulkanViewportStateV1 viewport;
    CellularityVulkanResolvedInputStateV1 resolved_input;
    CellularityVulkanFrameTimelineV1 timeline;
    CellularityVulkanFrameLayoutMetadataV1 layout;
    CellularityVulkanFrameInterpolationMetadataV1 interpolation;
    CellularityVulkanFrameDiagnosticsV1 diagnostics;
    CellularityVulkanMarkerSpanV1 markers;
    CellularityVulkanFloatSpanV1 cell_triangle_coords;
    CellularityVulkanFloatSpanV1 cell_texture_coords;
    CellularityVulkanFloatSpanV1 cell_center_coords;
    CellularityVulkanFloatSpanV1 cell_colors;
    CellularityVulkanUint16SpanV1 cell_indices;
    CellularityVulkanFloatSpanV1 food_instance_attributes;
    CellularityVulkanFloatSpanV1 food_vertex_attributes;
    CellularityVulkanFloatSpanV1 dead_cell_instance_attributes;
    CellularityVulkanFloatSpanV1 dead_cell_vertex_attributes;
    CellularityVulkanFloatSpanV1 link_triangle_coords;
    CellularityVulkanFloatSpanV1 link_texture_coords;
    CellularityVulkanInt32SpanV1 cell_vertex_count_before;
    CellularityVulkanInt32SpanV1 cell_vertex_count;
    CellularityVulkanInt32SpanV1 cell_index_count_before;
    CellularityVulkanInt32SpanV1 cell_index_count;
    CellularityVulkanInt32MatrixViewV1 bunch_start;
    CellularityVulkanInt32MatrixViewV1 bunch_size;
    CellularityVulkanInt32MatrixViewV1 bunch_index_start;
    CellularityVulkanInt32MatrixViewV1 bunch_index_size;
    CellularityVulkanInt32SpanV1 bunches;
    int32_t cell_draw_vertex_count;
    int32_t cell_draw_index_count;
    int32_t cell_center_float_count;
    int32_t food_instance_count;
    int32_t dead_cell_instance_count;
    int32_t draw_links_count;
    uint32_t reserved_zero;
} CellularityPreparedRenderFrameViewV1;

typedef struct CellularityVulkanEnvironmentFieldPreviewCommandV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    float matrix[16];
    int32_t surface_width;
    int32_t surface_height;
    double dish_radius;
    int32_t field_type;
    uint32_t vector_preview;
    double field_base;
    double field_strength;
    double field_angle;
    double field_offset;
    double field_softness;
    double display_min;
    double display_max;
    int32_t gravity_field_type;
    uint32_t reserved_zero;
    double gravity_field_angle;
} CellularityVulkanEnvironmentFieldPreviewCommandV1;

typedef struct CellularityVulkanGenomeGridCommandV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    float matrix[16];
    float min_x;
    float max_x;
    float min_y;
    float max_y;
    double world_radius;
    int32_t grid_size;
    uint32_t reserved_zero;
} CellularityVulkanGenomeGridCommandV1;

typedef struct CellularityVulkanGenomeDrawOptionsV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t draw_direction_arrow;
    uint32_t draw_markers;
    uint32_t draw_links;
    uint32_t reserved_zero;
} CellularityVulkanGenomeDrawOptionsV1;

typedef struct CellularityVulkanDebugHudViewV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t rate_sample_sequence;
    uint32_t actual_rate_valid;
    uint32_t reserved_zero;
    double actual_completed_ups;
    uint64_t stage_timing_sequence;
    const double* stage_nanos;
    uint32_t stage_count;
    uint32_t stage_element_size;
    double total_nanos;
} CellularityVulkanDebugHudViewV1;

typedef void (*CellularityVulkanRetainApiFnV1)(void* api_context);
typedef void (*CellularityVulkanReleaseApiFnV1)(void* api_context);
typedef CellularityVulkanSessionHandle (*CellularityVulkanCreateSessionFnV1)(
        const CellularityVulkanSessionCreateInfoV1* info,
        CellularityVulkanErrorV1* error);
typedef void (*CellularityVulkanDestroySessionFnV1)(
        CellularityVulkanSessionHandle session);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanHasAttachedSurfaceFnV1)(
        CellularityVulkanSessionHandle session,
        uint32_t* out_attached);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanAttachWindowFnV1)(
        CellularityVulkanSessionHandle session,
        struct ANativeWindow* window,
        int32_t width,
        int32_t height);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanResizeSurfaceFnV1)(
        CellularityVulkanSessionHandle session,
        int32_t width,
        int32_t height);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanDetachSurfaceFnV1)(
        CellularityVulkanSessionHandle session);
typedef CellularityVulkanSurfaceOutcomeV1 (*CellularityVulkanPresentMicroscopeFnV1)(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanPresentationStateV1* presentation,
        const CellularityPreparedRenderFrameViewV1* frame,
        const CellularityVulkanDebugHudViewV1* debug_hud);
typedef CellularityVulkanSurfaceOutcomeV1 (*CellularityVulkanClearSurfaceFnV1)(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanViewportStateV1* viewport,
        const float color[4]);
typedef CellularityVulkanSurfaceOutcomeV1 (*CellularityVulkanDrawEnvironmentFieldPreviewFnV1)(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanEnvironmentFieldPreviewCommandV1* command);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanBeginGenomeFrameFnV1)(
        CellularityVulkanSessionHandle session,
        int32_t width,
        int32_t height,
        uint32_t fully_transparent_clear);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanDrawGenomeGridFnV1)(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanGenomeGridCommandV1* command);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanDrawGenomeTreeFnV1)(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanPresentationStateV1* presentation,
        const CellularityPreparedRenderFrameViewV1* frame,
        const float matrix[16],
        CellularityVulkanFloatSpanV1 line_vertices,
        const CellularityVulkanGenomeDrawOptionsV1* options);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanDrawGenomeCellsFnV1)(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanPresentationStateV1* presentation,
        const CellularityPreparedRenderFrameViewV1* frame,
        const CellularityVulkanGenomeDrawOptionsV1* options);
typedef CellularityVulkanSurfaceOutcomeV1 (*CellularityVulkanEndGenomeSurfaceFrameFnV1)(
        CellularityVulkanSessionHandle session);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanCancelGenomeFrameFnV1)(
        CellularityVulkanSessionHandle session);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanBeginOffscreenRgbaFnV1)(
        CellularityVulkanSessionHandle session,
        CellularityVulkanOffscreenTargetV1 target,
        int32_t width,
        int32_t height);
typedef CellularityVulkanPassStatusV1 (*CellularityVulkanDrawMicroscopeOffscreenPassFnV1)(
        CellularityVulkanSessionHandle session,
        const CellularityVulkanPresentationStateV1* presentation,
        const CellularityPreparedRenderFrameViewV1* frame);
typedef CellularityVulkanPassStatusV1 (*CellularityVulkanEndGenomeOffscreenPassFnV1)(
        CellularityVulkanSessionHandle session);
typedef CellularityVulkanPassStatusV1 (*CellularityVulkanFinishOffscreenRgbaFnV1)(
        CellularityVulkanSessionHandle session,
        CellularityVulkanOffscreenTargetV1 target,
        uint8_t* destination,
        uint64_t destination_size,
        uint64_t* out_bytes_written);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanCancelOffscreenRgbaFnV1)(
        CellularityVulkanSessionHandle session,
        CellularityVulkanOffscreenTargetV1 target);
typedef CellularityVulkanOperationStatusV1 (*CellularityVulkanSetProfilingEnabledFnV1)(
        CellularityVulkanSessionHandle session,
        uint32_t enabled);
typedef CellularityVulkanDiagnosticsStatusV1 (*CellularityVulkanGetDiagnosticsSizeFnV1)(
        CellularityVulkanSessionHandle session,
        uint64_t* out_revision,
        uint64_t* out_total_bytes);
typedef CellularityVulkanDiagnosticsStatusV1 (*CellularityVulkanReadDiagnosticsChunkFnV1)(
        CellularityVulkanSessionHandle session,
        uint64_t revision,
        uint64_t offset,
        uint8_t* destination,
        uint32_t destination_size,
        uint32_t* out_bytes_written);
typedef CellularityVulkanDiagnosticsStatusV1 (*CellularityVulkanReleaseDiagnosticsSnapshotFnV1)(
        CellularityVulkanSessionHandle session,
        uint64_t revision);

typedef struct CellularityVulkanSurfaceApiV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void* api_context;
    CellularityVulkanRetainApiFnV1 retain_api;
    CellularityVulkanReleaseApiFnV1 release_api;
    CellularityVulkanCreateSessionFnV1 create_session;
    CellularityVulkanDestroySessionFnV1 destroy_session;
    CellularityVulkanHasAttachedSurfaceFnV1 has_attached_surface;
    CellularityVulkanAttachWindowFnV1 attach_window;
    CellularityVulkanResizeSurfaceFnV1 resize_surface;
    CellularityVulkanDetachSurfaceFnV1 detach_surface;
    CellularityVulkanPresentMicroscopeFnV1 present_microscope;
    CellularityVulkanClearSurfaceFnV1 clear_surface;
    CellularityVulkanDrawEnvironmentFieldPreviewFnV1
            draw_environment_field_preview;
    CellularityVulkanBeginGenomeFrameFnV1 begin_genome_frame;
    CellularityVulkanDrawGenomeGridFnV1 draw_genome_grid;
    CellularityVulkanDrawGenomeTreeFnV1 draw_genome_tree;
    CellularityVulkanDrawGenomeCellsFnV1 draw_genome_cells;
    CellularityVulkanEndGenomeSurfaceFrameFnV1 end_genome_surface_frame;
    CellularityVulkanCancelGenomeFrameFnV1 cancel_genome_frame;
    CellularityVulkanBeginOffscreenRgbaFnV1 begin_offscreen_rgba;
    CellularityVulkanDrawMicroscopeOffscreenPassFnV1
            draw_microscope_offscreen_pass;
    CellularityVulkanEndGenomeOffscreenPassFnV1 end_genome_offscreen_pass;
    CellularityVulkanFinishOffscreenRgbaFnV1 finish_offscreen_rgba;
    CellularityVulkanCancelOffscreenRgbaFnV1 cancel_offscreen_rgba;
    CellularityVulkanSetProfilingEnabledFnV1 set_profiling_enabled;
    CellularityVulkanGetDiagnosticsSizeFnV1 get_diagnostics_size;
    CellularityVulkanReadDiagnosticsChunkFnV1 read_diagnostics_chunk;
    CellularityVulkanReleaseDiagnosticsSnapshotFnV1
            release_diagnostics_snapshot;
} CellularityVulkanSurfaceApiV1;

#define CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE                    \
    (offsetof(CellularityVulkanSurfaceApiV1, release_diagnostics_snapshot) \
            + sizeof(CellularityVulkanReleaseDiagnosticsSnapshotFnV1))

typedef const CellularityVulkanSurfaceApiV1* (*CellularityVulkanGetSurfaceApiV1Fn)(void);

CELLULARITY_VULKAN_BACKEND_EXPORT const CellularityVulkanSurfaceApiV1*
cellularity_vulkan_get_surface_api_v1(void);

#ifdef __cplusplus
}
#endif

#endif
