#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <name> <substrate> [warmupSteps=200] [profileSteps=2000] [repeat=7]" >&2
    exit 2
fi

NAME="$1"
SUBSTRATE="$2"
WARMUP_STEPS="${3:-200}"
PROFILE_STEPS="${4:-2000}"
REPEAT="${5:-7}"
JSON_OUTPUT="/tmp/cellularity_${NAME}_detail.json"
STAT_OUTPUT="/tmp/cellularity_${NAME}_perf_stat.txt"
BUILD_OUTPUT="/tmp/cellularity_${NAME}_build.txt"

if [[ "${SUBSTRATE}" = /* ]]; then
    SUBSTRATE_PATH="${SUBSTRATE}"
else
    SUBSTRATE_PATH="${REPO_ROOT}/${SUBSTRATE}"
fi

if ! "${SCRIPT_DIR}/build.sh" >"${BUILD_OUTPUT}" 2>&1; then
    cat "${BUILD_OUTPUT}" >&2
    exit 1
fi

rm -f "${JSON_OUTPUT}" "${STAT_OUTPUT}"

perf stat -r "${REPEAT}" \
    -e cycles:u,instructions:u,cache-references:u,cache-misses:u,L1-dcache-loads:u,L1-dcache-load-misses:u \
    "${CELLULARITY_NATIVE_BUILD_DIR}/cellularity_perf" \
    "${SUBSTRATE_PATH}" \
    profile-detail-json \
    "${WARMUP_STEPS}" \
    "${PROFILE_STEPS}" \
    "${JSON_OUTPUT}" \
    2>"${STAT_OUTPUT}"

if [[ ! -s "${JSON_OUTPUT}" ]]; then
    cat "${STAT_OUTPUT}" >&2
    echo "Benchmark did not produce JSON output: ${JSON_OUTPUT}" >&2
    exit 1
fi

if grep -q "Segmentation fault\\|Aborted\\|Error:" "${STAT_OUTPUT}"; then
    cat "${STAT_OUTPUT}" >&2
    exit 1
fi

python3 - "${JSON_OUTPUT}" "${STAT_OUTPUT}" "${NAME}" <<'PY'
import json
import re
import sys

json_path, stat_path, name = sys.argv[1:4]
with open(json_path, "r", encoding="utf-8") as f:
    report = json.load(f)
with open(stat_path, "r", encoding="utf-8") as f:
    stat_text = f.read()

def counter(label):
    pattern = rf"^\s*([0-9,]+)\s+{re.escape(label)}\b"
    match = re.search(pattern, stat_text, re.MULTILINE)
    if match:
        return match.group(1).replace(",", "")
    if label.endswith(":u"):
        pattern = rf"^\s*([0-9,]+)\s+{re.escape(label[:-2])}\b"
        match = re.search(pattern, stat_text, re.MULTILINE)
        if match:
            return match.group(1).replace(",", "")
    return "n/a"

def elapsed():
    match = re.search(r"^\s*([0-9.]+)\s+\+-\s+([0-9.]+)\s+seconds time elapsed", stat_text, re.MULTILINE)
    if match:
        return f"{match.group(1)} +- {match.group(2)}s"
    match = re.search(r"^\s*([0-9.]+)\s+seconds time elapsed", stat_text, re.MULTILINE)
    return f"{match.group(1)}s" if match else "n/a"

stages = report.get("stages", {})
top_stages = sorted(
    ((stage, data.get("avgMs", 0.0), data.get("share", 0.0)) for stage, data in stages.items()),
    key=lambda item: item[1],
    reverse=True,
)[:8]

print()
print(f"=== {name} summary ===")
print(f"json: {json_path}")
print(f"perf: {stat_path}")
print(f"cells={report.get('cells')} food={report.get('food')} avgStepMs={report.get('avgStepMs'):.6f}")
print(f"perfElapsed={elapsed()}")
print(f"cycles={counter('cycles:u')} instructions={counter('instructions:u')}")
print(f"cacheRefs={counter('cache-references:u')} cacheMisses={counter('cache-misses:u')}")
print(f"L1Loads={counter('L1-dcache-loads:u')} L1Misses={counter('L1-dcache-load-misses:u')}")
print("topStages:")
for stage, avg_ms, share in top_stages:
    print(f"  {stage}: {avg_ms:.6f} ms ({share:.2f}%)")
PY
