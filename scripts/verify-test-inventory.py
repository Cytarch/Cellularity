#!/usr/bin/env python3

from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]
NATIVE_SOURCE_DIRECTORY = REPO_ROOT / "native/tests/parity"
NATIVE_SOURCE_NAMES = (
    "environment_gene_tests.cpp",
    "formats_tests.cpp",
    "interaction_tests.cpp",
    "rendering_tests.cpp",
    "simulation_tests.cpp",
    "topology_tests.cpp",
)
NATIVE_MANIFEST = REPO_ROOT / "native/tests/baseline/native-parity-cases-v1.txt"
JVM_MANIFEST = REPO_ROOT / "app/src/test/baseline/jvm-expectations-v1.txt"
TEST_SOURCE_SETS = (
    ("shared common", REPO_ROOT / "shared/src/commonTest", 145),
    ("shared JVM", REPO_ROOT / "shared/src/jvmTest", 8),
    ("shared Android host", REPO_ROOT / "shared/src/androidHostTest", 7),
    ("app unit", REPO_ROOT / "app/src/test", 246),
)
PYRAMID_EXPECTATIONS = (
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/settings/CellularitySettingsJsonTest.kt#currentSchemaRoundTripsAndIgnoresUnknownCurrentSchemaFields",
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/files/CellularityFileContractTest.kt#storedSubstrateIdAcceptsDisplayNamesButRejectsPaths",
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/main_menu/MainMenuStoreTest.kt#successfulSubstrateImportEmitsMessageThenTypedOpenEffectOnce",
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/main_menu/freezer/FreezerStoreTest.kt#reducerCreatesTypedOpenEffectAndSuppressesDuplicateLaunch",
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/main_menu/gene_bank/GeneBankStoreTest.kt#shareActionEmitsTheStorageIdRatherThanTheDisplayName",
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/main_menu/settings/SettingsStoreTest.kt#failedWriteKeepsCommittedStateAndEmitsOneTypedEffect",
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/play/PlayStoreTest.kt#startOpensStoredSourceOnceAndPublishesReadyCleanState",
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/play/genome_editor/GenomeEditorReducerTest.kt#invalidEditIsVisibleButNeverReachesPreviewOrSaveEffect",
    "shared/src/commonTest/kotlin/com/cytarch/cellularity/shared/ui/play/substrate_info/SubstrateInfoMapperTest.kt#partitionsEverySliderIntoExactlyOneSection",
)
TEST_METHOD_PATTERN = re.compile(
    r"@Test\b(?:(?!@Test).)*?\b(?:void|fun)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
    re.DOTALL,
)


def manifest_lines(path: Path) -> list[str]:
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def fail(message: str) -> None:
    print(f"test inventory error: {message}", file=sys.stderr)
    raise SystemExit(1)


def compare_ordered(label: str, expected: list[str], actual: list[str]) -> None:
    if expected == actual:
        return
    missing = [name for name in expected if name not in actual]
    added = [name for name in actual if name not in expected]
    fail(f"{label} changed (expected={len(expected)}, actual={len(actual)}, "
         f"missing={missing}, added={added})")


def test_methods(source_path: Path) -> list[str]:
    source = source_path.read_text(encoding="utf-8")
    annotation_count = len(re.findall(r"^\s*@Test\b", source, re.MULTILINE))
    methods = TEST_METHOD_PATTERN.findall(source)
    if len(methods) != annotation_count:
        fail(
            f"could not map every @Test annotation in "
            f"{source_path.relative_to(REPO_ROOT)} "
            f"(annotations={annotation_count}, methods={len(methods)})"
        )
    return methods


def verify_test_source_sets() -> None:
    summaries: list[str] = []
    for label, source_directory, expected_count in TEST_SOURCE_SETS:
        if not source_directory.is_dir():
            fail(f"test source set is missing: {source_directory.relative_to(REPO_ROOT)}")
        entries: list[str] = []
        source_paths = sorted(
            path for path in source_directory.rglob("*")
            if path.suffix in {".java", ".kt"}
        )
        for source_path in source_paths:
            relative_path = source_path.relative_to(REPO_ROOT).as_posix()
            entries.extend(
                f"{relative_path}#{method}" for method in test_methods(source_path)
            )
        if len(entries) != expected_count:
            fail(
                f"{label} test count changed "
                f"(expected={expected_count}, actual={len(entries)})"
            )
        if len(entries) != len(set(entries)):
            fail(f"{label} contains duplicate test entries")
        summaries.append(f"{label}={len(entries)}")
    print("JVM/Android source inventory: " + ", ".join(summaries))


