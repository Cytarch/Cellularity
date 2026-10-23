
# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html.
# For more examples on how to use CMake, see https://github.com/android/ndk-samples.

# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Declares the project name. The project name can be accessed via ${ PROJECT_NAME},
# Since this is the top level CMakeLists.txt, the project name is also accessible
# with ${CMAKE_PROJECT_NAME} (both CMake variables are in-sync within the top level
# build script scope).
project("cellularity")

find_package(Threads REQUIRED)

option(CELLULARITY_ENABLE_NATIVE_PROFILE "Enable native profiling counters in non-Android host builds" ON)
option(CELLULARITY_ENABLE_RENDER_TIMELINE "Enable local input-to-presentation timeline collection" OFF)
option(CELLULARITY_ENABLE_PERFORMANCE_HUD "Build the native microscope performance HUD" OFF)
option(CELLULARITY_ENABLE_VULKAN_PROFILING "Build opt-in Vulkan frame profiling" OFF)
option(CELLULARITY_ENABLE_SIMULATION_SCHEDULER "Build the native simulation scheduler" ON)
option(CELLULARITY_ENABLE_FUZZING "Build libFuzzer decoder targets with ASan and UBSan" OFF)
option(CELLULARITY_ENABLE_SANITIZERS "Instrument non-Android targets with ASan and UBSan" OFF)
option(CELLULARITY_ENABLE_THREAD_SANITIZER
        "Instrument non-Android targets with ThreadSanitizer"
        OFF)
