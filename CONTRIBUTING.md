# Contributing

Thank you for contributing to Cellularity.

## Development setup

Use JDK 21 as the Gradle runtime and install the platform dependencies listed in the root README. The build rejects a different JDK major version. Keep machine-specific SDK paths, vcpkg configuration, and signing material out of the repository.

Before opening a pull request, run the checks for the affected platforms.

Common and Android checks:

```bash
python3 scripts/verify-resource-locales.py
python3 scripts/verify-shared-portability.py
./gradlew :shared:jvmTest :shared:testAndroidHostTest :app:testDebugUnitTest :app:lintDebug :app:assembleDebug
scripts/native/test.sh
scripts/native/sanitizer-test.sh
```

Windows desktop changes additionally require the native `cellularity_desktop_jni` build described in the README, followed by:

```powershell
.\gradlew.bat :desktop:test :desktop:packageMsi
```

`scripts/check-public-release.sh` is part of the Android public-release gate on `main`; it intentionally rejects the additional desktop module in `windows-vulkan-port` and is not a check for this branch.

## Change guidelines

- Keep the Android application ID and package namespace as `com.cytarch.cellularity`.
- Keep shared UI behavior platform-neutral; put Windows window, storage, JNI, and packaging integration in `desktop`.
- Add or update tests for behavior changes.
- Do not commit APKs, MSIs, build directories, crash dumps, credentials, signing keys, or local SDK configuration.
- Preserve third-party copyright and license notices.
- Keep pull requests focused and describe user-visible changes and validation performed.
