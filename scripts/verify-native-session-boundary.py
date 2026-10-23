#!/usr/bin/env python3
"""Verify ownership boundaries between native world, interaction, and rendering."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
NATIVE_ROOT = REPO_ROOT / "native"
APP_ROOT = NATIVE_ROOT / "app"
CMAKE_PATH = NATIVE_ROOT / "CMakeLists.txt"
NATIVE_BUILD_SCRIPT = REPO_ROOT / "scripts/native/build.sh"
RENDER_FRAME_BUILDER_HEADER = NATIVE_ROOT / "render/frame/RenderFrameBuilder.h"
RENDER_FRAME_BUILDER_SOURCE = NATIVE_ROOT / "render/frame/RenderFrameBuilder.cpp"
CELL_BOUNDARY_MESH_BUILDER_HEADER = (
    NATIVE_ROOT / "render/frame/CellBoundaryMeshBuilder.h"
)
CELL_BOUNDARY_MESH_BUILDER_SOURCE = (
    NATIVE_ROOT / "render/frame/CellBoundaryMeshBuilder.cpp"
)
RENDER_LINK_GEOMETRY_BUILDER_HEADER = (
    NATIVE_ROOT / "render/frame/RenderLinkGeometryBuilder.h"
)
RENDER_LINK_GEOMETRY_BUILDER_SOURCE = (
    NATIVE_ROOT / "render/frame/RenderLinkGeometryBuilder.cpp"
)
RENDER_CONTACT_CHORD_RESOLVER_HEADER = (
    NATIVE_ROOT / "render/frame/RenderContactChordResolver.h"
)
RENDER_CONTACT_CHORD_RESOLVER_SOURCE = (
    NATIVE_ROOT / "render/frame/RenderContactChordResolver.cpp"
)
RENDER_FRAME_BUILDER_TEST = NATIVE_ROOT / "tests/render_frame_builder_tests.cpp"
LINK_RENDER_CONTRACT_TEST = NATIVE_ROOT / "tests/link_render_contract_tests.cpp"
BOUNDARY_LINK_CAPTURE_TEST = NATIVE_ROOT / "tests/boundary_link_capture_tests.cpp"
LINK_GEOMETRY_BUILDER_TEST = NATIVE_ROOT / "tests/link_geometry_builder_tests.cpp"
RENDER_SOURCE_FRAME_HEADER = NATIVE_ROOT / "render/frame/RenderSourceFrame.h"
RENDER_SOURCE_FRAME_SOURCE = NATIVE_ROOT / "render/frame/RenderSourceFrame.cpp"
RENDER_SOURCE_CAPTURE_HEADER = NATIVE_ROOT / "render/frame/RenderSourceCapture.h"
RENDER_SOURCE_CAPTURE_SOURCE = NATIVE_ROOT / "render/frame/RenderSourceCapture.cpp"
RENDER_FRAME_TYPES_HEADER = NATIVE_ROOT / "render/frame/RenderFrameTypes.h"
RENDER_FRAME_EXCHANGE_HEADER = NATIVE_ROOT / "render/frame/RenderFrameExchange.h"
RENDER_FRAME_EXCHANGE_SOURCE = NATIVE_ROOT / "render/frame/RenderFrameExchange.cpp"
RENDER_FRAME_EXCHANGE_TEST = NATIVE_ROOT / "tests/render_frame_exchange_tests.cpp"
CELL_DATA_HEADER = NATIVE_ROOT / "core/CellData.h"
CELL_DATA_SOURCE = NATIVE_ROOT / "core/CellData.cpp"
SIMULATION_WORLD_HEADER = NATIVE_ROOT / "core/SimulationWorld.h"
SIMULATION_WORLD_SOURCE = NATIVE_ROOT / "core/SimulationWorld.cpp"
RENDER_CONTACT_COLLECTOR_HEADER = (
    NATIVE_ROOT / "render/frame/RenderContactCollector.h"
)
RENDER_CONTACT_COLLECTOR_SOURCE = (
    NATIVE_ROOT / "render/frame/RenderContactCollector.cpp"
)
RENDER_CONTACT_CHORD_BUFFER_HEADER = (
    NATIVE_ROOT / "render/frame/RenderContactChordBuffer.h"
)
RENDER_CONTACT_CHORD_BUFFER_SOURCE = (
    NATIVE_ROOT / "render/frame/RenderContactChordBuffer.cpp"
)
RENDER_CONTACT_LIMITS_HEADER = NATIVE_ROOT / "render/frame/RenderContactLimits.h"
RELEASE_SUBSTRATE_CODEC_SOURCE = (
    NATIVE_ROOT / "formats/release/ReleaseSubstrateCodec.cpp"
)
LEGACY_SUBSTRATE_CODEC_SOURCE = (
    NATIVE_ROOT / "formats/LegacySubstrateCodec.cpp"
)
MICROSCOPE_FRAME_PIPELINE_HEADER = (
    APP_ROOT / "microscope/render/MicroscopeRenderFramePipeline.h"
)
MICROSCOPE_FRAME_PIPELINE_SOURCE = (
    APP_ROOT / "microscope/render/MicroscopeRenderFramePipeline.cpp"
)
MICROSCOPE_FRAME_PIPELINE_TEST = (
    NATIVE_ROOT / "tests/microscope_render_frame_pipeline_tests.cpp"
)
MICROSCOPE_FRAME_PIPELINE_TEST_ACCESS = (
    NATIVE_ROOT / "tests/support/MicroscopeRenderFramePipelineTestAccess.h"
)
NATIVE_MICROSCOPE_RENDERER_SOURCE = (
    APP_ROOT / "microscope/render/MicroscopeRenderer.cpp"
)
MICROSCOPE_DEBUG_HUD_HEADER = (
    APP_ROOT / "microscope/render/MicroscopeDebugHud.h"
)
MICROSCOPE_DEBUG_HUD_SOURCE = (
    APP_ROOT / "microscope/render/MicroscopeDebugHud.cpp"
)
NATIVE_CONTROLLER_SOURCE = APP_ROOT / "NativeCellularityController.cpp"
INPUT_TIMELINE_HEADER = APP_ROOT / "microscope/MicroscopeInputTimeline.h"
INPUT_TIMELINE_SOURCE = APP_ROOT / "microscope/MicroscopeInputTimeline.cpp"
SIMULATION_OWNER_HEADER = APP_ROOT / "NativeSimulationOwner.h"
SIMULATION_OWNER_SOURCE = APP_ROOT / "NativeSimulationOwner.cpp"
SIMULATION_STATE_HEADER = APP_ROOT / "microscope/MicroscopeSimulationState.h"
SIMULATION_STATE_SOURCE = APP_ROOT / "microscope/MicroscopeSimulationState.cpp"
TEMPERATURE_MODE_HEADER = APP_ROOT / "microscope/MicroscopeTemperatureMode.h"
MICROSCOPE_FRAME_CONTROLLER_HEADER = (
    APP_ROOT / "microscope/MicroscopeFrameController.h"
)
MICROSCOPE_FRAME_CONTROLLER_SOURCE = (
    APP_ROOT / "microscope/MicroscopeFrameController.cpp"
)
WORLD_INTERACTION_HEADER = (
    APP_ROOT / "microscope/interaction/MicroscopeWorldInteractionState.h"
)
WORLD_INTERACTION_SOURCE = (
    APP_ROOT / "microscope/interaction/MicroscopeWorldInteractionState.cpp"
)
SIMULATION_SCHEDULER_HEADER = APP_ROOT / "simulation/SimulationScheduler.h"
SIMULATION_SCHEDULER_SOURCE = APP_ROOT / "simulation/SimulationScheduler.cpp"
SIMULATION_SCHEDULER_TEST = NATIVE_ROOT / "tests/simulation_scheduler_tests.cpp"
SIMULATION_OWNER_TEST = NATIVE_ROOT / "tests/native_simulation_owner_tests.cpp"
SIMULATION_EXECUTION_CONTEXT_HEADER = (
    NATIVE_ROOT / "execution/SimulationExecutionContext.h"
)
SIMULATION_EXECUTION_CONTEXT_SOURCE = (
    NATIVE_ROOT / "execution/SimulationExecutionContext.cpp"
)
FLOATING_POINT_ENVIRONMENT_HEADER = (
    NATIVE_ROOT / "execution/FloatingPointEnvironment.h"
)
FLOATING_POINT_ENVIRONMENT_SOURCE = (
    NATIVE_ROOT / "execution/FloatingPointEnvironment.cpp"
)
SIMULATION_EXECUTION_CONTEXT_TEST = (
    NATIVE_ROOT / "tests/simulation_execution_context_tests.cpp"
)
LATEST_VALUE_EXCHANGE_HEADER = NATIVE_ROOT / "concurrency/LatestValueExchange.h"
MICROSCOPE_RUNTIME_HEADER = APP_ROOT / "microscope/NativeMicroscopeRuntime.h"
MICROSCOPE_RUNTIME_SOURCE = APP_ROOT / "microscope/NativeMicroscopeRuntime.cpp"
MICROSCOPE_RUNTIME_PUBLICATION_CONTRACT_HEADER = (
    APP_ROOT / "microscope/MicroscopeRuntimePublicationContract.h"
)
MICROSCOPE_JNI_SOURCE = NATIVE_ROOT / "jni/NativeMicroscopeJni.cpp"
MICROSCOPE_COMMAND_MAILBOX_HEADER = (
    APP_ROOT / "microscope/MicroscopeCommandMailbox.h"
)
MICROSCOPE_COMMAND_MAILBOX_SOURCE = (
    APP_ROOT / "microscope/MicroscopeCommandMailbox.cpp"
)
MICROSCOPE_PRESENTATION_STATE_HEADER = (
    APP_ROOT / "microscope/MicroscopePresentationState.h"
)
MICROSCOPE_PRESENTATION_STATE_SOURCE = (
    APP_ROOT / "microscope/MicroscopePresentationState.cpp"
)
MICROSCOPE_COMMAND_MAILBOX_TEST = (
    NATIVE_ROOT / "tests/microscope_command_mailbox_tests.cpp"
)
GPU_RENDERER_HEADER = NATIVE_ROOT / "render/SimulationWorldGpuRenderer.h"
GPU_RENDERER_SOURCE = NATIVE_ROOT / "render/SimulationWorldGpuRenderer.cpp"
RENDER_BACKEND_KIND_HEADER = (
    NATIVE_ROOT / "render/backend/RenderBackendKind.h"
)
RENDER_SURFACE_SESSION_HEADER = (
    NATIVE_ROOT / "render/backend/RenderSurfaceSession.h"
)
GLES_SURFACE_SESSION_HEADER = (
    NATIVE_ROOT / "render/backend/gles/GlesRenderSurfaceSession.h"
)
GLES_SURFACE_SESSION_SOURCE = (
    NATIVE_ROOT / "render/backend/gles/GlesRenderSurfaceSession.cpp"
)
RENDER_SURFACE_SESSION_TEST = (
    NATIVE_ROOT / "tests/render_surface_session_tests.cpp"
)
PREVIEW_FACADE_HEADER = APP_ROOT / "NativeGenomePreviewFacade.h"
PREVIEW_FACADE_SOURCE = APP_ROOT / "NativeGenomePreviewFacade.cpp"
PREVIEW_CONTROLLER_HEADER = APP_ROOT / "genome_preview/GenomePreviewController.h"
PREVIEW_CONTROLLER_SOURCE = APP_ROOT / "genome_preview/GenomePreviewController.cpp"
PREVIEW_GHOST_HEADER = APP_ROOT / "genome_preview/ghost/GenomePreviewGhostController.h"
ANDROID_PREVIEW_RENDERER = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/"
    "genome_editor/GenomePreviewView.kt"
)
ANDROID_MICROSCOPE_RENDERER = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/AndroidMicroscopeView.kt"
)
ANDROID_MICROSCOPE_HOST_VIEW = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/AndroidMicroscopeView.kt"
)
ANDROID_MICROSCOPE_SURFACE_CONTRACTS = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/MicroscopeSurfaceContracts.kt"
)
ANDROID_MICROSCOPE_RENDER_SURFACE_HOST = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/MicroscopeRenderSurfaceHost.kt"
)
ANDROID_GLES_MICROSCOPE_SURFACE = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/GlesMicroscopeSurfaceView.kt"
)
ANDROID_MICROSCOPE_PRESENTATION_POLICY = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/MicroscopePresentationPolicy.kt"
)
ANDROID_MICROSCOPE_PRESENTATION_STATE = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/MicroscopePresentationStateMachine.kt"
)
ANDROID_MICROSCOPE_SCREEN = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope/MicroscopeScreen.kt"
)
ANDROID_MICROSCOPE_PRESENTATION_STATE_TEST = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/render/microscope/MicroscopePresentationStateMachineTest.kt"
)
ANDROID_MICROSCOPE_STATUS_POLLER = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/microscope/MicroscopeStatusPoller.kt"
)
ANDROID_MICROSCOPE_STATUS_POLLER_TEST = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/render/microscope/MicroscopeStatusPollerTest.kt"
)
ANDROID_MICROSCOPE_PUBLICATION_SOURCE = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/simulation/MicroscopeRuntimePublicationSource.kt"
)
ANDROID_SIMULATION_GATEWAY_TEST = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/simulation/AndroidSimulationGatewayTest.kt"
)
ANDROID_MICROSCOPE_BACKEND = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/simulation/NativeMicroscopeSimulationBackend.kt"
)
ANDROID_MICROSCOPE_TELEMETRY = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/simulation/MicroscopeRuntimeTelemetry.kt"
)
ANDROID_RUNTIME_PUBLICATION = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/NativeMicroscopeRuntimePublication.java"
)
ANDROID_MICROSCOPE_TELEMETRY_TEST = (
    REPO_ROOT
    / "app/src/test/java/com/cytarch/cellularity/platform/simulation/MicroscopeRuntimeTelemetryTest.kt"
)

REQUIRED_FILES = (
    APP_ROOT / "NativeWorldSession.h",
    APP_ROOT / "NativeWorldSession.cpp",
    APP_ROOT / "NativeSimulationStepper.h",
    APP_ROOT / "NativeSimulationStepper.cpp",
    SIMULATION_OWNER_HEADER,
    SIMULATION_OWNER_SOURCE,
    APP_ROOT / "NativeInteractionController.h",
    APP_ROOT / "NativeInteractionController.cpp",
    APP_ROOT / "NativeRenderSession.h",
    APP_ROOT / "NativeRenderSession.cpp",
    APP_ROOT / "NativeWorldContext.h",
    APP_ROOT / "interaction/NativeViewport.h",
    APP_ROOT / "interaction/NativeViewport.cpp",
    INPUT_TIMELINE_HEADER,
    INPUT_TIMELINE_SOURCE,
    SIMULATION_STATE_HEADER,
    SIMULATION_STATE_SOURCE,
    TEMPERATURE_MODE_HEADER,
    MICROSCOPE_FRAME_CONTROLLER_HEADER,
    MICROSCOPE_FRAME_CONTROLLER_SOURCE,
    WORLD_INTERACTION_HEADER,
    WORLD_INTERACTION_SOURCE,
    SIMULATION_SCHEDULER_HEADER,
    SIMULATION_SCHEDULER_SOURCE,
    SIMULATION_SCHEDULER_TEST,
    SIMULATION_OWNER_TEST,
    SIMULATION_EXECUTION_CONTEXT_HEADER,
    SIMULATION_EXECUTION_CONTEXT_SOURCE,
    FLOATING_POINT_ENVIRONMENT_HEADER,
    FLOATING_POINT_ENVIRONMENT_SOURCE,
    SIMULATION_EXECUTION_CONTEXT_TEST,
    LATEST_VALUE_EXCHANGE_HEADER,
    MICROSCOPE_RUNTIME_HEADER,
    MICROSCOPE_RUNTIME_SOURCE,
    MICROSCOPE_RUNTIME_PUBLICATION_CONTRACT_HEADER,
    MICROSCOPE_JNI_SOURCE,
    MICROSCOPE_COMMAND_MAILBOX_HEADER,
    MICROSCOPE_COMMAND_MAILBOX_SOURCE,
    MICROSCOPE_PRESENTATION_STATE_HEADER,
    MICROSCOPE_PRESENTATION_STATE_SOURCE,
    MICROSCOPE_COMMAND_MAILBOX_TEST,
    RENDER_FRAME_BUILDER_HEADER,
    RENDER_FRAME_BUILDER_SOURCE,
    CELL_BOUNDARY_MESH_BUILDER_HEADER,
    CELL_BOUNDARY_MESH_BUILDER_SOURCE,
    RENDER_LINK_GEOMETRY_BUILDER_HEADER,
    RENDER_LINK_GEOMETRY_BUILDER_SOURCE,
    RENDER_CONTACT_CHORD_RESOLVER_HEADER,
    RENDER_CONTACT_CHORD_RESOLVER_SOURCE,
    RENDER_FRAME_BUILDER_TEST,
    LINK_RENDER_CONTRACT_TEST,
    BOUNDARY_LINK_CAPTURE_TEST,
    LINK_GEOMETRY_BUILDER_TEST,
    RENDER_SOURCE_FRAME_HEADER,
    RENDER_SOURCE_FRAME_SOURCE,
    RENDER_SOURCE_CAPTURE_HEADER,
    RENDER_SOURCE_CAPTURE_SOURCE,
    RENDER_FRAME_TYPES_HEADER,
    RENDER_FRAME_EXCHANGE_HEADER,
    RENDER_FRAME_EXCHANGE_SOURCE,
    RENDER_FRAME_EXCHANGE_TEST,
    CELL_DATA_HEADER,
    CELL_DATA_SOURCE,
    SIMULATION_WORLD_HEADER,
    SIMULATION_WORLD_SOURCE,
    RENDER_CONTACT_COLLECTOR_HEADER,
    RENDER_CONTACT_COLLECTOR_SOURCE,
    RENDER_CONTACT_CHORD_BUFFER_HEADER,
    RENDER_CONTACT_CHORD_BUFFER_SOURCE,
    RENDER_CONTACT_LIMITS_HEADER,
    RELEASE_SUBSTRATE_CODEC_SOURCE,
    LEGACY_SUBSTRATE_CODEC_SOURCE,
    MICROSCOPE_FRAME_PIPELINE_HEADER,
    MICROSCOPE_FRAME_PIPELINE_SOURCE,
    MICROSCOPE_FRAME_PIPELINE_TEST,
    MICROSCOPE_FRAME_PIPELINE_TEST_ACCESS,
    MICROSCOPE_DEBUG_HUD_HEADER,
    MICROSCOPE_DEBUG_HUD_SOURCE,
    NATIVE_ROOT / "tests/render_source_frame_tests.cpp",
    GPU_RENDERER_HEADER,
    GPU_RENDERER_SOURCE,
    RENDER_BACKEND_KIND_HEADER,
    RENDER_SURFACE_SESSION_HEADER,
    GLES_SURFACE_SESSION_HEADER,
    GLES_SURFACE_SESSION_SOURCE,
    RENDER_SURFACE_SESSION_TEST,
    PREVIEW_FACADE_HEADER,
    PREVIEW_FACADE_SOURCE,
    PREVIEW_CONTROLLER_HEADER,
    PREVIEW_CONTROLLER_SOURCE,
    PREVIEW_GHOST_HEADER,
    ANDROID_PREVIEW_RENDERER,
    ANDROID_MICROSCOPE_RENDERER,
    ANDROID_MICROSCOPE_HOST_VIEW,
    ANDROID_MICROSCOPE_SURFACE_CONTRACTS,
    ANDROID_MICROSCOPE_RENDER_SURFACE_HOST,
    ANDROID_GLES_MICROSCOPE_SURFACE,
    ANDROID_MICROSCOPE_PRESENTATION_POLICY,
    ANDROID_MICROSCOPE_PRESENTATION_STATE,
    ANDROID_MICROSCOPE_SCREEN,
    ANDROID_MICROSCOPE_PRESENTATION_STATE_TEST,
    ANDROID_MICROSCOPE_STATUS_POLLER,
    ANDROID_MICROSCOPE_STATUS_POLLER_TEST,
    ANDROID_MICROSCOPE_PUBLICATION_SOURCE,
    ANDROID_SIMULATION_GATEWAY_TEST,
    ANDROID_MICROSCOPE_BACKEND,
    ANDROID_MICROSCOPE_TELEMETRY,
    ANDROID_RUNTIME_PUBLICATION,
    ANDROID_MICROSCOPE_TELEMETRY_TEST,
    NATIVE_BUILD_SCRIPT,
)

INTERACTION_UNITS = {
    "app/NativeWorldEditing.cpp",
    "app/NativeWorldSession.cpp",
    "app/NativeSimulationStepper.cpp",
    "app/NativeSimulationOwner.cpp",
    "app/simulation/SimulationScheduler.cpp",
    "app/NativeInteractionController.cpp",
    "app/interaction/NativeViewport.cpp",
    "app/microscope/MicroscopeFrameController.cpp",
    "app/microscope/MicroscopeInputController.cpp",
    "app/microscope/MicroscopeInputBatchLayout.cpp",
    "app/microscope/MicroscopeInputTimeline.cpp",
    "app/microscope/MicroscopeCommandMailbox.cpp",
    "app/microscope/MicroscopePresentationState.cpp",
    "app/microscope/MicroscopeSimulationState.cpp",
    "app/microscope/interaction/MicroscopeDragController.cpp",
    "app/microscope/interaction/MicroscopeInteraction.cpp",
    "app/microscope/interaction/MicroscopeSelectionController.cpp",
    "app/microscope/interaction/MicroscopeTapController.cpp",
    "app/microscope/interaction/MicroscopeWorldInteractionState.cpp",
}

RENDER_PREPARATION_UNITS = {
    "render/frame/RenderContactChordBuffer.cpp",
    "render/frame/RenderContactCollector.cpp",
    "render/frame/PreparedRenderFrame.cpp",
    "render/frame/CellBoundaryMeshBuilder.cpp",
    "render/frame/RenderContactChordResolver.cpp",
    "render/frame/RenderSourceFrame.cpp",
    "render/frame/RenderSourceCapture.cpp",
    "render/frame/RenderFrameBuilder.cpp",
    "render/frame/RenderFrameExchange.cpp",
    "render/frame/RenderLinkGeometryBuilder.cpp",
}

RENDER_GEOMETRY_UNITS = {
    "render/geometry/RenderContactChordNormalizer.cpp",
}


def fail(messages: list[str]) -> None:
    print("native session boundary error:", file=sys.stderr)
    for message in messages:
        print(f"  {message}", file=sys.stderr)
    raise SystemExit(1)


def cmake_list(cmake: str, variable: str) -> set[str] | None:
    match = re.search(
        rf"set\(\s*{re.escape(variable)}\s+(?P<body>.*?)\)",
        cmake,
        re.DOTALL,
    )
    if match is None:
        return None
    return set(re.findall(r"(?:app|render)/[A-Za-z0-9_./-]+\.cpp", match.group("body")))


def cmake_scoped_links(cmake: str, target: str, scope: str) -> set[str] | None:
    match = re.search(
        rf"target_link_libraries\(\s*{re.escape(target)}\s+{re.escape(scope)}"
        rf"(?P<body>.*?)\)",
        cmake,
        re.DOTALL,
    )
    if match is None:
        return None
    return set(re.findall(r"\bcellularity_[a-z_]+\b|\bhwy\b|\bz\b", match.group("body")))


def line_for(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def cpp_code(source: str, *, preserve_literals: bool = False) -> str:
    """Mask comments and, by default, literals so checks cannot match prose."""
    non_code = re.compile(
        r'//[^\n]*|/\*.*?\*/|'
        r'(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})'
        r'\(.*?\)(?P=delimiter)"|'
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        re.DOTALL,
    )
    def mask(match: re.Match[str]) -> str:
        token = match.group(0)
        if preserve_literals and not token.startswith(("//", "/*")):
            return token
        return re.sub(r"[^\n]", " ", token)

    return non_code.sub(mask, source)


def cpp_block(code: str, declaration: str) -> tuple[int, int, str] | None:
    """Find one declaration and its balanced braced body in masked C++ code."""
    match = re.search(declaration + r"\s*\{", code, re.DOTALL)
    if match is None:
        return None
    start = match.end() - 1
    depth = 1
    for offset in range(start + 1, len(code)):
        depth += (code[offset] == "{") - (code[offset] == "}")
        if depth == 0:
            return match.start(), offset + 1, code[start + 1:offset]
    return None


def verify_link_preparation_contract(
    builder_source: str,
    link_header: str,
    link_source: str,
    source_header: str,
    capture_source: str,
) -> list[str]:
    """Check the shared hybrid Link seam; native fixtures prove its geometry."""
    errors: list[str] = []
    builder = cpp_code(builder_source)
    link = cpp_code(link_source)
    if re.search(
        r"\bvoid\s+build\s*\([^;{}]*RenderContactChordResolver\s*&\s*contactChordResolver",
        cpp_code(link_header),
    ) is None:
        errors.append("Link builder must receive the shared contact-chord resolver")
    for method in ("build", "profileBuild"):
        body = cpp_block(builder, rf"\bRenderFrameBuilder::{method}\s*\([^;{{}}]*\)")
        if body is None or re.search(
            r"\bmLinkGeometryBuilder\.build\s*\(\s*source\s*,\s*frame\s*,\s*mContactChordResolver\s*,",
            body[2],
        ) is None:
            errors.append(f"RenderFrameBuilder::{method} must forward its shared resolver to Links")
        if method == "profileBuild" and (body is None or re.search(
            r"\bmLinkGeometryBuilder\.build\s*\(\s*source\s*,\s*frame\s*,\s*mContactChordResolver\s*,"
            r"\s*&contactChordProfile\s*,\s*normalizationProfile\s*,\s*&linkProfile\s*\)",
            body[2],
        ) is None):
            errors.append("RenderFrameBuilder::profileBuild must forward Link and chord profiling")

    append = cpp_block(link, r"\bauto\s+appendLinkEdge\s*=\s*\[&\]\s*\([^;{}]*\)")
    if append is None:
        errors.append("Hybrid Link append body is missing")
    else:
        body = append[2]
        if re.search(
            r"\bcontactChordResolver\.resolve\s*\(\s*source\s*,\s*link\.sourceCellARecordIndex\s*,"
            r"\s*RenderContactChordResolver::Consumer::LinkLookup\s*,"
            r"\s*contactChordProfile\s*,\s*normalizationProfile\s*\)",
            body,
        ) is None:
            errors.append("Link lookup must resolve its captured owner and forward both profiles")
        if re.search(
            r"\bchords\.data\[index\]\.neighborCellIndex\s*==\s*link\.sourceCellBIndex",
            body,
        ) is None:
            errors.append("Link lookup must match its captured neighbor index")
        discarded = cpp_block(
            body,
            r"\bif\s*\(\s*matchingChord\s*!=\s*nullptr\s*&&\s*matchingChord->isDiscarded\s*\)",
        )
        fallback = cpp_block(body, r"\bif\s*\(\s*!\s*useContact\s*\)")
        if (discarded is None or re.search(r"\breturn\s*;", discarded[2]) is None
                or re.search(r"\b(?:appendLinkEdge|writeTriangle|ensureLinkAppendCapacity)\s*\(", discarded[2])):
            errors.append("Discarded contact must suppress the Link before fallback or append")
        if (fallback is None or re.search(r"\blink\.isBoundary\s*\(\s*\)", fallback[2]) is None
                or "link.fallbackCenterRatio" not in fallback[2]
                or re.search(r"\bbool\s+useContact\s*=\s*false\s*;", body) is None
                or re.search(r"\bmatchingChord\s*=\s*nullptr\s*;", body) is None):
            errors.append("Missing contact must retain normal and boundary fallback paths")
        if discarded is not None and fallback is not None and discarded[1] >= fallback[0]:
            errors.append("Discarded contact must be handled before the fallback branch")
        for missing in re.finditer(
            r"\bif\s*\(\s*(?:matchingChord\s*==\s*nullptr|!\s*matchingChord|chords\.count\s*(?:==|<=)\s*0)\s*\)",
            body,
        ):
            remainder = body[missing.end():].lstrip()
            if remainder.startswith("{"):
                branch = cpp_block(remainder, "")
                action = branch[2] if branch is not None else remainder
            else:
                action = remainder.split(";", 1)[0]
            if re.search(r"\b(?:return|continue|throw)\b", action):
                errors.append("Missing contact must not discard or reject a Link candidate")
                break

    boundary_kind = cpp_block(cpp_code(source_header), r"\bbool\s+isBoundary\s*\(\s*\)\s*const\s+noexcept")
    if boundary_kind is None or re.search(
        r"\breturn\s+cellBRuntimeId\s*==\s*kBoundaryCellRuntimeId\s*;", boundary_kind[2]
    ) is None:
        errors.append("Boundary Link identity must use the stable boundary sentinel")
    capture = cpp_code(capture_source)
    candidates = cpp_block(capture, r"\bRenderSourceCapture::captureCandidateLinks\s*\([^;{}]*\)")
    if (candidates is None
            or re.search(r"\bcaptureNormalCandidateLinks\s*\(", candidates[2]) is None
            or re.search(r"\bcaptureBoundaryCandidateLinks\s*\(", candidates[2]) is None
            or re.search(r"\breturn\b", candidates[2])):
        errors.append("Boundary capture must run independently of normal edge count")
    boundary = cpp_block(capture, r"\bRenderSourceCapture::captureBoundaryCandidateLinks\s*\([^;{}]*\)")
    for required in (
        r"\blink\.sourceCellBIndex\s*=\s*CellData::BOUNDARY_CONNECTION_INDEX\s*;",
        r"\blink\.cellBRuntimeId\s*=\s*kBoundaryCellRuntimeId\s*;",
        r"\blink\.sourceCellARecordIndex\s*=\s*ensureSourceCell\s*\(\s*world\s*,\s*owner\s*,\s*true\s*,\s*frame\s*\)",
    ):
        if boundary is None or re.search(required, boundary[2]) is None:
            errors.append(f"Boundary capture is missing its sentinel/owner payload contract: {required}")
    return errors


def verify_link_case_inventory(
    source: str, cmake: str, target: str, variable: str, relative_source: str
) -> list[str]:
    """Keep CLI fixture definitions, case tables and CTest registration aligned."""
    errors: list[str] = []
    code = cpp_code(source)
    definitions = re.findall(r"^void\s+(test[A-Za-z0-9_]+)\s*\(", code, re.MULTILINE)
    declaration = r"\bconst\s+std::array<TestCase,\s*(\d+)>\s+kCases"
    table = cpp_block(code, declaration)
    table_size = re.search(declaration, code)
    case_code = cpp_code(source[table[0]:table[1]], preserve_literals=True) if table else ""
    cases = re.findall(r'\{\s*"([a-z0-9_]+)"\s*,\s*(test[A-Za-z0-9_]+)\s*\}', case_code)
    if (table is None or table_size is None or int(table_size[1]) != len(cases)
            or [function for _, function in cases] != definitions
            or len(set(definitions)) != len(definitions)
            or len({name for name, _ in cases}) != len(cases)):
        errors.append(f"{target} must register every unique fixture definition in its CLI table")
    cmake_code = re.sub(r"#[^\n]*", "", cmake)
    foreach = re.search(rf"\bforeach\(\s*{re.escape(variable)}\s+IN\s+ITEMS\b(.*?)\)\s*(.*?)\bendforeach\(\)", cmake_code, re.DOTALL)
    registered = re.findall(r"\b[a-z][a-z0-9_]*\b", foreach[1]) if foreach else []
    if registered != [name for name, _ in cases]:
        errors.append(f"{target} CTest cases must match its CLI fixture table")
    command = rf"\bCOMMAND\s+{re.escape(target)}\s+\$\{{{re.escape(variable)}\}}\s*\)"
    if foreach is None or re.search(command, foreach[2]) is None:
        errors.append(f"{target} CTest must execute each named fixture")
    if re.search(rf"\badd_executable\(\s*{re.escape(target)}\s+{re.escape(relative_source)}\s*\)", cmake_code) is None:
        errors.append(f"{target} must build its actual fixture source")
    return errors


def main() -> None:
    errors: list[str] = []
    for path in REQUIRED_FILES:
        if not path.is_file():
            errors.append(f"required ownership component is missing: {path.relative_to(REPO_ROOT)}")

    obsolete = (
        APP_ROOT / "NativeMicroscopeFacade.h",
        APP_ROOT / "NativeMicroscopeFacade.cpp",
        NATIVE_ROOT / "render/NativeViewport.h",
        NATIVE_ROOT / "render/NativeViewport.cpp",
    )
    for path in obsolete:
        if path.exists():
            errors.append(f"obsolete mixed-ownership component remains: {path.relative_to(REPO_ROOT)}")

    if errors:
        fail(errors)

    cell_data = CELL_DATA_HEADER.read_text(encoding="utf-8")
    cell_data_source = CELL_DATA_SOURCE.read_text(encoding="utf-8")
    simulation_world = SIMULATION_WORLD_HEADER.read_text(encoding="utf-8")
    simulation_world_source = SIMULATION_WORLD_SOURCE.read_text(encoding="utf-8")
    contact_collector_header = RENDER_CONTACT_COLLECTOR_HEADER.read_text(
        encoding="utf-8"
    )
    contact_collector_source = RENDER_CONTACT_COLLECTOR_SOURCE.read_text(
        encoding="utf-8"
    )
    contact_chord_buffer_header = RENDER_CONTACT_CHORD_BUFFER_HEADER.read_text(
        encoding="utf-8"
    )
    contact_chord_buffer_source = RENDER_CONTACT_CHORD_BUFFER_SOURCE.read_text(
        encoding="utf-8"
    )
    contact_limits = RENDER_CONTACT_LIMITS_HEADER.read_text(encoding="utf-8")
    render_source_capture_header = RENDER_SOURCE_CAPTURE_HEADER.read_text(
        encoding="utf-8"
    )
    render_source_capture = RENDER_SOURCE_CAPTURE_SOURCE.read_text(
        encoding="utf-8"
    )
    for obsolete_name in (
        "mRenderContactChords",
        "mRenderContactChordCount",
        "RenderContactChordBuffer",
        "RenderContactCollector",
    ):
        if obsolete_name in cell_data + cell_data_source:
            errors.append(
                "CellData must not own or build transient render contacts: "
                f"{obsolete_name}"
            )
    for required in (
        "bool mCellContactCacheFresh = false",
        "void invalidateCellContactCache() noexcept",
        "bool cellContactCacheIsFresh() const noexcept",
    ):
        if required not in simulation_world:
            errors.append(
                f"SimulationWorld contact-freshness contract is missing: {required}"
            )
    for forbidden in (
        "RenderContactChordBuffer",
        "RenderContactCollector",
        "prepareRenderContactChords",
    ):
        if forbidden in simulation_world + simulation_world_source:
            errors.append(
                "SimulationWorld must not own or prepare transient render contacts: "
                f"{forbidden}"
            )
    for required in (
        "enum class RenderContactInput",
        "FreshPhysicsContacts",
        "CurrentGeometry",
        "class RenderContactCollector final",
        "CellSweepBroadphase mCurrentGeometryBroadphase",
        "void collect(",
        "collectPhysicsContacts",
        "collectCurrentGeometry",
        "collectBoundaryContacts",
    ):
        if required not in contact_collector_header + contact_collector_source:
            errors.append(
                f"RenderContactCollector contract is missing: {required}"
            )
    if "StalePhysicsContacts" in contact_collector_header + contact_collector_source:
        errors.append(
            "RenderContactCollector must not expose stale physics contacts"
        )
    for forbidden in (
        "mFertilizationTarget",
        "mOverlapDeathIntent",
        "mForceX",
        "mForceY",
        "markCellAsDead",
        "addCellCollisionContact",
        "processCellOverlaps(",
    ):
        if forbidden in contact_collector_header + contact_collector_source:
            errors.append(
                "RenderContactCollector must remain free of simulation side effects: "
                f"{forbidden}"
            )
    update_grids_start = simulation_world_source.find(
        "void SimulationWorld::updateGrids(bool buildFoodSpatialIndex)"
    )
    update_grids_end = simulation_world_source.find(
        "void SimulationWorld::processKnownCellOverlapPair(",
        update_grids_start,
    )
    update_grids_body = (
        simulation_world_source[update_grids_start:update_grids_end]
        if update_grids_start >= 0 and update_grids_end > update_grids_start
        else ""
    )
    invalidation_offset = update_grids_body.find("invalidateCellContactCache();")
    clear_offset = update_grids_body.find("clearGrids();")
    if invalidation_offset < 0 or clear_offset <= invalidation_offset:
        errors.append(
            "SimulationWorld contact-freshness contract is missing ordered "
            "updateGrids invalidation and clear"
        )
    for required in ("mCellContactCacheFresh = true",):
        if required not in simulation_world_source:
            errors.append(
                f"SimulationWorld contact-freshness contract is missing: {required}"
            )
    cmake = CMAKE_PATH.read_text(encoding="utf-8")
    simulation_sources = cmake_list(cmake, "CELLULARITY_SIMULATION_SOURCES")
    preparation_sources = cmake_list(
        cmake, "CELLULARITY_RENDER_PREPARATION_SOURCES"
    )
    for unit in (
        "render/frame/RenderContactChordBuffer.cpp",
        "render/frame/RenderContactCollector.cpp",
    ):
        if preparation_sources is None or unit not in preparation_sources:
            errors.append(f"Render-contact capture unit is missing from preparation: {unit}")
        if simulation_sources is not None and unit in simulation_sources:
            errors.append(f"Render-contact capture unit leaked into simulation: {unit}")
    for required in (
        "class ActiveRange final",
        "const RenderContactChord& operator[](int index) const noexcept",
        "void beginCollection(int activeCellCount)",
        "std::size_t retainedChordCapacity() const noexcept",
        "std::vector<std::uint32_t> mFirstEntryByCell",
        "std::vector<std::uint32_t> mLastEntryByCell",
        "std::vector<std::uint8_t> mChordCountByCell",
        "std::vector<Entry> mEntries",
        "bool tryAppend",
    ):
        if required not in contact_chord_buffer_header:
            errors.append(
                f"RenderContactChordBuffer sparse storage contract is missing: {required}"
            )
    for forbidden in (
        "SimulationWorld",
        "CellTopology",
        "clipAndOrderRenderContactChords",
        "neighborCellIndex ==",
        "CellStorage",
        "clearCell(",
        "copyCell(",
    ):
        if forbidden in contact_chord_buffer_header + contact_chord_buffer_source:
            errors.append(
                "RenderContactChordBuffer must remain policy-free storage: "
                f"{forbidden}"
            )
    for required in (
        '#include "render/frame/RenderContactChordBuffer.h"',
        '#include "render/frame/RenderContactCollector.h"',
        "RenderContactCollector mRenderContactCollector",
        "RenderContactChordBuffer mRenderContactChords",
    ):
        if required not in render_source_capture_header:
            errors.append(
                f"Render source capture ownership contract is missing: {required}"
            )
    for required in (
        "world.cellContactCacheIsFresh()",
        "RenderContactInput::FreshPhysicsContacts",
        "RenderContactInput::CurrentGeometry",
        "mRenderContactCollector.collect(",
        "mRenderContactChords.activeChords(cellIndex)",
        "frame.activeContactChords.insert",
    ):
        if required not in render_source_capture:
            errors.append(
                f"Render source capture must collect and own raw contacts: {required}"
            )
    for required in (
        "kMaximumCellContactChordsPerCell =\n        CellDataConstants::MAX_COLLISIONS_PER_CELL",
        "kMaximumBoundaryContactChordsPerCell = 1",
        "kMaximumRenderContactChordsPerCell =",
        "CellDataConstants::MAX_NEIGHBORS",
    ):
        if required not in contact_limits:
            errors.append(f"Independent render-contact limit is missing: {required}")

    release_substrate_codec = RELEASE_SUBSTRATE_CODEC_SOURCE.read_text(
        encoding="utf-8"
    )
    legacy_substrate_codec = LEGACY_SUBSTRATE_CODEC_SOURCE.read_text(
        encoding="utf-8"
    )
    for forbidden in ("RenderContactChordBuffer", "RenderContactCollector"):
        if forbidden in release_substrate_codec + legacy_substrate_codec:
            errors.append(
                "Persistent codecs must not serialize transient render contacts: "
                f"{forbidden}"
            )
    for required in (
        "const int readNeighbors = input.readInt();",
        "readNeighbors, CellData::MAX_NEIGHBORS, \"legacy Cell neighbor\"",
    ):
        if required not in legacy_substrate_codec:
            errors.append(
                f"Java legacy contact-count compatibility is missing: {required}"
            )

    world_context = (APP_ROOT / "NativeWorldContext.h").read_text(encoding="utf-8")
    if re.search(r"\b(?:renderer|NativeSimulationWorldRenderer|NativeGlApi)\b", world_context, re.IGNORECASE):
        errors.append("NativeWorldContext must contain world/viewport only, never renderer or GL state")

    world_session = (APP_ROOT / "NativeWorldSession.h").read_text(encoding="utf-8")
    for required in (
        "std::unique_ptr<SimulationWorld> mWorld",
        "std::recursive_mutex mMutex",
        "std::int64_t mEnvironmentRevision",
        "class ReadAccess",
        "class WriteAccess",
        "std::unique_lock<std::recursive_mutex>",
        "markEnvironmentChanged",
        "markWorldContentsReplaced",
    ):
        if required not in world_session:
            errors.append(f"NativeWorldSession ownership contract is missing: {required}")
    for deleted_copy in ("ReadAccess", "WriteAccess"):
        if re.search(
            rf"{deleted_copy}\(const\s+{deleted_copy}\s*&\)\s*=\s*delete",
            world_session,
        ) is None:
            errors.append(
                f"NativeWorldSession ownership contract is missing: "
                f"{deleted_copy} deleted copy constructor"
            )

    simulation_owner = SIMULATION_OWNER_HEADER.read_text(encoding="utf-8")
    simulation_owner_source = SIMULATION_OWNER_SOURCE.read_text(encoding="utf-8")
    for required in (
        "class NativeSimulationOwner final",
        "class ReadAccess final",
        "class WriteAccess final",
        "NativeWorldSession mWorldSession",
        "NativeSimulationStepper mSimulation",
        "MicroscopeSimulationState mMicroscopeSimulation",
        "MicroscopeWorldInteractionState",
        "mMicroscopeInteraction",
        "simulationStepper()",
        "microscopeSimulation()",
        "microscopeInteraction()",
        "resetCompletedUpdateCount()",
        "markEnvironmentChanged()",
        "markWorldContentsReplaced()",
        "executeScheduledSimulationCycle",
        "executeSimulationServiceCycle",
        "serviceMicroscopeCommands",
    ):
        if required not in simulation_owner:
            errors.append(f"NativeSimulationOwner contract is missing: {required}")
    for deleted_copy in ("NativeSimulationOwner", "ReadAccess", "WriteAccess"):
        if re.search(
            rf"{deleted_copy}\(const\s+{deleted_copy}\s*&\)\s*=\s*delete",
            simulation_owner,
        ) is None:
            errors.append(
                f"NativeSimulationOwner contract is missing: "
                f"{deleted_copy} deleted copy constructor"
            )
    read_access = simulation_owner.split("class WriteAccess final", 1)[0]
    if "NativeSimulationStepper &simulationStepper()" in read_access:
        errors.append("NativeSimulationOwner read access must not expose the mutable stepper")
    for forbidden in (
        r'#\s*include\s*[<"](?:render|platform|jni)/',
        r"\b(?:NativeGlApi|NativeRenderSession|NativeRenderFacade)\b",
    ):
        match = re.search(forbidden, simulation_owner + simulation_owner_source)
        if match is not None:
            errors.append("NativeSimulationOwner must not own rendering, GL, or JNI state")
    for required in (
        "mMicroscopeSimulation.resetForWorldReplacement()",
        "mMicroscopeInteraction.resetForWorldReplacement()",
        "mWorldAccess.markWorldContentsReplaced()",
    ):
        if required not in simulation_owner_source:
            errors.append(f"Simulation-owner reset safe point is missing: {required}")

    controller = (APP_ROOT / "NativeCellularityController.h").read_text(encoding="utf-8")
    for required in (
        "NativeSimulationOwner mSimulationOwner",
        "std::unique_ptr<NativeInteractionController> mInteraction",
        "std::unique_ptr<NativeRenderSession> mRenderSession",
    ):
        if required not in controller:
            errors.append(f"NativeCellularityController composition is missing: {required}")
    for forbidden in (
        r"\bSimulationWorld&\s+world\s*\(",
        r"\bNativeSimulationWorldRenderer&\s+renderer\s*\(",
        r"\bNativeViewport&\s+viewport\s*\(",
        r"\benvironmentMutex\s*\(",
        r"\bstd::unique_ptr<SimulationWorld>\s+mWorld\b",
        r"\bNativeWorldSession\s+mWorldSession\b",
        r"\bNativeSimulationStepper\s+mSimulation\b",
        r"\bNativeSimulationWorldRenderer\s+mRenderer\b",
        r"\bNativeViewport\s+mViewport\b",
    ):
        match = re.search(forbidden, controller)
        if match is not None:
            errors.append(
                "NativeCellularityController exposes obsolete raw ownership at "
                f"line {line_for(controller, match.start())}"
            )
    owner_member = controller.find("NativeSimulationOwner mSimulationOwner")
    render_member = controller.find(
        "std::unique_ptr<NativeRenderSession> mRenderSession"
    )
    if owner_member == -1 or render_member == -1 or owner_member >= render_member:
        errors.append(
            "NativeSimulationOwner must outlive NativeRenderSession so the "
            "preparation worker joins before world destruction"
        )

    interaction_controller = (APP_ROOT / "NativeInteractionController.h").read_text(
        encoding="utf-8"
    )
    for required in (
        "std::unique_ptr<NativeViewport> mViewport",
        "std::unique_ptr<MicroscopeInputController> mInput",
        "MicroscopeInputTimeline mInputTimeline",
        "MicroscopePresentationState mPresentationState",
        "MicroscopeCommandMailbox mCommandMailbox",
        "tryConsumeNewestPresentationViewport",
        "commandMailbox()",
        "mPresentationTemperatureMode",
    ):
        if required not in interaction_controller:
            errors.append(f"Presentation/input ownership is missing: {required}")
    for forbidden in (
        r"std::unique_ptr<MicroscopeDragController>\s+mDrag",
        r"std::unique_ptr<MicroscopeSelectionController>\s+mSelection",
        r"std::unique_ptr<MicroscopeTapController>\s+mTap",
        r"\bNativeSimulationStepper\s+mSimulation\b",
        r"\bSimulationWorld\s+mWorld\b",
    ):
        if re.search(forbidden, interaction_controller) is not None:
            errors.append(
                "NativeInteractionController retains world-facing owner state"
            )

    latest_value_exchange = LATEST_VALUE_EXCHANGE_HEADER.read_text(encoding="utf-8")
    command_mailbox_header = MICROSCOPE_COMMAND_MAILBOX_HEADER.read_text(
        encoding="utf-8"
    )
    command_mailbox_source = MICROSCOPE_COMMAND_MAILBOX_SOURCE.read_text(
        encoding="utf-8"
    )
    presentation_state_header = MICROSCOPE_PRESENTATION_STATE_HEADER.read_text(
        encoding="utf-8"
    )
    presentation_state_source = MICROSCOPE_PRESENTATION_STATE_SOURCE.read_text(
        encoding="utf-8"
    )
    interaction_controller_source = (
        APP_ROOT / "NativeInteractionController.cpp"
    ).read_text(encoding="utf-8")
    native_controller_source = (
        APP_ROOT / "NativeCellularityController.cpp"
    ).read_text(encoding="utf-8")

    for required in (
        "SlotCount >= 3",
        "LatestValueSlotState::Free",
        "LatestValueSlotState::Writing",
        "LatestValueSlotState::Published",
        "LatestValueSlotState::Reading",
        "std::array<Slot, SlotCount> mSlots",
        "std::atomic<std::uint64_t>::is_always_lock_free",
        "producerReplaced",
        "consumerDiscarded",
        "publishRetriesExhausted",
        "tryConsumeNewest",
    ):
        if required not in latest_value_exchange:
            errors.append(f"Latest-value exchange contract is missing: {required}")
    for forbidden in (
        "std::mutex",
        "std::condition_variable",
        "std::vector",
        "std::deque",
        "std::thread",
    ):
        if forbidden in latest_value_exchange:
            errors.append(
                f"Latest-value hot publication path must remain fixed and non-blocking: {forbidden}"
            )

    microscope_runtime_header = MICROSCOPE_RUNTIME_HEADER.read_text(encoding="utf-8")
    microscope_runtime_source = MICROSCOPE_RUNTIME_SOURCE.read_text(encoding="utf-8")
    microscope_jni = MICROSCOPE_JNI_SOURCE.read_text(encoding="utf-8")
    android_runtime_publication = ANDROID_RUNTIME_PUBLICATION.read_text(
        encoding="utf-8"
    )
    android_microscope_backend = ANDROID_MICROSCOPE_BACKEND.read_text(
        encoding="utf-8"
    )
    android_microscope_renderer = ANDROID_MICROSCOPE_RENDERER.read_text(
        encoding="utf-8"
    )
    microscope_debug_hud_header = MICROSCOPE_DEBUG_HUD_HEADER.read_text(
        encoding="utf-8"
    )
    microscope_debug_hud_source = MICROSCOPE_DEBUG_HUD_SOURCE.read_text(
        encoding="utf-8"
    )
    native_controller_source = NATIVE_CONTROLLER_SOURCE.read_text(encoding="utf-8")
    android_microscope_telemetry = ANDROID_MICROSCOPE_TELEMETRY.read_text(
        encoding="utf-8"
    )
    android_microscope_telemetry_test = ANDROID_MICROSCOPE_TELEMETRY_TEST.read_text(
        encoding="utf-8"
    )

    for required in (
        "NativeMicroscopeRateTelemetry",
        "SimulationSchedulerSnapshot scheduler",
        "actualRateSampleSequence",
        "actualCompletedUps",
        "actualSimulationHoursPerSecond",
        "actualRateWindowUpdates",
        "ownerServiceCount",
        "lastOwnerServiceInterval",
        "maximumOwnerServiceInterval",
        "droppedSourceFrameCount",
        "droppedPreparedFrameCount",
        "statusProducerReplacedCount",
        "statusPublishRetriesExhausted",
        "std::shared_ptr<const std::vector<std::uint8_t>> selectedCellState",
        "std::shared_ptr<const std::vector<std::uint8_t>> selectedGenomeState",
        "LatestValueExchange<NativeMicroscopeRuntimeSnapshot>",
        "mSnapshotConsumerMutex",
        "updateRateTelemetry",
        "mSnapshotExchange.publish(snapshot, snapshot.revision)",
        "mSnapshotExchange.tryConsumeNewest(newest)",
        "microscopeFrameExchangeTelemetry()",
        "writeCellSnapshotWire(world, next.selectedCellId)",
        "writeGenomeDraft(genome)",
    ):
        if required not in microscope_runtime_header + microscope_runtime_source:
            errors.append(f"Owner stats/inspection publication is missing: {required}")
    if "mSnapshotMutex" in microscope_runtime_header + microscope_runtime_source:
        errors.append("Owner stats publication must not share a mutex with UI consumers")
    publish_snapshot = re.search(
        r"void NativeMicroscopeRuntime::publishSnapshot\(.*?\n\}",
        microscope_runtime_source,
        re.DOTALL,
    )
    if publish_snapshot is None:
        errors.append("Owner latest-only status publication body cannot be identified")
    else:
        publish_body = publish_snapshot.group(0)
        if "mSnapshotExchange.publish" not in publish_body:
            errors.append("Owner status must publish through the fixed latest-value exchange")
        for forbidden in ("lock_guard", "unique_lock", "condition_variable", ".wait"):
            if forbidden in publish_body:
                errors.append(
                    f"Owner status publication must never wait for UI: {forbidden}"
                )

    for required in (
        "kActualRateWindow = 500ms",
        "completedUpdates - mActualRateWindowStartUpdates",
        "static_cast<double>(elapsedUpdates) / elapsedSeconds",
        "telemetry.scheduler = scheduler",
        "publication.producerReplaced",
        "publication.publishRetriesExhausted",
    ):
        if required not in microscope_runtime_source:
            errors.append(f"Owner-completed rate observation is missing: {required}")
    if re.search(
        r"mActualRatePolicyGeneration\s*!=\s*scheduler\.policyGeneration",
        microscope_runtime_source,
    ) is None:
        errors.append("Owner rate window must restart when scheduler policy changes")

    for required in (
        "kRateTelemetrySchemaVersion = 1",
        "kRateTelemetryFieldCount = 42",
        "scheduler.requestedTargetUps.has_value()",
        "hasRequestedTarget ? 1 : 0",
        '"([J[J[B[B[B)V"',
        "snapshot.rateTelemetry",
    ):
        if required not in microscope_jni:
            errors.append(f"Versioned JNI rate publication is missing: {required}")
    for required in (
        "private final long[] rateTelemetry",
        "getRateTelemetry()",
    ):
        if required not in android_runtime_publication:
            errors.append(f"Coherent Android runtime publication is missing: {required}")
    for required in (
        "SCHEMA_VERSION = 1L",
        "FIELD_COUNT = 42",
        "SimulationRatePolicy.MAX_THROUGHPUT",
        "requestedTarget != null",
        "NON_NEGATIVE_FIELDS",
        "statusProducerReplacedCount",
        "statusPublishRetriesExhausted",
    ):
        if required not in android_microscope_telemetry:
            errors.append(f"Android rate telemetry contract is missing: {required}")
    for required in (
        "publication.rateTelemetry",
        "runtimeTelemetry = runtimeTelemetry",
    ):
        if required not in android_microscope_backend:
            errors.append(f"Android coherent telemetry decode is missing: {required}")
    for forbidden in (
        "System.nanoTime",
        "cellSnapshotBytes",
        "selectedGenomeDraftBytes",
        "inspectMicroscope",
    ):
        if forbidden in android_microscope_renderer:
            errors.append(
                f"GL callback must not measure simulation or inspect the world: {forbidden}"
            )
    for forbidden in (
        "getActualRateSampleSequence",
        "getActualSimulationHoursPerSecond",
        "getActualCompletedUps",
        "onSimulationSpeedChanged",
    ):
        if forbidden in android_microscope_renderer:
            errors.append(
                f"Android renderer must not duplicate native HUD telemetry: {forbidden}"
            )
    for required in (
        "MicroscopeDebugHudTelemetry debugTelemetry",
        "runtimeSnapshot.rateTelemetry.actualRateSampleSequence",
        "runtimeSnapshot.rateTelemetry.actualRateValid",
        "runtimeSnapshot.rateTelemetry.actualCompletedUps",
        "runtimeSnapshot.stageTimings",
    ):
        if required not in native_controller_source:
            errors.append(f"Native HUD telemetry handoff is missing: {required}")
    native_hud = microscope_debug_hud_header + microscope_debug_hud_source
    for required in (
        "MicroscopeDebugHudTelemetry",
        "telemetry.rateSampleSequence",
        "telemetry.actualRateValid",
        "telemetry.actualCompletedUps",
        "telemetry.stageTimings",
    ):
        if required not in native_hud:
            errors.append(f"Native HUD must consume owner telemetry: {required}")
    for required in (
        "versionedNativePayloadPreservesRateBatchServiceAndDropTelemetry",
        "maxThroughputCannotAcquireANumericRequestedTarget",
        "malformedTelemetryIsRejectedAsOneSnapshot",
    ):
        if required not in android_microscope_telemetry_test:
            errors.append(f"Android rate telemetry codec test is missing: {required}")

    for required in (
        "kOrderedCapacity = 128",
        "kPathSegmentCapacity = 32",
        "kPointerCapacity = 20",
        "kLifecycleReserve = 4",
        "TouchMovePosition",
        "TouchMovePathSegment",
        "TouchUp",
        "TouchCancel",
        "SetRatePolicy",
        "SetOptionalTargetUps",
        "SetSlowMotionSpeed",
        "LifecyclePause",
        "LifecycleResume",
        "Save",
        "Apply",
        "Inspect",
        "RejectedOrderedCapacity",
        "RejectedPathCapacity",
        "RejectedPointerState",
        "AcceptedAndCoalescedPrevious",
        "lastResolvedInputSequence",
        "appliedWorldRevision",
        "presentedResolvedInputSequence",
        "enqueueTouchMovePosition",
        "enqueueTouchMovePathSegment",
        "enqueueSlowMotionSpeed",
        "enqueueRequest",
        "publishViewportCaptureHint",
        "advanceWorldEpoch",
        "markPresented",
        "waitForNotification",
        "serviceUntil",
    ):
        if required not in command_mailbox_header:
            errors.append(f"Microscope command-mailbox contract is missing: {required}")
    for required in (
        "PointerProducerState::Closing",
        "PointerProducerState::Releasing",
        "terminalReservations",
        "normalOrderedAdmission(1)",
        "producerState.store(PointerProducerState::Closing",
        "RejectedPathCapacity",
        "previousPathDepth =",
        "pathSegmentDepth.fetch_add(1",
        "pathSegmentDepth",
        "previousLifecycleDepth =",
        "lifecycleDepth.fetch_add(1",
        "lifecycleDepth",
        "deferredSequence",
        "consumedSequence",
        "retiredSequenceBoundary",
        "refreshThrough",
        "pendingViewportCaptureRevision.exchange",
        "MicroscopeCommandResolution::StaleEpoch",
    ):
        if required not in command_mailbox_source:
            errors.append(f"Microscope command-mailbox implementation is missing: {required}")
    for forbidden in (
        "SimulationWorld",
        "NativeWorldContext",
        "NativeGlApi",
        "Channel.UNLIMITED",
    ):
        if forbidden in command_mailbox_header or forbidden in command_mailbox_source:
            errors.append(
                f"Command ingress must carry values without world/GL/unbounded ownership: {forbidden}"
            )

    for required in (
        "PresentationViewportSnapshot",
        "RenderViewportState viewport",
        "LatestValueExchange<PresentationViewportSnapshot>",
        "publishViewport",
        "tryConsumeNewestViewport",
        "PresentationInteractionGate",
    ):
        if required not in presentation_state_header + presentation_state_source:
            errors.append(f"Immediate presentation-lane contract is missing: {required}")

    for method in ("queuePan", "queueZoom", "queueFling"):
        match = re.search(
            rf"bool\s+NativeInteractionController::{method}\s*\([^{{]*\)\s*"
            rf"\{{(?P<body>.*?)(?=\n\}}\n\n(?:bool|void|int|NativeViewport|cellularity::))",
            interaction_controller_source,
            re.DOTALL,
        )
        if match is None:
            errors.append(f"Immediate presentation method is missing: {method}")
            continue
        body = match.group("body")
        for forbidden in ("readWorld", "writeWorld", "SimulationWorld", "commandMailbox"):
            if forbidden in body:
                errors.append(
                    f"Immediate {method} path reaches owner/mailbox state: {forbidden}"
                )

    for method in (
        "queueMicroscopePan",
        "queueMicroscopeZoom",
        "queueMicroscopeFling",
    ):
        match = re.search(
            rf"bool\s+NativeCellularityController::{method}\s*\([^{{]*\)\s*"
            rf"\{{(?P<body>.*?)\n\}}",
            native_controller_source,
            re.DOTALL,
        )
        if match is None:
            errors.append(f"Native camera command is missing: {method}")
            continue
        if "readWorld" in match.group("body") or "writeWorld" in match.group("body"):
            errors.append(f"{method} must not acquire a simulation-owner lease")

    simulation_state = SIMULATION_STATE_HEADER.read_text(encoding="utf-8")
    simulation_state_source = SIMULATION_STATE_SOURCE.read_text(encoding="utf-8")
    temperature_mode = TEMPERATURE_MODE_HEADER.read_text(encoding="utf-8")
    frame_controller_header = MICROSCOPE_FRAME_CONTROLLER_HEADER.read_text(
        encoding="utf-8"
    )
    scheduler_header = SIMULATION_SCHEDULER_HEADER.read_text(encoding="utf-8")
    scheduler_source = SIMULATION_SCHEDULER_SOURCE.read_text(encoding="utf-8")
    scheduler_test = SIMULATION_SCHEDULER_TEST.read_text(encoding="utf-8")
    world_interaction = WORLD_INTERACTION_HEADER.read_text(encoding="utf-8")
    for required in (
        "mTemperatureMode",
        "mModeGeneration",
        "mSlowMotionSpeed",
        "mLastIncubateRenderTime",
        "mLastIncubateStepTime",
        "mIncubateStepCredit",
        "mLegacyIncubateTargetUps",
        "resetForWorldReplacement()",
        "MicroscopeSchedulerModeContract",
        "ScheduledSourceCaptureCadence",
        "setSchedulerLifecyclePaused",
        "scheduledSourceCaptureDue",
        "markScheduledSourceCaptureAttempt",
    ):
        if required not in simulation_state:
            errors.append(f"Owner simulation cadence state is missing: {required}")
    for required in (
        "enum class MicroscopeTemperatureMode",
        "Freeze = 0",
        "Observe = 1",
        "Incubate = 2",
        "Slow = 3",
        "IncubateNoRender = 4",
        "kDisplayPacedSimulationTargetUps = 60.0",
        "kIncubateCaptureIntervalNanos = 33'333'333LL",
    ):
        if required not in temperature_mode:
            errors.append(f"Central temperature-mode contract is missing: {required}")
    for required in (
        "MicroscopeTemperatureMode::Freeze",
        "MicroscopeTemperatureMode::Observe",
        "MicroscopeTemperatureMode::Incubate",
        "MicroscopeTemperatureMode::Slow",
        "MicroscopeTemperatureMode::IncubateNoRender",
        "kIncubateCaptureIntervalNanos",
    ):
        if required not in frame_controller_header:
            errors.append(f"Frame controller mode alias is missing: {required}")
    if "MicroscopeFrameController.h" in simulation_state_source:
        errors.append(
            "Owner simulation state must not import presentation frame-controller policy"
        )
    for required in (
        "SimulationRatePolicyKind::Paused",
        "SimulationRatePolicyKind::TargetRate",
        "SimulationRatePolicyKind::MaxThroughput",
        "std::chrono::steady_clock",
        "executeSimulationBatch",
        "executeSimulationServiceCycle",
        "SimulationServicePhase::BeforeBatch",
        "SimulationServicePhase::AfterBatch",
        "policyKind()",
    ):
        if required not in scheduler_header + scheduler_source:
            errors.append(f"Simulation scheduler execution contract is missing: {required}")
    for forbidden in (
        "std::this_thread::sleep",
        "std::this_thread::yield",
        "sleep_for(",
        "sleep_until(",
    ):
        if forbidden in scheduler_header or forbidden in scheduler_source:
            errors.append(
                f"Simulation scheduler must not impose pacing in its batch kernel: {forbidden}"
            )
    for production_source in APP_ROOT.rglob("*.cpp"):
        production_text = production_source.read_text(encoding="utf-8")
        if "executeScheduledSimulationCycle" in production_text:
            errors.append(
                "Production source activates the owner scheduler reference path: "
                f"{production_source.relative_to(REPO_ROOT)}"
            )
    for forbidden_magic in ("1700", "3000", "2358"):
        if forbidden_magic in scheduler_header or forbidden_magic in scheduler_source:
            errors.append(
                "Simulation scheduler contains a benchmark/device-derived target: "
                f"{forbidden_magic}"
            )
    if "testServiceCycleOrdersServiceAroundUpdateOnlyMeasurement" not in scheduler_test:
        errors.append("Simulation scheduler service-cycle test is missing")
    for required in (
        "mUpdateCostInitialized ? mLimits.hardBatchUpdateLimit() : 1",
        "testMaxThroughputUsesDeadlineBoundedQuotaAndLearnsClockStride",
        "testBothPoliciesShareOneKernelWithPolicySpecificQuota",
    ):
        if required not in scheduler_source + scheduler_test:
            errors.append(
                f"MaxThroughput deadline-bounded anti-throttle contract is missing: {required}"
            )

    scheduled_update = re.search(
        r"void NativeMicroscopeRuntime::executeScheduledUpdate\(.*?"
        r"(?=\nvoid NativeMicroscopeRuntime::captureScheduledIncubateFrame)",
        microscope_runtime_source,
        re.DOTALL,
    )
    scheduled_capture = re.search(
        r"void NativeMicroscopeRuntime::captureScheduledIncubateFrame\(.*?\n\}",
        microscope_runtime_source,
        re.DOTALL,
    )
    if scheduled_update is None or scheduled_capture is None:
        errors.append(
            "Owner runtime must expose separate measured-update and Incubate-capture phases"
        )
    else:
        update_body = scheduled_update.group(0)
        capture_body = scheduled_capture.group(0)
        for forbidden in (
            "scheduledSourceCaptureDue",
            "markScheduledSourceCaptureAttempt",
            "prepareRenderContactChords",
        ):
            if forbidden in update_body:
                errors.append(
                    f"Fixed-interval capture contaminates scheduler update learning: {forbidden}"
                )
        capture_call = capture_body.find(
            "capture(access, render::RenderCaptureStage::AfterCompletedUpdate"
        )
        completed_mark = capture_body.find("markScheduledSourceCaptureAttempt(")
        if capture_call == -1 or completed_mark <= capture_call:
            errors.append(
                "Incubate source cadence must be recorded after the capture attempt completes"
            )
    for required in (
        "std::unique_ptr<MicroscopeDragController> mDrag",
        "std::unique_ptr<MicroscopeSelectionController> mSelection",
        "std::unique_ptr<MicroscopeTapController> mTap",
        "int mToolOrdinal",
        "resetForWorldReplacement()",
    ):
        if required not in world_interaction:
            errors.append(f"Owner world-interaction state is missing: {required}")

    render_session = (APP_ROOT / "NativeRenderSession.h").read_text(encoding="utf-8")
    for required in (
        "std::unique_ptr<cellularity::microscope::MicroscopeRenderFramePipeline>",
        "mFramePipeline",
        "std::unique_ptr<cellularity::render::RenderSurfaceSession>",
        "mSurfaceSession",
    ):
        if required not in render_session:
            errors.append(f"NativeRenderSession backend seam is missing: {required}")
    if "NativeSimulationWorldRenderer mRenderBuffer" in render_session:
        errors.append("NativeRenderSession must not retain the obsolete synchronous render buffer")
    mutable_world = re.search(r"(?<!const )\bSimulationWorld&", render_session)
    if mutable_world is not None:
        errors.append("NativeRenderSession may borrow SimulationWorld only as const")

    controller_source = (APP_ROOT / "NativeCellularityController.cpp").read_text(
        encoding="utf-8"
    )
    preview_facade_header = PREVIEW_FACADE_HEADER.read_text(encoding="utf-8")
    preview_facade_source = PREVIEW_FACADE_SOURCE.read_text(encoding="utf-8")
    preview_controller_header = PREVIEW_CONTROLLER_HEADER.read_text(encoding="utf-8")
    preview_controller_source = PREVIEW_CONTROLLER_SOURCE.read_text(encoding="utf-8")
    preview_ghost_header = PREVIEW_GHOST_HEADER.read_text(encoding="utf-8")
    android_preview_renderer = ANDROID_PREVIEW_RENDERER.read_text(encoding="utf-8")
    android_preview_session = (
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/preview/"
        "NativeGenomePreviewSession.kt"
    ).read_text(encoding="utf-8")

    if "std::unique_ptr<NativeSimulationWorldRenderer> mSynchronousRenderer" not in preview_controller_header:
        errors.append("Genome Preview must own a dedicated synchronous renderer")
    if "GenomePreviewController(int maxCellCount)" not in preview_controller_header:
        errors.append("Genome Preview renderer capacity must be explicit at construction")
    preview_public_contract = preview_controller_header.split("private:", 1)[0]
    if re.search(
        r"\b(?:captureGhost|draw)\s*\([^;]*NativeSimulationWorldRenderer\s*&",
        preview_public_contract,
        re.DOTALL,
    ):
        errors.append("Genome Preview controller must not accept an external renderer")
    if "NativeSimulationWorldRenderer" in preview_facade_header:
        errors.append("Genome Preview facade must not expose the renderer type")
    if re.search(
        r"mGenomePreview->(?:captureGhost|draw)\s*\([^;]*renderBuffer",
        controller_source,
        re.DOTALL,
    ):
        errors.append("Native controller must not inject the general renderer into Preview/Ghost")
    if "mRenderSession->surfaceSession()" not in controller_source:
        errors.append(
            "Genome Preview must consume the installed high-level render backend"
        )
    if "cellularity::render::PreparedRenderFrame mSnapshot" not in preview_ghost_header:
        errors.append("Genome Preview Ghost must retain its synchronous prepared-frame snapshot")
    preview_sources = (
        preview_facade_header,
        preview_facade_source,
        preview_controller_header,
        preview_controller_source,
        preview_ghost_header,
    )
    for obsolete in (
        "createRendererGlBuffers",
        "deleteRendererGlBuffers",
        "abandonRendererGlBuffers",
    ):
        if any(obsolete in source for source in preview_sources):
            errors.append(
                f"Genome Preview still owns obsolete GLES lifecycle: {obsolete}"
            )
    forbidden_preview_runtime = re.compile(
        r"\b(?:RenderFrameExchange|RenderSourceFrame|std::thread|std::atomic|MicroscopeFrame)\b"
    )
    if any(forbidden_preview_runtime.search(source) for source in preview_sources):
        errors.append("Genome Preview/Ghost must remain outside the microscope frame runtime")
    if "private val previewSession = NativeGenomePreviewSession(" not in android_preview_renderer:
        errors.append("Android Genome Preview view must own a narrow preview session")
    if re.search(
        r"facade\s*=\s*NativeSimulationFacade\s*\([^)]*?\btrue\b",
        android_preview_session,
        re.DOTALL,
    ) is None:
        errors.append("Genome Preview session must construct its separate facade in preview mode")

    renderer_header = (NATIVE_ROOT / "render/NativeSimulationWorldRenderer.h").read_text(encoding="utf-8")
    renderer_source = (NATIVE_ROOT / "render/NativeSimulationWorldRenderer.cpp").read_text(encoding="utf-8")
    builder_header = RENDER_FRAME_BUILDER_HEADER.read_text(encoding="utf-8")
    builder_source = RENDER_FRAME_BUILDER_SOURCE.read_text(encoding="utf-8")
    cell_mesh_builder_header = CELL_BOUNDARY_MESH_BUILDER_HEADER.read_text(
        encoding="utf-8"
    )
    cell_mesh_builder_source = CELL_BOUNDARY_MESH_BUILDER_SOURCE.read_text(
        encoding="utf-8"
    )
    link_geometry_builder_header = RENDER_LINK_GEOMETRY_BUILDER_HEADER.read_text(
        encoding="utf-8"
    )
    link_geometry_builder_source = RENDER_LINK_GEOMETRY_BUILDER_SOURCE.read_text(
        encoding="utf-8"
    )
    contact_chord_resolver_header = RENDER_CONTACT_CHORD_RESOLVER_HEADER.read_text(
        encoding="utf-8"
    )
    contact_chord_resolver_source = RENDER_CONTACT_CHORD_RESOLVER_SOURCE.read_text(
        encoding="utf-8"
    )
    render_frame_builder_test = RENDER_FRAME_BUILDER_TEST.read_text(
        encoding="utf-8"
    )
    link_render_contract_test = LINK_RENDER_CONTRACT_TEST.read_text(encoding="utf-8")
    boundary_link_capture_test = BOUNDARY_LINK_CAPTURE_TEST.read_text(encoding="utf-8")
    link_geometry_builder_test = LINK_GEOMETRY_BUILDER_TEST.read_text(encoding="utf-8")
    render_frame_builder_test_code = cpp_code(render_frame_builder_test)
    link_render_contract_test_code = cpp_code(link_render_contract_test)
    source_frame_header = RENDER_SOURCE_FRAME_HEADER.read_text(encoding="utf-8")
    source_frame_source = RENDER_SOURCE_FRAME_SOURCE.read_text(encoding="utf-8")
    source_capture_header = RENDER_SOURCE_CAPTURE_HEADER.read_text(encoding="utf-8")
    source_capture_source = RENDER_SOURCE_CAPTURE_SOURCE.read_text(encoding="utf-8")
    frame_types_header = RENDER_FRAME_TYPES_HEADER.read_text(encoding="utf-8")
    frame_exchange_header = RENDER_FRAME_EXCHANGE_HEADER.read_text(encoding="utf-8")
    frame_exchange_source = RENDER_FRAME_EXCHANGE_SOURCE.read_text(encoding="utf-8")
    frame_exchange_test = RENDER_FRAME_EXCHANGE_TEST.read_text(encoding="utf-8")
    microscope_pipeline_header = MICROSCOPE_FRAME_PIPELINE_HEADER.read_text(encoding="utf-8")
    microscope_pipeline_source = MICROSCOPE_FRAME_PIPELINE_SOURCE.read_text(encoding="utf-8")
    microscope_pipeline_test = MICROSCOPE_FRAME_PIPELINE_TEST.read_text(encoding="utf-8")
    gpu_header = GPU_RENDERER_HEADER.read_text(encoding="utf-8")
    gpu_source = GPU_RENDERER_SOURCE.read_text(encoding="utf-8")
    render_surface_header = RENDER_SURFACE_SESSION_HEADER.read_text(encoding="utf-8")
    render_backend_kind_header = RENDER_BACKEND_KIND_HEADER.read_text(encoding="utf-8")
    gles_surface_header = GLES_SURFACE_SESSION_HEADER.read_text(encoding="utf-8")
    gles_surface_source = GLES_SURFACE_SESSION_SOURCE.read_text(encoding="utf-8")
    if re.search(
        r"\bbuild\s*\(\s*const\s+SourceFrame\s*&\s*source\s*,"
        r"\s*PreparedFrame\s*&\s*frame\s*\)",
        builder_header,
        re.DOTALL,
    ) is None:
        errors.append("RenderFrameBuilder must read const RenderSourceFrame and write a passed frame")
    cpu_builder_components = (
        (RENDER_FRAME_BUILDER_HEADER, builder_header),
        (RENDER_FRAME_BUILDER_SOURCE, builder_source),
        (CELL_BOUNDARY_MESH_BUILDER_HEADER, cell_mesh_builder_header),
        (CELL_BOUNDARY_MESH_BUILDER_SOURCE, cell_mesh_builder_source),
        (RENDER_LINK_GEOMETRY_BUILDER_HEADER, link_geometry_builder_header),
        (RENDER_LINK_GEOMETRY_BUILDER_SOURCE, link_geometry_builder_source),
        (RENDER_CONTACT_CHORD_RESOLVER_HEADER, contact_chord_resolver_header),
        (RENDER_CONTACT_CHORD_RESOLVER_SOURCE, contact_chord_resolver_source),
    )
    for path, source in cpu_builder_components:
        world_include = re.search(r'#\s*include\s*[<"]core/SimulationWorld\.h[>"]', source)
        world_signature = re.search(r"\bSimulationWorld\s*(?:const\s*)?[&*]", source)
        if world_include is not None or world_signature is not None:
            match = world_include or world_signature
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(source, match.start())}: "
                "CPU render preparation must compile without SimulationWorld"
            )
    for required in (
        "RenderContactChordResolver mContactChordResolver",
        "CellBoundaryMeshBuilder mCellBoundaryMeshBuilder",
        "RenderLinkGeometryBuilder mLinkGeometryBuilder",
    ):
        if required not in builder_header:
            errors.append(f"RenderFrameBuilder composition is missing: {required}")
    for obsolete in (
        "copyToDrawCells",
        "copyToDrawLinks",
        "resolveRenderContactChords",
        "mNormalizedRenderContactChords",
        "mRenderContactChordGenerationByCell",
    ):
        if obsolete in builder_header + builder_source:
            errors.append(
                "RenderFrameBuilder still owns extracted Cell/Link/chord work: "
                f"{obsolete}"
            )
    for owner, source, required in (
        (
            "CellBoundaryMeshBuilder",
            cell_mesh_builder_header + cell_mesh_builder_source,
            ("const RenderSourceFrame& source", "PreparedRenderFrame& frame"),
        ),
        (
            "RenderLinkGeometryBuilder",
            link_geometry_builder_header + link_geometry_builder_source,
            ("const RenderSourceFrame& source", "PreparedRenderFrame& frame"),
        ),
        (
            "RenderContactChordResolver",
            contact_chord_resolver_header + contact_chord_resolver_source,
            ("const RenderSourceFrame& source", "View resolve("),
        ),
    ):
        for snippet in required:
            if snippet not in source:
                errors.append(f"{owner} DTO contract is missing: {snippet}")
    errors.extend(verify_link_preparation_contract(
        builder_source, link_geometry_builder_header, link_geometry_builder_source,
        source_frame_header, source_capture_source,
    ))
    for source, target, variable, relative_source in (
        (link_render_contract_test, "cellularity_link_render_contract_tests", "link_case", "tests/link_render_contract_tests.cpp"),
        (boundary_link_capture_test, "cellularity_boundary_link_capture_tests", "boundary_case", "tests/boundary_link_capture_tests.cpp"),
        (link_geometry_builder_test, "cellularity_link_geometry_builder_tests", "geometry_case", "tests/link_geometry_builder_tests.cpp"),
    ):
        errors.extend(verify_link_case_inventory(source, cmake, target, variable, relative_source))
    for required in (
        "testRenderContactChordResolverCachesNormalizedDtoViews",
        "testCellBoundaryMeshBuilderBuildsDtoPerimeterAndFan",
    ):
        if re.search(rf"^void\s+{required}\s*\(", render_frame_builder_test_code, re.MULTILINE) is None:
            errors.append(f"direct render-preparation DTO test is missing: {required}")
    for required in (
        "testContactChordPresent",
        "testNormalizedContactChord",
        "testShortContactChord",
        "testDiscardedContactChord",
        "testNoContactNormalLink",
        "testShortNormalFallback",
        "testUnequalRadiiWithoutRatioClamp",
        "testRawPatternProjection",
        "testDeadOwnerExcluded",
        "testDeadTargetExcluded",
        "testRenderOwnerIndependentOfTopologyOrder",
        "testBoundaryContactChord",
        "testBoundaryRadialFallback",
        "testZeroCenterAxisUsesVisualAxis",
        "testNearZeroCenterAxisUsesVisualAxis",
        "testFullyDegenerateLinkIsSkipped",
        "testMixedNormalBoundaryCapacity",
    ):
        if re.search(rf"^void\s+{required}\s*\(", link_render_contract_test_code, re.MULTILINE) is None:
            errors.append(f"CPU Link geometry contract test is missing: {required}")
    if "cellularity_link_render_contract_tests" not in cmake:
        errors.append("CPU Link geometry contract tests are not built by CMake")
    for required in (
        "RenderSourceFrame mSourceFrame",
        "RenderSourceCapture mSourceCapture",
        "RenderFrameBuilder mFrameBuilder",
    ):
        if required not in renderer_header:
            errors.append(f"NativeSimulationWorldRenderer composition is missing: {required}")
    if "mSourceCapture.capture(" not in renderer_source:
        errors.append("NativeSimulationWorldRenderer must capture SimulationWorld into its owned source slot")
    if "mFrameBuilder.build(mSourceFrame, mPreparedFrame)" not in renderer_source:
        errors.append("NativeSimulationWorldRenderer preview path must build only from its source slot")

    for required in (
        "kRenderSourceFrameLayoutVersion = 5",
        "RenderSourceFrame(const RenderSourceFrame&) = delete",
        "RenderSourceFrame& operator=(const RenderSourceFrame&) = delete",
        "std::array<std::vector<int>, kCellTypeBucketCount> orderedCellRecordIndices",
        "std::vector<RenderSourceCell> cells",
        "std::vector<RenderContactChord> activeContactChords",
        "std::vector<CellRuntimeId> activeContactChordNeighborRuntimeIds",
        "std::vector<RenderSourceLink> candidateLinks",
        "std::vector<RenderSourceFood> foods",
        "std::vector<RenderSourceDeadSplash> deadSplashes",
        "std::vector<RenderMarker> markers",
    ):
        if required not in source_frame_header:
            errors.append(f"RenderSourceFrame ownership contract is missing: {required}")
    for required in (
        "CellRuntimeId runtimeId",
        "int sourceCellIndex",
        "bool mirrored",
        "bool previewArrow",
    ):
        if required not in source_frame_header:
            errors.append(f"RenderSourceCell identity contract is missing: {required}")
    for required in (
        "TopologyEdgeRuntimeId runtimeId",
        "CellRuntimeId cellARuntimeId",
        "CellRuntimeId cellBRuntimeId",
        "int sourceLinkIndex",
        "int sourceCellBIndex",
        "int sourceCellARecordIndex",
        "float cellAPositionX",
        "float cellAPositionY",
        "float cellBPositionX",
        "float cellBPositionY",
        "float fallbackCenterRatio",
        "float renderStartOffsetX",
        "float renderStartOffsetY",
        "float renderEndOffsetX",
        "float renderEndOffsetY",
        "bool isBoundary() const noexcept",
    ):
        if required not in source_frame_header:
            errors.append(f"RenderSourceLink payload contract is missing: {required}")
    for required in (
        "FoodRuntimeId runtimeId",
        "DeadSplashRuntimeId runtimeId",
    ):
        if required not in source_frame_header:
            errors.append(f"Auxiliary render identity contract is missing: {required}")
    for required in (
        "CellRuntimeId selectedCellRuntimeId",
        "struct RenderMarkerCaptureRequest",
        "struct RenderPresentationState",
        "std::uint64_t coverageRevision",
        "RenderViewBounds coverageBounds",
        "std::array<float, 16> mvpMatrix",
        "float cameraX",
        "float cameraY",
        "float zoom",
        "float viewWidth",
        "float viewHeight",
        "int surfaceWidth",
        "int surfaceHeight",
    ):
        if required not in frame_types_header:
            errors.append(f"Immutable render state contract is missing: {required}")
    if re.search(r"\bGene\b", source_frame_header) is not None:
        errors.append("RenderSourceFrame must not own a mutable simulation Gene")
    pointer_member = re.search(
        r"^(?![^\n]*\boperator\b)\s*(?:const\s+)?"
        r"[A-Za-z_:][A-Za-z0-9_:<> ,]*\s*[&*]\s*"
        r"[A-Za-z_][A-Za-z0-9_]*\s*(?:=|;)",
        source_frame_header,
        re.MULTILINE,
    )
    if pointer_member is not None:
        errors.append(
            f"{RENDER_SOURCE_FRAME_HEADER.relative_to(REPO_ROOT)}:"
            f"{line_for(source_frame_header, pointer_member.start())}: "
            "RenderSourceFrame must not retain pointers or references"
        )
    if re.search(
        r"\bcapture\s*\(\s*const\s+SimulationWorld\s*&\s*world\s*,",
        source_capture_header,
        re.DOTALL,
    ) is None:
        errors.append("RenderSourceCapture must be the const SimulationWorld reader")
    if "frame.valid = true;" not in source_capture_source:
        errors.append("RenderSourceCapture must commit source validity explicitly")
    commit_offset = source_capture_source.find("frame.valid = true;")
    marker_capture_offset = source_capture_source.find("captureMarkers(world")
    if commit_offset < marker_capture_offset:
        errors.append("RenderSourceCapture must commit valid only after every capture pass")
    forbidden_stage4a_runtime = re.compile(
        r"\b(?:RenderFrameExchange|std::thread|std::jthread|std::atomic)\b"
    )
    for path, source in (
        (RENDER_SOURCE_FRAME_HEADER, source_frame_header),
        (RENDER_SOURCE_FRAME_SOURCE, source_frame_source),
        (RENDER_SOURCE_CAPTURE_HEADER, source_capture_header),
        (RENDER_SOURCE_CAPTURE_SOURCE, source_capture_source),
    ):
        match = forbidden_stage4a_runtime.search(source)
        if match is not None:
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(source, match.start())}: "
                "compact source capture must not introduce exchange or concurrency"
            )

    for required in (
        "enum class RenderFrameSlotState",
        "Free = 0",
        "Writing = 1",
        "Published = 2",
        "Reading = 3",
        "std::array<Slot, SlotCount> mSlots",
        "SlotCount >= 2 && SlotCount <= 3",
        "std::atomic<std::uint64_t>::is_always_lock_free",
        "std::atomic<std::uint64_t> control",
        "std::memory_order_release",
        "std::memory_order_acquire",
        "class Producer final",
        "class Consumer final",
        "producerReplacedPublishedFrameCount",
        "consumerDiscardedPublishedFrameCount",
        "incomingDroppedFrameCount",
        "droppedSourceFrameCount",
        "droppedPreparedFrameCount",
        "tryAcquireNewest(std::uint64_t compatibleWorldEpoch)",
        "candidate.metadata.worldEpoch != compatibleWorldEpoch",
        "discardOlderPublished",
        "RenderSourceFrameExchange",
        "PreparedRenderFrameExchange",
    ):
        if required not in frame_exchange_header:
            errors.append(f"RenderFrameExchange contract is missing: {required}")
    forbidden_exchange_dependency = re.compile(
        r'#\s*include\s*[<"](?:app|jni|platform|render/gl)/|'
        r"\b(?:SimulationWorld|NativeViewport|NativeGlApi|JNIEnv|std::mutex|"
        r"std::condition_variable|std::thread|std::jthread)\b"
    )
    for path, source in (
        (RENDER_FRAME_EXCHANGE_HEADER, frame_exchange_header),
        (RENDER_FRAME_EXCHANGE_SOURCE, frame_exchange_source),
    ):
        match = forbidden_exchange_dependency.search(source)
        if match is not None:
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(source, match.start())}: "
                "bounded frame exchange imports scheduler, world, input, or GL state"
            )
    for required in (
        "testLeaseMovesKeepExactlyOneOwner",
        "testPartialFrameIsNeverPublished",
        "testConsumerSelectsNewestSequenceWithoutFifoReplay",
        "testEqualSequenceUsesNewestPublicationTicket",
        "testProducerReplacesOldestUnreadPublication",
        "testReadingSlotIsNeverOverwritten",
        "testProducerDropsImmediatelyWhenEverySlotIsReading",
        "testWorldEpochCompatibilityAndStaleRejection",
        "testSeparateSourceAndPreparedDropProfile",
    ):
        if required not in frame_exchange_test:
            errors.append(f"RenderFrameExchange state-machine test is missing: {required}")

    for required in (
        "SourceExchange sourceExchange",
        "PreparedExchange preparedExchange",
        "std::optional<render::RenderSourceCapture> sourceCapture",
        "std::optional<render::RenderFrameBuilder> frameBuilder",
        "SourceExchange::WriteLease sourceWrite",
        "PreparedExchange::WriteLease preparedWrite",
        "PreparedExchange::ReadLease currentPrepared",
        "std::thread worker",
        "std::condition_variable workerWake",
        "std::atomic<bool> stopRequested",
        "writableSource.publish()",
        "sourceExchange.consumer().tryAcquireNewest(requestedEpoch)",
        "frameBuilder->build(readableSource.frame(), preparedWrite.frame())",
        "preparedWrite.frame().valid = true",
        "preparedWrite.publish()",
        "worker.join()",
        "snapshotWhenQuiescent",
    ):
        if required not in microscope_pipeline_source:
            errors.append(f"Microscope worker frame pipeline is missing: {required}")
    for required in (
        "class MicroscopeRenderFramePipeline final",
        "MicroscopeRenderFramePipeline(const MicroscopeRenderFramePipeline&) =",
        "MicroscopeRenderFramePipeline(MicroscopeRenderFramePipeline&&) = delete",
        "MicroscopeRenderFramePipelineSnapshot",
    ):
        if required not in microscope_pipeline_header:
            errors.append(f"Microscope frame ownership contract is missing: {required}")
    if re.search(r"\b(?:std::jthread|std::stop_token)\b", microscope_pipeline_source):
        errors.append("11.3 worker must preserve the project C++17 boundary")
    worker_start = microscope_pipeline_source.find("void workerMain() noexcept")
    worker_end = microscope_pipeline_source.find(
        "SourceExchange::WriteLease beginSourceWrite(", worker_start
    )
    if worker_start == -1 or worker_end == -1:
        errors.append("Microscope preparation worker body cannot be identified")
    else:
        worker_body = microscope_pipeline_source[worker_start:worker_end]
        forbidden_worker_access = re.search(
            r"\b(?:SimulationWorld|NativeGlApi|JNIEnv|GLES|gl[A-Z][A-Za-z0-9_]*)\b",
            worker_body,
        )
        if forbidden_worker_access is not None:
            errors.append(
                "Preparation worker reads world/JNI/GLES state at "
                f"{MICROSCOPE_FRAME_PIPELINE_SOURCE.relative_to(REPO_ROOT)}:"
                f"{line_for(microscope_pipeline_source, worker_start + forbidden_worker_access.start())}"
            )

    capture_start = microscope_pipeline_source.find(
        "captureImpl(const SimulationWorld& world"
    )
    capture_end = microscope_pipeline_source.find(
        "void waitUntilSourcePrepared(", capture_start
    )
    if capture_start == -1 or capture_end == -1:
        errors.append("Microscope source admission body cannot be identified")
    else:
        capture_body = microscope_pipeline_source[capture_start:capture_end]
        accepted_epoch_position = capture_body.find(
            "latestAcceptedWorldEpoch.store(worldEpoch"
        )
        accepted_sequence_position = capture_body.find(
            "latestAcceptedSourceSequence.store(sourceSequence"
        )
        source_publish_position = capture_body.find("writableSource.publish()")
        worker_notification_position = capture_body.find(
            "++sourcePublicationGeneration"
        )
        if (
            accepted_epoch_position == -1
            or accepted_sequence_position == -1
            or source_publish_position == -1
            or worker_notification_position == -1
            or accepted_epoch_position > source_publish_position
            or accepted_sequence_position > source_publish_position
            or source_publish_position > worker_notification_position
        ):
            errors.append(
                "Source acceptance watermark must precede release-publication, "
                "and worker notification must follow publication"
            )
    native_render_session_source = (
        APP_ROOT / "NativeRenderSession.cpp"
    ).read_text(encoding="utf-8")
    viewport_capture = re.search(
        r"int NativeRenderSession::captureMicroscopeFrame\(.*?\n\}",
        native_render_session_source,
        re.DOTALL,
    )
    if viewport_capture is None or "captureViewport(" not in viewport_capture.group(0):
        errors.append("Production microscope capture must enqueue source without waiting")
    if "markerRequestCount, false" not in microscope_pipeline_source:
        errors.append("Production microscope capture must use non-blocking source admission")
    if "markerRequestCount, true" not in microscope_pipeline_source:
        errors.append(
            "Blocking test and benchmark capture must wait for source admission"
        )
    if "mRenderSession->captureMicroscopeFrame(" not in controller_source:
        errors.append("Microscope step path must use the non-blocking capture entry point")
    if "void ensureFoodSpatialIndexForCapture()" not in simulation_world:
        errors.append("SimulationWorld must expose an owner-side Food capture ensure")
    if "world.foodSpatialIndexIsFresh()" not in source_capture_source:
        errors.append("Render source capture must validate Food index freshness")
    if (
        "left.gridIndex < right.gridIndex" not in source_capture_source
        or "left.foodIndex > right.foodIndex" not in source_capture_source
    ):
        errors.append(
            "Stale Food capture fallback must preserve grid and bucket order"
        )
    if "access.world().ensureFoodSpatialIndexForCapture();" not in microscope_runtime_source:
        errors.append("Microscope runtime must ensure Food index under WriteAccess")
    if "access.world().ensureFoodSpatialIndexForCapture();" not in controller_source:
        errors.append(
            "Frame-step capture must ensure Food index under WriteAccess"
        )
    native_microscope_renderer_source = (
        NATIVE_MICROSCOPE_RENDERER_SOURCE.read_text(encoding="utf-8")
    )
    if "presentLatestPreparedFrame" not in native_microscope_renderer_source:
        errors.append("GL session must poll the newest complete prepared frame")
    controller_draw = re.search(
        r"(?:void|cellularity::render::RenderFrameResult)\s+"
        r"NativeCellularityController::drawMicroscope\(.*?\n\}",
        controller_source,
        re.DOTALL,
    )
    if controller_draw is None:
        errors.append("Microscope controller draw boundary cannot be identified")
    else:
        draw_body = controller_draw.group(0)
        runtime_position = draw_body.find("if (mMicroscopeRuntime != nullptr)")
        runtime_end = draw_body.find("\n    }\n    //", runtime_position)
        pacing_position = draw_body.find("mInteraction->paceBeforeDraw()")
        polling_position = draw_body.find("mRenderSession->drawMicroscope(")
        if (
            runtime_position == -1
            or runtime_end == -1
            or polling_position == -1
            or polling_position > runtime_end
            or (pacing_position != -1 and pacing_position < runtime_end)
        ):
            errors.append(
                "Production owner-backed GL polling must not use legacy frame-rate pacing"
            )
    if "paceAfterDraw" in controller_source:
        errors.append("Microscope controller must not sleep after presenting stale geometry")
    for required in (
        "testWorkerOwnershipTransitions",
        "testWorkerOutputMatchesSynchronousReference",
        "testBlockedWorkerCoalescesToNewestSource",
        "testBlockedGlConsumerKeepsWorkerBounded",
        "testEpochAndInputRollbackBarriers",
        "testAllFixedSlotsReportBoundedStorageAndStopReset",
        "testSessionDrawRefreshesEverySwapWithoutRepeatUpload",
        "testRuntimeControllerRefreshesEverySwapWithoutRepeatUpload",
        "testConcurrentProducerConsumerRaceContract",
    ):
        if required not in microscope_pipeline_test:
            errors.append(f"Microscope worker frame-pipeline test is missing: {required}")
    for obsolete_builder_member in (
        "mCellInViewIndex",
        "mRenderContactChordGenerationByCell",
        "mNormalizedRenderContactChords",
        "mLinkCandidateBits",
        "copyToDrawCells",
        "calculateCellFrustumCulling",
    ):
        if obsolete_builder_member in renderer_header:
            errors.append(
                "NativeSimulationWorldRenderer still owns CPU preparation state: "
                f"{obsolete_builder_member}"
            )
    for source_name, source in (
        ("NativeSimulationWorldRenderer", renderer_source),
        ("RenderSourceCapture", source_capture_source),
        ("RenderFrameBuilder", builder_source),
    ):
        if re.search(r"const_cast\s*<\s*SimulationWorld", source):
            errors.append(f"{source_name} must not cast away SimulationWorld constness")
    forbidden_builder_gl = re.compile(
        r'#\s*include\s*[<"]render/(?:gl|shaders)/|'
        r"\b(?:NativeGlApi|BackgroundShader|CellShader|DeadSplashShader|FoodShader|"
        r"LinkShader|MarkerShader)\b"
    )
    for path, source in cpu_builder_components:
        match = forbidden_builder_gl.search(source)
        if match is not None:
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(source, match.start())}: "
                "CPU render preparation imports GLES/shader state"
            )
    if re.search(r"\bPreparedFrame\s+m[A-Za-z0-9_]*\b", builder_header):
        errors.append("RenderFrameBuilder must write caller-owned slots, not own a frame")

    if "class SimulationWorldGpuRenderer final" not in gpu_header:
        errors.append("SimulationWorldGpuRenderer must remain a non-extensible GL owner")
    if "SimulationWorldGpuRenderer" in renderer_header or "NativeGlApi" in renderer_header:
        errors.append(
            "NativeSimulationWorldRenderer must remain a CPU-only compatibility builder"
        )
    for required in (
        "mCellCoordsVbo",
        "mCellIndicesIbo",
        "mUploadedFrame",
    ):
        if required not in gpu_header:
            errors.append(f"SimulationWorldGpuRenderer ownership contract is missing: {required}")
    for obsolete_gpu_member in (
        "cellCoordsVbo",
        "cellIndicesIbo",
        "uploadFloatRange",
        "drawCellsFromFrame",
    ):
        if obsolete_gpu_member in renderer_header:
            errors.append(
                "NativeSimulationWorldRenderer still owns GPU implementation state: "
                f"{obsolete_gpu_member}"
            )
    for path, source in (
        (GPU_RENDERER_HEADER, gpu_header),
        (GPU_RENDERER_SOURCE, gpu_source),
    ):
        world_include = re.search(r'#\s*include\s*[<"]core/SimulationWorld\.h[>"]', source)
        if world_include is not None:
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(source, world_include.start())}: "
                "GL frame consumer imports SimulationWorld"
            )
        mutable_frame = re.search(r"(?<!const )\bPreparedRenderFrame\s*&", source)
        if mutable_frame is not None:
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(source, mutable_frame.start())}: "
                "GL frame consumer may receive PreparedRenderFrame only as const"
            )
    for required in (
        "SimulationWorldGpuRenderer microscopeGpuRenderer",
        "SimulationWorldGpuRenderer genomePreviewGpuRenderer",
        "mImpl->microscopeGpuRenderer",
        "genomePreviewGpuRenderer.uploadDynamicBuffers",
    ):
        if required not in gles_surface_source:
            errors.append(f"GLES backend ownership contract is missing: {required}")

    neutral_render_units = {
        *NATIVE_ROOT.joinpath("render/frame").rglob("*.h"),
        *NATIVE_ROOT.joinpath("render/frame").rglob("*.cpp"),
        *NATIVE_ROOT.joinpath("render/geometry").rglob("*.h"),
        *NATIVE_ROOT.joinpath("render/geometry").rglob("*.cpp"),
        RENDER_BACKEND_KIND_HEADER,
        RENDER_SURFACE_SESSION_HEADER,
        APP_ROOT / "NativeRenderSession.h",
        APP_ROOT / "NativeRenderSession.cpp",
        APP_ROOT / "NativeCellularityController.h",
        APP_ROOT / "NativeCellularityController.cpp",
        NATIVE_ROOT / "render/NativeSimulationWorldRenderer.h",
        NATIVE_ROOT / "render/NativeSimulationWorldRenderer.cpp",
        PREVIEW_FACADE_HEADER,
        PREVIEW_FACADE_SOURCE,
        *APP_ROOT.joinpath("genome_preview").rglob("*.h"),
        *APP_ROOT.joinpath("genome_preview").rglob("*.cpp"),
    }
    forbidden_backend_api = re.compile(
        r'#\s*include\s*[<"]render/(?:gl|shaders|backend/gles|backend/vulkan)/'
        r'|#\s*include\s*<vulkan/'
        r'|\b(?:NativeGlApi|NativeCellRendererResources|NativeRenderFacade|'
        r'OffscreenRgbaRenderTarget|SimulationWorldGpuRenderer|GL_[A-Z0-9_]+|'
        r'Vk[A-Z][A-Za-z0-9_]*|VK_[A-Z0-9_]+)\b'
    )
    for path in sorted(neutral_render_units):
        source = path.read_text(encoding="utf-8")
        match = forbidden_backend_api.search(source)
        if match is not None:
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(source, match.start())}: "
                "backend-neutral render unit imports a GLES/Vulkan implementation detail"
            )
    if "enum class RenderBackendKind" not in render_backend_kind_header:
        errors.append("High-level render backend kind contract is missing")
    for required in (
        "class RenderSurfaceSession",
        "attachSurface",
        "resizeSurface",
        "presentMicroscope",
        "clearSurface",
        "beginOffscreenRgba",
        "finishOffscreenRgba",
        "abandonSurface",
    ):
        if required not in render_surface_header:
            errors.append(f"High-level render backend contract is missing: {required}")
    for forbidden in ("SimulationWorld", "NativeViewport"):
        if forbidden in render_surface_header:
            errors.append(
                "High-level render backend contract must consume immutable commands, "
                f"not live world/view state: {forbidden}"
            )
    render_surface_test = RENDER_SURFACE_SESSION_TEST.read_text(encoding="utf-8")
    for required in (
        "FakeRenderSurfaceSession",
        "testBackendLifecycleAndRetainedFrameOrchestration",
        "testFirstFrameClearAndSubstrateCaptureOrchestration",
    ):
        if required not in render_surface_test:
            errors.append(f"High-level backend lifecycle test is missing: {required}")

    render_world_readers = {
        APP_ROOT / "NativeRenderFacade.h",
        APP_ROOT / "NativeRenderFacade.cpp",
        APP_ROOT / "NativeRenderSession.h",
        APP_ROOT / "NativeRenderSession.cpp",
        *APP_ROOT.rglob("*Renderer.h"),
        *APP_ROOT.rglob("*Renderer.cpp"),
        *NATIVE_ROOT.joinpath("render").rglob("*Renderer.h"),
        *NATIVE_ROOT.joinpath("render").rglob("*Renderer.cpp"),
        RENDER_FRAME_BUILDER_HEADER,
        RENDER_FRAME_BUILDER_SOURCE,
        RENDER_SOURCE_CAPTURE_HEADER,
        RENDER_SOURCE_CAPTURE_SOURCE,
    }
    mutable_render_world = re.compile(r"(?<!const )\bSimulationWorld\s*(?:&|\*)")
    for path in sorted(render_world_readers):
        text = path.read_text(encoding="utf-8")
        match = mutable_render_world.search(text)
        if match is not None:
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(text, match.start())}: "
                "renderer-facing code may borrow SimulationWorld only as const"
            )

    cmake = CMAKE_PATH.read_text(encoding="utf-8")
    if re.search(
        r"option\(CELLULARITY_ENABLE_SIMULATION_SCHEDULER\s+"
        r'"[^"]+"\s+ON\)',
        cmake,
    ) is None:
        errors.append("Production simulation scheduler must default to ON")
    interaction_sources = cmake_list(cmake, "CELLULARITY_INTERACTION_SOURCES")
    geometry_sources = cmake_list(cmake, "CELLULARITY_RENDER_GEOMETRY_SOURCES")
    preparation_sources = cmake_list(cmake, "CELLULARITY_RENDER_PREPARATION_SOURCES")
    rendering_sources = cmake_list(cmake, "CELLULARITY_RENDERING_SOURCES")
    if interaction_sources is None:
        errors.append("CELLULARITY_INTERACTION_SOURCES is missing")
    elif interaction_sources != INTERACTION_UNITS:
        errors.append(
            "interaction target ownership differs from the session boundary: "
            f"missing={sorted(INTERACTION_UNITS - interaction_sources)}, "
            f"extra={sorted(interaction_sources - INTERACTION_UNITS)}"
        )
    if preparation_sources is None:
        errors.append("CELLULARITY_RENDER_PREPARATION_SOURCES is missing")
    elif preparation_sources != RENDER_PREPARATION_UNITS:
        errors.append(
            "render-preparation target ownership differs from the builder boundary: "
            f"missing={sorted(RENDER_PREPARATION_UNITS - preparation_sources)}, "
            f"extra={sorted(preparation_sources - RENDER_PREPARATION_UNITS)}"
        )
    if geometry_sources is None:
        errors.append("CELLULARITY_RENDER_GEOMETRY_SOURCES is missing")
    elif geometry_sources != RENDER_GEOMETRY_UNITS:
        errors.append(
            "render-geometry target ownership differs from its boundary: "
            f"missing={sorted(RENDER_GEOMETRY_UNITS - geometry_sources)}, "
            f"extra={sorted(geometry_sources - RENDER_GEOMETRY_UNITS)}"
        )
    if rendering_sources is None:
        errors.append("CELLULARITY_RENDERING_SOURCES is missing")
    else:
        for required in (
            "app/NativeRenderSession.cpp",
            "app/NativeCellularityController.cpp",
            "app/microscope/render/MicroscopeRenderFramePipeline.cpp",
            "render/SimulationWorldGpuRenderer.cpp",
            "render/NativeSimulationWorldRenderer.cpp",
            "render/backend/gles/GlesRenderSurfaceSession.cpp",
        ):
            if required not in rendering_sources:
                errors.append(f"rendering target does not own {required}")
        overlap = INTERACTION_UNITS & rendering_sources
        if overlap:
            errors.append(f"interaction units are also owned by rendering: {sorted(overlap)}")
        preparation_overlap = RENDER_PREPARATION_UNITS & rendering_sources
        if preparation_overlap:
            errors.append(
                "render-preparation units are also owned by rendering: "
                f"{sorted(preparation_overlap)}"
            )
        if any(source.startswith("render/backend/vulkan/") for source in rendering_sources):
            errors.append(
                "V-02 GLES rendering must not require an unfinished Vulkan source"
            )

    expected_links = {
        "cellularity_interaction": ("PUBLIC", {
            "cellularity_simulation",
            "cellularity_execution",
        }),
        "cellularity_render_preparation": ("PRIVATE", {
            "cellularity_simulation",
            "cellularity_render_geometry",
        }),
        "cellularity_rendering": ("PRIVATE", {
            "cellularity_simulation",
            "cellularity_formats",
            "cellularity_interaction",
            "cellularity_render_frame_result",
            "cellularity_render_preparation",
            "cellularity_render_overlay_contract",
            "cellularity_vulkan_backend_client",
        }),
    }
    for target, (scope, expected) in expected_links.items():
        links = cmake_scoped_links(cmake, target, scope)
        if links is None:
            errors.append(f"{target} {scope} link declaration is missing")
        elif links != expected:
            errors.append(
                f"{target} {scope} dependency boundary differs: "
                f"expected={sorted(expected)}, actual={sorted(links)}"
            )

    if interaction_sources is not None:
        forbidden_interaction = re.compile(
            r'#\s*include\s*[<"](?:render|platform|jni)/|'
            r"\b(?:NativeGlApi|NativeSimulationWorldRenderer|NativeRenderSession|NativeRenderFacade)\b"
        )
        for relative in sorted(interaction_sources):
            source_path = NATIVE_ROOT / relative
            candidates = (source_path, source_path.with_suffix(".h"))
            for path in candidates:
                if not path.is_file():
                    continue
                text = path.read_text(encoding="utf-8")
                boundary_text = re.sub(
                    r'#\s*include\s*[<"]render/frame/'
                    r'(?:RenderFrameTypes|RenderTimelineClock)\.h[>"]',
                    "",
                    text,
                )
                match = forbidden_interaction.search(boundary_text)
                if match is not None:
                    errors.append(
                        f"{path.relative_to(REPO_ROOT)}: "
                        "interaction target imports rendering/platform state"
                    )

    core_files = sorted((NATIVE_ROOT / "core").glob("*"))
    forbidden_core = re.compile(
        r'#\s*include\s*[<"](?:app|render|platform|jni)/|'
        r"\b(?:NativeGlApi|NativeSimulationWorldRenderer|NativeRenderSession)\b"
    )
    for path in core_files:
        if path.suffix not in {".h", ".cpp"}:
            continue
        text = path.read_text(encoding="utf-8")
        match = forbidden_core.search(text)
        if match is not None:
            errors.append(
                f"{path.relative_to(REPO_ROOT)}:{line_for(text, match.start())}: "
                "simulation core depends on application/rendering"
            )

    interaction_tests = (NATIVE_ROOT / "tests/parity/interaction_tests.cpp").read_text(encoding="utf-8")
    rendering_tests = (NATIVE_ROOT / "tests/parity/rendering_tests.cpp").read_text(encoding="utf-8")
    source_frame_tests = (NATIVE_ROOT / "tests/render_source_frame_tests.cpp").read_text(
        encoding="utf-8"
    )
    native_build_script = NATIVE_BUILD_SCRIPT.read_text(encoding="utf-8")
    native_build_targets = re.search(
        r"\bBUILD_TARGETS=\((.*?)\)",
        re.sub(r"#[^\n]*", "", native_build_script), re.DOTALL,
    )
    for required in (
        "cellularity_link_render_contract_tests",
        "cellularity_boundary_link_capture_tests",
        "cellularity_link_geometry_builder_tests",
    ):
        if native_build_targets is None or re.search(rf"\b{required}\b", native_build_targets[1]) is None:
            errors.append(f"scripts/native/build.sh does not build the Link contract suite: {required}")
    if "cellularity_render_source_frame_tests" not in native_build_script:
        errors.append(
            "scripts/native/build.sh does not build the RenderSourceFrame contract suite"
        )
    if "cellularity_render_frame_exchange_tests" not in native_build_script:
        errors.append(
            "scripts/native/build.sh does not build the RenderFrameExchange contract suite"
        )
    if "cellularity_microscope_render_frame_pipeline_tests" not in native_build_script:
        errors.append(
            "scripts/native/build.sh does not build the microscope worker pipeline suite"
        )
    if "cellularity_simulation_scheduler_tests" not in native_build_script:
        errors.append(
            "scripts/native/build.sh does not build the simulation scheduler suite"
        )
    if "cellularity_native_simulation_owner_tests" not in native_build_script:
        errors.append(
            "scripts/native/build.sh does not build the simulation-owner suite"
        )
    if "cellularity_simulation_execution_context_tests" not in native_build_script:
        errors.append(
            "scripts/native/build.sh does not build the execution-context suite"
        )
    if "cellularity_microscope_command_mailbox_tests" not in native_build_script:
        errors.append(
            "scripts/native/build.sh does not build the command-mailbox suite"
        )
    for required in (
        "!std::is_copy_constructible_v<NativeWorldSession::ReadAccess>",
        "!std::is_copy_constructible_v<NativeWorldSession::WriteAccess>",
        "access.markEnvironmentChanged()",
        "access.markWorldContentsReplaced()",
    ):
        if required not in interaction_tests:
            errors.append(f"world-session RAII/revision test is missing: {required}")
    simulation_owner_tests = SIMULATION_OWNER_TEST.read_text(encoding="utf-8")
    for required in (
        "!std::is_copy_constructible_v<NativeSimulationOwner>",
        "!std::is_move_constructible_v<NativeSimulationOwner>",
        "testOwnerCombinesWorldSimulationAndInteractionState",
        "testWorldReplacementIsOneOwnerSafePoint",
        "testExecutionContextIsBoundToOwnerSafePoints",
        "testOwnerLeaseSerializesCrossThreadAccess",
        "testSchedulerModeContractsBelongToOwnerState",
        "testOwnerExecutesScheduledPoliciesThroughCommonCycle",
        "testOwnerServicesModeCommandBeforePlanning",
        "testOwnerScheduledBatchStopsForServiceNotification",
        "testSchedulerLifecyclePauseHasNoCatchUp",
        "testEquivalentTargetModeChangeResetsCredit",
        "testScheduledSourceCaptureCadenceHasNoDebt",
        "CELLULARITY_EXPECT_NATIVE_PROFILE",
        "access.microscopeSimulation()",
        "access.microscopeInteraction()",
        "access.markWorldContentsReplaced()",
        "access.bindSimulationExecutionToCurrentThread()",
        "access.releaseSimulationExecutionFromCurrentThread()",
    ):
        if required not in simulation_owner_tests:
            errors.append(f"simulation-owner boundary test is missing: {required}")
    execution_context = SIMULATION_EXECUTION_CONTEXT_HEADER.read_text(
        encoding="utf-8"
    ) + SIMULATION_EXECUTION_CONTEXT_SOURCE.read_text(encoding="utf-8")
    execution_context_tests = SIMULATION_EXECUTION_CONTEXT_TEST.read_text(
        encoding="utf-8"
    )
    floating_point_environment = FLOATING_POINT_ENVIRONMENT_HEADER.read_text(
        encoding="utf-8"
    ) + FLOATING_POINT_ENVIRONMENT_SOURCE.read_text(encoding="utf-8")
    for required in (
        "class SimulationExecutionContext final",
        "void bindToCurrentThread()",
        "void releaseFromCurrentThread() noexcept",
        "runParticipantShards",
        "Simulation worker tasks must be noexcept",
        "std::unique_ptr<hwy::ThreadPool>",
        '"CellSimW"',
        "captureFloatingPointEnvironment()",
        "applyFloatingPointEnvironment(",
    ):
        if required not in execution_context:
            errors.append(f"simulation execution context is missing: {required}")
    for required in (
        "std::fegetround()",
        "std::fesetround(",
        "floatingPointEnvironmentMatches(",
    ):
        if required not in floating_point_environment:
            errors.append(f"floating-point environment helper is missing: {required}")
    for required in (
        "testSerialFallbackRequiresNoBindingOrPool",
        "testLazyMaterializationAndWorkerContract",
        "testParallelDispatchRejectsAnotherOwnerThread",
        "testWorkersInheritRoundingAndDenormalPolicy",
        "testResetReuseReconfigurationAndRelease",
        "testRepeatedSessionLifecycleReturnsThreadCountToBaseline",
    ):
        if required not in execution_context_tests:
            errors.append(f"execution-context contract test is missing: {required}")
    mailbox_tests = MICROSCOPE_COMMAND_MAILBOX_TEST.read_text(encoding="utf-8")
    for required in (
        "testPresentationPublishesNewestWholeViewport",
        "testPresentationExchangeHasNoTornConcurrentSnapshots",
        "testPositionMoveCoalescingAndTerminalReservation",
        "testCancelClosesPointerGeneration",
        "testPathSegmentsRemainOrderedAndBounded",
        "testLifecycleReserveSurvivesDiscreteSaturation",
        "testLatestSettingsAndRateValidation",
        "testEpochBarrierCompletionAndPresentationStates",
        "testViewportCaptureHintIsNewestOnly",
        "testNotificationDeadlineHardLimitAndEventCoordinates",
        "testConcurrentHighRateProducerAndOwnerRemainBounded",
        "testCameraCommandsDoNotAcquireWorldOwnerLease",
        "testMailboxApplicationIsBoundToOwnerWriteAccess",
        "kMoveCount = 2'400",
        "RejectedPathCapacity",
        "RejectedOrderedCapacity",
        "MicroscopeCommandResolution::StaleEpoch",
    ):
        if required not in mailbox_tests:
            errors.append(f"command-mailbox contract test is missing: {required}")
    for required in (
        "testRenderFrameBuilderMatchesRendererCompatibilityWrapper",
        "testSimulationWorldGpuRendererSkipsOnlyUnchangedUploads",
        "testNativeSimulationWorldRendererCopyToDrawDoesNotMutateContactChords",
        "testNativeCellularityControllerSeparatesWorldAndRenderLifetimes",
        "testNativeCellularityControllerGenomePreviewOwnsDedicatedRenderer",
        "testNativeCellularityControllerGenomePreviewGhostSurvivesSurfaceLoss",
        "controller.abandonRendererResources()",
        "simulation continues after surface loss",
    ):
        if required not in rendering_tests:
            errors.append(f"render/session lifecycle test is missing: {required}")
    for required in (
        "!std::is_copy_constructible_v<RenderSourceFrame>",
        "testCaptureOwnsOrderedCompactRenderInput",
        "testBuilderUsesSourceOrderWithoutReselection",
        "testSourceRemainsExactAfterWorldMutationAndDestruction",
        "Failed capture invalidates the reusable source slot",
    ):
        if required not in source_frame_tests:
            errors.append(f"RenderSourceFrame contract test is missing: {required}")

    bridge = (REPO_ROOT / "app/src/main/java/com/cytarch/cellularity/NativeCellularityControllerBridge.java").read_text(
        encoding="utf-8"
    )
    facade = (REPO_ROOT / "app/src/main/java/com/cytarch/cellularity/NativeSimulationFacade.java").read_text(
        encoding="utf-8"
    )
    gateway = (REPO_ROOT / "app/src/main/java/com/cytarch/cellularity/platform/simulation/AndroidSimulationGateway.kt").read_text(
        encoding="utf-8"
    )
    microscope_renderer = ANDROID_MICROSCOPE_RENDERER.read_text(
        encoding="utf-8"
    )
    microscope_view = ANDROID_MICROSCOPE_HOST_VIEW.read_text(encoding="utf-8")
    microscope_surface_contracts = ANDROID_MICROSCOPE_SURFACE_CONTRACTS.read_text(
        encoding="utf-8"
    )
    microscope_render_surface_host = ANDROID_MICROSCOPE_RENDER_SURFACE_HOST.read_text(
        encoding="utf-8"
    )
    gles_microscope_surface = ANDROID_GLES_MICROSCOPE_SURFACE.read_text(
        encoding="utf-8"
    )
    microscope_screen = ANDROID_MICROSCOPE_SCREEN.read_text(encoding="utf-8")
    presentation_policy = ANDROID_MICROSCOPE_PRESENTATION_POLICY.read_text(
        encoding="utf-8"
    )
    presentation_state = ANDROID_MICROSCOPE_PRESENTATION_STATE.read_text(
        encoding="utf-8"
    )
    presentation_state_test = ANDROID_MICROSCOPE_PRESENTATION_STATE_TEST.read_text(
        encoding="utf-8"
    )
    status_poller = ANDROID_MICROSCOPE_STATUS_POLLER.read_text(encoding="utf-8")
    status_poller_test = ANDROID_MICROSCOPE_STATUS_POLLER_TEST.read_text(
        encoding="utf-8"
    )
    publication_source = ANDROID_MICROSCOPE_PUBLICATION_SOURCE.read_text(
        encoding="utf-8"
    )
    microscope_backend = ANDROID_MICROSCOPE_BACKEND.read_text(encoding="utf-8")
    session_microscope_backend = (
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/simulation/SessionMicroscopeRenderEndpoint.kt"
    ).read_text(encoding="utf-8")
    native_render_endpoint = (
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/NativeMicroscopeRenderEndpoint.java"
    ).read_text(encoding="utf-8")
    publication_contract = MICROSCOPE_RUNTIME_PUBLICATION_CONTRACT_HEADER.read_text(
        encoding="utf-8"
    )
    gateway_test = ANDROID_SIMULATION_GATEWAY_TEST.read_text(encoding="utf-8")
    native_jni = (NATIVE_ROOT / "jni/NativeRenderJni.cpp").read_text(encoding="utf-8")
    if "nativeSurfaceDestroyed(endpoint.getHandle())" not in bridge:
        errors.append("Java bridge must notify native render session about a lost surface")
    if "bridge.onSurfaceDestroyed(owner)" not in native_render_endpoint:
        errors.append("durable render endpoint must detach rendering without closing its world")
    if "nativeSurfaceDestroyed" not in native_jni or "abandonRendererResources" not in native_jni:
        errors.append("native surface-loss path must abandon render resources")
    bridge_draw = re.search(
        r"(?:void|long) drawMicroscope\([^)]*\)\s*\{(.*?)\n\s*\}",
        bridge,
        re.DOTALL,
    )
    if (
        bridge_draw is None
        or "requireRenderBorrow(owner)" not in bridge_draw.group(1)
    ):
        errors.append(
            "microscope draw must borrow the durable native endpoint handle"
        )
    for obsolete_render_pin in (
        "withPinnedRenderHandleAction",
        "activeRenderCalls",
        "renderCallStateLock",
    ):
        if obsolete_render_pin in bridge:
            errors.append(
                f"obsolete Java render lifetime pin remains: {obsolete_render_pin}"
            )
    if "public synchronized void drawMicroscope" in bridge:
        errors.append("microscope draw must not share the Java command monitor")
    if "nativeAccessLock" in gateway:
        errors.append("render attachment must not use the legacy global native access lock")
    if "stepFrame(" in microscope_renderer or "processFrameSafePoint" in microscope_renderer:
        errors.append("Android render callback must not advance or service simulation")
    if microscope_renderer.count("endpoint.clearWithoutRendering()") != 1:
        errors.append("No Render callback must have exactly one guarded clear path")
    for forbidden in ("pollNotifications", "pollFrameNotification"):
        if forbidden in microscope_renderer:
            errors.append(
                "Android render callback still owns runtime publication: "
                f"{forbidden}"
            )
    for forbidden in (
        "GLSurfaceView",
        "android.opengl",
        "setEGLContextClientVersion",
        "setEGLConfigChooser",
        "RENDERMODE_",
    ):
        if forbidden in microscope_view:
            errors.append(
                "backend-neutral Android microscope host owns GLES mechanics: "
                f"{forbidden}"
            )
    for required in (
        "surfaceHostFactory.create(",
        "MicroscopePresentationStateMachine(",
        "MicroscopeGestureInterpreter",
        "Choreographer.FrameCallback",
        "framePacedCallback",
        "MicroscopeStatusPoller(",
        "statusPoller.update(",
        "substratePreviewCapture.attach(substratePreviewEndpoint)",
        "presentationState.onLifecyclePaused()",
    ):
        if required not in microscope_view:
            errors.append(f"common Android microscope orchestration is missing: {required}")
    for required in (
        "interface MicroscopeRenderSurfaceHost",
        "fun setCadence(cadence: MicroscopeRenderCadence)",
        "fun requestFrame()",
        "fun resumeRendering()",
        "fun pauseRendering()",
    ):
        if required not in microscope_render_surface_host:
            errors.append(f"Android render-surface contract is missing: {required}")
    if "queueRenderTask" in microscope_render_surface_host:
        errors.append("Android render-surface contract still permits arbitrary tasks")
    for required in (
        "class GlesMicroscopeSurfaceView",
        ": GLSurfaceView(context), MicroscopeRenderSurfaceHost",
        "setEGLContextClientVersion(3)",
        "MicroscopeRenderCadence.CONTINUOUS -> RENDERMODE_CONTINUOUSLY",
        "MicroscopeRenderCadence.ON_DEMAND -> RENDERMODE_WHEN_DIRTY",
        "private val androidRenderer = AndroidGlRenderer(",
        "lifecycle = lifecycle",
        "swapInterval = 1",
    ):
        if required not in gles_microscope_surface:
            errors.append(f"GLES microscope surface host is incomplete: {required}")
    for required in (
        "abstract class AndroidMicroscopeRenderHost",
        "): AndroidMicroscopeRenderHost",
        "when (backendKind)",
        "RenderBackendKind.OPEN_GL_ES -> GlesMicroscopeSurfaceHostFactory",
        "RenderBackendKind.VULKAN ->",
    ):
        if required not in microscope_surface_contracts:
            errors.append(f"backend-aware Android microscope factory is missing: {required}")
    for forbidden in ("import android.", "GLSurfaceView", "Choreographer"):
        if forbidden in presentation_state:
            errors.append(
                "pure microscope presentation state depends on Android UI: "
                f"{forbidden}"
            )
    for required in (
        "fun shouldRunFramePacedCallback()",
        "fun shouldRunActiveCameraCallback()",
        "fun shouldRequestInitialFrame()",
        "fun shouldWakeForQueuedInput()",
        "fun onLifecyclePaused()",
        "fun onLifecycleResumed()",
        "fun setPresentationVisible(visible: Boolean)",
        "fun onInputCommandsExecuted(cameraChanged: Boolean)",
    ):
        if required not in presentation_state:
            errors.append(f"pure microscope presentation state is incomplete: {required}")
    for required in (
        "if (!presentationVisible)",
        "cadence = MicroscopeRenderCadence.ON_DEMAND",
        "requestInitialFrame = false",
        "frameIntervalMillis = null",
        "statusPollIntervalMillis = NO_RENDER_STATUS_INTERVAL_MILLIS",
        "MicroscopeTemperature.INCUBATE -> MicroscopePresentationPlan",
        "MicroscopeTemperature.INCUBATE_NO_RENDER -> MicroscopePresentationPlan",
    ):
        if required not in presentation_policy:
            errors.append(f"Android microscope visibility policy is missing: {required}")
    if microscope_screen.count("setPresentationVisible(isVisible)") < 2:
        errors.append(
            "Android microscope visibility must be applied during both surface creation and updates"
        )
    if "surfaceFactory.create(" not in microscope_screen:
        errors.append("Compose microscope UI must create its host through the surface factory")
    if "AndroidMicroscopeView" in microscope_screen or "GlesMicroscopeSurfaceView" in microscope_screen:
        errors.append("Compose microscope UI must not depend on a concrete surface host")
    for required in (
        "observeIdleKeepsContinuousPresentationWithoutPacedCallbacks",
        "incubateUsesOneIndependentPresentationTimerInsteadOfSimulationUpdates",
        "cameraMotionChangesOnlyTemporaryPresentationCadence",
        "hidingAndRestoringTabPreservesIncubatePolicy",
        "pauseAndResumeCannotRetainAnActiveCameraFlag",
        "noRenderModePollsStatusWithoutRecurringFramePresentation",
    ):
        if required not in presentation_state_test:
            errors.append(f"microscope presentation regression test is missing: {required}")
    for required in (
        "class MicroscopeStatusPoller",
        "source.tryStartPoll(activeGeneration, ::onPublication)",
        "pending.generation != generation",
        "host.postDelayed(tick, delay)",
    ):
        if required not in status_poller:
            errors.append(f"microscope status poller is incomplete: {required}")
    for forbidden in ("GLSurfaceView", "onRenderFrame", "requestFrame"):
        if forbidden in status_poller:
            errors.append(
                "microscope status poller depends on presentation work: "
                f"{forbidden}"
            )
    for required in (
        "interface MicroscopeRuntimePublicationSource",
        "fun tryStartPoll(",
        "interface MicroscopeRuntimePublicationExecutor",
        "fun tryStartPublicationPoll(",
    ):
        if required not in publication_source:
            errors.append(f"microscope publication source is incomplete: {required}")
    for required in (
        "publicationPollInFlight.compareAndSet(false, true)",
        "runtimeAccessLock.tryLock()",
        "publicationJob.cancel()",
    ):
        if required not in gateway:
            errors.append(f"gateway publication executor is incomplete: {required}")
    for required in (
        ": MicroscopeSimulationBackend, MicroscopeRuntimePublicationSource",
        "publicationExecutor.tryStartPublicationPoll(",
        "private fun pollRuntimePublication()",
    ):
        if required not in microscope_backend:
            errors.append(f"native microscope publication owner is incomplete: {required}")
    if "pollFrameNotification" in microscope_backend:
        errors.append("native microscope render backend still exposes frame publication polling")
    if "simulation.runtimePublicationSource" not in session_microscope_backend:
        errors.append("session render attachment does not expose its surface-independent publication source")
    if "kPublicationIntervalNanos = 50'000'000LL" not in publication_contract:
        errors.append("native microscope publication interval contract is missing or changed")
    if "nativeMicroscopeRuntimePublicationIntervalMillis" not in bridge or \
            "nativeMicroscopeRuntimePublicationIntervalMillis" not in MICROSCOPE_JNI_SOURCE.read_text(
                encoding="utf-8"
            ):
        errors.append("cold JNI microscope publication interval getter is incomplete")
    for required in (
        "cadenceDoesNotOverlapSlowPollAndDeliversOnlyThroughMainHost",
        "detachReconfigurationAndReleaseDiscardOldGenerationResults",
        "diagnosisAndPreparationFailureKeepTheirTransitionSemantics",
    ):
        if required not in status_poller_test:
            errors.append(f"microscope status regression test is missing: {required}")
    for required in (
        "publicationRunsWithoutSurfaceAndAllowsOnlyOnePollInFlight",
        "publicationSkipsRuntimeLockContentionWithoutWaiting",
        "closeDropsLatePublicationCompletion",
    ):
        if required not in gateway_test:
            errors.append(f"gateway publication regression test is missing: {required}")

    if errors:
        fail(errors)
    print(
        "native session boundary verified: sole simulation storage owner, "
        "RAII world/revision safe points, isolated presentation/input state, "
        "owner-leased simulation capture, "
        "dedicated CPU-only source-to-prepared worker, bounded SPSC frame exchanges, "
        "non-blocking latest-only owner stats/inspection publication, "
        "high-level backend surface seam, GLES-owned immutable-frame consumers, "
        "const render input, dedicated synchronous Preview/Ghost preparation, "
        "surface-independent world"
    )


if __name__ == "__main__":
    main()
