#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

if [[ ! -f "${CELLULARITY_NATIVE_BUILD_DIR}/build.ninja" ]] \
        || ! grep -Fqx \
            "CMAKE_HOME_DIRECTORY:INTERNAL=${CELLULARITY_NATIVE_SOURCE_DIR}" \
            "${CELLULARITY_NATIVE_BUILD_DIR}/CMakeCache.txt"; then
    "${SCRIPT_DIR}/configure.sh"
fi

BUILD_TARGETS=(
    cellularity_semantic_contract_tests
    cellularity_relaxed_semantic_contract_tests
    cellularity_vulkan_backend_abi_c11_tests
    cellularity_vulkan_backend_abi_short_enum_c11_tests
    cellularity_vulkan_backend_abi_tests
    cellularity_formats_parity_tests
    cellularity_environment_gene_tests
    cellularity_environment_batch_contract_tests
    cellularity_genome_draft_contract_tests
    cellularity_canonical_gene_tests
    cellularity_gene_pool_tests
    cellularity_genome_pool_tests
    cellularity_gene_access_tests
    cellularity_topology_tests
    cellularity_simulation_tests
    cellularity_dynamic_genome_operation_tests
    cellularity_interaction_tests
    cellularity_simulation_scheduler_tests
    cellularity_simulation_execution_context_tests
    cellularity_native_simulation_owner_tests
    cellularity_microscope_command_mailbox_tests
    cellularity_microscope_input_batch_tests
    cellularity_environment_field_preview_state_tests
    cellularity_genome_preview_command_infrastructure_tests
    cellularity_rendering_tests
    cellularity_gles_render_contract_tests
    cellularity_render_surface_session_tests
    cellularity_native_render_attachment_lease_tests
    cellularity_native_vulkan_render_worker_tests
    cellularity_native_preview_endpoint_handle_tests
    cellularity_render_asset_texture_loader_tests
    cellularity_vulkan_basic_cell_frame_tests
    cellularity_render_frame_contract_tests
    cellularity_render_frame_builder_tests
    cellularity_render_source_frame_tests
    cellularity_boundary_link_capture_tests
    cellularity_link_render_contract_tests
    cellularity_link_geometry_builder_tests
    cellularity_renderer_link_bounds_tests
    cellularity_render_frame_exchange_tests
    cellularity_microscope_render_frame_pipeline_tests
    cellularity_native_diagnostics_tests
    cellularity_format_fixture_tool
    cellularity_format_semantic_tests
    cellularity_format_malformed_tests
    cellularity_release_genome_codec_tests
    cellularity_release_substrate_codec_tests
    cellularity_behavior_baseline_tests
    cellularity_behavior_performance
    cellularity_perf
    cellularity_memory_probe
)

if [[ "${CELLULARITY_NATIVE_THREAD_SANITIZER}" != "ON" ]]; then
    BUILD_TARGETS+=(cellularity_steady_state_allocation_tests)
fi

cmake --build "${CELLULARITY_NATIVE_BUILD_DIR}" \
      --target "${BUILD_TARGETS[@]}" \
      --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
