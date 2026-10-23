#!/usr/bin/env python3
"""Capture an opt-in Cellularity render profile from an explicit Android target."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


PACKAGE = "com.cytarch.cellularity"
RECEIVER = f"{PACKAGE}/com.cytarch.cellularity.debug.DebugRenderBackendReceiver"
ACTION_PREFIX = f"{PACKAGE}.debug"
LOG_TAG = "CellularityRenderDebug"
COMMAND_LOG: list[list[str]] = []
SCENARIOS = (
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
)


def run_adb(serial: str, *arguments: str) -> str:
    command = ["adb", "-s", serial, *arguments]
    COMMAND_LOG.append(command.copy())
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise RuntimeError(f"{' '.join(command)} failed: {detail}")
    return result.stdout.strip()


def device_property(serial: str, name: str) -> str:
    return run_adb(serial, "shell", "getprop", name).strip()


def verify_target(serial: str) -> dict[str, str]:
    device_state = run_adb(serial, "get-state")
    if device_state != "device":
        raise RuntimeError(f"Android target {serial!r} is not ready: {device_state!r}")

    properties = {
        "manufacturer": device_property(serial, "ro.product.manufacturer"),
        "model": device_property(serial, "ro.product.model"),
        "device": device_property(serial, "ro.product.device"),
        "hardware": device_property(serial, "ro.hardware"),
        "fingerprint": device_property(serial, "ro.build.fingerprint"),
        "android_release": device_property(serial, "ro.build.version.release"),
        "sdk": device_property(serial, "ro.build.version.sdk"),
        "kernel_qemu": device_property(serial, "ro.kernel.qemu"),
    }
    identity = " ".join(properties.values()).lower()
    is_waydroid = "waydroid" in identity
    is_android_emulator = (
        serial.startswith("emulator-")
        or properties["kernel_qemu"] == "1"
        or "sdk_gphone" in identity
        or "android sdk built for" in identity
    )
    if is_android_emulator and not is_waydroid:
        raise RuntimeError(
            "Android Emulator/AVD/QEMU targets are intentionally rejected; "
            "use the explicit Waydroid serial or a physical device"
        )
    properties["target_kind"] = "waydroid" if is_waydroid else "physical"
    return properties


def broadcast(serial: str, action: str, *extras: str) -> str:
    return run_adb(
        serial,
        "shell",
        "am",
        "broadcast",
        "-n",
        RECEIVER,
        "-a",
        f"{ACTION_PREFIX}.{action}",
        *extras,
    )


def display_size(serial: str) -> tuple[int, int]:
    output = run_adb(serial, "shell", "wm", "size")
    matches = re.findall(r"(?:Physical|Override) size:\s*(\d+)x(\d+)", output)
    if not matches:
        raise RuntimeError(f"Could not parse target display size: {output!r}")
    width, height = matches[-1]
    return int(width), int(height)


def display_metrics(serial: str) -> dict[str, int]:
    width, height = display_size(serial)
    density_output = run_adb(serial, "shell", "wm", "density")
    density_matches = re.findall(
        r"(?:Physical|Override) density:\s*(\d+)", density_output
    )
    result = {"width_pixels": width, "height_pixels": height}
    if density_matches:
        result["density_dpi"] = int(density_matches[-1])
    return result


def exercise_camera(serial: str, duration_seconds: float, sweep_count: int) -> None:
    width, height = display_size(serial)
    interval = duration_seconds / max(sweep_count, 1)
    y = int(height * 0.48)
    left = int(width * 0.38)
    right = int(width * 0.62)
    started_at = time.monotonic()
    for index in range(sweep_count):
        expected_start = started_at + index * interval
        remaining = expected_start - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)
        x1, x2 = (left, right) if index % 2 == 0 else (right, left)
        run_adb(
            serial,
            "shell",
            "input",
            "swipe",
            str(x1),
            str(y),
            str(x2),
            str(y),
            "220",
        )
    remaining = duration_seconds - (time.monotonic() - started_at)
    if remaining > 0:
        time.sleep(remaining)


def append_metric(metrics: dict[str, object], key: str, value: str) -> None:
    previous = metrics.get(key)
    if previous is None:
        metrics[key] = value
    elif isinstance(previous, list):
        previous.append(value)
    else:
        metrics[key] = [previous, value]


def parse_metrics(report: str) -> dict[str, object]:
    metrics: dict[str, object] = {}
    for line in report.splitlines():
        candidate = line.strip()
        if "=" not in candidate:
            continue
        key, value = candidate.split("=", 1)
        if re.fullmatch(r"[a-z][a-z0-9_]*", key):
            append_metric(metrics, key, value)
    return metrics


def memory_snapshot(serial: str) -> dict[str, int]:
    output = run_adb(serial, "shell", "dumpsys", "meminfo", "--local", PACKAGE)
    result: dict[str, int] = {}
    labels = {
        "Java Heap": "java_heap_pss_kb",
        "Native Heap": "native_heap_pss_kb",
        "Code": "code_pss_kb",
        "Stack": "stack_pss_kb",
        "Graphics": "graphics_pss_kb",
        "Private Other": "private_other_pss_kb",
        "System": "system_pss_kb",
    }
    for source_label, target_label in labels.items():
        match = re.search(
            rf"^\s*{re.escape(source_label)}:\s+(\d+)",
            output,
            re.MULTILINE,
        )
        if match:
            result[target_label] = int(match.group(1))
    for pattern, target_label in (
        (r"TOTAL PSS:\s+(\d+)", "total_pss_kb"),
        (r"TOTAL RSS:\s+(\d+)", "total_rss_kb"),
        (r"TOTAL SWAP PSS:\s+(\d+)", "total_swap_pss_kb"),
    ):
        match = re.search(pattern, output)
        if match:
            result[target_label] = int(match.group(1))
    return result


def memory_delta(before: dict[str, int], after: dict[str, int]) -> dict[str, int]:
    return {
        key: after[key] - before[key]
        for key in sorted(before.keys() & after.keys())
    }


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_metadata() -> dict[str, object]:
    repository = Path(__file__).resolve().parents[1]
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repository,
        text=True,
        capture_output=True,
        check=True,
    ).stdout.strip()
    status = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=repository,
        text=True,
        capture_output=True,
        check=True,
    ).stdout
    patch = subprocess.run(
        ["git", "diff", "--binary", "HEAD"],
        cwd=repository,
        capture_output=True,
        check=True,
    ).stdout
    digest = hashlib.sha256(patch)
    untracked = subprocess.run(
        ["git", "ls-files", "--others", "--exclude-standard", "-z"],
        cwd=repository,
        capture_output=True,
        check=True,
    ).stdout.split(b"\0")
    for raw_path in sorted(path for path in untracked if path):
        path = repository / raw_path.decode("utf-8")
        if not path.is_file():
            continue
        digest.update(b"\0untracked\0")
        digest.update(raw_path)
        digest.update(b"\0")
        digest.update(path.read_bytes())
    return {
        "revision": revision,
        "dirty": bool(status.strip()),
        "worktree_patch_sha256": digest.hexdigest(),
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Profile the currently open Cellularity scene. The target serial is "
            "mandatory; this tool never starts or selects an Android emulator."
        )
    )
    parser.add_argument("--serial", required=True, help="Exact adb target serial")
    parser.add_argument(
        "--backend",
        choices=("vulkan", "open_gl_es", "unchanged"),
        default="unchanged",
    )
    parser.add_argument(
        "--scene",
        required=True,
        help="Stable scene/save identifier recorded in the raw evidence",
    )
    parser.add_argument("--scenario", required=True, choices=SCENARIOS)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--settle", type=float, default=3.0)
    parser.add_argument(
        "--camera-sweeps",
        type=int,
        default=0,
        help="Issue alternating horizontal camera gestures during the sample",
    )
    parser.add_argument("--label", default="")
    parser.add_argument(
        "--notes",
        default="",
        help="Optional manual interaction/setup notes stored with the capture",
    )
    parser.add_argument("--apk", type=Path, help="Optional installed APK for hashing")
    parser.add_argument("--output", type=Path, help="Optional JSON output path")
    arguments = parser.parse_args()
    if arguments.duration <= 0:
        parser.error("--duration must be positive")
    if arguments.settle < 0:
        parser.error("--settle cannot be negative")
    if arguments.camera_sweeps < 0:
        parser.error("--camera-sweeps cannot be negative")
    if not arguments.scene.strip():
        parser.error("--scene must not be blank")
    if arguments.apk is not None and not arguments.apk.is_file():
        parser.error(f"APK does not exist: {arguments.apk}")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    profile_started = False
    try:
        device = verify_target(arguments.serial)
        display = display_metrics(arguments.serial)
        if arguments.backend != "unchanged":
            broadcast(
                arguments.serial,
                "SET_RENDER_BACKEND",
                "--es",
                "backend",
                arguments.backend,
            )
            time.sleep(arguments.settle)

        memory_before = memory_snapshot(arguments.serial)
        run_adb(arguments.serial, "logcat", "-c")
        broadcast(arguments.serial, "START_RENDER_PROFILE")
        profile_started = True
        if arguments.camera_sweeps > 0:
            exercise_camera(
                arguments.serial,
                arguments.duration,
                arguments.camera_sweeps,
            )
        else:
            time.sleep(arguments.duration)
        broadcast(arguments.serial, "DUMP_RENDER_DIAGNOSTICS")
        broadcast(arguments.serial, "STOP_RENDER_PROFILE")
        profile_started = False
        memory_after = memory_snapshot(arguments.serial)
        time.sleep(0.25)
        report = run_adb(
            arguments.serial,
            "logcat",
            "-d",
            "-v",
            "raw",
            "-s",
            f"{LOG_TAG}:I",
            "*:S",
        )
    except (OSError, RuntimeError, subprocess.SubprocessError) as failure:
        print(f"render profile failed: {failure}", file=sys.stderr)
        return 1
    finally:
        if profile_started:
            try:
                broadcast(arguments.serial, "STOP_RENDER_PROFILE")
            except (OSError, RuntimeError, subprocess.SubprocessError) as cleanup_failure:
                print(
                    f"render profile cleanup failed: {cleanup_failure}",
                    file=sys.stderr,
                )

    if "Render performance profile" not in report or "boundary_schema=1" not in report:
        print(
            "render profile failed: V-19 boundary diagnostics were not emitted; "
            "install a current diagnostic build and keep the requested scene open",
            file=sys.stderr,
        )
        return 1

    payload: dict[str, object] = {
        "schema": 2,
        "captured_at_utc": datetime.now(timezone.utc).isoformat(),
        "label": arguments.label,
        "notes": arguments.notes,
        "scene": arguments.scene,
        "scenario": arguments.scenario,
        "serial": arguments.serial,
        "requested_backend": arguments.backend,
        "duration_seconds": arguments.duration,
        "camera_sweeps": arguments.camera_sweeps,
        "device": device,
        "display": display,
        "git": git_metadata(),
        "memory_before_kb": memory_before,
        "memory_after_kb": memory_after,
        "memory_delta_kb": memory_delta(memory_before, memory_after),
        "metrics": parse_metrics(report),
        "raw_report": report,
        "invocation": [str(Path(__file__).resolve()), *sys.argv[1:]],
        "adb_commands": COMMAND_LOG,
    }
    if arguments.apk is not None:
        payload["apk"] = {
            "path": str(arguments.apk.resolve()),
            "sha256": sha256(arguments.apk),
        }

    encoded = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    if arguments.output is not None:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(encoded, encoding="utf-8")
        print(arguments.output.resolve())
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
