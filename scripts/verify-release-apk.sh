#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: scripts/verify-release-apk.sh <release.apk>" >&2
    exit 2
fi

apk="$1"
if [[ ! -f "${apk}" ]]; then
    echo "Release APK not found: ${apk}" >&2
    exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version_catalog="${repo_root}/gradle/libs.versions.toml"
expected_version_code="$(sed -n 's/^appVersionCode = "\([^"]*\)"$/\1/p' "${version_catalog}")"
expected_version_name="$(sed -n 's/^appVersionName = "\([^"]*\)"$/\1/p' "${version_catalog}")"
if [[ ! "${expected_version_code}" =~ ^[0-9]+$ ]] \
    || [[ -z "${expected_version_name}" ]]; then
    echo "Release version metadata is missing from ${version_catalog}" >&2
    exit 1
fi

aapt2="${AAPT2:-${ANDROID_HOME:-}/build-tools/36.1.0/aapt2}"
if [[ ! -x "${aapt2}" ]]; then
    echo "Set AAPT2 or ANDROID_HOME to Android Build-Tools 36.1.0" >&2
    exit 1
fi

badging="$(${aapt2} dump badging "${apk}")"
manifest="$(${aapt2} dump xmltree "${apk}" --file AndroidManifest.xml)"
resources="$(${aapt2} dump resources "${apk}")"

if ! grep -q "package: name='com.cytarch.cellularity' versionCode='${expected_version_code}' versionName='${expected_version_name}'" <<<"${badging}"; then
    echo "Unexpected release package or version metadata" >&2
    exit 1
fi

if ! grep -q "application-label:'Cellularity'" <<<"${badging}"; then
    echo "Unexpected release application label" >&2
    exit 1
fi

if ! grep -q "native-code: 'arm64-v8a' 'armeabi-v7a' 'x86' 'x86_64'" <<<"${badging}"; then
    echo "Release APK does not contain every declared ABI" >&2
    exit 1
fi

if grep -q 'androidx.compose.ui.tooling.PreviewActivity' <<<"${manifest}"; then
    echo "Compose PreviewActivity must not be packaged in release builds" >&2
    exit 1
fi

if grep -q 'android:debuggable.*=true\|android:largeHeap.*=true' <<<"${manifest}"; then
    echo "Release manifest contains a debug or large-heap flag" >&2
    exit 1
fi

if grep -Eq '\((en-rXA|ar-rXB)\)' <<<"${resources}"; then
    echo "Release APK contains debug-only pseudo-locale resources" >&2
    exit 1
fi

if ! grep -q 'android:allowBackup.*=false' <<<"${manifest}"; then
    echo "Release backup policy is not disabled" >&2
    exit 1
fi

play_block="$(grep -A4 'com.cytarch.cellularity.ui.play_menu.PlayActivity' <<<"${manifest}")"
if ! grep -q 'android:exported.*=false' <<<"${play_block}"; then
    echo "PlayActivity is not private in the merged release manifest" >&2
    exit 1
fi

recovery_block="$(grep -A30 'com.cytarch.cellularity.ui.recovery.RecoveryActivity' <<<"${manifest}")"
if ! grep -q 'android:exported.*=true' <<<"${recovery_block}" \
    || ! grep -q 'android.intent.action.MAIN' <<<"${recovery_block}" \
    || ! grep -q 'android.intent.category.LAUNCHER' <<<"${recovery_block}"; then
    echo "RecoveryActivity is not the exported launcher in the merged release manifest" >&2
    exit 1
fi

main_block="$(grep -A4 'com.cytarch.cellularity.ui.main_menu.MainMenuActivity' <<<"${manifest}")"
if ! grep -q 'android:exported.*=false' <<<"${main_block}"; then
    echo "MainMenuActivity is unexpectedly exported in the merged release manifest" >&2
    exit 1
fi

python3 "${repo_root}/scripts/verify-render-assets.py" --apk "${apk}"

echo "Release APK metadata checks passed."
