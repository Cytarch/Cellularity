#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <substrate> [output=/tmp/cellularity_perf.json] [warmupSteps=200] [profileSteps=2000] [repeat=7]" >&2
    exit 2
fi

SUBSTRATE="$1"
OUTPUT="${2:-/tmp/cellularity_perf.json}"
WARMUP_STEPS="${3:-200}"
PROFILE_STEPS="${4:-2000}"
REPEAT="${5:-7}"

if [[ "${SUBSTRATE}" = /* ]]; then
    SUBSTRATE_PATH="${SUBSTRATE}"
else
    SUBSTRATE_PATH="${REPO_ROOT}/${SUBSTRATE}"
fi

if [[ ! -f "${SUBSTRATE_PATH}" ]]; then
    echo "Substrate file not found: ${SUBSTRATE_PATH}" >&2
    exit 2
fi

"${SCRIPT_DIR}/build.sh"
perf stat -r "${REPEAT}" \
    -e cycles:u,instructions:u,cache-references:u,cache-misses:u,L1-dcache-loads:u,L1-dcache-load-misses:u \
    "${CELLULARITY_NATIVE_BUILD_DIR}/cellularity_perf" \
    "${SUBSTRATE_PATH}" \
    profile-detail-json \
    "${WARMUP_STEPS}" \
    "${PROFILE_STEPS}" \
    "${OUTPUT}"
