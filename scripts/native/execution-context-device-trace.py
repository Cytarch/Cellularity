#!/usr/bin/env python3
"""Build and run the C-01 execution-context trace check on one explicit device."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
NATIVE_ROOT = REPO_ROOT / "native"
PROPERTY_KEYS = (
    "ro.product.cpu.abi",
    "ro.product.model",
    "ro.product.device",
    "ro.product.name",
    "ro.hardware",
    "ro.build.fingerprint",
    "ro.kernel.qemu",
    "ro.boot.qemu",
)


class TraceCheckError(RuntimeError):
    pass


def run(
    arguments: list[str],
    *,
    cwd: Path = REPO_ROOT,
    capture: bool = False,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        arguments,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
        check=False,
    )
    if check and result.returncode != 0:
        raise TraceCheckError(
            f"Command failed ({result.returncode}): {' '.join(arguments)}\n"
            f"{result.stdout or ''}"
        )
    return result


def find_executable(name: str, candidates: list[Path]) -> Path:
    discovered = shutil.which(name)
    values = ([Path(discovered)] if discovered else []) + candidates
    for value in values:
        if value.is_file():
            return value.resolve()
    raise TraceCheckError(f"{name} was not found")


def find_cmake() -> Path:
    return find_executable(
        "cmake", [Path.home() / "Android/Sdk/cmake/3.22.1/bin/cmake"]
    )


def find_ninja(cmake: Path) -> Path:
    return find_executable("ninja", [cmake.parent / "ninja"])


def find_adb() -> Path:
    candidates: list[Path] = []
    for variable in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        value = os.environ.get(variable)
        if value:
            candidates.append(Path(value) / "platform-tools/adb")
    candidates.append(Path.home() / "Android/Sdk/platform-tools/adb")
    return find_executable("adb", candidates)


def find_ndk() -> Path:
    candidates: list[Path] = []
    for variable in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT"):
        value = os.environ.get(variable)
        if value:
            candidates.append(Path(value))
    ndk_root = Path.home() / "Android/Sdk/ndk"
    if ndk_root.is_dir():
        candidates.extend(sorted(ndk_root.iterdir(), reverse=True))
    for candidate in candidates:
        toolchain = candidate / "build/cmake/android.toolchain.cmake"
        if toolchain.is_file():
            return candidate.resolve()
    raise TraceCheckError("Android NDK was not found")


def adb_run(
    adb: Path,
    serial: str,
    arguments: list[str],
    *,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    return run(
        [str(adb), "-s", serial, *arguments], capture=True, check=check
    )


def adb_text(
    adb: Path, serial: str, arguments: list[str], *, check: bool = True
) -> str:
    return adb_run(adb, serial, arguments, check=check).stdout.strip()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_source_state() -> dict[str, Any]:
    revision = run(["git", "rev-parse", "HEAD"], capture=True).stdout.strip()
    status = run(["git", "status", "--porcelain"], capture=True).stdout
    return {"revision": revision, "dirty": bool(status.strip()), "status": status}


def device_properties(adb: Path, serial: str) -> dict[str, str]:
    return {
        key: adb_text(adb, serial, ["shell", "getprop", key], check=False)
        for key in PROPERTY_KEYS
    }


def classify_device(serial: str, properties: dict[str, str]) -> dict[str, Any]:
    searchable = " ".join([serial, *properties.values()]).lower()
    virtual_markers: list[str] = []
    if serial.startswith("emulator-"):
        virtual_markers.append("emulator serial")
    if (
        properties.get("ro.kernel.qemu") == "1"
        or properties.get("ro.boot.qemu") == "1"
    ):
        virtual_markers.append("qemu property")
    for marker in ("waydroid", "goldfish", "ranchu", "cuttlefish", "sdk_gphone"):
        if marker in searchable:
            virtual_markers.append(marker)
    abi = properties.get("ro.product.cpu.abi", "")
    return {
        "abi": abi,
        "virtual": bool(virtual_markers),
        "virtualMarkers": sorted(set(virtual_markers)),
    }


def configure_and_build(
    build_dir: Path, cmake: Path, ninja: Path, ndk: Path, abi: str
) -> Path:
    run(
        [
            str(cmake),
            "-S",
            str(NATIVE_ROOT),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={abi}",
            "-DANDROID_PLATFORM=android-21",
            "-DANDROID_STL=c++_static",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCELLULARITY_BUILD_EXECUTION_CONTEXT_TRACE=ON",
            "-DCELLULARITY_ENABLE_RUNTIME_OVERLAP_SELECTION=OFF",
            "-DCELLULARITY_ENABLE_NATIVE_PROFILE=OFF",
            "-DCELLULARITY_ENABLE_IPO=OFF",
            "-DCELLULARITY_WARNINGS_AS_ERRORS=ON",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ]
    )
    run(
        [
            str(cmake),
            "--build",
            str(build_dir),
            "--target",
            "cellularity_execution_context_trace",
            "--parallel",
        ]
    )
    binary = build_dir / "cellularity_execution_context_trace"
    if not binary.is_file():
        raise TraceCheckError("Execution-context trace binary was not produced")
    return binary


def load_probe_result(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TraceCheckError(f"Unable to read device result {path}: {error}") from error
    if not isinstance(value, dict):
        raise TraceCheckError("Device result root is not an object")
    return value


def validate_probe_result(value: dict[str, Any], cycles: int) -> list[str]:
    errors: list[str] = []
    if value.get("schemaVersion") != 1:
        errors.append("unexpected probe schema")
    if value.get("passed") is not True:
        errors.append("device probe reported failure")
    cycle_values = value.get("cycles")
    if not isinstance(cycle_values, list) or len(cycle_values) != cycles:
        errors.append("device probe returned an unexpected cycle count")
        return errors
    baseline = value.get("baselineProcessThreadCount")
    for cycle_index, cycle in enumerate(cycle_values):
        if not isinstance(cycle, dict):
            errors.append(f"cycle {cycle_index} is not an object")
            continue
        participant_count = cycle.get("participantCount")
        participants = cycle.get("participants")
        if not isinstance(participant_count, int) or participant_count <= 1:
            errors.append(f"cycle {cycle_index} did not materialize a worker")
            continue
        if (
            not isinstance(participants, list)
            or len(participants) != participant_count
        ):
            errors.append(f"cycle {cycle_index} has an invalid participant list")
            continue
        for participant_index, participant in enumerate(participants):
            if not isinstance(participant, dict):
                errors.append(
                    f"cycle {cycle_index} participant {participant_index} is invalid"
                )
                continue
            expected = (
                "CellularitySim"
                if participant_index == 0
                else f"CellSimW{participant_index}"
            )
            if participant.get("name") != expected:
                errors.append(
                    f"cycle {cycle_index} participant {participant_index} "
                    f"has name {participant.get('name')!r}, expected {expected!r}"
                )
            if participant.get("workerIndex") != participant_index:
                errors.append(
                    f"cycle {cycle_index} participant {participant_index} "
                    "has an unexpected worker index"
                )
        if (
            isinstance(baseline, int)
            and cycle.get("processThreadsAfterRelease", baseline + 1) > baseline
        ):
            errors.append(f"cycle {cycle_index} retained a worker after release")
    final_snapshot = value.get("finalSnapshot")
    if not isinstance(final_snapshot, dict):
        errors.append("final execution-context snapshot is missing")
    elif (
        final_snapshot.get("ownerThreadBound") is not False
        or final_snapshot.get("poolMaterialized") is not False
        or final_snapshot.get("poolConstructionCount") != cycles
        or final_snapshot.get("poolDestructionCount") != cycles
    ):
        errors.append("final execution-context lifecycle snapshot is invalid")
    return errors


def expected_trace_names(probe: dict[str, Any]) -> list[str]:
    names: set[str] = set()
    for cycle in probe.get("cycles", []):
        if not isinstance(cycle, dict):
            continue
        for participant in cycle.get("participants", []):
            if isinstance(participant, dict) and isinstance(
                participant.get("name"), str
            ):
                names.add(participant["name"])
    return sorted(names)


def write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def execute_device_check(
    *,
    adb: Path,
    serial: str,
    binary: Path,
    output_dir: Path,
    source: dict[str, Any],
    properties: dict[str, str],
    classification: dict[str, Any],
    additional_threads: int,
    cycles: int,
    duration_ms: int,
) -> dict[str, Any]:
    fingerprint = source["revision"][:12]
    remote_root = f"/data/local/tmp/cellularity-c01-{fingerprint}"
    if not fingerprint or not all(
        character in "0123456789abcdef" for character in fingerprint
    ):
        raise TraceCheckError("Unsafe source revision for remote directory")
    remote_binary = f"{remote_root}/execution-context-trace"
    remote_result = f"{remote_root}/result.json"
    remote_trace = f"{remote_root}/sched-trace.txt"
    local_result = output_dir / "device-result.json"
    local_trace = output_dir / "sched-trace.txt"
    trace_started = False
    start_result: subprocess.CompletedProcess[str] | None = None
    stop_result: subprocess.CompletedProcess[str] | None = None
    probe_process: subprocess.CompletedProcess[str] | None = None
    cleanup_output = ""

    try:
        for local_path in (local_result, local_trace):
            local_path.unlink(missing_ok=True)
        # The suffix is a validated hexadecimal commit prefix, so this removes
        # only a prior run of this exact checker and cannot target a broad path.
        adb_run(adb, serial, ["shell", "rm", "-rf", remote_root], check=False)
        adb_text(adb, serial, ["shell", "mkdir", "-p", remote_root])
        adb_text(adb, serial, ["push", str(binary), remote_binary])
        adb_text(adb, serial, ["shell", "chmod", "755", remote_binary])
        start_result = adb_run(
            adb,
            serial,
            [
                "shell",
                "atrace",
                "--async_start",
                "-b",
                "16384",
                "sched",
            ],
            check=False,
        )
        start_output_lower = (start_result.stdout or "").lower()
        if (
            start_result.returncode != 0
            or "error" in start_output_lower
            or "already active" in start_output_lower
        ):
            raise TraceCheckError(
                "Unable to start Android scheduler trace:\n"
                + (start_result.stdout or "")
            )
        trace_started = True
        probe_process = adb_run(
            adb,
            serial,
            [
                "shell",
                remote_binary,
                "--additional-threads",
                str(additional_threads),
                "--cycles",
                str(cycles),
                "--duration-ms",
                str(duration_ms),
                "--output",
                remote_result,
            ],
            check=False,
        )
    finally:
        if trace_started:
            stop_result = adb_run(
                adb,
                serial,
                ["shell", "atrace", "--async_stop", "-o", remote_trace],
                check=False,
            )
        adb_run(adb, serial, ["pull", remote_result, str(local_result)], check=False)
        adb_run(adb, serial, ["pull", remote_trace, str(local_trace)], check=False)
        cleanup = adb_run(
            adb, serial, ["shell", "rm", "-rf", remote_root], check=False
        )
        cleanup_output = cleanup.stdout or ""

    if probe_process is None:
        raise TraceCheckError("Device probe did not run")
    if not local_result.is_file():
        raise TraceCheckError(
            f"Device probe produced no result (exit {probe_process.returncode}):\n"
            f"{probe_process.stdout or ''}"
        )
    if (
        stop_result is None
        or stop_result.returncode != 0
        or not local_trace.is_file()
    ):
        raise TraceCheckError(
            "Android scheduler trace was not captured:\n"
            + ((stop_result.stdout if stop_result else "trace stop did not run") or "")
        )

    probe = load_probe_result(local_result)
    validation_errors = validate_probe_result(probe, cycles)
    trace_text = local_trace.read_text(encoding="utf-8", errors="replace")
    expected_names = expected_trace_names(probe)
    missing_trace_names = [
        name for name in expected_names if name not in trace_text
    ]
    if missing_trace_names:
        validation_errors.append(
            "scheduler trace is missing thread names: " + ", ".join(missing_trace_names)
        )
    if probe_process.returncode != 0:
        validation_errors.append(
            f"device probe exited with {probe_process.returncode}: "
            f"{(probe_process.stdout or '').strip()}"
        )

    return {
        "schemaVersion": 2,
        "scenario": "c01/execution-context-device-trace-capture",
        "capturedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "passed": not validation_errors,
        "source": {key: value for key, value in source.items() if key != "status"},
        "device": {
            "serial": serial,
            "properties": properties,
            "classification": classification,
        },
        "binary": {
            "sha256": sha256_file(binary),
            "sizeBytes": binary.stat().st_size,
        },
        "configuration": {
            "additionalThreads": additional_threads,
            "cycles": cycles,
            "durationMsPerCycle": duration_ms,
        },
        "probe": probe,
        "trace": {
            "path": local_trace.name,
            "sha256": sha256_file(local_trace),
            "sizeBytes": local_trace.stat().st_size,
            "expectedThreadNames": expected_names,
            "missingThreadNames": missing_trace_names,
            "startOutput": (start_result.stdout or "") if start_result else "",
            "stopOutput": (stop_result.stdout or "") if stop_result else "",
        },
        "cleanupOutput": cleanup_output,
        "validationErrors": validation_errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--android-serial", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--additional-threads", type=int, default=2)
    parser.add_argument("--cycles", type=int, default=2)
    parser.add_argument("--duration-ms", type=int, default=1500)
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()

    if not 1 <= args.additional_threads <= 7:
        raise TraceCheckError("--additional-threads must be in [1, 7]")
    if not 1 <= args.cycles <= 8:
        raise TraceCheckError("--cycles must be in [1, 8]")
    if not 100 <= args.duration_ms <= 10_000:
        raise TraceCheckError("--duration-ms must be in [100, 10000]")

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    source = git_source_state()
    if source["dirty"] and not args.allow_dirty:
        raise TraceCheckError(
            "Source tree is dirty; commit the exact trace candidate or pass "
            "--allow-dirty for a non-verdict development run"
        )

    adb = find_adb()
    if adb_text(adb, args.android_serial, ["get-state"], check=False) != "device":
        raise TraceCheckError(
            f"Android target {args.android_serial!r} is not connected"
        )
    properties = device_properties(adb, args.android_serial)
    classification = classify_device(args.android_serial, properties)
    abi = classification["abi"]
    if abi not in ("arm64-v8a", "armeabi-v7a", "x86", "x86_64"):
        raise TraceCheckError(f"Unsupported Android ABI: {abi!r}")

    cmake = find_cmake()
    ninja = find_ninja(cmake)
    ndk = find_ndk()
    build_dir = output_dir / f"build-{abi}"
    binary = configure_and_build(build_dir, cmake, ninja, ndk, abi)
    preserved_binary = output_dir / "cellularity_execution_context_trace"
    shutil.copy2(binary, preserved_binary)

    report = execute_device_check(
        adb=adb,
        serial=args.android_serial,
        binary=binary,
        output_dir=output_dir,
        source=source,
        properties=properties,
        classification=classification,
        additional_threads=args.additional_threads,
        cycles=args.cycles,
        duration_ms=args.duration_ms,
    )
    report["binary"]["preservedPath"] = preserved_binary.name
    report["build"] = {
        "abi": abi,
        "ndk": str(ndk),
        "cmake": str(cmake),
        "cachePath": str(build_dir / "CMakeCache.txt"),
        "compileCommandsPath": str(build_dir / "compile_commands.json"),
    }
    write_json(output_dir / "report.json", report)
    if report["passed"]:
        print("C-01 Android execution-context trace capture: PASS")
        return 0
    print("C-01 execution-context trace: FAIL", file=sys.stderr)
    for error in report["validationErrors"]:
        print(f"- {error}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except TraceCheckError as error:
        print(f"execution-context device trace error: {error}", file=sys.stderr)
        raise SystemExit(2)
