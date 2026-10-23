#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

WARMUP_STEPS="${1:-200}"
PROFILE_STEPS="${2:-5000}"
MAX_CELLS="${3:-1000}"
MAX_FOODS="${4:-3500}"
RADIUS="${5:-1.0}"
BUILD_DIR="${CELLULARITY_NATIVE_RELEASE_BUILD_DIR:-/tmp/cellularity_native_clang_release_noprofile}"
OUTPUT="${CELLULARITY_DEFAULT_RANDOM_CELLS_JSON:-/tmp/cellularity_default_random_cells.json}"

CELLULARITY_NATIVE_BUILD_DIR="${BUILD_DIR}" CELLULARITY_NATIVE_PROFILE=OFF "${SCRIPT_DIR}/configure.sh" >/tmp/cellularity_default_random_cells_configure.txt
CELLULARITY_NATIVE_BUILD_DIR="${BUILD_DIR}" CELLULARITY_NATIVE_PROFILE=OFF "${SCRIPT_DIR}/build.sh" >/tmp/cellularity_default_random_cells_build.txt

"${BUILD_DIR}/cellularity_perf" \
    default-random-cells-json \
    "${WARMUP_STEPS}" \
    "${PROFILE_STEPS}" \
    "${MAX_CELLS}" \
    "${MAX_FOODS}" \
    "${RADIUS}" \
    "${OUTPUT}"

python3 - "${OUTPUT}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    report = json.load(f)

print(f"json: {sys.argv[1]}")
print(f"scenario={report.get('scenario')}")
print(f"maxCells={report.get('maxCells')} maxFoods={report.get('maxFoods')} radius={report.get('radius')}")
print(f"warmupSteps={report.get('warmupSteps')} profileSteps={report.get('profileSteps')}")
print(f"cells={report.get('startCellCount')}->{report.get('endCellCount')} food={report.get('startFoodCount')}->{report.get('endFoodCount')}")
print(f"avgStepMs={report.get('avgStepMs'):.6f}")
print(f"stepsPerSecond={report.get('stepsPerSecond'):.2f}")
PY
