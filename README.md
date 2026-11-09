# Cellularity

Cellularity is an open-source cellular simulation for Android, Windows and Linux. The simulation core and renderer are implemented in C++; the application UI uses Kotlin and Compose Multiplatform shared code.

The current release line is **0.1.0-alpha.2** (`versionCode 4`), shared by Android and the desktop application for Windows and Linux. The desktop packages (MSI, deb, rpm, AppImage) take their version from it; see `gradle/libs.versions.toml`.

Desktop platform status: the Linux desktop application is built, tested and run. The Windows desktop application is experimental: its native library compiles and links (checked with a MinGW cross-build, which does not cover the MSVC-only code paths), but it has not yet been run or packaged on a real Windows system.

## Requirements

Common:

- JDK 21 as the Gradle runtime.

Android:

- Android SDK Platform 36
- Android SDK Build-Tools 36.1.0
- Android NDK 28.2.13676358
- CMake 3.22.1

Windows desktop:

- MSVC and CMake with Ninja support
- Vulkan headers and loader
- `glslc` and `spirv-val`
- zlib

Linux desktop:

- Clang or GCC, CMake and Ninja
- Vulkan headers and loader, and a Vulkan 1.0 driver
- Xlib headers (X11 or XWayland session)
- `glslc` and `spirv-val`
- zlib and a JDK 21 (for `jni.h`)

On Arch-based systems: `pacman -S vulkan-headers vulkan-icd-loader libx11 shaderc spirv-tools zlib jdk21-openjdk`.

The Windows native dependencies can be supplied through vcpkg. Android Studio can install the Android components declared by the project. For command-line Android builds, set `ANDROID_HOME` or add an untracked `local.properties` file:

```properties
sdk.dir=/absolute/path/to/Android/Sdk
```

## Build and test

Build a debug APK:

```bash
./gradlew :app:assembleDebug
```

Run shared JVM/Android host tests, app tests, and Android lint:

```bash
./gradlew :shared:jvmTest :shared:testAndroidHostTest :app:testDebugUnitTest :app:lintDebug
```

Build the unsigned release APK:

```bash
./gradlew :app:assembleRelease
```

The Android package and application ID are both `com.cytarch.cellularity`. Release signing is intentionally left to the distributor and is not stored in this repository. After `./gradlew :app:assembleRelease :app:bundleRelease`, `scripts/sign-android-release.sh --keystore <file.jks>` signs and verifies both outputs; it reads the passwords from the terminal and never stores them.

### Windows desktop

Experimental: these steps have not yet been verified end to end on a Windows system; see the platform status above.

Install the native dependencies with vcpkg, then configure and build the desktop JNI library from an x64 MSVC developer shell:

```powershell
vcpkg install zlib:x64-windows vulkan-headers:x64-windows vulkan-loader:x64-windows shaderc:x64-windows 'spirv-tools[tools]:x64-windows'

cmake -S native -B build/desktop-cmake -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/desktop-cmake --target cellularity_desktop_jni
```

Copy the runtime support libraries beside `cellularity_desktop.dll` when vcpkg has not already done so:

```powershell
Copy-Item "$env:VCPKG_ROOT\installed\x64-windows\bin\vulkan-1.dll", `
  "$env:VCPKG_ROOT\installed\x64-windows\bin\z.dll" build\desktop-cmake\
```

Run the desktop application and its JVM tests:

```powershell
.\gradlew.bat :desktop:run
.\gradlew.bat :desktop:test :shared:jvmTest
```

Build the MSI with the same Gradle runtime:

```powershell
.\gradlew.bat :desktop:packageMsi
```

The installer is written under `desktop\build\compose\binaries\main\msi`. It registers a Start Menu shortcut, offers an installation-directory choice, and does not create a desktop shortcut. Packaging stages the native library, Vulkan support libraries, generated shaders, and textures from `build\desktop-cmake`; warnings from `stageNativeAssets` identify any missing input. A tree configured earlier as `build\win-cmake` is still found.

### Linux desktop

Configure and build the desktop JNI library, then run the application and its tests:

```bash
cmake -S native -B build/desktop-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCELLULARITY_BUILD_DESKTOP=ON
cmake --build build/desktop-cmake --target cellularity_desktop_jni

./gradlew :desktop:run
./gradlew :desktop:test :shared:jvmTest
```

Packages are built with `./gradlew :desktop:packageDeb`, `:desktop:packageRpm` or `:desktop:packageAppImage` (deb needs `dpkg-deb` and `fakeroot`, rpm needs `rpmbuild`) and are written under `desktop/build/compose/binaries/main`. The renderer draws off-screen; it needs an X display, which Wayland sessions provide through XWayland, the same as the desktop toolkit itself.

## Data compatibility and limits

Current and legacy substrate files remain supported, as do existing genome files. Android imports external files through the document-provider interface and copies them into private application storage. Desktop imports use local files; desktop data is stored under `%APPDATA%\Cellularity` on Windows and `$XDG_DATA_HOME/cellularity` (default `~/.local/share/cellularity`) on Linux, unless `CELLULARITY_DATA_DIR` overrides it.

Input files and decompressed legacy substrate data have an absolute 2 GiB safety limit. Simulation capacity is also rejected when its estimated allocation exceeds 2 GiB. These are upper rejection boundaries, not a promise that every device can run a world near the limit; practical capacity depends on available RAM and GPU resources.

## Modules

- `app` — Android activities, platform integration, and packaging.
- `shared` — Compose UI, resources, and presentation logic shared by Android and desktop.
- `desktop` — Compose Desktop window, desktop storage and settings, desktop JNI, and Windows/Linux packaging.
- `native` — C++ simulation, file formats, interaction, GLES/Vulkan rendering, JNI, and host tests.

## Native host tests

The C++ parity tests can run on Linux with Clang, CMake, Ninja, lld, and zlib development headers installed. ccache is optional:

```bash
scripts/native/test.sh
```

See [`scripts/native/README.md`](scripts/native/README.md) for Windows host requirements, sanitizer commands, and benchmarks.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) before submitting a change. Security issues should follow [`SECURITY.md`](SECURITY.md).

Android release operators should follow [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md). User-visible changes are recorded in [`CHANGELOG.md`](CHANGELOG.md).

## License

Cellularity is released under the [MIT License](LICENSE). Bundled dependencies retain their own licenses; see [Third-Party Notices](THIRD_PARTY_NOTICES.md).
