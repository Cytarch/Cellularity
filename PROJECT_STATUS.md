# Cellularity Project Status

Last reviewed: 2026-10-31

## Overall state

Cellularity has a public Android prerelease line and a Windows desktop application. The current Android release candidate remains `0.1.0-alpha.1` (`versionCode 1`). The `desktop` module adds a Compose Desktop application, Windows storage and settings, desktop JNI, Vulkan rendering, and MSI packaging; Windows installers are development artifacts rather than a published release line.

The public Android package namespace and application ID are consistently `com.cytarch.cellularity`. Android native packaging produces `libcellularity.so`; the desktop JNI build produces `cellularity_desktop.dll`. GitHub Releases remains the planned binary distribution channel for the Android prerelease.

## Release scope

The source tree consists of three Gradle modules and one root native source tree:

| Module | Purpose | Main technology |
| --- | --- | --- |
| `app` | Android application and platform integration | Java, Kotlin |
| `shared` | UI, resources, and presentation logic consumed by Android and desktop | Kotlin, Compose Multiplatform |
| `desktop` | Windows window, storage, settings, JNI integration, and packaging | Kotlin, Compose Desktop |
| `native` | Simulation, formats, interaction, GLES/Vulkan rendering, JNI, and host tests | C++ |

No signing keys, local SDK paths, generated APKs, build outputs, crash logs, or local IDE state are part of the release tree.

## Repository and publication boundary

This directory is a standalone Git repository with an intentional public history. Both the Android application and the Windows desktop application are maintained on `main` and share the `shared` module and the native sources. Private-repository metadata and migration archives are outside the public source tree.

Generated Gradle, Kotlin, CMake, native, lint, test, APK, and MSI outputs are ignored. Signing files, local SDK configuration, credentials, and local IDE state must remain untracked.

## Explicit exclusions

The public tree does not contain:

- the legacy Java simulation engine;
- the old standalone Java substrate-conversion project; legacy decoding required by the applications remains in the active codecs;
- private-repository metadata, migration archives, internal work plans, signing material, or generated artifacts.

The `desktop` module is part of the project. It owns the Compose Desktop entry point, Windows platform adapters, and desktop JNI integration; the native tree owns the Vulkan implementation and Win32 host. The `shared` module remains platform-neutral and has no window, JNI, or renderer implementation. Host-native parity tests, performance tools, and the genome development CLI are development tooling, not packaged desktop application entry points.

## Naming and privacy audit

The product display name is `Cellularity`. The application ID, Android namespace, Java/Kotlin package root, JNI symbol prefix, CMake project, and native library name consistently use the new project identity. The release check rejects known legacy product names, contributor identities, and package roots in both content and paths.

The committed tree was scanned for private-key blocks, common cloud/API token formats, signing material, credential files, local absolute paths, and inherited repository configuration. No matches were found. The source manifest declares no network, billing, storage, camera, microphone, or location permissions. Exported file-opening entry points were smoke-tested with malformed input.

`RecoveryActivity` is the only externally exported activity. It is the launcher and document-import recovery gate; `MainMenuActivity` and `PlayActivity` are private. FileProvider exposes only generated files under `cache/exports`; general internal storage is not shared. Android backup is disabled for private simulation data.

## Licensing

Cellularity uses the unmodified MIT license text at the repository root. Bundled dependencies retain their own license files:

- Google Highway: Apache License 2.0 and BSD 3-Clause;
- nlohmann/json 3.12.0: MIT;
- Nunito fonts: SIL Open Font License 1.1.

The exact locations are documented in `THIRD_PARTY_NOTICES.md`, and the public release check requires every referenced license file to remain present.

## Android configuration

- Minimum SDK: 24 (Android 7.0)
- Target and compile SDK: 36
- Java/Kotlin bytecode target: 17
- Build JDK: 21
- Gradle: 9.1.0
- Android Gradle Plugin: 9.0.1
- NDK: 28.2.13676358
- CMake: 3.22.1
- Native ABIs: `arm64-v8a`, `armeabi-v7a`, `x86`, and `x86_64`

Debug builds are not minified. Release builds use R8 resource shrinking and optimization and require distributor-provided signing configuration.

The release starts a new public update line at `versionCode 1`. It is not intended to update any differently identified historical application; future `com.cytarch.cellularity` builds update only this application line and must use the same team-controlled signing key.

## Architecture

Compose screens use immutable contracts and gateway interfaces from `shared`. Android implementations in `app` own controller lifecycle, storage, rendering surfaces, and JNI calls. The JNI layer delegates simulation, file decoding, snapshots, and rendering to C++ code under the root `native/` source tree; mutable simulation state remains native-owned.

