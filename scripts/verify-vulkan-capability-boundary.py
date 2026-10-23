#!/usr/bin/env python3
"""Verify opt-in Vulkan production paths and the default GLES path."""

from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"Vulkan boundary error: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative_path: str) -> str:
    path = REPO_ROOT / relative_path
    if not path.is_file():
        fail(f"required file is missing: {relative_path}")
    return path.read_text(encoding="utf-8")


def require(source: str, snippet: str, owner: str) -> None:
    if snippet not in source:
        fail(f"{owner} is missing: {snippet}")


def forbid(source: str, snippet: str, owner: str) -> None:
    if snippet in source:
        fail(f"{owner} is still present: {snippet}")


def cmake_android_lines(cmake: str) -> set[int]:
    stack: list[str] = []
    android_lines: set[int] = set()
    for number, line in enumerate(cmake.splitlines(), start=1):
        stripped = line.strip()
        if_match = re.match(r"if\s*\((.*)", stripped, re.IGNORECASE)
        if if_match:
            stack.append(if_match.group(1).strip().rstrip(")").strip())
        if any(condition == "ANDROID" for condition in stack):
            android_lines.add(number)
        if re.match(r"endif(?:\s*\(.*\))?\s*$", stripped, re.IGNORECASE):
            if not stack:
                fail(f"unbalanced CMake endif at line {number}")
            stack.pop()
    if stack:
        fail("unbalanced CMake if block")
    return android_lines


def verify_android_only_build_boundary() -> None:
    cmake = read("native/CMakeLists.txt")
    android_lines = cmake_android_lines(cmake)
    required_fragments = (
        "find_library(CELLULARITY_VULKAN_LIBRARY NAMES vulkan REQUIRED)",
        "add_library(cellularity_vulkan SHARED",
        "jni/AndroidJniVulkanCapabilityProbe.cpp",
        "platform/android/vulkan/AndroidVulkanCapabilityProbe.cpp",
        "render/backend/vulkan/AndroidVulkanClearFrameSession.cpp",
        "render/backend/vulkan/VulkanBackendAbiExports.cpp",
        "render/backend/vulkan/VulkanBasicCellRenderer.cpp",
        "render/backend/vulkan/VulkanResourceFoundation.cpp",
        "render/backend/vulkan/VulkanResult.cpp",
        "${CELLULARITY_VULKAN_LIBRARY}",
    )
    for fragment in required_fragments:
        matching_lines = [
            number
            for number, line in enumerate(cmake.splitlines(), start=1)
            if fragment in line
        ]
        if not matching_lines:
            fail(f"native/CMakeLists.txt is missing: {fragment}")
        if any(number not in android_lines for number in matching_lines):
            fail(f"Vulkan build fragment is not Android-only: {fragment}")

    main_target = re.search(
        r"add_library\(cellularity_android\s+SHARED(?P<body>.*?)"
        r"target_link_libraries\(cellularity_android\s+PRIVATE(?P<links>.*?)\n\s*\)",
        cmake,
        re.DOTALL,
    )
    if main_target is None:
        fail("could not inspect the production GLES JNI target")
    if "CELLULARITY_VULKAN_LIBRARY" in main_target.group("links") \
            or re.search(r"(?m)^\s*cellularity_vulkan\s*$", main_target.group("links")):
        fail("the production JNI target must not depend on the Vulkan loader/DSO")
    require(
        main_target.group("links"),
        "cellularity_vulkan_backend_client",
        "main-side Vulkan C-ABI client",
    )

    host_source_lists = re.sub(
        r"if\(ANDROID\).*?^endif\(\)",
        "",
        cmake,
        flags=re.DOTALL | re.MULTILINE,
    )
    if "AndroidVulkanCapabilityProbe" in host_source_lists \
            or "AndroidVulkanClearFrameSession" in host_source_lists \
            or "VulkanBasicCellRenderer" in host_source_lists \
            or "VulkanResourceFoundation" in host_source_lists:
        fail("host CMake source lists include the Android Vulkan backend")

    for fragment in (
        "add_library(cellularity_vulkan_frame_contract STATIC",
        "render/backend/vulkan/VulkanBasicCellFrame.cpp",
        "cellularity_vulkan_basic_cell_frame_tests",
    ):
        require(cmake, fragment, "cross-platform Vulkan frame contract")


def verify_native_probe_scope() -> None:
    source = read("native/platform/android/vulkan/AndroidVulkanCapabilityProbe.cpp")
    result_source = read("native/render/backend/vulkan/VulkanResult.cpp")
    header = read("native/platform/android/vulkan/AndroidVulkanCapabilityProbe.h")
    jni = read("native/jni/AndroidJniVulkanCapabilityProbe.cpp")

    for snippet in (
        "VK_API_VERSION_1_0",
        "vkEnumerateInstanceVersion",
        "vkEnumeratePhysicalDevices",
        "vkGetPhysicalDeviceQueueFamilyProperties",
        "vkGetPhysicalDeviceSurfaceSupportKHR",
        "VK_KHR_SWAPCHAIN_EXTENSION_NAME",
        "vkGetPhysicalDeviceSurfaceFormatsKHR",
        "vkGetPhysicalDeviceFormatProperties",
        "vkDestroySurfaceKHR",
        "vkDestroyInstance",
        '"live_vulkan_objects_after_probe"',
        'std::string_view(statusName(report.status))',
    ):
        require(source, snippet, "native Vulkan capability probe")
    require(result_source, "resultName(VkResult result)", "shared Vulkan result diagnostics")

    for advanced_api in (
        "vkCreateDevice",
        "vkCreateSwapchainKHR",
        "VkPhysicalDeviceTimelineSemaphoreFeatures",
        "VkPhysicalDeviceDynamicRenderingFeatures",
        "VkPhysicalDeviceDescriptorIndexingFeatures",
        "VK_KHR_DYNAMIC_RENDERING",
        "VK_KHR_TIMELINE_SEMAPHORE",
        "VK_EXT_DESCRIPTOR_INDEXING",
    ):
        if advanced_api in source:
            fail(f"capability probe creates or requires a later-phase feature: {advanced_api}")

    if source.index("vkDestroySurfaceKHR") > source.index("vkDestroyInstance"):
        fail("Vulkan surface must be destroyed before its instance")
    require(header, "ANativeWindow*", "native Vulkan capability probe contract")
    require(jni, "ANativeWindow_release", "Vulkan JNI window ownership")
    require(jni, "catch (...)" , "Vulkan JNI failure boundary")


