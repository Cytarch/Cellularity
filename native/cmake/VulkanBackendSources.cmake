# Shared definition of the Vulkan backend, used by both the Android DSO and the
# Windows host.
#
# The point of these helpers is that one platform cannot quietly diverge from the
# other. The backend sources, the profiling compile definition and the linked
# contract libraries are all named once here, so adding a file to the backend adds
# it everywhere, and a platform that stops compiling one of them is a visible
# change to this list rather than an edit buried in one branch of the top-level
# file.
#
# What stays platform-specific is deliberately small and lives at the call site:
# the window system, the asset source, the loader import library, and how symbols
# are exported.

# Sources that make up the backend itself. Every one of them is platform-neutral
# now: the window and surface differences are behind this list, not inside it.
function(cellularity_vulkan_backend_sources out_var)
    set(sources
            render/backend/vulkan/VulkanClearFrameSession.cpp
            render/backend/vulkan/NativeVulkanSurface.cpp
            render/backend/vulkan/VulkanBackendAbiExports.cpp
            render/backend/vulkan/VulkanBasicCellRenderer.cpp
            render/backend/vulkan/VulkanResourceFoundation.cpp
            render/backend/vulkan/VulkanResult.cpp
    )
    set(${out_var} ${sources} PARENT_SCOPE)
endfunction()

# Compile definitions the backend expects on both platforms. The profiling flag is
# shared so a frame profiled on one platform reports what the other does.
function(cellularity_configure_vulkan_backend target)
    target_compile_definitions(${target} PRIVATE
            CELLULARITY_ENABLE_VULKAN_PROFILING=$<BOOL:${CELLULARITY_ENABLE_VULKAN_PROFILING}>
            "$<$<CONFIG:Debug>:CELLULARITY_VULKAN_VALIDATION=1>"
    )
endfunction()

# Contract libraries every backend build depends on, so the two platforms cannot
# disagree about which rendering contracts they are built against.
function(cellularity_link_vulkan_backend_contracts target)
    target_link_libraries(${target} PRIVATE
            cellularity_vulkan_frame_contract
            cellularity_render_overlay_contract
            cellularity_render_assets
            cellularity_render_frame_result
    )
endfunction()
