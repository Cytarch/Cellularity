#!/usr/bin/env python3
"""Capture and compare reproducible host simulation performance samples."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


SCRIPT_PATH = Path(__file__).resolve()
REPO_ROOT = SCRIPT_PATH.parents[2]
CONFIGURE_SCRIPT = REPO_ROOT / "scripts" / "native" / "configure.sh"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / ".codex" / "performance"
STATE_PHASES = ("initial", "measuredStart", "final")


class BenchmarkError(RuntimeError):
    pass


def utc_timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False)
        output.write("\n")
    temporary.replace(path)


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as source:
            value = json.load(source)
    except (OSError, json.JSONDecodeError) as error:
        raise BenchmarkError(f"Could not read JSON report {path}: {error}") from error
    if not isinstance(value, dict):
        raise BenchmarkError(f"Expected a JSON object in {path}")
    return value


def command_output(command: list[str], *, check: bool = True) -> str:
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if check and result.returncode != 0:
        raise BenchmarkError(
            f"Command failed with exit code {result.returncode}: {' '.join(command)}\n"
            f"{result.stdout.strip()}"
        )
    return result.stdout.strip()


def run_logged(command: list[str], environment: dict[str, str], log_path: Path) -> None:
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            env=environment,
            text=True,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    if result.returncode != 0:
        tail = log_path.read_text(encoding="utf-8", errors="replace").splitlines()[-40:]
        raise BenchmarkError(
            f"Command failed with exit code {result.returncode}: {' '.join(command)}\n"
            f"Build log: {log_path}\n" + "\n".join(tail)
        )


def executable_path(value: str) -> Path | None:
    candidate = Path(value).expanduser()
    if candidate.is_file() and os.access(candidate, os.X_OK):
        return candidate.resolve()
    resolved = shutil.which(value)
    return Path(resolved).resolve() if resolved else None


def android_sdk_roots() -> list[Path]:
    roots: list[Path] = []
    for variable in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        if os.environ.get(variable):
            roots.append(Path(os.environ[variable]).expanduser())
    local_properties = REPO_ROOT / "local.properties"
    if local_properties.is_file():
        for line in local_properties.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("sdk.dir="):
                roots.append(Path(line.partition("=")[2].replace("\\\\", "\\")).expanduser())
    roots.append(Path.home() / "Android" / "Sdk")
    unique: list[Path] = []
    for root in roots:
        resolved = root.resolve()
        if resolved not in unique:
            unique.append(resolved)
    return unique


def version_key(path: Path) -> tuple[int, ...]:
    values = tuple(int(value) for value in re.findall(r"\d+", path.parent.parent.name))
    return values or (0,)


def find_cmake() -> Path:
    configured = os.environ.get("CMAKE_COMMAND")
    if configured:
        result = executable_path(configured)
        if result:
            return result
        raise BenchmarkError(f"CMAKE_COMMAND is not executable: {configured}")
    from_path = executable_path("cmake")
    if from_path:
        return from_path
    bundled: list[Path] = []
    for sdk_root in android_sdk_roots():
        bundled.extend(sdk_root.glob("cmake/*/bin/cmake"))
    bundled = [path.resolve() for path in bundled if path.is_file() and os.access(path, os.X_OK)]
    if not bundled:
        raise BenchmarkError("CMake was not found in PATH or an Android SDK installation")
    return max(bundled, key=version_key)


def source_fingerprint() -> dict[str, Any]:
    """Hash every non-ignored source/build input for native and Android binaries."""
    listed = subprocess.run(
        [
            "git", "ls-files", "--cached", "--others", "--exclude-standard", "-z", "--",
            "native", "scripts", "gradle", "buildSrc", "app", "shared",
        ],
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if listed.returncode != 0:
        raise BenchmarkError(listed.stderr.decode("utf-8", errors="replace").strip())
    files: set[Path] = {
        REPO_ROOT / value.decode("utf-8", errors="surrogateescape")
        for value in listed.stdout.split(b"\0")
        if value
    }

    for pattern in (
        "*.gradle",
        "*.gradle.kts",
        "gradle.properties",
        "gradlew",
        "gradlew.bat",
    ):
        files.update(path for path in REPO_ROOT.glob(pattern) if path.is_file())

    ordered = sorted(
        (path for path in files if path.is_file()),
        key=lambda path: path.relative_to(REPO_ROOT).as_posix(),
    )
    digest = hashlib.sha256()
    total_bytes = 0
    entries: list[dict[str, Any]] = []
    for path in ordered:
        relative_text = path.relative_to(REPO_ROOT).as_posix()
        relative = relative_text.encode("utf-8")
        content = path.read_bytes()
        mode = path.stat().st_mode & 0o777
        digest.update(relative)
        digest.update(b"\0")
        digest.update(mode.to_bytes(4, byteorder="big"))
        digest.update(len(content).to_bytes(8, byteorder="big"))
        digest.update(content)
        total_bytes += len(content)
        entries.append(
            {
                "path": relative_text,
                "sha256": hashlib.sha256(content).hexdigest(),
                "sizeBytes": len(content),
                "mode": f"{mode:04o}",
            }
        )
    return {
        "schemaVersion": 1,
        "sha256": digest.hexdigest(),
        "fileCount": len(ordered),
        "byteCount": total_bytes,
        "entries": entries,
    }


def working_tree_fingerprint() -> dict[str, Any]:
    """Hash the complete tracked plus non-ignored untracked working tree."""
    listed = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if listed.returncode != 0:
        raise BenchmarkError(listed.stderr.decode("utf-8", errors="replace").strip())
    relative_paths = sorted(
        value.decode("utf-8", errors="surrogateescape")
        for value in listed.stdout.split(b"\0")
        if value
    )
    digest = hashlib.sha256()
    total_bytes = 0
    entries: list[dict[str, Any]] = []
    for relative_text in relative_paths:
        path = REPO_ROOT / relative_text
        if path.is_symlink():
            content = os.readlink(path).encode("utf-8", errors="surrogateescape")
            kind = "symlink"
            mode = 0o120000
        elif path.is_file():
            content = path.read_bytes()
            kind = "file"
            mode = path.stat().st_mode & 0o777
        else:
            content = b""
            kind = "missing"
            mode = 0
        relative = relative_text.encode("utf-8", errors="surrogateescape")
        digest.update(relative)
        digest.update(b"\0")
        digest.update(kind.encode("ascii"))
        digest.update(b"\0")
        digest.update(mode.to_bytes(4, byteorder="big"))
        digest.update(len(content).to_bytes(8, byteorder="big"))
        digest.update(content)
        total_bytes += len(content)
        entries.append({
            "path": relative_text,
            "kind": kind,
            "mode": f"{mode:06o}",
            "sizeBytes": len(content),
            "sha256": hashlib.sha256(content).hexdigest(),
        })
    return {
        "schemaVersion": 1,
        "sha256": digest.hexdigest(),
        "fileCount": len(entries),
        "byteCount": total_bytes,
        "entries": entries,
    }


def git_metadata() -> dict[str, Any]:
    revision = command_output(["git", "rev-parse", "--verify", "HEAD"], check=False) or "unavailable"
    status = command_output(["git", "status", "--porcelain", "--untracked-files=normal"], check=False)
    return {
        "revision": revision,
        "headTree": command_output(["git", "rev-parse", "HEAD^{tree}"], check=False) or "unavailable",
        "branch": command_output(["git", "branch", "--show-current"], check=False) or "detached",
        "dirty": bool(status),
        "statusShort": status.splitlines(),
    }


def cpu_model() -> str:
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
            key, separator, value = line.partition(":")
            if separator and key.strip() in ("model name", "Hardware"):
                return value.strip()
    return platform.processor() or "unknown"


def cpu_governors() -> list[str]:
    values = set()
    for path in Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_governor"):
        try:
            values.add(path.read_text(encoding="utf-8").strip())
        except OSError:
            pass
    return sorted(value for value in values if value)


def host_metadata() -> dict[str, Any]:
    return {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "cpuModel": cpu_model(),
        "logicalCpuCount": os.cpu_count(),
        "cpuGovernors": cpu_governors(),
        "pythonVersion": platform.python_version(),
        "timer": "benchmark binary: std::chrono::steady_clock",
    }


def first_line(value: str) -> str:
    return value.splitlines()[0] if value else "unavailable"


def toolchain_metadata(cmake: Path, environment: dict[str, str]) -> dict[str, Any]:
    compiler = environment.get("CELLULARITY_NATIVE_CXX_COMPILER", "clang++")
    ninja = shutil.which("ninja", path=environment.get("PATH"))
    return {
        "cmake": first_line(command_output([str(cmake), "--version"])),
        "cmakePath": str(cmake),
        "compiler": first_line(command_output([compiler, "--version"])),
        "compilerCommand": compiler,
        "ninja": first_line(command_output([ninja, "--version"])) if ninja else "unavailable",
        "buildType": "Release",
        "ipo": True,
        "warningsAsErrors": True,
    }


def benchmark_environment(
    cmake: Path,
    build_dir: Path,
    profile_enabled: bool,
    algorithm: str,
) -> dict[str, str]:
    environment = os.environ.copy()
    environment["PATH"] = str(cmake.parent) + os.pathsep + environment.get("PATH", "")
    environment.update(
        {
            "LC_ALL": "C",
            "CELLULARITY_NATIVE_BUILD_DIR": str(build_dir),
            "CELLULARITY_NATIVE_BUILD_TYPE": "Release",
            "CELLULARITY_NATIVE_PROFILE": "ON" if profile_enabled else "OFF",
            "CELLULARITY_NATIVE_SANITIZERS": "OFF",
            "CELLULARITY_NATIVE_WARNINGS_AS_ERRORS": "ON",
            "CELLULARITY_NATIVE_IPO": "ON",
            "CELLULARITY_NATIVE_OVERLAP_SCAN_ALGORITHM": algorithm,
            "CELLULARITY_NATIVE_RUNTIME_OVERLAP_SELECTION": "OFF",
            "CELLULARITY_NATIVE_CELL_OWNED_SIGNAL_KERNEL": "OFF",
            "CELLULARITY_NATIVE_EXPORT_COMPILE_COMMANDS": "ON",
        }
    )
    return environment


def build_variant(
    name: str,
    profile_enabled: bool,
    algorithm: str,
    fingerprint: str,
    cmake: Path,
    output_dir: Path,
    build_root: Path,
) -> tuple[Path, dict[str, str], Path]:
    build_dir = build_root / f"cellularity-ab-{fingerprint[:16]}-{algorithm}-{name}"
    environment = benchmark_environment(cmake, build_dir, profile_enabled, algorithm)
    logs = output_dir / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    print(f"Building {name} benchmark in {build_dir}", flush=True)
    run_logged([str(CONFIGURE_SCRIPT)], environment, logs / f"configure-{name}.log")
    run_logged(
        [str(cmake), "--build", str(build_dir), "--target", "cellularity_perf", "--parallel"],
        environment,
        logs / f"build-{name}.log",
    )
    binary = build_dir / "cellularity_perf"
    if not binary.is_file():
        raise BenchmarkError(f"Build did not produce {binary}")
    return binary, environment, build_dir


def preserve_build_metadata(build_dir: Path, output_dir: Path, name: str) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "buildDirectory": str(build_dir),
        "compileCommandsAvailable": False,
    }
    for source_name, destination_name in (
        ("compile_commands.json", f"compile-commands-{name}.json"),
        ("CMakeCache.txt", f"cmake-cache-{name}.txt"),
    ):
        source = build_dir / source_name
        if not source.is_file():
            continue
        destination = output_dir / "build-metadata" / destination_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        metadata[source_name] = {
            "path": destination.relative_to(output_dir).as_posix(),
            "sha256": sha256_file(destination),
            "sizeBytes": destination.stat().st_size,
        }
        if source_name == "compile_commands.json":
            metadata["compileCommandsAvailable"] = True
    return metadata


def copy_binary(source: Path, destination: Path) -> dict[str, Any]:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    destination.chmod(destination.stat().st_mode | 0o100)
    return {
        "sha256": sha256_file(destination),
        "sizeBytes": destination.stat().st_size,
    }


def repo_argument(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return str(resolved)


def validate_report(
    report: dict[str, Any],
    report_path: Path,
    scenario: str,
    warmup_steps: int,
    profile_steps: int,
) -> None:
    if report.get("schemaVersion", 0) < 2:
        raise BenchmarkError(f"{report_path} has no state-digest schema")
    if report.get("scenario") != scenario:
        raise BenchmarkError(f"Unexpected scenario in {report_path}: {report.get('scenario')}")
    if report.get("warmupSteps") != warmup_steps or report.get("profileSteps") != profile_steps:
        raise BenchmarkError(f"Update-count mismatch in {report_path}")
    steps_per_second = report.get("stepsPerSecond")
    if not isinstance(steps_per_second, (int, float)) or not math.isfinite(steps_per_second):
        raise BenchmarkError(f"Invalid stepsPerSecond in {report_path}")
    state = report.get("state")
    if not isinstance(state, dict):
        raise BenchmarkError(f"Missing state in {report_path}")
    for phase in STATE_PHASES:
        phase_state = state.get(phase)
        if not isinstance(phase_state, dict):
            raise BenchmarkError(f"Missing state.{phase} in {report_path}")
        digest = phase_state.get("digest")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise BenchmarkError(f"Invalid state.{phase}.digest in {report_path}")
        if phase_state.get("finite") is not True:
            raise BenchmarkError(f"Non-finite state.{phase} in {report_path}")


def run_benchmark(
    binary: Path,
    substrate: Path,
    mode: str,
    warmup_steps: int,
    profile_steps: int,
    output_path: Path,
) -> dict[str, Any]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(binary),
        repo_argument(substrate),
        mode,
        str(warmup_steps),
        str(profile_steps),
        str(output_path),
    ]
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise BenchmarkError(
            f"Benchmark failed with exit code {result.returncode}: {' '.join(command)}\n"
            f"{result.stdout.strip()}"
        )
    report = load_json(output_path)
    scenario = "simulation/full_update" if mode == "profile-json" else "simulation/stage_breakdown"
    validate_report(report, output_path, scenario, warmup_steps, profile_steps)
    return report


def percentile(sorted_values: list[float], quantile: float) -> float:
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = (len(sorted_values) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[lower]
    fraction = position - lower
    return sorted_values[lower] * (1.0 - fraction) + sorted_values[upper] * fraction


def summarize(values: Iterable[float], *, relative_range: bool = True) -> dict[str, Any]:
    samples = [float(value) for value in values]
    if not samples:
        raise BenchmarkError("Cannot summarize an empty sample")
    ordered = sorted(samples)
    median = statistics.median(ordered)
    relative_scale = abs(median)
    result = {
        "samples": samples,
        "count": len(samples),
        "minimum": ordered[0],
        "p05": percentile(ordered, 0.05),
        "median": median,
        "mean": statistics.fmean(ordered),
        "p95": percentile(ordered, 0.95),
        "maximum": ordered[-1],
        "populationStdDev": statistics.pstdev(ordered),
    }
    if relative_range:
        result["relativeRangePercent"] = (
            (ordered[-1] - ordered[0]) / relative_scale * 100.0
        ) if relative_scale else None
    return result


def assert_equivalent_state(reports: list[dict[str, Any]], context: str) -> dict[str, Any]:
    if not reports:
        raise BenchmarkError(f"No reports to validate for {context}")
    reference = reports[0]["state"]
    for index, report in enumerate(reports[1:], start=2):
        if report["state"] != reference:
            raise BenchmarkError(
                f"Simulation state differs in {context} run {index}; performance comparison is invalid"
            )
    return reference


def assert_compiled_algorithm(
    reports: list[dict[str, Any]], algorithm: str, context: str
) -> None:
    expected = f"{algorithm}-sweep"
    for index, report in enumerate(reports, start=1):
        if report.get("overlapBroadphaseAlgorithm") != expected:
            raise BenchmarkError(
                f"{context} run {index} reported "
                f"{report.get('overlapBroadphaseAlgorithm')!r}; expected {expected!r}"
            )


def summarize_stages(reports: list[dict[str, Any]]) -> dict[str, Any]:
    stage_names = sorted(reports[0].get("stages", {}).keys())
    if any(sorted(report.get("stages", {}).keys()) != stage_names for report in reports):
        raise BenchmarkError("Profile stage sets differ between runs")
    return {
        stage: {
            "avgMs": summarize(report["stages"][stage]["avgMs"] for report in reports),
            "sharePercent": summarize(report["stages"][stage]["share"] for report in reports),
        }
        for stage in stage_names
    }


def relative_paths(paths: list[Path], base: Path) -> list[str]:
    return [path.relative_to(base).as_posix() for path in paths]


def collect_runs(
    binary: Path,
    substrate: Path,
    mode: str,
    warmup_steps: int,
    profile_steps: int,
    count: int,
    run_directory: Path,
    prefix: str,
) -> tuple[list[dict[str, Any]], list[Path]]:
    reports: list[dict[str, Any]] = []
    paths: list[Path] = []
    for index in range(1, count + 1):
        print(f"Running {prefix} {index}/{count}", flush=True)
        path = run_directory / f"{prefix}-{index:02d}.json"
        reports.append(run_benchmark(binary, substrate, mode, warmup_steps, profile_steps, path))
        paths.append(path)
    return reports, paths


def create_output_directory(root: Path, prefix: str, explicit: Path | None) -> Path:
    destination = explicit.resolve() if explicit else root.resolve() / f"{prefix}-{utc_timestamp()}"
    destination.mkdir(parents=True, exist_ok=False)
    return destination


def validate_positive_arguments(args: argparse.Namespace) -> None:
    if args.warmup_steps < 0:
        raise BenchmarkError("--warmup-steps must be non-negative")
    if args.profile_steps <= 0:
        raise BenchmarkError("--profile-steps must be positive")
    if args.throughput_runs <= 0 or args.detail_runs <= 0:
        raise BenchmarkError("Run counts must be positive")


def capture_baseline(args: argparse.Namespace) -> Path:
    validate_positive_arguments(args)
    source_substrate = args.substrate.resolve()
    if not source_substrate.is_file():
        raise BenchmarkError(f"Substrate does not exist: {source_substrate}")

    fingerprint = source_fingerprint()
    working_tree = working_tree_fingerprint()
    label = re.sub(r"[^A-Za-z0-9_.-]+", "-", args.label or source_substrate.stem).strip("-") or "substrate"
    output_dir = create_output_directory(args.output_root, f"baseline-{label}", args.output_dir)
    source_manifest_path = output_dir / "source-manifest.json"
    write_json(source_manifest_path, fingerprint)
    fingerprint_summary = {key: value for key, value in fingerprint.items() if key != "entries"}
    working_tree_manifest_path = output_dir / "working-tree-manifest.json"
    write_json(working_tree_manifest_path, working_tree)
    working_tree_summary = {key: value for key, value in working_tree.items() if key != "entries"}
    input_dir = output_dir / "input"
    input_dir.mkdir()
    benchmark_substrate = input_dir / source_substrate.name
    shutil.copy2(source_substrate, benchmark_substrate)

    cmake = find_cmake()
    build_root = args.build_root.resolve()
    off_source, off_environment, off_build_dir = build_variant(
        "profile-off", False, args.algorithm, fingerprint["sha256"], cmake, output_dir, build_root
    )
    on_source, _, on_build_dir = build_variant(
        "profile-on", True, args.algorithm, fingerprint["sha256"], cmake, output_dir, build_root
    )
    off_binary = output_dir / "bin" / "cellularity_perf-profile-off"
    on_binary = output_dir / "bin" / "cellularity_perf-profile-on"
    off_metadata = copy_binary(off_source, off_binary)
    on_metadata = copy_binary(on_source, on_binary)
    off_metadata["path"] = off_binary.relative_to(output_dir).as_posix()
    on_metadata["path"] = on_binary.relative_to(output_dir).as_posix()
    off_metadata["build"] = preserve_build_metadata(off_build_dir, output_dir, "profile-off")
    on_metadata["build"] = preserve_build_metadata(on_build_dir, output_dir, "profile-on")

    preflight_dir = output_dir / "runs" / "preflight"
    print("Running discarded cache-warming samples", flush=True)
    run_benchmark(
        off_binary,
        benchmark_substrate,
        "profile-json",
        args.warmup_steps,
        args.profile_steps,
        preflight_dir / "profile-off.json",
    )
    run_benchmark(
        on_binary,
        benchmark_substrate,
        "profile-detail-json",
        args.warmup_steps,
        args.profile_steps,
        preflight_dir / "profile-on.json",
    )

    throughput_reports, throughput_paths = collect_runs(
        off_binary,
        benchmark_substrate,
        "profile-json",
        args.warmup_steps,
        args.profile_steps,
        args.throughput_runs,
        output_dir / "runs" / "throughput",
        "profile-off",
    )
    detail_reports, detail_paths = collect_runs(
        on_binary,
        benchmark_substrate,
        "profile-detail-json",
        args.warmup_steps,
        args.profile_steps,
        args.detail_runs,
        output_dir / "runs" / "detail",
        "profile-on",
    )

    throughput_state = assert_equivalent_state(throughput_reports, "Profile-OFF")
    detail_state = assert_equivalent_state(detail_reports, "Profile-ON")
    assert_compiled_algorithm(throughput_reports + detail_reports, args.algorithm, "baseline capture")
    if throughput_state != detail_state:
        raise BenchmarkError("Profile-OFF and Profile-ON produced different simulation states")

    manifest = {
        "schemaVersion": 1,
        "kind": "cellularity.simulation-performance-baseline",
        "createdAtUtc": datetime.now(timezone.utc).isoformat(),
        "status": "valid",
        "comparisonPolicy": {
            "throughput": "Release Profile-OFF; median is primary; no universal pass threshold",
            "attribution": "Release Profile-ON; stage timing is diagnostic only",
            "correctness": "all serialized state digests and summaries must match exactly",
            "processIsolation": "fresh process and fresh substrate load for every sample",
        },
        "input": {
            "path": benchmark_substrate.relative_to(output_dir).as_posix(),
            "fileName": source_substrate.name,
            "sha256": sha256_file(benchmark_substrate),
            "sizeBytes": benchmark_substrate.stat().st_size,
        },
        "parameters": {
            "overlapAlgorithm": args.algorithm,
            "runtimeOverlapSelection": False,
            "cellOwnedSignalKernel": False,
            "warmupSteps": args.warmup_steps,
            "profileSteps": args.profile_steps,
            "throughputRuns": args.throughput_runs,
            "detailRuns": args.detail_runs,
            "discardedPreflightRunsPerMode": 1,
        },
        "source": {
            **git_metadata(),
            "fingerprint": fingerprint_summary,
            "manifest": {
                "path": source_manifest_path.relative_to(output_dir).as_posix(),
                "sha256": sha256_file(source_manifest_path),
            },
            "actualWorkingTree": working_tree_summary,
            "workingTreeManifest": {
                "path": working_tree_manifest_path.relative_to(output_dir).as_posix(),
                "sha256": sha256_file(working_tree_manifest_path),
            },
        },
        "host": host_metadata(),
        "toolchain": toolchain_metadata(cmake, off_environment),
        "binaries": {"profileOff": off_metadata, "profileOn": on_metadata},
        "state": {
            "equivalentAcrossAllMeasuredRuns": True,
            "profileModesEquivalent": True,
            **throughput_state,
        },
        "throughput": {
            "metric": "stepsPerSecond",
            "primaryStatistic": "median",
            "reportFiles": relative_paths(throughput_paths, output_dir),
            "summary": summarize(report["stepsPerSecond"] for report in throughput_reports),
        },
        "stageProfile": {
            "reportFiles": relative_paths(detail_paths, output_dir),
            "stepsPerSecond": summarize(report["stepsPerSecond"] for report in detail_reports),
            "stages": summarize_stages(detail_reports),
        },
    }
    manifest_path = output_dir / "baseline.json"
    write_json(manifest_path, manifest)
    print(f"Baseline: {manifest_path}")
    print(f"Profile-OFF median: {manifest['throughput']['summary']['median']:.2f} UPS")
    return manifest_path


def resolve_manifest_file(manifest_path: Path, relative_path: str, expected_sha256: str) -> Path:
    path = (manifest_path.parent / relative_path).resolve()
    if not path.is_file():
        raise BenchmarkError(f"Baseline artifact is missing: {path}")
    actual_sha256 = sha256_file(path)
    if actual_sha256 != expected_sha256:
        raise BenchmarkError(f"Baseline artifact digest mismatch: {path}")
    return path


def percentage_change(candidate: float, baseline: float) -> float:
    if baseline == 0.0:
        raise BenchmarkError("Cannot calculate a percentage against zero")
    return (candidate / baseline - 1.0) * 100.0


def compare_stage_profiles(
    baseline_reports: list[dict[str, Any]], candidate_reports: list[dict[str, Any]]
) -> dict[str, Any]:
    baseline = summarize_stages(baseline_reports)
    candidate = summarize_stages(candidate_reports)
    if baseline.keys() != candidate.keys():
        raise BenchmarkError("Baseline and candidate stage sets differ")
    result: dict[str, Any] = {}
    for stage in baseline:
        baseline_ms = baseline[stage]["avgMs"]["median"]
        candidate_ms = candidate[stage]["avgMs"]["median"]
        result[stage] = {
            "baselineAvgMs": baseline[stage]["avgMs"],
            "candidateAvgMs": candidate[stage]["avgMs"],
            "candidateDurationChangePercent": percentage_change(candidate_ms, baseline_ms)
            if baseline_ms
            else None,
            "baselineSharePercent": baseline[stage]["sharePercent"],
            "candidateSharePercent": candidate[stage]["sharePercent"],
        }
    return result


def compare_with_baseline(args: argparse.Namespace) -> Path:
    manifest_path = args.baseline.resolve()
    baseline_manifest = load_json(manifest_path)
    if baseline_manifest.get("kind") != "cellularity.simulation-performance-baseline":
        raise BenchmarkError(f"Not a Cellularity simulation baseline: {manifest_path}")
    if baseline_manifest.get("schemaVersion") != 1:
        raise BenchmarkError(f"Unsupported baseline schema in {manifest_path}")
    if baseline_manifest.get("status") != "valid":
        raise BenchmarkError(f"Baseline is not valid: {manifest_path}")

    parameters = baseline_manifest["parameters"]
    args.warmup_steps = int(parameters["warmupSteps"])
    args.profile_steps = int(parameters["profileSteps"])
    args.throughput_runs = args.throughput_runs or int(parameters["throughputRuns"])
    args.detail_runs = args.detail_runs or int(parameters["detailRuns"])
    validate_positive_arguments(args)

    baseline_substrate = resolve_manifest_file(
        manifest_path,
        baseline_manifest["input"]["path"],
        baseline_manifest["input"]["sha256"],
    )
    substrate = args.substrate.resolve() if args.substrate else baseline_substrate
    if not substrate.is_file() or sha256_file(substrate) != baseline_manifest["input"]["sha256"]:
        raise BenchmarkError("Candidate substrate does not match the baseline input SHA-256")

    baseline_off = resolve_manifest_file(
        manifest_path,
        baseline_manifest["binaries"]["profileOff"]["path"],
        baseline_manifest["binaries"]["profileOff"]["sha256"],
    )
    baseline_on = resolve_manifest_file(
        manifest_path,
        baseline_manifest["binaries"]["profileOn"]["path"],
        baseline_manifest["binaries"]["profileOn"]["sha256"],
    )

    fingerprint = source_fingerprint()
    working_tree = working_tree_fingerprint()
    output_dir = create_output_directory(args.output_root, "comparison", args.output_dir)
    source_manifest_path = output_dir / "source-manifest.json"
    write_json(source_manifest_path, fingerprint)
    fingerprint_summary = {key: value for key, value in fingerprint.items() if key != "entries"}
    working_tree_manifest_path = output_dir / "working-tree-manifest.json"
    write_json(working_tree_manifest_path, working_tree)
    working_tree_summary = {key: value for key, value in working_tree.items() if key != "entries"}
    cmake = find_cmake()
    candidate_off_source, off_environment, off_build_dir = build_variant(
        "profile-off", False, args.candidate_algorithm, fingerprint["sha256"], cmake,
        output_dir, args.build_root.resolve()
    )
    candidate_on_source, _, on_build_dir = build_variant(
        "profile-on", True, args.candidate_algorithm, fingerprint["sha256"], cmake,
        output_dir, args.build_root.resolve()
    )
    candidate_off = output_dir / "bin" / "cellularity_perf-candidate-profile-off"
    candidate_on = output_dir / "bin" / "cellularity_perf-candidate-profile-on"
    candidate_off_metadata = copy_binary(candidate_off_source, candidate_off)
    candidate_on_metadata = copy_binary(candidate_on_source, candidate_on)
    candidate_off_metadata["path"] = candidate_off.relative_to(output_dir).as_posix()
    candidate_on_metadata["path"] = candidate_on.relative_to(output_dir).as_posix()
    candidate_off_metadata["build"] = preserve_build_metadata(
        off_build_dir, output_dir, "candidate-profile-off"
    )
    candidate_on_metadata["build"] = preserve_build_metadata(
        on_build_dir, output_dir, "candidate-profile-on"
    )

    preflight = output_dir / "runs" / "preflight"
    print("Running discarded A/B cache-warming samples", flush=True)
    run_benchmark(
        baseline_off, substrate, "profile-json", args.warmup_steps, args.profile_steps,
        preflight / "baseline-profile-off.json"
    )
    run_benchmark(
        candidate_off, substrate, "profile-json", args.warmup_steps, args.profile_steps,
        preflight / "candidate-profile-off.json"
    )
    run_benchmark(
        baseline_on, substrate, "profile-detail-json", args.warmup_steps, args.profile_steps,
        preflight / "baseline-profile-on.json"
    )
    run_benchmark(
        candidate_on, substrate, "profile-detail-json", args.warmup_steps, args.profile_steps,
        preflight / "candidate-profile-on.json"
    )

    baseline_throughput: list[dict[str, Any]] = []
    candidate_throughput: list[dict[str, Any]] = []
    throughput_paths: list[Path] = []
    for index in range(args.throughput_runs):
        order = (("baseline", baseline_off), ("candidate", candidate_off))
        if index % 2:
            order = tuple(reversed(order))
        for variant, binary in order:
            print(f"Throughput pair {index + 1}/{args.throughput_runs}: {variant}", flush=True)
            path = output_dir / "runs" / "throughput" / f"{index + 1:02d}-{variant}.json"
            report = run_benchmark(
                binary, substrate, "profile-json", args.warmup_steps, args.profile_steps, path
            )
            (baseline_throughput if variant == "baseline" else candidate_throughput).append(report)
            throughput_paths.append(path)

    baseline_detail: list[dict[str, Any]] = []
    candidate_detail: list[dict[str, Any]] = []
    detail_paths: list[Path] = []
    for index in range(args.detail_runs):
        order = (("baseline", baseline_on), ("candidate", candidate_on))
        if index % 2:
            order = tuple(reversed(order))
        for variant, binary in order:
            print(f"Stage-profile pair {index + 1}/{args.detail_runs}: {variant}", flush=True)
            path = output_dir / "runs" / "detail" / f"{index + 1:02d}-{variant}.json"
            report = run_benchmark(
                binary, substrate, "profile-detail-json", args.warmup_steps, args.profile_steps, path
            )
            (baseline_detail if variant == "baseline" else candidate_detail).append(report)
            detail_paths.append(path)

    all_reports = baseline_throughput + candidate_throughput + baseline_detail + candidate_detail
    comparison_state = assert_equivalent_state(all_reports, "A/B comparison")
    baseline_algorithm = baseline_manifest["parameters"].get("overlapAlgorithm")
    if baseline_algorithm:
        assert_compiled_algorithm(
            baseline_throughput + baseline_detail, baseline_algorithm, "preserved baseline"
        )
    assert_compiled_algorithm(
        candidate_throughput + candidate_detail, args.candidate_algorithm, "candidate"
    )
    expected_state = {phase: baseline_manifest["state"][phase] for phase in STATE_PHASES}
    if comparison_state != expected_state:
        raise BenchmarkError("A/B state does not match the captured baseline state")

    baseline_summary = summarize(report["stepsPerSecond"] for report in baseline_throughput)
    candidate_summary = summarize(report["stepsPerSecond"] for report in candidate_throughput)
    paired_changes = [
        percentage_change(candidate["stepsPerSecond"], baseline["stepsPerSecond"])
        for baseline, candidate in zip(baseline_throughput, candidate_throughput)
    ]
    median_change = percentage_change(candidate_summary["median"], baseline_summary["median"])
    paired_change_summary = summarize(paired_changes, relative_range=False)

    report = {
        "schemaVersion": 1,
        "kind": "cellularity.simulation-performance-comparison",
        "createdAtUtc": datetime.now(timezone.utc).isoformat(),
        "status": "valid",
        "baselineManifest": str(manifest_path),
        "input": baseline_manifest["input"],
        "parameters": {
            "baselineOverlapAlgorithm": baseline_manifest["parameters"].get(
                "overlapAlgorithm", "unspecified"
            ),
            "candidateOverlapAlgorithm": args.candidate_algorithm,
            "runtimeOverlapSelection": False,
            "cellOwnedSignalKernel": False,
            "warmupSteps": args.warmup_steps,
            "profileSteps": args.profile_steps,
            "throughputRunsPerVariant": args.throughput_runs,
            "detailRunsPerVariant": args.detail_runs,
            "discardedPreflightRunsPerVariantAndMode": 1,
            "ordering": "alternating baseline/candidate pairs",
        },
        "baselineSource": baseline_manifest["source"],
        "candidateSource": {
            **git_metadata(),
            "fingerprint": fingerprint_summary,
            "manifest": {
                "path": source_manifest_path.relative_to(output_dir).as_posix(),
                "sha256": sha256_file(source_manifest_path),
            },
            "actualWorkingTree": working_tree_summary,
            "workingTreeManifest": {
                "path": working_tree_manifest_path.relative_to(output_dir).as_posix(),
                "sha256": sha256_file(working_tree_manifest_path),
            },
        },
        "host": host_metadata(),
        "toolchain": toolchain_metadata(cmake, off_environment),
        "candidateBinaries": {
            "profileOff": candidate_off_metadata,
            "profileOn": candidate_on_metadata,
        },
        "state": {
            "semanticallyEquivalent": True,
            "matchesCapturedBaseline": True,
            **comparison_state,
        },
        "throughput": {
            "metric": "stepsPerSecond",
            "positiveChangeMeansFaster": True,
            "primaryComparison": "pairedChangePercent.median",
            "baseline": baseline_summary,
            "candidate": candidate_summary,
            "ratioOfMediansChangePercent": median_change,
            "pairedChangePercent": paired_change_summary,
            "reportFiles": relative_paths(throughput_paths, output_dir),
        },
        "stageProfile": {
            "negativeDurationChangeMeansFaster": True,
            "stages": compare_stage_profiles(baseline_detail, candidate_detail),
            "reportFiles": relative_paths(detail_paths, output_dir),
        },
    }
    report_path = output_dir / "comparison.json"
    write_json(report_path, report)
    print(f"Comparison: {report_path}")
    print(f"Paired median change: {paired_change_summary['median']:+.2f}%")
    return report_path


def add_common_output_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument(
        "--build-root",
        type=Path,
        default=Path(tempfile.gettempdir()) / "cellularity-performance-builds",
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture a simulation baseline or compare current code against a preserved baseline"
    )
    commands = parser.add_subparsers(dest="command", required=True)

    capture = commands.add_parser("capture", help="build and save a reproducible baseline")
    capture.add_argument("--substrate", required=True, type=Path)
    capture.add_argument("--label")
    capture.add_argument(
        "--algorithm",
        choices=(
            "baseline",
            "compact-record",
            "compact-primary",
            "incremental-bucketed-f64-source32",
        ),
        default="baseline",
        help="compile-time overlap algorithm; runtime selection is disabled",
    )
    capture.add_argument("--warmup-steps", type=int, default=200)
    capture.add_argument("--profile-steps", type=int, default=5000)
    capture.add_argument("--throughput-runs", type=int, default=7)
    capture.add_argument("--detail-runs", type=int, default=3)
    add_common_output_arguments(capture)

    compare = commands.add_parser("compare", help="run interleaved A/B samples")
    compare.add_argument("--baseline", required=True, type=Path)
    compare.add_argument("--substrate", type=Path)
    compare.add_argument(
        "--candidate-algorithm",
        choices=(
            "baseline",
            "compact-record",
            "compact-primary",
            "incremental-bucketed-f64-source32",
        ),
        default="baseline",
        help="compile-time candidate overlap algorithm; runtime selection is disabled",
    )
    compare.add_argument("--throughput-runs", type=int)
    compare.add_argument("--detail-runs", type=int)
    add_common_output_arguments(compare)
    return parser.parse_args()


def main() -> int:
    try:
        args = parse_arguments()
        if args.command == "capture":
            capture_baseline(args)
        else:
            compare_with_baseline(args)
        return 0
    except (BenchmarkError, OSError, subprocess.SubprocessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
