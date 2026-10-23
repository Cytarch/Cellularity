#!/usr/bin/env python3
"""Verify that native profiling code is absent from Profile-OFF objects."""

from __future__ import annotations

import argparse
import hashlib
import json
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


PROFILED_SOURCES = (
    ("native/core/SimulationWorld.cpp", "CELLULARITY_PROFILE", True, True),
    ("native/core/CellData.cpp", "CELLULARITY_PROFILE", True, True),
    ("native/core/CellSweepBroadphase.cpp", "CELLULARITY_PROFILE", True, True),
    (
        "native/render/frame/RenderSourceCapture.cpp",
        "CELLULARITY_RENDER_PROFILE",
        True,
        False,
    ),
    (
        "native/render/frame/RenderFrameBuilder.cpp",
        "CELLULARITY_RENDER_PROFILE",
        True,
        False,
    ),
    (
        "native/render/NativeSimulationWorldRenderer.cpp",
        "CELLULARITY_RENDER_PROFILE",
        False,
        False,
    ),
)

FORBIDDEN_OFF_SYMBOL_FRAGMENTS = (
    "std::chrono::_V2::steady_clock::now",
    "std::chrono::steady_clock::now",
    "CoarseRegionTimer",
    "cellularityProfileRegion_",
)


class VerificationError(RuntimeError):
    pass


def read_compile_database(build_directory: Path) -> list[dict[str, Any]]:
    path = build_directory / "compile_commands.json"
    try:
        with path.open("r", encoding="utf-8") as source:
            value = json.load(source)
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"Could not read {path}: {error}") from error
    if not isinstance(value, list):
        raise VerificationError(f"Expected a JSON array in {path}")
    return value


def command_tokens(entry: dict[str, Any]) -> list[str]:
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and all(isinstance(value, str) for value in arguments):
        return arguments
    command = entry.get("command")
    if isinstance(command, str):
        return shlex.split(command)
    raise VerificationError(f"Compile entry has no command: {entry.get('file')}")


def compile_entry(
    database: list[dict[str, Any]], source_suffix: str
) -> dict[str, Any]:
    matches = [
        entry
        for entry in database
        if isinstance(entry.get("file"), str)
        and Path(entry["file"]).as_posix().endswith(source_suffix)
    ]
    if len(matches) > 1:
        primary_matches = [
            entry
            for entry in matches
            if not any(
                token == "-DCELLULARITY_SEMANTIC_TRACE=1"
                or token == "-DCELLULARITY_SEMANTIC_TRACE"
                for token in command_tokens(entry)
            )
        ]
        if len(primary_matches) == 1:
            return primary_matches[0]
    if len(matches) != 1:
        raise VerificationError(
            f"Expected exactly one compile entry for {source_suffix}, found {len(matches)}"
        )
    return matches[0]


def object_path(entry: dict[str, Any]) -> Path:
    tokens = command_tokens(entry)
    try:
        output_index = tokens.index("-o") + 1
        output = Path(tokens[output_index])
    except (ValueError, IndexError) as error:
        raise VerificationError(
            f"Could not find object output for {entry.get('file')}"
        ) from error
    directory = Path(entry.get("directory", "."))
    path = output if output.is_absolute() else directory / output
    path = path.resolve()
    if not path.is_file():
        raise VerificationError(f"Object file does not exist: {path}")
    return path


def macros(entry: dict[str, Any]) -> set[str]:
    result: set[str] = set()
    tokens = command_tokens(entry)
    index = 0
    while index < len(tokens):
        token = tokens[index]
        definition: str | None = None
        if token == "-D" and index + 1 < len(tokens):
            index += 1
            definition = tokens[index]
        elif token.startswith("-D"):
            definition = token[2:]
        if definition:
            result.add(definition.partition("=")[0])
        index += 1
    return result


def run(command: list[str]) -> str:
    result = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise VerificationError(
            f"Command failed with exit code {result.returncode}: {' '.join(command)}\n"
            f"{result.stdout.strip()}"
        )
    return result.stdout


def normalized_disassembly(objdump: str, path: Path) -> str:
    output = run(
        [
            objdump,
            "--disassemble",
            "--demangle",
            "--no-show-raw-insn",
            str(path),
        ]
    )
    lines = output.splitlines()
    while lines and (not lines[0].strip() or "file format" in lines[0]):
        lines.pop(0)
    return "\n".join(lines).strip() + "\n"


