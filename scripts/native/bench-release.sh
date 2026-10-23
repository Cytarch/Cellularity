#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <substrate> [warmupSteps=200] [profileSteps=5000]" >&2
    exit 2
fi

SUBSTRATE="$1"
WARMUP_STEPS="${2:-200}"
PROFILE_STEPS="${3:-5000}"
BUILD_DIR="${CELLULARITY_NATIVE_RELEASE_BUILD_DIR:-/tmp/cellularity_native_clang_release_noprofile}"
OUTPUT="${CELLULARITY_NATIVE_RELEASE_JSON:-/tmp/cellularity_release_profile.json}"

if [[ "${SUBSTRATE}" = /* ]]; then
    SUBSTRATE_PATH="${SUBSTRATE}"
else
    SUBSTRATE_PATH="${REPO_ROOT}/${SUBSTRATE}"
fi

CELLULARITY_NATIVE_BUILD_DIR="${BUILD_DIR}" CELLULARITY_NATIVE_PROFILE=OFF "${SCRIPT_DIR}/configure.sh" >/tmp/cellularity_release_configure.txt
CELLULARITY_NATIVE_BUILD_DIR="${BUILD_DIR}" CELLULARITY_NATIVE_PROFILE=OFF "${SCRIPT_DIR}/build.sh" >/tmp/cellularity_release_build.txt

"${BUILD_DIR}/cellularity_perf" \
    "${SUBSTRATE_PATH}" \
    profile-json \
    "${WARMUP_STEPS}" \
    "${PROFILE_STEPS}" \
    "${OUTPUT}"

python3 - "${OUTPUT}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    report = json.load(f)

print(f"json: {sys.argv[1]}")
print(f"cells={report.get('cells')} food={report.get('food')}")
print(f"avgStepMs={report.get('avgStepMs'):.6f}")
print(f"stepsPerSecond={report.get('stepsPerSecond'):.2f}")
PY