option(CELLULARITY_WARNINGS_AS_ERRORS "Treat project-owned source warnings as errors" OFF)
option(CELLULARITY_ENABLE_IPO "Enable IPO/LTO in supported optimized configurations" ON)
set(CELLULARITY_SIGNAL_BLOCK_SIMD_DEFAULT OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(CELLULARITY_SIGNAL_BLOCK_SIMD_DEFAULT ON)
endif()
option(CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD
        "Use explicit four-float SIMD blocks in signal kernels"
        ${CELLULARITY_SIGNAL_BLOCK_SIMD_DEFAULT})
if(ANDROID)
    set(CELLULARITY_CELL_OWNED_SIGNAL_KERNEL_DEFAULT OFF)
else()
    set(CELLULARITY_CELL_OWNED_SIGNAL_KERNEL_DEFAULT ON)
endif()
option(CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL
        "Build the host/C-03 Cell-owned signal kernel candidate"
        ${CELLULARITY_CELL_OWNED_SIGNAL_KERNEL_DEFAULT})
option(CELLULARITY_BUILD_MEMORY_PROBE "Build the standalone simulation memory probe" OFF)
option(CELLULARITY_BUILD_THROUGHPUT_PROBE "Build the standalone Android/host throughput probe" OFF)
option(CELLULARITY_BUILD_DISPATCHER_LAB
        "Build the standalone persistent-dispatcher laboratory"
        OFF)
option(CELLULARITY_BUILD_EXECUTION_CONTEXT_TRACE
        "Build the standalone simulation execution-context device trace check"
        OFF)
option(CELLULARITY_BUILD_HOST_GLES_PROBE
        "Build the opt-in headless GLES baseline and diagnostics probe"
        OFF)
if(ANDROID)
    set(CELLULARITY_RUNTIME_OVERLAP_SELECTION_DEFAULT OFF)
else()
    set(CELLULARITY_RUNTIME_OVERLAP_SELECTION_DEFAULT ON)
endif()
option(CELLULARITY_ENABLE_RUNTIME_OVERLAP_SELECTION
        "Allow host tests and benchmarks to change the overlap scan algorithm at runtime"
        ${CELLULARITY_RUNTIME_OVERLAP_SELECTION_DEFAULT})
set(CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM "incremental-bucketed-f64-source32" CACHE STRING
        "Default Cell overlap scan algorithm (baseline, compact-record, compact-primary, or incremental-bucketed-f64-source32)"
)
set_property(CACHE CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM PROPERTY STRINGS
        baseline
        compact-record
        compact-primary
        incremental-bucketed-f64-source32
)

if(NOT CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "baseline"
        AND NOT CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "compact-record"
        AND NOT CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "compact-primary"
        AND NOT CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "incremental-bucketed-f64-source32")
    message(FATAL_ERROR
            "Unsupported CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM: "
            "${CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM}"
    )
endif()

if(CELLULARITY_ENABLE_SANITIZERS AND ANDROID)
    message(FATAL_ERROR "CELLULARITY_ENABLE_SANITIZERS is supported only by the host build")
endif()
if(CELLULARITY_ENABLE_THREAD_SANITIZER AND ANDROID)
    message(FATAL_ERROR "CELLULARITY_ENABLE_THREAD_SANITIZER is supported only by the host build")
endif()
if(CELLULARITY_BUILD_EXECUTION_CONTEXT_TRACE
        AND NOT ANDROID
        AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
            "CELLULARITY_BUILD_EXECUTION_CONTEXT_TRACE is supported only on Linux and Android")
endif()
if(ANDROID AND CELLULARITY_ENABLE_RUNTIME_OVERLAP_SELECTION)
    message(FATAL_ERROR
            "CELLULARITY_ENABLE_RUNTIME_OVERLAP_SELECTION is a host-only test/benchmark option")
endif()
if(ANDROID AND CELLULARITY_BUILD_HOST_GLES_PROBE)
    message(FATAL_ERROR
            "CELLULARITY_BUILD_HOST_GLES_PROBE is supported only by host builds")
endif()
if(CELLULARITY_ENABLE_SANITIZERS AND CELLULARITY_ENABLE_FUZZING)
    message(FATAL_ERROR "Use either the full sanitizer suite or the decoder fuzz targets, not both")
endif()
if(CELLULARITY_ENABLE_THREAD_SANITIZER
        AND (CELLULARITY_ENABLE_SANITIZERS OR CELLULARITY_ENABLE_FUZZING))
    message(FATAL_ERROR
            "ThreadSanitizer must use a separate build from ASan/UBSan and decoder fuzzing")
endif()
if(CELLULARITY_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "CELLULARITY_ENABLE_SANITIZERS requires Clang or GCC")
endif()
if(CELLULARITY_ENABLE_THREAD_SANITIZER
        AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "CELLULARITY_ENABLE_THREAD_SANITIZER requires Clang or GCC")
endif()
if(CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD
        AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR
            "CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD requires Clang or GCC")
endif()

set(CELLULARITY_PROJECT_WARNING_FLAGS)
if(MSVC)
    list(APPEND CELLULARITY_PROJECT_WARNING_FLAGS /W4)
    if(CELLULARITY_WARNINGS_AS_ERRORS)
        list(APPEND CELLULARITY_PROJECT_WARNING_FLAGS /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    list(APPEND CELLULARITY_PROJECT_WARNING_FLAGS -Wall -Wextra -Wpedantic)
    if(CELLULARITY_WARNINGS_AS_ERRORS)
        list(APPEND CELLULARITY_PROJECT_WARNING_FLAGS -Werror)
    endif()
elseif(CELLULARITY_WARNINGS_AS_ERRORS)
    message(FATAL_ERROR "CELLULARITY_WARNINGS_AS_ERRORS is unsupported by ${CMAKE_CXX_COMPILER_ID}")
endif()

set(CELLULARITY_IPO_SUPPORTED OFF)
if(CELLULARITY_ENABLE_IPO
        AND NOT ANDROID
        AND NOT CELLULARITY_ENABLE_SANITIZERS
        AND NOT CELLULARITY_ENABLE_THREAD_SANITIZER
        AND NOT CELLULARITY_ENABLE_FUZZING)
    include(CheckIPOSupported)
    check_ipo_supported(
            RESULT CELLULARITY_IPO_SUPPORTED
            OUTPUT CELLULARITY_IPO_ERROR
            LANGUAGES CXX
    )
    if(CELLULARITY_IPO_SUPPORTED)
        message(STATUS "Cellularity IPO/LTO enabled for optimized configurations")
    else()
        message(STATUS "Cellularity IPO/LTO unavailable for this toolchain: ${CELLULARITY_IPO_ERROR}")
    endif()
elseif(CELLULARITY_ENABLE_IPO AND ANDROID)
    message(STATUS "Cellularity IPO/LTO disabled for Android: the bundled CMake 3.22 probe is not reliable with the NDK linker")
endif()

set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_CMAKE_ARM7 OFF CACHE BOOL "" FORCE)
set(HWY_WARNINGS_ARE_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/highway EXCLUDE_FROM_ALL)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Highway 1.2.0 declares AVX-512 attributes unknown to newer Clang releases.
    target_compile_options(hwy PRIVATE -Wno-ignored-attributes)
endif()

set(CELLULARITY_SIMULATION_SOURCES
        core/SimulationRandom.cpp
        core/BesselLookup.cpp
        core/CellType.cpp
        core/Gene.cpp
        core/GeneAccess.cpp
        core/CanonicalGene.cpp
        core/GenePool.cpp
        core/GenomePool.cpp
        core/GenomeMetadata.cpp
        core/Environment.cpp
        core/EnvironmentBatch.cpp
        core/RendererMemoryModel.cpp
        core/SimulationMemoryPolicy.cpp
        core/CollisionGrid.cpp
        core/CellTopology.cpp
        core/CellIncrementalBucketedF64Broadphase.cpp
        core/CellSweepBroadphase.cpp
        core/SimulationWorld.cpp
        core/CellData.cpp
        core/FoodData.cpp
        core/DeadSplashData.cpp
        diagnostics/HighwaySmoke.cpp
)
if(CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL)
    list(APPEND CELLULARITY_SIMULATION_SOURCES
            core/CellSignalAdjacency.cpp
    )
endif()

set(CELLULARITY_RENDER_GEOMETRY_SOURCES
        render/geometry/RenderContactChordNormalizer.cpp
)

set(CELLULARITY_FORMAT_SOURCES
        io/JavaObjectStreamReader.cpp
        io/Sha256.cpp
        formats/GenomeDraftCodec.cpp
        formats/release/SaveContainerCodec.cpp
        formats/release/SaveContainerBody.cpp
        formats/release/ReleaseGenomeCodec.cpp
        formats/release/ReleaseSubstrateCodec.cpp
        formats/SnapshotCodecSupport.cpp
        formats/SnapshotFileIO.cpp
        formats/DecodedWorldValidation.cpp
        formats/CellSnapshotWireCodec.cpp
        formats/LegacyGeneCodec.cpp
        formats/LegacyEnvironmentCodec.cpp
        formats/LegacySubstrateCodec.cpp
        formats/SubstrateFileCodec.cpp
        formats/NativeSubstrateLoader.cpp
)

set(CELLULARITY_INTERACTION_SOURCES
        app/NativeWorldEditing.cpp
        app/NativeWorldSession.cpp
        app/NativeSimulationStepper.cpp
        app/NativeSimulationOwner.cpp
        app/simulation/SimulationScheduler.cpp
        app/NativeInteractionController.cpp
        app/interaction/NativeViewport.cpp
        app/microscope/MicroscopeFrameController.cpp
        app/microscope/MicroscopeInputController.cpp
        app/microscope/MicroscopeInputBatchLayout.cpp
        app/microscope/MicroscopeInputTimeline.cpp
        app/microscope/MicroscopeCommandMailbox.cpp
        app/microscope/MicroscopePresentationState.cpp
        app/microscope/MicroscopeSimulationState.cpp
        app/microscope/interaction/MicroscopeDragController.cpp
        app/microscope/interaction/MicroscopeInteraction.cpp
        app/microscope/interaction/MicroscopeSelectionController.cpp
        app/microscope/interaction/MicroscopeTapController.cpp
        app/microscope/interaction/MicroscopeWorldInteractionState.cpp
)

set(CELLULARITY_RENDERING_SOURCES
        app/render/NativeRenderAttachmentLease.cpp
        app/render/sources/MicroscopeRenderFrameSource.cpp
        app/render/sources/GenomePreviewRenderFrameSource.cpp
        app/render/sources/EnvironmentFieldPreviewRenderFrameSource.cpp
        app/render/sources/VulkanDemoFrameProducer.cpp
        jni/NativeRenderEndpointHandle.cpp
        jni/NativePreviewEndpointHandle.cpp
        app/genome_preview/GenomeFilePreviewSimulation.cpp
        app/genome_preview/GenomePreviewCaptureRequestStore.cpp
        app/genome_preview/GenomePreviewEndpoint.cpp
        app/genome_preview/GenomePreviewEndpointLayouts.cpp
        app/genome_preview/selection/GenomePreviewCellsSelection.cpp
        app/genome_preview/GenomePreviewController.cpp
        app/genome_preview/GenomePreviewInteractionQueue.cpp
        app/genome_preview/GenomePreviewRetainedState.cpp
        app/genome_preview/RetainedPayloadBudget.cpp
        app/render/preview/NativePreviewCaptureLayouts.cpp
        app/render/preview/NativePreviewCaptureRequestState.cpp
        app/EnvironmentFieldPreviewEndpoint.cpp
        app/EnvironmentFieldPreviewRenderer.cpp
        app/EnvironmentFieldPreviewState.cpp
        app/genome_preview/layers/GenomePreviewCellsRenderer.cpp
        app/genome_preview/layers/GenomePreviewFrameRenderer.cpp
        app/genome_preview/layers/GenomePreviewGridRenderer.cpp
        app/genome_preview/ghost/GenomePreviewGhostController.cpp
        app/genome_preview/layers/GenomePreviewGhostRenderer.cpp
        app/NativeGenomePreviewFacade.cpp
        app/genome_preview/GenomePreviewRenderPipeline.cpp
        app/genome_preview/GenomePreviewRenderer.cpp
        app/genome_preview/tree/GenomePreviewTreeController.cpp
        app/genome_preview/tree/GenomePreviewTreeLayout.cpp
        app/genome_preview/tree/GenomePreviewTreeRenderer.cpp
        app/genome_preview/tree/GenomePreviewTreeRenderWorld.cpp
        app/genome_preview/tree/GenomePreviewTreeSelection.cpp
        app/genome_preview/tree/GenomePreviewTreeState.cpp
        app/NativeRenderFacade.cpp
        app/NativeRenderSession.cpp
        app/microscope/NativeMicroscopeRuntime.cpp
        app/microscope/SubstratePreviewCaptureRequestStore.cpp
        app/microscope/SubstratePreviewCaptureEndpoint.cpp
        app/microscope/render/MicroscopeRenderFramePipeline.cpp
        app/microscope/render/MicroscopeRenderer.cpp
        app/NativeCellularityController.cpp
        render/SimulationWorldGpuRenderer.cpp
        render/NativeSimulationWorldRenderer.cpp
        render/NativeCellRendererResources.cpp
        render/NativeRendererResourcesController.cpp
        render/backend/gles/GlesRenderSurfaceSession.cpp
        render/gl/NativeGlApi.cpp
        render/gl/OffscreenRgbaRenderTarget.cpp
        render/text/PixelTextRenderer.cpp
        render/shaders/ShaderProgram.cpp
        render/shaders/CellShader.cpp
        render/shaders/LinkShader.cpp
        render/shaders/FoodShader.cpp
        render/shaders/DeadSplashShader.cpp
        render/shaders/BackgroundShader.cpp
        render/shaders/DensityFieldShader.cpp
        render/shaders/GenomeGridShader.cpp
        render/shaders/SolidColorShader.cpp
        render/shaders/PixelTextShader.cpp
        render/shaders/MarkerShader.cpp
)
if(CELLULARITY_ENABLE_PERFORMANCE_HUD)
    list(APPEND CELLULARITY_RENDERING_SOURCES
            app/microscope/render/MicroscopeDebugHudGles.cpp
    )
endif()

set(CELLULARITY_RENDER_OVERLAY_CONTRACT_SOURCES
        app/microscope/render/MicroscopeDebugHud.cpp
        render/text/PixelTextGeometry.cpp
)

set(CELLULARITY_RENDER_PREPARATION_SOURCES
        render/frame/RenderContactChordBuffer.cpp
        render/frame/RenderContactCollector.cpp
        render/frame/PreparedRenderFrame.cpp
        render/frame/RenderSourceFrame.cpp
        render/frame/RenderSourceCapture.cpp
        render/frame/RenderContactChordResolver.cpp
        render/frame/CellBoundaryMeshBuilder.cpp
        render/frame/RenderLinkGeometryBuilder.cpp
        render/frame/RenderFrameBuilder.cpp
        render/frame/RenderFrameExchange.cpp
)

set(CELLULARITY_VULKAN_FRAME_CONTRACT_SOURCES
        render/backend/vulkan/VulkanBasicCellFrame.cpp
)

set(CELLULARITY_RENDER_FRAME_RESULT_SOURCES
        render/backend/RenderFrameResult.cpp
)

set(CELLULARITY_JNI_SOURCES
        jni/AndroidRenderAssetsLeaseHandle.cpp
        jni/NativeRenderProfilingJni.cpp
        jni/NativeVulkanRenderWorkerJni.cpp
        jni/NativeJniBridgeState.cpp
        jni/NativeControllerHandle.cpp
        jni/NativeControllerJni.cpp
        jni/NativeRenderJni.cpp
        jni/NativeMicroscopeJni.cpp
        jni/NativeGenomePreviewJni.cpp
        jni/NativePreviewCaptureJni.cpp
        jni/NativePreviewCaptureRequestHandle.cpp
        jni/NativeCellSnapshotJni.cpp
        jni/AndroidJniRenderBridge.cpp
)

set(CELLULARITY_ANDROID_PLATFORM_SOURCES
        render/gl/AndroidGles30Api.cpp
        platform/android/AndroidRenderAssets.cpp
        platform/android/AndroidRenderAssetsLease.cpp
        platform/android/AndroidVulkanBackendSessionFactory.cpp
)

function(cellularity_configure_source_target target_name)
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(${target_name} PRIVATE cxx_std_17)
    target_compile_options(${target_name} PRIVATE ${CELLULARITY_PROJECT_WARNING_FLAGS})

    if(CELLULARITY_ENABLE_SANITIZERS)
        target_compile_options(${target_name} PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )
        get_target_property(CELLULARITY_TARGET_TYPE ${target_name} TYPE)
        if(CELLULARITY_TARGET_TYPE STREQUAL "EXECUTABLE"
                OR CELLULARITY_TARGET_TYPE STREQUAL "SHARED_LIBRARY"
                OR CELLULARITY_TARGET_TYPE STREQUAL "MODULE_LIBRARY")
            target_link_options(${target_name} PRIVATE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer
            )
        endif()
    endif()

    if(CELLULARITY_ENABLE_THREAD_SANITIZER)
        target_compile_options(${target_name} PRIVATE
                -fsanitize=thread
                -fno-omit-frame-pointer
        )
        get_target_property(CELLULARITY_TARGET_TYPE ${target_name} TYPE)
        if(CELLULARITY_TARGET_TYPE STREQUAL "EXECUTABLE"
                OR CELLULARITY_TARGET_TYPE STREQUAL "SHARED_LIBRARY"
                OR CELLULARITY_TARGET_TYPE STREQUAL "MODULE_LIBRARY")
            target_link_options(${target_name} PRIVATE
                    -fsanitize=thread
                    -fno-omit-frame-pointer
            )
        endif()
    endif()

    if(CELLULARITY_IPO_SUPPORTED)
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
    endif()
endfunction()

function(cellularity_optimize_target target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        foreach(CELLULARITY_CONFIG IN ITEMS Release RelWithDebInfo MinSizeRel)
            target_compile_options(${target_name} PRIVATE
                    "$<$<CONFIG:${CELLULARITY_CONFIG}>:-O3>"
                    "$<$<CONFIG:${CELLULARITY_CONFIG}>:-fomit-frame-pointer>"
            )
        endforeach()
        target_compile_options(${target_name} PRIVATE
                "$<$<CONFIG:Debug>:-Og>"
                "$<$<CONFIG:Debug>:-g3>"
                "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
        )
    endif()
endfunction()

# Persistent simulation dispatch support is a production runtime component.
# ThreadPool itself is header-only; topology and the small scalar autotuner
# sort shim are kept in this focused library instead of linking all of
# hwy_contrib and its unrelated SIMD sort implementations.
add_library(cellularity_execution STATIC
        third_party/highway/hwy/contrib/thread_pool/topology.cc
        execution/HighwayThreadPoolSupport.cpp
        execution/DispatcherCapacity.cpp
        execution/FloatingPointEnvironment.cpp
        execution/SimulationExecutionContext.cpp
)
cellularity_configure_source_target(cellularity_execution)
cellularity_optimize_target(cellularity_execution)
set_target_properties(cellularity_execution PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)
target_include_directories(cellularity_execution SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/highway
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(cellularity_execution PRIVATE
            -Wno-gnu-zero-variadic-macro-arguments)
endif()
target_link_libraries(cellularity_execution PUBLIC
        hwy
        Threads::Threads
)

# Opt-in lifecycle/trace executable for optional C-01 Android diagnostics. It
# is never linked into the application and cannot change the production
# default of zero additional simulation threads.
if(CELLULARITY_BUILD_EXECUTION_CONTEXT_TRACE)
    add_executable(cellularity_execution_context_trace
            benchmarks/cellularity_execution_context_trace.cpp
    )
    cellularity_configure_source_target(cellularity_execution_context_trace)
    cellularity_optimize_target(cellularity_execution_context_trace)
    target_link_libraries(cellularity_execution_context_trace PRIVATE
            cellularity_execution
            Threads::Threads
    )
endif()

add_library(cellularity_simulation STATIC ${CELLULARITY_SIMULATION_SOURCES})
cellularity_configure_source_target(cellularity_simulation)
cellularity_optimize_target(cellularity_simulation)
if(ANDROID)
    # The simulation is linked into one JNI shared object. Keep its C++ ABI
    # internal so --gc-sections can discard unselected overlap variants and
    # calls between simulation translation units do not go through the PLT.
    set_target_properties(cellularity_simulation PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
    )
endif()
target_link_libraries(cellularity_simulation PRIVATE hwy)
target_include_directories(cellularity_simulation SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/highway
)
target_compile_definitions(cellularity_simulation PRIVATE
        CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD=$<BOOL:${CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD}>
)
target_compile_definitions(cellularity_simulation PUBLIC
        CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL=$<BOOL:${CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL}>
)
if(CELLULARITY_ENABLE_PERFORMANCE_HUD)
    target_compile_definitions(cellularity_simulation PUBLIC
            CELLULARITY_PERFORMANCE_HUD=1
    )
endif()

if(CELLULARITY_ENABLE_RUNTIME_OVERLAP_SELECTION)
    # CellSweepBroadphase has configured inline templates in its public header;
    # every consumer must compile those templates with the same selection mode.
    target_compile_definitions(cellularity_simulation PUBLIC
            CELLULARITY_RUNTIME_OVERLAP_SELECTION=1
    )
endif()

if(CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "compact-record")
    target_compile_definitions(cellularity_simulation PUBLIC
            CELLULARITY_DEFAULT_COMPACT_RECORD_SWEEP=1
    )
elseif(CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "compact-primary")
    target_compile_definitions(cellularity_simulation PUBLIC
            CELLULARITY_DEFAULT_COMPACT_PRIMARY_SWEEP=1
    )
elseif(CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "incremental-bucketed-f64-source32")
    target_compile_definitions(cellularity_simulation PUBLIC
            CELLULARITY_DEFAULT_INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP=1
    )
endif()
message(STATUS
        "Cellularity default overlap scan: ${CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM}"
)

if(CELLULARITY_ENABLE_RENDER_TIMELINE)
    target_compile_definitions(cellularity_simulation PUBLIC
            CELLULARITY_RENDER_TIMELINE=1
    )
endif()

if(NOT ANDROID AND CELLULARITY_ENABLE_NATIVE_PROFILE)
    target_compile_definitions(cellularity_simulation PUBLIC
            CELLULARITY_PROFILE=1
            CELLULARITY_RENDER_PROFILE=1
    )
endif()

file(GLOB CELLULARITY_ZSTD_COMMON_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/common/*.c)
file(GLOB CELLULARITY_ZSTD_COMPRESS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/compress/*.c)
file(GLOB CELLULARITY_ZSTD_DECOMPRESS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/decompress/*.c)
add_library(zstd_vendor STATIC
        ${CELLULARITY_ZSTD_COMMON_SOURCES}
        ${CELLULARITY_ZSTD_COMPRESS_SOURCES}
        ${CELLULARITY_ZSTD_DECOMPRESS_SOURCES}
)
cellularity_optimize_target(zstd_vendor)
set_target_properties(zstd_vendor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(zstd_vendor PRIVATE
        ZSTD_DISABLE_ASM=1
        ZSTD_LEGACY_SUPPORT=0
)
target_include_directories(zstd_vendor SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd
)

add_library(cellularity_formats STATIC ${CELLULARITY_FORMAT_SOURCES})
cellularity_configure_source_target(cellularity_formats)
cellularity_optimize_target(cellularity_formats)
target_link_libraries(cellularity_formats PRIVATE
        cellularity_simulation
        zstd_vendor
        z
)

add_library(cellularity_interaction STATIC ${CELLULARITY_INTERACTION_SOURCES})
cellularity_configure_source_target(cellularity_interaction)
cellularity_optimize_target(cellularity_interaction)
# Public interaction headers expose SimulationWorld and other simulation types whose
# inline code and layout depend on cellularity_simulation's public compile
# definitions. Propagate that complete usage contract to every consumer.
target_link_libraries(cellularity_interaction PUBLIC
        cellularity_simulation
        cellularity_execution
)

if(CELLULARITY_ENABLE_SIMULATION_SCHEDULER)
    target_compile_definitions(cellularity_interaction PUBLIC
            CELLULARITY_SIMULATION_SCHEDULER=1
    )
endif()

add_library(cellularity_render_geometry STATIC ${CELLULARITY_RENDER_GEOMETRY_SOURCES})
cellularity_configure_source_target(cellularity_render_geometry)
cellularity_optimize_target(cellularity_render_geometry)
if(NOT ANDROID AND CELLULARITY_ENABLE_NATIVE_PROFILE)
    target_compile_definitions(cellularity_render_geometry PRIVATE
            CELLULARITY_RENDER_PROFILE=1
    )
endif()

add_library(cellularity_render_preparation STATIC ${CELLULARITY_RENDER_PREPARATION_SOURCES})
cellularity_configure_source_target(cellularity_render_preparation)
cellularity_optimize_target(cellularity_render_preparation)
target_link_libraries(cellularity_render_preparation PRIVATE
        cellularity_simulation
        cellularity_render_geometry
)

add_library(cellularity_render_frame_result STATIC
        ${CELLULARITY_RENDER_FRAME_RESULT_SOURCES}
)
cellularity_configure_source_target(cellularity_render_frame_result)
cellularity_optimize_target(cellularity_render_frame_result)
set_target_properties(cellularity_render_frame_result PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

# Main-library client for the optional Vulkan DSO. Keep this target free of
# Vulkan, Android and JNI dependencies: the only cross-library contract is the
# pure-C API table declared by CellularityVulkanBackendAbi.h.
add_library(cellularity_vulkan_backend_client STATIC
        render/backend/abi/VulkanBackendSession.cpp
        render/backend/abi/VulkanRenderSurfaceSessionAdapter.cpp
        render/backend/abi/VulkanRenderSurfaceSessionBinding.cpp
)
cellularity_configure_source_target(cellularity_vulkan_backend_client)
cellularity_optimize_target(cellularity_vulkan_backend_client)
set_target_properties(cellularity_vulkan_backend_client PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
)
target_link_libraries(cellularity_vulkan_backend_client PRIVATE
        cellularity_render_frame_result
        Threads::Threads
)

add_library(cellularity_render_worker STATIC
        app/render/NativeRenderProfilingState.cpp
        app/render/NativeVulkanRenderWorker.cpp
        app/render/NativeVulkanSurfaceLease.cpp
        app/render/NativeVulkanWorkerStatus.cpp
)
cellularity_configure_source_target(cellularity_render_worker)
cellularity_optimize_target(cellularity_render_worker)
set_target_properties(cellularity_render_worker PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
)
target_link_libraries(cellularity_render_worker PRIVATE
        cellularity_vulkan_backend_client
        cellularity_render_frame_result
        Threads::Threads
)
target_compile_definitions(cellularity_render_worker PRIVATE
        CELLULARITY_ENABLE_VULKAN_PROFILING=$<BOOL:${CELLULARITY_ENABLE_VULKAN_PROFILING}>
)

# This small cross-platform library validates and maps immutable prepared
# frames without including Vulkan headers or touching the simulation. Android
# Vulkan consumes it directly, while host tests verify the exact upload and
# draw contract without requiring a desktop Vulkan runtime.
add_library(cellularity_vulkan_frame_contract STATIC
        ${CELLULARITY_VULKAN_FRAME_CONTRACT_SOURCES}
)
cellularity_configure_source_target(cellularity_vulkan_frame_contract)
cellularity_optimize_target(cellularity_vulkan_frame_contract)
set_target_properties(cellularity_vulkan_frame_contract PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

# CPU glyph layout, atlas generation, and Performance HUD batching are shared
# by GLES and Vulkan. Keep them free of either graphics API so both backends
# consume byte-identical overlay geometry.
add_library(cellularity_render_overlay_contract STATIC
        ${CELLULARITY_RENDER_OVERLAY_CONTRACT_SOURCES}
)
cellularity_configure_source_target(cellularity_render_overlay_contract)
cellularity_optimize_target(cellularity_render_overlay_contract)
set_target_properties(cellularity_render_overlay_contract PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

# Packaged image decoding is backend-neutral and independent from Android.
# GLES and Vulkan both consume the same validated RGBA8 texture payloads.
add_library(cellularity_render_assets STATIC
        render/AssetRenderTextureLoader.cpp
)
cellularity_configure_source_target(cellularity_render_assets)
cellularity_optimize_target(cellularity_render_assets)
set_target_properties(cellularity_render_assets PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)
target_include_directories(cellularity_render_assets SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

add_library(cellularity_rendering STATIC ${CELLULARITY_RENDERING_SOURCES})
cellularity_configure_source_target(cellularity_rendering)
cellularity_optimize_target(cellularity_rendering)
target_link_libraries(cellularity_rendering PRIVATE
        cellularity_simulation
        cellularity_formats
        cellularity_interaction
        cellularity_render_preparation
        cellularity_render_overlay_contract
        cellularity_render_frame_result
        cellularity_vulkan_backend_client
        Threads::Threads
)

if(CELLULARITY_BUILD_HOST_GLES_PROBE)
    find_library(CELLULARITY_EGL_LIBRARY NAMES EGL REQUIRED)
    find_library(CELLULARITY_GLES_LIBRARY NAMES GLESv2 REQUIRED)
    add_executable(cellularity_gles_baseline_probe
            tools/gles_baseline_probe.cpp
            render/gl/AndroidGles30Api.cpp
    )
    cellularity_configure_source_target(cellularity_gles_baseline_probe)
    cellularity_optimize_target(cellularity_gles_baseline_probe)
    target_compile_definitions(cellularity_gles_baseline_probe PRIVATE
            CELLULARITY_HOST_GLES_PROBE=1
    )
    target_include_directories(cellularity_gles_baseline_probe SYSTEM PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
    )
    target_link_libraries(cellularity_gles_baseline_probe PRIVATE
            cellularity_formats
            cellularity_interaction
            cellularity_rendering
            cellularity_render_geometry
            ${CELLULARITY_EGL_LIBRARY}
            ${CELLULARITY_GLES_LIBRARY}
    )
endif()

if(ANDROID)
    find_library(CELLULARITY_VULKAN_LIBRARY NAMES vulkan REQUIRED)

    add_library(cellularity_android SHARED
            ${CELLULARITY_JNI_SOURCES}
            ${CELLULARITY_ANDROID_PLATFORM_SOURCES}
    )
    cellularity_configure_source_target(cellularity_android)
    cellularity_optimize_target(cellularity_android)
    set_target_properties(cellularity_android PROPERTIES OUTPUT_NAME ${CMAKE_PROJECT_NAME})
    target_link_libraries(cellularity_android PRIVATE
            cellularity_formats
            cellularity_interaction
            cellularity_rendering
            cellularity_render_assets
            cellularity_vulkan_backend_client
            cellularity_render_worker
            cellularity_vulkan_frame_contract
            android
            GLESv3
            log
    )

    # Keep the optional Vulkan loader dependency out of the GLES simulation
    # library. This focused backend library is loaded lazily from settings, so
    # a missing or broken Vulkan loader cannot prevent the GLES path from
    # loading.
    add_library(cellularity_vulkan SHARED
            jni/AndroidJniVulkanCapabilityProbe.cpp
            platform/android/AndroidRenderAssets.cpp
            platform/android/vulkan/AndroidVulkanCapabilityProbe.cpp
            render/backend/vulkan/AndroidVulkanClearFrameSession.cpp
            render/backend/vulkan/VulkanBackendAbiExports.cpp
            render/backend/vulkan/VulkanBasicCellRenderer.cpp
            render/backend/vulkan/VulkanResourceFoundation.cpp
            render/backend/vulkan/VulkanResult.cpp
    )
    cellularity_configure_source_target(cellularity_vulkan)
    cellularity_optimize_target(cellularity_vulkan)
    set_target_properties(cellularity_vulkan PROPERTIES
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
            LINK_DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/render/backend/vulkan/cellularity_vulkan.exports.map"
    )
    target_link_options(cellularity_vulkan PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/render/backend/vulkan/cellularity_vulkan.exports.map"
    )
    target_compile_definitions(cellularity_vulkan PRIVATE
            CELLULARITY_ENABLE_VULKAN_PROFILING=$<BOOL:${CELLULARITY_ENABLE_VULKAN_PROFILING}>
            "$<$<CONFIG:Debug>:CELLULARITY_VULKAN_VALIDATION=1>"
    )
    target_link_libraries(cellularity_vulkan PRIVATE
            cellularity_vulkan_frame_contract
            cellularity_render_overlay_contract
            cellularity_render_assets
            cellularity_render_frame_result
            android
            log
            ${CELLULARITY_VULKAN_LIBRARY}
    )

    add_library(cellularity_diagnostics SHARED
            diagnostics/NativeCrashHandler.cpp
            diagnostics/NativeCrashReportWriter.cpp
    )

    cellularity_configure_source_target(cellularity_diagnostics)
    cellularity_optimize_target(cellularity_diagnostics)

endif()

# Host builds always expose the probe for local policy calibration. Android app
# builds omit it unless an explicit one-off ABI measurement enables the option.
if(NOT ANDROID OR CELLULARITY_BUILD_MEMORY_PROBE)
    add_executable(cellularity_memory_probe
            benchmarks/cellularity_memory_probe.cpp
    )
    cellularity_configure_source_target(cellularity_memory_probe)
    cellularity_optimize_target(cellularity_memory_probe)
    target_link_libraries(cellularity_memory_probe PRIVATE cellularity_simulation)
endif()

if(NOT ANDROID OR CELLULARITY_BUILD_THROUGHPUT_PROBE)
    add_library(cellularity_stable_digest STATIC
            tests/support/StableDigest.cpp
    )
    cellularity_configure_source_target(cellularity_stable_digest)
endif()

if(CELLULARITY_BUILD_THROUGHPUT_PROBE)
    add_executable(cellularity_throughput_probe
            benchmarks/cellularity_throughput_probe.cpp
            benchmarks/PerfJson.cpp
            benchmarks/PerfState.cpp
    )
    cellularity_configure_source_target(cellularity_throughput_probe)
    cellularity_optimize_target(cellularity_throughput_probe)
    target_include_directories(cellularity_throughput_probe PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
    )
    target_include_directories(cellularity_throughput_probe SYSTEM PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
    )
    target_link_libraries(cellularity_throughput_probe PRIVATE
            cellularity_simulation
            cellularity_formats
            cellularity_stable_digest
    )
endif()

if(CELLULARITY_BUILD_DISPATCHER_LAB)
    set(CELLULARITY_DISPATCHER_SOURCE_REVISION "unavailable")
    set(CELLULARITY_DISPATCHER_SOURCE_DIRTY "unknown")
    find_package(Git QUIET)
    if(Git_FOUND)
        get_filename_component(CELLULARITY_DISPATCHER_REPO_ROOT
                "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
        execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${CELLULARITY_DISPATCHER_REPO_ROOT}"
                        rev-parse --short=12 HEAD
                RESULT_VARIABLE CELLULARITY_DISPATCHER_GIT_REVISION_RESULT
                OUTPUT_VARIABLE CELLULARITY_DISPATCHER_GIT_REVISION
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
        )
        if(CELLULARITY_DISPATCHER_GIT_REVISION_RESULT EQUAL 0)
            set(CELLULARITY_DISPATCHER_SOURCE_REVISION
                    "${CELLULARITY_DISPATCHER_GIT_REVISION}")
            execute_process(
                    COMMAND "${GIT_EXECUTABLE}" -C
                            "${CELLULARITY_DISPATCHER_REPO_ROOT}"
                            status --porcelain --untracked-files=normal
                    OUTPUT_VARIABLE CELLULARITY_DISPATCHER_GIT_STATUS
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
            )
            if(CELLULARITY_DISPATCHER_GIT_STATUS STREQUAL "")
                set(CELLULARITY_DISPATCHER_SOURCE_DIRTY "false")
            else()
                set(CELLULARITY_DISPATCHER_SOURCE_DIRTY "true")
            endif()
        endif()
    endif()

    add_executable(cellularity_dispatcher_lab
            benchmarks/cellularity_dispatcher_lab.cpp
            benchmarks/DispatcherLab.cpp
            benchmarks/PerfJson.cpp
            app/microscope/render/MicroscopeRenderFramePipeline.cpp
    )
    cellularity_configure_source_target(cellularity_dispatcher_lab)
    cellularity_optimize_target(cellularity_dispatcher_lab)
    target_include_directories(cellularity_dispatcher_lab PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
    )
    target_include_directories(cellularity_dispatcher_lab SYSTEM PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
    )
    target_compile_definitions(cellularity_dispatcher_lab PRIVATE
            CELLULARITY_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
            CELLULARITY_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
            CELLULARITY_SYSTEM_PROCESSOR="${CMAKE_SYSTEM_PROCESSOR}"
            CELLULARITY_SOURCE_REVISION="${CELLULARITY_DISPATCHER_SOURCE_REVISION}"
            CELLULARITY_SOURCE_DIRTY="${CELLULARITY_DISPATCHER_SOURCE_DIRTY}"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(cellularity_dispatcher_lab PRIVATE
                -Wno-gnu-zero-variadic-macro-arguments)
    endif()
    target_link_libraries(cellularity_dispatcher_lab PRIVATE
            cellularity_simulation
            cellularity_formats
            cellularity_render_geometry
            cellularity_render_preparation
            cellularity_execution
            Threads::Threads
    )

    add_executable(cellularity_dispatcher_capacity_tests
            tests/dispatcher_capacity_tests.cpp
    )
    cellularity_configure_source_target(cellularity_dispatcher_capacity_tests)
    cellularity_optimize_target(cellularity_dispatcher_capacity_tests)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(cellularity_dispatcher_capacity_tests PRIVATE
                -Wno-gnu-zero-variadic-macro-arguments)
    endif()
    target_link_libraries(cellularity_dispatcher_capacity_tests PRIVATE
            cellularity_execution
    )

    add_executable(cellularity_dispatcher_allocation_tests
            tests/dispatcher_allocation_tests.cpp
    )
    cellularity_configure_source_target(cellularity_dispatcher_allocation_tests)
    cellularity_optimize_target(cellularity_dispatcher_allocation_tests)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(cellularity_dispatcher_allocation_tests PRIVATE
                -Wno-gnu-zero-variadic-macro-arguments)
    endif()
    target_link_libraries(cellularity_dispatcher_allocation_tests PRIVATE
            cellularity_execution
    )

    if(NOT ANDROID)
        add_test(NAME cellularity_dispatcher_capacity_tests
                COMMAND cellularity_dispatcher_capacity_tests)
        set_tests_properties(cellularity_dispatcher_capacity_tests PROPERTIES
                LABELS "throughput;dispatcher")

        add_test(NAME cellularity_dispatcher_allocation_tests
                COMMAND cellularity_dispatcher_allocation_tests)
        set_tests_properties(cellularity_dispatcher_allocation_tests PROPERTIES
                LABELS "throughput;dispatcher;allocations")
    endif()
endif()

if(NOT ANDROID)
    enable_testing()

    # The semantic contract is compiled as a separate host-only copy. Trace
    # calls therefore cannot enter Android or ordinary Profile-OFF objects.
    add_library(cellularity_simulation_semantic STATIC
            ${CELLULARITY_SIMULATION_SOURCES}
            verification/SemanticTrace.cpp
    )
    cellularity_configure_source_target(cellularity_simulation_semantic)
    cellularity_optimize_target(cellularity_simulation_semantic)
    target_compile_definitions(cellularity_simulation_semantic PUBLIC
            CELLULARITY_SEMANTIC_TRACE=1
    )
    target_link_libraries(cellularity_simulation_semantic PRIVATE hwy)
    target_include_directories(cellularity_simulation_semantic SYSTEM PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/highway
    )
    target_compile_definitions(cellularity_simulation_semantic PRIVATE
            CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD=$<BOOL:${CELLULARITY_ENABLE_SIGNAL_BLOCK_SIMD}>
    )
    target_compile_definitions(cellularity_simulation_semantic PUBLIC
            CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL=$<BOOL:${CELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL}>
    )
    if(CELLULARITY_ENABLE_RUNTIME_OVERLAP_SELECTION)
        target_compile_definitions(cellularity_simulation_semantic PUBLIC
                CELLULARITY_RUNTIME_OVERLAP_SELECTION=1
        )
    endif()
    if(CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "compact-record")
        target_compile_definitions(cellularity_simulation_semantic PUBLIC
                CELLULARITY_DEFAULT_COMPACT_RECORD_SWEEP=1
        )
    elseif(CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "compact-primary")
        target_compile_definitions(cellularity_simulation_semantic PUBLIC
                CELLULARITY_DEFAULT_COMPACT_PRIMARY_SWEEP=1
        )
    elseif(CELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM STREQUAL "incremental-bucketed-f64-source32")
        target_compile_definitions(cellularity_simulation_semantic PUBLIC
                CELLULARITY_DEFAULT_INCREMENTAL_BUCKETED_F64_SOURCE32_SWEEP=1
        )
    endif()

    add_library(cellularity_formats_semantic STATIC ${CELLULARITY_FORMAT_SOURCES})
    cellularity_configure_source_target(cellularity_formats_semantic)
    cellularity_optimize_target(cellularity_formats_semantic)
    target_link_libraries(cellularity_formats_semantic PRIVATE
            cellularity_simulation_semantic
            zstd_vendor
            z
    )

    add_executable(cellularity_semantic_contract_tests
            tests/semantic/semantic_contract_tests.cpp
            tests/semantic/SemanticContractFixtures.cpp
    )
    cellularity_configure_source_target(cellularity_semantic_contract_tests)
    target_compile_definitions(cellularity_semantic_contract_tests PRIVATE
            CELLULARITY_SEMANTIC_BASELINE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic/semantic-contract-v1.tsv"
    )
    target_link_libraries(cellularity_semantic_contract_tests PRIVATE
            cellularity_simulation_semantic
            cellularity_formats_semantic
            cellularity_stable_digest
    )
    # The isolation test inspects both archives. Keep the public archive
    # available even when only the semantic test target is requested.
    add_dependencies(cellularity_semantic_contract_tests cellularity_simulation)
    add_test(
            NAME cellularity_semantic_contract_tests
            COMMAND cellularity_semantic_contract_tests
    )
    set_tests_properties(cellularity_semantic_contract_tests PROPERTIES
            LABELS semantic_contract
    )

    add_test(
            NAME cellularity_semantic_trace_isolation
            COMMAND ${CMAKE_COMMAND}
                    -DNM_TOOL=${CMAKE_NM}
                    -DPUBLIC_ARCHIVE=$<TARGET_FILE:cellularity_simulation>
                    -DVERIFICATION_ARCHIVE=$<TARGET_FILE:cellularity_simulation_semantic>
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic/VerifySemanticTraceIsolation.cmake
    )
    set_tests_properties(cellularity_semantic_trace_isolation PROPERTIES
            LABELS semantic_contract
    )

    add_executable(cellularity_relaxed_semantic_contract_tests
            tests/semantic/relaxed_semantic_contract_tests.cpp
            tests/semantic/RelaxedSemanticContract.cpp
    )
    cellularity_configure_source_target(cellularity_relaxed_semantic_contract_tests)
    target_link_libraries(cellularity_relaxed_semantic_contract_tests PRIVATE
            cellularity_simulation_semantic
            cellularity_render_preparation
    )
    add_test(
            NAME cellularity_relaxed_semantic_contract_tests
            COMMAND cellularity_relaxed_semantic_contract_tests
    )
    set_tests_properties(cellularity_relaxed_semantic_contract_tests PROPERTIES
            LABELS semantic_contract
    )

    function(cellularity_add_host_test target_name source_file test_label)
        add_executable(${target_name} ${source_file})
        cellularity_configure_source_target(${target_name})
        target_link_libraries(${target_name} PRIVATE ${ARGN})
        add_test(NAME ${target_name} COMMAND ${target_name})
        set_tests_properties(${target_name} PROPERTIES LABELS ${test_label})
    endfunction()

    function(cellularity_add_vulkan_abi_c_test target_name)
        add_executable(${target_name}
                tests/vulkan_backend_abi_c11_tests.c
        )
        target_include_directories(${target_name} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
        )
        target_compile_features(${target_name} PRIVATE c_std_11)
        target_compile_options(${target_name} PRIVATE
                ${CELLULARITY_PROJECT_WARNING_FLAGS}
                ${ARGN}
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
        set_tests_properties(${target_name} PROPERTIES LABELS vulkan_backend_abi)
    endfunction()

    cellularity_add_host_test(
            cellularity_substrate_geometry_tests
            tests/substrate_geometry_tests.cpp
            substrate_geometry
            cellularity_simulation
    )

    cellularity_add_vulkan_abi_c_test(
            cellularity_vulkan_backend_abi_c11_tests
    )
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        cellularity_add_vulkan_abi_c_test(
                cellularity_vulkan_backend_abi_short_enum_c11_tests
                -fshort-enums
        )
    endif()

    cellularity_add_host_test(
            cellularity_vulkan_backend_abi_tests
            tests/vulkan_backend_abi_tests.cpp
            vulkan_backend_abi
            cellularity_vulkan_backend_client
            Threads::Threads
    )
    cellularity_add_host_test(
            cellularity_native_vulkan_render_worker_tests
            tests/native_vulkan_render_worker_tests.cpp
            native_vulkan_render_worker
            cellularity_render_worker
            cellularity_rendering
            Threads::Threads
    )
    add_test(
            NAME cellularity_vulkan_backend_client_isolation
            COMMAND ${CMAKE_COMMAND}
                    -DNM_TOOL=${CMAKE_NM}
                    -DCLIENT_ARCHIVE=$<TARGET_FILE:cellularity_vulkan_backend_client>
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/VerifyVulkanBackendClientIsolation.cmake
    )
    set_tests_properties(cellularity_vulkan_backend_client_isolation PROPERTIES
            LABELS vulkan_backend_abi
    )

    cellularity_add_host_test(
            cellularity_formats_parity_tests
            tests/parity/formats_tests.cpp
            formats
            cellularity_simulation
            cellularity_formats
            cellularity_render_preparation
    )
    cellularity_add_host_test(
            cellularity_environment_gene_tests
            tests/parity/environment_gene_tests.cpp
            environment_gene
            cellularity_simulation
    )
    cellularity_add_host_test(
            cellularity_environment_batch_contract_tests
            tests/environment_batch_contract_tests.cpp
            environment_batch
            cellularity_simulation
    )
    cellularity_add_host_test(
            cellularity_genome_draft_contract_tests
            tests/genome_draft_contract_tests.cpp
            genome_draft
            cellularity_simulation
            cellularity_formats
    )
    cellularity_add_host_test(
            cellularity_canonical_gene_tests
            tests/canonical_gene_tests.cpp
            canonical_gene
            cellularity_simulation
            cellularity_formats
    )
    cellularity_add_host_test(
            cellularity_gene_pool_tests
            tests/gene_pool_tests.cpp
            gene_pool
            cellularity_simulation
    )
    cellularity_add_host_test(
            cellularity_genome_pool_tests
            tests/genome_pool_tests.cpp
            genome_pool
            cellularity_simulation
    )
    cellularity_add_host_test(
            cellularity_gene_access_tests
            tests/gene_access_tests.cpp
            gene_access
            cellularity_simulation
    )
    cellularity_add_host_test(
            cellularity_topology_tests
            tests/parity/topology_tests.cpp
            topology
            cellularity_simulation
            cellularity_render_geometry
            cellularity_render_preparation
    )
    cellularity_add_host_test(
            cellularity_simulation_tests
            tests/parity/simulation_tests.cpp
            simulation
            cellularity_simulation
            cellularity_render_preparation
    )
    cellularity_add_host_test(
            cellularity_dynamic_genome_operation_tests
            tests/dynamic_genome_operation_tests.cpp
            dynamic_genome_operations
            cellularity_simulation
    )
    # This test intentionally replaces every global new/delete overload. TSan
    # provides strong definitions for the same operators, so the two
    # interceptors cannot coexist in one executable. The allocation contract
    # remains covered by the ordinary and ASan/UBSan configurations.
    if(NOT CELLULARITY_ENABLE_THREAD_SANITIZER)
        cellularity_add_host_test(
                cellularity_steady_state_allocation_tests
                tests/steady_state_allocation_tests.cpp
                allocation
                cellularity_simulation
        )
    endif()
    cellularity_add_host_test(
            cellularity_interaction_tests
            tests/parity/interaction_tests.cpp
            interaction
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
    )
    cellularity_add_host_test(
            cellularity_simulation_scheduler_tests
            tests/simulation_scheduler_tests.cpp
            simulation_scheduler
            cellularity_interaction
    )
    cellularity_add_host_test(
            cellularity_simulation_execution_context_tests
            tests/simulation_execution_context_tests.cpp
            simulation_execution_context
            cellularity_execution
            Threads::Threads
    )
    cellularity_add_host_test(
            cellularity_native_simulation_owner_tests
            tests/native_simulation_owner_tests.cpp
            simulation_owner
            cellularity_interaction
            Threads::Threads
    )
    if(CELLULARITY_ENABLE_NATIVE_PROFILE)
        target_compile_definitions(cellularity_native_simulation_owner_tests PRIVATE
                CELLULARITY_EXPECT_NATIVE_PROFILE=1
        )
    else()
        target_compile_definitions(cellularity_native_simulation_owner_tests PRIVATE
                CELLULARITY_EXPECT_NATIVE_PROFILE=0
        )
    endif()
    cellularity_add_host_test(
            cellularity_microscope_command_mailbox_tests
            tests/microscope_command_mailbox_tests.cpp
            microscope_command_mailbox
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            Threads::Threads
    )
    cellularity_add_host_test(
            cellularity_microscope_input_batch_tests
            tests/microscope_input_batch_tests.cpp
            microscope_input_batch
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            Threads::Threads
    )
    cellularity_add_host_test(
            cellularity_environment_field_preview_state_tests
            tests/environment_field_preview_state_tests.cpp
            environment_field_preview
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            Threads::Threads
    )
    cellularity_add_host_test(
            cellularity_genome_preview_command_infrastructure_tests
            tests/genome_preview_command_infrastructure_tests.cpp
            genome_preview_commands
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            Threads::Threads
    )
    cellularity_add_host_test(
            cellularity_rendering_tests
            tests/parity/rendering_tests.cpp
            rendering
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            cellularity_render_geometry
    )
    cellularity_add_host_test(
            cellularity_gles_render_contract_tests
            tests/gles_render_contract_tests.cpp
            rendering
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            cellularity_render_geometry
            cellularity_vulkan_frame_contract
    )
    cellularity_add_host_test(
            cellularity_render_surface_session_tests
            tests/render_surface_session_tests.cpp
            rendering
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            cellularity_render_geometry
    )
    cellularity_add_host_test(
            cellularity_native_render_attachment_lease_tests
            tests/native_render_attachment_lease_tests.cpp
            render_attachment_lifetime
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            cellularity_render_geometry
            Threads::Threads
    )
    cellularity_add_host_test(
            cellularity_native_preview_endpoint_handle_tests
            tests/native_preview_endpoint_handle_tests.cpp
            preview_endpoint_lifetime
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
            cellularity_render_geometry
            Threads::Threads
    )
    cellularity_add_host_test(
            cellularity_render_asset_texture_loader_tests
            tests/render_asset_texture_loader_tests.cpp
            rendering
            cellularity_render_assets
    )
    target_compile_definitions(cellularity_render_asset_texture_loader_tests PRIVATE
            CELLULARITY_RENDER_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/rendering/assets"
    )
    cellularity_add_host_test(
            cellularity_render_frame_contract_tests
            tests/render_frame_contract_tests.cpp
            render_frame_contract
            cellularity_render_frame_result
    )
    cellularity_add_host_test(
            cellularity_vulkan_basic_cell_frame_tests
            tests/vulkan_basic_cell_frame_tests.cpp
            rendering
            cellularity_vulkan_frame_contract
            cellularity_render_overlay_contract
    )
    cellularity_add_host_test(
            cellularity_render_frame_builder_tests
            tests/render_frame_builder_tests.cpp
            render_frame_builder
            cellularity_simulation
            cellularity_render_preparation
    )
    cellularity_add_host_test(
            cellularity_render_source_frame_tests
            tests/render_source_frame_tests.cpp
            render_source_frame
            cellularity_simulation
            cellularity_render_preparation
    )
    add_test(NAME cellularity_render_source_link_dto_tests
            COMMAND cellularity_render_source_frame_tests --dto-only
    )
    set_tests_properties(cellularity_render_source_link_dto_tests PROPERTIES
            LABELS "render_source_frame;link_dto_contract"
    )
    add_test(NAME cellularity_render_source_normal_link_capture_tests
            COMMAND cellularity_render_source_frame_tests --link-capture-only
    )
    set_tests_properties(cellularity_render_source_normal_link_capture_tests PROPERTIES
            LABELS "render_source_frame;normal_link_capture_contract"
    )
    add_executable(cellularity_boundary_link_capture_tests
            tests/boundary_link_capture_tests.cpp
    )
    cellularity_configure_source_target(cellularity_boundary_link_capture_tests)
    target_link_libraries(cellularity_boundary_link_capture_tests PRIVATE
            cellularity_simulation
            cellularity_rendering
    )
    foreach(boundary_case IN ITEMS
            boundary_only_with_contact
            boundary_only_without_contact
            mixed_order_and_counters
            boundary_culling_and_reuse
            invalid_boundary_owner
    )
        add_test(NAME cellularity_boundary_link_capture_${boundary_case}
                COMMAND cellularity_boundary_link_capture_tests ${boundary_case}
        )
        set_tests_properties(cellularity_boundary_link_capture_${boundary_case} PROPERTIES
                LABELS "render_source_frame;boundary_link_capture_contract"
        )
    endforeach()
    add_executable(cellularity_link_render_contract_tests
            tests/link_render_contract_tests.cpp
    )
    cellularity_configure_source_target(cellularity_link_render_contract_tests)
    target_link_libraries(cellularity_link_render_contract_tests PRIVATE
            cellularity_simulation
            cellularity_render_preparation
    )
    foreach(link_case IN ITEMS
            contact_chord_present
            normalized_contact_chord
            short_contact_chord
            discarded_contact_chord
            no_contact_normal_link
            short_normal_fallback
            unequal_radii_without_ratio_clamp
            raw_pattern_projection
            dead_owner_excluded
            dead_target_excluded
            render_owner_topology_order
            boundary_contact_chord
            boundary_radial_fallback
            zero_center_visual_axis
            near_zero_center_visual_axis
            fully_degenerate_link
            mixed_normal_boundary_capacity
    )
        add_test(NAME cellularity_link_render_${link_case}
                COMMAND cellularity_link_render_contract_tests ${link_case}
        )
        set_tests_properties(cellularity_link_render_${link_case} PROPERTIES
                LABELS "rendering;link_render_contract"
        )
    endforeach()
    add_executable(cellularity_link_geometry_builder_tests
            tests/link_geometry_builder_tests.cpp
    )
    cellularity_configure_source_target(cellularity_link_geometry_builder_tests)
    target_link_libraries(cellularity_link_geometry_builder_tests PRIVATE
            cellularity_render_preparation
    )
    foreach(geometry_case IN ITEMS
            unusable_contact_fallback
            fallback_axis_policy
            finite_output_and_atomic_append
            profile_and_immutable_resolver
    )
        add_test(NAME cellularity_link_geometry_builder_${geometry_case}
                COMMAND cellularity_link_geometry_builder_tests ${geometry_case}
        )
        set_tests_properties(cellularity_link_geometry_builder_${geometry_case} PROPERTIES
                LABELS "rendering;link_geometry_builder_contract"
        )
    endforeach()
    add_executable(cellularity_renderer_link_bounds_tests
            tests/renderer_link_bounds_tests.cpp
    )
    cellularity_configure_source_target(cellularity_renderer_link_bounds_tests)
    target_link_libraries(cellularity_renderer_link_bounds_tests PRIVATE
            cellularity_simulation
            cellularity_render_preparation
    )
    foreach(bounds_case IN ITEMS
            logical_bounds_and_initial_sizes
            upload_byte_boundaries
            file_and_admission_policy
            boundary_only_full_capacity
            source_capacity_preflight
            prepared_candidate_preflight
            auxiliary_capacity_preflight
            unsupported_constructor_and_reset
    )
        add_test(NAME cellularity_renderer_link_bounds_${bounds_case}
                COMMAND cellularity_renderer_link_bounds_tests ${bounds_case}
        )
        set_tests_properties(cellularity_renderer_link_bounds_${bounds_case} PROPERTIES
                LABELS "rendering;memory_bounds"
        )
    endforeach()
    cellularity_add_host_test(
            cellularity_render_frame_exchange_tests
            tests/render_frame_exchange_tests.cpp
            render_frame_exchange
            cellularity_render_preparation
    )
    cellularity_add_host_test(
            cellularity_microscope_render_frame_pipeline_tests
            tests/microscope_render_frame_pipeline_tests.cpp
            microscope_render_frame_pipeline
            cellularity_simulation
            cellularity_interaction
            cellularity_rendering
    )

    add_executable(cellularity_native_diagnostics_tests
            tests/diagnostics/native_crash_report_writer_tests.cpp
            diagnostics/NativeCrashReportWriter.cpp
    )
    cellularity_configure_source_target(cellularity_native_diagnostics_tests)
    add_test(
            NAME cellularity_native_diagnostics_tests
            COMMAND cellularity_native_diagnostics_tests
    )
    set_tests_properties(cellularity_native_diagnostics_tests PROPERTIES LABELS diagnostics)

    add_library(cellularity_behavior_baseline_support STATIC
            tests/baseline/BehaviorBaseline.cpp
    )
    cellularity_configure_source_target(cellularity_behavior_baseline_support)
    target_link_libraries(cellularity_behavior_baseline_support PRIVATE
            cellularity_simulation
            cellularity_formats
            cellularity_interaction
            cellularity_stable_digest
    )

    add_executable(cellularity_behavior_baseline_tests
            tests/baseline/behavior_baseline_tests.cpp
    )
    cellularity_configure_source_target(cellularity_behavior_baseline_tests)
    target_link_libraries(cellularity_behavior_baseline_tests PRIVATE
            cellularity_behavior_baseline_support
            cellularity_simulation
    )
    add_test(NAME cellularity_behavior_baseline_tests COMMAND cellularity_behavior_baseline_tests)
    set_tests_properties(cellularity_behavior_baseline_tests PROPERTIES LABELS simulation)

    add_library(cellularity_format_test_support STATIC
            tests/fixtures/FormatFixtureCorpus.cpp
    )
    cellularity_configure_source_target(cellularity_format_test_support)
    target_link_libraries(cellularity_format_test_support PRIVATE cellularity_stable_digest z)

    add_executable(cellularity_format_fixture_tool
            tests/fixtures/format_fixture_tool.cpp
    )
    cellularity_configure_source_target(cellularity_format_fixture_tool)
    target_link_libraries(cellularity_format_fixture_tool PRIVATE cellularity_format_test_support)
    add_test(
            NAME cellularity_format_fixture_corpus
            COMMAND cellularity_format_fixture_tool
                    --verify
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/corpus
    )
    set_tests_properties(cellularity_format_fixture_corpus PROPERTIES LABELS formats)

    add_executable(cellularity_format_semantic_tests
            tests/format_semantic_tests.cpp
    )
    cellularity_configure_source_target(cellularity_format_semantic_tests)
    target_compile_definitions(cellularity_format_semantic_tests PRIVATE
            CELLULARITY_FIXTURE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/corpus"
            CELLULARITY_LEGACY_EFFECTIVE_STATE_ORACLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/baseline/legacy-effective-state-v1.tsv"
    )
    target_link_libraries(cellularity_format_semantic_tests PRIVATE
            cellularity_simulation
            cellularity_formats
            cellularity_render_preparation
            cellularity_format_test_support
            cellularity_stable_digest
    )
    add_test(NAME cellularity_format_semantic_tests COMMAND cellularity_format_semantic_tests)
    set_tests_properties(cellularity_format_semantic_tests PROPERTIES LABELS formats)

    add_executable(cellularity_format_malformed_tests
            tests/format_malformed_tests.cpp
    )
    cellularity_configure_source_target(cellularity_format_malformed_tests)
    target_compile_definitions(cellularity_format_malformed_tests PRIVATE
            CELLULARITY_FIXTURE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/corpus"
    )
    target_link_libraries(cellularity_format_malformed_tests PRIVATE
            cellularity_simulation
            cellularity_formats
            cellularity_format_test_support
    )
    add_test(NAME cellularity_format_malformed_tests COMMAND cellularity_format_malformed_tests)
    set_tests_properties(cellularity_format_malformed_tests PROPERTIES LABELS formats)

    add_executable(cellularity_release_genome_codec_tests
            tests/release_genome_codec_tests.cpp
    )
    cellularity_configure_source_target(cellularity_release_genome_codec_tests)
    target_compile_definitions(cellularity_release_genome_codec_tests PRIVATE
            CELLULARITY_RELEASE_FORMAT_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/../testdata/release-format"
            CELLULARITY_RELEASE_FORMAT_REGISTRY_PATH="${CMAKE_SOURCE_DIR}/../testdata/release-format/registry-v1.tsv"
    )
    target_link_libraries(cellularity_release_genome_codec_tests PRIVATE
            cellularity_simulation
            cellularity_formats
    )
    add_test(
            NAME cellularity_release_genome_codec_tests
            COMMAND cellularity_release_genome_codec_tests
    )
    set_tests_properties(
            cellularity_release_genome_codec_tests
            PROPERTIES LABELS formats
    )

    add_executable(cellularity_release_substrate_codec_tests
            tests/release_substrate_codec_tests.cpp
    )
    cellularity_configure_source_target(cellularity_release_substrate_codec_tests)
    target_compile_definitions(cellularity_release_substrate_codec_tests PRIVATE
            CELLULARITY_RELEASE_FORMAT_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/../testdata/release-format"
    )
    target_link_libraries(cellularity_release_substrate_codec_tests PRIVATE
            cellularity_simulation
            cellularity_formats
    )
    add_test(
            NAME cellularity_release_substrate_codec_tests
            COMMAND cellularity_release_substrate_codec_tests
    )
    set_tests_properties(
            cellularity_release_substrate_codec_tests
            PROPERTIES LABELS formats
    )

    add_executable(cellularity_perf
            benchmarks/cellularity_perf.cpp
            benchmarks/ThroughputWorkloads.cpp
            benchmarks/PerfJson.cpp
            benchmarks/PerfScenarios.cpp
            benchmarks/PerfOverlap.cpp
            benchmarks/PerfSignal.cpp
            benchmarks/PerfState.cpp
    )
    cellularity_configure_source_target(cellularity_perf)
    target_include_directories(cellularity_perf PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
    )
    target_include_directories(cellularity_perf SYSTEM PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
    )
    target_link_libraries(cellularity_perf PRIVATE
            cellularity_simulation
            cellularity_formats
            cellularity_interaction
            cellularity_rendering
            cellularity_stable_digest
    )

    add_executable(cellularity_behavior_performance
            benchmarks/behavior_performance.cpp
            benchmarks/PerfJson.cpp
    )
    cellularity_configure_source_target(cellularity_behavior_performance)
    set(CELLULARITY_SOURCE_REVISION "unavailable")
    set(CELLULARITY_SOURCE_DIRTY "unknown")
    find_package(Git QUIET)
    if(Git_FOUND)
        get_filename_component(CELLULARITY_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
        execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${CELLULARITY_REPO_ROOT}" rev-parse --short=12 HEAD
                RESULT_VARIABLE CELLULARITY_GIT_REVISION_RESULT
                OUTPUT_VARIABLE CELLULARITY_GIT_REVISION
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
        )
        if(CELLULARITY_GIT_REVISION_RESULT EQUAL 0)
            set(CELLULARITY_SOURCE_REVISION "${CELLULARITY_GIT_REVISION}")
            execute_process(
                    COMMAND "${GIT_EXECUTABLE}" -C "${CELLULARITY_REPO_ROOT}" status --porcelain --untracked-files=normal
                    OUTPUT_VARIABLE CELLULARITY_GIT_STATUS
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
            )
            if(CELLULARITY_GIT_STATUS STREQUAL "")
                set(CELLULARITY_SOURCE_DIRTY "false")
            else()
                set(CELLULARITY_SOURCE_DIRTY "true")
            endif()
        endif()
    endif()
    target_include_directories(cellularity_behavior_performance PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks
    )
    target_include_directories(cellularity_behavior_performance SYSTEM PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
    )
    target_compile_definitions(cellularity_behavior_performance PRIVATE
            CELLULARITY_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
            CELLULARITY_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
            CELLULARITY_SYSTEM_PROCESSOR="${CMAKE_SYSTEM_PROCESSOR}"
            CELLULARITY_SOURCE_REVISION="${CELLULARITY_SOURCE_REVISION}"
            CELLULARITY_SOURCE_DIRTY="${CELLULARITY_SOURCE_DIRTY}"
    )
    target_link_libraries(cellularity_behavior_performance PRIVATE
            cellularity_behavior_baseline_support
            cellularity_simulation
    )

    add_executable(genome_development_lab
            tools/development_lab/genome_development_lab.cpp
    )
    cellularity_configure_source_target(genome_development_lab)
    target_link_libraries(genome_development_lab PRIVATE
            cellularity_simulation
            cellularity_interaction
            cellularity_formats
    )

    if(CELLULARITY_ENABLE_FUZZING)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "CELLULARITY_ENABLE_FUZZING requires Clang")
        endif()

        foreach(FUZZ_LIBRARY IN ITEMS cellularity_simulation cellularity_formats)
            target_compile_options(${FUZZ_LIBRARY} PRIVATE
                    -fsanitize=fuzzer-no-link,address,undefined
                    -fno-omit-frame-pointer
            )
        endforeach()

        add_executable(cellularity_release_substrate_fuzzer
                tests/fuzz/release_substrate_fuzzer.cpp
        )
        cellularity_configure_source_target(cellularity_release_substrate_fuzzer)
        add_executable(cellularity_legacy_java_stream_fuzzer
                tests/fuzz/legacy_java_stream_fuzzer.cpp
        )
        cellularity_configure_source_target(cellularity_legacy_java_stream_fuzzer)
        foreach(FUZZ_TARGET IN ITEMS
                cellularity_release_substrate_fuzzer
                cellularity_legacy_java_stream_fuzzer)
            target_link_libraries(${FUZZ_TARGET} PRIVATE
                    cellularity_simulation
                    cellularity_formats
            )
            target_compile_options(${FUZZ_TARGET} PRIVATE
                    -fsanitize=fuzzer,address,undefined
                    -fno-omit-frame-pointer
            )
            target_link_options(${FUZZ_TARGET} PRIVATE
                    -fsanitize=fuzzer,address,undefined
                    -fno-omit-frame-pointer
            )
        endforeach()
    endif()
endif()