def verify_backend_c_abi() -> None:
    cmake = read("native/CMakeLists.txt")
    abi = read("native/render/backend/abi/CellularityVulkanBackendAbi.h")
    client = read("native/render/backend/abi/VulkanBackendSession.cpp")
    validation = read(
        "native/render/backend/abi/VulkanBackendAbiValidation.h"
    )
    error_helpers = read(
        "native/render/backend/abi/VulkanBackendAbiError.h"
    )
    adapter = read(
        "native/render/backend/abi/VulkanRenderSurfaceSessionAdapter.cpp"
    )
    backend_exports = read(
        "native/render/backend/vulkan/VulkanBackendAbiExports.cpp"
    )
    export_map = read(
        "native/render/backend/vulkan/cellularity_vulkan.exports.map"
    )
    loader = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanBackendLoader.java"
    )
    worker_bridge = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanRenderWorkerBridge.kt"
    )
    worker_jni_java = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanRenderWorkerJni.java"
    )
    worker_jni = read("native/jni/NativeVulkanRenderWorkerJni.cpp")
    diagnostics_layout = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanDiagnosticsChunkLayout.kt"
    )
    worker_status_layout = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanWorkerStatusLayout.kt"
    )
    runtime_diagnostics = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanRuntimeDiagnostics.kt"
    )
    c_fixture = read("native/tests/vulkan_backend_abi_c11_tests.c")
    cpp_fixture = read("native/tests/vulkan_backend_abi_tests.cpp")

    for snippet in (
        "struct CellularityVulkanSessionOpaque;",
        "typedef struct CellularityVulkanSessionOpaque*",
        "CELLULARITY_VULKAN_ABI_VERSION_V1",
        "CELLULARITY_VULKAN_SURFACE_API_V1_REQUIRED_SIZE",
        "CellularityPreparedRenderFrameViewV1",
        "CellularityVulkanGetSurfaceApiV1Fn",
        "cellularity_vulkan_get_surface_api_v1(void)",
    ):
        require(abi, snippet, "versioned Vulkan backend C ABI")
    for wire_type in (
        "CellularityVulkanOperationStatusV1",
        "CellularityVulkanSurfaceOutcomeV1",
        "CellularityVulkanPassStatusV1",
        "CellularityVulkanDiagnosticsStatusV1",
        "CellularityVulkanErrorCodeV1",
        "CellularityVulkanOffscreenTargetV1",
    ):
        require(abi, f"typedef uint32_t {wire_type};", "fixed-width Vulkan ABI")
    if re.search(r"\b(?:typedef\s+)?enum\b", abi):
        fail("the public Vulkan backend ABI must not expose a C enum")

    for snippet in (
        "add_library(cellularity_vulkan_backend_client STATIC",
        "render/backend/abi/VulkanBackendSession.cpp",
        "render/backend/abi/VulkanRenderSurfaceSessionAdapter.cpp",
        "CXX_VISIBILITY_PRESET hidden",
        "cellularity_vulkan.exports.map",
    ):
        require(cmake, snippet, "isolated Vulkan backend ABI build")
    for snippet in (
        "cellularity_vulkan_get_surface_api_v1;",
        "NativeVulkanBackendLoader_nativeBackendApiV1Address;",
        "NativeVulkanCapabilityBridge_nativeProbe;",
        "local:",
        "*;",
    ):
        require(export_map, snippet, "Vulkan DSO export allowlist")

    for obsolete_path in (
        "native/jni/AndroidJniVulkanClearFrame.cpp",
        "native/render/backend/vulkan/VulkanRenderSurfaceSession.h",
        "native/render/backend/vulkan/VulkanRenderSurfaceSession.cpp",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanClearFrameBridge.java",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanBackendSessionBridge.java",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameNativeApi.kt",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameRenderThread.kt",
        "native/jni/NativeVulkanBackendSessionHandle.cpp",
        "native/jni/NativeVulkanBackendSessionJni.cpp",
    ):
        if REPO_ROOT.joinpath(obsolete_path).exists():
            fail(f"obsolete cross-DSO C++ bridge remains: {obsolete_path}")

    for snippet in (
        "copied.retain_api(copied.api_context)",
        "copied.create_session(&info, &error)",
        "copied.destroy_session(session)",
        "mApi.release_api(mApi.api_context)",
    ):
        require(client, snippet, "main-side Vulkan API owner")
    for snippet in (
        "count > std::numeric_limits<std::size_t>::max() / sizeof(T)",
        "reinterpret_cast<std::uintptr_t>(data) % alignof(T) == 0",
        "matrix.row_stride_elements < matrix.columns",
    ):
        require(validation, snippet, "Vulkan ABI borrowed-span validation")
    for snippet in (
        "validUtf8Prefix",
        "std::memset(output, 0, sizeof(*output))",
        "CELLULARITY_VULKAN_ERROR_MESSAGE_TRUNCATED_V1",
    ):
        require(error_helpers, snippet, "deterministic Vulkan ABI errors")
    for snippet in (
        "destination.cell_triangle_coords = floatSpan(source.cellTriangleCoords)",
        "destination.cell_indices = {source.cellIndices.data()",
        "destination.bunches = intSpan(source.bunches)",
    ):
        require(adapter, snippet, "zero-copy Vulkan prepared-frame adapter")
    require(
        backend_exports,
        "cellularity_vulkan_get_surface_api_v1(void)",
        "Vulkan backend API getter",
    )

    expected_natives = {
        "nativeCreateWorkerControlBlock",
        "nativeAuthorizeMicroscopeWorker",
        "nativeStartWorker",
        "nativeAttachSurface",
        "nativeResizeSurface",
        "nativeDetachSurface",
        "nativeResume",
        "nativePause",
        "nativeRequestFrame",
        "nativeSetFramePacingMode",
        "nativeSetDemoPerformanceHudEnabled",
        "nativeReadCompactStatus",
        "nativeRequestDiagnostics",
        "nativeReadDiagnosticsChunk",
        "nativeReleaseDiagnostics",
        "nativeRequestStop",
        "nativeAwaitStopped",
        "nativeFinalizeWorkerBinding",
        "nativeDestroyStoppedWorker",
    }
    declarations = set(
        re.findall(r"\bstatic\s+native\s+\S+\s+(native\w+)\s*\(", worker_jni_java)
    )
    if declarations != set(expected_natives):
        fail(
            "native Vulkan worker JNI declarations differ: "
            f"expected={sorted(expected_natives)}, actual={sorted(declarations)}"
        )
    for method in expected_natives:
        symbol = (
            "Java_com_cytarch_cellularity_platform_render_vulkan_"
            f"NativeVulkanRenderWorkerJni_{method}"
        )
        if worker_jni.count(symbol) != 1:
            fail(f"native Vulkan worker JNI must define {symbol} exactly once")

    managed_sources = "\n".join(
        path.read_text(encoding="utf-8")
        for suffix in ("*.java", "*.kt")
        for path in (REPO_ROOT / "app/src/main/java").rglob(suffix)
    )
    if managed_sources.count('System.loadLibrary("cellularity_vulkan")') != 1:
        fail("the optional Vulkan DSO must have exactly one managed loader")
    require(loader, "private static final LoadResult LOAD_RESULT", "immutable Vulkan loader result")
    require(cmake, "-fshort-enums", "short-enum Vulkan ABI fixture")
    require(c_fixture, "sizeof(CellularityVulkanErrorV1) == 536", "C11 Vulkan ABI fixture")
    require(cpp_fixture, "geometryPointersBorrowed", "Vulkan ABI zero-copy fixture")
    require(cpp_fixture, "UINT64_MAX", "Vulkan ABI span-overflow fixture")
    require(cpp_fixture, "partial UTF-8 code point", "Vulkan ABI UTF-8 fixture")
    for snippet in (
        "MAGIC = 0x31434456",
        "HEADER_SIZE = 32",
        "MAXIMUM_PAYLOAD_SIZE = 64 * 1024",
    ):
        require(diagnostics_layout, snippet, "VDC1 diagnostics")
    for snippet in (
        "NativeVulkanWorkerStatus",
        "SIZE = 96",
        "Phase.RECOVERABLE_FAILURE",
    ):
        require(worker_status_layout, snippet, "native Vulkan worker status")
    for snippet in (
        "NativeVulkanRenderWorkerBridge",
        "NativeRenderWorkerCloseCoordinator.closeAsync",
        "NativeVulkanFailureDiagnosticsReader",
    ):
        require(worker_bridge, snippet, "native Vulkan worker bridge")
    for snippet in (
        'backend_abi_version=',
        'backend_api_required_size=',
        'backend_abi_status=',
    ):
        require(runtime_diagnostics, snippet, "Vulkan ABI diagnostics report")


