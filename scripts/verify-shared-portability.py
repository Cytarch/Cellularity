#!/usr/bin/env python3
"""Reject platform implementation dependencies from shared common Kotlin."""

from __future__ import annotations

import re
import sys
import tomllib
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SHARED_ROOT = REPOSITORY_ROOT / "shared"
VERSION_CATALOG = REPOSITORY_ROOT / "gradle/libs.versions.toml"
COMMON_SOURCE_ROOTS = (
    SHARED_ROOT / "src/commonMain/kotlin",
    SHARED_ROOT / "src/commonTest/kotlin",
)
JVM_SOURCE_ROOTS = (SHARED_ROOT / "src/jvmMain/kotlin",)

FORBIDDEN_IMPORT_PREFIXES = (
    "android.",
    "java.",
    "javax.",
    "kotlin.jvm.",
    "org.jetbrains.skia.",
    "org.jetbrains.skiko.",
    "org.lwjgl.",
    "org.jetbrains.compose.desktop.",
)
FORBIDDEN_ANDROIDX_PREFIXES = (
    "androidx.activity.",
    "androidx.annotation.",
    "androidx.appcompat.",
    "androidx.core.",
    "androidx.fragment.",
    "androidx.lifecycle.",
    "androidx.preference.",
)
FORBIDDEN_JVM_IMPLEMENTATION_IMPORT_PREFIXES = (
    "android.",
    "androidx.activity.",
    "androidx.appcompat.",
    "androidx.core.",
    "androidx.fragment.",
    "androidx.lifecycle.",
    "androidx.preference.",
    "java.awt.",
    "javax.swing.",
    "org.jetbrains.compose.desktop.",
    "org.jetbrains.skia.",
    "org.jetbrains.skiko.",
    "org.lwjgl.",
)
FORBIDDEN_DESKTOP_WINDOW_IMPORT = re.compile(
    r"^androidx\.compose\.ui\.window\."
    r"(?:Window|WindowScope|WindowState|application|exitApplication|"
    r"rememberWindowState|singleWindowApplication|Tray|TrayState|"
    r"Notification|MenuBar|\*)$"
)
IMPORT_PATTERN = re.compile(r"^\s*import\s+([^\s;]+)", re.MULTILINE)
FORBIDDEN_QUALIFIED_REFERENCE = re.compile(
    r"(?<![A-Za-z0-9_])(?:"
    r"android\."
    r"|java\."
    r"|javax\."
    r"|kotlin\.jvm\."
    r"|androidx\.(?!compose\.)"
    r"|org\.jetbrains\.(?:skia|skiko)\."
    r"|org\.jetbrains\.compose\.desktop\."
    r"|org\.lwjgl\."
    r"|com\.cytarch\.cellularity\.(?!shared(?:\.|$))"
    r")"
)
FORBIDDEN_CODE_PATTERNS = (
    ("JNI declaration", re.compile(r"\bexternal\s+fun\b")),
    ("native library load", re.compile(r"\bSystem\s*\.\s*loadLibrary\s*\(")),
    (
        "desktop window API",
        re.compile(
            r"\bandroidx\.compose\.ui\.window\."
            r"(?:Window|WindowScope|WindowState|application|exitApplication|"
            r"rememberWindowState|singleWindowApplication|Tray|TrayState|"
            r"Notification|MenuBar)\b"
        ),
    ),
)


def fail(message: str) -> None:
    print(f"shared portability error: {message}", file=sys.stderr)
    raise SystemExit(1)


def kotlin_sources(roots: tuple[Path, ...]) -> list[Path]:
    return sorted(
        source
        for root in roots
        if root.is_dir()
        for source in root.rglob("*.kt")
    )


def code_without_comments_or_literals(text: str) -> str:
    """Mask Kotlin comments and literals while preserving offsets and newlines."""
    result = list(text)
    index = 0
    block_depth = 0
    state = "code"
    while index < len(text):
        if state == "code":
            if text.startswith("//", index):
                result[index:index + 2] = "  "
                index += 2
                state = "line_comment"
            elif text.startswith("/*", index):
                result[index:index + 2] = "  "
                index += 2
                block_depth = 1
                state = "block_comment"
            elif text.startswith('"""', index):
                result[index:index + 3] = "   "
                index += 3
                state = "triple_string"
            elif text[index] == '"':
                result[index] = " "
                index += 1
                state = "string"
            elif text[index] == "'":
                result[index] = " "
                index += 1
                state = "character"
            else:
                index += 1
        elif state == "line_comment":
            if text[index] == "\n":
                state = "code"
            else:
                result[index] = " "
            index += 1
        elif state == "block_comment":
            if text.startswith("/*", index):
                result[index:index + 2] = "  "
                block_depth += 1
                index += 2
            elif text.startswith("*/", index):
                result[index:index + 2] = "  "
                block_depth -= 1
                index += 2
                if block_depth == 0:
                    state = "code"
            else:
                if text[index] != "\n":
                    result[index] = " "
                index += 1
        elif state == "triple_string":
            if text.startswith('"""', index):
                result[index:index + 3] = "   "
                index += 3
                state = "code"
            else:
                if text[index] != "\n":
                    result[index] = " "
                index += 1
        else:
            if text[index] == "\\":
                result[index] = " "
                if index + 1 < len(text):
                    if text[index + 1] != "\n":
                        result[index + 1] = " "
                    index += 2
                else:
                    index += 1
            elif (state == "string" and text[index] == '"') or (
                state == "character" and text[index] == "'"
            ):
                result[index] = " "
                index += 1
                state = "code"
            else:
                if text[index] != "\n":
                    result[index] = " "
                index += 1
    return "".join(result)