Native CMake is split into static `cellularity_simulation`, `cellularity_formats`, `cellularity_interaction`, and `cellularity_rendering` layers. Android-only JNI, GLES, and asset adapters compose them into the single main `libcellularity.so`; the small independent `libcellularity_diagnostics.so` supports recovery from crashes that happen before the simulation library is loaded.

Substrate data is decoded by the active native codec. UI-facing state changes are serialized through the controller and revision tracking. `shared` owns no low-level GL or Vulkan API: platform adapters own surface lifecycle and native rendering, while bitmap upload remains in the Android platform layer. Surface recreation reloads renderer resources without replacing the native simulation world; simulation load/save commands are separate from the surface contract.

On Windows, `desktop/Main.kt` owns the Compose window and process services. `DesktopEnvironment` provides the roaming data directory, settings, and file repositories. `DesktopNativeRenderer` binds the shared C++ renderer through `cellularity_desktop.dll`; the Gradle `stageNativeAssets` task packages that library, `vulkan-1.dll`, `z.dll`, generated SPIR-V shaders, and textures. The MSI task uses the same desktop and shared JARs as development runs and registers a Start Menu shortcut without a desktop shortcut.

Current and legacy substrate formats remain supported. External imports are streamed to a temporary private file and atomically installed after validation rather than being loaded into a Java `byte[]`. Input files, decompressed legacy data, and estimated simulation allocations have a 2 GiB safety ceiling. Native collection counts, topology counts, file headers, environment sizes, and world capacity are validated before large allocations.

UI-facing file repositories are asynchronous and return typed storage outcomes. Android performs their I/O on `Dispatchers.IO`; Freezer and Gene Bank publish immutable list snapshots. Saves, imports, and same-directory renames use durable atomic replacement where the platform permits it.

Application settings have one process owner in `CellularityApplication`. Repository construction and initialization are lazy: the recovery launcher can expose a pending startup-crash report before settings are touched, and normal startup initializes them at the base Activity lifecycle boundary before locale or Compose UI setup. The common repository publishes a typed `StateFlow<CellularitySettings>`; the Android-only display setting remains in its own platform document. Both JSON documents declare schema version 1 and are committed as a validated sibling temporary file followed by file fsync, atomic rename, and directory fsync. An unreadable, unsupported, or oversized current document is preserved as a recovery copy before defaults may replace it; if preservation fails, ordinary writes stay blocked until an explicit reset. No migration path for private prerelease settings is part of the first public schema. Main Menu, Settings, and Play observe the in-memory state with lifecycle-aware collectors; the keep-screen-on decision uses an explicit Activity-creation snapshot rather than reading storage during composition.

Settings, Freezer, and Gene Bank use the same minimal feature-store boundary: immutable `StateFlow` state, typed actions, and single-consumer one-shot effects. Freezer and Gene Bank keep storage IDs separate from display names, own refresh and file-operation decisions in `shared`, and leave Android adapters responsible only for platform navigation, sharing, and feedback presentation.

Main Menu uses the same immutable state/action/effect boundary for its selected page, import progress, and collection refresh revisions. A retained Android coordinator owns `Intent`, `Uri`, and repository transport, serializes imports, and suppresses redelivery of completed commands across Activity recreation. Recovery routes subsequent document intents into the existing menu shell, while theme and language remain lifecycle-observed settings state.

Play uses one immutable shared store and one retained Android session owner. Launch decoding, saved UI state, file commands, dialogs, and native-session lifetime have separate boundaries; `PlayActivity` only connects the Android window, lifecycle, settings, and effects. Save, reload, and reset are serialized, while gateway-owned content revisions distinguish a clean stored snapshot from an unsaved world change. Saved instance state contains only primitive configuration values, stable substrate IDs, and UI selections.

## Validation status

### Android public-release baseline

The Android release gate covers:

- host-JVM compilation and tests for the complete shared common source set;
- JVM unit tests for native contracts, genome editing, pending environment changes, and substrate file handling;
- Android lint, including API-level compatibility from SDK 24;
- debug and release Android compilation;
- 13 native contract, compatibility, behavior, and parity suites on a Linux host;
- a synthetic legacy Java-serialization/gzip substrate conversion test;
- JNI export verification against the built native library;
- a public-tree scan for excluded components, credentials, signing material, generated artifacts, and obsolete identifiers.

Local Android release validation completed on 2026-09-11 with these results:

