#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

WARMUP_UPDATES="${1:-80}"
MEASURED_UPDATES="${2:-500}"
OUTPUT="${3:-/tmp/cellularity_behavior_performance.json}"
BUILD_DIR="${CELLULARITY_BEHAVIOR_BASELINE_BUILD_DIR:-/tmp/cellularity_native_behavior_release}"
CONFIGURE_LOG="/tmp/cellularity_behavior_configure.log"
BUILD_LOG="/tmp/cellularity_behavior_build.log"

if ! CELLULARITY_NATIVE_BUILD_DIR="${BUILD_DIR}" CELLULARITY_NATIVE_PROFILE=OFF \
        "${SCRIPT_DIR}/configure.sh" >"${CONFIGURE_LOG}" 2>&1; then
    cat "${CONFIGURE_LOG}" >&2
    exit 1
fi
if ! cmake --build "${BUILD_DIR}" \
        --target cellularity_behavior_baseline_tests cellularity_behavior_performance \
        -j >"${BUILD_LOG}" 2>&1; then
    cat "${BUILD_LOG}" >&2
    exit 1
fi

"${BUILD_DIR}/cellularity_behavior_baseline_tests"
"${BUILD_DIR}/cellularity_behavior_performance" \
    "${WARMUP_UPDATES}" \
    "${MEASURED_UPDATES}" \
    "${OUTPUT}"

python3 - "${OUTPUT}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as report_file:
    report = json.load(report_file)

metadata = report["metadata"]
print(f"json: {sys.argv[1]}")
print(f"benchmark={report['benchmark']} seed={report['seed']}")
print(f"warmupUpdates={report['warmupUpdates']} measuredUpdates={report['measuredUpdates']}")
print(f"wallTimeMs={report['wallTimeMs']:.3f}")
print(f"updatesPerSecond={report['updatesPerSecond']:.2f}")
print(f"peakRssKiB={report['peakRssKiB']}")
print(f"compiler={metadata['compiler']} {metadata['compilerVersion']}")
print(f"system={metadata['system']} processor={metadata['processor']} buildType={metadata['buildType']}")
print(f"sourceRevision={metadata['sourceRevision']} sourceDirty={metadata['sourceDirty']}")
print("comparisonPolicy=signal only; no hard regression threshold")
PY
