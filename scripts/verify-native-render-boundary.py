#!/usr/bin/env python3
"""Verify the characterized V-19 managed/native render boundary."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
CMAKE_PATH = REPO_ROOT / "native/CMakeLists.txt"
APP_BUILD_PATH = REPO_ROOT / "app/build.gradle"
BASELINE_PATH = REPO_ROOT / "native/rendering/NATIVE_RENDER_BOUNDARY_BASELINE.md"
MAIN_BRIDGE_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/NativeCellularityControllerBridge.java"
)
VULKAN_WORKER_JNI_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
    "NativeVulkanRenderWorkerJni.java"
)
FIELD_PREVIEW_BRIDGE_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
    "EnvironmentFieldPreviewBridge.java"
)
PREVIEW_CAPTURE_BRIDGE_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
    "NativePreviewCaptureBridge.java"
)
PREVIEW_CAPTURE_CONTRACT_TEST_PATH = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/render/preview/"
    "NativePreviewCaptureContractTest.kt"
)
MAIN_LIBRARY_LOADER_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/"
    "NativeCellularityLibraryLoader.java"
)
FIELD_PREVIEW_VIEW_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
    "substrate_info/DensityFieldPreviewView.kt"
)
GENOME_PREVIEW_VIEW_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
    "genome_editor/GenomePreviewView.kt"
)
ANDROID_MICROSCOPE_VIEW_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
    "AndroidMicroscopeView.kt"
)
COMMAND_MIGRATION_MAP_PATH = (
    REPO_ROOT / "native/rendering/NATIVE_RENDER_COMMAND_MIGRATION_MAP.md"
)
TELEMETRY_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/"
    "RenderPerformanceTelemetry.kt"
)
BOUNDARY_TELEMETRY_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/"
    "RenderBoundaryTelemetry.kt"
)
PROFILER_PATH = REPO_ROOT / "scripts/profile-android-render-backend.py"
VULKAN_WORKER_TEST_PATH = (
    REPO_ROOT / "native/tests/native_vulkan_render_worker_tests.cpp"
)
TELEMETRY_TEST_PATH = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/render/"
    "RenderPerformanceTelemetryTest.kt"
)
MICROSCOPE_RENDER_ENDPOINT_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/NativeMicroscopeRenderEndpoint.java"
)
SESSION_RENDER_ENDPOINT_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
    "SessionMicroscopeRenderEndpoint.kt"
)
MICROSCOPE_STATUS_POLLER_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
    "MicroscopeStatusPoller.kt"
)
MICROSCOPE_STATUS_POLLER_TEST_PATH = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/render/microscope/"
    "MicroscopeStatusPollerTest.kt"
)
MICROSCOPE_INPUT_BATCH_LAYOUT_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
    "MicroscopeInputBatchLayout.kt"
)
MICROSCOPE_INPUT_BATCH_TEST_PATH = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/render/microscope/"
    "MicroscopeInputBatchLayoutTest.kt"
)
MICROSCOPE_INPUT_STREAM_TEST_PATH = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/render/microscope/"
    "MicroscopeInputStreamStateTest.kt"
)
MICROSCOPE_GESTURE_PATH = (
    REPO_ROOT
    / "shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/play/"
    "microscope/MicroscopeInput.kt"
)
MICROSCOPE_GESTURE_TEST_PATH = (
    REPO_ROOT
    / "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/play/"
    "microscope/MicroscopeGestureInterpreterTest.kt"
)
RUNTIME_PUBLICATION_SOURCE_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
    "MicroscopeRuntimePublicationSource.kt"
)
SIMULATION_GATEWAY_PATH = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
    "AndroidSimulationGateway.kt"
)
VULKAN_WORKER_PATH = (
    REPO_ROOT / "native/app/render/NativeVulkanRenderWorker.cpp"
)

MAIN_JNI_PREFIX = (
    "Java_com_cytarch_cellularity_NativeCellularityControllerBridge_"
)
FIELD_PREVIEW_JNI_PREFIX = (
    "Java_com_cytarch_cellularity_platform_render_preview_"
    "EnvironmentFieldPreviewBridge_"
)
PREVIEW_CAPTURE_JNI_PREFIX = (
    "Java_com_cytarch_cellularity_platform_render_preview_"
    "NativePreviewCaptureBridge_"
)

# Current frame-boundary allowlist. Adding an entry requires an explicit
# architecture review and an update to the boundary document.
FRAME_BOUNDARY_ENTRY_POINTS = {
    "nativePollMicroscopeRuntimePublication": (
        MAIN_BRIDGE_PATH,
        REPO_ROOT / "native/jni/NativeMicroscopeJni.cpp",
        MAIN_JNI_PREFIX,
    ),
    "nativeViewportState": (
        MAIN_BRIDGE_PATH,
        REPO_ROOT / "native/jni/NativeRenderJni.cpp",
        MAIN_JNI_PREFIX,
    ),
    "nativeDrawGenomePreview": (
        MAIN_BRIDGE_PATH,
        REPO_ROOT / "native/jni/NativeRenderJni.cpp",
        MAIN_JNI_PREFIX,
    ),
    "nativeDrawEnvironmentFieldPreview": (
        MAIN_BRIDGE_PATH,
        REPO_ROOT / "native/jni/NativeRenderJni.cpp",
        MAIN_JNI_PREFIX,
    ),
    "nativeDrawMicroscope": (
        MAIN_BRIDGE_PATH,
        REPO_ROOT / "native/jni/NativeRenderJni.cpp",
        MAIN_JNI_PREFIX,
    ),
    "nativeClearMicroscopeNoRender": (
        MAIN_BRIDGE_PATH,
        REPO_ROOT / "native/jni/NativeRenderJni.cpp",
        MAIN_JNI_PREFIX,
    ),
}

EXPECTED_SOURCE_OWNERSHIP = {
    "CELLULARITY_JNI_SOURCES": {
        "jni/NativeRenderProfilingJni.cpp",
        "jni/NativeVulkanRenderWorkerJni.cpp",
        "jni/NativeControllerJni.cpp",
        "jni/NativeRenderJni.cpp",
        "jni/NativeMicroscopeJni.cpp",
        "jni/NativeGenomePreviewJni.cpp",
        "jni/NativePreviewCaptureJni.cpp",
        "jni/NativePreviewCaptureRequestHandle.cpp",
    },
    "CELLULARITY_RENDERING_SOURCES": {
        "app/EnvironmentFieldPreviewEndpoint.cpp",
        "app/EnvironmentFieldPreviewState.cpp",
        "app/NativeRenderSession.cpp",
        "app/genome_preview/GenomePreviewCaptureRequestStore.cpp",
        "app/genome_preview/GenomePreviewEndpoint.cpp",
        "app/genome_preview/GenomePreviewEndpointLayouts.cpp",
        "app/genome_preview/GenomePreviewInteractionQueue.cpp",
        "app/genome_preview/GenomePreviewRetainedState.cpp",
        "app/genome_preview/RetainedPayloadBudget.cpp",
        "app/microscope/NativeMicroscopeRuntime.cpp",
        "app/microscope/SubstratePreviewCaptureRequestStore.cpp",
        "app/microscope/SubstratePreviewCaptureEndpoint.cpp",
        "app/render/preview/NativePreviewCaptureLayouts.cpp",
        "app/render/preview/NativePreviewCaptureRequestState.cpp",
        "app/render/sources/MicroscopeRenderFrameSource.cpp",
        "app/render/sources/GenomePreviewRenderFrameSource.cpp",
        "app/render/sources/EnvironmentFieldPreviewRenderFrameSource.cpp",
        "app/render/sources/VulkanDemoFrameProducer.cpp",
    },
    "CELLULARITY_RENDER_FRAME_RESULT_SOURCES": {
        "render/backend/RenderFrameResult.cpp",
    },
}

EXPECTED_SCENARIOS = {
    "freeze-static",
    "observe-idle",
    "observe-drag",
    "observe-pinch",
    "observe-fling",
    "incubate-idle",
    "incubate-camera",
    "incubate-no-render",
    "genome-ordinary",
    "genome-grid",
    "genome-tree",
    "genome-ghost",
    "genome-editor-input",
    "environment-density",
    "environment-salinity",
    "environment-food-lipid",
    "environment-gravity",
    "genome-save",
    "substrate-save",
}

JAVA_NATIVE_DECLARATION = re.compile(
    r"\bprivate\s+static\s+native\s+[A-Za-z0-9_.$<>?\[\]]+\s+"
    r"(?P<name>native[A-Za-z0-9_]+)\s*\(",
    re.MULTILINE,
)
FRAME_BOUNDARY_NAME = re.compile(
    r"^native(?:"
    r"PollMicroscopeRuntimePublication|ViewportState|"
    r"DrawGenomePreview|"
    r"DrawEnvironmentFieldPreview|DrawMicroscope|ClearMicroscopeNoRender|"
    r"RenderDemoFrame"
    r")$"
)


class VerificationError(RuntimeError):
    pass


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify the characterized managed/native render boundary."
    )
    apk_group = parser.add_mutually_exclusive_group()
    apk_group.add_argument(
        "--release-apk",
        type=Path,
        help="Verify optimized JNI/R8 contracts and require profiler stripping",
    )
    apk_group.add_argument(
        "--optimized-apk",
        type=Path,
        help="Verify optimized JNI/R8 contracts while allowing diagnostic profiling",
    )
    return parser.parse_args()


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise VerificationError(f"Could not read {path}: {error}") from error


def relative(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def cmake_set(cmake: str, name: str) -> set[str]:
    match = re.search(rf"set\({re.escape(name)}\s+(.*?)\n\)", cmake, re.DOTALL)
    if match is None:
        raise VerificationError(f"CMake source set {name} is missing")
    return set(re.findall(r"(?m)^\s*([A-Za-z0-9_./+-]+\.(?:c|cc|cpp))\s*$", match.group(1)))


def cmake_target_body(cmake: str, target: str) -> str:
    match = re.search(
        rf"add_library\({re.escape(target)}\s+SHARED\s+(.*?)\n\s*\)",
        cmake,
        re.DOTALL,
    )
    if match is None:
        raise VerificationError(f"CMake shared target {target} is missing")
    return match.group(1)


def cmake_link_body(cmake: str, target: str) -> str:
    match = re.search(
        rf"target_link_libraries\({re.escape(target)}\s+PRIVATE\s+(.*?)\n\s*\)",
        cmake,
        re.DOTALL,
    )
    if match is None:
        raise VerificationError(f"CMake link block for {target} is missing")
    return match.group(1)


def verify_entry_points(errors: list[str]) -> None:
    bridge_sources = {
        MAIN_BRIDGE_PATH: read(MAIN_BRIDGE_PATH),
        FIELD_PREVIEW_BRIDGE_PATH: read(FIELD_PREVIEW_BRIDGE_PATH),
    }
    declarations: set[str] = set()
    for source in bridge_sources.values():
        declarations.update(JAVA_NATIVE_DECLARATION.findall(source))
    characterized = {name for name in declarations if FRAME_BOUNDARY_NAME.fullmatch(name)}
    expected = set(FRAME_BOUNDARY_ENTRY_POINTS)
    require(
        characterized == expected,
        "frame JNI allowlist differs from Java declarations: "
        f"missing={sorted(expected - characterized)}, "
        f"unexpected={sorted(characterized - expected)}",
        errors,
    )

    jni_sources = {
        path: read(path)
        for path in (REPO_ROOT / "native/jni").glob("*.cpp")
    }
    for method, (bridge_path, implementation_path, prefix) in sorted(
        FRAME_BOUNDARY_ENTRY_POINTS.items()
    ):
        bridge = bridge_sources[bridge_path]
        declaration_count = len(
            re.findall(
                rf"\bprivate\s+static\s+native\s+"
                rf"[A-Za-z0-9_.$<>?\[\]]+\s+{re.escape(method)}\s*\(",
                bridge,
                re.MULTILINE,
            )
        )
        require(
            declaration_count == 1,
            f"{relative(bridge_path)} must declare {method} exactly once",
            errors,
        )
        symbol = prefix + method
        defining_sources = [
            path for path, source in jni_sources.items() if symbol in source
        ]
        require(
            defining_sources == [implementation_path],
            f"{symbol} ownership differs: "
            f"expected={relative(implementation_path)}, "
            f"actual={[relative(path) for path in defining_sources]}",
            errors,
        )


def verify_source_ownership(errors: list[str]) -> None:
    cmake = read(CMAKE_PATH)
    for source_set, required_sources in EXPECTED_SOURCE_OWNERSHIP.items():
        actual_sources = cmake_set(cmake, source_set)
        for source in sorted(required_sources):
            require(
                source in actual_sources,
                f"{source} must remain in {source_set}",
                errors,
            )
            require(
                cmake.count(source) == 1,
                f"{source} must have exactly one production CMake owner",
                errors,
            )

    main_sources = cmake_target_body(cmake, "cellularity_android")
    vulkan_sources = cmake_target_body(cmake, "cellularity_vulkan")
    main_links = cmake_link_body(cmake, "cellularity_android")
    vulkan_links = cmake_link_body(cmake, "cellularity_vulkan")
    require(
        "${CELLULARITY_JNI_SOURCES}" in main_sources,
        "cellularity_android must own CELLULARITY_JNI_SOURCES",
        errors,
    )
    require(
        "render/backend/vulkan/VulkanBackendAbiExports.cpp" in vulkan_sources,
        "cellularity_vulkan must own the backend ABI export implementation",
        errors,
    )
    require(
        cmake.count("render/backend/vulkan/VulkanBackendAbiExports.cpp") == 1,
        "VulkanBackendAbiExports.cpp must have one CMake owner",
        errors,
    )
    require(
        "VulkanBackendAbiExports.cpp" not in main_sources,
        "cellularity_android must not compile the Vulkan ABI implementation",
        errors,
    )
    require(
        "CELLULARITY_VULKAN_LIBRARY" not in main_links
        and re.search(r"(?m)^\s*cellularity_vulkan\s*$", main_links) is None,
        "cellularity_android must remain independent of the Vulkan loader/DSO",
        errors,
    )
    require(
        "cellularity_vulkan_backend_client" in main_links,
        "cellularity_android must consume the isolated main-side Vulkan ABI client",
        errors,
    )
    require(
        "cellularity_render_frame_result" in cmake
        and "cellularity_render_frame_result" in vulkan_links,
        "both Android render DSOs must consume the shared frame-result contract",
        errors,
    )


def verify_profiling_gate(errors: list[str]) -> None:
    cmake = read(CMAKE_PATH)
    app_build = read(APP_BUILD_PATH)
    telemetry = read(TELEMETRY_PATH)
    boundary_telemetry = read(BOUNDARY_TELEMETRY_PATH)
    telemetry_sources = telemetry + boundary_telemetry
    require(
        'option(CELLULARITY_ENABLE_VULKAN_PROFILING "Build opt-in Vulkan frame profiling" OFF)'
        in cmake,
        "the existing native profiling option must remain release-disabled",
        errors,
    )
    require(
        "CELLULARITY_ENABLE_RENDER_PROFILING" not in cmake,
        "V-19 must not introduce a second native profiling flag",
        errors,
    )
    require(
        cmake.count(
            "CELLULARITY_ENABLE_VULKAN_PROFILING="
            "$<BOOL:${CELLULARITY_ENABLE_VULKAN_PROFILING}>"
        ) == 2,
        "the native worker and Vulkan DSO must share the profiling compile definition",
        errors,
    )
    require(
        app_build.count(
            "buildConfigField 'boolean', 'ENABLE_RENDER_PROFILING', 'false'"
        ) == 1,
        "managed render profiling must default to false",
        errors,
    )
    require(
        app_build.count(
            "buildConfigField 'boolean', 'ENABLE_RENDER_PROFILING', 'true'"
        ) == 3,
        "only debug, performance, and experimental may enable managed profiling",
        errors,
    )
    for snippet in (
        "AtomicLongArray",
        "recordBoundaryJniCall",
        "recordRuntimePublicationPoll",
        "recordViewportSnapshot",
        "recordRenderTaskQueued",
        "recordRenderTaskExecuted",
        "recordFullDiagnostics",
        "art.gc.bytes-allocated",
    ):
        require(
            snippet in telemetry_sources,
            f"boundary telemetry is missing {snippet}",
            errors,
        )
    require(
        "BuildConfig.ENABLE_RENDER_PROFILING" in boundary_telemetry,
        "boundary counter storage must use the managed compile-time profiling gate",
        errors,
    )
    for path in (
        MAIN_BRIDGE_PATH,
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "AndroidMicroscopeView.kt",
    ):
        require(
            "BuildConfig.ENABLE_RENDER_PROFILING" in read(path),
            f"{relative(path)} must compile-gate its V-19 counters",
            errors,
        )


def verify_baseline_document(errors: list[str]) -> None:
    baseline = read(BASELINE_PATH)
    profiler = read(PROFILER_PATH)
    for method in FRAME_BOUNDARY_ENTRY_POINTS:
        require(
            f"`{method}`" in baseline,
            f"baseline call graph does not mention {method}",
            errors,
        )
    for scenario in EXPECTED_SCENARIOS:
        require(
            f"`{scenario}`" in baseline,
            f"baseline scenario matrix does not mention {scenario}",
            errors,
        )
        require(
            f'"{scenario}"' in profiler,
            f"Android profiler does not accept scenario {scenario}",
            errors,
        )
    for count in ("1/frame", "1/changed state", "1/poll"):
        require(count in baseline, f"baseline call graph is missing {count}", errors)
    require(
        "3/frame" not in baseline,
        "V-22 baseline still counts the removed Environment snapshot/patch round trip",
        errors,
    )
    require(
        "2/frame" not in baseline,
        "V-21 baseline must not count publication polling as frame work",
        errors,
    )
    require(
        ".codex/native-render-boundary/v19/" in baseline,
        "baseline must keep raw evidence outside Git",
        errors,
    )
    for snippet in (
        'parser.add_argument("--serial", required=True',
        '"--scene",',
        'parser.add_argument("--scenario", required=True',
        'serial.startswith("emulator-")',
        '"worktree_patch_sha256"',
        '"adb_commands"',
        '"display"',
        '"boundary_schema=1"',
    ):
        require(snippet in profiler, f"Android profiler is missing {snippet}", errors)


def verify_characterization_tests(errors: list[str]) -> None:
    worker_tests = read(VULKAN_WORKER_TEST_PATH)
    telemetry_tests = read(TELEMETRY_TEST_PATH)
    status_poller_tests = read(MICROSCOPE_STATUS_POLLER_TEST_PATH)
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
        require(test_name in worker_tests, f"worker test {test_name} is missing", errors)
    for test_name in (
        "frameResultCodecRoundTripsEveryOutcomeAndSignedSemanticValue",
        "frameResultCodecRejectsUnknownReservedAndAmbiguousValues",
    ):
        require(test_name in telemetry_tests, f"codec test {test_name} is missing", errors)
    for metric in (
        "boundary_jni_draw_microscope_count=1",
        "boundary_runtime_publication_poll_count=2",
        "boundary_viewport_snapshot_copy_bytes=28",
        "boundary_vulkan_render_task_pre_frame_drain_count=1",
        "boundary_full_diagnostics_p50_ns=123",
    ):
        require(metric in telemetry_tests, f"telemetry test is missing {metric}", errors)
    for test_name in (
        "cadenceDoesNotOverlapSlowPollAndDeliversOnlyThroughMainHost",
        "detachReconfigurationAndReleaseDiscardOldGenerationResults",
        "diagnosisAndPreparationFailureKeepTheirTransitionSemantics",
    ):
        require(
            test_name in status_poller_tests,
            f"runtime publication test {test_name} is missing",
            errors,
        )


def verify_v21_decoupling(errors: list[str]) -> None:
    renderer = read(ANDROID_MICROSCOPE_VIEW_PATH)
    poller = read(MICROSCOPE_STATUS_POLLER_PATH)
    publication_source = read(RUNTIME_PUBLICATION_SOURCE_PATH)
    gateway = read(SIMULATION_GATEWAY_PATH)
    worker = read(VULKAN_WORKER_PATH)
    main_bridge = read(MAIN_BRIDGE_PATH)
    native_microscope_jni = read(REPO_ROOT / "native/jni/NativeMicroscopeJni.cpp")

    for forbidden in ("pollNotifications", "pollFrameNotification"):
        require(
            forbidden not in renderer,
            f"microscope frame callback still owns runtime publication: {forbidden}",
            errors,
        )
    for snippet in (
        "class MicroscopeStatusPoller",
        "source.tryStartPoll(activeGeneration, ::onPublication)",
        "pending.generation != generation",
        "host.postDelayed(tick, delay)",
    ):
        require(snippet in poller, f"status poller is missing {snippet}", errors)
    for snippet in (
        "interface MicroscopeRuntimePublicationSource",
        "fun tryStartPoll(",
        "interface MicroscopeRuntimePublicationExecutor",
        "fun tryStartPublicationPoll(",
    ):
        require(
            snippet in publication_source,
            f"runtime publication boundary is missing {snippet}",
            errors,
        )
    for snippet in (
        "publicationPollInFlight.compareAndSet(false, true)",
        "runtimeAccessLock.tryLock()",
        "publicationJob.cancel()",
    ):
        require(snippet in gateway, f"publication executor is missing {snippet}", errors)
    require(
        "DIAGNOSTICS_FRAME_INTERVAL" not in worker,
        "Vulkan worker still schedules full diagnostics by frame interval",
        errors,
    )
    require(
        "diagnosticsReport()" not in worker[
            worker.index("bool NativeVulkanRenderWorker::renderRequestedFrame()"):
            worker.index("bool NativeVulkanRenderWorker::applyProfilingConfiguration()")
        ],
        "Vulkan frame execution must not serialize full diagnostics",
        errors,
    )
    require(
        "nativeMicroscopeRuntimePublicationIntervalMillis" in main_bridge
        and "nativeMicroscopeRuntimePublicationIntervalMillis" in native_microscope_jni,
        "native microscope publication cadence getter is incomplete",
        errors,
    )


def verify_v22_field_preview_boundary(errors: list[str]) -> None:
    controller_bridge = read(MAIN_BRIDGE_PATH)
    field_bridge = read(FIELD_PREVIEW_BRIDGE_PATH)
    loader = read(MAIN_LIBRARY_LOADER_PATH)
    view = read(FIELD_PREVIEW_VIEW_PATH)
    renderer = view
    native_render_jni = read(REPO_ROOT / "native/jni/NativeRenderJni.cpp")
    state_header = read(REPO_ROOT / "native/app/EnvironmentFieldPreviewState.h")
    endpoint_header = read(
        REPO_ROOT / "native/app/EnvironmentFieldPreviewEndpoint.h"
    )

    all_managed_sources = "\n".join(
        read(path)
        for pattern in ("*.java", "*.kt")
        for path in (REPO_ROOT / "app/src/main/java").rglob(pattern)
    )
    require(
        all_managed_sources.count('System.loadLibrary("cellularity")') == 1
        and loader.count('System.loadLibrary("cellularity")') == 1,
        "the required main DSO must have exactly one loader owner",
        errors,
    )
    for source, label in (
        (controller_bridge, "controller bridge"),
        (field_bridge, "field bridge"),
    ):
        require(
            "NativeCellularityLibraryLoader.requireLoaded()" in source,
            f"{label} does not use the shared required-DSO load result",
            errors,
        )
        require(
            "System.loadLibrary" not in source,
            f"{label} performs an independent main-DSO load",
            errors,
        )
    for snippet in (
        "private static final Throwable LOAD_FAILURE",
        "catch (UnsatisfiedLinkError | SecurityException error)",
        "public static boolean isAvailable()",
        "public static String failureDescription()",
        "public static void requireLoaded()",
    ):
        require(snippet in loader, f"main DSO loader is missing {snippet}", errors)

    expected_native_methods = {"nativePublishEnvironmentFieldPreviewState"}
    declarations = JAVA_NATIVE_DECLARATION.findall(field_bridge)
    require(
        set(declarations) == expected_native_methods
        and len(declarations) == len(expected_native_methods),
        "EnvironmentFieldPreviewBridge native declarations differ from V-26B",
        errors,
    )
    publish_declaration = re.findall(
        r"private\s+static\s+native\s+void\s+"
        r"nativePublishEnvironmentFieldPreviewState\s*\(\s*"
        r"long\s+endpointHandle\s*,\s*long\s+endpointGeneration\s*,\s*"
        r"int\s+targetWire\s*,\s*int\s+fieldTypeWire\s*,\s*"
        r"double\s+base\s*,\s*double\s+strength\s*,\s*double\s+angle\s*,\s*"
        r"double\s+offset\s*,\s*double\s+softness\s*,\s*"
        r"ByteBuffer\s+resultBuffer\s*\)\s*;",
        field_bridge,
        re.DOTALL,
    )
    require(
        len(publish_declaration) == 1
        and field_bridge.count("nativePublishEnvironmentFieldPreviewState(") == 2,
        "field preview publish ingress is not the single fixed V-22 descriptor",
        errors,
    )
    for method in expected_native_methods:
        symbol = FIELD_PREVIEW_JNI_PREFIX + method
        defining_sources = [
            path
            for path in (REPO_ROOT / "native/jni").glob("*.cpp")
            if symbol in read(path)
        ]
        require(
            defining_sources == [REPO_ROOT / "native/jni/NativeRenderJni.cpp"],
            f"{symbol} must be defined exactly once by NativeRenderJni.cpp",
            errors,
        )
    require(
        "nativeResolveEndpointGeneration" not in field_bridge
        and "nativeResolveEndpointGeneration" not in native_render_jni,
        "V-26B must remove the transitional field-preview generation resolver",
        errors,
    )

    for forbidden in (
        "EnvironmentBatchCodec",
        "EnvironmentPatch",
        "EnvironmentChange",
        "environmentStateBytes",
        "applyEnvironmentPatch",
        "pendingFieldState",
        "currentRadius",
        ".ordinal",
    ):
        require(
            forbidden not in renderer,
            f"field preview renderer retains obsolete managed state path: {forbidden}",
            errors,
        )
    require(
        "queueRenderTask" not in view,
        "field preview state still creates an unbounded managed render-task queue",
        errors,
    )
    for snippet in (
        "EnvironmentFieldPreviewUpdate final",
        "EnvironmentFieldPreviewState final",
        "kEnvironmentFieldPreviewResultSize = 24",
        "WrongEndpointKind = 6",
    ):
        require(snippet in state_header, f"V-22 state contract is missing {snippet}", errors)
    for snippet in (
        "std::shared_ptr<const EnvironmentFieldPreviewState>",
        "std::uint64_t mNextIngressRevision",
        "std::mutex mIngressMutex",
    ):
        require(snippet in endpoint_header, f"V-22 endpoint is missing {snippet}", errors)
    require(
        "GetDirectBufferAddress" in native_render_jni
        and "GetDirectBufferCapacity" in native_render_jni,
        "field preview JNI does not independently validate direct result storage",
        errors,
    )


def verify_v23_typed_render_command_infrastructure(errors: list[str]) -> None:
    cmake = read(CMAKE_PATH)
    migration_map = read(COMMAND_MIGRATION_MAP_PATH)
    worker = read(VULKAN_WORKER_PATH)
    interaction_header = read(
        REPO_ROOT
        / "native/app/genome_preview/GenomePreviewInteractionQueue.h"
    )
    retained_header = read(
        REPO_ROOT / "native/app/genome_preview/GenomePreviewRetainedState.h"
    )
    budget_header = read(
        REPO_ROOT / "native/app/genome_preview/RetainedPayloadBudget.h"
    )
    genome_capture_header = read(
        REPO_ROOT
        / "native/app/genome_preview/GenomePreviewCaptureRequestStore.h"
    )
    substrate_capture_header = read(
        REPO_ROOT
        / "native/app/microscope/SubstratePreviewCaptureRequestStore.h"
    )
    capture_state_header = read(
        REPO_ROOT
        / "native/app/render/preview/NativePreviewCaptureRequestState.h"
    )
    infrastructure_tests = read(
        REPO_ROOT
        / "native/tests/genome_preview_command_infrastructure_tests.cpp"
    )
    genome_codec_header = read(
        REPO_ROOT / "native/formats/GenomeDraftCodec.h"
    )
    managed_genome_contract_test = read(
        REPO_ROOT
        / "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/model/"
        "GenomeDraftWireCodecTest.kt"
    )
    worker_tests = read(VULKAN_WORKER_TEST_PATH)
    gateway_tests = read(
        REPO_ROOT
        / "app/src/test/java/com/cytarch/cellularity/platform/simulation/"
        "AndroidSimulationGatewayTest.kt"
    )

    managed_call_sites: dict[str, int] = {}
    call_pattern = re.compile(r"\brenderSurface\.queueRenderTask\s*\{")
    for path in (REPO_ROOT / "app/src/main/java").rglob("*.kt"):
        count = len(call_pattern.findall(read(path)))
        if count:
            managed_call_sites[relative(path)] = count
    require(
        managed_call_sites == {},
        "V-25 must not retain producer-side queueRenderTask closures: "
        f"{managed_call_sites}",
        errors,
    )
    all_queue_render_task_invocations: dict[str, int] = {}
    invocation_pattern = re.compile(r"\.\s*queueRenderTask\s*(?:\(|\{)")
    for path in (REPO_ROOT / "app/src/main/java").rglob("*.kt"):
        count = len(invocation_pattern.findall(read(path)))
        if count:
            all_queue_render_task_invocations[relative(path)] = count
    require(
        all_queue_render_task_invocations == {},
        "V-26B must remove every arbitrary managed render-task invocation: "
        f"{all_queue_render_task_invocations}",
        errors,
    )
    require(
        "Command.Task" not in worker
        and "data class Task(" not in worker
        and "drainTasksBeforeFrame" not in worker,
        "V-26B must remove the Vulkan arbitrary-task compatibility adapter",
        errors,
    )

    for operation in (
        "GenomePreviewView.restoreViewport",
        "GenomePreviewView.update",
        "GenomePreviewView.captureGhost",
        "GenomePreviewView.clearGhost",
        "GenomePreviewView.handleMove",
        "GenomePreviewView.handleZoom",
        "GenomePreviewView.handleTap",
        "GenomePreviewView.enqueueGenomePreviewCapture",
        "AndroidMicroscopeView.enqueueSubstratePreviewCapture",
        "AndroidMicroscopeView.submit(List)",
    ):
        require(
            f"`{operation}`" in migration_map,
            f"typed migration map is missing {operation}",
            errors,
        )
    for contract in (
        "Current owner / producer",
        "Current consumer",
        "Ordering and coalescing",
        "Cancellation and lifetime",
        "Completion/publication",
        "Typed replacement",
        "kGenomePreviewInteractionCapacity = 512",
        "V-23 adds no JNI method",
    ):
        require(
            contract in migration_map,
            f"typed migration map is missing {contract}",
            errors,
        )

    for snippet in (
        "kV19GenomePreviewInteractionHighWaterUpperBound = 196",
        "kGenomePreviewInteractionCapacity =",
        "GenomePreviewRestoreViewport",
        "GenomePreviewMoveViewport",
        "GenomePreviewZoomViewport",
        "GenomePreviewSelectGene",
        "GenomePreviewCaptureGhost",
        "GenomePreviewClearGhost",
        "GenomePreviewFrameRequestFlag final",
        "consumePendingOnRenderOwner(",
        "std::array<GenomePreviewInteractionCommand,",
    ):
        require(
            snippet in interaction_header,
            f"typed Genome Preview interaction queue is missing {snippet}",
            errors,
        )
    for snippet in (
        "LatestValueExchange<",
        "mContentExchange",
        "mViewExchange",
        "std::mutex mIngressMutex",
        "kGenomePreviewInteractionCapacity + 4U",
        "enqueueInteraction(",
        "consumePendingOnRenderOwner(",
    ):
        require(
            snippet in retained_header,
            f"Genome Preview retained state is missing {snippet}",
            errors,
        )
    for snippet in (
        "class Reservation final",
        "tryReserve(std::size_t bytes)",
        "Reservation(const Reservation&) = delete",
        "boundedCapacityFromObservedHighWater(",
        "checkedSizeProductOrZero(",
    ):
        require(
            snippet in budget_header,
            f"retained payload budget is missing {snippet}",
            errors,
        )
    for source, snippets in (
        (
            genome_capture_header,
            (
                "kGenomePreviewCaptureSlotCapacity",
                "kGenomePreviewCaptureInputByteBudget",
                "GenomePreviewCaptureRequestStore final",
                "consumePendingOnRenderOwner(",
                "cancelAttachment(",
            ),
        ),
        (
            substrate_capture_header,
            (
                "kSubstratePreviewCaptureSlotCapacity",
                "SubstratePreviewCaptureRequestStore final",
                "consumePendingOnRenderOwner(",
                "cancelAttachment(",
            ),
        ),
    ):
        for snippet in snippets:
            require(
                snippet in source,
                f"typed capture store is missing {snippet}",
                errors,
            )
    for snippet in (
        "PreviewCaptureRequestState",
        "waitForTerminal(",
        "await(",
        "consumeDelivery(",
        "abandonDelivery(",
    ):
        require(
            snippet in capture_state_header,
            f"common typed capture state is missing {snippet}",
            errors,
        )

    require(
        "kGenomeDraftMaximumWireBytes" in genome_codec_header
        and "78'616U" in genome_codec_header
        and "assertEquals(78_616, GenomeContract.MAX_WIRE_BYTES)"
        in managed_genome_contract_test,
        "Kotlin/native GenomeDraft maximum wire-byte golden contract is incomplete",
        errors,
    )
    retained_implementation = read(
        REPO_ROOT / "native/app/genome_preview/GenomePreviewRetainedState.cpp"
    )
    capture_implementation = read(
        REPO_ROOT
        / "native/app/genome_preview/GenomePreviewCaptureRequestStore.cpp"
    )
    require(
        retained_implementation.count(
            "genomeDraftBytes(bytes, bytes + byteCount)"
        )
        == 1
        and capture_implementation.count(
            "genomeDraftBytes(bytes, bytes + byteCount)"
        )
        == 1,
        "each V-23 GenomeDraft owner must perform one explicit source copy",
        errors,
    )
    for test_name in (
        "testCapacityPolicyUsesMeasuredBoundsWithoutClamping",
        "testPayloadBudgetCountsUniqueAllocationsRatherThanFences",
        "testEditorWorstCaseBudgetFormulaHasFourExtraPositions",
        "testLatestStateCoalescesAndDependentCommandKeepsPriorFence",
        "testFrameRequestFlagCoalescesAndConsumesNewestPublishedState",
        "testRetainedStateBudgetFailurePreservesPreviouslyPublishedSnapshot",
        "testRetainedStateRevisionsNeverWrapToZero",
        "testInteractionQueuePreservesFifoOverflowAndGenerationFences",
        "testConcurrentLatestPublicationNeverProducesTornContent",
        "testGenomeCaptureStoreBoundsCopiesAndTerminalTransitions",
        "testGenomeCaptureBudgetAndCloseAreIndependentFromEditorBudget",
        "testCaptureTerminalRaceHasExactlyOneWinner",
        "testGenomeSurfaceLossCancelsOnlyMatchingAttachment",
        "testSubstrateCaptureStoreIsBoundedOrderedAndAttachmentAware",
        "testSubstrateCloseCancelsAndWakesExactlyOnce",
        "testSubstrateSurfaceLossCancelsOnlyMatchingAttachment",
    ):
        require(
            test_name in infrastructure_tests,
            f"V-23 host suite is missing {test_name}",
            errors,
        )
    for source, test_names in (
        (
            worker_tests,
            (
                "testPendingFrameOpensAfterResumeAndSurface",
                "testPauseRetiresSurfaceAndRequiresResubmit",
                "testOnDemandRetryIsNativeAndBounded",
                "testRecoverableFailureNeedsReplacementSurface",
                "testSurfaceLossSignalsAndReattaches",
            ),
        ),
        (
            infrastructure_tests,
            (
                "testEndpointReplacesManagedGateWithoutResimulatingViewOrSurfaceState",
                "testEndpointInteractionsApplyInFifoAgainstTheirStateFence",
                "testReplacingSurfaceCancelsOnlyOldAttachmentCaptures",
                "testEndpointCloseWakesBlockedCaptureAwait",
            ),
        ),
        (
            gateway_tests,
            (
                "controlsReachDurableEndpointWithoutWaitingForSurfaceAttachment",
                "surfaceReplacementReusesWorldAndStaleDetachCannotPauseReplacement",
            ),
        ),
    ):
        for test_name in test_names:
            require(
                test_name in source,
                f"V-23 compatibility test is missing {test_name}",
                errors,
            )

    for source in (
        "app/genome_preview/GenomePreviewCaptureRequestStore.cpp",
        "app/genome_preview/GenomePreviewInteractionQueue.cpp",
        "app/genome_preview/GenomePreviewRetainedState.cpp",
        "app/genome_preview/RetainedPayloadBudget.cpp",
        "app/microscope/SubstratePreviewCaptureRequestStore.cpp",
    ):
        require(
            cmake.count(source) == 1
            and source in cmake_set(cmake, "CELLULARITY_RENDERING_SOURCES"),
            f"{source} must have one CELLULARITY_RENDERING_SOURCES owner",
            errors,
        )
    require(
        "cellularity_genome_preview_command_infrastructure_tests" in cmake,
        "V-23 host-test target is missing",
        errors,
    )



def verify_v24_microscope_input_batch(errors: list[str]) -> None:
    cmake = read(CMAKE_PATH)
    bridge = read(MAIN_BRIDGE_PATH)
    view = read(ANDROID_MICROSCOPE_VIEW_PATH)
    renderer = view
    managed_layout = read(MICROSCOPE_INPUT_BATCH_LAYOUT_PATH)
    managed_layout_tests = read(MICROSCOPE_INPUT_BATCH_TEST_PATH)
    input_stream_tests = read(MICROSCOPE_INPUT_STREAM_TEST_PATH)
    gesture = read(MICROSCOPE_GESTURE_PATH)
    gesture_tests = read(MICROSCOPE_GESTURE_TEST_PATH)
    status_poller_tests = read(MICROSCOPE_STATUS_POLLER_TEST_PATH)
    gateway = read(SIMULATION_GATEWAY_PATH)
    gateway_tests = read(
        REPO_ROOT
        / "app/src/test/java/com/cytarch/cellularity/platform/simulation/"
        "AndroidSimulationGatewayTest.kt"
    )
    native_layout_header = read(
        REPO_ROOT / "native/app/microscope/MicroscopeInputBatchLayout.h"
    )
    native_layout_source = read(
        REPO_ROOT / "native/app/microscope/MicroscopeInputBatchLayout.cpp"
    )
    native_jni = read(REPO_ROOT / "native/jni/NativeMicroscopeJni.cpp")
    native_batch_tests = read(
        REPO_ROOT / "native/tests/microscope_input_batch_tests.cpp"
    )
    migration_map = read(
        REPO_ROOT / "native/rendering/NATIVE_RENDER_COMMAND_MIGRATION_MAP.md"
    )

    for source, snippets in (
        (
            managed_layout,
            (
                "const val INPUT_HEADER_SIZE = 48",
                "const val COMMAND_RECORD_SIZE = 32",
                "const val COMMAND_CAPACITY = 64",
                "const val RESULT_HEADER_SIZE = 64",
                "const val RESULT_CAPACITY = RESULT_HEADER_SIZE + COMMAND_CAPACITY * RESULT_STATUS_SIZE",
                "ByteBuffer.allocateDirect(INPUT_CAPACITY)",
                "ByteBuffer.allocateDirect(RESULT_CAPACITY)",
            ),
        ),
        (
            native_layout_header,
            (
                "kMicroscopeInputHeaderSize = 48",
                "kMicroscopeInputCommandRecordSize = 32",
                "kMicroscopeInputCommandCapacity = 64",
                "kMicroscopeInputResultHeaderSize = 64",
                "kPresentationMotionSnapshotSize = 32",
            ),
        ),
        (
            native_layout_source,
            (
                "decodeMicroscopeInputBatch(",
                "writeMicroscopeInputBatchResult(",
                "markMicroscopeInputBatchInternalFailure(",
                "writePresentationMotionSnapshot(",
                "std::memset(destination, 0, kMicroscopeInputResultBufferCapacity)",
            ),
        ),
    ):
        for snippet in snippets:
            require(
                snippet in source,
                f"V-24 fixed-layout contract is missing {snippet}",
                errors,
            )

    native_declaration = re.compile(
        r"private static native void nativeSubmitMicroscopeInputBatch\(\s*"
        r"long endpointHandle,\s*long surfaceToken,\s*ByteBuffer input,\s*"
        r"ByteBuffer result,\s*long expectedPlacementGenomeRevision\s*\);",
        re.MULTILINE | re.DOTALL,
    )
    require(
        len(native_declaration.findall(bridge)) == 1,
        "V-26B must expose exactly one microscope batch JNI descriptor",
        errors,
    )
    descriptor = "(JJLjava/nio/ByteBuffer;Ljava/nio/ByteBuffer;J)V"
    require(
        migration_map.count(descriptor) == 1
        and "[B)V" not in migration_map,
        "V-26B migration map must retain only the revision-based batch descriptor",
        errors,
    )
    batch_jni_symbol = MAIN_JNI_PREFIX + "nativeSubmitMicroscopeInputBatch"
    exact_batch_jni_symbol = re.compile(
        rf"\b{re.escape(batch_jni_symbol)}\s*\("
    )
    require(
        len(exact_batch_jni_symbol.findall(native_jni)) == 1,
        "V-24 microscope batch JNI symbol must be defined exactly once",
        errors,
    )
    batch_helper_start = native_jni.index("void submitMicroscopeInputBatch(")
    batch_helper_end = native_jni.index(
        'extern "C" JNIEXPORT', batch_helper_start
    )
    batch_jni = native_jni[batch_helper_start:batch_helper_end]
    ordered_batch_jni_steps = (
        "requireDirectOutputBuffer(",
        "initializeMicroscopeInputBatchResult(",
        "GetDirectBufferAddress(inputBuffer)",
        "decodeMicroscopeInputBatch(",
        "renderEndpointLeaseFromHandle(env, endpointHandle)",
        "lease->tryAcquireInput(",
        "lease->acquirePlacementGenome(",
        "dispatchStarted = true",
        "controller->submitMicroscopeInputBatch(",
    )
    ordered_offsets = [batch_jni.find(step) for step in ordered_batch_jni_steps]
    require(
        all(offset >= 0 for offset in ordered_offsets)
        and ordered_offsets == sorted(ordered_offsets),
        "V-26B JNI must validate storage/input and acquire retained placement state before dispatch",
        errors,
    )
    require(
        "batch.tapCount == 0 && expectedPlacementGenomeRevision != 0" in batch_jni
        and "batch.tapCount == 1" in batch_jni
        and "expectedPlacementGenomeRevision == 0" in batch_jni,
        "V-26B Tap placement-revision cardinality validation is incomplete",
        errors,
    )
    for snippet in (
        "requireDirectOutputBuffer(",
        "controller->closeMicroscopeInputAdmission();",
        "markMicroscopeInputBatchInternalFailure(",
    ):
        require(
            snippet in native_jni,
            f"V-24 fail-closed JNI contract is missing {snippet}",
            errors,
        )
    for snippet in (
        "result.isReadOnly()",
        "result.capacity() < 320",
    ):
        require(
            snippet in bridge,
            f"V-24 result-buffer validation is missing {snippet}",
            errors,
        )
    for snippet in (
        "MotionEventMicroscopePointerEventView",
        "AndroidMicroscopeInputBatchBuilder",
        "endpoint.submitMicroscopeInputBatch(",
        "result.decode(builder.batchSequence, builder.commandCount)",
        "statusPoller.requestImmediatePoll()",
    ):
        require(snippet in view, f"V-24 Android input path is missing {snippet}", errors)
    require(
        "renderSurface.queueRenderTask" not in view,
        "V-25 must remove the remaining substrate-capture render task",
        errors,
    )
    touch_start = view.index("override fun onTouchEvent(event: MotionEvent)")
    touch_end = view.index("private inline fun collectDetectorCommand", touch_start)
    touch_handler = view[touch_start:touch_end]
    pointer_command_offset = touch_handler.find(
        "gestureInterpreter.onPointerEvent("
    )
    detector_command_offset = touch_handler.find(
        "gestureDetector.onTouchEvent(event)", pointer_command_offset
    )
    submit_offset = touch_handler.find(
        "submitCollectedInput(inputBatchBuilder)", detector_command_offset
    )
    touch_order = (
        pointer_command_offset,
        detector_command_offset,
        submit_offset,
    )
    require(
        all(offset >= 0 for offset in touch_order)
        and tuple(sorted(touch_order)) == touch_order,
        "V-24 must append synchronous detector commands after pointer commands before one submit",
        errors,
    )
    require(
        view.count("RenderPerformanceTelemetry.recordCameraInput(") == 1
        and "if (result.cameraChanged && backendKind == RenderBackendKind.OPEN_GL_ES)"
        in view,
        "V-28 must keep managed camera latency only on the GLES input path",
        errors,
    )
    require(
        "viewportState()" not in view and "executeInputCommand" not in renderer,
        "V-24 restored an allocating or per-command managed microscope input path",
        errors,
    )
    for snippet in (
        "interface MicroscopeInputCommandSink",
        "fun onPointerEvent(event: MicroscopePointerEventView, sink: MicroscopeInputCommandSink)",
        "fun reset(timestampMillis: Long)",
    ):
        require(snippet in gesture, f"V-24 shared gesture contract is missing {snippet}", errors)
    for snippet in (
        "tryLock()",
        "MicroscopeInputIngressStatus.ATTACHMENT_UNAVAILABLE",
        "detachActiveRenderAttachmentLocked()",
        "activePointerCount() == 0",
    ):
        require(snippet in gateway, f"V-24 gateway contract is missing {snippet}", errors)

    for source, test_names in (
        (
            gesture_tests,
            (
                "twoPointersPreserveTouchThenPanThenPinchOrdering",
                "resetDropsOldGestureGeometryAndSuppressesItsLateFling",
            ),
        ),
        (
            managed_layout_tests,
            (
                "builderWritesExactLittleEndianCib1Records",
                "managedRejectionClearsEveryCir1StatusSlot",
                "builderAcceptsExactlySixtyFourCommandsAndRejectsTheNextRecord",
                "eventTimeConversionRejectsNegativeAndOverflowingAndroidClocks",
                "resultReaderAcceptsOnlyCanonicalInternalFailureStatusOrdering",
                "motionReaderUsesRevisionAndFlagsWithoutAllocatingViewportArray",
            ),
        ),
        (
            status_poller_tests,
            (
                "immediatePollCoalescesAndDeliversIngressWithoutRuntimeFrame",
                "rejectedMainHostPostRetainsCompletionAndCannotLoseImmediateWakeup",
            ),
        ),
        (
            input_stream_tests,
            (
                "staleGestureIsConsumedUntilANewEpochAndFreshDownArrive",
                "detachedDetectorBatchesUseOneMonotonicSequenceAndExhaustFailClosed",
                "everyIngressStatusHasAnExplicitRecoveryPolicy",
            ),
        ),
        (
            read(
                REPO_ROOT
                / "app/src/test/java/com/cytarch/cellularity/platform/render/"
                "microscope/ActiveCameraPresentationCadenceTest.kt"
            ),
            ("unavailableMotionSnapshotSettlesInsteadOfLeavingAHotLoop",),
        ),
        (
            read(
                REPO_ROOT
                / "app/src/test/java/com/cytarch/cellularity/platform/render/"
                "microscope/MicroscopePresentationStateMachineTest.kt"
            ),
            ("rejectedCameraCommandNeverStartsAnActiveCadence",),
        ),
        (
            gateway_tests,
            (
                "inputEpochRejectsStaleBatchesAndAdvancesOnlyAfterPointerDrain",
                "inputBatchDoesNotWaitForSurfaceWriteLock",
                "closeRequestsPointerCancellationBeforeDetachingSurface",
                "attachmentWithOldPointersPublishesNoEpochUntilOwnerDrain",
                "rejectedUpDuringHandoffCannotLeakIntoTheFreshEpoch",
                "inputBatchEpochValidationAndResyncAreOneAttachmentTransaction",
            ),
        ),
        (
            native_batch_tests,
            (
                "testExactCib1DecodeAndCapacityBoundary",
                "testMalformedBatchIsRejectedBeforeDispatch",
                "testProfilingTimestampIsDiagnosticMetadataOnly",
                "testCir1AndCms1WritersAreCanonical",
                "testPresentationBatchPublishesOnlyTheFinalViewport",
                "testTwoPointerMoveIsOnePresentationTransaction",
                "testSemanticRejectionPreservesPrefixAndLaterCommands",
                "testWorldCommandOrderMatchesBatchRecords",
                "testInternalFailureStatusImageIsExactAtEveryDispatchPosition",
                "testIngressCapacityDoesNotRaiseTheTwentyPointerMailboxLimit",
                "testRejectedCameraCommandStillReturnsCurrentRevisions",
                "testWorldCoordinatesPrecedeLaterCameraMutationAndResyncDrainsPointers",
            ),
        ),
    ):
        for test_name in test_names:
            require(test_name in source, f"V-24 suite is missing {test_name}", errors)

    require(
        cmake.count("app/microscope/MicroscopeInputBatchLayout.cpp") == 1
        and "cellularity_microscope_input_batch_tests" in cmake,
        "V-24 native layout source or host-test target is missing",
        errors,
    )


def verify_v25_native_preview_endpoint(errors: list[str]) -> None:
    capture_bridge = read(PREVIEW_CAPTURE_BRIDGE_PATH)
    capture_contract_tests = read(PREVIEW_CAPTURE_CONTRACT_TEST_PATH)
    managed_capture_layouts = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "NativePreviewCaptureLayouts.kt"
    )
    managed_endpoint_layouts = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "GenomePreviewEndpointLayouts.kt"
    )
    capture_request = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "NativePreviewCaptureRequest.kt"
    )
    capture_result = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "NativePreviewCaptureResult.kt"
    )
    genome_coordinator = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "GenomePreviewCaptureCoordinator.kt"
    )
    substrate_coordinator = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "SubstratePreviewCaptureCoordinator.kt"
    )
    genome_view = read(GENOME_PREVIEW_VIEW_PATH)
    microscope_view = read(ANDROID_MICROSCOPE_VIEW_PATH)
    native_layouts = read(
        REPO_ROOT
        / "native/app/render/preview/NativePreviewCaptureLayouts.h"
    )
    request_state = read(
        REPO_ROOT
        / "native/app/render/preview/NativePreviewCaptureRequestState.h"
    )
    endpoint_header = read(
        REPO_ROOT / "native/app/genome_preview/GenomePreviewEndpoint.h"
    )
    endpoint_source = read(
        REPO_ROOT / "native/app/genome_preview/GenomePreviewEndpoint.cpp"
    )
    substrate_endpoint = read(
        REPO_ROOT / "native/app/microscope/SubstratePreviewCaptureEndpoint.cpp"
    )
    render_jni = read(REPO_ROOT / "native/jni/NativeRenderJni.cpp")
    capture_jni_path = REPO_ROOT / "native/jni/NativePreviewCaptureJni.cpp"
    handle_jni_path = (
        REPO_ROOT / "native/jni/NativePreviewCaptureRequestHandle.cpp"
    )
    capture_jni = read(capture_jni_path)
    handle_jni = read(handle_jni_path)
    infrastructure_tests = read(
        REPO_ROOT
        / "native/tests/genome_preview_command_infrastructure_tests.cpp"
    )
    rendering_tests = read(
        REPO_ROOT / "native/tests/parity/rendering_tests.cpp"
    )

    obsolete_gate_paths = (
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
        "genome_editor/GenomePreviewUpdateGate.kt",
        REPO_ROOT
        / "app/src/test/java/com/cytarch/cellularity/ui/play_menu/screens/"
        "genome_editor/GenomePreviewUpdateGateTest.kt",
    )
    for path in obsolete_gate_paths:
        require(
            not path.exists(),
            f"V-25 must remove obsolete managed gate {relative(path)}",
            errors,
        )
    require(
        "queueRenderTask" not in genome_view
        and "queueRenderTask" not in microscope_view,
        "V-25 preview producers must submit typed native commands without closures",
        errors,
    )

    expected_capture_methods = {
        "nativeEnqueueGenome",
        "nativeEnqueueSubstrate",
        "nativeAwait",
        "nativeCancel",
        "nativeDestroy",
    }
    actual_capture_methods = set(JAVA_NATIVE_DECLARATION.findall(capture_bridge))
    require(
        actual_capture_methods == expected_capture_methods
        and len(JAVA_NATIVE_DECLARATION.findall(capture_bridge)) == 5,
        "NativePreviewCaptureBridge must own exactly five fixed native methods: "
        f"{sorted(actual_capture_methods)}",
        errors,
    )
    for declaration in (
        r"private\s+static\s+native\s+void\s+nativeEnqueueGenome\(\s*long\s+endpointHandle,\s*long\s+requestId,\s*byte\[\]\s+genomeDraftBytes,\s*double\s+maximumSimulationTime,\s*ByteBuffer\s+admissionBuffer\s*\)",
        r"private\s+static\s+native\s+void\s+nativeEnqueueSubstrate\(\s*long\s+endpointHandle,\s*long\s+requestId,\s*long\s+substratePreviewToken,\s*long\s+attachmentGeneration,\s*ByteBuffer\s+admissionBuffer\s*\)",
        r"private\s+static\s+native\s+byte\[\]\s+nativeAwait\(\s*long\s+requestHandle,\s*long\s+timeoutMillis,\s*ByteBuffer\s+resultBuffer\s*\)",
        r"private\s+static\s+native\s+int\s+nativeCancel\(\s*long\s+requestHandle\s*\)",
        r"private\s+static\s+native\s+void\s+nativeDestroy\(\s*long\s+requestHandle\s*\)",
    ):
        require(
            re.search(declaration, capture_bridge, re.MULTILINE) is not None,
            f"V-25 capture bridge descriptor does not match {declaration}",
            errors,
        )
    require(
        capture_bridge.count("@Keep") == 6
        and "NativeCellularityLibraryLoader.requireLoaded();" in capture_bridge,
        "V-25 capture bridge class and all five native methods must survive R8",
        errors,
    )
    require(
        "return new JniNativePreviewCaptureRequest(requestHandle, requestId);"
        in capture_bridge
        and "catch (Throwable failure)" in capture_bridge
        and "nativeDestroy(requestHandle);" in capture_bridge,
        "accepted native capture handles must be destroyed if Java ownership allocation fails",
        errors,
    )

    symbol_owners = {
        "nativeEnqueueGenome": capture_jni_path,
        "nativeEnqueueSubstrate": capture_jni_path,
        "nativeAwait": capture_jni_path,
        "nativeCancel": handle_jni_path,
        "nativeDestroy": handle_jni_path,
    }
    jni_sources = {
        path: read(path) for path in (REPO_ROOT / "native/jni").glob("*.cpp")
    }
    for method, owner in symbol_owners.items():
        symbol = PREVIEW_CAPTURE_JNI_PREFIX + method
        actual_owners = {
            path for path, source in jni_sources.items() if symbol in source
        }
        require(
            actual_owners == {owner},
            f"{symbol} must have exactly one owner: {relative(owner)}",
            errors,
        )
    other_jni = "\n".join(
        source for path, source in jni_sources.items() if path != handle_jni_path
    )
    require(
        "reinterpret_cast<NativePreviewCaptureRequestHandle" in handle_jni
        and "reinterpret_cast<std::uintptr_t>" in handle_jni
        and "reinterpret_cast<NativePreviewCaptureRequestHandle" not in other_jni,
        "preview request pointer conversion must remain isolated in its handle source",
        errors,
    )

    all_production_sources = "\n".join(
        read(path)
        for root in (REPO_ROOT / "app/src/main", REPO_ROOT / "native")
        for path in root.rglob("*")
        if path.is_file()
        and path.suffix in {".java", ".kt", ".cpp", ".h"}
    )
    for obsolete_name in (
        "nativeCaptureGenomeFilePreviewRgba",
        "nativeCaptureSubstratePreviewRgba",
    ):
        require(
            obsolete_name not in all_production_sources,
            f"V-25 still exposes obsolete synchronous capture {obsolete_name}",
            errors,
        )

    for source, snippets in (
        (
            native_layouts,
            (
                "kPreviewCaptureAdmissionMagic = 0x31414350U",
                "kPreviewCaptureAdmissionSize = 24",
                "kPreviewCaptureResultMagic = 0x31524350U",
                "kPreviewCaptureResultSize = 56",
                "Completed = 0",
                "TimedOut = 3",
                "AlreadyConsumed = 4",
                "Rgba8 = 1",
                "TopLeft = 1",
            ),
        ),
        (
            managed_capture_layouts,
            (
                "MAGIC = 0x31414350",
                "SIZE_BYTES = 24",
                "MAGIC = 0x31524350",
                "SIZE_BYTES = 56",
                "bytes.getInt(44) == 0",
                "requestId > 0L",
                "Math.multiplyExact",
            ),
        ),
    ):
        for snippet in snippets:
            require(
                snippet in source,
                f"V-25 fixed capture layout is missing {snippet}",
                errors,
            )
    for snippet in (
        "std::condition_variable mTerminalCondition",
        "bool mConsumed = false",
        "bool mAwaitActive = false",
        "consumeDelivery(",
        "abandonDelivery(",
    ):
        require(
            snippet in request_state,
            f"V-25 exactly-once request state is missing {snippet}",
            errors,
        )
    for snippet in (
        "GenomePreviewRetainedState mRetainedState",
        "GenomePreviewInteractionQueue mInteractionQueue",
        "GenomePreviewCaptureRequestStore mCaptureStore",
        "GenomePreviewFrameRequestFlag mFrameRequest",
        "readAcknowledgment() const noexcept",
        "draw(NativeCellularityController& controller)",
    ):
        require(
            snippet in endpoint_header,
            f"V-25 native Genome Preview endpoint is missing {snippet}",
            errors,
        )
    native_draw_start = render_jni.index(MAIN_JNI_PREFIX + "nativeDrawGenomePreview")
    native_draw_end = render_jni.index(
        'extern "C" JNIEXPORT', native_draw_start + 1
    )
    native_draw_body = render_jni[native_draw_start:native_draw_end]
    require(
        "selectedGeneIndex" not in native_draw_body
        and re.search(
            r"private\s+static\s+native\s+long\s+nativeDrawGenomePreview\(\s*long\s+handle,\s*ByteBuffer\s+acknowledgmentBuffer\s*\)",
            read(MAIN_BRIDGE_PATH),
            re.MULTILINE,
        )
        is not None,
        "drawGenomePreview must consume retained state without frame-varying arguments",
        errors,
    )
    require(
        ".stats(" not in genome_view,
        "Genome Preview must publish cell count through its acknowledgment",
        errors,
    )
    for source, timeout in (
        (genome_coordinator, "CAPTURE_TIMEOUT_MILLIS = 30_000L"),
        (substrate_coordinator, "CAPTURE_TIMEOUT_MILLIS = 5_000L"),
    ):
        require(
            timeout in source
            and "request.await(timeoutMillis)" in source
            and "request.cancel()" in source
            and "request.close()" in source,
            f"V-25 coordinator contract is missing {timeout}",
            errors,
        )
    for snippet in (
        "val requestId: Long",
        "val status: NativePreviewCaptureStatus",
        "val width: Int",
        "val height: Int",
        "val rowStrideBytes: Int",
        "val pixelFormat: NativePreviewPixelFormat",
        "val origin: NativePreviewOrigin",
        "val alphaMode: NativePreviewAlphaMode",
        "val payload: ByteArray?",
    ):
        require(snippet in capture_result, f"capture result is missing {snippet}", errors)
    for snippet in (
        "AtomicLong(requestHandle)",
        "@Synchronized",
        "getAndSet(0L)",
        "NativePreviewCaptureBridge.destroy(current)",
    ):
        require(snippet in capture_request, f"capture request is missing {snippet}", errors)

    await_start = capture_jni.index(PREVIEW_CAPTURE_JNI_PREFIX + "nativeAwait")
    await_body = capture_jni[await_start:]
    direct_buffer_offset = await_body.find("requireDirectBuffer(")
    timeout_offset = await_body.find("if (timeoutMillis <= 0)")
    mutation_offset = await_body.find("std::memset(resultBytes")
    require(
        0 <= direct_buffer_offset < timeout_offset < mutation_offset
        and "controllerFromHandle" not in await_body
        and "env->NewByteArray(" in await_body
        and await_body.count("abandonDelivery(") >= 2
        and "consumeDelivery(" in await_body,
        "nativeAwait must validate before mutation, avoid controller ownership, and consume exactly once",
        errors,
    )
    require(
        "previousAttachmentGeneration" in endpoint_source
        and "cancelAttachment(previousAttachmentGeneration)" in endpoint_source
        and "previousAttachmentGeneration" in substrate_endpoint
        and "cancelAttachment(previousAttachmentGeneration)" in substrate_endpoint,
        "surface replacement must cancel only work bound to the old attachment",
        errors,
    )

    for test_name in (
        "testEndpointReplacesManagedGateWithoutResimulatingViewOrSurfaceState",
        "testEndpointInteractionsApplyInFifoAgainstTheirStateFence",
        "testReplacingSurfaceCancelsOnlyOldAttachmentCaptures",
        "testEndpointCloseWakesBlockedCaptureAwait",
        "testCompletedPayloadReleasesExactOutputReservationAfterDelivery",
        "testInvalidCaptureMetadataFailsWithoutRetainingOutput",
        "testCaptureLayoutsAndExactlyOnceAwaitContract",
        "testEndpointAndCaptureGoldenLayouts",
    ):
        require(
            test_name in infrastructure_tests,
            f"V-25 native endpoint suite is missing {test_name}",
            errors,
        )
    require(
        "Independent file capture preserves visible editor revisions"
        in rendering_tests
        and "The first ordinary frame after file capture renders visible editor state"
        in rendering_tests,
        "V-25 rendering suite must prove file capture leaves visible state intact",
        errors,
    )
    for test_name in (
        "pca1GoldenLayoutDecodesEveryFixedOffset",
        "pcr1GoldenLayoutDecodesCompletedAndTerminalResults",
        "layoutsFailClosedForMalformedMetadataAndStorage",
        "captureBridgeOwnsExactlyFiveNativeMethodsWithFixedDescriptors",
        "endpointAcknowledgmentTreatsUint64FieldsAsRawBits",
    ):
        require(
            test_name in capture_contract_tests,
            f"V-25 managed contract suite is missing {test_name}",
            errors,
        )
    for snippet in (
        "mCaptureStore.consumePendingOnRenderOwner",
        "mStore.consumePendingOnRenderOwner",
    ):
        require(
            snippet in endpoint_source + substrate_endpoint
            or snippet in render_jni,
            f"V-25 render-owner consumption path is missing {snippet}",
            errors,
        )
    require(
        "Long.MIN_VALUE" in capture_contract_tests
        and "attachmentGeneration <= 0L" not in capture_bridge
        and "substratePreviewToken <= 0L" not in capture_bridge
        and "substratePreviewToken <= 0L" not in substrate_coordinator
        and "attachmentGeneration <= 0" not in capture_jni
        and "substratePreviewToken <= 0" not in capture_jni,
        "V-25 must preserve raw uint64 token/generation bit patterns",
        errors,
    )
    require(
        "mFacade->captureFilePreviewRgba(" in endpoint_source
        and "controller.captureSubstratePreviewRgba(" in substrate_endpoint,
        "typed capture endpoints must execute the existing production renderers",
        errors,
    )
    require(
        "GenomePreviewUpdateGate" not in managed_endpoint_layouts,
        "V-25 managed endpoint layout must not reintroduce the deleted gate",
        errors,
    )


def verify_v26a_attachment_lifetime(errors: list[str]) -> None:
    controller_handle_path = REPO_ROOT / "native/jni/NativeControllerHandle.cpp"
    controller_handle = read(controller_handle_path)
    jni_sources = {
        path: read(path) for path in (REPO_ROOT / "native/jni").glob("*.cpp")
    }
    other_jni = "\n".join(
        source
        for path, source in jni_sources.items()
        if path != controller_handle_path
    )
    require(
        "reinterpret_cast<NativeControllerHandle*>" in controller_handle
        and "reinterpret_cast<NativeControllerHandle*>" not in other_jni,
        "V-26A controller wrapper pointer conversion must be isolated in "
        "NativeControllerHandle.cpp",
        errors,
    )
    require(
        re.search(
            r"reinterpret_cast\s*<\s*(?:const\s+)?NativeCellularityController\s*\*",
            "\n".join(jni_sources.values()),
        )
        is None,
        "V-26A JNI must never decode a Java long as a raw controller pointer",
        errors,
    )

    lease_header = read(
        REPO_ROOT / "native/app/render/NativeRenderAttachmentLease.h"
    )
    lease_source = read(
        REPO_ROOT / "native/app/render/NativeRenderAttachmentLease.cpp"
    )
    for token in (
        "FrameGuard",
        "InputBatchGuard",
        "ControllerRebindTicket",
        "beginControllerRebindAndDrain",
        "commitControllerRebind",
        "abortControllerRebind",
        "beginAttachmentCloseAndDrain",
    ):
        require(
            token in lease_header or token in lease_source,
            f"V-26A attachment lease is missing {token}",
            errors,
        )
    require(
        "ControllerRebindTicket::~ControllerRebindTicket()" in lease_source
        and "owner->lifecycle = AttachmentLifecycle::Open;" in lease_source,
        "V-26A must fail-open an abandoned controller-rebind ticket",
        errors,
    )

    gateway = read(SIMULATION_GATEWAY_PATH)
    runtime = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
        "NativeSimulationRuntime.kt"
    )
    facade = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/NativeSimulationFacade.java"
    )
    bridge = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/"
        "NativeCellularityControllerBridge.java"
    )
    microscope_backend = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
        "NativeMicroscopeSimulationBackend.kt"
    )
    for token in (
        "prepareReplacement",
        "commitReplacement",
        "detachForClose",
        "renderWriteLock.lock()",
        "runtimeAccessLock.lock()",
    ):
        require(
            token in gateway,
            f"V-26A gateway replacement path is missing {token}",
            errors,
        )
    require(
        gateway.index("renderWriteLock.lock()")
        < gateway.index("runtimeAccessLock.lock()"),
        "V-26A replacement lock order must be renderWrite then runtimeAccess",
        errors,
    )
    require(
        "if (issued == Long.MAX_VALUE) 0L else issued + 1L" in gateway,
        "V-26A managed surface tokens must fail closed before zero reuse",
        errors,
    )
    require(
        "prepareSubstrateReplacement" in runtime
        and "commitSubstrateReplacement" in runtime
        and "bridge = candidate" not in facade
        and "oldBridge.close()" not in facade,
        "V-26A reload must keep the durable bridge and use two-phase replacement",
        errors,
    )
    require(
        "private volatile double radius;" in bridge
        and "double candidateRadius = nativeRadius(candidateHandle);" in bridge
        and "radius = prepared.radius;" in bridge,
        "V-26A replacement must publish the committed controller radius",
        errors,
    )
    require(
        "microscopePublicationSourceGeneration" in facade
        and "microscopePublicationSourceGeneration = "
        "nextPublicationSourceGeneration;" in facade
        and "resetIfSourceChanged(" in microscope_backend,
        "V-26A replacement must reset managed native-publication cursors",
        errors,
    )
    microscope_jni = read(REPO_ROOT / "native/jni/NativeMicroscopeJni.cpp")
    require(
        "lease->requestInputResync(" in microscope_jni
        and "static_cast<std::uint64_t>(surfaceToken)" in microscope_jni,
        "V-26A input resync must validate the active attachment token",
        errors,
    )

    start_configuration = read(
        REPO_ROOT
        / "native/app/microscope/MicroscopeRuntimeStartConfiguration.h"
    )
    for field in (
        "temperatureMode",
        "toolOrdinal",
        "slowMotionSpeed",
        "incubateTargetUps",
        "lifecyclePaused",
        "performanceHudEnabled",
    ):
        require(
            field in start_configuration,
            f"V-26A runtime start configuration is missing {field}",
            errors,
        )


def verify_v26b_pass_through_cleanup(errors: list[str]) -> None:
    obsolete_paths = (
        "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "MicroscopeRenderer.java",
        "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "GenomePreviewRenderer.java",
        "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "DensityFieldPreviewRenderer.kt",
        "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
        "SessionMicroscopeSimulationBackend.kt",
        "app/src/test/java/com/cytarch/cellularity/platform/render/microscope/"
        "MicroscopeRendererTest.kt",
    )
    for relative_path in obsolete_paths:
        require(
            not (REPO_ROOT / relative_path).exists(),
            f"V-26B obsolete pass-through remains: {relative_path}",
            errors,
        )

    simulation_backend = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/simulation/"
        "MicroscopeSimulationBackend.kt"
    )
    for forbidden in (
        "draw(",
        "clearWithoutRendering",
        "onSurfaceCreated",
        "bindVulkanRenderSession",
        "submitMicroscopeInputBatch",
        "setToolOrdinal",
    ):
        require(
            forbidden not in simulation_backend,
            f"V-26B simulation backend still exposes render operation {forbidden}",
            errors,
        )

    endpoint = read(MICROSCOPE_RENDER_ENDPOINT_PATH)
    session_endpoint = read(SESSION_RENDER_ENDPOINT_PATH)
    endpoint_owner = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "NativeRenderEndpointOwner.kt"
    )
    endpoint_access = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/"
        "NativeMicroscopeRenderAccess.java"
    )
    bridge = read(MAIN_BRIDGE_PATH)
    facade = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/NativeSimulationFacade.java"
    )
    native_handle_header = read(
        REPO_ROOT / "native/jni/NativeRenderEndpointHandle.h"
    )
    native_handle_source = read(
        REPO_ROOT / "native/jni/NativeRenderEndpointHandle.cpp"
    )
    native_render_jni = read(REPO_ROOT / "native/jni/NativeRenderJni.cpp")
    native_microscope_jni = read(
        REPO_ROOT / "native/jni/NativeMicroscopeJni.cpp"
    )
    native_capture_jni = read(
        REPO_ROOT / "native/jni/NativePreviewCaptureJni.cpp"
    )
    lease_header = read(
        REPO_ROOT / "native/app/render/NativeRenderAttachmentLease.h"
    )
    lease_source = read(
        REPO_ROOT / "native/app/render/NativeRenderAttachmentLease.cpp"
    )
    android_view = read(ANDROID_MICROSCOPE_VIEW_PATH)
    worker = read(VULKAN_WORKER_PATH)
    all_managed_sources = "\n".join(
        read(path)
        for pattern in ("*.java", "*.kt")
        for path in (REPO_ROOT / "app/src/main/java").rglob(pattern)
    )

    for snippet in (
        "implements NativeMicroscopeRenderAccess, AutoCloseable",
        "private final NativeRenderEndpointOwner owner",
        "private volatile boolean closed",
        "if (surfaceAttached) return",
        "owner.close()",
    ):
        require(snippet in endpoint, f"V-26B durable endpoint is missing {snippet}", errors)
    for snippet in (
        "activeBorrowCount",
        "pendingCloseHandle",
        "fun borrow(): Borrow?",
        "nativeCloseAndDestroyEndpointHandle",
    ):
        require(snippet in endpoint_owner, f"V-26B managed endpoint owner is missing {snippet}", errors)
    for snippet in (
        "NativeMicroscopeRenderAccess",
        "private val render: NativeMicroscopeRenderAccess",
        "render.publishPlacementGenome",
        "render.submitInputBatch",
    ):
        require(
            snippet in endpoint_access + session_endpoint,
            f"V-26B typed session endpoint is missing {snippet}",
            errors,
        )
    require(
        "microscopeRenderEndpoint" in facade
        and "NativeMicroscopeRenderEndpoint" in facade,
        "V-26B facade must expose one durable render endpoint",
        errors,
    )
    require(
        "boolean committed = false;" in facade
        and "if (!committed) result.close();" in facade,
        "V-26B post-commit endpoint failure must release the retired controller",
        errors,
    )
    for forbidden in (
        "activeRenderCalls",
        "renderCallStateLock",
        "renderCallsClosing",
        "withPinnedRenderHandle",
        "surfaceBridge",
        "ensureMicroscopeSurfaceResources",
        "nativeResolveEndpointGeneration",
    ):
        require(
            forbidden not in bridge + facade + native_render_jni,
            f"V-26B removed bridge path returned: {forbidden}",
            errors,
        )
    require(
        "queueRenderTask" not in all_managed_sources
        and "Command.Task" not in worker
        and "drainTasksBeforeFrame" not in worker,
        "V-26B must not retain arbitrary managed render work",
        errors,
    )

    for snippet in (
        "NativeRenderEndpointHandle final",
        "std::shared_ptr<cellularity::app::NativeRenderAttachmentLease>",
        "std::shared_ptr<AndroidRenderAssetsLease>",
        "acquireAttachmentLease() const",
        "acquireAssetsLease() const",
    ):
        require(
            snippet in native_handle_header,
            f"V-26B native endpoint handle is missing {snippet}",
            errors,
        )
    require(
        native_handle_source.count("reinterpret_cast<NativeRenderEndpointHandle*>") == 1,
        "V-26B endpoint pointer decoding must stay isolated in its handle source",
        errors,
    )
    for source, label in (
        (native_render_jni, "render JNI"),
        (native_microscope_jni, "microscope JNI"),
        (native_capture_jni, "substrate-preview capture JNI"),
    ):
        require(
            "renderEndpointLeaseFromHandle" in source,
            f"V-26B {label} does not resolve the durable endpoint lease",
            errors,
        )
    require(
        "controllerSharedFromHandle" not in native_capture_jni
        and "bridge.enqueueSubstratePreviewCapture(\n                owner" in endpoint
        and "enqueueSubstratePreviewCapture(" not in facade,
        "V-26B substrate-preview capture must enter through the durable render endpoint",
        errors,
    )

    for snippet in (
        "publishPlacementGenome(",
        "acquirePlacementGenome(",
        "std::shared_ptr<const cellularity::GenomeData>",
        "placementGenomeRevision",
    ):
        require(
            snippet in lease_header + lease_source,
            f"V-26B immutable placement-genome state is missing {snippet}",
            errors,
        )
    for snippet in (
        "updatePlacementGenome(genome: GenomeDraft)",
        "endpoint.publishPlacementGenome(genome)",
        "placementGenomeRevision",
    ):
        require(
            snippet in android_view,
            f"V-26B explicit placement publication is missing {snippet}",
            errors,
        )
    require(
        "MicroscopeCellSource" not in all_managed_sources,
        "V-26B Tap path must not re-read or re-encode a cell source",
        errors,
    )

    lease_tests = read(
        REPO_ROOT / "native/tests/native_render_attachment_lease_tests.cpp"
    )
    input_tests = read(
        REPO_ROOT / "native/tests/microscope_input_batch_tests.cpp"
    )
    gateway_tests = read(
        REPO_ROOT
        / "app/src/test/java/com/cytarch/cellularity/platform/simulation/"
        "AndroidSimulationGatewayTest.kt"
    )
    for test_name, source in (
        ("testRenderEndpointHandleOwnsDurableSharedLeases", lease_tests),
        ("testNativeViewportInitializationAndSurfaceRecreation", lease_tests),
        ("testPlacementGenomeRevisionSurvivesControllerRebind", lease_tests),
        ("testStalePlacementGenomeRejectsOnlyTapRecord", input_tests),
        ("controlsReachDurableEndpointWithoutWaitingForSurfaceAttachment", gateway_tests),
    ):
        require(
            test_name in source,
            f"V-26B regression test is missing {test_name}",
            errors,
        )


def verify_v28_native_vulkan_worker(errors: list[str]) -> None:
    obsolete_paths = (
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameRenderThread.kt",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameNativeApi.kt",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanBackendSessionBridge.java",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanBackendDiagnosticsLayout.kt",
        "app/src/test/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameRenderThreadTest.kt",
        "native/jni/NativeVulkanBackendSessionHandle.h",
        "native/jni/NativeVulkanBackendSessionHandle.cpp",
        "native/jni/NativeVulkanBackendSessionJni.cpp",
    )
    for path in obsolete_paths:
        require(
            not (REPO_ROOT / path).exists(),
            f"V-28 transitional worker/session owner remains: {path}",
            errors,
        )

    required_paths = (
        "native/app/render/NativeRenderFrameSource.h",
        "native/app/render/NativeVulkanRenderWorker.h",
        "native/app/render/NativeVulkanRenderWorker.cpp",
        "native/app/render/NativeVulkanSurfaceLease.h",
        "native/app/render/NativeVulkanSurfaceLease.cpp",
        "native/app/render/NativeVulkanWorkerStatus.h",
        "native/app/render/NativeVulkanWorkerStatus.cpp",
        "native/app/render/NativeRenderProfilingState.h",
        "native/app/render/NativeRenderProfilingState.cpp",
        "native/render/backend/abi/VulkanBackendSessionFactory.h",
        "native/platform/android/AndroidVulkanBackendSessionFactory.cpp",
        "native/jni/NativeVulkanRenderWorkerJni.cpp",
        "native/jni/NativeRenderProfilingJni.cpp",
        "native/tests/native_vulkan_render_worker_tests.cpp",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanRenderWorkerBridge.kt",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanRenderWorkerJni.java",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeRenderWorkerCloseCoordinator.kt",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanWorkerStatusListener.kt",
        "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanFailureDiagnosticsReader.kt",
    )
    for path in required_paths:
        require((REPO_ROOT / path).is_file(), f"V-28 file is missing: {path}", errors)

    cmake = read(CMAKE_PATH)
    worker = read(VULKAN_WORKER_PATH)
    worker_header = read(
        REPO_ROOT / "native/app/render/NativeVulkanRenderWorker.h"
    )
    worker_jni = read(REPO_ROOT / "native/jni/NativeVulkanRenderWorkerJni.cpp")
    worker_jni_java = read(VULKAN_WORKER_JNI_PATH)
    bridge = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanRenderWorkerBridge.kt"
    )
    close_coordinator = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeRenderWorkerCloseCoordinator.kt"
    )
    listener = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanWorkerStatusListener.kt"
    )
    diagnostics_reader = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "NativeVulkanFailureDiagnosticsReader.kt"
    )
    surface_view = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameView.kt"
    )
    gateway = read(SIMULATION_GATEWAY_PATH)
    proguard = read(REPO_ROOT / "app/proguard-rules.pro")

    for snippet in (
        'setCurrentThreadName("CellularityVk")',
        "ownerThreadTrampoline() noexcept",
        "std::condition_variable mWake",
        "NativeVulkanSurfaceLease",
        "surfaceIntentRevision",
        "mDesired.frameRequested",
        "mDesired.retryPending",
        "failureDiagnosticsSnapshot()",
        "mWake.wait(lock",
    ):
        require(
            snippet in worker + worker_header,
            f"V-28 native worker contract is missing {snippet}",
            errors,
        )
    require(
        "wait_for(lock, std::chrono::milliseconds(8))" not in worker,
        "V-28 source rebind must wait for a native wake instead of polling",
        errors,
    )

    worker_target = re.search(
        r"add_library\(cellularity_render_worker\s+STATIC(?P<body>.*?)\n\s*\)",
        cmake,
        re.DOTALL,
    )
    require(worker_target is not None, "V-28 native worker CMake target is missing", errors)
    if worker_target is not None:
        for source in (
            "app/render/NativeRenderProfilingState.cpp",
            "app/render/NativeVulkanRenderWorker.cpp",
            "app/render/NativeVulkanSurfaceLease.cpp",
            "app/render/NativeVulkanWorkerStatus.cpp",
        ):
            require(
                source in worker_target.group("body"),
                f"V-28 worker target is missing {source}",
                errors,
            )
    worker_links = cmake_link_body(cmake, "cellularity_render_worker")
    require(
        "cellularity_vulkan_backend_client" in worker_links
        and "Threads::Threads" in worker_links,
        "V-28 worker target must use the host-testable backend client and Threads",
        errors,
    )
    require(
        "CELLULARITY_VULKAN_LIBRARY" not in worker_links
        and re.search(r"(?m)^\s*(?:android|log|GLESv3)\s*$", worker_links) is None,
        "V-28 worker core must not link Android, GLES, or the Vulkan loader",
        errors,
    )
    for source, source_set in (
        ("jni/NativeVulkanRenderWorkerJni.cpp", "CELLULARITY_JNI_SOURCES"),
        ("jni/NativeRenderProfilingJni.cpp", "CELLULARITY_JNI_SOURCES"),
        (
            "platform/android/AndroidVulkanBackendSessionFactory.cpp",
            "CELLULARITY_ANDROID_PLATFORM_SOURCES",
        ),
        (
            "app/render/sources/MicroscopeRenderFrameSource.cpp",
            "CELLULARITY_RENDERING_SOURCES",
        ),
        (
            "app/render/sources/GenomePreviewRenderFrameSource.cpp",
            "CELLULARITY_RENDERING_SOURCES",
        ),
        (
            "app/render/sources/EnvironmentFieldPreviewRenderFrameSource.cpp",
            "CELLULARITY_RENDERING_SOURCES",
        ),
        (
            "app/render/sources/VulkanDemoFrameProducer.cpp",
            "CELLULARITY_RENDERING_SOURCES",
        ),
    ):
        require(
            source in cmake_set(cmake, source_set) and cmake.count(source) == 1,
            f"V-28 source ownership is invalid for {source}",
            errors,
        )

    expected_worker_natives = {
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
    declared_worker_natives = set(
        re.findall(r"\bstatic\s+native\s+\S+\s+(native\w+)\s*\(", worker_jni_java)
    )
    require(
        declared_worker_natives == expected_worker_natives,
        "V-28 worker JNI declaration set differs: "
        f"missing={sorted(expected_worker_natives - declared_worker_natives)}, "
        f"unexpected={sorted(declared_worker_natives - expected_worker_natives)}",
        errors,
    )
    for method in expected_worker_natives:
        symbol = (
            "Java_com_cytarch_cellularity_platform_render_vulkan_"
            f"NativeVulkanRenderWorkerJni_{method}"
        )
        require(
            worker_jni.count(symbol) == 1,
            f"V-28 worker JNI must define {symbol} exactly once",
            errors,
        )

    for snippet, owner in (
        ("private val ingressLock = Any()", bridge),
        ("AtomicLong(initialHandle)", bridge),
        ("nativeRequestStop(current)", bridge),
        ("NativeRenderWorkerCloseCoordinator.closeAsync", bridge),
        ("nativeAwaitStopped", close_coordinator),
        ("nativeFinalizeWorkerBinding", close_coordinator),
        ("nativeDestroyStoppedWorker", close_coordinator),
        ("Long.compareUnsigned", listener),
        ("NativeVulkanFailureDiagnosticsReader", bridge),
        ("Dispatchers.IO", diagnostics_reader),
        ("bridge.readFailureDiagnostics", diagnostics_reader),
        ("Choreographer.FrameCallback", surface_view),
        ("beginVulkanWorkerBinding", gateway),
        ("completeVulkanWorkerUnbind", gateway),
    ):
        require(snippet in owner, f"V-28 managed handoff is missing {snippet}", errors)
    require(
        "Thread(" not in surface_view and "LinkedBlockingQueue" not in surface_view,
        "V-28 SurfaceView must not own a managed render loop",
        errors,
    )
    for snippet in (
        "NativeVulkanRenderWorkerJni { native <methods>; }",
        "NativeRenderProfilingBridge { native <methods>; }",
        "onNativeWorkerStatusSignal(long, long, int);",
    ):
        require(snippet in proguard, f"V-28 optimized JNI keep rule is missing {snippet}", errors)

    code_sources = "\n".join(
        read(path)
        for root in (REPO_ROOT / "app/src/main", REPO_ROOT / "native")
        for suffix in ("*.java", "*.kt", "*.h", "*.cpp")
        for path in root.rglob(suffix)
    )
    for obsolete_symbol in (
        "VulkanClearFrameRenderThread",
        "VulkanClearFrameNativeApi",
        "NativeVulkanBackendSessionBridge",
        "NativeVulkanBackendSessionHandle",
        "bindVulkanRenderSession",
        "unbindVulkanRenderSession",
    ):
        require(
            obsolete_symbol not in code_sources,
            f"V-28 obsolete production symbol remains: {obsolete_symbol}",
            errors,
        )


def verify_v30c_genome_preview_reveal(errors: list[str]) -> None:
    native_layout = read(
        REPO_ROOT
        / "native/app/genome_preview/GenomePreviewEndpointLayouts.h"
    )
    managed_layout = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "GenomePreviewEndpointLayouts.kt"
    )
    endpoint_header = read(
        REPO_ROOT / "native/app/genome_preview/GenomePreviewEndpoint.h"
    )
    endpoint_source = read(
        REPO_ROOT / "native/app/genome_preview/GenomePreviewEndpoint.cpp"
    )
    render_jni = read(REPO_ROOT / "native/jni/NativeRenderJni.cpp")
    bridge = read(MAIN_BRIDGE_PATH)
    genome_view = read(GENOME_PREVIEW_VIEW_PATH)
    reveal = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
        "genome_editor/GenomePreviewRevealCoordinator.kt"
    )
    visual = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
        "genome_editor/GenomePreviewVisualContract.kt"
    )
    surface_host = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "OnDemandRenderSurfaceHost.kt"
    )
    vulkan_view = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/vulkan/"
        "VulkanClearFrameView.kt"
    )
    reveal_tests = read(
        REPO_ROOT
        / "app/src/test/java/com/cytarch/cellularity/ui/play_menu/screens/"
        "genome_editor/GenomePreviewRevealCoordinatorTest.kt"
    )
    surface_tests = read(
        REPO_ROOT / "native/tests/render_surface_session_tests.cpp"
    )
    retained_state = read(
        REPO_ROOT
        / "native/app/genome_preview/GenomePreviewRetainedState.cpp"
    )
    vulkan_session = read(
        REPO_ROOT
        / "native/render/backend/vulkan/AndroidVulkanClearFrameSession.cpp"
    )
    genome_frame_source = read(
        REPO_ROOT
        / "native/app/render/sources/GenomePreviewRenderFrameSource.cpp"
    )
    genome_screen = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
        "genome_editor/GenomeEditorScreen.kt"
    )
    play_content = read(
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayContent.kt"
    )
    play_screen = read(
        REPO_ROOT
        / "shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/play/"
        "PlayScreen.kt"
    )
    play_tests = read(
        REPO_ROOT
        / "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/play/"
        "PlayStoreTest.kt"
    )

    for source, snippets in (
        (
            native_layout,
            (
                "kGenomePreviewAcknowledgmentMagic = 0x32414647U",
                "kGenomePreviewAcknowledgmentVersion = 2",
                "kGenomePreviewAcknowledgmentSize = 112",
            ),
        ),
        (
            managed_layout,
            (
                "MAGIC = 0x32414647",
                "VERSION = 2",
                "SIZE_BYTES = 112",
                "presentedContentRevision",
                "presentedViewRevision",
                "presentedInteractionToken",
            ),
        ),
        (
            endpoint_header,
            (
                "presentedContentRevision",
                "presentedViewRevision",
                "presentedInteractionToken",
                "mAppliedInteractionToken",
            ),
        ),
    ):
        for snippet in snippets:
            require(
                snippet in source,
                f"V-30C GFA2 contract is missing {snippet}",
                errors,
            )

    require(
        "isPrimaryContentPresented(result)" in endpoint_source
        and "mAcknowledgment.presentedContentRevision =" in endpoint_source
        and "mAcknowledgment.presentedViewRevision =" in endpoint_source
        and "mAcknowledgment.presentedInteractionToken =" in endpoint_source,
        "V-30C must publish one presented tuple only after primary presentation",
        errors,
    )
    require(
        endpoint_source.count("mAcknowledgment.presentedContentRevision = 0")
        >= 3,
        "V-30C must invalidate presented state on attach, detach and close",
        errors,
    )

    read_method = "nativeReadGenomePreviewAcknowledgment"
    require(
        len(re.findall(rf"\b{read_method}\b", bridge)) == 2
        and re.search(
            rf"private\s+static\s+native\s+boolean\s+{read_method}\(\s*long\s+endpointHandle,\s*long\s+endpointGeneration,\s*ByteBuffer\s+acknowledgmentBuffer\s*\)",
            bridge,
            re.MULTILINE,
        )
        is not None,
        "V-30C typed acknowledgment read declaration is missing or duplicated",
        errors,
    )
    read_symbol = MAIN_JNI_PREFIX + read_method
    require(
        render_jni.count(read_symbol) == 1,
        "V-30C typed acknowledgment read JNI must have exactly one owner",
        errors,
    )
    read_start = render_jni.index(read_symbol)
    read_end = render_jni.index('extern "C" JNIEXPORT', read_start + 1)
    read_body = render_jni[read_start:read_end]
    require(
        "acquireGenomePreviewEndpoint(" in read_body
        and "readAcknowledgment()" in read_body
        and "controllerSharedFromHandle" not in read_body,
        "V-30C acknowledgment read must use only the typed preview endpoint",
        errors,
    )

    for snippet in (
        "GenomePreviewRevealCoordinator",
        "renderSurfaceAvailable",
        "readAcknowledgmentForReveal",
        "previewSession.readAcknowledgment(acknowledgment)",
        "requestFrameWithPresentationTracking()",
        "initialPreview?.let(::update)",
        "restoreViewport(initialViewport)",
    ):
        require(
            snippet in genome_view,
            f"V-30C managed reveal gate is missing {snippet}",
            errors,
        )
    for snippet in (
        "requestedViewportWidth",
        "requestedViewportHeight",
        "updateLogicalViewport",
        "updateBackingSurfaceSize",
        "LayoutParams(backingSurfaceWidth, backingSurfaceHeight)",
        "backendKind == RenderBackendKind.VULKAN",
    ):
        require(
            snippet in genome_view,
            f"V-30C stable backing Surface is missing {snippet}",
            errors,
        )
    for snippet in (
        "viewportWidth <= 0",
        "viewportHeight <= 0",
        "left.viewportWidth == right.viewportWidth",
        "left.viewportHeight == right.viewportHeight",
    ):
        require(
            snippet in retained_state,
            f"V-30C logical viewport contract is missing {snippet}",
            errors,
        )
    for snippet in (
        "internal data class GenomePreviewSurfaceGeometry(",
        "internal fun genomePreviewSurfaceGeometry(",
        "viewportWidth = previewSurfaceGeometry.viewportWidth",
        "viewportHeight = previewSurfaceGeometry.viewportHeight",
        "backingSurfaceWidth = previewSurfaceGeometry.backingWidth",
        "backingSurfaceHeight = previewSurfaceGeometry.backingHeight",
        "view.updateLogicalViewport(viewportWidth, viewportHeight)",
        "view.updateBackingSurfaceSize(",
    ):
        require(
            snippet in genome_screen,
            f"V-30C editor surface geometry handoff is missing {snippet}",
            errors,
        )
    require(
        genome_screen.count(
            "val previewSurfaceGeometry = genomePreviewSurfaceGeometry("
        )
        == 2
        and "viewportWidth = visibleWidthPx.roundToInt().coerceAtLeast(1)"
        in genome_screen
        and "backingWidth = parentWidthPx.roundToInt().coerceAtLeast(1)"
        in genome_screen,
        "V-30C portrait and landscape layouts must share one non-zero stable "
        "backing geometry contract",
        errors,
    )
    require(
        "const VkExtent2D presentationExtent" in vulkan_session
        and "topLeftIdentityOffset(" in vulkan_session
        and "const VkOffset2D presentationOffset" in vulkan_session
        and "const VkRect2D scissor{presentationOffset, presentationExtent}"
        in vulkan_session
        and "{{0, 0}, targetExtent}" in vulkan_session
        and "Genome Preview viewport exceeds its backing Surface" in vulkan_session,
        "V-30C Vulkan preview must clear the stable target and draw only the "
        "logical viewport",
        errors,
    )
    require(
        "setViewportSurfaceSize(width, height)" not in genome_frame_source,
        "V-30C Vulkan surface lifecycle must not overwrite the retained "
        "Genome Preview viewport with backing dimensions",
        errors,
    )
    for snippet in (
        "retainGenomeContent",
        "RetainedOffscreenContent(",
        "playContentVisibility(",
        "genomeRequested && !genomeContentReady",
        "placeable.placeRelative(",
    ):
        require(
            snippet in play_screen,
            f"V-30C retained tab presentation is missing {snippet}",
            errors,
        )
    require(
        "var genomePreviewReady by remember(microscopeSurfaceFactory)" in play_content
        and "genomeContentReady = genomePreviewReady" in play_content
        and "onPreviewReadyChanged = { genomePreviewReady = it }" in play_content
        and "playContentVisibility(PlayTab.GENOME, genomeContentReady = false)"
        in play_tests
        and "playContentVisibility(PlayTab.GENOME, genomeContentReady = true)"
        in play_tests,
        "V-30C must keep the microscope visible until retained Genome content is current",
        errors,
    )
    require(
        "fun currentViewportState(): FloatArray?" in genome_view
        and "backendKind == RenderBackendKind.VULKAN" in genome_view
        and "retainViewport(decodeAcknowledgment(), notify = false)" in genome_view
        and "viewportRetentionAcceptsOnlyAValidNativeAcknowledgment"
        in reveal_tests,
        "V-30C must retain the native Vulkan viewport once at disposal",
        errors,
    )
    capture_admission = genome_view[
        genome_view.index("isGenomePreviewCaptureAvailable("):
        genome_view.index("isGenomePreviewCaptureAvailable(") + 400
    ]
    require(
        "previewCover" not in capture_admission,
        "V-30C capture admission must be independent from cover visibility",
        errors,
    )
    require(
        "java.lang.Long.compareUnsigned" in reveal
        and "DEFAULT_MAXIMUM_RETRY_COUNT = 8" in reveal
        and "attachmentGeneration != 0L" in reveal,
        "V-30C reveal gate must reject stale tuples with bounded retries",
        errors,
    )
    require(
        "BACKGROUND_ARGB: Int = -14671840" in visual
        and "COVER_CLICKABLE = false" in visual
        and "COVER_ENABLED = false" in visual,
        "V-30C cover must use the stable non-interactive #202020 contract",
        errors,
    )
    require(
        surface_host.count("requestFrameWithPresentationTracking") >= 3
        and "fun requestFrameWithPresentationTracking()" in vulkan_view
        and "forceFrameRequest = true" in vulkan_view,
        "V-30C must connect state targets to Vulkan presentation tracking",
        errors,
    )
    require(
        "repeat(100)" in reveal_tests
        and "stalePresentationRetriesAreBoundedAndFailClosed" in reveal_tests
        and "testGenomePreviewAcknowledgesOnlyPresentedCurrentState"
        in surface_tests
        and "Offscreen capture never advances onscreen presentation state"
        in surface_tests,
        "V-30C lifecycle and native presentation regression coverage is incomplete",
        errors,
    )
    for document in (read(BASELINE_PATH), read(COMMAND_MIGRATION_MAP_PATH)):
        require(
            "GFA1" not in document and "GFA2" in document,
            "V-30C documents must describe only the GFA2 acknowledgment",
            errors,
        )


def find_android_dexdump() -> Path | None:
    executable = shutil.which("dexdump")
    if executable is not None:
        return Path(executable)

    sdk_roots: list[Path] = []
    for variable in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        value = os.environ.get(variable)
        if value:
            sdk_roots.append(Path(value))

    local_properties = REPO_ROOT / "local.properties"
    if local_properties.is_file():
        for line in local_properties.read_text(encoding="utf-8").splitlines():
            if line.startswith("sdk.dir="):
                sdk_roots.append(Path(line.removeprefix("sdk.dir=")))
                break

    candidates: list[Path] = []
    for sdk_root in sdk_roots:
        candidates.extend((sdk_root / "build-tools").glob("*/dexdump"))
    return max(candidates, default=None, key=lambda path: path.parent.name)


def verify_preview_capture_dex_contract(
    dex_payloads: dict[str, bytes], errors: list[str]
) -> None:
    dexdump = find_android_dexdump()
    if dexdump is None:
        errors.append("dexdump is unavailable for optimized capture ABI verification")
        return

    class_descriptor = (
        "Lcom/cytarch/cellularity/platform/render/preview/"
        "NativePreviewCaptureBridge;"
    )
    class_blocks: list[str] = []
    with tempfile.TemporaryDirectory(prefix="cellularity-v25-dex-") as directory:
        for entry, payload in dex_payloads.items():
            dex_path = Path(directory) / entry
            dex_path.write_bytes(payload)
            try:
                dump = subprocess.check_output(
                    [str(dexdump), str(dex_path)],
                    text=True,
                    stderr=subprocess.STDOUT,
                )
            except (OSError, subprocess.CalledProcessError) as error:
                errors.append(f"could not inspect {entry} with dexdump: {error}")
                continue

            position = dump.find(f"Class descriptor  : '{class_descriptor}'")
            if position < 0:
                continue
            start = dump.rfind("Class #", 0, position)
            end = dump.find("Class #", position)
            class_blocks.append(dump[start : end if end >= 0 else None])

    require(
        len(class_blocks) == 1,
        "optimized DEX must contain exactly one NativePreviewCaptureBridge class",
        errors,
    )
    if len(class_blocks) != 1:
        return

    native_methods = re.findall(
        r"name\s+: '([^']+)'\s+type\s+: '([^']+)'\s+"
        r"access\s+: [^\n]*\bNATIVE\b",
        class_blocks[0],
    )
    expected_methods = {
        ("nativeEnqueueGenome", "(JJ[BDLjava/nio/ByteBuffer;)V"),
        ("nativeEnqueueSubstrate", "(JJJJLjava/nio/ByteBuffer;)V"),
        ("nativeAwait", "(JJLjava/nio/ByteBuffer;)[B"),
        ("nativeCancel", "(J)I"),
        ("nativeDestroy", "(J)V"),
    }
    require(
        set(native_methods) == expected_methods
        and len(native_methods) == len(expected_methods),
        "NativePreviewCaptureBridge optimized DEX must retain exactly the five "
        f"fixed native descriptors; found {native_methods!r}",
        errors,
    )


def verify_optimized_apk(
    path: Path, errors: list[str], require_profiler_stripped: bool
) -> None:
    if not path.is_file():
        errors.append(f"release APK does not exist: {path}")
        return
    try:
        with zipfile.ZipFile(path) as apk:
            dex_entries = sorted(
                name
                for name in apk.namelist()
                if re.fullmatch(r"classes(?:\d+)?\.dex", name)
            )
            require(bool(dex_entries), f"{path} contains no DEX payload", errors)
            dex_payloads = {name: apk.read(name) for name in dex_entries}
            dex_payload = b"".join(dex_payloads.values())
            native_entries = sorted(
                name
                for name in apk.namelist()
                if re.fullmatch(r"lib/[^/]+/libcellularity\.so", name)
            )
            native_payloads = {
                name: apk.read(name) for name in native_entries
            }
    except (OSError, zipfile.BadZipFile, KeyError) as error:
        errors.append(f"could not inspect release APK {path}: {error}")
        return

    if require_profiler_stripped:
        for marker in (
            b"boundary_schema=1",
            b"boundary_jni_total_count=",
            b"art.gc.bytes-allocated",
            b"_input_to_native_accept",
            b"_native_accept_to_present",
        ):
            require(
                marker not in dex_payload,
                "release APK retains V-19 profiler marker "
                f"{marker.decode('ascii')!r}",
                errors,
            )
    for marker in (
        b"EnvironmentFieldPreviewBridge",
        b"nativePublishEnvironmentFieldPreviewState",
        b"NativePreviewCaptureBridge",
        b"nativeEnqueueGenome",
        b"nativeEnqueueSubstrate",
        b"nativeAwait",
        b"nativeCancel",
        b"nativeDestroy",
    ):
        require(
            marker in dex_payload,
            f"release DEX is missing retained field-preview contract marker {marker!r}",
            errors,
        )
    require(
        b"nativeResolveEndpointGeneration" not in dex_payload,
        "release DEX retains the removed field-preview generation resolver",
        errors,
    )
    verify_preview_capture_dex_contract(dex_payloads, errors)

    require(
        bool(native_payloads),
        f"{path} contains no main native libraries",
        errors,
    )
    nm = shutil.which("nm")
    if nm is None:
        errors.append("nm is unavailable for release field-preview JNI verification")
        return
    expected_symbols = {
        FIELD_PREVIEW_JNI_PREFIX + "nativePublishEnvironmentFieldPreviewState",
        PREVIEW_CAPTURE_JNI_PREFIX + "nativeEnqueueGenome",
        PREVIEW_CAPTURE_JNI_PREFIX + "nativeEnqueueSubstrate",
        PREVIEW_CAPTURE_JNI_PREFIX + "nativeAwait",
        PREVIEW_CAPTURE_JNI_PREFIX + "nativeCancel",
        PREVIEW_CAPTURE_JNI_PREFIX + "nativeDestroy",
    }
    with tempfile.TemporaryDirectory(prefix="cellularity-v22-") as directory:
        for entry, payload in native_payloads.items():
            library = Path(directory) / Path(entry).parent.name
            library.write_bytes(payload)
            try:
                symbols = subprocess.check_output(
                    [nm, "-D", str(library)], text=True, stderr=subprocess.STDOUT
                )
            except (OSError, subprocess.CalledProcessError) as error:
                errors.append(f"could not inspect {entry}: {error}")
                continue
            for symbol in expected_symbols:
                require(
                    len(re.findall(rf"\b{re.escape(symbol)}$", symbols, re.MULTILINE))
                    == 1,
                    f"{entry} must export {symbol} exactly once",
                    errors,
                )
            require(
                "Java_com_cytarch_cellularity_NativeCellularityControllerBridge_"
                "nativePublishEnvironmentFieldPreviewState" not in symbols,
                f"{entry} exports the field publish method from the wrong owner",
                errors,
            )
            for obsolete_name in (
                "nativeCaptureGenomeFilePreviewRgba",
                "nativeCaptureSubstratePreviewRgba",
            ):
                require(
                    obsolete_name not in symbols,
                    f"{entry} exports obsolete synchronous capture {obsolete_name}",
                    errors,
                )


def main() -> int:
    arguments = parse_arguments()
    errors: list[str] = []
    try:
        verify_entry_points(errors)
        verify_source_ownership(errors)
        verify_profiling_gate(errors)
        verify_baseline_document(errors)
        verify_characterization_tests(errors)
        verify_v21_decoupling(errors)
        verify_v22_field_preview_boundary(errors)
        verify_v23_typed_render_command_infrastructure(errors)
        verify_v24_microscope_input_batch(errors)
        verify_v25_native_preview_endpoint(errors)
        verify_v26a_attachment_lifetime(errors)
        verify_v26b_pass_through_cleanup(errors)
        verify_v28_native_vulkan_worker(errors)
        verify_v30c_genome_preview_reveal(errors)
        if arguments.release_apk is not None:
            verify_optimized_apk(
                arguments.release_apk, errors, require_profiler_stripped=True
            )
        elif arguments.optimized_apk is not None:
            verify_optimized_apk(
                arguments.optimized_apk, errors, require_profiler_stripped=False
            )
    except VerificationError as error:
        errors.append(str(error))

    if errors:
        for error in errors:
            print(f"native render boundary verification failed: {error}", file=sys.stderr)
        return 1

    apk_suffix = ""
    if arguments.release_apk is not None:
        apk_suffix = f", release stripped={arguments.release_apk}"
    elif arguments.optimized_apk is not None:
        apk_suffix = f", optimized ABI/R8={arguments.optimized_apk}"
    print(
        "native render boundary verified: "
        f"{len(FRAME_BOUNDARY_ENTRY_POINTS)} frame JNI entries, "
        f"{sum(len(value) for value in EXPECTED_SOURCE_OWNERSHIP.values()) + 1} "
        "native source owners, "
        f"{len(EXPECTED_SCENARIOS)} baseline scenarios"
        f"{apk_suffix}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
