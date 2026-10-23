#!/usr/bin/env python3

from pathlib import Path
import re
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"native build policy error: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative_path: str) -> str:
    path = REPO_ROOT / relative_path
    if not path.is_file():
        fail(f"required file is missing: {relative_path}")
    return path.read_text(encoding="utf-8")


def require(source: str, snippet: str, owner: str) -> None:
    if snippet not in source:
        fail(f"{owner} is missing required policy: {snippet}")


def require_system_include(cmake: str, target: str, include_fragment: str) -> None:
    pattern = re.compile(
        rf"target_include_directories\(\s*{re.escape(target)}\s+SYSTEM\s+PRIVATE"
        rf"(?:(?!\n\s*\)).)*{re.escape(include_fragment)}(?:(?!\n\s*\)).)*\n\s*\)",
        re.DOTALL,
    )
    if pattern.search(cmake) is None:
        fail(f"{target} must treat {include_fragment} as a system include")


def verify_cmake_policy() -> None:
    cmake = read("native/CMakeLists.txt")

    for option in (
        'option(CELLULARITY_ENABLE_SANITIZERS "Instrument non-Android targets with ASan and UBSan" OFF)',
        'option(CELLULARITY_ENABLE_THREAD_SANITIZER',
        'option(CELLULARITY_BUILD_EXECUTION_CONTEXT_TRACE',
        'option(CELLULARITY_WARNINGS_AS_ERRORS "Treat project-owned source warnings as errors" OFF)',
        'option(CELLULARITY_ENABLE_IPO "Enable IPO/LTO in supported optimized configurations" ON)',
    ):
        require(cmake, option, "native/CMakeLists.txt")

    for warning_flag in ("-Wall", "-Wextra", "-Wpedantic"):
        require(cmake, warning_flag, "project warning policy")
    require(cmake, "if(CELLULARITY_WARNINGS_AS_ERRORS)", "project warning policy")
    require(cmake, "list(APPEND CELLULARITY_PROJECT_WARNING_FLAGS -Werror)", "project warning policy")

    require(cmake, "CELLULARITY_ENABLE_SANITIZERS AND ANDROID", "sanitizer host guard")
    require(cmake, "CELLULARITY_ENABLE_THREAD_SANITIZER AND ANDROID", "thread sanitizer host guard")
    require(
        cmake,
        "CELLULARITY_BUILD_EXECUTION_CONTEXT_TRACE\n        AND NOT ANDROID",
        "execution-context trace platform guard",
    )
    require(cmake, "-fsanitize=address,undefined", "sanitizer instrumentation")
    require(cmake, "-fsanitize=thread", "thread sanitizer instrumentation")
    require(
        cmake,
        "if(NOT CELLULARITY_ENABLE_THREAD_SANITIZER)",
        "allocation-test thread sanitizer exclusion",
    )
    require(cmake, "include(CheckIPOSupported)", "IPO capability check")
    require(cmake, "check_ipo_supported(", "IPO capability check")
    require(cmake, "AND NOT ANDROID", "host-only IPO capability check")
    for configuration in ("RELEASE", "RELWITHDEBINFO", "MINSIZEREL"):
        require(
            cmake,
            f"INTERPROCEDURAL_OPTIMIZATION_{configuration} TRUE",
            "configuration-scoped IPO policy",
        )
    if re.search(r"INTERPROCEDURAL_OPTIMIZATION\s+TRUE", cmake):
        fail("IPO must not be enabled without an explicit optimized configuration")
    if "-flto" in cmake:
        fail("raw -flto flags are forbidden; use checked CMake IPO properties")
    if re.search(r"set\(CMAKE_(?:C|CXX)_FLAGS_[A-Z]+[^)]*\bFORCE\b", cmake, re.DOTALL):
        fail("project must not overwrite toolchain configuration flags")

    require(cmake, "set(HWY_WARNINGS_ARE_ERRORS OFF CACHE BOOL \"\" FORCE)", "Highway isolation")
    require_system_include(cmake, "cellularity_simulation", "third_party/highway")
    require_system_include(cmake, "cellularity_render_assets", "third_party/stb")
    require_system_include(cmake, "cellularity_perf", "third_party/nlohmann_json/include")
    require_system_include(
        cmake,
        "cellularity_behavior_performance",
        "third_party/nlohmann_json/include",
    )

    direct_targets = set(
        re.findall(
            r"^\s*add_(?:library|executable)\(\s*"
            r"(cellularity_[A-Za-z0-9_]+|genome_development_lab)\b",
            cmake,
            re.MULTILINE,
        )
    )
    if not direct_targets:
        fail("could not discover project-owned CMake targets")
    missing_configuration = sorted(
        target
        for target in direct_targets
        if f"cellularity_configure_source_target({target})" not in cmake
    )
    if missing_configuration:
        fail(f"project targets bypass the warning/sanitizer policy: {missing_configuration}")
    require(
        cmake,
        "cellularity_configure_source_target(${target_name})",
        "generated host test targets",
    )


