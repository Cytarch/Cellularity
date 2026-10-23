#!/usr/bin/env python3
"""Verify the native format layer remains independent and decomposed."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
NATIVE_ROOT = REPO_ROOT / "native"
FORMATS_ROOT = NATIVE_ROOT / "formats"
RELEASE_FORMATS_ROOT = FORMATS_ROOT / "release"
IO_ROOT = NATIVE_ROOT / "io"
CMAKE_PATH = NATIVE_ROOT / "CMakeLists.txt"
ANDROID_LEGACY_GENOME_CODEC = (
    REPO_ROOT
    / "app/src/main/java/com/cytarch/cellularity/platform/files/GenomeFileCodec.kt"
)

REQUIRED_COMPONENTS = {
    "CellSnapshotWireCodec.cpp",
    "CellSnapshotWireCodec.h",
    "DecodedWorldValidation.cpp",
    "DecodedWorldValidation.h",
    "FormatLimits.h",
    "GenomeDraftCodec.cpp",
    "GenomeDraftCodec.h",
    "InternalWireFormatConstants.h",
    "LegacyEnvironmentCodec.cpp",
    "LegacyEnvironmentCodec.h",
    "LegacyFormatConstants.h",
    "LegacyGeneCodec.cpp",
    "LegacyGeneCodec.h",
    "LegacySubstrateCodec.cpp",
    "LegacySubstrateCodec.h",
    "NativeSubstrateLoader.cpp",
    "NativeSubstrateLoader.h",
    "SnapshotCodecSupport.cpp",
    "SnapshotCodecSupport.h",
    "SnapshotFileIO.cpp",
    "SnapshotFileIO.h",
    "SubstrateFileCodec.cpp",
    "SubstrateFileCodec.h",
    "SubstrateFormatTypes.h",
}
REQUIRED_RELEASE_COMPONENTS = {
    "LogicalSaveSnapshot.h",
    "ReleaseGenomeCodec.cpp",
    "ReleaseGenomeCodec.h",
    "ReleaseSubstrateCodec.cpp",
    "ReleaseSubstrateCodec.h",
    "SaveContainerBody.cpp",
    "SaveContainerBody.h",
    "SaveContainerCodec.cpp",
    "SaveContainerCodec.h",
}
EXPECTED_IO_PRIMITIVES = {
    "DecodeError.h",
    "JavaObjectStreamReader.cpp",
    "JavaObjectStreamReader.h",
    "NativeByteStream.h",
    "Sha256.cpp",
    "Sha256.h",
}
OBSOLETE_MONOLITHS = (
    NATIVE_ROOT / "io/NativeSnapshotCodec.cpp",
    NATIVE_ROOT / "io/NativeSnapshotCodec.h",
    NATIVE_ROOT / "io/NativeSubstrateLoader.cpp",
    NATIVE_ROOT / "io/NativeSubstrateLoader.h",
)
FORBIDDEN_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"](?:android|app|jni|platform|render)/',
    re.MULTILINE,
)
FORBIDDEN_PLATFORM_INCLUDE = re.compile(
    r'^\s*#\s*include\s*<(?:android/|GLES|jni\.h)',
    re.MULTILINE,
)
FORBIDDEN_TYPE = re.compile(
    r"\b(?:NativeCellularityController|NativeSimulationWorldRenderer|"
    r"NativeGlApi|NativeWorldContext)\b"
)


def fail(messages: list[str]) -> None:
    print("native format boundary error:", file=sys.stderr)
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
    return set(re.findall(r"(?:formats|io)/[A-Za-z0-9_./-]+\.cpp", match.group("body")))


def call_body(cmake: str, function: str, first_argument: str) -> str | None:
    match = re.search(
        rf"{re.escape(function)}\(\s*{re.escape(first_argument)}\b(?P<body>.*?)\)",
        cmake,
        re.DOTALL,
    )
    return None if match is None else match.group("body")


def main() -> None:
    errors: list[str] = []
    if not FORMATS_ROOT.is_dir():
        fail(["native/formats is missing"])

    actual_components = {path.name for path in FORMATS_ROOT.iterdir() if path.is_file()}
    missing_components = sorted(REQUIRED_COMPONENTS - actual_components)
    if missing_components:
        errors.append(f"required codec components are missing: {missing_components}")

    actual_release_components = {
        path.name for path in RELEASE_FORMATS_ROOT.iterdir() if path.is_file()
    } if RELEASE_FORMATS_ROOT.is_dir() else set()
    if actual_release_components != REQUIRED_RELEASE_COMPONENTS:
        errors.append(
            "native/formats/release must contain the complete production CLTY codec: "
            f"missing={sorted(REQUIRED_RELEASE_COMPONENTS - actual_release_components)}, "
            f"extra={sorted(actual_release_components - REQUIRED_RELEASE_COMPONENTS)}"
        )

    actual_io = {path.name for path in IO_ROOT.iterdir() if path.is_file()}
    if actual_io != EXPECTED_IO_PRIMITIVES:
        errors.append(
            "native/io must contain only low-level decoding/integrity primitives: "
            f"expected={sorted(EXPECTED_IO_PRIMITIVES)}, actual={sorted(actual_io)}"
        )

    obsolete = [path.relative_to(REPO_ROOT).as_posix()
                for path in OBSOLETE_MONOLITHS if path.exists()]
    if obsolete:
        errors.append(f"obsolete format monoliths remain: {obsolete}")

    format_sources = sorted(
        path for directory in (FORMATS_ROOT, RELEASE_FORMATS_ROOT)
        for path in directory.iterdir()
        if path.is_file() and path.suffix in {".cpp", ".h"}
    )
    for source in format_sources:
        text = source.read_text(encoding="utf-8")
        for label, pattern in (
            ("application/render include", FORBIDDEN_INCLUDE),
            ("Android/GL include", FORBIDDEN_PLATFORM_INCLUDE),
            ("application/render type", FORBIDDEN_TYPE),
        ):
            match = pattern.search(text)
            if match is not None:
                line = text.count("\n", 0, match.start()) + 1
                relative = source.relative_to(NATIVE_ROOT).as_posix()
                errors.append(f"native/{relative}:{line}: forbidden {label}")

    cmake = CMAKE_PATH.read_text(encoding="utf-8")
    declared_sources = cmake_list(cmake, "CELLULARITY_FORMAT_SOURCES")
    expected_sources = {
        f"formats/{path.name}" for path in FORMATS_ROOT.glob("*.cpp")
    } | {
        f"formats/release/{path.name}" for path in RELEASE_FORMATS_ROOT.glob("*.cpp")
    } | {
        "io/JavaObjectStreamReader.cpp",
        "io/Sha256.cpp",
    }
    if declared_sources is None:
        errors.append("CELLULARITY_FORMAT_SOURCES is missing")
    elif declared_sources != expected_sources:
        errors.append(
            "CELLULARITY_FORMAT_SOURCES does not own every format unit exactly once: "
            f"missing={sorted(expected_sources - declared_sources)}, "
            f"extra={sorted(declared_sources - expected_sources)}"
        )

    format_links = call_body(cmake, "target_link_libraries", "cellularity_formats")
    if format_links is None:
        errors.append("cellularity_formats link declaration is missing")
    else:
        link_tokens = set(re.findall(r"[A-Za-z0-9_:+.-]+", format_links))
        if link_tokens != {
            "PRIVATE",
            "cellularity_simulation",
            "zstd_vendor",
            "z",
        }:
            errors.append(
                "cellularity_formats must link only simulation, vendored zstd, and zlib: "
                f"actual={sorted(link_tokens)}"
            )

    for test_target in (
        "cellularity_formats_parity_tests",
        "cellularity_format_semantic_tests",
        "cellularity_format_malformed_tests",
    ):
        target_region = re.search(
            rf"{re.escape(test_target)}(?P<body>.{{0,700}}?)(?:\n\s*\)|\n\s*add_test)",
            cmake,
            re.DOTALL,
        )
        if target_region is None:
            errors.append(f"independent format test target is missing: {test_target}")
            continue
        if re.search(r"\b(?:cellularity_android|cellularity_rendering|android|GLESv3)\b",
                     target_region.group("body")):
            errors.append(f"{test_target} depends on Android or rendering")

    header = (FORMATS_ROOT / "SubstrateFileCodec.h").read_text(encoding="utf-8")
    semantic_tests = (
        NATIVE_ROOT / "tests/format_semantic_tests.cpp"
    ).read_text(encoding="utf-8")
    if "inspectSubstrateHeader(" not in header:
        errors.append("independent inspectSubstrateHeader API is missing")
    if semantic_tests.count("inspectSubstrateHeader(") < 2:
        errors.append("current and legacy header-inspection tests are required")

    file_io = (FORMATS_ROOT / "SnapshotFileIO.cpp").read_text(encoding="utf-8")
    file_io_header = (FORMATS_ROOT / "SnapshotFileIO.h").read_text(encoding="utf-8")
    substrate_codec = (FORMATS_ROOT / "SubstrateFileCodec.cpp").read_text(
        encoding="utf-8"
    )
    loader = (FORMATS_ROOT / "NativeSubstrateLoader.cpp").read_text(
        encoding="utf-8"
    )
    legacy_codec = (FORMATS_ROOT / "LegacySubstrateCodec.cpp").read_text(
        encoding="utf-8"
    )
    legacy_gene_codec = (FORMATS_ROOT / "LegacyGeneCodec.cpp").read_text(
        encoding="utf-8"
    )
    if re.search(r"\breadSnapshotFile\s*\(", file_io + file_io_header):
        errors.append("whole-file native snapshot reader API must not return")
    if re.search(r"\bloadSubstratePath\s*\(", substrate_codec + header):
        errors.append("whole-file native substrate path API must not return")
    if "decodeReleaseSubstratePath(" not in loader:
        errors.append("release substrate path must use the seekable CLTY reader")
    if "loadLegacySubstratePathDirect(" not in loader:
        errors.append("Java legacy substrate path must use its direct bounded reader")
    if "decodeReleaseSubstrate(" in loader:
        errors.append("production CLTY path must not route through an in-memory file buffer")
    if ("{'C', 'L', 'S', 'B'}" not in loader
            or "UnsupportedFeature" not in loader
            or "Discarded pre-release Cellularity format" not in loader):
        errors.append("discarded pre-release formats must be rejected explicitly")
    if "inflateGzipStream(" not in legacy_codec \
            or "TemporarySnapshotFile" not in legacy_codec:
        errors.append("legacy gzip path must expand through a bounded temporary file")

    legacy_sources = [
        path.read_text(encoding="utf-8")
        for path in sorted(FORMATS_ROOT.glob("Legacy*Codec.*"))
    ]
    if ANDROID_LEGACY_GENOME_CODEC.is_file():
        legacy_sources.append(ANDROID_LEGACY_GENOME_CODEC.read_text(encoding="utf-8"))
    else:
        errors.append("Android legacy Genome codec is missing")
    canonical_dependency = re.compile(
        r'#\s*include\s*[<"]core/CanonicalGene\.h[>"]|'
        r'\bCanonicalGeneDefaults\s*(?:::|\.)'
    )
    if any(canonical_dependency.search(source) for source in legacy_sources):
        errors.append(
            "legacy decoders must not depend on current CanonicalGeneDefaults"
        )
    if not re.search(
        r"\b(?:Gene|GeneData)\s+legacyWireDefaults\s*\(\s*\)",
        legacy_gene_codec,
    ):
        errors.append("native legacy Gene decoder must define explicit wire defaults")

    if errors:
        fail(errors)
    print(
        "native format boundary verified: "
        f"{len(expected_sources) - 2} production format units, "
        f"{len(EXPECTED_IO_PRIMITIVES)} low-level IO primitives, "
        "release/legacy header inspection and streamed path loading"
    )


if __name__ == "__main__":
    main()
