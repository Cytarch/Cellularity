#!/usr/bin/env python3
"""Capture a reproducible simulation throughput baseline."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


SCRIPT_PATH = Path(__file__).resolve()
REPO_ROOT = SCRIPT_PATH.parents[2]
BENCHMARK_SCRIPT = SCRIPT_PATH.with_name("benchmark-ab.py")
DEFAULT_OUTPUT_ROOT = REPO_ROOT / ".codex" / "simulation-throughput-baseline"
DEFAULT_ANDROID_SERIAL = os.environ.get("CELLULARITY_THROUGHPUT_ANDROID_SERIAL")

spec = importlib.util.spec_from_file_location("cellularity_benchmark_ab", BENCHMARK_SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"Could not import {BENCHMARK_SCRIPT}")
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class CaptureError(RuntimeError):
    pass


def timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False)
        output.write("\n")
    temporary.replace(path)


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise CaptureError(f"Expected JSON object in {path}")
    return value


def run(
    command: list[str],
    *,
    cwd: Path = REPO_ROOT,
    environment: dict[str, str] | None = None,
    log_path: Path | None = None,
) -> str:
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if log_path is not None:
        log_path.write_text(result.stdout, encoding="utf-8")
    if result.returncode != 0:
        tail = "\n".join(result.stdout.splitlines()[-80:])
        raise CaptureError(
            f"Command failed with exit code {result.returncode}: {' '.join(command)}\n{tail}"
        )
    return result.stdout.strip()


def run_json(command: list[str], output_path: Path) -> dict[str, Any]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    run(command + [str(output_path)])
    return read_json(output_path)


def percent_change(candidate: float, baseline: float) -> float:
    if baseline == 0.0:
        raise CaptureError("Cannot calculate percentage change against zero")
    return (candidate / baseline - 1.0) * 100.0


def robust_noise(changes: list[float]) -> dict[str, Any]:
    summary = benchmark.summarize(changes, relative_range=False)
    center = statistics.median(changes)
    mad = statistics.median(abs(value - center) for value in changes)
    robust_sigma = 1.4826 * mad
    band = max(
        abs(float(summary["p05"])),
        abs(float(summary["p95"])),
        abs(center) + 3.0 * robust_sigma,
    )
    return {
        "pairedChangePercent": summary,
        "medianAbsoluteDeviation": mad,
        "robustSigma": robust_sigma,
        "noiseBandPercent": band,
        "formula": "max(abs(p05), abs(p95), abs(median) + 3 * 1.4826 * MAD)",
    }


def state_key(report: dict[str, Any]) -> Any:
    return report.get("state")


def require_equal(values: list[Any], context: str) -> Any:
    if not values:
        raise CaptureError(f"No values supplied for {context}")
    reference = values[0]
    for index, value in enumerate(values[1:], start=2):
        if value != reference:
            raise CaptureError(f"Correctness mismatch in {context}, value {index}")
    return reference


def require_algorithm(report: dict[str, Any], expected: str, context: str) -> None:
    canonical = f"{expected}-sweep"
    if report.get("overlapBroadphaseAlgorithm") != canonical:
        raise CaptureError(
            f"{context} reported {report.get('overlapBroadphaseAlgorithm')!r}, expected {canonical!r}"
        )


def paired_runs(
    count: int,
    first_name: str,
    second_name: str,
    run_first: Callable[[int, Path], dict[str, Any]],
    run_second: Callable[[int, Path], dict[str, Any]],
    directory: Path,
    metric: str,
) -> dict[str, Any]:
    first_reports: list[dict[str, Any]] = []
    second_reports: list[dict[str, Any]] = []
    order_log: list[list[str]] = []
    for pair in range(1, count + 1):
        order = [(first_name, run_first, first_reports), (second_name, run_second, second_reports)]
        if pair % 2 == 0:
            order.reverse()
        order_log.append([entry[0] for entry in order])
        for name, callback, reports in order:
            print(f"  pair {pair}/{count}: {name}", flush=True)
            reports.append(callback(pair, directory / f"{pair:02d}-{name}.json"))

    require_equal([state_key(report) for report in first_reports + second_reports], directory.name)
    first_values = [float(report[metric]) for report in first_reports]
    second_values = [float(report[metric]) for report in second_reports]
    changes = [percent_change(second, first) for first, second in zip(first_values, second_values)]
    return {
        "metric": metric,
        "positiveChangeMeansSecondIsFaster": True,
        "first": {"name": first_name, "summary": benchmark.summarize(first_values)},
        "second": {"name": second_name, "summary": benchmark.summarize(second_values)},
        "pairedChangePercent": benchmark.summarize(changes, relative_range=False),
        "pairOrder": order_log,
        "reportDirectory": str(directory),
    }


def thermal_snapshot() -> dict[str, Any]:
    temperatures: dict[str, str] = {}
    for path in sorted(Path("/sys/class/thermal").glob("thermal_zone*/temp")):
        try:
            temperatures[path.parent.name] = path.read_text(encoding="utf-8").strip()
        except OSError:
            pass
    frequencies: dict[str, str] = {}
    for path in sorted(Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_cur_freq")):
        try:
            frequencies[path.parent.parent.name] = path.read_text(encoding="utf-8").strip()
        except OSError:
            pass
    return {
        "capturedAtUtc": datetime.now(timezone.utc).isoformat(),
        "loadAverage": Path("/proc/loadavg").read_text(encoding="utf-8").strip(),
        "temperaturesRaw": temperatures,
        "cpuCurrentFrequencyKHz": frequencies,
    }


def build_host_variants(
    output_dir: Path,
    build_root: Path,
    fingerprint: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    cmake = benchmark.find_cmake()
    variants: dict[str, dict[str, Any]] = {}
    for algorithm in ("baseline", "compact-primary"):
        for profile in (False, True):
            profile_name = "profile-on" if profile else "profile-off"
            key = f"{algorithm}-{profile_name}"
            source, environment, build_dir = benchmark.build_variant(
                key,
                profile,
                algorithm,
                fingerprint["sha256"],
                cmake,
                output_dir,
                build_root,
            )
            destination = output_dir / "host" / "bin" / f"cellularity_perf-{key}"
            binary_metadata = benchmark.copy_binary(source, destination)
            binary_metadata["path"] = destination.relative_to(output_dir).as_posix()
            binary_metadata["build"] = benchmark.preserve_build_metadata(
                build_dir, output_dir, key
            )
            variants[key] = {
                "binary": destination,
                "buildDir": build_dir,
                "environment": environment,
                "metadata": binary_metadata,
            }
    return variants


def run_behavior_and_memory(
    variants: dict[str, dict[str, Any]], output_dir: Path, cmake: Path
) -> dict[str, Any]:
    result: dict[str, Any] = {"behavior": {}, "memory": []}
    for algorithm in ("baseline", "compact-primary"):
        variant = variants[f"{algorithm}-profile-off"]
        build_dir: Path = variant["buildDir"]
        environment: dict[str, str] = variant["environment"]
        run(
            [str(cmake), "--build", str(build_dir), "--target",
             "cellularity_behavior_baseline_tests", "cellularity_memory_probe", "--parallel"],
            environment=environment,
            log_path=output_dir / "logs" / f"build-regression-{algorithm}.log",
        )
        behavior_output = run(
            [str(build_dir / "cellularity_behavior_baseline_tests")],
            environment=environment,
            log_path=output_dir / "logs" / f"behavior-{algorithm}.log",
        )
        result["behavior"][algorithm] = {
            "passed": True,
            "output": behavior_output.splitlines(),
        }
        if algorithm == "baseline":
            for cells, nutrients, radius in ((1000, 3500, 1.0), (4000, 7500, 4.0)):
                output = run([
                    str(build_dir / "cellularity_memory_probe"),
                    str(cells), str(nutrients), str(radius),
                ], environment=environment)
                result["memory"].append(json.loads(output))
    return result


def workload_parameters(name: str, quick: bool) -> tuple[int, int, int]:
    if quick:
        return (0, 2, 1)
    if name in ("empty", "one_cell", "two_cells", "tiny_16", "tiny_32"):
        return (50, 1000, 3)
    if name == "all_overlap":
        return (0, 4, 3)
    if name in ("lifecycle_grow", "fertilization"):
        return (0, 12, 3)
    if name in ("lifecycle_shrink_compact", "lifecycle_refill", "ciliocyte_capacity"):
        return (0, 30, 3)
    if name == "ciliocyte_boundary":
        return (5, 100, 3)
    if name.startswith("typed_") or name == "pure_force":
        return (10, 200, 3)
    if name.startswith("high_"):
        return (20, 250, 3)
    return (30, 400, 3)


def run_synthetic_matrix(
    baseline: Path,
    compact: Path,
    output_dir: Path,
    quick: bool,
) -> dict[str, Any]:
    workload_list = read_json_from_stdout(
        [str(baseline), "throughput-workloads-json", "-"]
    )["workloads"]
    matrix: dict[str, Any] = {}
    for workload in workload_list:
        warmup, steps, pair_count = workload_parameters(str(workload), quick)
        print(f"Synthetic workload: {workload}", flush=True)
        directory = output_dir / "host" / "matrix" / str(workload)

        def invoke(binary: Path, pair: int, path: Path) -> dict[str, Any]:
            del pair
            report = run_json([
                str(binary), "throughput-synthetic-profile-json", str(workload),
                str(warmup), str(steps),
            ], path)
            require_algorithm(
                report,
                "compact-primary" if binary == compact else "baseline",
                f"synthetic {workload}",
            )
            return report

        comparison = paired_runs(
            pair_count,
            "baseline",
            "compact-primary",
            lambda pair, path: invoke(baseline, pair, path),
            lambda pair, path: invoke(compact, pair, path),
            directory,
            "stepsPerSecond",
        )
        baseline_trace = run_json([
            str(baseline), "throughput-synthetic-trace-json", str(workload),
            str(min(16, max(1, steps))),
        ], directory / "trace-baseline.json")
        compact_trace = run_json([
            str(compact), "throughput-synthetic-trace-json", str(workload),
            str(min(16, max(1, steps))),
        ], directory / "trace-compact-primary.json")
        require_algorithm(baseline_trace, "baseline", f"{workload} baseline trace")
        require_algorithm(compact_trace, "compact-primary", f"{workload} compact trace")
        require_equal(
            [baseline_trace["state"], compact_trace["state"]],
            f"{workload} trace state",
        )
        trace_fields = ("pairs", "endpoints", "applyIntents")
        trace_exact = all(baseline_trace[field] == compact_trace[field] for field in trace_fields)
        comparison["trace"] = {
            "stateEquivalent": True,
            "orderedStreamsExactlyEqual": trace_exact,
            "baseline": {field: baseline_trace[field] for field in trace_fields},
            "compactPrimary": {field: compact_trace[field] for field in trace_fields},
        }
        matrix[str(workload)] = comparison
    return matrix


def read_json_from_stdout(command: list[str]) -> dict[str, Any]:
    output = run(command)
    value = json.loads(output)
    if not isinstance(value, dict):
        raise CaptureError(f"Expected JSON object from {' '.join(command)}")
    return value


def run_host_measurements(
    variants: dict[str, dict[str, Any]],
    substrate: Path,
    output_dir: Path,
    host_pairs: int,
    warmup_steps: int,
    measured_steps: int,
    quick: bool,
) -> dict[str, Any]:
    baseline = variants["baseline-profile-off"]["binary"]
    compact = variants["compact-primary-profile-off"]["binary"]
    baseline_profile = variants["baseline-profile-on"]["binary"]
    compact_profile = variants["compact-primary-profile-on"]["binary"]

    def full(binary: Path, path: Path) -> dict[str, Any]:
        report = benchmark.run_benchmark(
            binary, substrate, "profile-json", warmup_steps, measured_steps, path
        )
        require_algorithm(
            report,
            "compact-primary" if binary == compact else "baseline",
            "real dynamic benchmark",
        )
        return report

    print("Host baseline-vs-baseline noise", flush=True)
    control = paired_runs(
        host_pairs,
        "baseline-a",
        "baseline-b",
        lambda pair, path: full(baseline, path),
        lambda pair, path: full(baseline, path),
        output_dir / "host" / "real-dynamic" / "noise-control",
        "stepsPerSecond",
    )
    control_changes = control["pairedChangePercent"]["samples"]
    noise = robust_noise([float(value) for value in control_changes])

    print("Host baseline-vs-compact-primary", flush=True)
    dynamic = paired_runs(
        host_pairs,
        "baseline",
        "compact-primary",
        lambda pair, path: full(baseline, path),
        lambda pair, path: full(compact, path),
        output_dir / "host" / "real-dynamic" / "algorithm-comparison",
        "stepsPerSecond",
    )

    detail_count = 1 if quick else 3
    details: dict[str, list[dict[str, Any]]] = {"baseline": [], "compact-primary": []}
    for pair in range(1, detail_count + 1):
        order = [("baseline", baseline_profile), ("compact-primary", compact_profile)]
        if pair % 2 == 0:
            order.reverse()
        for name, binary in order:
            path = output_dir / "host" / "real-dynamic" / "attribution" / f"{pair:02d}-{name}.json"
            details[name].append(benchmark.run_benchmark(
                binary, substrate, "profile-detail-json", warmup_steps, measured_steps, path
            ))
            require_algorithm(
                details[name][-1], name, f"{name} Profile-ON attribution"
            )
    require_equal(
        [state_key(report) for reports in details.values() for report in reports],
        "Profile-ON attribution",
    )

    stationary_repeats = 20 if quick else 5000
    stationary_warmup = 2 if quick else 200

    def stationary(binary: Path, path: Path) -> dict[str, Any]:
        report = run_json([
            str(binary), str(substrate), "throughput-stationary-json",
            str(stationary_warmup), str(stationary_repeats),
        ], path)
        require_algorithm(
            report,
            "compact-primary" if binary == compact else "baseline",
            "stationary tardigrade benchmark",
        )
        return report

    print("Host stationary tardigrade", flush=True)
    stationary_comparison = paired_runs(
        max(1, min(host_pairs, 7)),
        "baseline",
        "compact-primary",
        lambda pair, path: stationary(baseline, path),
        lambda pair, path: stationary(compact, path),
        output_dir / "host" / "real-stationary",
        "repeatsPerSecond",
    )

    trace_steps = 2 if quick else 32
    baseline_trace = run_json([
        str(baseline), str(substrate), "throughput-trace-json", str(trace_steps),
    ], output_dir / "host" / "real-dynamic" / "trace-baseline.json")
    compact_trace = run_json([
        str(compact), str(substrate), "throughput-trace-json", str(trace_steps),
    ], output_dir / "host" / "real-dynamic" / "trace-compact-primary.json")
    require_algorithm(baseline_trace, "baseline", "real baseline trace")
    require_algorithm(compact_trace, "compact-primary", "real compact trace")
    require_equal([baseline_trace["state"], compact_trace["state"]], "real trace state")
    trace_fields = ("pairs", "endpoints", "applyIntents")

    return {
        "noiseControl": control,
        "noise": noise,
        "dynamicComparison": dynamic,
        "stationaryComparison": stationary_comparison,
        "attribution": {
            name: {
                "stepsPerSecond": benchmark.summarize(
                    report["stepsPerSecond"] for report in reports
                ),
                "stages": benchmark.summarize_stages(reports),
            }
            for name, reports in details.items()
        },
        "orderedTrace": {
            "steps": trace_steps,
            "stateEquivalent": True,
            "orderedStreamsExactlyEqual": all(
                baseline_trace[field] == compact_trace[field] for field in trace_fields
            ),
            "baseline": {field: baseline_trace[field] for field in trace_fields},
            "compactPrimary": {field: compact_trace[field] for field in trace_fields},
        },
        "syntheticMatrix": run_synthetic_matrix(baseline, compact, output_dir, quick),
    }


def adb_path() -> Path | None:
    found = shutil.which("adb")
    if found:
        return Path(found)
    for root in benchmark.android_sdk_roots():
        candidate = root / "platform-tools" / "adb"
        if candidate.is_file():
            return candidate
    return None


def adb_output(adb: Path, serial: str, arguments: list[str], *, check: bool = True) -> str:
    result = subprocess.run(
        [str(adb), "-s", serial, *arguments],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if check and result.returncode != 0:
        raise CaptureError(f"ADB command failed: {' '.join(arguments)}\n{result.stdout}")
    return result.stdout.strip()


def android_context(adb: Path, serial: str) -> dict[str, Any]:
    properties = {}
    for prop in (
        "ro.product.manufacturer", "ro.product.model", "ro.product.device",
        "ro.product.cpu.abi", "ro.build.version.release", "ro.build.version.sdk",
        "ro.build.fingerprint", "ro.kernel.qemu", "ro.hardware",
    ):
        properties[prop] = adb_output(adb, serial, ["shell", "getprop", prop], check=False)
    return {
        "serial": serial,
        "properties": properties,
        "cpuInfo": adb_output(adb, serial, ["shell", "cat", "/proc/cpuinfo"], check=False),
        "battery": adb_output(adb, serial, ["shell", "dumpsys", "battery"], check=False),
        "thermalService": adb_output(
            adb, serial, ["shell", "dumpsys", "thermalservice"], check=False
        ),
        "uptime": adb_output(adb, serial, ["shell", "cat", "/proc/uptime"], check=False),
    }


def build_android_probe(
    algorithm: str,
    abi: str,
    fingerprint: str,
    output_dir: Path,
    cmake: Path,
) -> tuple[Path, dict[str, Any]]:
    ndk = None
    for sdk_root in benchmark.android_sdk_roots():
        candidate = sdk_root / "ndk" / "28.2.13676358"
        if candidate.is_dir():
            ndk = candidate
            break
    if ndk is None:
        raise CaptureError("Android NDK 28.2.13676358 was not found")
    build_dir = Path(tempfile.gettempdir()) / (
        f"cellularity-throughput-android-{fingerprint[:16]}-{abi}-{algorithm}"
    )
    environment = os.environ.copy()
    environment["PATH"] = str(cmake.parent) + os.pathsep + environment.get("PATH", "")
    configure = [
        str(cmake), "-S", str(REPO_ROOT / "native"), "-B", str(build_dir), "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build' / 'cmake' / 'android.toolchain.cmake'}",
        f"-DANDROID_ABI={abi}", "-DANDROID_PLATFORM=android-21", "-DANDROID_STL=c++_static",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCELLULARITY_BUILD_THROUGHPUT_PROBE=ON",
        "-DCELLULARITY_ENABLE_RUNTIME_OVERLAP_SELECTION=OFF",
        f"-DCELLULARITY_DEFAULT_OVERLAP_SCAN_ALGORITHM={algorithm}",
        "-DCELLULARITY_ENABLE_NATIVE_PROFILE=OFF", "-DCELLULARITY_WARNINGS_AS_ERRORS=ON",
        "-DCELLULARITY_ENABLE_IPO=OFF",
    ]
    run(configure, environment=environment,
        log_path=output_dir / "logs" / f"android-probe-configure-{algorithm}.log")
    run(
        [str(cmake), "--build", str(build_dir), "--target", "cellularity_throughput_probe", "--parallel"],
        environment=environment,
        log_path=output_dir / "logs" / f"android-probe-build-{algorithm}.log",
    )
    source = build_dir / "cellularity_throughput_probe"
    destination = output_dir / "android" / "bin" / f"throughput-probe-{abi}-{algorithm}"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    destination.chmod(destination.stat().st_mode | 0o100)
    return destination, {
        "path": destination.relative_to(output_dir).as_posix(),
        "sha256": sha256_file(destination),
        "sizeBytes": destination.stat().st_size,
        "build": benchmark.preserve_build_metadata(
            build_dir, output_dir, f"android-{abi}-{algorithm}"
        ),
    }


def build_android_apks(output_dir: Path) -> dict[str, Any]:
    print("Building Android performance and experimental APKs", flush=True)
    log = output_dir / "logs" / "gradle-apks.log"
    environment = os.environ.copy()
    java_home = environment.get("JAVA_HOME")
    if not java_home or not (Path(java_home) / "bin" / "java").is_file():
        java = shutil.which("java")
        candidates = ([Path(java).resolve().parent.parent] if java else []) + [
            Path.home() / ".gradle" / "jdks" / "eclipse_adoptium-21-amd64-linux.2",
            Path("/opt/android-studio/jbr"),
            Path("/usr/lib/jvm/java-21-openjdk"),
        ]
        java_home_path = next(
            (candidate for candidate in candidates if (candidate / "bin" / "java").is_file()),
            None,
        )
        if java_home_path is None:
            raise CaptureError("A Java 21 runtime was not found for the Gradle APK build")
        environment["JAVA_HOME"] = str(java_home_path)
        environment["PATH"] = str(java_home_path / "bin") + os.pathsep + environment.get("PATH", "")
    sdk_root = next((root for root in benchmark.android_sdk_roots() if root.is_dir()), None)
    if sdk_root is None:
        raise CaptureError("Android SDK was not found for the Gradle APK build")
    environment["ANDROID_SDK_ROOT"] = str(sdk_root)
    environment["ANDROID_HOME"] = str(sdk_root)
    run(
        [str(REPO_ROOT / "gradlew"), "--no-daemon", "assemblePerformance", "assembleExperimental"],
        environment=environment,
        log_path=log,
    )
    result: dict[str, Any] = {}
    for variant in ("performance", "experimental"):
        candidates = sorted((REPO_ROOT / "app" / "build" / "outputs" / "apk" / variant).glob("*.apk"))
        if len(candidates) != 1:
            raise CaptureError(f"Expected one {variant} APK, found {len(candidates)}")
        source = candidates[0]
        destination = output_dir / "android" / "apk" / source.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        result[variant] = {
            "path": destination.relative_to(output_dir).as_posix(),
            "sha256": sha256_file(destination),
            "sizeBytes": destination.stat().st_size,
        }
    return result


def run_android_measurements(
    output_dir: Path,
    substrate: Path,
    fingerprint: dict[str, Any],
    serial: str,
    pairs: int,
    warmup_steps: int,
    measured_steps: int,
    quick: bool,
) -> dict[str, Any]:
    adb = adb_path()
    if adb is None:
        raise CaptureError("adb was not found")
    state = adb_output(adb, serial, ["get-state"], check=False)
    if state != "device":
        raise CaptureError(f"Android target {serial} is not connected (state={state!r})")
    before = android_context(adb, serial)
    abi = before["properties"]["ro.product.cpu.abi"] or "unknown"
    if abi not in ("arm64-v8a", "x86_64"):
        raise CaptureError(f"Unsupported throughput probe ABI: {abi}")
    cmake = benchmark.find_cmake()
    baseline, baseline_metadata = build_android_probe(
        "baseline", abi, fingerprint["sha256"], output_dir, cmake
    )
    compact, compact_metadata = build_android_probe(
        "compact-primary", abi, fingerprint["sha256"], output_dir, cmake
    )
    remote_root = f"/data/local/tmp/cellularity-throughput-{fingerprint['sha256'][:12]}"
    adb_output(adb, serial, ["shell", "mkdir", "-p", remote_root])
    remote_substrate = f"{remote_root}/tardigrade.substrate"
    remote_baseline = f"{remote_root}/probe-baseline"
    remote_compact = f"{remote_root}/probe-compact-primary"
    adb_output(adb, serial, ["push", str(substrate), remote_substrate])
    adb_output(adb, serial, ["push", str(baseline), remote_baseline])
    adb_output(adb, serial, ["push", str(compact), remote_compact])
    adb_output(adb, serial, ["shell", "chmod", "755", remote_baseline, remote_compact])

    android_warmup = 1 if quick else warmup_steps
    android_steps = 2 if quick else measured_steps

    def probe(remote_binary: str, output_path: Path) -> dict[str, Any]:
        output = adb_output(adb, serial, [
            "shell", remote_binary, remote_substrate,
            str(android_warmup), str(android_steps), "-",
        ])
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(output + "\n", encoding="utf-8")
        value = json.loads(output)
        if not isinstance(value, dict):
            raise CaptureError("Android probe did not return a JSON object")
        require_algorithm(
            value,
            "compact-primary" if remote_binary == remote_compact else "baseline",
            "Android throughput probe",
        )
        return value

    try:
        print(f"Android ({serial}) baseline-vs-baseline noise", flush=True)
        control = paired_runs(
            pairs,
            "baseline-a",
            "baseline-b",
            lambda pair, path: probe(remote_baseline, path),
            lambda pair, path: probe(remote_baseline, path),
            output_dir / "android" / "noise-control",
            "stepsPerSecond",
        )
        noise = robust_noise(
            [float(value) for value in control["pairedChangePercent"]["samples"]]
        )
        print(f"Android ({serial}) baseline-vs-compact-primary", flush=True)
        comparison = paired_runs(
            pairs,
            "baseline",
            "compact-primary",
            lambda pair, path: probe(remote_baseline, path),
            lambda pair, path: probe(remote_compact, path),
            output_dir / "android" / "algorithm-comparison",
            "stepsPerSecond",
        )
        after = android_context(adb, serial)
    finally:
        adb_output(adb, serial, ["shell", "rm", "-rf", remote_root], check=False)
    property_text = " ".join(before["properties"].values()).lower()
    return {
        "classification": "virtual-android"
                if before["properties"].get("ro.kernel.qemu") == "1"
                or "waydroid" in property_text
                else "physical-device",
        "abi": abi,
        "contextBefore": before,
        "contextAfter": after,
        "noiseControl": control,
        "noise": noise,
        "dynamicComparison": comparison,
        "probeBinaries": {
            "baseline": baseline_metadata,
            "compactPrimary": compact_metadata,
        },
    }


def markdown_report(report: dict[str, Any]) -> str:
    host = report["hostMeasurements"]
    dynamic = host["dynamicComparison"]
    stationary = host["stationaryComparison"]
    android = report.get("androidMeasurements")
    lines = [
        "# Reproducible simulation throughput baseline",
        "",
        f"Captured: `{report['createdAtUtc']}`  ",
        f"Source tree: `{report['source']['fingerprint']['sha256']}`  ",
        f"Complete working tree: `{report['source']['actualWorkingTree']['sha256']}`  ",
        f"Git HEAD: `{report['source']['git']['revision']}`  ",
        f"Status: **{report['status']}**",
        "",
        "## Result",
        "",
        f"- Host noise band: `±{host['noise']['noiseBandPercent']:.3f}%`.",
        f"- Dynamic tardigrade compact-primary change: "
        f"`{dynamic['pairedChangePercent']['median']:+.3f}%` (paired median).",
        f"- Stationary tardigrade compact-primary change: "
        f"`{stationary['pairedChangePercent']['median']:+.3f}%` (paired median).",
        f"- Real ordered streams exactly equal: `{host['orderedTrace']['orderedStreamsExactlyEqual']}`.",
        f"- Synthetic workloads captured: `{len(host['syntheticMatrix'])}`.",
        "- The capture infrastructure does not change production simulation behavior.",
        "",
        "## Correctness and provenance",
        "",
        f"- Source manifest files: `{report['source']['fingerprint']['fileCount']}`.",
        f"- Behavior baseline: baseline=`{report['regression']['behavior']['baseline']['passed']}`, "
        f"compact-primary=`{report['regression']['behavior']['compact-primary']['passed']}`.",
        "- Profile-OFF is the throughput signal; Profile-ON is attribution only.",
        "- Both algorithms are selected at compile time with runtime selection disabled.",
        "- Raw reports, binaries, compile commands, CMake caches, APKs and logs are siblings of this file.",
    ]
    if android is not None:
        lines.extend([
            "",
            "## Android target",
            "",
            f"- Serial: `{android['contextBefore']['serial']}`.",
            f"- Classification: `{android['classification']}`.",
            f"- ABI: `{android['abi']}`.",
            f"- Local Android noise band: `±{android['noise']['noiseBandPercent']:.3f}%`.",
            f"- Compact-primary change: "
            f"`{android['dynamicComparison']['pairedChangePercent']['median']:+.3f}%`.",
            "- Android measurements are optional diagnostics; device type does not gate promotion or release.",
        ])
    lines.extend([
        "",
        "## Noise definition",
        "",
        f"`{host['noise']['formula']}` from alternating baseline-vs-baseline process pairs. "
        "No fixed percentage is reused across devices.",
        "",
        "## Reproduction",
        "",
        "```bash",
        "python3 scripts/native/capture-throughput-baseline.py \\",
        "  --substrate /absolute/path/to/tardigrade.substrate \\",
        "  --android-serial <adb-serial> \\",
        "  --output-root /absolute/path/to/.codex/simulation-throughput-baseline",
        "```",
        "",
        "See `baseline.json` for every sample and artifact reference.",
        "",
    ])
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--substrate", required=True, type=Path)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument(
        "--build-root",
        type=Path,
        default=Path(tempfile.gettempdir()) / "cellularity-throughput-builds",
    )
    parser.add_argument("--host-pairs", type=int, default=11)
    parser.add_argument("--android-pairs", type=int, default=7)
    parser.add_argument("--warmup-steps", type=int, default=200)
    parser.add_argument("--measured-steps", type=int, default=5000)
    parser.add_argument("--android-serial", default=DEFAULT_ANDROID_SERIAL)
    parser.add_argument("--skip-android", action="store_true")
    parser.add_argument("--skip-apk", action="store_true")
    parser.add_argument("--quick", action="store_true", help="infrastructure smoke, not a baseline")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        substrate = args.substrate.resolve()
        if not substrate.is_file():
            raise CaptureError(f"Substrate not found: {substrate}")
        if args.host_pairs < 1 or args.android_pairs < 1:
            raise CaptureError("Pair counts must be positive")
        if not args.skip_android and not args.android_serial:
            raise CaptureError(
                "--android-serial is required unless --skip-android is used"
            )
        output_root = args.output_root.resolve()
        output_root.mkdir(parents=True, exist_ok=True)
        output_dir = output_root / f"run-{timestamp()}"
        output_dir.mkdir()
        copied_substrate = output_dir / "input" / substrate.name
        copied_substrate.parent.mkdir(parents=True)
        shutil.copy2(substrate, copied_substrate)

        fingerprint = benchmark.source_fingerprint()
        write_json(output_dir / "source-manifest.json", fingerprint)
        fingerprint_summary = {key: value for key, value in fingerprint.items() if key != "entries"}
        working_tree = benchmark.working_tree_fingerprint()
        write_json(output_dir / "working-tree-manifest.json", working_tree)
        working_tree_summary = {
            key: value for key, value in working_tree.items() if key != "entries"
        }
        git = benchmark.git_metadata()
        before_thermal = thermal_snapshot()
        cmake = benchmark.find_cmake()

        print(f"Throughput baseline artifacts: {output_dir}", flush=True)
        variants = build_host_variants(output_dir, args.build_root.resolve(), fingerprint)
        regression = run_behavior_and_memory(variants, output_dir, cmake)
        host_measurements = run_host_measurements(
            variants,
            copied_substrate,
            output_dir,
            1 if args.quick else args.host_pairs,
            1 if args.quick else args.warmup_steps,
            2 if args.quick else args.measured_steps,
            args.quick,
        )
        apks = None if args.skip_apk else build_android_apks(output_dir)
        android = None
        if not args.skip_android:
            android = run_android_measurements(
                output_dir,
                copied_substrate,
                fingerprint,
                args.android_serial,
                1 if args.quick else args.android_pairs,
                args.warmup_steps,
                args.measured_steps,
                args.quick,
            )

        all_traces_equal = host_measurements["orderedTrace"]["orderedStreamsExactlyEqual"] and all(
            workload["trace"]["orderedStreamsExactlyEqual"]
            for workload in host_measurements["syntheticMatrix"].values()
        )
        report = {
            "schemaVersion": 1,
            "kind": "cellularity.simulation-throughput-baseline",
            "createdAtUtc": datetime.now(timezone.utc).isoformat(),
            "status": "smoke-only" if args.quick else (
                "valid" if all_traces_equal else "invalid-ordered-trace-mismatch"
            ),
            "quickMode": args.quick,
            "source": {
                "git": git,
                "fingerprint": fingerprint_summary,
                "sourceManifest": {
                    "path": "source-manifest.json",
                    "sha256": sha256_file(output_dir / "source-manifest.json"),
                },
                "actualWorkingTree": working_tree_summary,
                "workingTreeManifest": {
                    "path": "working-tree-manifest.json",
                    "sha256": sha256_file(output_dir / "working-tree-manifest.json"),
                },
            },
            "input": {
                "path": copied_substrate.relative_to(output_dir).as_posix(),
                "sha256": sha256_file(copied_substrate),
                "sizeBytes": copied_substrate.stat().st_size,
            },
            "policy": {
                "throughputBuild": "Release, Profile-OFF, IPO/LTO ON, runtime overlap selection OFF",
                "attributionBuild": "Release, Profile-ON, IPO/LTO ON, runtime overlap selection OFF",
                "correctness": "serialized states must match exactly",
                "orderedTraceExactAcrossAlgorithms": all_traces_equal,
                "noise": host_measurements["noise"]["formula"],
            },
            "host": benchmark.host_metadata(),
            "toolchain": benchmark.toolchain_metadata(
                cmake, variants["baseline-profile-off"]["environment"]
            ),
            "hostThermalBefore": before_thermal,
            "hostThermalAfter": thermal_snapshot(),
            "binaries": {key: value["metadata"] for key, value in variants.items()},
            "regression": regression,
            "hostMeasurements": host_measurements,
            "androidApks": apks,
            "androidMeasurements": android,
        }
        write_json(output_dir / "baseline.json", report)
        (output_dir / "THROUGHPUT_BASELINE_REPORT.md").write_text(
            markdown_report(report), encoding="utf-8"
        )
        (output_root / "LATEST_RUN.txt").write_text(output_dir.name + "\n", encoding="utf-8")
        print(
            f"Throughput baseline report: {output_dir / 'THROUGHPUT_BASELINE_REPORT.md'}",
            flush=True,
        )
        return 0
    except (
        CaptureError,
        benchmark.BenchmarkError,
        OSError,
        ValueError,
        json.JSONDecodeError,
        subprocess.SubprocessError,
    ) as error:
        print(f"Throughput baseline capture failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