def verify_expectations(label: str, entries: list[str] | tuple[str, ...]) -> None:
    for entry in entries:
        try:
            relative_path, method = entry.split("#", 1)
        except ValueError:
            fail(f"invalid {label} entry: {entry}")
        source_path = REPO_ROOT / relative_path
        if not source_path.is_file():
            fail(f"{label} source is missing: {relative_path}")
        if method not in test_methods(source_path):
            fail(f"{label} is missing or is no longer a test: {entry}")


def verify_native_inventory() -> None:
    expected = manifest_lines(NATIVE_MANIFEST)
    if len(expected) != 268 or len(set(expected)) != 268:
        fail("native baseline manifest must contain exactly 268 unique cases")

    actual_paths = tuple(path.name for path in sorted(NATIVE_SOURCE_DIRECTORY.glob("*_tests.cpp")))
    if actual_paths != NATIVE_SOURCE_NAMES:
        fail(f"native parity suite files changed (expected={NATIVE_SOURCE_NAMES}, actual={actual_paths})")

    all_definitions: list[str] = []
    all_calls: list[str] = []
    suite_counts: list[str] = []
    for source_name in NATIVE_SOURCE_NAMES:
        source_path = NATIVE_SOURCE_DIRECTORY / source_name
        source = source_path.read_text(encoding="utf-8")
        definitions = re.findall(r"^void\s+(test[A-Za-z0-9_]+)\s*\(", source, re.MULTILINE)
        main_match = re.search(r"\bint\s+main\s*\([^)]*\)\s*\{(?P<body>.*)\}\s*$", source, re.DOTALL)
        if main_match is None:
            fail(f"could not locate native parity runner in {source_name}")
        calls = re.findall(r"^\s*(test[A-Za-z0-9_]+)\s*\(\s*\)\s*;",
                           main_match.group("body"), re.MULTILINE)
        compare_ordered(f"{source_name} definitions/runner", definitions, calls)
        all_definitions.extend(definitions)
        all_calls.extend(calls)
        suite_counts.append(f"{source_name.removesuffix('_tests.cpp')}={len(definitions)}")

    if len(all_definitions) != len(set(all_definitions)):
        duplicates = sorted(name for name in set(all_definitions) if all_definitions.count(name) > 1)
        fail(f"native cases occur in more than one suite: {duplicates}")
    if set(all_definitions) != set(expected):
        missing = [name for name in expected if name not in all_definitions]
        added = [name for name in all_definitions if name not in expected]
        fail(f"native case inventory changed (missing={missing}, added={added})")
    if all_calls != all_definitions:
        fail("native suite runners do not match their definitions")

    print("native suite inventory: " + ", ".join(suite_counts))


def verify_jvm_inventory() -> None:
    expected = manifest_lines(JVM_MANIFEST)
    if len(expected) != 17 or len(set(expected)) != 17:
        fail("JVM baseline manifest must contain exactly 17 unique expectations")
    verify_expectations("JVM expectation", expected)


def verify_pyramid_expectations() -> None:
    if len(PYRAMID_EXPECTATIONS) != len(set(PYRAMID_EXPECTATIONS)):
        fail("test-pyramid expectations must be unique")
    verify_expectations("test-pyramid expectation", PYRAMID_EXPECTATIONS)


def main() -> None:
    verify_native_inventory()
    verify_test_source_sets()
    verify_jvm_inventory()
    verify_pyramid_expectations()
    native_test_count = len(manifest_lines(NATIVE_MANIFEST))
    managed_test_count = sum(entry[2] for entry in TEST_SOURCE_SETS)
    print(
        f"test inventory verified: {native_test_count} native cases, "
        f"{managed_test_count} managed source tests, "
        f"17 JVM expectations, and {len(PYRAMID_EXPECTATIONS)} cross-layer expectations"
    )


if __name__ == "__main__":
    main()
