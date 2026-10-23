#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/env.sh"

"${SCRIPT_DIR}/configure.sh"
"${SCRIPT_DIR}/build.sh"
python3 "${REPO_ROOT}/scripts/verify-native-format-boundary.py"
python3 "${REPO_ROOT}/scripts/verify-native-session-boundary.py"
python3 "${REPO_ROOT}/scripts/tests/native_session_boundary_tests.py"
python3 "${REPO_ROOT}/scripts/verify-native-build-policy.py"
python3 "${REPO_ROOT}/scripts/verify-vulkan-capability-boundary.py"
python3 "${REPO_ROOT}/scripts/verify-gene-access-boundary.py"
python3 "${REPO_ROOT}/scripts/verify-test-inventory.py"
python3 "${REPO_ROOT}/scripts/verify-render-assets.py" --validate-shaders
ctest --test-dir "${CELLULARITY_NATIVE_BUILD_DIR}" --output-on-failure