def digest_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def inspect_all_profile_off_objects(
    database: list[dict[str, Any]], nm: str
) -> tuple[dict[str, Any], list[str]]:
    inspected: list[dict[str, str]] = []
    violations: list[dict[str, Any]] = []
    errors: list[str] = []
    for entry in database:
        source_value = entry.get("file")
        if not isinstance(source_value, str):
            continue
        source = Path(source_value).resolve()
        source_text = source.as_posix()
        if "/native/core/" not in source_text and "/native/render/" not in source_text:
            continue

        obj = object_path(entry)
        symbols = run([nm, "-C", str(obj)])
        forbidden = [
            fragment
            for fragment in FORBIDDEN_OFF_SYMBOL_FRAGMENTS
            if fragment in symbols
        ]
        inspected.append({"source": str(source), "object": str(obj)})
        if forbidden:
            violation = {
                "source": str(source),
                "object": str(obj),
                "forbiddenSymbols": forbidden,
            }
            violations.append(violation)
            errors.append(
                f"{source}: forbidden Profile-OFF symbols: {', '.join(forbidden)}"
            )

    return (
        {
            "inspectedObjectCount": len(inspected),
            "scope": ["native/core", "native/render"],
            "forbiddenSymbolFragments": list(FORBIDDEN_OFF_SYMBOL_FRAGMENTS),
            "violations": violations,
        },
        errors,
    )


def verify(
    profile_off_build: Path,
    profile_on_build: Path,
    reference_off_build: Path | None,
) -> dict[str, Any]:
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    objdump = shutil.which("llvm-objdump") or shutil.which("objdump")
    if nm is None or objdump is None:
        raise VerificationError("llvm-nm/nm and llvm-objdump/objdump are required")

    off_database = read_compile_database(profile_off_build)
    on_database = read_compile_database(profile_on_build)
    reference_database = (
        read_compile_database(reference_off_build)
        if reference_off_build is not None
        else None
    )

    source_reports: list[dict[str, Any]] = []
    errors: list[str] = []
    all_object_report, all_object_errors = inspect_all_profile_off_objects(
        off_database, nm
    )
    errors.extend(all_object_errors)
    for source, expected_macro, expects_clock, compare_reference in PROFILED_SOURCES:
        off_entry = compile_entry(off_database, source)
        on_entry = compile_entry(on_database, source)
        off_object = object_path(off_entry)
        on_object = object_path(on_entry)
        off_macros = macros(off_entry)
        on_macros = macros(on_entry)
        off_symbols = run([nm, "-C", str(off_object)])
        on_symbols = run([nm, "-C", str(on_object)])

        macro_absent_off = expected_macro not in off_macros
        macro_present_on = expected_macro in on_macros
        forbidden_symbols = [
            fragment
            for fragment in FORBIDDEN_OFF_SYMBOL_FRAGMENTS
            if fragment in off_symbols
        ]
        on_has_clock = (
            "steady_clock::now" in on_symbols
            or "steady_clock::now" in normalized_disassembly(objdump, on_object)
        )

        if not macro_absent_off:
            errors.append(f"{source}: {expected_macro} is defined in Profile-OFF")
        if not macro_present_on:
            errors.append(f"{source}: {expected_macro} is missing in Profile-ON")
        if forbidden_symbols:
            errors.append(
                f"{source}: forbidden Profile-OFF symbols: {', '.join(forbidden_symbols)}"
            )
        if expects_clock and not on_has_clock:
            errors.append(f"{source}: Profile-ON object has no steady clock call")

        reference_report: dict[str, Any] = {"checked": False}
        if reference_database is not None and compare_reference:
            reference_entry = compile_entry(reference_database, source)
            reference_object = object_path(reference_entry)
            current_disassembly = normalized_disassembly(objdump, off_object)
            reference_disassembly = normalized_disassembly(objdump, reference_object)
            current_digest = digest_text(current_disassembly)
            reference_digest = digest_text(reference_disassembly)
            identical = current_disassembly == reference_disassembly
            reference_report = {
                "checked": True,
                "identical": identical,
                "currentDisassemblySha256": current_digest,
                "referenceDisassemblySha256": reference_digest,
                "referenceObject": str(reference_object),
            }
            if not identical:
                errors.append(
                    f"{source}: Profile-OFF disassembly differs from the reference build"
                )

        source_reports.append(
            {
                "source": source,
                "expectedMacro": expected_macro,
                "macroAbsentInProfileOff": macro_absent_off,
                "macroPresentInProfileOn": macro_present_on,
                "profileOffObject": str(off_object),
                "profileOnObject": str(on_object),
                "forbiddenProfileOffSymbols": forbidden_symbols,
                "profileOnHasSteadyClock": on_has_clock,
                "referenceDisassembly": reference_report,
            }
        )

    return {
        "schemaVersion": 1,
        "profileOffBuild": str(profile_off_build.resolve()),
        "profileOnBuild": str(profile_on_build.resolve()),
        "referenceOffBuild": (
            str(reference_off_build.resolve())
            if reference_off_build is not None
            else None
        ),
        "sources": source_reports,
        "allProfileOffCoreAndRenderObjects": all_object_report,
        "errors": errors,
        "passed": not errors,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile-off-build", type=Path, required=True)
    parser.add_argument("--profile-on-build", type=Path, required=True)
    parser.add_argument("--reference-off-build", type=Path)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    try:
        report = verify(
            arguments.profile_off_build,
            arguments.profile_on_build,
            arguments.reference_off_build,
        )
    except VerificationError as error:
        print(f"profile boundary verification failed: {error}", file=sys.stderr)
        return 2

    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
