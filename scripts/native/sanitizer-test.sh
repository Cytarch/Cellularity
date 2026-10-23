#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

: "${CELLULARITY_NATIVE_SANITIZER_BUILD_DIR:=/tmp/cellularity_native_sanitizer_build}"

export CELLULARITY_NATIVE_BUILD_DIR="${CELLULARITY_NATIVE_SANITIZER_BUILD_DIR}"
export CELLULARITY_NATIVE_BUILD_TYPE=Debug
export CELLULARITY_NATIVE_PROFILE=OFF
export CELLULARITY_NATIVE_SANITIZERS=ON
export CELLULARITY_NATIVE_THREAD_SANITIZER=OFF
export CELLULARITY_NATIVE_WARNINGS_AS_ERRORS=ON
export CELLULARITY_NATIVE_IPO=OFF

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:strict_string_checks=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

"${SCRIPT_DIR}/test.sh"
