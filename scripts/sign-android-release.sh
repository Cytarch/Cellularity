#!/usr/bin/env bash
# Signs the Android release APK and App Bundle built by
#     ./gradlew :app:assembleRelease :app:bundleRelease
# with the distributor's keystore, then verifies both.
#
# The repository stores no signing material: the keystore is passed in, and the passwords are
# read from the terminal without echo and handed to the signing tools through the environment,
# never on a command line (where other processes could read them) and never written to disk.
#
# usage: scripts/sign-android-release.sh --keystore <file.jks> [--alias <key alias>]
#        (CELLULARITY_RELEASE_KEYSTORE and CELLULARITY_RELEASE_KEY_ALIAS work as well)
#
# Writes, next to the unsigned outputs:
#     app/build/outputs/apk/release/app-release.apk
#     app/build/outputs/bundle/release/app-release-signed.aab
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
keystore="${CELLULARITY_RELEASE_KEYSTORE:-}"
key_alias="${CELLULARITY_RELEASE_KEY_ALIAS:-}"

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --keystore) keystore="${2:-}"; shift 2 ;;
        --alias) key_alias="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown argument: $1 (see --help)" >&2; exit 2 ;;
    esac
done

if [[ -z "${keystore}" || ! -f "${keystore}" ]]; then
    echo "Keystore not found: '${keystore}'. Pass --keystore <file.jks>." >&2
    exit 2
fi

apk_dir="${repo_root}/app/build/outputs/apk/release"
bundle_dir="${repo_root}/app/build/outputs/bundle/release"
unsigned_apk="${apk_dir}/app-release-unsigned.apk"
signed_apk="${apk_dir}/app-release.apk"
unsigned_bundle="${bundle_dir}/app-release.aab"
signed_bundle="${bundle_dir}/app-release-signed.aab"

if [[ ! -f "${unsigned_apk}" && ! -f "${unsigned_bundle}" ]]; then
    echo "No release outputs found; build them first:" >&2
    echo "    ./gradlew :app:assembleRelease :app:bundleRelease" >&2
    exit 1
fi

android_home="${ANDROID_HOME:-${HOME}/Android/Sdk}"
build_tools="$(ls -d "${android_home}"/build-tools/* 2>/dev/null | sort -V | tail -1 || true)"
apksigner="${APKSIGNER:-${build_tools}/apksigner}"
zipalign="${ZIPALIGN:-${build_tools}/zipalign}"
for tool in "${apksigner}" "${zipalign}"; do
    if [[ ! -x "${tool}" ]]; then
        echo "Android Build-Tools not found (${tool}); set ANDROID_HOME" >&2
        exit 1
    fi
done
for tool in keytool jarsigner; do
    if ! command -v "${tool}" >/dev/null; then
        echo "${tool} not found; put a JDK on PATH" >&2
        exit 1
    fi
done

# Passwords live only in this process's environment, exported to the tools it starts.
read -r -s -p "Keystore password: " CELLULARITY_SIGN_STORE_PASS
echo
read -r -s -p "Key password (Enter if it is the same): " CELLULARITY_SIGN_KEY_PASS
echo
if [[ -z "${CELLULARITY_SIGN_KEY_PASS}" ]]; then
    CELLULARITY_SIGN_KEY_PASS="${CELLULARITY_SIGN_STORE_PASS}"
fi
export CELLULARITY_SIGN_STORE_PASS CELLULARITY_SIGN_KEY_PASS
trap 'unset CELLULARITY_SIGN_STORE_PASS CELLULARITY_SIGN_KEY_PASS' EXIT

if [[ -z "${key_alias}" ]]; then
    aliases="$(keytool -J-Duser.language=en -list -keystore "${keystore}" -storepass:env CELLULARITY_SIGN_STORE_PASS \
        | sed -n 's/^\([^,]*\), .*PrivateKeyEntry.*$/\1/p')"
    alias_count="$(grep -c . <<<"${aliases}" || true)"
    if [[ "${alias_count}" -ne 1 ]]; then
        echo "The keystore holds ${alias_count} private keys; choose one with --alias:" >&2
        echo "${aliases}" >&2
        exit 1
    fi
    key_alias="${aliases}"
fi
echo "Signing with key '${key_alias}'"

if [[ -f "${unsigned_apk}" ]]; then
    # apksigner needs a 4-byte-aligned input; align a copy, sign it, keep the unsigned one.
    aligned_apk="$(mktemp "${apk_dir}/aligned-XXXXXX.apk")"
    trap 'rm -f "${aligned_apk}"; unset CELLULARITY_SIGN_STORE_PASS CELLULARITY_SIGN_KEY_PASS' EXIT
    "${zipalign}" -f -p 4 "${unsigned_apk}" "${aligned_apk}"
    "${apksigner}" sign \
        --ks "${keystore}" --ks-key-alias "${key_alias}" \
        --ks-pass env:CELLULARITY_SIGN_STORE_PASS --key-pass env:CELLULARITY_SIGN_KEY_PASS \
        --out "${signed_apk}" "${aligned_apk}"
    "${apksigner}" verify --verbose "${signed_apk}" | grep -E "^Verifies|^Verified using.*: true"
    if [[ -x "${build_tools}/aapt2" ]]; then
        AAPT2="${AAPT2:-${build_tools}/aapt2}" bash "${repo_root}/scripts/verify-release-apk.sh" "${signed_apk}"
    fi
    echo "Signed APK: ${signed_apk}"
fi

if [[ -f "${unsigned_bundle}" ]]; then
    jarsigner -J-Duser.language=en -keystore "${keystore}" \
        -storepass:env CELLULARITY_SIGN_STORE_PASS -keypass:env CELLULARITY_SIGN_KEY_PASS \
        -signedjar "${signed_bundle}" "${unsigned_bundle}" "${key_alias}" >/dev/null
    jarsigner -J-Duser.language=en -verify "${signed_bundle}" | grep -E "jar verified" || {
        echo "The signed bundle did not verify" >&2
        exit 1
    }
    echo "Signed bundle: ${signed_bundle}"
fi