- 129 host-JVM common, 130 Android shared, and 97 app unit tests passed with no failures or skipped tests;
- Android lint completed with 0 errors and 26 dependency/toolchain upgrade notices;
- debug and release builds passed on the enforced JDK 21 runtime; the complete task graph also stored and reused Gradle's configuration cache without compatibility problems;
- the debug APK (24,027,245 bytes) and R8-minified unsigned release APK (5,705,012 bytes) built successfully;
- all four declared native ABIs packaged the main `libcellularity.so` and independent recovery `libcellularity_diagnostics.so`;
- the two environment batch JNI exports were verified and the removed per-property exports were confirmed absent;
- 58 canonical renderer assets were verified in source and APK; all 28 ESSL 100 shader pairs compile/link in the host gate and the microscope, genome-preview, and density-preview paths passed a GLES runtime smoke;
- all 13 Linux host CTest suites passed; all 197 frozen parity cases are split across formats, environment/gene, topology, simulation, interaction, and rendering;
- the packaged application ID and display label were verified as `com.cytarch.cellularity` and `Cellularity`;
- the Gradle 9.1.0 wrapper JAR and distribution checksum were verified against the official Gradle checksums;
- Waydroid x86_64 smoke-tests completed cold launches, rendered all three active GL surfaces, exercised substrate and genome save/list/share/rename/delete flows, verified the shared Freezer memory estimate, reopened a renamed substrate, and imported, saved, and reopened a legacy version-103 substrate without a fatal exception or ANR;
- settings runtime tests changed and persisted a common preview limit across a cold process restart, restored it, exercised the Android-only cutout option, and verified a one-shot theme recreation before restoring the default theme; Genome Editor received the injected preview limit, retained its resized preview, and toggled Ghost without a fatal exception or ANR;
- Main Menu runtime tests observed live language and theme changes without losing the selected page, imported a legacy v103 genome and a current substrate through external `content://` providers, refreshed an already visible collection, rejected a duplicate substrate without opening PlayActivity, and redelivered one command across Activity recreation without re-importing it or stacking another menu Activity;
- Play runtime tests saved, reloaded, reset, reopened, and removed a temporary substrate; clean and dirty back navigation behaved distinctly, while the selected tab and an unfinished save dialog survived both Activity recreation and process death without duplicating a command or native session;
- malformed external file intents and a direct `PlayActivity` launch without extras were handled without a crash.

Static checks and automated tests are the release gates. Runtime checks on Android
targets can provide useful vendor- and GPU-specific diagnostics, but they are
optional and do not determine release readiness.

### Windows desktop checks

The current desktop verification path is:

```powershell
cmake --build build/win-cmake --target cellularity_desktop_jni
.\gradlew.bat :desktop:test :shared:jvmTest :desktop:packageMsi
```

The packaged app image and installed MSI should also be smoke-tested for a cold Start Menu launch, native Vulkan rendering, genome editing, and substrate import/save/reopen flows.

The Windows-only native sources (`native/host/win32`, desktop JNI, Win32 Vulkan tests) cannot be compiled by the Linux host gate and must be built and tested on Windows before a desktop change is merged. `scripts/check-public-release.sh` covers the whole tree, including the `desktop` module.

## Known maintenance work

- Android lint reports 26 upgrade-only dependency/toolchain notices; upgrades remain isolated follow-up changes rather than part of the build-system migration.
- Automated legacy coverage includes a synthetic version-24 substrate; a sanitized real-file corpus spanning the supported format eras is still needed for stronger compatibility assurance.
- Runtime performance varies by device CPU/GPU and should be profiled with representative simulations before publishing performance claims.
- There is no Android instrumentation/UI automation suite yet; existing runtime smoke tests are optional diagnostics, not a release gate or a claim of broad device coverage.
- A 2 GiB rejection ceiling is not a guarantee that low-memory devices can run simulations near that size.
- GitHub release notes, release signing, screenshots, and signed APK publication remain project-team tasks; private signing material must not enter this repository.

## Android release readiness criteria

An Android revision is ready to publish when all of the following pass:

```bash
scripts/check-public-release.sh
python3 scripts/verify-resource-locales.py
python3 scripts/verify-shared-portability.py
./gradlew :shared:jvmTest :shared:testAndroidHostTest :app:testDebugUnitTest :app:lintDebug :app:assembleDebug :app:assembleRelease
scripts/native/test.sh
```

The release repository must also contain one intentional public history, no remotes inherited from a private repository, and no untracked release artifacts.
