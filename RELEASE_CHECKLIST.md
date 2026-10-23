# GitHub Release Checklist

This project currently publishes Android prereleases only through GitHub Releases.

Physical Android hardware is never a release gate. Host/JVM tests, sanitizers,
static APK verification, and cross-builds for every supported ABI are the
required checks. A run on a physical, virtual, or containerized Android target
is optional diagnostic evidence; record the target class when one is used.

## One-time signing setup

- Create a dedicated Android release signing key owned by the Cellularity team.
- Store the key and passwords outside this repository in at least two secure locations.
- Record the certificate fingerprint and recovery procedure in the team's private records.
- Keep the same key for every future `com.cytarch.cellularity` update. Losing it prevents normal updates of existing installations.
- Never commit a keystore, encoded key, password, or signing configuration containing secrets.

## Source release gate

- Confirm `versionName`, monotonically increasing `versionCode`, and `CHANGELOG.md` agree with the intended tag.
- Confirm the release commit is on `main`, the working tree is clean, and the public repository has only the intentional public history.
- Run:

```bash
scripts/check-public-release.sh
python3 scripts/verify-resource-locales.py
python3 scripts/verify-shared-portability.py
./gradlew :shared:jvmTest :shared:testAndroidHostTest :app:testDebugUnitTest :app:lintDebug :app:assembleDebug :app:assembleRelease
scripts/native/test.sh
scripts/native/sanitizer-test.sh
scripts/native/fuzz-smoke.sh
bash scripts/verify-release-apk.sh app/build/outputs/apk/release/app-release-unsigned.apk
```

- Review every warning newly introduced by the release diff, even when the gate remains green.

## Binary preparation

- Sign the release APK outside the repository with the permanent team key. Do not publish `app-release-unsigned.apk`.
- Verify the signed APK with Android Build-Tools `apksigner`, including its certificate fingerprint.
- Generate and record the signed APK's SHA-256 checksum.

Optional runtime diagnostics on any explicitly selected Android target:

- Install and smoke-test the exact signed artifact on a clean runtime/profile.
- For later releases, verify an update over the immediately previous public Cellularity version without clearing app data.
- Test creating, saving, reopening, sharing, and importing a substrate; test a genome file.
- Test a sanitized real-file corpus spanning the supported legacy format eras, including pre-gzip and gzip substrates. Do not publish private user files as fixtures.

## GitHub publication

- Create an annotated tag matching the version, for example `v0.1.0-alpha.1`.
- Create a GitHub Release from that exact tag and mark alpha/beta builds as prereleases.
- Paste the relevant changelog section into the release notes.
- Attach only the signed APK and its SHA-256 checksum.
- Confirm the published APK version, package name, signature fingerprint, and checksum after downloading it back from GitHub. A cold launch of the downloaded artifact is optional target-specific diagnostics.
- Keep GitHub Releases as the only binary distribution channel until the release policy is intentionally changed.
