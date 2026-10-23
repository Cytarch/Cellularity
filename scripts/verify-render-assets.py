#!/usr/bin/env python3

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
from zipfile import ZipFile


REPO_ROOT = Path(__file__).resolve().parents[1]
ASSET_ROOT = REPO_ROOT / "native/rendering/assets"
MANIFEST = REPO_ROOT / "native/rendering/assets.sha256"
RENDER_CONTRACT = REPO_ROOT / "native/rendering/gles-render-contract.json"
VULKAN_SHADER_ROOT = REPO_ROOT / "native/rendering/vulkan/shaders"
VULKAN_SHADER_CONTRACT = (
    REPO_ROOT / "native/rendering/vulkan/vulkan-shader-contract.json"
)
NATIVE_TEXTURE_LOADER = REPO_ROOT / "native/render/AssetRenderTextureLoader.cpp"
ANDROID_ASSET_STORE = REPO_ROOT / "native/platform/android/AndroidRenderAssets.cpp"
ANDROID_GL_RENDERER = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/AndroidGlRenderer.kt"
)
RENDER_SURFACE_CONTRACT = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/render/RenderSurfaceLifecycle.kt"
)
SHARED_RENDER_ROOTS = (
    REPO_ROOT / "shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/render",
    REPO_ROOT / "shared/src/androidMain/kotlin/com/cytarch/cellularity/shared/render",
)
BINARY_RESOURCE_ROOTS = (
    REPO_ROOT / "app/src/main/res",
    REPO_ROOT / "shared/src/commonMain/composeResources",
    ASSET_ROOT,
)
RENDER_ASSET_SOURCE_ROOTS = (
    REPO_ROOT / "app/src/main",
    REPO_ROOT / "shared/src",
    REPO_ROOT / "native",
)
BINARY_SUFFIXES = {".jpeg", ".jpg", ".otf", ".png", ".ttf", ".webp"}
EXPECTED_TEXTURES = {
    "textures/gauss_bessel_integral.png",
    "textures/small_dots.png",
}
EXPECTED_VULKAN_BASIC_CELL_PROGRAMS = {
    "buoyocyte",
    "gamete",
    "keratinocyte",
    "lipocyte",
    "myocyte",
    "neurocyte",
    "phagocyte",
    "photocyte",
    "secrocyte",
    "stemocyte",
}
EXPECTED_VULKAN_WORLD_PROGRAMS = {
    "arrow",
    "ciliocyte_outline",
    "dead",
    "devorocyte_outline",
    "flagellocyte_outline",
    "food",
    "genome_grid",
    "genome_tree",
    "glueocyte_outline",
    "link",
    "marker",
    "nitrocyte_outline",
    "pixel_text",
    "senseocyte_outline",
    "stereocyte_outline",
    "virocyte_outline",
}
EXPECTED_VULKAN_PREVIEW_PROGRAMS = {"density_field"}


def safe_relative_path(value: object, context: str, errors: list[str]) -> str | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{context} must be a non-empty relative path")
        return None
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or value != path.as_posix():
        errors.append(f"{context} is unsafe: {value}")
        return None
    return value


def fail(messages: list[str]) -> None:
    for message in messages:
        print(f"resource ownership error: {message}", file=sys.stderr)
    raise SystemExit(1)


def digest_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def digest_file(path: Path) -> str:
    return digest_bytes(path.read_bytes())


def shader_declarations(source: str, storage_classes: tuple[str, ...]) -> dict[str, str]:
    storage = "|".join(re.escape(value) for value in storage_classes)
    declaration = re.compile(
        rf"^\s*(?:{storage})\s+(?:(?:lowp|mediump|highp)\s+)?"
        r"(?P<type>[A-Za-z_][A-Za-z0-9_]*)\s+"
        r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;",
        re.MULTILINE,
    )
    return {
        match.group("name"): match.group("type")
        for match in declaration.finditer(source)
    }


def contract_pass_references(entries: object, context: str, errors: list[str]) -> set[str]:
    references: set[str] = set()
    if not isinstance(entries, list):
        errors.append(f"{context} must be an array")
        return references
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            errors.append(f"{context}[{index}] must be an object")
            continue
        direct = entry.get("pass")
        group = entry.get("group")
        if isinstance(direct, str) and group is None:
            references.add(direct)
        elif isinstance(group, list) and direct is None and all(
            isinstance(value, str) for value in group
        ):
            references.update(group)
        else:
            errors.append(
                f"{context}[{index}] must contain exactly one pass or string group"
            )
    return references


