#!/usr/bin/env bash
# Turns the application image built by
#     ./gradlew :desktop:packageAppImage
# (a directory) into a single Cellularity-<version>-x86_64.AppImage with appimagetool.
#
# The version is the application version from gradle/libs.versions.toml and the icon is the
# Android launcher artwork, the same as every other package.
#
# Writes desktop/build/compose/binaries/main/appimage/Cellularity-<version>-x86_64.AppImage
#
# appimagetool downloads the AppImage runtime from GitHub unless it is given one; set
# APPIMAGE_RUNTIME_FILE to a downloaded runtime-x86_64 to build offline.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(sed -n 's/^appVersionName = "\([^"]*\)"$/\1/p' "${repo_root}/gradle/libs.versions.toml")"
if [[ -z "${version}" ]]; then
    echo "appVersionName is missing from gradle/libs.versions.toml" >&2
    exit 1
fi

app_image="${repo_root}/desktop/build/compose/binaries/main/app/Cellularity"
if [[ ! -x "${app_image}/bin/Cellularity" ]]; then
    echo "No application image at ${app_image}; build it first:" >&2
    echo "    ./gradlew :desktop:packageAppImage" >&2
    exit 1
fi
if ! command -v appimagetool >/dev/null; then
    echo "appimagetool not found on PATH" >&2
    exit 1
fi

app_dir="${repo_root}/desktop/build/appimage/Cellularity.AppDir"
output_dir="${repo_root}/desktop/build/compose/binaries/main/appimage"
output="${output_dir}/Cellularity-${version}-x86_64.AppImage"

rm -rf "${app_dir}"
mkdir -p "${app_dir}" "${output_dir}"
cp -a "${app_image}/." "${app_dir}/"

cat > "${app_dir}/AppRun" <<'APPRUN'
#!/bin/sh
# The launcher finds its runtime and libraries relative to itself.
here="$(dirname "$(readlink -f "$0")")"
exec "${here}/bin/Cellularity" "$@"
APPRUN
chmod +x "${app_dir}/AppRun"

cat > "${app_dir}/cellularity.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Cellularity
Comment=Cellularity microscope simulation
Exec=Cellularity
Icon=cellularity
Categories=Education;Science;
Terminal=false
DESKTOP

cp "${repo_root}/app/src/main/ic_launcher-playstore.png" "${app_dir}/cellularity.png"
ln -sf cellularity.png "${app_dir}/.DirIcon"

rm -f "${output}"
runtime_args=()
if [[ -n "${APPIMAGE_RUNTIME_FILE:-}" ]]; then
    runtime_args=(--runtime-file "${APPIMAGE_RUNTIME_FILE}")
fi
ARCH=x86_64 appimagetool --no-appstream "${runtime_args[@]}" "${app_dir}" "${output}"
echo "AppImage: ${output}"
