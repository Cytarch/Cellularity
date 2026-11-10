# Changelog

Notable user-visible changes are recorded in this file.

## Unreleased

- Simulate identically on Android, Linux and Windows. The simulation now uses its
  own sine, cosine, arctangent, exponential and logarithm instead of the platform's
  C library, whose results differ in the last bit, and the native build no longer
  fuses multiplications and additions into FMA instructions. A substrate saved on
  one platform now evolves the same way on the others; existing substrates may
  evolve slightly differently than in earlier versions.
- Write the performance HUD's numbers with a decimal point regardless of the
  system locale.

## 0.1.0-alpha.2 - 2026-11-09

Packages for Android (APK, AAB) and Linux (deb, rpm, AppImage). No Windows package
is published for this release: it could not be built and tested on Windows yet.
The Windows code compiles and links in a cross-build, and a Windows package will
follow once it has been built and run on a Windows system.

- Give the desktop packages the Cellularity icon instead of the toolkit default.
- Use one application version for Android and the desktop; the desktop packages
  derive theirs from it.
- Drag cells only with the optical tweezers (MOVE) tool; pressing and dragging a
  cell moved it with every tool.
- Speed up the food step without changing the simulation, including plates with
  mobile food on x86, where settled food was about 35 times slower to move.
- Add the Windows Compose Desktop application with the shared UI, native Vulkan
  rendering, local settings, genome editing, and substrate import/save flows.
- Add Gradle MSI packaging with native runtime assets, an installation-directory
  choice, and a Start Menu shortcut.
- Run the desktop application on Linux as well, with the same Vulkan renderer,
  XDG data directory, and deb/rpm/AppImage packaging.
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