def read_render_contract() -> tuple[dict[str, object], set[str]]:
    if not RENDER_CONTRACT.is_file():
        fail(
            [
                "GLES render contract is missing: "
                + RENDER_CONTRACT.relative_to(REPO_ROOT).as_posix()
            ]
        )
    try:
        contract = json.loads(RENDER_CONTRACT.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as error:
        fail([f"GLES render contract is unreadable: {error}"])

    errors: list[str] = []
    if not isinstance(contract, dict):
        fail(["GLES render contract root must be an object"])
    if contract.get("schemaVersion") != 1:
        errors.append("GLES render contract schemaVersion must be 1")
    if contract.get("backend") != "OpenGL ES 3.0":
        errors.append("GLES render contract backend must be OpenGL ES 3.0")

    raw_programs = contract.get("shaderPrograms")
    if not isinstance(raw_programs, list):
        fail(["GLES render contract shaderPrograms must be an array"])
    program_names: list[str] = []
    program_vertex_inputs: dict[str, set[str]] = {}
    declared_uniform_names: set[str] = set()
    textures = contract.get("textures")
    if not isinstance(textures, dict):
        errors.append("GLES render contract textures must be an object")
        textures = {}
    for index, program in enumerate(raw_programs):
        context = f"shaderPrograms[{index}]"
        if not isinstance(program, dict):
            errors.append(f"{context} must be an object")
            continue
        name = program.get("name")
        vertex_inputs = program.get("vertexInputs")
        uniforms = program.get("uniforms")
        samplers = program.get("samplers")
        if not isinstance(name, str) or re.fullmatch(r"[a-z][a-z0-9_]*", name) is None:
            errors.append(f"{context}.name is invalid")
            continue
        program_names.append(name)
        if not isinstance(vertex_inputs, dict) or not all(
            isinstance(key, str) and isinstance(value, str)
            for key, value in vertex_inputs.items()
        ):
            errors.append(f"{context}.vertexInputs must map names to GLSL types")
            continue
        if not isinstance(uniforms, dict) or not all(
            isinstance(key, str) and isinstance(value, str)
            for key, value in uniforms.items()
        ):
            errors.append(f"{context}.uniforms must map names to GLSL types")
            continue
        if not isinstance(samplers, dict) or not all(
            isinstance(key, str) and isinstance(value, str)
            for key, value in samplers.items()
        ):
            errors.append(f"{context}.samplers must map uniforms to textures")
            continue
        program_vertex_inputs[name] = set(vertex_inputs)
        declared_uniform_names.update(uniforms)

        vertex_path = ASSET_ROOT / "shaders/vertex" / f"{name}.glsl"
        fragment_path = ASSET_ROOT / "shaders/fragment" / f"{name}.glsl"
        if not vertex_path.is_file() or not fragment_path.is_file():
            errors.append(f"{context} has no complete vertex/fragment shader pair")
            continue
        vertex_source = vertex_path.read_text(encoding="utf-8")
        fragment_source = fragment_path.read_text(encoding="utf-8")
        actual_inputs = shader_declarations(vertex_source, ("attribute", "in"))
        actual_uniforms = shader_declarations(vertex_source, ("uniform",))
        actual_uniforms.update(shader_declarations(fragment_source, ("uniform",)))
        if vertex_inputs != actual_inputs:
            errors.append(
                f"{context}.vertexInputs differs from {name}.glsl "
                f"(contract={vertex_inputs}, source={actual_inputs})"
            )
        if uniforms != actual_uniforms:
            errors.append(
                f"{context}.uniforms differs from {name}.glsl "
                f"(contract={uniforms}, source={actual_uniforms})"
            )
        actual_sampler_names = {
            uniform_name
            for uniform_name, uniform_type in actual_uniforms.items()
            if uniform_type.startswith("sampler")
        }
        if set(samplers) != actual_sampler_names:
            errors.append(
                f"{context}.samplers differs from declared sampler uniforms"
            )
        unknown_textures = sorted(set(samplers.values()) - set(textures))
        if unknown_textures:
            errors.append(f"{context}.samplers uses unknown textures: {unknown_textures}")

    if len(program_names) != 28:
        errors.append(
            f"GLES render contract must contain 28 shader programs, found {len(program_names)}"
        )
    if len(program_names) != len(set(program_names)):
        errors.append("GLES render contract contains duplicate shader programs")
    if program_names != sorted(program_names):
        errors.append("GLES render contract shader programs must be sorted by name")
    shader_programs = set(program_names)

    uniform_semantics = contract.get("uniformSemantics")
    if not isinstance(uniform_semantics, dict) or not all(
        isinstance(key, str)
        and isinstance(value, str)
        and bool(value.strip())
        for key, value in uniform_semantics.items()
    ):
        errors.append(
            "GLES render contract uniformSemantics must map names to descriptions"
        )
    elif set(uniform_semantics) != declared_uniform_names:
        errors.append(
            "GLES render contract uniformSemantics differs from declared uniforms "
            f"(missing={sorted(declared_uniform_names - set(uniform_semantics))}, "
            f"unknown={sorted(set(uniform_semantics) - declared_uniform_names)})"
        )

    contract_texture_sources = {
        value.get("source")
        for value in textures.values()
        if isinstance(value, dict)
        and isinstance(value.get("source"), str)
        and value["source"].startswith("textures/")
    }
    if contract_texture_sources != EXPECTED_TEXTURES:
        errors.append(
            "GLES render contract file textures differ from production assets "
            f"(contract={sorted(contract_texture_sources)}, source={sorted(EXPECTED_TEXTURES)})"
        )

    layouts = contract.get("bufferLayouts")
    if not isinstance(layouts, dict) or not layouts:
        errors.append("GLES render contract bufferLayouts must be a non-empty object")
        layouts = {}
    layout_inputs: dict[str, set[str]] = {}
    for name, entries in layouts.items():
        context = f"bufferLayouts.{name}"
        if not isinstance(entries, list) or not entries:
            errors.append(f"{context} must be a non-empty array")
            continue
        inputs: list[str] = []
        for index, entry in enumerate(entries):
            entry_context = f"{context}[{index}]"
            if not isinstance(entry, dict):
                errors.append(f"{entry_context} must be an object")
                continue
            input_name = entry.get("input")
            source = entry.get("source")
            value_type = entry.get("type")
            components = entry.get("components")
            stride = entry.get("strideBytes")
            rate = entry.get("rate")
            offset = entry.get("offsetBytes", 0)
            if not isinstance(input_name, str) or not input_name:
                errors.append(f"{entry_context}.input must be a non-empty string")
                continue
            inputs.append(input_name)
            if not isinstance(source, str) or not source:
                errors.append(f"{entry_context}.source must be a non-empty string")
            if value_type not in {"float32", "uint16"}:
                errors.append(f"{entry_context}.type is unsupported")
            if not isinstance(components, int) or not 1 <= components <= 4:
                errors.append(f"{entry_context}.components must be in 1..4")
            if not isinstance(stride, int) or stride <= 0:
                errors.append(f"{entry_context}.strideBytes must be positive")
            if rate not in {"vertex", "instance", "index"}:
                errors.append(f"{entry_context}.rate is unsupported")
            if not isinstance(offset, int) or offset < 0 or (
                isinstance(stride, int) and stride > 0 and offset >= stride
            ):
                errors.append(f"{entry_context}.offsetBytes is outside the stride")
        if len(inputs) != len(set(inputs)):
            errors.append(f"{context} contains duplicate inputs")
        layout_inputs[name] = set(inputs) - {"index"}
    passes = contract.get("passes")
    if not isinstance(passes, dict) or not passes:
        errors.append("GLES render contract passes must be a non-empty object")
        passes = {}
    programs_used_by_passes: set[str] = set()
    for name, render_pass in passes.items():
        context = f"passes.{name}"
        if not isinstance(render_pass, dict):
            errors.append(f"{context} must be an object")
            continue
        pass_programs = render_pass.get("programs")
        if not isinstance(pass_programs, list) or not pass_programs or not all(
            isinstance(value, str) for value in pass_programs
        ):
            errors.append(f"{context}.programs must be a non-empty string array")
        else:
            programs_used_by_passes.update(pass_programs)
            unknown_programs = sorted(set(pass_programs) - shader_programs)
            if unknown_programs:
                errors.append(f"{context} uses unknown programs: {unknown_programs}")
        layout_name = render_pass.get("layout")
        if layout_name not in layouts:
            errors.append(f"{context} uses an unknown buffer layout")
        elif isinstance(pass_programs, list) and all(
            isinstance(value, str) for value in pass_programs
        ):
            provided_inputs = layout_inputs.get(layout_name, set())
            for program_name in pass_programs:
                required_inputs = program_vertex_inputs.get(program_name, set())
                missing_inputs = sorted(required_inputs - provided_inputs)
                if missing_inputs:
                    errors.append(
                        f"{context} layout {layout_name} does not provide "
                        f"{program_name} inputs: {missing_inputs}"
                    )
        state = render_pass.get("state")
        if not isinstance(state, dict) or set(state) != {
            "blend",
            "depthTest",
            "depthCompare",
            "depthWrite",
        }:
            errors.append(f"{context}.state must define blend and complete depth state")
    if programs_used_by_passes != shader_programs:
        errors.append(
            "GLES render passes do not cover the complete shader catalog "
            f"(unused={sorted(shader_programs - programs_used_by_passes)}, "
            f"unknown={sorted(programs_used_by_passes - shader_programs)})"
        )

    scenarios = contract.get("scenarios")
    expected_scenarios = {
        "microscope",
        "genome-preview",
        "environment-field-preview",
        "saved-file-preview",
    }
    scenario_names: list[str] = []
    scenario_passes: set[str] = set()
    if not isinstance(scenarios, list):
        errors.append("GLES render contract scenarios must be an array")
        scenarios = []
    for index, scenario in enumerate(scenarios):
        context = f"scenarios[{index}]"
        if not isinstance(scenario, dict):
            errors.append(f"{context} must be an object")
            continue
        name = scenario.get("name")
        if not isinstance(name, str):
            errors.append(f"{context}.name must be a string")
            continue
        scenario_names.append(name)
        target = scenario.get("target")
        if not isinstance(target, dict) or not {
            "framebuffer",
            "viewport",
            "color",
            "depth",
        }.issubset(target):
            errors.append(f"{context}.target must define framebuffer, viewport, color, and depth")
        if "orderedPasses" in scenario:
            scenario_passes.update(
                contract_pass_references(
                    scenario["orderedPasses"], f"{context}.orderedPasses", errors
                )
            )
        variants = scenario.get("variants")
        if variants is not None:
            if not isinstance(variants, dict):
                errors.append(f"{context}.variants must be an object")
            else:
                for variant_name, variant in variants.items():
                    if not isinstance(variant, dict):
                        errors.append(f"{context}.variants.{variant_name} must be an object")
                        continue
                    scenario_passes.update(
                        contract_pass_references(
                            variant.get("orderedPasses"),
                            f"{context}.variants.{variant_name}.orderedPasses",
                            errors,
                        )
                    )
    if set(scenario_names) != expected_scenarios or len(scenario_names) != len(
        expected_scenarios
    ):
        errors.append(
            "GLES render contract must define exactly the four production scenarios"
        )
    unknown_scenario_passes = sorted(scenario_passes - set(passes))
    if unknown_scenario_passes:
        errors.append(
            f"GLES render scenarios use unknown passes: {unknown_scenario_passes}"
        )

    if errors:
        fail(errors)
    return contract, shader_programs


def read_vulkan_shader_contract() -> dict[str, str]:
    if not VULKAN_SHADER_CONTRACT.is_file():
        fail(["Vulkan shader contract is missing"])
    try:
        contract = json.loads(VULKAN_SHADER_CONTRACT.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as error:
        fail([f"Vulkan shader contract is unreadable: {error}"])

    errors: list[str] = []
    if not isinstance(contract, dict):
        fail(["Vulkan shader contract root must be an object"])
    if contract.get("schemaVersion") != 1:
        errors.append("Vulkan shader contract schemaVersion must be 1")
    if contract.get("targetEnvironment") != "vulkan1.0":
        errors.append("Vulkan shader targetEnvironment must be vulkan1.0")
    asset_directory = safe_relative_path(
        contract.get("assetDirectory"), "Vulkan assetDirectory", errors
    )
    if asset_directory != "vulkan/shaders":
        errors.append("Vulkan generated shaders must use assets/vulkan/shaders")

    programs = contract.get("programs")
    if not isinstance(programs, list) or not programs:
        fail(errors + ["Vulkan shader programs must be a non-empty array"])

    program_names: list[str] = []
    source_to_asset: dict[str, str] = {}
    for index, program in enumerate(programs):
        context = f"Vulkan programs[{index}]"
        if not isinstance(program, dict):
            errors.append(f"{context} must be an object")
            continue
        name = program.get("name")
        if not isinstance(name, str) or re.fullmatch(r"[a-z][a-z0-9_]*", name) is None:
            errors.append(f"{context}.name is invalid")
            continue
        program_names.append(name)
        if program.get("entryPoint") != "main":
            errors.append(f"{context}.entryPoint must be main")
        push_constant_bytes = program.get("pushConstantBytes")
        if (
            not isinstance(push_constant_bytes, int)
            or isinstance(push_constant_bytes, bool)
            or not 0 <= push_constant_bytes <= 128
            or push_constant_bytes % 4 != 0
        ):
            errors.append(f"{context}.pushConstantBytes is invalid")
        descriptor_sets = program.get("descriptorSets")
        if not isinstance(descriptor_sets, list):
            errors.append(f"{context}.descriptorSets must be an array")
        else:
            set_indices: list[int] = []
            for set_position, descriptor_set in enumerate(descriptor_sets):
                set_context = f"{context}.descriptorSets[{set_position}]"
                if not isinstance(descriptor_set, dict) or set(descriptor_set) != {
                    "set",
                    "bindings",
                }:
                    errors.append(f"{set_context} must define set and bindings")
                    continue
                set_index = descriptor_set.get("set")
                bindings = descriptor_set.get("bindings")
                if (
                    not isinstance(set_index, int)
                    or isinstance(set_index, bool)
                    or set_index < 0
                ):
                    errors.append(f"{set_context}.set is invalid")
                    continue
                set_indices.append(set_index)
                if not isinstance(bindings, list) or not bindings:
                    errors.append(f"{set_context}.bindings must be non-empty")
                    continue
                binding_indices: list[int] = []
                buffer_types = {
                    "uniform_buffer",
                    "uniform_buffer_dynamic",
                    "storage_buffer",
                    "storage_buffer_dynamic",
                }
                descriptor_types = buffer_types | {"combined_image_sampler"}
                for binding_position, binding in enumerate(bindings):
                    binding_context = (
                        f"{set_context}.bindings[{binding_position}]"
                    )
                    if not isinstance(binding, dict):
                        errors.append(f"{binding_context} must be an object")
                        continue
                    required_fields = {"binding", "type", "count", "stages"}
                    allowed_fields = required_fields | {"byteRange"}
                    if not required_fields <= set(binding) or not set(binding) <= allowed_fields:
                        errors.append(f"{binding_context} has an invalid shape")
                        continue
                    binding_index = binding.get("binding")
                    descriptor_type = binding.get("type")
                    count = binding.get("count")
                    stages = binding.get("stages")
                    byte_range = binding.get("byteRange")
                    if (
                        not isinstance(binding_index, int)
                        or isinstance(binding_index, bool)
                        or binding_index < 0
                    ):
                        errors.append(f"{binding_context}.binding is invalid")
                    else:
                        binding_indices.append(binding_index)
                    if descriptor_type not in descriptor_types:
                        errors.append(f"{binding_context}.type is invalid")
                    if (
                        not isinstance(count, int)
                        or isinstance(count, bool)
                        or count < 1
                    ):
                        errors.append(f"{binding_context}.count is invalid")
                    if (
                        not isinstance(stages, list)
                        or not stages
                        or any(stage not in {"vertex", "fragment"} for stage in stages)
                        or len(stages) != len(set(stages))
                    ):
                        errors.append(f"{binding_context}.stages is invalid")
                    if descriptor_type in buffer_types:
                        if (
                            not isinstance(byte_range, int)
                            or isinstance(byte_range, bool)
                            or byte_range < 1
                            or byte_range % 4 != 0
                        ):
                            errors.append(f"{binding_context}.byteRange is invalid")
                    elif "byteRange" in binding:
                        errors.append(
                            f"{binding_context}.byteRange is valid only for buffers"
                        )
                if binding_indices != sorted(binding_indices) or len(
                    binding_indices
                ) != len(set(binding_indices)):
                    errors.append(
                        f"{set_context}.bindings must be sorted and unique"
                    )
            if set_indices != sorted(set_indices) or len(set_indices) != len(
                set(set_indices)
            ):
                errors.append(f"{context}.descriptorSets must be sorted and unique")
        if name == "background":
            expected_descriptors = [
                {
                    "set": 0,
                    "bindings": [
                        {
                            "binding": 0,
                            "type": "uniform_buffer_dynamic",
                            "count": 1,
                            "byteRange": 144,
                            "stages": ["vertex", "fragment"],
                        }
                    ],
                }
            ]
            if push_constant_bytes != 0:
                errors.append("Vulkan background must not use push constants")
            if descriptor_sets != expected_descriptors:
                errors.append(
                    "Vulkan background descriptor contract must contain exactly "
                    "one 144-byte dynamic uniform buffer"
                )
        elif name == "density_field":
            expected_descriptors = [
                {
                    "set": 0,
                    "bindings": [
                        {
                            "binding": 0,
                            "type": "uniform_buffer_dynamic",
                            "count": 1,
                            "byteRange": 144,
                            "stages": ["vertex", "fragment"],
                        }
                    ],
                }
            ]
            if push_constant_bytes != 0:
                errors.append("Vulkan density-field preview must not use push constants")
            if descriptor_sets != expected_descriptors:
                errors.append(
                    "Vulkan density-field descriptor contract must contain exactly "
                    "one 144-byte dynamic uniform buffer"
                )
        elif name in EXPECTED_VULKAN_BASIC_CELL_PROGRAMS:
            expected_descriptors = [
                {
                    "set": 0,
                    "bindings": [
                        {
                            "binding": 0,
                            "type": "uniform_buffer_dynamic",
                            "count": 1,
                            "byteRange": 96,
                            "stages": ["vertex"],
                        },
                        {
                            "binding": 1,
                            "type": "combined_image_sampler",
                            "count": 1,
                            "stages": ["fragment"],
                        },
                    ],
                }
            ]
            if push_constant_bytes != 0:
                errors.append(f"Vulkan basic-cell program {name} must not use push constants")
            if descriptor_sets != expected_descriptors:
                errors.append(
                    f"Vulkan basic-cell descriptor contract differs for {name}"
                )
        elif name in EXPECTED_VULKAN_WORLD_PROGRAMS:
            world_bindings = [
                {
                    "binding": 0,
                    "type": "uniform_buffer_dynamic",
                    "count": 1,
                    "byteRange": 96,
                    "stages": ["vertex"],
                }
            ]
            if name == "dead":
                world_bindings.append(
                    {
                        "binding": 2,
                        "type": "combined_image_sampler",
                        "count": 1,
                        "stages": ["fragment"],
                    }
                )
            elif name == "pixel_text":
                world_bindings.append(
                    {
                        "binding": 3,
                        "type": "combined_image_sampler",
                        "count": 1,
                        "stages": ["fragment"],
                    }
                )
            expected_descriptors = [{"set": 0, "bindings": world_bindings}]
            if push_constant_bytes != 0:
                errors.append(
                    f"Vulkan world program {name} must not use push constants"
                )
            if descriptor_sets != expected_descriptors:
                errors.append(
                    f"Vulkan world descriptor contract differs for {name}"
                )

        stages = program.get("stages")
        if not isinstance(stages, dict) or set(stages) != {"vertex", "fragment"}:
            errors.append(f"{context}.stages must define vertex and fragment")
            continue
        for stage, suffix in (("vertex", ".vert"), ("fragment", ".frag")):
            source = safe_relative_path(stages.get(stage), f"{context}.{stage}", errors)
            if source is None:
                continue
            if not source.endswith(suffix):
                errors.append(f"{context}.{stage} must end in {suffix}")
            if source in source_to_asset:
                errors.append(f"duplicate Vulkan shader source: {source}")
                continue
            source_to_asset[source] = f"{asset_directory}/{source}.spv"

    if program_names != sorted(program_names) or len(program_names) != len(set(program_names)):
        errors.append("Vulkan shader programs must be sorted and unique")
    if "background" not in program_names:
        errors.append("Vulkan shader contract is missing the production background pass")
    expected_programs = (
        {"background"}
        | EXPECTED_VULKAN_BASIC_CELL_PROGRAMS
        | EXPECTED_VULKAN_WORLD_PROGRAMS
        | EXPECTED_VULKAN_PREVIEW_PROGRAMS
    )
    actual_programs = set(program_names)
    if actual_programs != expected_programs:
        errors.append(
            "Vulkan world program inventory differs from production "
            f"(missing={sorted(expected_programs - actual_programs)}, "
            f"unexpected={sorted(actual_programs - expected_programs)})"
        )
    if errors:
        fail(errors)
    return source_to_asset


def verify_vulkan_shader_sources(source_to_asset: dict[str, str]) -> None:
    errors: list[str] = []
    actual_sources = {
        path.relative_to(VULKAN_SHADER_ROOT).as_posix()
        for path in VULKAN_SHADER_ROOT.rglob("*")
        if path.is_file()
    }
    expected_sources = set(source_to_asset)
    if actual_sources != expected_sources:
        errors.append(
            "Vulkan shader source inventory differs from its contract "
            f"(missing={sorted(expected_sources - actual_sources)}, "
            f"unexpected={sorted(actual_sources - expected_sources)})"
        )

    for relative in sorted(expected_sources & actual_sources):
        source = (VULKAN_SHADER_ROOT / relative).read_text(encoding="utf-8")
        if re.search(r"^\s*#version\s+450\s*$", source, re.MULTILINE) is None:
            errors.append(f"Vulkan shader does not declare GLSL 450: {relative}")
        if "layout(location = 0)" not in source:
            errors.append(f"Vulkan shader has no explicit stage I/O location: {relative}")
        if relative in {"background.vert", "background.frag"}:
            if "layout(push_constant)" in source:
                errors.append(
                    f"Vulkan background shader still uses push constants: {relative}"
                )
            if "layout(std140, set = 0, binding = 0)" not in source:
                errors.append(
                    f"Vulkan background shader has no explicit dynamic UBO: {relative}"
                )
            for offset in (
                "layout(offset = 0)",
                "layout(offset = 64)",
                "layout(offset = 80)",
                "layout(offset = 96)",
            ):
                if offset not in source:
                    errors.append(f"Vulkan shader is missing {offset}: {relative}")
        elif Path(relative).stem == "density_field":
            expected_density_source = (
                (
                    "layout(std140, set = 0, binding = 0)",
                    "layout(offset = 0) mat4 mvpMatrix",
                    "layout(offset = 64) vec4 field",
                    "layout(offset = 80) vec4 fieldExtra",
                    "layout(offset = 96) vec4 preview",
                    "layout(offset = 112) mat2 preRotation",
                    "POSITIONS[gl_VertexIndex]",
                    "clipPosition.y = -clipPosition.y",
                    "environmentField.preRotation * clipPosition.xy",
                    "clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w)",
                )
                if relative.endswith(".vert")
                else (
                    "FIELD_LINEAR_DIRECTIONAL",
                    "FIELD_RADIAL",
                    "VECTOR_FIELD_RADIAL_INWARD",
                    "pointInRadialArrows",
                    "vec2 center = radialDirection * 0.48",
                    "vec2 arrowDirection = radialDirection * directionSign",
                    "smoothstep(0.0045, 0.0049, ringDistance)",
                    "compositeOver",
                    "outColor = vec4(ARROW_COLOR, 1.0)",
                )
            )
            for declaration in expected_density_source:
                if declaration not in source:
                    errors.append(
                        f"Vulkan density-field contract is missing "
                        f"{declaration}: {relative}"
                    )
        elif Path(relative).stem in EXPECTED_VULKAN_BASIC_CELL_PROGRAMS:
            if "layout(push_constant)" in source:
                errors.append(f"Vulkan basic-cell shader uses push constants: {relative}")
            if relative.endswith(".vert"):
                for declaration in (
                    "layout(std140, set = 0, binding = 0)",
                    "layout(offset = 0) mat4 mvpMatrix",
                    "layout(offset = 64) mat2 preRotation",
                    "layout(location = 0) in vec2 vPosition",
                    "layout(location = 1) in vec4 vTPosition",
                    "layout(location = 2) in vec2 vCenter",
                    "layout(location = 3) in vec4 vColor",
                    "clipPosition.y = -clipPosition.y",
                    "clipPosition.xy = cell.preRotation * clipPosition.xy",
                    "clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w)",
                ):
                    if declaration not in source:
                        errors.append(
                            f"Vulkan basic-cell vertex contract is missing "
                            f"{declaration}: {relative}"
                        )
            else:
                if "layout(location = 0) out vec4 outColor" not in source:
                    errors.append(
                        f"Vulkan basic-cell fragment output is not explicit: {relative}"
                    )
        elif Path(relative).stem == "marker":
            expected_marker_source = (
                (
                    "layout(std140, set = 0, binding = 0)",
                    "layout(location = 0) in vec2 vPosition",
                    "layout(location = 1) in vec4 vCenter",
                    "vPosition * 1.1 * (vCenter.z + 0.005) + vCenter.xy",
                    "clipPosition.y = -clipPosition.y",
                    "clipPosition.xy = cell.preRotation * clipPosition.xy",
                    "clipPosition.z = 0.5 * (clipPosition.z + clipPosition.w)",
                )
                if relative.endswith(".vert")
                else (
                    "vec3(0.407843, 0.8, 0.294118)",
                    "vec3(0.12549)",
                    "smoothstep(0.0045, 0.0049, ringDistance)",
                    "smoothstep(0.0025, 0.0029, ringDistance)",
                    "backingCoverage * cp.a",
                )
            )
            for declaration in expected_marker_source:
                if declaration not in source:
                    errors.append(
                        f"Vulkan marker contract is missing {declaration}: {relative}"
                    )
        elif Path(relative).stem == "pixel_text":
            expected_text_source = (
                (
                    "layout(std140, set = 0, binding = 0)",
                    "layout(location = 0) in vec4 aRect",
                    "layout(location = 1) in float aGlyph",
                    "layout(location = 2) in vec4 aColor",
                    "QUAD_VERTICES[gl_VertexIndex]",
                    "vec2(5.0, 7.0)",
                    "vec2(40.0, 42.0)",
                    "aGlyph >= 42.5",
                    "clipPosition.y = -clipPosition.y",
                    "clipPosition.xy = cell.preRotation * clipPosition.xy",
                )
                if relative.endswith(".vert")
                else (
                    "layout(set = 0, binding = 3) uniform sampler2D glyphAtlas",
                    "texture(glyphAtlas, glyphUv).r < 0.5",
                    "outColor = glyphColor",
                )
            )
            for declaration in expected_text_source:
                if declaration not in source:
                    errors.append(
                        f"Vulkan pixel-text contract is missing {declaration}: {relative}"
                    )

    committed_spirv = sorted(
        path.relative_to(REPO_ROOT).as_posix()
        for root in RENDER_ASSET_SOURCE_ROOTS
        for path in root.rglob("*.spv")
        if path.is_file()
    )
    if committed_spirv:
        errors.append(
            "generated SPIR-V must not live in source directories: "
            + ", ".join(committed_spirv)
        )
    if errors:
        fail(errors)


def normalized_shader_logic(source: str) -> str:
    source = re.sub(
        r"layout\(std140, set = 0, binding = 0\) uniform CellUniforms\s*\{.*?\}\s*cell\s*;",
        "",
        source,
        flags=re.DOTALL,
    )
    source = re.sub(r"^\s*#version\s+450\s*$", "", source, flags=re.MULTILINE)
    source = re.sub(r"^\s*precision\s+\w+\s+\w+\s*;\s*$", "", source, flags=re.MULTILINE)
    source = re.sub(
        r"^\s*(?:layout\([^\n]*\)\s+)?(?:in|out|varying|attribute|uniform)\s+"
        r"(?:\w+\s+)?\w+\s*;\s*$",
        "",
        source,
        flags=re.MULTILINE,
    )
    source = source.replace("texture2D", "texture")
    source = source.replace("gl_FragColor", "outColor")
    return re.sub(r"\s+", " ", source).strip()


def verify_vulkan_basic_cell_formula_parity() -> None:
    errors: list[str] = []
    gles_shader_root = ASSET_ROOT / "shaders"
    for name in sorted(EXPECTED_VULKAN_BASIC_CELL_PROGRAMS):
        gles_vertex = (gles_shader_root / "vertex" / f"{name}.glsl").read_text(
            encoding="utf-8"
        )
        vulkan_vertex = (VULKAN_SHADER_ROOT / f"{name}.vert").read_text(
            encoding="utf-8"
        )
        gles_logic_prefix = gles_vertex.split("vec4 mp =", maxsplit=1)[0]
        vulkan_logic_prefix = vulkan_vertex.split("vec4 clipPosition =", maxsplit=1)[0]
        if normalized_shader_logic(gles_logic_prefix) != normalized_shader_logic(
            vulkan_logic_prefix
        ):
            errors.append(
                f"Vulkan basic-cell vertex formula drifted from GLES: {name}"
            )

        gles_fragment = (
            gles_shader_root / "fragment" / f"{name}.glsl"
        ).read_text(encoding="utf-8")
        vulkan_fragment = (VULKAN_SHADER_ROOT / f"{name}.frag").read_text(
            encoding="utf-8"
        )
        if normalized_shader_logic(gles_fragment) != normalized_shader_logic(
            vulkan_fragment
        ):
            errors.append(
                f"Vulkan basic-cell fragment formula drifted from GLES: {name}"
            )
    if errors:
        fail(errors)


def link_shader_contract_errors(gles_source: str, vulkan_source: str) -> list[str]:
    def normalized_link_logic(source: str) -> str:
        source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.DOTALL)
        source = source.replace("fragmentColor", "gl_FragColor")
        return re.sub(r"\s+", "", normalized_shader_logic(source))

    # Freeze the unclamped formula, not just equality between two wrong backends.
    expected_logic = normalized_link_logic("""
        const float LINK_TAPER_TIP_Y = 1.0;
        const float LINK_TAPER_EPSILON = 0.000001;
        const float LINK_STRIPE_FREQUENCY = 3.0;
        const float LINK_STRIPE_PHASE_SHIFT = 0.3;
        const float LINK_STRIPE_VISIBLE_PHASE_WIDTH = 0.4;
        const float LINK_STRIPE_PATTERN_LIMIT = 0.7;
        const float LINK_ALPHA_INSET = 0.1;

        void main() {
            float taperWidth = LINK_TAPER_TIP_Y - linkLocal.y;
            if (taperWidth <= LINK_TAPER_EPSILON) {
                gl_FragColor = vec4(0.0);
                return;
            }
            float taperedSide = linkLocal.x / taperWidth;
            float linkPatternPosition = mix(taperedSide, linkLocal.z,
                    LINK_TAPER_TIP_Y - abs(taperedSide) * linkLocal.y);
            float stripePhase = fract(linkPatternPosition * LINK_STRIPE_FREQUENCY
                    - LINK_STRIPE_PHASE_SHIFT);
            float linkAlpha = (linkLocal.y - LINK_TAPER_TIP_Y)
                    * (linkLocal.y - LINK_TAPER_TIP_Y)
                    + linkLocal.x * linkLocal.x - LINK_ALPHA_INSET;
            if (stripePhase < LINK_STRIPE_VISIBLE_PHASE_WIDTH
                    && linkPatternPosition < LINK_STRIPE_PATTERN_LIMIT
                    && linkPatternPosition > -LINK_STRIPE_PATTERN_LIMIT) {
                gl_FragColor = vec4(0.0, 0.0, 0.0, linkAlpha);
            } else {
                gl_FragColor = vec4(0.0);
            }
        }
    """)
    gles_logic = normalized_link_logic(gles_source)
    vulkan_logic = normalized_link_logic(vulkan_source)
    errors: list[str] = []
    if gles_logic != vulkan_logic:
        errors.append("Vulkan link fragment formula drifted from GLES")
    for backend, logic in (("GLES", gles_logic), ("Vulkan", vulkan_logic)):
        if logic != expected_logic:
            errors.append(
                f"{backend} link shader must preserve the unclamped taper, stripes "
                "and alpha with only a transparent apex guard"
            )
    return errors


def verify_link_shader_formula_parity() -> None:
    gles_source = (ASSET_ROOT / "shaders/fragment/link.glsl").read_text(
        encoding="utf-8"
    )
    vulkan_source = (VULKAN_SHADER_ROOT / "link.frag").read_text(encoding="utf-8")
    errors = link_shader_contract_errors(gles_source, vulkan_source)
    if errors:
        fail(errors)


def discover_production_shader_programs() -> set[str]:
    cell_type_source = (REPO_ROOT / "native/core/CellType.cpp").read_text(encoding="utf-8")
    cell_type_catalog = re.search(
        r"CELL_TYPE_INFO\s*=\s*\{(?P<body>.*?)\n\};",
        cell_type_source,
        re.DOTALL,
    )
    if cell_type_catalog is None:
        fail(["could not locate the production cell shader catalog"])

    programs = set(re.findall(r'"([a-z][a-z0-9_]*)"', cell_type_catalog.group("body")))
    for source in (REPO_ROOT / "native/render/shaders").glob("*.cpp"):
        programs.update(
            re.findall(
                r'ShaderProgram\(\s*assets\s*,\s*gl\s*,\s*"([a-z][a-z0-9_]*)"\s*\)',
                source.read_text(encoding="utf-8"),
            )
        )

    resources_source = (
        REPO_ROOT / "native/render/NativeCellRendererResources.cpp"
    ).read_text(encoding="utf-8")
    programs.update(
        re.findall(
            r'make_unique<CellShader>\(\s*assets\s*,\s*gl\s*,\s*"([a-z][a-z0-9_]*)"\s*\)',
            resources_source,
        )
    )
    return programs


def discover_production_textures() -> set[str]:
    resources_source = (
        REPO_ROOT / "native/render/NativeCellRendererResources.cpp"
    ).read_text(encoding="utf-8")
    return {
        f"textures/{name}.png"
        for name in re.findall(
            r'loadTexture\(\s*"([a-z][a-z0-9_]*)"\s*,',
            resources_source,
        )
    }


def read_manifest() -> dict[str, str]:
    if not MANIFEST.is_file():
        fail([f"renderer asset manifest is missing: {MANIFEST.relative_to(REPO_ROOT)}"])

    entries: dict[str, str] = {}
    entry_paths: list[str] = []
    errors: list[str] = []
    for line_number, raw_line in enumerate(
        MANIFEST.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2 or re.fullmatch(r"[0-9a-f]{64}", parts[0]) is None:
            errors.append(f"invalid manifest line {line_number}")
            continue
        expected_hash, relative = parts
        posix_path = PurePosixPath(relative)
        if (
            posix_path.is_absolute()
            or ".." in posix_path.parts
            or relative != posix_path.as_posix()
            or relative in entries
        ):
            errors.append(f"unsafe or duplicate manifest path on line {line_number}: {relative}")
            continue
        entries[relative] = expected_hash
        entry_paths.append(relative)
    if entry_paths != sorted(entry_paths):
        errors.append("renderer asset manifest paths must be sorted")
    if errors:
        fail(errors)
    return entries


def verify_source_assets(
    entries: dict[str, str], expected_shader_programs: set[str]
) -> None:
    errors: list[str] = []
    production_shader_programs = discover_production_shader_programs()
    if production_shader_programs != expected_shader_programs:
        errors.append(
            "declared shader catalog differs from production references "
            f"(source-only={sorted(production_shader_programs - expected_shader_programs)}, "
            f"catalog-only={sorted(expected_shader_programs - production_shader_programs)})"
        )
    production_textures = discover_production_textures()
    if production_textures != EXPECTED_TEXTURES:
        errors.append(
            "declared texture catalog differs from production references "
            f"(source-only={sorted(production_textures - EXPECTED_TEXTURES)}, "
            f"catalog-only={sorted(EXPECTED_TEXTURES - production_textures)})"
        )

    noncanonical: list[Path] = []
    third_party_root = REPO_ROOT / "native/third_party"
    for root in RENDER_ASSET_SOURCE_ROOTS:
        for path in root.rglob("*"):
            if not path.is_file() or ASSET_ROOT in path.parents or third_party_root in path.parents:
                continue
            if path.suffix == ".glsl" or path.name in {
                "gauss_bessel_integral.png",
                "small_dots.png",
            }:
                noncanonical.append(path.relative_to(REPO_ROOT))
    if noncanonical:
        errors.append(
            "renderer assets exist outside the canonical source root: "
            + ", ".join(sorted(path.as_posix() for path in noncanonical))
        )

    actual = {
        path.relative_to(ASSET_ROOT).as_posix()
        for path in ASSET_ROOT.rglob("*")
        if path.is_file()
    }
    expected = set(entries)
    expected_asset_paths = EXPECTED_TEXTURES | {
        f"shaders/{stage}/{program}.glsl"
        for stage in ("fragment", "vertex")
        for program in expected_shader_programs
    }
    catalog_missing = sorted(expected_asset_paths - expected)
    catalog_unexpected = sorted(expected - expected_asset_paths)
    if catalog_missing:
        errors.append(f"renderer manifest is missing production assets: {catalog_missing}")
    if catalog_unexpected:
        errors.append(f"renderer manifest contains non-production assets: {catalog_unexpected}")
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing:
        errors.append(f"renderer assets missing from source: {missing}")
    if unexpected:
        errors.append(f"renderer assets missing from manifest: {unexpected}")

    for relative in sorted(expected & actual):
        actual_hash = digest_file(ASSET_ROOT / relative)
        if actual_hash != entries[relative]:
            errors.append(f"renderer asset content changed without manifest update: {relative}")

    fragment_stems = {
        Path(relative).stem
        for relative in expected
        if relative.startswith("shaders/fragment/") and relative.endswith(".glsl")
    }
    vertex_stems = {
        Path(relative).stem
        for relative in expected
        if relative.startswith("shaders/vertex/") and relative.endswith(".glsl")
    }
    if fragment_stems != expected_shader_programs or vertex_stems != expected_shader_programs:
        errors.append(
            "renderer shader inventory differs from the production catalog "
            f"(missing fragment={sorted(expected_shader_programs - fragment_stems)}, "
            f"unexpected fragment={sorted(fragment_stems - expected_shader_programs)}, "
            f"missing vertex={sorted(expected_shader_programs - vertex_stems)}, "
            f"unexpected vertex={sorted(vertex_stems - expected_shader_programs)})"
        )

    textures = {relative for relative in expected if relative.startswith("textures/")}
    if textures != EXPECTED_TEXTURES:
        errors.append(f"unexpected renderer texture inventory: {sorted(textures)}")

    shared_render_files = sorted(
        path.relative_to(REPO_ROOT).as_posix()
        for root in SHARED_RENDER_ROOTS
        if root.exists()
        for path in root.rglob("*")
        if path.is_file()
    )
    if shared_render_files:
        errors.append(
            "shared still owns Android/low-level renderer code: "
            + ", ".join(shared_render_files)
        )

    shared_low_level_tokens = (
        "android.opengl",
        "javax.microedition.khronos",
        "GLES20",
        "GLES30",
        "GLUtils",
        "GlApi",
        "RenderTextureLoader",
        "nativeHandle",
        "textureHandle",
    )
    for source in (REPO_ROOT / "shared/src").rglob("*.kt"):
        content = source.read_text(encoding="utf-8")
        present = [token for token in shared_low_level_tokens if token in content]
        if present:
            errors.append(
                "shared source owns low-level renderer API in "
                + source.relative_to(REPO_ROOT).as_posix()
                + ": "
                + ", ".join(present)
            )

    if not NATIVE_TEXTURE_LOADER.is_file():
        errors.append(
            "native renderer texture adapter is missing: "
            + NATIVE_TEXTURE_LOADER.relative_to(REPO_ROOT).as_posix()
        )
    else:
        texture_loader_source = NATIVE_TEXTURE_LOADER.read_text(encoding="utf-8")
        required_decoder_tokens = (
            '"textures/" + name + ".png"',
            "mAssets.readBytes(path)",
            "stbi_load_from_memory",
            "NativeRenderTexturePixelFormat::Rgba8",
            "result.pixels.assign",
        )
        missing_decoder_tokens = [
            token for token in required_decoder_tokens
            if token not in texture_loader_source
        ]
        if missing_decoder_tokens:
            errors.append(
                "native texture adapter does not expose the backend-neutral "
                "decoded payload: " + ", ".join(missing_decoder_tokens)
            )
        forbidden_upload_tokens = (
            "AAssetManager",
            "JNIEnv",
            "GLES30",
            "GLUtils",
            "texImage2D",
        )
        present_upload_tokens = [
            token for token in forbidden_upload_tokens
            if token in texture_loader_source
        ]
        if present_upload_tokens:
            errors.append(
                "native texture decoder crosses its platform/backend boundary: "
                + ", ".join(present_upload_tokens)
            )

    if not ANDROID_ASSET_STORE.is_file():
        errors.append(
            "Android native asset store is missing: "
            + ANDROID_ASSET_STORE.relative_to(REPO_ROOT).as_posix()
        )
    else:
        android_asset_source = ANDROID_ASSET_STORE.read_text(encoding="utf-8")
        for token in (
            "AAssetManager_open",
            "AAsset_getLength64",
            "AAsset_read",
        ):
            if token not in android_asset_source:
                errors.append(
                    "Android native asset store does not use " + token
                )

    obsolete_texture_loader = (
        REPO_ROOT
        / "app/src/main/java/com/cytarch/cellularity/platform/render/AndroidRenderTextureLoader.kt"
    )
    if obsolete_texture_loader.exists():
        errors.append(
            "Kotlin renderer texture loader still exists: "
            + obsolete_texture_loader.relative_to(REPO_ROOT).as_posix()
        )

    if not ANDROID_GL_RENDERER.is_file():
        errors.append(
            "Android GL lifecycle adapter is missing: "
            + ANDROID_GL_RENDERER.relative_to(REPO_ROOT).as_posix()
        )
    else:
        android_gl_renderer_source = ANDROID_GL_RENDERER.read_text(encoding="utf-8")
        if "GLSurfaceView.Renderer" not in android_gl_renderer_source:
            errors.append("Android GL lifecycle adapter does not own GLSurfaceView.Renderer")
        if "lifecycle.onSurfaceDestroyed()" not in android_gl_renderer_source:
            errors.append("Android GL lifecycle adapter does not forward surface destruction")

    direct_gl_renderers = []
    for source in (REPO_ROOT / "app/src/main").rglob("*"):
        if not source.is_file() or source.suffix not in {".java", ".kt"}:
            continue
        content = source.read_text(encoding="utf-8")
        if (
            "GLSurfaceView.Renderer" in content
            or "GLSurfaceView.Renderer;" in content
        ) and source != ANDROID_GL_RENDERER:
            direct_gl_renderers.append(source.relative_to(REPO_ROOT).as_posix())
    if direct_gl_renderers:
        errors.append(
            "feature renderers bypass the Android GL lifecycle adapter: "
            + ", ".join(sorted(direct_gl_renderers))
        )

    if not RENDER_SURFACE_CONTRACT.is_file():
        errors.append(
            "application render surface contract is missing: "
            + RENDER_SURFACE_CONTRACT.relative_to(REPO_ROOT).as_posix()
        )
    else:
        surface_contract_source = RENDER_SURFACE_CONTRACT.read_text(encoding="utf-8")
        forbidden_contract_tokens = (
            "android.",
            "ByteArray",
            "Environment",
            "Gene",
            "GLES",
            "GlApi",
            "InputStream",
            "OutputStream",
            "nativeHandle",
        )
        present = [
            token for token in forbidden_contract_tokens if token in surface_contract_source
        ]
        if present:
            errors.append(
                "application render surface contract contains domain/platform payloads: "
                + ", ".join(present)
            )

    obsolete_loader_symbols = (
        "com.cytarch.cellularity.shared.render",
        "NativeRenderTextureLoaderBridge",
        "CellularityRenderSurface",
    )
    active_source_suffixes = {".cpp", ".h", ".java", ".kt"}
    old_prefix = "composeResources/com.cytarch.cellularity.shared.generated.resources/files/"
    for root in RENDER_ASSET_SOURCE_ROOTS:
        for source in root.rglob("*"):
            if not source.is_file() or source.suffix not in active_source_suffixes:
                continue
            content = source.read_text(encoding="utf-8")
            if old_prefix in content:
                errors.append(
                    "renderer source still depends on the old Compose path: "
                    + source.relative_to(REPO_ROOT).as_posix()
                )
            present = [symbol for symbol in obsolete_loader_symbols if symbol in content]
            if present:
                errors.append(
                    "obsolete shared renderer bridge remains in "
                    + source.relative_to(REPO_ROOT).as_posix()
                    + ": "
                    + ", ".join(present)
                )

    hashes: dict[str, list[Path]] = defaultdict(list)
    for root in BINARY_RESOURCE_ROOTS:
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in BINARY_SUFFIXES:
                hashes[digest_file(path)].append(path.relative_to(REPO_ROOT))
    for paths in hashes.values():
        if len(paths) > 1:
            errors.append(
                "duplicate binary runtime resources: "
                + ", ".join(sorted(path.as_posix() for path in paths))
            )

    if errors:
        fail(errors)


def validate_shader_programs(expected_shader_programs: set[str]) -> None:
    validator = shutil.which("glslangValidator")
    if validator is None:
        fail(["glslangValidator is required for shader compile/link validation"])

    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="cellularity-render-shaders-") as temporary:
        temporary_root = Path(temporary)
        for name in sorted(expected_shader_programs):
            vertex = temporary_root / f"{name}.vert"
            fragment = temporary_root / f"{name}.frag"
            shutil.copyfile(ASSET_ROOT / "shaders/vertex" / f"{name}.glsl", vertex)
            shutil.copyfile(ASSET_ROOT / "shaders/fragment" / f"{name}.glsl", fragment)
            command = [validator]
            vertex_source = vertex.read_text(encoding="utf-8")
            fragment_source = fragment.read_text(encoding="utf-8")
            if not any(
                re.match(r"\s*#version\b", source)
                for source in (vertex_source, fragment_source)
            ):
                command.extend(("--glsl-version", "100"))
            command.extend(("-l", str(vertex), str(fragment)))
            result = subprocess.run(
                command,
                capture_output=True,
                check=False,
                text=True,
            )
            if result.returncode != 0:
                detail = "\n".join(part.strip() for part in (result.stdout, result.stderr) if part.strip())
                errors.append(f"shader program does not compile/link: {name}\n{detail}")
            elif re.search(r"\bwarning\s*:", result.stdout + result.stderr, re.IGNORECASE):
                errors.append(
                    f"shader program compiled with warnings: {name}\n"
                    f"{result.stdout.strip()}\n{result.stderr.strip()}"
                )

    if errors:
        fail(errors)


def verify_apk(
    entries: dict[str, str], vulkan_assets: set[str], apk: Path
) -> None:
    if not apk.is_file():
        fail([f"APK is missing: {apk}"])

    errors: list[str] = []
    with ZipFile(apk) as archive:
        names = set(archive.namelist())
        expected_names = {f"assets/{relative}" for relative in entries}
        renderer_names = {
            name
            for name in names
            if name.startswith("assets/shaders/") or name.startswith("assets/textures/")
        }
        missing = sorted(expected_names - renderer_names)
        unexpected = sorted(renderer_names - expected_names)
        if missing:
            errors.append(f"renderer assets missing from APK: {missing}")
        if unexpected:
            errors.append(f"unexpected renderer assets in APK: {unexpected}")

        for relative, expected_hash in entries.items():
            archive_name = f"assets/{relative}"
            if archive_name in names and digest_bytes(archive.read(archive_name)) != expected_hash:
                errors.append(f"renderer asset differs in APK: {archive_name}")

        expected_vulkan_names = {f"assets/{relative}" for relative in vulkan_assets}
        actual_vulkan_names = {
            name for name in names if name.startswith("assets/vulkan/shaders/")
        }
        if actual_vulkan_names != expected_vulkan_names:
            errors.append(
                "Vulkan shader assets differ in APK "
                f"(missing={sorted(expected_vulkan_names - actual_vulkan_names)}, "
                f"unexpected={sorted(actual_vulkan_names - expected_vulkan_names)})"
            )
        for archive_name in sorted(expected_vulkan_names & names):
            spirv = archive.read(archive_name)
            if len(spirv) < 4 or len(spirv) % 4 != 0 or spirv[:4] != b"\x03\x02#\x07":
                errors.append(f"invalid SPIR-V asset in APK: {archive_name}")

        stale = sorted(
            name
            for name in names
            if "/files/shaders/" in name
            or "/files/textures/" in name
            or (
                name.startswith("assets/composeResources/")
                and (
                    name.endswith("/gauss_bessel_integral.png")
                    or name.endswith("/small_dots.png")
                )
            )
        )
        if stale:
            errors.append(f"obsolete renderer asset copies are packaged: {stale}")

    if errors:
        fail(errors)


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify canonical renderer resources")
    parser.add_argument("--apk", type=Path, help="also verify packaged renderer assets")
    parser.add_argument(
        "--validate-shaders",
        action="store_true",
        help="compile and link every manifest shader pair with glslangValidator",
    )
    parser.add_argument(
        "--update-manifest",
        action="store_true",
        help="refresh canonical asset hashes only after resource verification succeeds",
    )
    arguments = parser.parse_args()

    _, expected_shader_programs = read_render_contract()
    vulkan_sources = read_vulkan_shader_contract()
    entries = read_manifest()
    if arguments.update_manifest:
        missing = [relative for relative in entries if not (ASSET_ROOT / relative).is_file()]
        if missing:
            fail([f"cannot refresh hashes of missing renderer assets: {missing}"])
        entries = {
            relative: digest_file(ASSET_ROOT / relative) for relative in entries
        }
    verify_source_assets(entries, expected_shader_programs)
    verify_vulkan_shader_sources(vulkan_sources)
    verify_vulkan_basic_cell_formula_parity()
    verify_link_shader_formula_parity()
    if arguments.validate_shaders:
        validate_shader_programs(expected_shader_programs)
    if arguments.apk is not None:
        verify_apk(entries, set(vulkan_sources.values()), arguments.apk)
    if arguments.update_manifest:
        manifest_lines: list[str] = []
        for raw_line in MANIFEST.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#"):
                manifest_lines.append(raw_line)
            else:
                relative = line.split(None, 1)[1]
                manifest_lines.append(f"{entries[relative]}  {relative}")
        MANIFEST.write_text(
            "\n".join(manifest_lines) + "\n",
            encoding="utf-8",
        )
    suffix = f" and {arguments.apk}" if arguments.apk is not None else ""
    shader_suffix = (
        f", {len(expected_shader_programs)} shader programs compile/link"
        if arguments.validate_shaders
        else ""
    )
    print(
        f"renderer resources verified: {len(entries)} canonical GLES assets, "
        f"{len(vulkan_sources)} Vulkan shader stages{shader_suffix}{suffix}"
    )


if __name__ == "__main__":
    main()