def verify_script_policy() -> None:
    configure = read("scripts/native/configure.sh")
    build = read("scripts/native/build.sh")
    sanitizer = read("scripts/native/sanitizer-test.sh")
    thread_sanitizer = read("scripts/native/thread-sanitizer-test.sh")
    execution_context_trace = read(
        "scripts/native/execution-context-device-trace.py"
    )
    smoke = read("scripts/native/fuzz-smoke.sh")

    for option in (
        'DCMAKE_BUILD_TYPE="${CELLULARITY_NATIVE_BUILD_TYPE}"',
        'DCMAKE_CXX_COMPILER_AR="${CELLULARITY_NATIVE_AR}"',
        'DCMAKE_CXX_COMPILER_RANLIB="${CELLULARITY_NATIVE_RANLIB}"',
        'DCELLULARITY_ENABLE_SANITIZERS="${CELLULARITY_NATIVE_SANITIZERS}"',
        'DCELLULARITY_ENABLE_THREAD_SANITIZER="${CELLULARITY_NATIVE_THREAD_SANITIZER}"',
        'DCELLULARITY_WARNINGS_AS_ERRORS="${CELLULARITY_NATIVE_WARNINGS_AS_ERRORS}"',
        'DCELLULARITY_ENABLE_IPO="${CELLULARITY_NATIVE_IPO}"',
    ):
        require(configure, option, "native configure script")

    for setting in (
        "export CELLULARITY_NATIVE_BUILD_TYPE=Debug",
        "export CELLULARITY_NATIVE_PROFILE=OFF",
        "export CELLULARITY_NATIVE_SANITIZERS=ON",
        "export CELLULARITY_NATIVE_WARNINGS_AS_ERRORS=ON",
        "export CELLULARITY_NATIVE_IPO=OFF",
    ):
        require(sanitizer, setting, "sanitizer test script")
    require(sanitizer, '"${SCRIPT_DIR}/test.sh"', "sanitizer test script")
    require(
        build,
        'if [[ "${CELLULARITY_NATIVE_THREAD_SANITIZER}" != "ON" ]]',
        "allocation-test thread sanitizer exclusion",
    )
    for setting in (
        "export CELLULARITY_NATIVE_BUILD_TYPE=Debug",
        "export CELLULARITY_NATIVE_PROFILE=OFF",
        "export CELLULARITY_NATIVE_SANITIZERS=OFF",
        "export CELLULARITY_NATIVE_THREAD_SANITIZER=ON",
        "export CELLULARITY_NATIVE_WARNINGS_AS_ERRORS=ON",
        "export CELLULARITY_NATIVE_IPO=OFF",
    ):
        require(thread_sanitizer, setting, "thread sanitizer test script")
    require(
        thread_sanitizer,
        '"${SCRIPT_DIR}/test.sh"',
        "thread sanitizer test script",
    )
    for policy in (
        'parser.add_argument("--android-serial", required=True)',
        '"scenario": "c01/execution-context-device-trace-capture"',
        '"-DCELLULARITY_BUILD_EXECUTION_CONTEXT_TRACE=ON"',
        '"cellularity_execution_context_trace"',
        '"atrace"',
    ):
        require(
            execution_context_trace,
            policy,
            "execution-context Android trace runner",
        )
    for forbidden_policy in (
        'parser.add_argument("--allow-virtual',
        'if classification["virtual"]',
    ):
        if forbidden_policy in execution_context_trace:
            fail(
                "execution-context Android trace runner still restricts targets "
                f"by device class: {forbidden_policy}"
            )
    require(smoke, "-DCELLULARITY_ENABLE_IPO=OFF", "decoder robustness configuration")


def main() -> None:
    verify_cmake_policy()
    verify_script_policy()
    print(
        "native build policy verified: project warnings, isolated dependencies, "
        "host ASan/UBSan, configurable Clang toolchain, and checked optimized-only IPO"
    )


if __name__ == "__main__":
    main()
