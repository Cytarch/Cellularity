#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <substrate> [mode=profile-detail-json] [warmupSteps=200] [profileSteps=2000] [output=-]" >&2
    exit 2
fi

SUBSTRATE="$1"
MODE="${2:-profile-detail-json}"
WARMUP_STEPS="${3:-200}"
PROFILE_STEPS="${4:-2000}"
OUTPUT="${5:--}"

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
"${CELLULARITY_NATIVE_BUILD_DIR}/cellularity_perf" "${SUBSTRATE_PATH}" "${MODE}" "${WARMUP_STEPS}" "${PROFILE_STEPS}" "${OUTPUT}"