def verify_prepared_frame_session() -> None:
    source = read("native/render/backend/vulkan/AndroidVulkanClearFrameSession.cpp")
    header = read("native/render/backend/vulkan/AndroidVulkanClearFrameSession.h")
    cell_frame_header = read(
        "native/render/backend/vulkan/VulkanBasicCellFrame.h"
    )
    cell_frame_source = read(
        "native/render/backend/vulkan/VulkanBasicCellFrame.cpp"
    )
    cell_renderer_header = read(
        "native/render/backend/vulkan/VulkanBasicCellRenderer.h"
    )
    cell_renderer_source = read(
        "native/render/backend/vulkan/VulkanBasicCellRenderer.cpp"
    )
    debug_hud_header = read(
        "native/app/microscope/render/MicroscopeDebugHud.h"
    )
    debug_hud_source = read(
        "native/app/microscope/render/MicroscopeDebugHud.cpp"
    )
    pixel_text_header = read("native/render/text/PixelTextRenderer.h")
    pixel_text_source = read("native/render/text/PixelTextGeometry.cpp")
    foundation_header = read(
        "native/render/backend/vulkan/VulkanResourceFoundation.h"
    )
    foundation_source = read(
        "native/render/backend/vulkan/VulkanResourceFoundation.cpp"
    )
    backend_exports = read(
        "native/render/backend/vulkan/VulkanBackendAbiExports.cpp"
    )
    worker_jni = read("native/jni/NativeVulkanRenderWorkerJni.cpp")
    adapter_header = read(
        "native/render/backend/abi/VulkanRenderSurfaceSessionAdapter.h"
    )
    adapter_source = read(
        "native/render/backend/abi/VulkanRenderSurfaceSessionAdapter.cpp"
    )

    for snippet in (
        "VK_API_VERSION_1_0",
        "VK_PRESENT_MODE_FIFO_KHR",
        "kFramesInFlight = 2",
        "vkCreateAndroidSurfaceKHR",
        "vkCreateDevice",
        "vkCreateSwapchainKHR",
        "vkCreateImageView",
        "vkCreateRenderPass",
        "vkCreateShaderModule",
        "vkCreatePipelineLayout",
        "vkCreateGraphicsPipelines",
        "vkCreateFramebuffer",
        "vkCreateSemaphore",
        "vkCreateFence",
        "vkCreateDescriptorSetLayout",
        "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC",
        "vkUpdateDescriptorSets",
        "vkAcquireNextImageKHR",
        "vkQueueSubmit",
        "vkQueuePresentKHR",
        "vkCmdBindPipeline",
        "vkCmdBindDescriptorSets",
        "vkCmdDraw(commandBuffer, 6, 1, 0, 0)",
        "basicCellRenderer.record(",
        "basicCellRenderer.uploadGeometry(",
        "basicCellRenderer.uploadTextGeometry(",
        "debugHud.prepareGeometry(",
        "makeVulkanBasicCellDemoFrame()",
        "const PreparedRenderFrameView& preparedFrame",
        "const RenderPresentationState& presentation",
        "BackgroundUniformData",
        "CellUniformData",
        "VulkanUploadArena",
        "renderFinishedSemaphores[imageIndex]",
        "steady_state_resource_create_count=",
        "synchronization_reuse_violation_count=",
        "resource_accounting_consistent=",
        "clip_y_flipped=true",
        "clip_depth_converted_to_zero_one=true",
        "VK_ERROR_OUT_OF_DATE_KHR",
        "VK_SUBOPTIMAL_KHR",
        "VK_ERROR_SURFACE_LOST_KHR",
        "capabilities.currentTransform",
        "requestedWidth <= 0",
        "destroySwapchain();",
    ):
        require(source, snippet, "native Vulkan prepared-frame session")

    for snippet in (
        "VulkanPreparedFrameUploadIdentity",
        "frameAddress",
        "layoutVersion",
        "worldEpoch",
        "modeGeneration",
        "captureSequence",
        "geometryRevision",
        "instanceRevision",
        "markerRevision",
        "VulkanBasicCellUploadPlan",
        "VulkanWorldProgram",
        "foodInstances",
        "deadInstances",
        "linkVertices",
        "markerInstances",
        "markerInstanceCount",
        "arrowDraws",
        "outlineDraws",
        "CellTypes::length * kMaximumDrawBunches",
        "buildVulkanBasicCellUploadPlan",
        "makeVulkanBasicCellDemoFrame",
        "makeVulkanBasicCellDemoPresentation",
    ):
        require(cell_frame_header, snippet, "Vulkan basic-cell frame contract")
    for snippet in (
        "frame.cellTriangleCoords",
        "frame.cellTextureCoords",
        "frame.cellCenterCoords",
        "frame.cellColors",
        "frame.cellIndices",
        "frame.bunchStart",
        "frame.bunchIndexStart",
        "frame.bunchIndexSize",
        "frame.foodInstanceAttributes",
        "frame.foodVertexAttributes",
        "frame.deadCellInstanceAttributes",
        "frame.deadCellVertexAttributes",
        "frame.linkTriangleCoords",
        "frame.linkTextureCoords",
        "frame.markers",
        "frame.layout.geometryRevision",
        "frame.layout.instanceRevision",
        "frame.layout.markerRevision",
    ):
        require(cell_frame_source, snippet, "Vulkan basic-cell frame mapping")
    for snippet in (
        "kGeometrySlotCount = 2",
        "kTextGeometrySlotCount = 2",
        "needsGeometryUpload",
        "requiredGeometryWaitSerial",
        "markActiveGeometryUsed",
        "needsTextGeometryUpload",
        "requiredTextGeometryWaitSerial",
        "markActiveTextGeometryUsed",
        "geometryUploadSkipCount",
        "kVulkanWorldProgramCount",
        "bool blendBasicCells = true;",
        "blendedBasicCells",
        "opaqueBasicCells",
        "mPipelineSets",
        "besselTextureWidth",
        "outlineDrawCount",
        "markerDrawCount",
        "textDrawCount",
        "textGeometryBufferGrowthCount",
        "glyphAtlasWidth",
    ):
        require(cell_renderer_header, snippet, "Vulkan basic-cell renderer contract")
    for snippet in (
        "VK_VERTEX_INPUT_RATE_VERTEX",
        "VK_FORMAT_R32G32_SFLOAT",
        "VK_FORMAT_R32G32B32A32_SFLOAT",
        "VK_INDEX_TYPE_UINT16",
        "VK_COMPARE_OP_ALWAYS",
        "VK_BLEND_FACTOR_SRC_ALPHA",
        "VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA",
        "basicCellBlend.blendEnable = blendEnabled;",
        "options.blendBasicCells",
        "VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT",
        "VulkanWorldProgram::Food",
        "VulkanWorldProgram::Link",
        "VulkanWorldProgram::Marker",
        "VulkanWorldProgram::PixelText",
        "VulkanWorldProgram::Dead",
        "VulkanWorldProgram::Arrow",
        "VK_COMPARE_OP_LESS",
        "vkCmdDraw(commandBuffer, 6, slot.plan.foodInstanceCount",
        "vkCmdDrawIndexed",
        "VulkanPreparedFrameUploadIdentity::from(frame)",
        "std::memcpy",
        "NativeRenderTexturePixelFormat::R8",
        "VK_FILTER_NEAREST",
        "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER",
    ):
        require(cell_renderer_source, snippet, "Vulkan basic-cell renderer")
    require(
        source,
        "drawOptions.blendBasicCells = false;",
        "Vulkan Genome Preview opaque-cell parity",
    )
    if "vkDeviceWaitIdle" in cell_renderer_source \
            or "vkQueueWaitIdle" in cell_renderer_source:
        fail("world renderer performs a whole-device or whole-queue wait")

    for snippet in (
        "class VulkanDeviceContext",
        "class VulkanUniqueDeviceHandle",
        "class VulkanDeviceMemory",
        "class VulkanBuffer",
        "class VulkanImage",
        "using VulkanImageView",
        "using VulkanSampler",
        "using VulkanShaderModule",
        "using VulkanDescriptorSetLayout",
        "class VulkanDescriptorPool",
        "class VulkanDescriptorSet",
        "using VulkanPipelineLayout",
        "using VulkanPipeline",
        "class VulkanCommandPool",
        "class VulkanCommandBuffer",
        "using VulkanSemaphore",
        "using VulkanFence",
        "class VulkanDeferredReleaseQueue",
        "class VulkanUploadArena",
        "deferredReleaseRetireCount",
    ):
        require(foundation_header, snippet, "Vulkan RAII resource foundation")
    for snippet in (
        "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT",
        "VK_MEMORY_PROPERTY_HOST_COHERENT_BIT",
        "vkMapMemory",
        "vkFlushMappedMemoryRanges",
        "vkAllocateDescriptorSets",
        "vkAllocateCommandBuffers",
        "uploadArenaGrowthCount",
    ):
        require(foundation_source, snippet, "Vulkan resource implementation")
    if "vkCmdPushConstants" in source or "vkCmdPushConstants" in cell_renderer_source:
        fail("V-10 prepared-frame path bypasses the descriptor/upload foundation")

    for unsupported_api in (
        "VkPhysicalDeviceTimelineSemaphoreFeatures",
        "VkPhysicalDeviceDynamicRenderingFeatures",
        "VkPhysicalDeviceDescriptorIndexingFeatures",
        "VK_KHR_DYNAMIC_RENDERING",
        "VK_KHR_TIMELINE_SEMAPHORE",
        "VK_EXT_DESCRIPTOR_INDEXING",
    ):
        if unsupported_api in source or unsupported_api in foundation_source \
                or unsupported_api in cell_renderer_source:
            fail(f"V-10 renderer requires a later-phase feature: {unsupported_api}")

    for snippet in (
        "prepareGeometry",
        "SimulationWorldProfile::STAGE_COUNT",
        '"FPS"',
        '"UPS"',
        '"SIM TOTAL"',
        '"SIMULATION STAGES"',
        "mTextRenderer.commitGeometry()",
    ):
        require(debug_hud_source, snippet, "shared Performance HUD geometry")
    require(
        debug_hud_header,
        "MicroscopeDebugHudTelemetry",
        "shared Performance HUD contract",
    )
    for snippet in (
        "kAtlasWidth = kGlyphWidth * kAtlasColumns",
        "kAtlasHeight = kGlyphHeight * kAtlasRows",
        "kInstanceFloatCount = 9",
        "glyphAtlas()",
        "packedInstances()",
        "geometryRevision()",
    ):
        require(pixel_text_header, snippet, "shared pixel-text contract")
    for snippet in (
        "buildGlyphAtlas()",
        "kSolidGlyphSlot",
        "appendRectangle",
        "appendText",
        "commitGeometry",
    ):
        require(pixel_text_source, snippet, "shared pixel-text geometry")

    require(header, "attachWindow always consumes one ANativeWindow reference", "window contract")
    require(header, "Instance/device resources survive detachSurface", "resource ownership contract")
    require(worker_jni, "ANativeWindow_fromSurface", "main JNI window acquisition")
    require(backend_exports, "ANativeWindow_release", "Vulkan backend window release")
    require(backend_exports, "destroySession", "Vulkan backend session destruction")
    require(backend_exports, "readSpirv", "Vulkan backend SPIR-V boundary")
    require(backend_exports, "readShaderSpirv", "Vulkan native shader asset boundary")
    require(backend_exports, "AndroidRenderAssets assets(info->asset_manager)", "borrowed native AssetManager boundary")
    require(backend_exports, "AssetRenderTextureLoader", "Vulkan native texture boundary")
    for snippet in (
        "create_session",
        "destroy_session",
        "present_microscope",
    ):
        require(backend_exports, snippet, "Vulkan backend API table")
    for snippet in ("nativeBeginExternalFrame", "nativeLastFrameResult"):
        forbid(backend_exports + worker_jni, snippet, "removed Vulkan frame sandwich")
    for snippet in (
        "class VulkanRenderSurfaceSessionAdapter final",
        "RenderBackendKind kind() const noexcept override",
        "const void* bindingIdentity() const noexcept override",
        "RenderFrameResult presentMicroscope(",
        "void beginGenomePreviewFrame(",
    ):
        require(adapter_header, snippet, "Vulkan surface adapter")
    for snippet in (
        "return RenderBackendKind::Vulkan",
        "mSession->presentMicroscope(abiPresentation, abiFrame, hud)",
        "mSession->hasAttachedSurface(attached)",
    ):
        require(adapter_source, snippet, "Vulkan surface adapter")


