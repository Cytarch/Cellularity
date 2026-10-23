# Contributing

Thank you for contributing to Cellularity.

## Development setup

Use JDK 21 as the Gradle runtime and install the Android SDK/NDK versions listed in the root README. The build rejects a different JDK major version. Keep machine-specific SDK paths and signing material out of the repository.

Before opening a pull request, run:

```bash
scripts/check-public-release.sh
python3 scripts/verify-resource-locales.py
./gradlew :shared:jvmTest :shared:testAndroidHostTest :app:testDebugUnitTest :app:lintDebug :app:assembleDebug
scripts/native/test.sh
scripts/native/sanitizer-test.sh
```

## Change guidelines

- Keep the Android application ID and package namespace as `com.cytarch.cellularity`.
- Add or update tests for behavior changes.
- Do not commit APKs, build directories, crash dumps, credentials, signing keys, or local SDK configuration.
- Preserve third-party copyright and license notices.
- Keep pull requests focused and describe user-visible changes and validation performed.
