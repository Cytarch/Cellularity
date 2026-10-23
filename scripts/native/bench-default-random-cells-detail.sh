#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

WARMUP_STEPS="${1:-500}"
PROFILE_STEPS="${2:-10000}"
MAX_CELLS="${3:-1000}"
MAX_FOODS="${4:-3500}"
RADIUS="${5:-1.0}"
BUILD_DIR="${CELLULARITY_NATIVE_PROFILE_BUILD_DIR:-/tmp/cellularity_native_clang_build}"
OUTPUT="${CELLULARITY_DEFAULT_RANDOM_CELLS_DETAIL_JSON:-/tmp/cellularity_default_random_cells_detail.json}"

CELLULARITY_NATIVE_BUILD_DIR="${BUILD_DIR}" CELLULARITY_NATIVE_PROFILE=ON "${SCRIPT_DIR}/configure.sh" >/tmp/cellularity_default_random_cells_detail_configure.txt
CELLULARITY_NATIVE_BUILD_DIR="${BUILD_DIR}" CELLULARITY_NATIVE_PROFILE=ON "${SCRIPT_DIR}/build.sh" >/tmp/cellularity_default_random_cells_detail_build.txt

"${BUILD_DIR}/cellularity_perf" \
    default-random-cells-detail-json \
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
print(f"cells={report.get('cells')} food={report.get('food')}")
print(f"avgStepMs={report.get('avgStepMs'):.6f}")
print(f"stepsPerSecond={report.get('stepsPerSecond'):.2f}")
print("topStages:")
for name, stage in sorted(report.get("stages", {}).items(), key=lambda item: item[1].get("avgMs", 0.0), reverse=True)[:10]:
    print(f"  {name}: {stage.get('avgMs', 0.0):.6f} ms ({stage.get('share', 0.0):.2f}%)")

eat = report.get("eatFoodCounters", {})
print("eatFood:")
for key in (
    "avgFoodVisitsPerStep",
    "avgCandidateChecksPerStep",
    "avgContactChecksPerStep",
    "avgMovedFoodsPerStep",
    "avgSmellGridAddsPerStep",
):
    print(f"  {key}={eat.get(key)}")

overlap = report.get("overlapCounters", {})
if overlap:
    print("overlap:")
    for key in (
        "avgOverlapCandidatePairsPerStep",
        "avgOverlapExactPairsPerStep",
        "avgOverlapRowsRelayoutsPerStep",
        "avgOverlapLayoutRebuildsPerStep",
    ):
        print(f"  {key}={overlap.get(key)}")
PY
