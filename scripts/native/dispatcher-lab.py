#!/usr/bin/env python3
"""Build and run the C-00 persistent-dispatcher laboratory."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
NATIVE_ROOT = REPO_ROOT / "native"


class LabError(RuntimeError):
    pass


def run(
    arguments: list[str],
    *,
    cwd: Path = REPO_ROOT,
    environment: dict[str, str] | None = None,
    capture: bool = False,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        arguments,
        cwd=cwd,
        env=environment,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
        check=False,
    )
    if check and result.returncode != 0:
        output = result.stdout or ""
        raise LabError(f"Command failed ({result.returncode}): {' '.join(arguments)}\n{output}")
    return result


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def preserve_build_metadata(build_dir: Path, output_dir: Path, prefix: str) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for source_name, destination_name in (
        ("CMakeCache.txt", f"{prefix}-CMakeCache.txt"),
        ("compile_commands.json", f"{prefix}-compile_commands.json"),
    ):
        source = build_dir / source_name
        if not source.is_file():
            continue
        destination = output_dir / destination_name
        shutil.copy2(source, destination)
        result[source_name] = {
            "path": destination.name,
            "sha256": sha256_file(destination),
            "sizeBytes": destination.stat().st_size,
        }
    return result


def source_fingerprint() -> dict[str, Any]:
    listed = run(
        ["git", "ls-files", "-co", "--exclude-standard", "-z"], capture=True
    ).stdout
    paths = sorted(value for value in listed.split("\0") if value)
    digest = hashlib.sha256()
    entries: list[dict[str, Any]] = []
    for relative in paths:
        path = REPO_ROOT / relative
        if not path.is_file():
            continue
        content = path.read_bytes()
        file_hash = hashlib.sha256(content).hexdigest()
        encoded = relative.encode("utf-8")
        digest.update(len(encoded).to_bytes(8, "little"))
        digest.update(encoded)
        digest.update(bytes.fromhex(file_hash))
        entries.append({"path": relative, "sizeBytes": len(content), "sha256": file_hash})
    revision = run(["git", "rev-parse", "HEAD"], capture=True).stdout.strip()
    dirty = bool(run(["git", "status", "--porcelain"], capture=True).stdout.strip())
    return {
        "sha256": digest.hexdigest(),
        "fileCount": len(entries),
        "revision": revision,
        "dirty": dirty,
        "entries": entries,
    }


def read_optional_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return None


def host_runtime_context() -> dict[str, Any]:
    cpu_frequency: list[dict[str, Any]] = []
    cpu_root = Path("/sys/devices/system/cpu")
    for directory in sorted(cpu_root.glob("cpu[0-9]*/cpufreq")):
        entry: dict[str, Any] = {"cpu": directory.parent.name}
        for name in (
            "scaling_governor",
            "scaling_cur_freq",
            "scaling_min_freq",
            "scaling_max_freq",
        ):
            entry[name] = read_optional_text(directory / name)
        cpu_frequency.append(entry)

    thermal_zones: list[dict[str, Any]] = []
    for directory in sorted(Path("/sys/class/thermal").glob("thermal_zone*")):
        thermal_zones.append(
            {
                "zone": directory.name,
                "type": read_optional_text(directory / "type"),
                "tempMilliCelsius": read_optional_text(directory / "temp"),
            }
        )

    pressure = {}
    for resource in ("cpu", "memory", "io"):
        pressure[resource] = read_optional_text(Path("/proc/pressure") / resource)

    try:
        load_average: list[float] | None = list(os.getloadavg())
    except OSError:
        load_average = None

    return {
        "capturedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "loadAverage": load_average,
        "procLoadavg": read_optional_text(Path("/proc/loadavg")),
        "pressure": pressure,
        "cpuFrequency": cpu_frequency,
        "thermalZones": thermal_zones,
    }


def find_cmake() -> Path:
    candidates = [
        shutil.which("cmake"),
        str(Path.home() / "Android/Sdk/cmake/3.22.1/bin/cmake"),
    ]
    for value in candidates:
        if value and Path(value).is_file():
            return Path(value)
    raise LabError("CMake was not found")


def find_ninja(cmake: Path) -> Path:
    candidates = [shutil.which("ninja"), str(cmake.parent / "ninja")]
    for value in candidates:
        if value and Path(value).is_file():
            return Path(value)
    raise LabError("Ninja was not found")


def compiler_path(name: str) -> Path:
    value = shutil.which(name)
    if not value:
        raise LabError(f"{name} was not found")
    return Path(value)


def configure_and_build_host(
    build_dir: Path, fingerprint: str, cmake: Path, ninja: Path
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
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_C_COMPILER={compiler_path('clang')}",
            f"-DCMAKE_CXX_COMPILER={compiler_path('clang++')}",
            "-DCELLULARITY_BUILD_DISPATCHER_LAB=ON",
            "-DCELLULARITY_ENABLE_NATIVE_PROFILE=OFF",
            "-DCELLULARITY_ENABLE_IPO=ON",
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
            "cellularity_dispatcher_lab",
            "cellularity_dispatcher_capacity_tests",
            "cellularity_dispatcher_allocation_tests",
            "--parallel",
        ]
    )
    run([str(build_dir / "cellularity_dispatcher_capacity_tests")])
    run([str(build_dir / "cellularity_dispatcher_allocation_tests")])
    binary = build_dir / "cellularity_dispatcher_lab"
    if not binary.is_file():
        raise LabError("Host dispatcher laboratory binary was not produced")
    return binary


def lab_arguments(
    output: Path,
    *,
    quick: bool,
    interference: str = "none",
    substrate: Path | None = None,
    selected_additional: int | None = None,
) -> list[str]:
    arguments = ["--output", str(output)]
    if quick:
        arguments.append("--quick")
    if interference != "none":
        if substrate is None:
            raise LabError("Interference measurement requires a substrate")
        selected = max(0, selected_additional or 0)
        additional_counts = "0" if selected == 0 else f"0,{selected}"
        arguments += [
            "--interference",
            interference,
            "--substrate",
            str(substrate),
            "--mode",
            "block",
            "--additional-counts",
            additional_counts,
            "--task-counts",
            "8,32,128",
            "--target-us",
            "100,400",
        ]
        if not quick:
            arguments += ["--lifecycle-samples", "9"]
    return arguments


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise LabError(f"Expected a JSON object in {path}")
    return value


def run_host_matrix(
    binary: Path, output_dir: Path, substrate: Path | None, quick: bool
) -> dict[str, Any]:
    base_path = output_dir / "host-base.json"
    run([str(binary), *lab_arguments(base_path, quick=quick)])
    base = load_json(base_path)
    selected = int(base["recommendation"]["additionalThreadCount"])
    interference_selected = selected
    if interference_selected == 0:
        interference_selected = min(1, int(base["capacity"]["safeMaxAdditionalThreads"]))
    result: dict[str, Any] = {
        "base": base_path.name,
        "selectedAdditionalThreadCount": selected,
    }
    if substrate is not None:
        for mode in ("active-preparation", "moving-camera"):
            destination = output_dir / f"host-{mode}.json"
            run(
                [
                    str(binary),
                    *lab_arguments(
                        destination,
                        quick=quick,
                        interference=mode,
                        substrate=substrate,
                        selected_additional=interference_selected,
                    ),
                ]
            )
            result[mode] = destination.name
    return result


def adb_path() -> Path:
    value = shutil.which("adb")
    if not value:
        raise LabError("adb was not found")
    return Path(value)


def adb_output(adb: Path, serial: str, arguments: list[str], check: bool = True) -> str:
    return run(
        [str(adb), "-s", serial, *arguments], capture=True, check=check
    ).stdout.strip()


def android_context(adb: Path, serial: str) -> dict[str, Any]:
    properties = {}
    for key in (
        "ro.product.manufacturer",
        "ro.product.model",
        "ro.product.device",
        "ro.product.cpu.abi",
        "ro.build.version.release",
        "ro.build.version.sdk",
        "ro.build.fingerprint",
        "ro.kernel.qemu",
        "ro.boot.qemu",
        "ro.hardware",
        "ro.product.name",
    ):
        properties[key] = adb_output(adb, serial, ["shell", "getprop", key], False)
    return {
        "serial": serial,
        "properties": properties,
        "cpuInfo": adb_output(adb, serial, ["shell", "cat", "/proc/cpuinfo"], False),
        "battery": adb_output(adb, serial, ["shell", "dumpsys", "battery"], False),
        "thermalService": adb_output(
            adb, serial, ["shell", "dumpsys", "thermalservice"], False
        ),
    }


def find_ndk() -> Path:
    candidates = [Path.home() / "Android/Sdk/ndk/28.2.13676358"]
    for variable in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        if os.environ.get(variable):
            candidates.append(Path(os.environ[variable]) / "ndk/28.2.13676358")
    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    raise LabError("Android NDK 28.2.13676358 was not found")


def build_android(
    abi: str, build_dir: Path, cmake: Path, ninja: Path
) -> dict[str, Path]:
    ndk = find_ndk()
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
            "-DCELLULARITY_BUILD_DISPATCHER_LAB=ON",
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
            "cellularity_dispatcher_lab",
            "cellularity_dispatcher_capacity_tests",
            "cellularity_dispatcher_allocation_tests",
            "--parallel",
        ]
    )
    binaries = {
        "laboratory": build_dir / "cellularity_dispatcher_lab",
        "capacity": build_dir / "cellularity_dispatcher_capacity_tests",
        "allocation": build_dir / "cellularity_dispatcher_allocation_tests",
    }
    missing = [name for name, path in binaries.items() if not path.is_file()]
    if missing:
        raise LabError(
            "Android dispatcher binaries were not produced: " + ", ".join(missing)
        )
    return binaries


def run_android_matrix(
    serial: str,
    binaries: dict[str, Path],
    output_dir: Path,
    substrate: Path | None,
    quick: bool,
    fingerprint: str,
) -> dict[str, Any]:
    adb = adb_path()
    if adb_output(adb, serial, ["get-state"], False) != "device":
        raise LabError(f"Android target {serial!r} is not connected")
    before = android_context(adb, serial)
    abi = before["properties"]["ro.product.cpu.abi"]
    remote_root = f"/data/local/tmp/cellularity-dispatcher-{fingerprint[:12]}"
    binary = binaries["laboratory"]
    remote_binary = f"{remote_root}/dispatcher-lab"
    remote_substrate = f"{remote_root}/input.substrate"
    device_validation: dict[str, Any] = {}

    def execute(name: str, arguments: list[str], trace: bool = False) -> dict[str, Any]:
        remote_output = f"{remote_root}/{name}.json"
        local_output = output_dir / f"android-{name}.json"
        trace_result: dict[str, Any] = {"requested": trace, "captured": False}
        remote_trace = f"{remote_root}/{name}.atrace"
        local_trace = output_dir / f"android-{name}.atrace"
        try:
            if trace:
                trace_result["startOutput"] = adb_output(
                    adb,
                    serial,
                    [
                        "shell",
                        "atrace",
                        "--async_start",
                        "-b",
                        "16384",
                        "sched",
                        "freq",
                        "idle",
                    ],
                    False,
                )
            adb_output(
                adb,
                serial,
                ["shell", remote_binary, *arguments, "--output", remote_output],
            )
            adb_output(adb, serial, ["pull", remote_output, str(local_output)])
        finally:
            if trace:
                trace_result["stopOutput"] = adb_output(
                    adb,
                    serial,
                    ["shell", "atrace", "--async_stop", "-z", "-o", remote_trace],
                    False,
                )
                pulled = run(
                    [str(adb), "-s", serial, "pull", remote_trace, str(local_trace)],
                    capture=True,
                    check=False,
                )
                trace_result["captured"] = (
                    pulled.returncode == 0 and local_trace.is_file()
                )
                if trace_result["captured"]:
                    trace_result["path"] = local_trace.name
                    trace_result["sha256"] = sha256_file(local_trace)
        return {"path": local_output.name, "trace": trace_result}

    try:
        adb_output(adb, serial, ["shell", "mkdir", "-p", remote_root])
        adb_output(adb, serial, ["push", str(binary), remote_binary])
        adb_output(adb, serial, ["shell", "chmod", "755", remote_binary])
        for name in ("capacity", "allocation"):
            local_test = binaries[name]
            remote_test = f"{remote_root}/{name}-test"
            adb_output(adb, serial, ["push", str(local_test), remote_test])
            adb_output(adb, serial, ["shell", "chmod", "755", remote_test])
            output = adb_output(adb, serial, ["shell", remote_test])
            device_validation[name] = {
                "passed": True,
                "stdout": output,
                "binarySha256": sha256_file(local_test),
                "binarySizeBytes": local_test.stat().st_size,
            }
        if substrate is not None:
            adb_output(adb, serial, ["push", str(substrate), remote_substrate])

        base_args = ["--quick"] if quick else []
        base_result = execute("base", base_args)
        base = load_json(output_dir / base_result["path"])
        selected = int(base["recommendation"]["additionalThreadCount"])
        properties = before["properties"]
        virtual_markers = (
            properties.get("ro.kernel.qemu") == "1"
            or properties.get("ro.boot.qemu") == "1"
            or serial.startswith("emulator-")
            or any(
                marker in " ".join(
                    (
                        properties.get("ro.hardware", ""),
                        properties.get("ro.product.device", ""),
                        properties.get("ro.product.name", ""),
                    )
                ).lower()
                for marker in ("goldfish", "ranchu", "cuttlefish", "waydroid")
            )
        )
        result: dict[str, Any] = {
            "before": before,
            "classification": (
                "physical-arm64"
                if abi == "arm64-v8a" and not virtual_markers
                else "virtual-or-non-arm64"
            ),
            "binary": {"sha256": sha256_file(binary), "sizeBytes": binary.stat().st_size},
            "deviceValidation": device_validation,
            "base": base_result,
            "selectedAdditionalThreadCount": selected,
        }
        if substrate is not None:
            if selected == 0:
                selected = min(1, int(base["capacity"]["safeMaxAdditionalThreads"]))
            additional_counts = "0" if selected == 0 else f"0,{selected}"
            common = [
                "--mode", "block",
                "--additional-counts", additional_counts,
                "--task-counts", "8,32,128",
                "--target-us", "100,400",
                "--substrate", remote_substrate,
            ]
            if quick:
                common.append("--quick")
            result["activePreparation"] = execute(
                "active-preparation",
                [*common, "--interference", "active-preparation"],
            )
            result["movingCamera"] = execute(
                "moving-camera",
                [*common, "--interference", "moving-camera"],
                trace=True,
            )
        result["after"] = android_context(adb, serial)
        return result
    finally:
        adb_output(adb, serial, ["shell", "rm", "-rf", remote_root], False)


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--substrate", type=Path)
    parser.add_argument("--android-serial", help="Explicit adb serial; never auto-selected")
    parser.add_argument("--quick", action="store_true", help="Smoke only, not a verdict")
    args = parser.parse_args()

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    substrate = args.substrate.resolve() if args.substrate else None
    if substrate is not None and not substrate.is_file():
        raise LabError(f"Substrate does not exist: {substrate}")

    fingerprint = source_fingerprint()
    write_json(output_dir / "source-manifest.json", fingerprint)
    cmake = find_cmake()
    ninja = find_ninja(cmake)
    build_key = fingerprint["sha256"][:16]
    host_build = Path(tempfile.gettempdir()) / f"cellularity-c00-host-{build_key}"
    host_binary = configure_and_build_host(host_build, build_key, cmake, ninja)
    preserved_host_binary = output_dir / "cellularity-dispatcher-lab-host"
    shutil.copy2(host_binary, preserved_host_binary)

    host_before = host_runtime_context()
    host_measurements = run_host_matrix(
        host_binary, output_dir, substrate, args.quick
    )
    host_after = host_runtime_context()

    report: dict[str, Any] = {
        "schemaVersion": 1,
        "scenario": "c00/dispatcher-laboratory-capture",
        "capturedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "quickSmoke": args.quick,
        "source": {key: value for key, value in fingerprint.items() if key != "entries"},
        "host": {
            "platform": platform.platform(),
            "processor": platform.processor(),
            "affinityLogicalProcessors": (
                sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
            ),
            "lscpu": run(["lscpu"], capture=True, check=False).stdout,
            "cpuInfo": Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace"),
            "runtimeBeforeMeasurements": host_before,
            "runtimeAfterMeasurements": host_after,
            "binary": {
                "path": preserved_host_binary.name,
                "sha256": sha256_file(preserved_host_binary),
                "sizeBytes": preserved_host_binary.stat().st_size,
            },
            "build": preserve_build_metadata(host_build, output_dir, "host"),
            "measurements": host_measurements,
        },
        "android": None,
    }

    if args.android_serial:
        adb = adb_path()
        context = android_context(adb, args.android_serial)
        abi = context["properties"]["ro.product.cpu.abi"]
        if abi not in ("arm64-v8a", "x86_64"):
            raise LabError(f"Unsupported Android ABI for dispatcher lab: {abi}")
        android_build = (
            Path(tempfile.gettempdir()) / f"cellularity-c00-android-{build_key}-{abi}"
        )
        android_binaries = build_android(abi, android_build, cmake, ninja)
        preserved_android_binaries = {
            "laboratory": output_dir / f"cellularity-dispatcher-lab-{abi}",
            "capacity": output_dir / f"cellularity-dispatcher-capacity-tests-{abi}",
            "allocation": output_dir
            / f"cellularity-dispatcher-allocation-tests-{abi}",
        }
        for name, source in android_binaries.items():
            shutil.copy2(source, preserved_android_binaries[name])
        report["android"] = run_android_matrix(
            args.android_serial,
            preserved_android_binaries,
            output_dir,
            substrate,
            args.quick,
            fingerprint["sha256"],
        )
        report["android"]["build"] = preserve_build_metadata(
            android_build, output_dir, f"android-{abi}"
        )

    write_json(output_dir / "capture.json", report)
    print(output_dir / "capture.json")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except LabError as error:
        print(f"dispatcher lab capture failed: {error}", file=sys.stderr)
        raise SystemExit(1)