def verify_common_imports() -> None:
    failures: list[str] = []
    for source in kotlin_sources(COMMON_SOURCE_ROOTS):
        text = source.read_text(encoding="utf-8")
        for match in IMPORT_PATTERN.finditer(text):
            imported = match.group(1)
            forbidden = imported.startswith(
                FORBIDDEN_IMPORT_PREFIXES + FORBIDDEN_ANDROIDX_PREFIXES
            ) or FORBIDDEN_DESKTOP_WINDOW_IMPORT.fullmatch(imported) is not None
            if imported.startswith("com.cytarch.cellularity."):
                forbidden = forbidden or not imported.startswith(
                    "com.cytarch.cellularity.shared."
                )
            if forbidden:
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{source.relative_to(REPOSITORY_ROOT)}:{line}: {imported}"
                )
        code = code_without_comments_or_literals(text)
        for match in FORBIDDEN_QUALIFIED_REFERENCE.finditer(code):
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{source.relative_to(REPOSITORY_ROOT)}:{line}: "
                f"qualified reference {match.group(0)}"
            )
        for label, pattern in FORBIDDEN_CODE_PATTERNS:
            for match in pattern.finditer(code):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{source.relative_to(REPOSITORY_ROOT)}:{line}: {label}"
                )
    if failures:
        fail("forbidden common import(s):\n  " + "\n  ".join(failures))


def verify_jvm_target_is_compile_only() -> None:
    failures: list[str] = []
    for source in kotlin_sources(JVM_SOURCE_ROOTS):
        text = source.read_text(encoding="utf-8")
        for match in IMPORT_PATTERN.finditer(text):
            imported = match.group(1)
            forbidden = imported.startswith(
                FORBIDDEN_JVM_IMPLEMENTATION_IMPORT_PREFIXES
            ) or FORBIDDEN_DESKTOP_WINDOW_IMPORT.fullmatch(imported) is not None
            if imported.startswith("com.cytarch.cellularity."):
                forbidden = forbidden or not imported.startswith(
                    "com.cytarch.cellularity.shared."
                )
            if forbidden:
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{source.relative_to(REPOSITORY_ROOT)}:{line}: {imported}"
                )
        code = code_without_comments_or_literals(text)
        for label, pattern in FORBIDDEN_CODE_PATTERNS:
            for match in pattern.finditer(code):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{source.relative_to(REPOSITORY_ROOT)}:{line}: {label}"
                )

    renderer_sources = sorted(
        source.relative_to(REPOSITORY_ROOT)
        for source_root in COMMON_SOURCE_ROOTS + JVM_SOURCE_ROOTS
        for source in source_root.glob("com/cytarch/cellularity/shared/render/**/*.kt")
    )
    if renderer_sources:
        failures.extend(f"project renderer source {source}" for source in renderer_sources)
    if failures:
        fail(
            "JVM target must remain a compile/test target without Android, "
            "window, JNI, or renderer code:\n  " + "\n  ".join(failures)
        )


def verify_no_obsolete_expect() -> None:
    obsolete_name = "PlatformFileRepositoryDefaults"
    matches: list[str] = []
    for source in sorted((SHARED_ROOT / "src").rglob("*.kt")):
        if obsolete_name in source.read_text(encoding="utf-8"):
            matches.append(str(source.relative_to(REPOSITORY_ROOT)))
    if matches:
        fail(f"obsolete expect/actual contract remains: {', '.join(matches)}")


def verify_no_desktop_application() -> None:
    desktop_source_sets = sorted(
        path.relative_to(REPOSITORY_ROOT)
        for path in (SHARED_ROOT / "src").glob("desktop*")
    )
    if desktop_source_sets:
        fail(
            "desktop source set(s) are outside F-01 scope: "
            + ", ".join(map(str, desktop_source_sets))
        )

    build_script = (SHARED_ROOT / "build.gradle").read_text(encoding="utf-8")
    forbidden_build_fragments = (
        "id 'application'",
        "compose.desktop",
        "application {",
        "mainClass",
        "nativeDistributions",
    )
    present = [
        fragment for fragment in forbidden_build_fragments if fragment in build_script
    ]
    if present:
        fail("desktop application configuration found: " + ", ".join(present))


def verify_jvm_target() -> None:
    build_script = (SHARED_ROOT / "build.gradle").read_text(encoding="utf-8")
    if re.search(r"(?m)^\s*jvm(?:\s*\(\s*\))?\s*(?:\{|$)", build_script) is None:
        fail("shared/build.gradle does not declare a plain JVM target")
    if not VERSION_CATALOG.is_file():
        fail("Gradle version catalog is missing")
    with VERSION_CATALOG.open("rb") as catalog_file:
        catalog = tomllib.load(catalog_file)
    catalog_target = str(catalog.get("versions", {}).get("jvmTarget", ""))
    if catalog_target != "17":
        fail("catalogued JVM bytecode target must remain Java 17")
    if build_script.count("libs.versions.jvmTarget.get()") < 2:
        fail("Android and plain JVM targets must both use the version catalog")
    if re.search(r"JvmTarget\.JVM_[0-9]+|fromTarget\(\s*[\"'][0-9]+", build_script):
        fail("shared/build.gradle must not duplicate the catalogued JVM target")


def main() -> None:
    verify_jvm_target()
    verify_common_imports()
    verify_jvm_target_is_compile_only()
    verify_no_obsolete_expect()
    verify_no_desktop_application()
    print(
        "shared portability verified: common imports are platform-neutral; "
        "plain JVM target present; no desktop application"
    )


if __name__ == "__main__":
    main()
