#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

: "${CELLULARITY_FUZZ_BUILD_DIR:=/tmp/cellularity_native_fuzz_build}"
if [[ -f "${CELLULARITY_FUZZ_BUILD_DIR}/CMakeCache.txt" ]] \
        && ! grep -Fqx \
            "CMAKE_HOME_DIRECTORY:INTERNAL=${CELLULARITY_NATIVE_SOURCE_DIR}" \
            "${CELLULARITY_FUZZ_BUILD_DIR}/CMakeCache.txt"; then
    source_path_key="$(
        printf '%s' "${CELLULARITY_NATIVE_SOURCE_DIR}" \
            | sha256sum \
            | cut -c1-12
    )"
    compatible_build_dir="${CELLULARITY_FUZZ_BUILD_DIR}_${source_path_key}"
    echo "Ignoring incompatible CMake cache at ${CELLULARITY_FUZZ_BUILD_DIR}; using ${compatible_build_dir}" >&2
    CELLULARITY_FUZZ_BUILD_DIR="${compatible_build_dir}"
fi
CELLULARITY_FUZZ_CORPUS_DIR="$(mktemp -d /tmp/cellularity_fuzz_smoke.XXXXXX)"
trap 'rm -rf -- "${CELLULARITY_FUZZ_CORPUS_DIR}"' EXIT

RELEASE_CORPUS="${CELLULARITY_FUZZ_CORPUS_DIR}/release"
LEGACY_CORPUS="${CELLULARITY_FUZZ_CORPUS_DIR}/legacy"
mkdir -p "${RELEASE_CORPUS}" "${LEGACY_CORPUS}" "${CCACHE_DIR}"

xxd -r -p \
    "${REPO_ROOT}/testdata/release-format/substrate-v1-golden.hex" \
    "${RELEASE_CORPUS}/substrate-v1.bin"
cp "${CELLULARITY_NATIVE_SOURCE_DIR}/tests/fixtures/corpus/legacy-substrate-v012.bin" \
   "${CELLULARITY_NATIVE_SOURCE_DIR}/tests/fixtures/corpus/legacy-substrate-v013.bin" \
   "${CELLULARITY_NATIVE_SOURCE_DIR}/tests/fixtures/corpus/legacy-substrate-v103.bin" \
   "${CELLULARITY_NATIVE_SOURCE_DIR}/tests/fixtures/corpus/jos-object-graph.bin" \
   "${CELLULARITY_NATIVE_SOURCE_DIR}/tests/fixtures/corpus/jos-primitive-array.bin" \
   "${LEGACY_CORPUS}/"

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

cmake --build "${CELLULARITY_FUZZ_BUILD_DIR}" \
      --target cellularity_release_substrate_fuzzer \
               cellularity_legacy_java_stream_fuzzer \
      -j

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:allocator_may_return_null=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

"${CELLULARITY_FUZZ_BUILD_DIR}/cellularity_release_substrate_fuzzer" \
    -runs=512 -max_len=65536 -timeout=5 -rss_limit_mb=3072 "${RELEASE_CORPUS}"
"${CELLULARITY_FUZZ_BUILD_DIR}/cellularity_legacy_java_stream_fuzzer" \
    -runs=512 -max_len=65536 -timeout=5 -rss_limit_mb=3072 "${LEGACY_CORPUS}"