def verify_shader_build_pipeline() -> None:
    app_build = read("app/build.gradle")
    build_logic = read("gradle/vulkan-shaders.gradle")
    contract = read("native/rendering/vulkan/vulkan-shader-contract.json")
    vertex = read("native/rendering/vulkan/shaders/background.vert")
    fragment = read("native/rendering/vulkan/shaders/background.frag")
    cell_vertex = read("native/rendering/vulkan/shaders/phagocyte.vert")
    cell_fragment = read("native/rendering/vulkan/shaders/phagocyte.frag")

    require(
        app_build,
        "apply from: rootProject.file('gradle/vulkan-shaders.gradle')",
        "Android Vulkan shader build",
    )
    for snippet in (
        "@CacheableTask",
        "androidComponents.sdkComponents.ndkDirectory",
        "glslc${executableSuffix}",
        "spirv-val${executableSuffix}",
        'targetEnvironment.set("vulkan1.0")',
        '"--target-env=${requestedTargetEnvironment}"',
        '"-Werror"',
        "addGeneratedSourceDirectory",
        'layout.buildDirectory.dir("generated/vulkanShaders/',
        "source inventory differs from its contract",
        "Generated Vulkan shader inventory is stale",
    ):
        require(build_logic, snippet, "cacheable Vulkan shader build")
    if "VULKAN_SDK" in build_logic or "glslangValidator" in build_logic:
        fail("Vulkan shader build depends on a desktop Vulkan SDK")

    for snippet in (
        '"targetEnvironment": "vulkan1.0"',
        '"assetDirectory": "vulkan/shaders"',
        '"pushConstantBytes": 0',
        '"type": "uniform_buffer_dynamic"',
        '"byteRange": 144',
        '"vertex": "background.vert"',
        '"fragment": "background.frag"',
        '"name": "phagocyte"',
        '"byteRange": 96',
        '"type": "combined_image_sampler"',
    ):
        require(contract, snippet, "Vulkan shader contract")
    for shader, owner in ((vertex, "background vertex shader"), (fragment, "background fragment shader")):
        for snippet in (
            "#version 450",
            "layout(location = 0)",
            "layout(std140, set = 0, binding = 0)",
            "layout(offset = 0)",
            "layout(offset = 64)",
            "layout(offset = 80)",
            "layout(offset = 96)",
        ):
            require(shader, snippet, owner)
        if "layout(push_constant)" in shader:
            fail(f"{owner} still bypasses the V-07 descriptor path")
    require(vertex, "clipPosition.y = -clipPosition.y", "background vertex shader")
    require(
        vertex,
        "background.preRotation * clipPosition.xy",
        "background vertex shader",
    )
    require(
        vertex,
        "clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w)",
        "background vertex shader",
    )
    require(fragment, "const vec3 WARM_COLOR", "background fragment shader")
    for snippet in (
        "layout(std140, set = 0, binding = 0)",
        "layout(offset = 0) mat4 mvpMatrix",
        "layout(offset = 64) mat2 preRotation",
        "layout(location = 0) in vec2 vPosition",
        "layout(location = 1) in vec4 vTPosition",
        "layout(location = 2) in vec2 vCenter",
        "layout(location = 3) in vec4 vColor",
        "clipPosition.y = -clipPosition.y",
        "clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w)",
    ):
        require(cell_vertex, snippet, "basic-cell vertex shader")
    require(cell_fragment, "layout(location = 0) out vec4 outColor", "basic-cell fragment shader")


def verify_profiling_boundary() -> None:
    cmake = read("native/CMakeLists.txt")
    app_build = read("app/build.gradle")
    session = read(
        "native/render/backend/vulkan/AndroidVulkanClearFrameSession.cpp"
    )
    receiver = read(
        "app/src/diagnostic/java/com/cytarch/cellularity/debug/"
        "DebugRenderBackendReceiver.kt"
    )
    profiler = read("scripts/profile-android-render-backend.py")

    require(
        cmake,
        'option(CELLULARITY_ENABLE_VULKAN_PROFILING "Build opt-in Vulkan frame profiling" OFF)',
        "release-disabled Vulkan profiler",
    )
    require(
        cmake,
        "CELLULARITY_ENABLE_VULKAN_PROFILING=$<BOOL:${CELLULARITY_ENABLE_VULKAN_PROFILING}>",
        "Vulkan profiler compile boundary",
    )
    if app_build.count("-DCELLULARITY_ENABLE_VULKAN_PROFILING=ON") != 3:
        fail(
            "Vulkan profiling must be enabled only for debug, performance, "
            "and experimental"
        )
    if app_build.count(
        "buildConfigField 'boolean', 'ENABLE_RENDER_PROFILING', 'true'"
    ) != 3:
        fail(
            "managed render profiling must be enabled only for debug, "
            "performance, and experimental"
        )
    for snippet in (
        "buildConfigField 'boolean', 'ENABLE_RENDER_PROFILING', 'false'",
        "buildConfig = true",
    ):
        require(app_build, snippet, "release-disabled managed profiler")
    for snippet in (
        "performance_profile_available=",
        "gpu_timestamp_query_pool_available=",
        "profile_descriptor_update_count=",
        "profile_steady_state_resource_create_count=",
    ):
        require(session, snippet, "Vulkan profiling telemetry")
    for snippet in (
        "ACTION_START_RENDER_PROFILE",
        "ACTION_STOP_RENDER_PROFILE",
        "RenderPerformanceTelemetry.snapshot()",
        "MicroscopeRuntimeDiagnostics.snapshot()",
    ):
        require(receiver, snippet, "diagnostic profiling receiver")
    for snippet in (
        'parser.add_argument("--serial", required=True',
        'serial.startswith("emulator-")',
        '"memory_delta_kb"',
        '"camera_sweeps"',
    ):
        require(profiler, snippet, "explicit-target Android profiler")


