# Changelog

Notable user-visible changes are recorded in this file.

## Unreleased

- Store standalone genomes in the bounded `CLTY` release format with dynamic
  1–256-gene support, named columnar data, and per-column compression.
- Keep original Java genome files as read-only imports while new saves use the
  release format.
- Read Gene Bank metadata without loading genome bodies and load embedded
  previews only for visible entries.

## 0.1.0-alpha.1 - 2026-09-07

Initial public Android prerelease.

- Ships the Android application, Compose shared UI, C++ simulation core, JNI bridge, and OpenGL ES renderer.
- Preserves current and legacy substrate compatibility and existing genome-file support.
- Uses the `com.cytarch.cellularity` application ID and package namespace.
- Adds bounded external-file import, defensive snapshot validation, and a 2 GiB file/decompressed-data/allocation safety boundary.
- Limits public file opening to the main import activity and limits FileProvider sharing to generated cache exports.
- Includes local JVM tests, Linux native parity tests, Android lint, debug/release builds, and public-tree checks.

This prerelease is intended for publication through GitHub Releases only. Signed release binaries are produced by the project team outside the source tree.
