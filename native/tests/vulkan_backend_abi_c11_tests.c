#include <stddef.h>
#include <stdint.h>

#include "render/backend/abi/CellularityVulkanBackendAbi.h"

#define ASSERT_NEXT_FIELD(struct_type, previous, field, field_type)                                                                                        \
    _Static_assert(offsetof(struct_type, field) == offsetof(struct_type, previous) + sizeof(((struct_type*)0)->previous), "Unexpected " #field " offset"); \
    _Static_assert(sizeof(((struct_type*)0)->field) == sizeof(field_type), "Unexpected " #field " size")

#define ASSERT_CALLBACK_TYPE(field, callback_type) \
    _Static_assert(_Generic(((CellularityVulkanSurfaceApiV1*)0)->field, callback_type: 1, default: 0), "Unexpected " #field " callback type")

_Static_assert(sizeof(CellularityVulkanOperationStatusV1) == 4, "Operation status must be uint32");
_Static_assert(sizeof(CellularityVulkanSurfaceOutcomeV1) == 4, "Surface outcome must be uint32");
_Static_assert(sizeof(CellularityVulkanPassStatusV1) == 4, "Pass status must be uint32");
_Static_assert(sizeof(CellularityVulkanDiagnosticsStatusV1) == 4, "Diagnostics status must be uint32");
_Static_assert(sizeof(CellularityVulkanErrorCodeV1) == 4, "Error code must be uint32");
_Static_assert(sizeof(CellularityVulkanOffscreenTargetV1) == 4, "Offscreen target must be uint32");

_Static_assert(sizeof(CellularityVulkanErrorV1) == 536, "Error record size changed");
_Static_assert(sizeof(CellularityVulkanEnvironmentStateV1) == 64, "Environment ABI retains its existing size");
_Static_assert(offsetof(CellularityVulkanEnvironmentStateV1, dish_inner_radius) == 60, "Ring radius uses the former reserved float slot");
_Static_assert(offsetof(CellularityVulkanErrorV1, abi_version) == 0, "Error ABI offset changed");
_Static_assert(offsetof(CellularityVulkanErrorV1, struct_size) == 4, "Error size offset changed");
_Static_assert(offsetof(CellularityVulkanErrorV1, code) == 8, "Error code offset changed");
_Static_assert(offsetof(CellularityVulkanErrorV1, flags) == 12, "Error flags offset changed");
_Static_assert(offsetof(CellularityVulkanErrorV1, backend_code) == 16, "Backend code offset changed");
_Static_assert(offsetof(CellularityVulkanErrorV1, message_length) == 20, "Message length offset changed");
_Static_assert(offsetof(CellularityVulkanErrorV1, message) == 24, "Message offset changed");

_Static_assert(offsetof(CellularityVulkanSessionCreateInfoV1, abi_version) == 0, "Create ABI offset changed");
_Static_assert(offsetof(CellularityVulkanSessionCreateInfoV1, struct_size) == 4, "Create size offset changed");
_Static_assert(offsetof(CellularityVulkanSessionCreateInfoV1, asset_manager) == 8, "Create asset offset changed");
ASSERT_NEXT_FIELD(CellularityVulkanSessionCreateInfoV1, asset_manager, flags, uint32_t);
ASSERT_NEXT_FIELD(CellularityVulkanSessionCreateInfoV1, flags, reserved_zero, uint32_t);
_Static_assert(CELLULARITY_VULKAN_SESSION_CREATE_INFO_V1_REQUIRED_SIZE == offsetof(CellularityVulkanSessionCreateInfoV1, reserved_zero) + sizeof(uint32_t), "Create required size changed");

_Static_assert(offsetof(CellularityVulkanSurfaceApiV1, abi_version) == 0, "API ABI offset changed");
_Static_assert(offsetof(CellularityVulkanSurfaceApiV1, struct_size) == 4, "API size offset changed");
_Static_assert(offsetof(CellularityVulkanSurfaceApiV1, api_context) == 8, "API context offset changed");
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, api_context, retain_api, CellularityVulkanRetainApiFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, retain_api, release_api, CellularityVulkanReleaseApiFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, release_api, create_session, CellularityVulkanCreateSessionFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, create_session, destroy_session, CellularityVulkanDestroySessionFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, destroy_session, has_attached_surface, CellularityVulkanHasAttachedSurfaceFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, has_attached_surface, attach_window, CellularityVulkanAttachWindowFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, attach_window, resize_surface, CellularityVulkanResizeSurfaceFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, resize_surface, detach_surface, CellularityVulkanDetachSurfaceFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, detach_surface, present_microscope, CellularityVulkanPresentMicroscopeFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, present_microscope, clear_surface, CellularityVulkanClearSurfaceFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, clear_surface, draw_environment_field_preview, CellularityVulkanDrawEnvironmentFieldPreviewFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, draw_environment_field_preview, begin_genome_frame, CellularityVulkanBeginGenomeFrameFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, begin_genome_frame, draw_genome_grid, CellularityVulkanDrawGenomeGridFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, draw_genome_grid, draw_genome_tree, CellularityVulkanDrawGenomeTreeFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, draw_genome_tree, draw_genome_cells, CellularityVulkanDrawGenomeCellsFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, draw_genome_cells, end_genome_surface_frame, CellularityVulkanEndGenomeSurfaceFrameFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, end_genome_surface_frame, cancel_genome_frame, CellularityVulkanCancelGenomeFrameFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, cancel_genome_frame, begin_offscreen_rgba, CellularityVulkanBeginOffscreenRgbaFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, begin_offscreen_rgba, draw_microscope_offscreen_pass, CellularityVulkanDrawMicroscopeOffscreenPassFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, draw_microscope_offscreen_pass, end_genome_offscreen_pass, CellularityVulkanEndGenomeOffscreenPassFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, end_genome_offscreen_pass, finish_offscreen_rgba, CellularityVulkanFinishOffscreenRgbaFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, finish_offscreen_rgba, cancel_offscreen_rgba, CellularityVulkanCancelOffscreenRgbaFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, cancel_offscreen_rgba, set_profiling_enabled, CellularityVulkanSetProfilingEnabledFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, set_profiling_enabled, get_diagnostics_size, CellularityVulkanGetDiagnosticsSizeFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, get_diagnostics_size, read_diagnostics_chunk, CellularityVulkanReadDiagnosticsChunkFnV1);
ASSERT_NEXT_FIELD(CellularityVulkanSurfaceApiV1, read_diagnostics_chunk, release_diagnostics_snapshot, CellularityVulkanReleaseDiagnosticsSnapshotFnV1);

ASSERT_CALLBACK_TYPE(retain_api, CellularityVulkanRetainApiFnV1);
ASSERT_CALLBACK_TYPE(release_api, CellularityVulkanReleaseApiFnV1);
ASSERT_CALLBACK_TYPE(create_session, CellularityVulkanCreateSessionFnV1);
ASSERT_CALLBACK_TYPE(destroy_session, CellularityVulkanDestroySessionFnV1);
ASSERT_CALLBACK_TYPE(has_attached_surface, CellularityVulkanHasAttachedSurfaceFnV1);
ASSERT_CALLBACK_TYPE(attach_window, CellularityVulkanAttachWindowFnV1);
ASSERT_CALLBACK_TYPE(resize_surface, CellularityVulkanResizeSurfaceFnV1);
ASSERT_CALLBACK_TYPE(detach_surface, CellularityVulkanDetachSurfaceFnV1);
ASSERT_CALLBACK_TYPE(present_microscope, CellularityVulkanPresentMicroscopeFnV1);
ASSERT_CALLBACK_TYPE(clear_surface, CellularityVulkanClearSurfaceFnV1);
ASSERT_CALLBACK_TYPE(draw_environment_field_preview, CellularityVulkanDrawEnvironmentFieldPreviewFnV1);
ASSERT_CALLBACK_TYPE(begin_genome_frame, CellularityVulkanBeginGenomeFrameFnV1);
ASSERT_CALLBACK_TYPE(draw_genome_grid, CellularityVulkanDrawGenomeGridFnV1);
ASSERT_CALLBACK_TYPE(draw_genome_tree, CellularityVulkanDrawGenomeTreeFnV1);
ASSERT_CALLBACK_TYPE(draw_genome_cells, CellularityVulkanDrawGenomeCellsFnV1);
ASSERT_CALLBACK_TYPE(end_genome_surface_frame, CellularityVulkanEndGenomeSurfaceFrameFnV1);
ASSERT_CALLBACK_TYPE(cancel_genome_frame, CellularityVulkanCancelGenomeFrameFnV1);
ASSERT_CALLBACK_TYPE(begin_offscreen_rgba, CellularityVulkanBeginOffscreenRgbaFnV1);
ASSERT_CALLBACK_TYPE(draw_microscope_offscreen_pass, CellularityVulkanDrawMicroscopeOffscreenPassFnV1);
ASSERT_CALLBACK_TYPE(end_genome_offscreen_pass, CellularityVulkanEndGenomeOffscreenPassFnV1);
ASSERT_CALLBACK_TYPE(finish_offscreen_rgba, CellularityVulkanFinishOffscreenRgbaFnV1);
ASSERT_CALLBACK_TYPE(cancel_offscreen_rgba, CellularityVulkanCancelOffscreenRgbaFnV1);
ASSERT_CALLBACK_TYPE(set_profiling_enabled, CellularityVulkanSetProfilingEnabledFnV1);
ASSERT_CALLBACK_TYPE(get_diagnostics_size, CellularityVulkanGetDiagnosticsSizeFnV1);
ASSERT_CALLBACK_TYPE(read_diagnostics_chunk, CellularityVulkanReadDiagnosticsChunkFnV1);
ASSERT_CALLBACK_TYPE(release_diagnostics_snapshot, CellularityVulkanReleaseDiagnosticsSnapshotFnV1);

_Static_assert(CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE == offsetof(CellularityVulkanSurfaceApiV1, release_diagnostics_snapshot) + sizeof(CellularityVulkanReleaseDiagnosticsSnapshotFnV1), "API required size changed");
_Static_assert(sizeof(CellularityVulkanSurfaceApiV1) == CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE, "Unexpected API tail padding");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE == 224, "64-bit API size changed");
#elif UINTPTR_MAX == UINT32_MAX
_Static_assert(CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE == 116, "32-bit API size changed");
#else
#error Unsupported pointer width
#endif

int main(void) {
    return 0;
}