def verify_environment_field_preview_backend() -> None:
    contract = read("native/rendering/vulkan/vulkan-shader-contract.json")
    vertex = read("native/rendering/vulkan/shaders/density_field.vert")
    fragment = read("native/rendering/vulkan/shaders/density_field.frag")
    session = read(
        "native/render/backend/vulkan/AndroidVulkanClearFrameSession.cpp"
    )
    adapter = read(
        "native/render/backend/abi/VulkanRenderSurfaceSessionAdapter.cpp"
    )
    backend_exports = read(
        "native/render/backend/vulkan/VulkanBackendAbiExports.cpp"
    )
    worker_bridge = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanRenderWorkerBridge.kt"
    )
    surface_view = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameView.kt"
    )
    surface_host = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "OnDemandRenderSurfaceHost.kt"
    )
    preview_view = read(
        "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
        "substrate_info/DensityFieldPreviewView.kt"
    )
    preview_session = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "NativeEnvironmentFieldPreviewSession.kt"
    )
    substrate_screen = read(
        "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
        "substrate_info/SubstrateInfoScreen.kt"
    )
    play_content = read(
        "app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayContent.kt"
    )

    for snippet in (
        '"name": "density_field"',
        '"byteRange": 144',
        '"stages": ["vertex", "fragment"]',
        '"vertex": "density_field.vert"',
        '"fragment": "density_field.frag"',
    ):
        require(contract, snippet, "Vulkan environment-field shader contract")

    for shader, owner in (
        (vertex, "Vulkan environment-field vertex shader"),
        (fragment, "Vulkan environment-field fragment shader"),
    ):
        for snippet in (
            "layout(std140, set = 0, binding = 0)",
            "layout(offset = 0) mat4 mvpMatrix",
            "layout(offset = 64) vec4 field",
            "layout(offset = 80) vec4 fieldExtra",
            "layout(offset = 96) vec4 preview",
            "layout(offset = 112) mat2 preRotation",
        ):
            require(shader, snippet, owner)
        if "layout(push_constant)" in shader:
            fail(f"{owner} bypasses the dynamic-uniform contract")

    for snippet in (
        "clipPosition.y = -clipPosition.y",
        "environmentField.preRotation * clipPosition.xy",
        "clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w)",
    ):
        require(vertex, snippet, "Vulkan environment-field vertex shader")
    for snippet in (
        "FIELD_CONSTANT",
        "FIELD_LINEAR_DIRECTIONAL",
        "FIELD_RADIAL",
        "VECTOR_FIELD_LINEAR_DIRECTIONAL",
        "VECTOR_FIELD_RADIAL_INWARD",
        "VECTOR_FIELD_RADIAL_OUTWARD",
        "vec2 center = radialDirection * 0.48",
        "vec2 arrowDirection = radialDirection * directionSign",
        "strength < 0.0 ? -1.0 : 1.0",
        "smoothstep(0.0045, 0.0049, ringDistance)",
        "outColor = vec4(0.0, 0.0, 0.0, 1.0)",
        "compositeOver",
    ):
        require(fragment, snippet, "Vulkan environment-field fragment shader")

    for snippet in (
        "struct alignas(16) EnvironmentFieldUniformData",
        "sizeof(EnvironmentFieldUniformData) == 144",
        "offsetof(EnvironmentFieldUniformData, preRotation) == 112",
        "createDensityFieldPipeline()",
        "renderEnvironmentFieldPreview(",
        "environmentPreviewBackgroundUniformData(command)",
        "environmentFieldUniformData(command)",
        "densityFieldPipelineCreationCount",
        '"density_field_push_constant_bytes=0\\n"',
        '"live_density_field_pipeline="',
    ):
        require(session, snippet, "native Vulkan environment-field session")
    preview_render_body = session.split(
        "[[nodiscard]] ClearFrameResult renderEnvironmentFieldPreview(", 1
    )[1].split("void beginGenomePreviewFrame(", 1)[0]
    require(
        preview_render_body,
        "if (!densityFieldPipeline && !createDensityFieldPipeline())",
        "lazy environment-field pipeline creation",
    )
    if preview_render_body.count("createDensityFieldPipeline()") != 1 \
            or "vkCreateGraphicsPipelines" in preview_render_body:
        fail("slider frames must reuse one already-created environment-field pipeline")
    swapchain_create_body = session.split(
        "[[nodiscard]] bool createSwapchain()", 1
    )[1].split("[[nodiscard]] BackgroundUniformData backgroundUniformData(", 1)[0]
    if "createDensityFieldPipeline()" in swapchain_create_body:
        fail("ordinary Vulkan surfaces must not compile the preview-only pipeline")

    require(
        adapter,
        "mSession->drawEnvironmentFieldPreview(abi)",
        "Vulkan environment-field surface adapter",
    )
    if 'unsupported("Environment Field Preview")' in adapter:
        fail("Vulkan environment-field preview still routes to an unsupported path")
    for snippet in (
        '"vulkan/shaders/density_field.vert.spv"',
        '"vulkan/shaders/density_field.frag.spv"',
    ):
        require(backend_exports, snippet, "native Vulkan environment-field asset loading")
    require(backend_exports, "Density-field vertex", "Vulkan environment-field ABI export")
    require(backend_exports, "Density-field fragment", "Vulkan environment-field ABI export")
    forbid(worker_bridge, "densityFieldVertexSpirv", "managed environment-field asset loading")
    forbid(worker_bridge, "densityFieldFragmentSpirv", "managed environment-field asset loading")

    for snippet in (
        "opaqueSurface: Boolean = false",
        "if (opaqueSurface) PixelFormat.OPAQUE else PixelFormat.RGBA_8888",
    ):
        require(surface_view, snippet, "Vulkan preview surface format")
    for snippet in (
        "GLSurfaceView.RENDERMODE_WHEN_DIRTY",
        "initialContinuousRendering = false",
        "surface.onHostPause()",
        "lifecycle.onRenderFrame()",
    ):
        require(surface_host, snippet, "shared on-demand preview host")
    old_host = REPO_ROOT / (
        "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "GenomePreviewSurfaceHost.kt"
    )
    if old_host.exists():
        fail("the duplicated Genome Preview surface host was not removed")

    for snippet in (
        "key(backendKind)",
        "DensityFieldPreviewView(",
        "onRenderBackendFailure",
        "private val renderEndpoint = previewSession.renderEndpoint",
        "opaqueSurface = true",
        "visibility = if (isVisible) View.VISIBLE else View.GONE",
        "renderSurface.pauseRendering()",
        "previewSession.publish(fieldState)",
        "renderSurface.requestFrame()",
    ):
        require(preview_view, snippet, "backend-aware environment-field preview view")
    publish_position = preview_view.index("previewSession.publish(fieldState)")
    request_position = preview_view.index("renderSurface.requestFrame()", publish_position)
    if request_position < publish_position:
        fail("environment-field state must be published before its frame request")
    if "queueRenderTask" in preview_view:
        fail("environment-field preview state must not use the managed task queue")

    for snippet in (
        "override fun createVulkanRenderWorker(",
        "previewSession.borrowVulkanEndpoint()",
        "CONTENT_ENVIRONMENT_FIELD_PREVIEW",
        "override fun onVulkanPlatformSurfaceAvailable()",
        "previewSession.draw()",
    ):
        require(preview_view, snippet, "environment-field preview view")
    for snippet in (
        "facade.publishEnvironmentFieldPreviewState(",
        "EnvironmentFieldPreviewUpdateResultLayout.decode",
        "facade.drawEnvironmentFieldPreview()",
    ):
        require(preview_session, snippet, "typed environment-field preview session")
    for obsolete in (
        "pendingFieldState",
        "simulation.applyEnvironmentPatch",
        "simulation.environmentStateBytes",
        "EnvironmentBatchCodec",
    ):
        if obsolete in preview_view + preview_session:
            fail(f"environment-field preview retains obsolete state path: {obsolete}")
    require(
        substrate_screen,
        "backendKind = renderBackendKind",
        "substrate environment-field backend selection",
    )
    require(
        play_content,
        "renderBackendKind = microscopeSurfaceFactory.backendKind",
        "Play environment-field backend selection",
    )


