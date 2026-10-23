#!/usr/bin/env python3
"""Capture, audit, and compare the local GLES renderer baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


SCRIPT_PATH = Path(__file__).resolve()
REPO_ROOT = SCRIPT_PATH.parents[2]
NATIVE_ROOT = REPO_ROOT / "native"
ASSET_ROOT = NATIVE_ROOT / "rendering" / "assets"
DEFAULT_BUILD_DIR = REPO_ROOT / ".codex" / "build" / "gles-baseline-probe"
DEFAULT_FIXTURE = (
    REPO_ROOT
    / ".codex"
    / "gles-reference-baselines"
    / "fixtures"
    / "tardigrade.substrate"
)
WIDTH = 2560
HEIGHT = 1390
DENSITY_DPI = 180


class BaselineError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BaselineError(f"Could not read {path}: {error}") from error
    if not isinstance(value, dict):
        raise BaselineError(f"Expected a JSON object in {path}")
    return value


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def run(
    command: list[str],
    *,
    cwd: Path = REPO_ROOT,
    environment: dict[str, str] | None = None,
) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        tail = "\n".join(result.stdout.splitlines()[-100:])
        raise BaselineError(
            f"Command failed with exit code {result.returncode}: "
            f"{' '.join(command)}\n{tail}"
        )
    return result.stdout.strip()


def find_program(name: str, fallback: Path | None = None) -> str:
    located = shutil.which(name)
    if located is not None:
        return located
    if fallback is not None and fallback.is_file():
        return str(fallback)
    raise BaselineError(f"Required command is missing: {name}")


def configured_android_sdk() -> Path | None:
    for variable in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        value = os.environ.get(variable)
        if value:
            candidate = Path(value).expanduser()
            if candidate.is_dir():
                return candidate

    local_properties = REPO_ROOT / "local.properties"
    if local_properties.is_file():
        for line in local_properties.read_text(encoding="utf-8").splitlines():
            if line.startswith("sdk.dir="):
                value = line.removeprefix("sdk.dir=").replace(r"\:", ":")
                candidate = Path(value.replace(r"\\", "\\")).expanduser()
                if candidate.is_dir():
                    return candidate

    conventional = Path.home() / "Android" / "Sdk"
    return conventional if conventional.is_dir() else None


def pinned_cmake_version() -> str:
    catalog = (REPO_ROOT / "gradle" / "libs.versions.toml").read_text(
        encoding="utf-8"
    )
    match = re.search(r'^cmake\s*=\s*"([^"]+)"\s*$', catalog, re.MULTILINE)
    if match is None:
        raise BaselineError("Could not read the pinned CMake version")
    return match.group(1)


def build_programs() -> tuple[str, str]:
    sdk = configured_android_sdk()
    sdk_bin = (
        sdk / "cmake" / pinned_cmake_version() / "bin"
        if sdk is not None
        else None
    )
    cmake_fallback = sdk_bin / "cmake" if sdk_bin is not None else None
    ninja_fallback = sdk_bin / "ninja" if sdk_bin is not None else None
    return (
        str(cmake_fallback)
        if cmake_fallback is not None and cmake_fallback.is_file()
        else find_program("cmake"),
        str(ninja_fallback)
        if ninja_fallback is not None and ninja_fallback.is_file()
        else find_program("ninja"),
    )


def source_fingerprint() -> str:
    roots = [
        NATIVE_ROOT / "app",
        NATIVE_ROOT / "core",
        NATIVE_ROOT / "execution",
        NATIVE_ROOT / "formats",
        NATIVE_ROOT / "io",
        NATIVE_ROOT / "render",
        NATIVE_ROOT / "rendering" / "assets",
    ]
    files = sorted(
        path
        for root in roots
        for path in root.rglob("*")
        if path.is_file()
    )
    digest = hashlib.sha256()
    for path in files:
        relative = path.relative_to(REPO_ROOT).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        digest.update(bytes.fromhex(sha256_file(path)))
    return digest.hexdigest()


def configure_and_build(build_dir: Path) -> Path:
    cmake, ninja = build_programs()
    clang = find_program("clang")
    clangxx = find_program("clang++")
    run(
        [
            cmake,
            "-S",
            str(NATIVE_ROOT),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            f"-DCMAKE_C_COMPILER={clang}",
            f"-DCMAKE_CXX_COMPILER={clangxx}",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCELLULARITY_BUILD_HOST_GLES_PROBE=ON",
            "-DCELLULARITY_ENABLE_NATIVE_PROFILE=OFF",
            "-DCELLULARITY_ENABLE_IPO=OFF",
            "-DCELLULARITY_ENABLE_RUNTIME_OVERLAP_SELECTION=OFF",
            "-DCELLULARITY_ENABLE_CELL_OWNED_SIGNAL_KERNEL=OFF",
        ]
    )
    run(
        [
            cmake,
            "--build",
            str(build_dir),
            "--target",
            "cellularity_gles_baseline_probe",
            "--parallel",
        ]
    )
    executable = build_dir / "cellularity_gles_baseline_probe"
    if not executable.is_file():
        raise BaselineError(f"Probe executable was not produced: {executable}")
    return executable


def prepare_raw_textures(directory: Path) -> None:
    magick = find_program("magick")
    directory.mkdir(parents=True, exist_ok=True)
    for name in ("small_dots", "gauss_bessel_integral"):
        destination = directory / f"{name}.rgba8"
        run(
            [
                magick,
                str(ASSET_ROOT / "textures" / f"{name}.png"),
                "-alpha",
                "on",
                "-depth",
                "8",
                f"rgba:{destination}",
            ]
        )
        if destination.stat().st_size != 256 * 256 * 4:
            raise BaselineError(f"Unexpected raw texture size: {destination}")


def run_probe(
    executable: Path,
    substrate: Path,
    output: Path,
    samples: int,
    temporary_root: Path,
) -> dict[str, Any]:
    texture_root = temporary_root / "textures"
    prepare_raw_textures(texture_root)
    output.mkdir(parents=True, exist_ok=False)
    environment = os.environ.copy()
    environment.setdefault("EGL_PLATFORM", "surfaceless")
    run(
        [
            str(executable),
            str(ASSET_ROOT),
            str(texture_root),
            str(substrate),
            str(output),
            str(samples),
        ],
        environment=environment,
    )
    report = read_json(output / "probe.json")
    frame = report.get("frame")
    if not isinstance(frame, dict) or (
        frame.get("width"), frame.get("height"), frame.get("densityDpi")
    ) != (WIDTH, HEIGHT, DENSITY_DPI):
        raise BaselineError("Probe returned an unexpected framebuffer contract")
    return report


def raw_paths(report: dict[str, Any], root: Path) -> list[Path]:
    frames = report.get("frames")
    if not isinstance(frames, list) or len(frames) != 10:
        raise BaselineError("Probe must return exactly ten incubation frames")
    paths: list[Path] = []
    for index, frame in enumerate(frames):
        if not isinstance(frame, dict):
            raise BaselineError(f"Invalid frame record {index}")
        for key, expected_size in (
            ("colorRaw", WIDTH * HEIGHT * 4),
            ("depthRaw", WIDTH * HEIGHT * 2),
        ):
            relative = frame.get(key)
            if not isinstance(relative, str):
                raise BaselineError(f"Frame {index} has no {key}")
            path = root / relative
            if not path.is_file() or path.stat().st_size != expected_size:
                raise BaselineError(f"Invalid frame payload: {path}")
            paths.append(path)
    return paths


def pixel_manifest(report: dict[str, Any], root: Path) -> tuple[list[dict[str, Any]], str]:
    entries: list[dict[str, Any]] = []
    lines: list[str] = []
    for path in raw_paths(report, root):
        relative = path.relative_to(root).as_posix()
        digest = sha256_file(path)
        entries.append(
            {"path": relative, "bytes": path.stat().st_size, "sha256": digest}
        )
        lines.append(f"{digest}  {relative}\n")
    checksum_file = root / "pixel-sha256.txt"
    checksum_file.write_text("".join(lines), encoding="utf-8")
    return entries, sha256_file(checksum_file)


def generate_previews(report: dict[str, Any], root: Path) -> None:
    magick = find_program("magick")
    png_root = root / "png"
    png_root.mkdir(parents=True, exist_ok=True)
    color_previews: list[Path] = []
    depth_previews: list[Path] = []
    for frame in report["frames"]:
        name = frame["name"]
        color_raw = root / frame["colorRaw"]
        depth_raw = root / frame["depthRaw"]
        color_png = png_root / f"{name}.png"
        depth_png = png_root / f"{name}.depth.png"
        round_trip = png_root / f".{name}.roundtrip.rgba8"
        run(
            [
                magick,
                "-size",
                f"{WIDTH}x{HEIGHT}",
                "-depth",
                "8",
                f"rgba:{color_raw}",
                "-flip",
                "-units",
                "PixelsPerInch",
                "-density",
                str(DENSITY_DPI),
                "-define",
                "png:color-type=6",
                str(color_png),
            ]
        )
        run(
            [
                magick,
                str(color_png),
                "-flip",
                "-alpha",
                "on",
                "-depth",
                "8",
                f"rgba:{round_trip}",
            ]
        )
        if sha256_file(color_raw) != sha256_file(round_trip):
            raise BaselineError(f"Lossless PNG round-trip failed: {name}")
        round_trip.unlink()
        run(
            [
                magick,
                "-size",
                f"{WIDTH}x{HEIGHT}",
                "-depth",
                "16",
                "-endian",
                "LSB",
                f"gray:{depth_raw}",
                "-flip",
                "-auto-level",
                "-negate",
                "-depth",
                "8",
                "-units",
                "PixelsPerInch",
                "-density",
                str(DENSITY_DPI),
                str(depth_png),
            ]
        )
        color_previews.append(color_png)
        depth_previews.append(depth_png)

    run(
        [
            magick,
            "montage",
            *map(str, color_previews),
            "-thumbnail",
            "640x348",
            "-tile",
            "2x5",
            "-geometry",
            "+8+8",
            "-background",
            "#202124",
            str(png_root / "color-contact-sheet.png"),
        ]
    )
    run(
        [
            magick,
            "montage",
            *map(str, depth_previews),
            "-thumbnail",
            "640x348",
            "-tile",
            "2x5",
            "-geometry",
            "+8+8",
            "-background",
            "#202124",
            str(png_root / "depth-contact-sheet.png"),
        ]
    )


def git_value(*arguments: str) -> str:
    return run(["git", *arguments]).strip()


def finalize_capture(
    output: Path,
    report: dict[str, Any],
    substrate: Path,
    *,
    previews: bool,
) -> dict[str, Any]:
    files, overall = pixel_manifest(report, output)
    if previews:
        generate_previews(report, output)
    manifest = {
        "schemaVersion": 1,
        "format": "cellularity-host-gles-incubation-baseline-v1",
        "artifactRole": "immutable-reference",
        "capturedAtUtc": datetime.now(timezone.utc).isoformat(),
        "source": {
            "gitCommit": git_value("rev-parse", "HEAD"),
            "gitDirty": bool(git_value("status", "--porcelain")),
            "nativeSourceFingerprintSha256": source_fingerprint(),
        },
        "input": {
            "name": substrate.name,
            "bytes": substrate.stat().st_size,
            "sha256": sha256_file(substrate),
        },
        "target": {
            "width": WIDTH,
            "height": HEIGHT,
            "densityDpi": DENSITY_DPI,
            "color": "RGBA8",
            "depth": "DEPTH_COMPONENT16",
            "glVendor": report.get("glVendor"),
            "glRenderer": report.get("glRenderer"),
            "glVersion": report.get("glVersion"),
            "glslVersion": report.get("glslVersion"),
        },
        "frameCount": len(report["frames"]),
        "overallPixelSha256": overall,
        "files": files,
        "probe": "probe.json",
        "probeSha256": sha256_file(output / "probe.json"),
    }
    write_json(output / "manifest.json", manifest)
    return manifest


def audit_manifest(root: Path) -> dict[str, Any]:
    manifest = read_json(root / "manifest.json")
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise BaselineError(f"Baseline has no file inventory: {root}")
    checked = 0
    for entry in files:
        if not isinstance(entry, dict):
            raise BaselineError(f"Invalid file inventory entry in {root}")
        relative = entry.get("path")
        expected = entry.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected, str):
            raise BaselineError(f"Invalid file inventory entry in {root}")
        path = root / relative
        if not path.is_file():
            raise BaselineError(f"Baseline file is missing: {path}")
        actual = sha256_file(path)
        if actual != expected:
            raise BaselineError(
                f"Baseline checksum mismatch for {relative}: {actual} != {expected}"
            )
        checked += 1
    checksum_file = root / "pixel-sha256.txt"
    expected_overall = manifest.get("overallPixelSha256")
    if checksum_file.is_file() and isinstance(expected_overall, str):
        actual_overall = sha256_file(checksum_file)
        if actual_overall != expected_overall:
            raise BaselineError(
                f"Overall baseline checksum mismatch: {actual_overall} != "
                f"{expected_overall}"
            )
    return {
        "baseline": str(root),
        "format": manifest.get("format"),
        "filesChecked": checked,
        "overallPixelSha256": expected_overall,
    }


def compare_payloads(
    expected_root: Path,
    expected_manifest: dict[str, Any],
    actual_root: Path,
    actual_report: dict[str, Any],
) -> None:
    expected_target = expected_manifest.get("target")
    if not isinstance(expected_target, dict):
        raise BaselineError("Comparable host baseline has no target identity")
    for key in ("glVendor", "glRenderer", "glVersion", "glslVersion"):
        if expected_target.get(key) != actual_report.get(key):
            raise BaselineError(
                f"GLES identity differs for {key}: "
                f"{actual_report.get(key)!r} != {expected_target.get(key)!r}"
            )

    expected_files = expected_manifest.get("files")
    if not isinstance(expected_files, list):
        raise BaselineError("Comparable host baseline has no file inventory")
    actual_by_path = {
        path.relative_to(actual_root).as_posix(): sha256_file(path)
        for path in raw_paths(actual_report, actual_root)
    }
    expected_by_path = {
        entry["path"]: entry["sha256"]
        for entry in expected_files
        if isinstance(entry, dict)
        and isinstance(entry.get("path"), str)
        and isinstance(entry.get("sha256"), str)
    }
    if actual_by_path != expected_by_path:
        differing = sorted(
            path
            for path in set(actual_by_path) | set(expected_by_path)
            if actual_by_path.get(path) != expected_by_path.get(path)
        )
        raise BaselineError(
            "Rendered pixel payload differs from the baseline: "
            + ", ".join(differing)
        )

    for relative in actual_by_path:
        if not (expected_root / relative).is_file():
            raise BaselineError(f"Baseline payload is missing: {relative}")


def capture(arguments: argparse.Namespace) -> None:
    output = arguments.output.resolve()
    if output.exists():
        raise BaselineError(f"Capture destination already exists: {output}")
    substrate = arguments.substrate.resolve()
    if not substrate.is_file():
        raise BaselineError(f"Substrate fixture is missing: {substrate}")
    executable = configure_and_build(arguments.build_dir.resolve())
    with tempfile.TemporaryDirectory(prefix="cellularity-gles-capture-") as temporary:
        report = run_probe(
            executable,
            substrate,
            output,
            arguments.samples,
            Path(temporary),
        )
    manifest = finalize_capture(
        output, report, substrate, previews=not arguments.no_previews
    )
    print(
        json.dumps(
            {
                "result": "captured",
                "baseline": str(output),
                "overallPixelSha256": manifest["overallPixelSha256"],
                "glRenderer": manifest["target"]["glRenderer"],
            },
            indent=2,
        )
    )


def verify(arguments: argparse.Namespace) -> None:
    baseline = arguments.baseline.resolve()
    audit_manifest(baseline)
    manifest = read_json(baseline / "manifest.json")
    if manifest.get("format") != "cellularity-host-gles-incubation-baseline-v1":
        raise BaselineError("Pixel replay requires a host GLES baseline")
    expected_source = manifest.get("source")
    if not isinstance(expected_source, dict):
        raise BaselineError("Host baseline has no source fingerprint")
    baseline_fingerprint = expected_source.get("nativeSourceFingerprintSha256")
    current_fingerprint = source_fingerprint()
    source_fingerprint_matches = baseline_fingerprint == current_fingerprint
    if not source_fingerprint_matches and not arguments.compare_current_source:
        raise BaselineError(
            "Native source fingerprint differs from the immutable baseline; "
            "use --compare-current-source to render the current tree and "
            "compare its pixels without modifying the baseline"
        )

    substrate = arguments.substrate.resolve()
    expected_input = manifest.get("input")
    if not isinstance(expected_input, dict) or expected_input.get("sha256") != sha256_file(
        substrate
    ):
        raise BaselineError("Substrate fixture differs from the baseline input")

    executable = configure_and_build(arguments.build_dir.resolve())
    with tempfile.TemporaryDirectory(prefix="cellularity-gles-verify-") as temporary:
        temporary_root = Path(temporary)
        run_a = temporary_root / "run-a"
        run_b = temporary_root / "run-b"
        report_a = run_probe(
            executable,
            substrate,
            run_a,
            arguments.samples,
            temporary_root / "assets-a",
        )
        report_b = run_probe(
            executable,
            substrate,
            run_b,
            arguments.samples,
            temporary_root / "assets-b",
        )
        compare_payloads(baseline, manifest, run_a, report_a)
        compare_payloads(baseline, manifest, run_b, report_b)
        files_a = {
            path.relative_to(run_a).as_posix(): sha256_file(path)
            for path in raw_paths(report_a, run_a)
        }
        files_b = {
            path.relative_to(run_b).as_posix(): sha256_file(path)
            for path in raw_paths(report_b, run_b)
        }
        if files_a != files_b:
            raise BaselineError("Two fresh GLES captures are not byte-exact")
        verification = {
            "schemaVersion": 1,
            "verifiedAtUtc": datetime.now(timezone.utc).isoformat(),
            "result": "PASS",
            "baseline": str(baseline),
            "baselineSourceFingerprintSha256": baseline_fingerprint,
            "sourceFingerprintSha256": current_fingerprint,
            "sourceFingerprintMatchesBaseline": source_fingerprint_matches,
            "repeatCaptureExact": True,
            "overallPixelSha256": manifest.get("overallPixelSha256"),
            "diagnostics": {
                "runA": report_a.get("diagnostics"),
                "runB": report_b.get("diagnostics"),
            },
        }
        write_json(baseline / "last-verification.json", verification)
    print(json.dumps(verification, indent=2))


def audit(arguments: argparse.Namespace) -> None:
    reports = [audit_manifest(path.resolve()) for path in arguments.baselines]
    print(json.dumps({"result": "PASS", "baselines": reports}, indent=2))


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture_parser = subparsers.add_parser("capture")
    capture_parser.add_argument("output", type=Path)
    capture_parser.add_argument("--substrate", type=Path, default=DEFAULT_FIXTURE)
    capture_parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    capture_parser.add_argument("--samples", type=int, default=120)
    capture_parser.add_argument("--no-previews", action="store_true")
    capture_parser.set_defaults(callback=capture)

    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("baseline", type=Path)
    verify_parser.add_argument("--substrate", type=Path, default=DEFAULT_FIXTURE)
    verify_parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    verify_parser.add_argument("--samples", type=int, default=120)
    verify_parser.add_argument(
        "--compare-current-source",
        action="store_true",
        help=(
            "render the current source even when its fingerprint differs, "
            "then require byte-exact pixels against the immutable baseline"
        ),
    )
    verify_parser.set_defaults(callback=verify)

    audit_parser = subparsers.add_parser("audit")
    audit_parser.add_argument("baselines", nargs="+", type=Path)
    audit_parser.set_defaults(callback=audit)
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    if getattr(arguments, "samples", 1) < 1:
        raise BaselineError("Sample count must be positive")
    arguments.callback(arguments)


if __name__ == "__main__":
    try:
        main()
    except BaselineError as error:
        print(f"GLES baseline error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
