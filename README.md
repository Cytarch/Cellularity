# Cellularity

Cellularity is an open-source cellular simulation for Android. The simulation core and renderer are implemented in C++, while the application UI uses Java, Kotlin, and Compose Multiplatform shared code.

The current release line is **0.1.0-alpha.1** (`versionCode 1`). It is an Android-only prerelease, and GitHub Releases is its only planned binary distribution channel. Release binaries must be signed by the project team; signing material is never stored in this repository.

## Requirements

- JDK 21
- Android SDK Platform 36
- Android SDK Build-Tools 36.1.0
- Android NDK 28.2.13676358
- CMake 3.22.1

Android Studio can install the Android components declared by the project. For command-line builds, set `ANDROID_HOME` or add an untracked `local.properties` file:

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

The Android package and application ID are both `com.cytarch.cellularity`. Release signing is intentionally left to the distributor and is not stored in this repository.

## Data compatibility and limits

Current and legacy substrate files remain supported, as do existing genome files. External files are imported through Android's document-provider interface and copied into private application storage before use.

Input files and decompressed legacy substrate data have an absolute 2 GiB safety limit. Simulation capacity is also rejected when its estimated allocation exceeds 2 GiB. These are upper rejection boundaries, not a promise that every device can run a world near the limit; practical capacity depends on available RAM and GPU resources.

## Modules

- `app` — Android activities, platform integration, and packaging.
- `shared` — shared Compose UI, resources, and presentation logic used by Android.
- `native` — C++ simulation, file formats, interaction, renderer, JNI, and host tests.

## Native host tests

The C++ parity tests can run on Linux with Clang, CMake, Ninja, lld, and zlib development headers installed. ccache is optional:

```bash
scripts/native/test.sh
```

See [`scripts/native/README.md`](scripts/native/README.md) for benchmark commands.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) before submitting a change. Security issues should follow [`SECURITY.md`](SECURITY.md).

Release operators should follow [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md). User-visible changes are recorded in [`CHANGELOG.md`](CHANGELOG.md).

## License

Cellularity is released under the [MIT License](LICENSE). Bundled dependencies retain their own licenses; see [Third-Party Notices](THIRD_PARTY_NOTICES.md).