def verify_saved_file_preview_backend() -> None:
    surface_contract = read("native/render/backend/RenderSurfaceSession.h")
    session = read(
        "native/render/backend/vulkan/AndroidVulkanClearFrameSession.cpp"
    )
    session_header = read(
        "native/render/backend/vulkan/AndroidVulkanClearFrameSession.h"
    )
    adapter = read(
        "native/render/backend/abi/VulkanRenderSurfaceSessionAdapter.cpp"
    )
    renderer_header = read(
        "native/render/backend/vulkan/VulkanBasicCellRenderer.h"
    )
    renderer_source = read(
        "native/render/backend/vulkan/VulkanBasicCellRenderer.cpp"
    )
    foundation_header = read(
        "native/render/backend/vulkan/VulkanResourceFoundation.h"
    )
    foundation_source = read(
        "native/render/backend/vulkan/VulkanResourceFoundation.cpp"
    )
    native_session = read("native/app/NativeRenderSession.cpp")
    genome_preview = read(
        "native/app/genome_preview/GenomePreviewController.cpp"
    )
    surface_host = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "OnDemandRenderSurfaceHost.kt"
    )
    microscope_view = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "AndroidMicroscopeView.kt"
    )
    vulkan_microscope_host = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "VulkanMicroscopeSurfaceHost.kt"
    )

    for snippet in (
        "enum class OffscreenRenderTarget",
        "beginOffscreenRgba(OffscreenRenderTarget target",
        "finishOffscreenRgba(OffscreenRenderTarget target)",
        "cancelOffscreenRgba(",
    ):
        require(surface_contract, snippet, "backend-neutral file-preview contract")

    for snippet in (
        "mSession->beginOffscreenRgba(",
        "mSession->finishOffscreenRgba(",
        "mSession->cancelOffscreenRgba(",
    ):
        require(adapter, snippet, "Vulkan off-screen surface adapter")
    if "scheduled for a later Vulkan migration phase" in adapter:
        fail("Vulkan saved-file preview still routes to an unsupported path")
    resize_body = adapter.split(
        "void VulkanRenderSurfaceSessionAdapter::resizeSurface(", 1
    )[1].split(
        "void VulkanRenderSurfaceSessionAdapter::detachSurface()", 1
    )[0]
    if "mSession.resizeSurface" in resize_body:
        fail(
            "controller viewport notifications must not resize Vulkan outside "
            "its render-thread command queue"
        )
    require(
        resize_body,
        "See attachSurface(). Controller viewport notifications are independent",
        "Vulkan physical-surface owner boundary",
    )
    require(
        adapter,
        "Physical Vulkan surface lifecycle is owned by the render worker",
        "Vulkan physical-surface owner boundary",
    )

    for snippet in (
        "void beginOffscreenRgba(",
        "finishOffscreenRgba(",
        "cancelOffscreenRgba(",
    ):
        require(session_header, snippet, "Vulkan off-screen session ABI")

    for snippet in (
        "constexpr VkFormat kOffscreenColorFormat = VK_FORMAT_R8G8B8A8_UNORM",
        "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT",
        "VK_IMAGE_USAGE_TRANSFER_SRC_BIT",
        "VK_BUFFER_USAGE_TRANSFER_DST_BIT",
        "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT",
        "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
        "optimalBufferCopyRowPitchAlignment",
        "copy.bufferRowLength",
        "vkCmdCopyImageToBuffer",
        "readbackBuffer.invalidate(",
        "std::memcpy(rgba.data() + row * packedRowBytes",
        "preRotationMatrix(renderPreTransform())",
        "VulkanPipelineTarget::Offscreen",
        '"offscreen_output_row_order=top_left\\n"',
        '"offscreen_readback_only_on_capture=true\\n"',
    ):
        require(session, snippet, "Vulkan saved-file preview implementation")
    if session.count("vkCmdCopyImageToBuffer") != 2:
        fail("Vulkan readback copies must exist only for substrate and genome capture")

    capture_body = session.split(
        "[[nodiscard]] std::vector<std::uint8_t> finishOffscreenRgba(", 1
    )[1].split("void cancelOffscreenRgba(", 1)[0]
    if "for (std::size_t row = 0; row < height; ++row)" not in capture_body:
        fail("Vulkan readback must copy mapped rows using the stored row pitch")
    if "std::reverse" in capture_body:
        fail("Vulkan file previews must already use top-left row order")

    ordinary_frame_body = session.split(
        "[[nodiscard]] ClearFrameResult renderFrame(\n"
        "            const PreparedRenderFrameView& preparedFrame,", 1
    )[1].split(
        "[[nodiscard]] ClearFrameResult renderClear(", 1
    )[0]
    require(
        ordinary_frame_body,
        "return renderPreparedFrameOffscreen(",
        "explicit substrate capture branch",
    )
    require(
        ordinary_frame_body,
        "preparedFrame, presentation",
        "explicit substrate capture branch",
    )
    if "readbackBuffer.invalidate" in ordinary_frame_body:
        fail("ordinary Vulkan frame rendering performs a CPU readback")

    for snippet in (
        "enum class VulkanPipelineTarget",
        "Offscreen = 1",
        "std::array<PipelineSet",
    ):
        require(renderer_header, snippet, "independent Vulkan pipeline sets")
    for snippet in (
        "destroyPipelines(",
        "VulkanPipelineTarget target) noexcept",
        "const PipelineSet& pipelines = pipelineSet(target)",
    ):
        require(renderer_source, snippet, "independent Vulkan pipeline sets")

    for snippet in (
        "VkResult invalidate(",
    ):
        require(foundation_header, snippet, "host readback memory contract")
    require(
        foundation_source,
        "vkInvalidateMappedMemoryRanges",
        "non-coherent Vulkan readback",
    )

    if surface_host.count(
        "override val supportsOffscreenCapture: Boolean = true"
    ) != 2:
        fail("both GLES and Vulkan on-demand hosts must expose file capture")
    for snippet in (
        "override fun onSurfaceCreated()",
        "endpoint.onSurfaceCreated()",
        "override fun onSurfaceChanged(width: Int, height: Int)",
        "endpoint.onSurfaceChanged(width, height)",
        "override fun onSurfaceDestroyed()",
        "endpoint.onSurfaceDestroyed()",
    ):
        require(
            microscope_view,
            snippet,
            "backend-neutral substrate-preview surface lifecycle",
        )
    for snippet in (
        "renderEndpoint.createVulkanRenderWorker(",
        "nativeSurfaceReadyReceiver = renderEndpoint::onVulkanSurfaceReady",
        "renderEndpoint.onVulkanSurfaceUnavailable()",
    ):
        require(
            vulkan_microscope_host,
            snippet,
            "native-worker Vulkan microscope lifecycle",
        )

    if not re.search(
        r"fileViewState,\s*metadata,\s*true\)",
        genome_preview,
    ):
        fail("transparent genome file preview is missing its transparent-clear request")
    for snippet in (
        "OffscreenRenderTarget::GenomePreview",
        "kGenomeFilePreviewPixels, kGenomeFilePreviewPixels",
    ):
        require(genome_preview, snippet, "transparent genome file preview")
    for snippet in (
        "token != mSubstratePreview->activeToken",
        "OffscreenRenderTarget::SubstratePreview",
        "SUBSTRATE_PREVIEW_PIXELS, SUBSTRATE_PREVIEW_PIXELS",
    ):
        require(native_session, snippet, "staged substrate file preview")


def verify_manifest_is_optional() -> None:
    manifest = read("app/src/main/AndroidManifest.xml")
    for feature, version in (
        ("android.hardware.vulkan.level", "0"),
        ("android.hardware.vulkan.version", "0x00400000"),
    ):
        pattern = re.compile(
            rf"<uses-feature(?=[^>]*android:name=\"{re.escape(feature)}\")"
            rf"(?=[^>]*android:required=\"false\")"
            rf"(?=[^>]*android:version=\"{re.escape(version)}\")[^>]*/>",
            re.DOTALL,
        )
        if pattern.search(manifest) is None:
            fail(f"manifest feature must be optional with version {version}: {feature}")
    if re.search(
        r"<uses-feature(?=[^>]*android:glEsVersion=\"0x00030000\")"
        r"(?=[^>]*android:required=\"true\")[^>]*/>",
        manifest,
        re.DOTALL,
    ) is None:
        fail("the GLES 3 requirement changed while adding optional Vulkan metadata")


