#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 release|legacy <seconds> <corpus-directory>" >&2
    exit 2
fi

FUZZ_KIND="$1"
FUZZ_SECONDS="$2"
FUZZ_CORPUS="$3"
case "${FUZZ_KIND}" in
    release) FUZZ_TARGET=cellularity_release_substrate_fuzzer ;;
    legacy) FUZZ_TARGET=cellularity_legacy_java_stream_fuzzer ;;
    *) echo "Unknown fuzz target: ${FUZZ_KIND}" >&2; exit 2 ;;
esac
if [[ ! "${FUZZ_SECONDS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "Fuzz duration must be a positive integer" >&2
    exit 2
fi

: "${CELLULARITY_FUZZ_BUILD_DIR:=/tmp/cellularity_native_fuzz_build}"
mkdir -p "${FUZZ_CORPUS}" "${CCACHE_DIR}"

LAUNCHER_ARGS=()
if command -v ccache >/dev/null 2>&1; then
    LAUNCHER_ARGS+=(
        -DCMAKE_C_COMPILER_LAUNCHER=ccache
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
    )
fi

cmake -S "${CELLULARITY_NATIVE_SOURCE_DIR}" \
      -B "${CELLULARITY_FUZZ_BUILD_DIR}" \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER="${CELLULARITY_NATIVE_C_COMPILER}" \
      -DCMAKE_CXX_COMPILER="${CELLULARITY_NATIVE_CXX_COMPILER}" \
      -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=${CELLULARITY_NATIVE_LINKER}" \
      -DCELLULARITY_ENABLE_NATIVE_PROFILE=OFF \
      -DCELLULARITY_ENABLE_FUZZING=ON \
      -DCELLULARITY_ENABLE_IPO=OFF \
      -DCELLULARITY_WARNINGS_AS_ERRORS="${CELLULARITY_NATIVE_WARNINGS_AS_ERRORS}" \
      "${LAUNCHER_ARGS[@]}"
cmake --build "${CELLULARITY_FUZZ_BUILD_DIR}" --target "${FUZZ_TARGET}" -j

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:allocator_may_return_null=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
"${CELLULARITY_FUZZ_BUILD_DIR}/${FUZZ_TARGET}" \
    -max_total_time="${FUZZ_SECONDS}" \
    -max_len=65536 \
    -timeout=10 \
    -rss_limit_mb=3072 \
    "${FUZZ_CORPUS}"