def verify_isolated_surface_integration() -> None:
    bridge = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanCapabilityBridge.java"
    )
    backend_loader = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanBackendLoader.java"
    )
    repository = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "AndroidVulkanCapabilityRepository.kt"
    )
    application = read("app/src/main/java/com/cytarch/cellularity/CellularityApplication.kt")
    settings = read(
        "app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/settings/"
        "SettingsScreen.kt"
    )
    preference = read(
        "app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/settings/"
        "VulkanCapabilityPreference.kt"
    )
    worker_bridge = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanRenderWorkerBridge.kt"
    )
    native_worker = read(
        "native/app/render/NativeVulkanRenderWorker.cpp"
    )
    surface_view = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameView.kt"
    )
    activity = read(
        "app/src/main/java/com/cytarch/cellularity/ui/vulkan/"
        "VulkanClearFrameActivity.kt"
    )
    manifest = read("app/src/main/AndroidManifest.xml")
    crash_logger = read("app/src/main/java/com/cytarch/cellularity/CrashLogger.kt")
    native_crash_handler = read("native/diagnostics/NativeCrashHandler.cpp")
    native_crash_writer = read("native/diagnostics/NativeCrashReportWriter.cpp")
    simulation_factory = read(
        "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
        "AndroidSimulationFactory.kt"
    )
    microscope_factory = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "MicroscopeSurfaceContracts.kt"
    )
    microscope_host = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "VulkanMicroscopeSurfaceHost.kt"
    )
    simulation_facade = read(
        "app/src/main/java/com/cytarch/cellularity/NativeSimulationFacade.java"
    )
    session_endpoint = read(
        "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
        "SessionMicroscopeRenderEndpoint.kt"
    )
    controller_bridge = read(
        "app/src/main/java/com/cytarch/cellularity/"
        "NativeCellularityControllerBridge.java"
    )
    worker_jni = read("native/jni/NativeVulkanRenderWorkerJni.cpp")
    attachment_lease = read("native/app/render/NativeRenderAttachmentLease.cpp")
    microscope_source = read(
        "native/app/render/sources/MicroscopeRenderFrameSource.cpp"
    )
    vulkan_exports = read(
        "native/render/backend/vulkan/VulkanBackendAbiExports.cpp"
    )
    native_texture_loader = read("native/render/AssetRenderTextureLoader.cpp")
    android_asset_store = read("native/platform/android/AndroidRenderAssets.cpp")
    play_activity = read(
        "app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayActivity.kt"
    )
    play_content = read(
        "app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayContent.kt"
    )
    play_view_model = read(
        "app/src/main/java/com/cytarch/cellularity/ui/play_menu/"
        "PlayActivityViewModel.kt"
    )
    microscope_screen = read(
        "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope/"
        "MicroscopeScreen.kt"
    )
    backend_selection = read(
        "app/src/main/java/com/cytarch/cellularity/platform/render/"
        "RenderBackendSelection.kt"
    )
    backend_settings = read(
        "shared/src/androidMain/kotlin/com/cytarch/cellularity/shared/settings/"
        "AndroidRenderBackendSettings.kt"
    )
    settings_repositories = read(
        "app/src/main/java/com/cytarch/cellularity/platform/settings/"
        "AndroidSettingsRepositories.kt"
    )
    backend_preference = read(
        "app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/settings/"
        "RenderBackendPreference.kt"
    )

    require(bridge, "NativeVulkanBackendLoader.isAvailable()", "Vulkan probe bridge")
    require(bridge, "catch (RuntimeException | LinkageError throwable)", "Vulkan probe failure boundary")
    require(backend_loader, 'System.loadLibrary("cellularity_vulkan")', "single Vulkan DSO loader")
    require(backend_loader, "catch (UnsatisfiedLinkError | SecurityException error)", "Vulkan loader failure boundary")
    require(worker_bridge, "nativeCreateWorkerControlBlock(", "native worker bridge")
    require(worker_bridge, "NativeVulkanBackendLoader.backendApiV1Address()", "native worker bridge")
    forbid(worker_bridge, "byte[][]", "managed SPIR-V bridge")
    forbid(worker_bridge, "DecodedRenderTexture", "managed texture bridge")
    require(repository, "VulkanCapabilityDiagnostics.update(report)", "Vulkan diagnostics")
    require(settings, "VulkanCapabilityPreference", "experimental settings")
    require(settings, "VulkanClearFramePreference", "experimental clear-frame entry")
    require(preference, "TextureView.SurfaceTextureListener", "temporary probe surface")
    require(preference, "settings_vulkan_capability_retry_action", "probe retry UI")
    require(crash_logger, "VulkanCapabilityDiagnostics.snapshot()", "diagnostics export")
    require(crash_logger, "VulkanRuntimeDiagnostics.snapshot()", "runtime diagnostics export")
    require(crash_logger, "RenderBackendDiagnostics.snapshot()", "backend diagnostics export")
    require(
        crash_logger,
        "nativeUpdateRenderBackendDiagnostics(backend, fallback)",
        "native crash backend diagnostics",
    )
    require(
        native_crash_handler,
        "gRenderBackendDiagnostics",
        "signal-safe native backend diagnostics",
    )
    require(native_crash_writer, '"\\nrenderBackend="', "native crash backend report")
    require(
        native_crash_writer,
        '"\\nrenderBackendFallbackReason="',
        "native crash fallback report",
    )
    require(native_worker, 'setCurrentThreadName("CellularityVk")', "dedicated native render thread")
    require(native_worker, "mDesired.frameRequested", "frame-request coalescing")
    require(native_worker, "mDesired.retryPending", "deferred frame request retention")
    require(native_worker, "surfaceIntentRevision", "surface intent ordering")
    require(native_worker, "surfaceGeneration", "surface generation ordering")
    forbid(native_worker, "drainTasksBeforeFrame", "removed arbitrary render-task drain")
    forbid(native_worker, "Command::Task", "removed arbitrary render-task command")
    require(
        native_worker,
        "mDesired.demoPerformanceHudEnabled",
        "native Performance HUD toggle",
    )
    require(surface_view, "SurfaceView", "Vulkan Android surface host")
    require(surface_view, "Choreographer.FrameCallback", "display-paced frame requests")
    require(surface_view, "renderWorker.attachSurface(", "managed Surface handoff")
    require(
        surface_view,
        "renderWorker.setDemoPerformanceHudEnabled(value)",
        "Vulkan Performance HUD setting",
    )
    require(
        worker_bridge,
        "assetsOwner.requireNativeLeaseHandle()",
        "native render-asset lease handoff",
    )
    forbid(worker_bridge, "assets.open", "managed Vulkan asset loading")
    forbid(worker_bridge, "readAsset", "managed Vulkan asset loading")
    forbid(worker_bridge, "AndroidRenderTextureLoader", "managed texture decoding")
    forbid(worker_jni, "AAssetManager_fromJava", "duplicate raw AssetManager bridge")
    require(vulkan_exports, "AndroidRenderAssets assets(info->asset_manager)", "native Vulkan asset loading")
    require(
        vulkan_exports,
        '"vulkan/shaders/background.vert.spv"',
        "native generated vertex shader loading",
    )
    require(
        vulkan_exports,
        '"vulkan/shaders/background.frag.spv"',
        "native generated fragment shader loading",
    )
    require(vulkan_exports, "readShaderSpirv", "native shader catalog loading")
    require(vulkan_exports, '"small_dots"', "native Vulkan dot-texture loading")
    require(
        vulkan_exports,
        '"gauss_bessel_integral"',
        "native Vulkan Bessel-texture loading",
    )
    require(
        native_texture_loader,
        "stbi_load_from_memory",
        "backend-neutral native PNG decoding",
    )
    require(android_asset_store, "AAssetManager_open", "native packaged asset loading")
    require(activity, "VulkanClearFrameView", "clear-frame activity")
    require(
        activity,
        "settings.performanceHudEnabled",
        "Vulkan Performance HUD setting",
    )
    for snippet in (
        "VulkanClearFrameView(",
        "renderEndpoint.createVulkanRenderWorker(",
        "nativeSurfaceReadyReceiver = renderEndpoint::onVulkanSurfaceReady",
        "nativeSurfaceUnavailableReceiver",
    ):
        require(microscope_host, snippet, "production Vulkan microscope host")
    for snippet in (
        "RenderBackendKind.OPEN_GL_ES -> GlesMicroscopeSurfaceHostFactory",
        "RenderBackendKind.VULKAN -> VulkanMicroscopeSurfaceHostFactory",
    ):
        require(microscope_factory, snippet, "backend-aware microscope factory")
    for snippet in (
        "NativeVulkanRenderWorkerBridge.createDormantMicroscope(",
        "session.beginVulkanWorkerBinding(surfaceToken, worker)",
        "worker.authorizeMicroscope(endpoint.handle, surfaceToken)",
        "session.completeVulkanWorkerUnbind(surfaceToken, worker)",
    ):
        require(session_endpoint, snippet, "durable Vulkan worker handoff")
    require(
        simulation_facade,
        "microscopeRenderEndpoint",
        "simulation-to-render endpoint composition",
    )
    for snippet in (
        "installBackendBinding(",
        "removeBackendBinding(",
    ):
        require(attachment_lease, snippet, "controller Vulkan adapter ownership")
    for snippet in (
        "VulkanRenderSurfaceSessionBinding",
        "mLease->installBackendBinding",
        "mLease->removeBackendBinding",
    ):
        require(microscope_source, snippet, "native Vulkan frame source binding")
    for obsolete in (
        "withPinnedRenderHandleAction",
        "activeRenderCalls",
        "renderCallsClosing",
    ):
        forbid(controller_bridge, obsolete, "V-26B controller render lifetime")
    if re.search(
        r"public void (?:drawMicroscope|clearMicroscopeNoRender)\(\)\s*\{"
        r"\s*withHandleAction\(",
        controller_bridge,
    ):
        fail("Vulkan acquire/present must not retain the controller read lock")
    for snippet in (
        "nativeAuthorizeMicroscopeWorker",
        "MicroscopeRenderFrameSource",
        "rebindWakeControl()",
    ):
        require(worker_jni, snippet, "native worker/source boundary")
    require(manifest, 'android:name="com.cytarch.cellularity.ui.vulkan.VulkanClearFrameActivity"', "manifest")
    require(manifest, 'android:configChanges="orientation|screenSize"', "rotation lifecycle")

    on_create = re.search(
        r"override fun onCreate\(\)\s*\{(?P<body>.*?)\n\s*\}",
        application,
        re.DOTALL,
    )
    if on_create is None or "vulkan" in on_create.group("body").lower():
        fail("application startup must not run the Vulkan capability probe")
    require(
        simulation_factory,
        "backendKind = RenderBackendKind.OPEN_GL_ES",
        "default renderer selection",
    )
    if "EXTRA_VULKAN_MICROSCOPE_TEST" in play_activity \
            or "intent.getBooleanExtra" in play_activity:
        fail("Play still has a hidden intent bypass around persisted backend selection")
    for snippet in (
        "RenderBackendResolver.resolve(",
        "backendDecision is RenderBackendDecision.AwaitingCapability",
        "VulkanProbeSurface(",
        "RenderBackendDecision.VulkanUnavailable",
        "var presentedRenderBackend by remember",
        "presentedRenderBackend = null",
        "withFrameNanos { }",
        "presentedRenderBackend = target",
        "renderBackendKind = presentedRenderBackend",
        "current.copy(backend = AndroidRenderBackendPreference.OPEN_GL_ES)",
    ):
        require(play_activity, snippet, "runtime backend gate")
    for snippet in (
        "renderBackendKind: RenderBackendKind?",
        "renderBackendKind?.let { backend ->",
        "if (microscopeSurfaceFactory == null)",
    ):
        require(play_content, snippet, "state-preserving backend handoff")
    for snippet in (
        "AndroidRenderBackendPreference.OPEN_GL_ES",
        "AndroidRenderBackendPreference.AUTO",
        "AndroidRenderBackendPreference.VULKAN",
        "RenderBackendFallbackReason.VULKAN_RUNTIME_FAILED",
        "sessionVulkanFailure",
        "RenderBackendDiagnostics.recordRuntimeFailure",
    ):
        require(backend_selection, snippet, "runtime backend policy")
    require(
        backend_settings,
        "backend: AndroidRenderBackendPreference = AndroidRenderBackendPreference.OPEN_GL_ES",
        "stable GLES backend default",
    )
    require(
        settings_repositories,
        'RENDER_BACKEND_SETTINGS_FILE_NAME = "android-render-backend-settings.json"',
        "independent backend settings document",
    )
    require(backend_preference, "SettingsListPreference(", "backend settings UI")
    require(settings, "probeAutomatically =", "explicit GLES probe boundary")
    require(preference, "probeRequested ||", "opt-in capability probe")
    require(
        play_content,
        "viewModel.requireMicroscopeSurfaceFactory(",
        "render-only backend replacement",
    )
    require(play_view_model, ".withBackend", "retained simulation backend replacement")
    require(microscope_screen, "key(surfaceFactory)", "microscope surface replacement")

    backend_tests = read(
        "app/src/test/java/com/cytarch/cellularity/platform/render/"
        "RenderBackendResolverTest.kt"
    )
    for test_name in (
        "explicitGlesNeedsNeitherCapabilityNorVulkanRuntimeRecovery",
        "autoSelectsVulkanOnlyAfterSupportedCapabilityReport",
        "autoFallsBackBeforeSurfaceCreationWhenCapabilityIsUnsupported",
        "explicitVulkanExposesCapabilityFailureInsteadOfHidingIt",
        "knownRuntimeFailureCannotRetryVulkanAutomaticallyInSameSession",
        "sessionRetainsOnlyTheFirstRuntimeFailure",
    ):
        require(backend_tests, f"fun {test_name}()", "backend resolver tests")

    tests = read(
        "app/src/test/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanCapabilityReportParserTest.kt"
    )
    for test_name in (
        "parsesSupportedDeviceAndEverySelectionRequirement",
        "parsesUnsupportedLoaderWithNoPhysicalDevices",
        "preservesSymbolicVkResultFromProbeFailure",
        "convertsMalformedNativeTextIntoSafeFailure",
    ):
        require(tests, f"fun {test_name}()", "Vulkan report parser tests")

    render_thread_tests = read("native/tests/native_vulkan_render_worker_tests.cpp")
    for test_name in (
        "testDormantCloseCreatesNoSession",
        "testPendingFrameOpensAfterResumeAndSurface",
        "testDeferredStartupBindingWaitsForNativeWake",
        "testPauseRetiresSurfaceAndRequiresResubmit",
        "testOnDemandRetryIsNativeAndBounded",
        "testRecoverableFailureNeedsReplacementSurface",
        "testSurfaceLossSignalsAndReattaches",
        "testSourceFailureIsTerminal",
        "testStatusSignalRetriesOnce",
    ):
        require(render_thread_tests, f"void {test_name}()", "native Vulkan worker tests")

    renderer_tests = read("native/tests/native_render_attachment_lease_tests.cpp")
    for test_name in (
        "testNativeViewportInitializationAndSurfaceRecreation",
        "testRebindDrainsFrameAndInvalidatesOldKey",
    ):
        require(renderer_tests, test_name, "Vulkan lifecycle camera tests")


def main() -> None:
    verify_android_only_build_boundary()
    verify_native_probe_scope()
    verify_backend_c_abi()
    verify_prepared_frame_session()
    verify_shader_build_pipeline()
    verify_profiling_boundary()
    verify_environment_field_preview_backend()
    verify_saved_file_preview_backend()
    verify_manifest_is_optional()
    verify_isolated_surface_integration()
    print(
        "Vulkan boundary verified: reproducible Vulkan 1.0 shaders, immutable "
        "prepared-frame world passes and overlays, bounded two-slot geometry "
        "synchronization, shared native HUD text, generation-safe production "
        "microscope lifecycle, render-on-demand field previews, Vulkan "
        "saved-file readback, and persisted runtime backend fallback policy"
    )


if __name__ == "__main__":
    main()
