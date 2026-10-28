# Native C++ tools

The host build uses Clang, Ninja, lld, CMake, and zlib. Renderer shader validation uses `glslangValidator`. If ccache is installed, the scripts use it automatically.

## Windows host toolchain

The scripts in this directory are bash and target Linux. On a Windows host use
the wrappers in `scripts/windows/`. Always call the `.cmd` wrapper rather than
the `.ps1` directly: with the default `RemoteSigned` execution policy a `.ps1`
fails with "not digitally signed", which looks like nothing happened.

```bat
:: Report only. Installs nothing, changes nothing.
scripts\windows\install-toolchain.cmd -VerifyOnly

:: Show what would be done, without doing it.
scripts\windows\install-toolchain.cmd -DryRun

:: Install everything that is missing, then verify. Self-elevates.
scripts\windows\install-toolchain.cmd

:: Install a subset.
scripts\windows\install-toolchain.cmd -Components Clang,Jdk21
```

The installer resolves each component in this order: already present, then a
winget package, then a direct download from the vendor. It is idempotent, so
re-running only acts on what is missing. It requires Windows PowerShell 5.1 or
later; PowerShell 7 is optional.

Two components are deliberately not installed:

- **zlib** is a build dependency, not a system tool. Supply it through vcpkg
  (`vcpkg install zlib:x64-windows` and configure CMake with the vcpkg
  toolchain file) or vendor it under `native/third_party/zlib`. vcpkg names the
  x64-windows import library `z.lib` on purpose, so the bare `z` entries in
  `native/CMakeLists.txt` (lines 563, 997, 1527) already resolve given the vcpkg
  toolchain file.
- **`glslc` and `spirv-val`** ship inside the Android NDK at
  `shader-tools/windows-x86_64/`, and `gradle/vulkan-shaders.gradle` already
  resolves them from there per host tag. The Vulkan SDK is therefore optional.

### Building the native host tests on Windows

```bat
:: Configure and build the semantic contract, then run it.
scripts\windows\build-native.cmd

:: Print the exact generated commands without running them.
scripts\windows\build-native.cmd -ShowCommands

:: A different target, or a full ctest pass.
scripts\windows\build-native.cmd -Targets cellularity_simulation_tests
scripts\windows\build-native.cmd -RunTests
```

The script drives MSVC through `vcvars64.bat`, points CMake at the vcpkg
toolchain so zlib resolves, and disables IPO/LTO (its toolchain probe is
unreliable here). It defaults to a Release build in `build/native-win`.

For configure, build and test in one step, use the wrapper that also applies the
environment the host tests need:

```bat
scripts\windows\run-native-tests.cmd
scripts\windows\run-native-tests.cmd -Target cellularity_semantic_contract_tests -SkipTests
scripts\windows\run-native-tests.cmd -Clean
```

It exists because three things are easy to get wrong by hand:

- `vcvars64.bat` must run before CMake, or configuration fails with "No
  CMAKE_CXX_COMPILER could be found";
- host tests create fixtures through the system temp directory, and in a
  restricted environment that directory can be writable by the shell but not by
  child processes, which makes the format tests fail with "Could not create a
  temporary substrate file" - so `TEMP` and `TMP` are redirected under the build
  tree;
- vcpkg's zlib is a shared library, so its `bin` directory must be on `PATH` or
  a test binary exits with `STATUS_DLL_NOT_FOUND` (`0xC0000135`).

An earlier CMake-free single-target builder existed to work around CMake hanging
inside the DSH sandbox. That hang turned out to be a confinement artifact: with
the file sandbox lifted, CMake configures this project in about seven seconds.
The workaround was deleted rather than kept alongside, because it also masked a
real defect - it gave every target one shared include-path list, which hid a
missing zlib include-path dependency that the real build then reported.

Validate the canonical renderer asset inventory and compile/link every shader pair:

```bash
python3 scripts/verify-render-assets.py --validate-shaders
```

Run the native correctness and format-compatibility tests in Release mode:

```bash
scripts/native/test.sh
```

Run the complete host test suite in Debug mode with AddressSanitizer and
UndefinedBehaviorSanitizer:

```bash
scripts/native/sanitizer-test.sh
```

Run the separate host ThreadSanitizer configuration for execution-context,
owner lifecycle, scheduler, and the remaining native tests:

```bash
scripts/native/thread-sanitizer-test.sh
```

ThreadSanitizer is intentionally a separate local gate: it is mutually exclusive
with ASan/UBSan and decoder robustness builds. The
steady-allocation executable is the sole omitted native test because it
intentionally replaces global `new/delete`, which conflicts with TSan's own
allocation interceptors; ordinary and ASan/UBSan suites continue to run it.

Project-owned targets compile with `-Wall -Wextra -Wpedantic`. Sanitizer
configurations additionally use `-Werror`; bundled dependencies keep their own
warning policy. The compiler and associated LLVM tools can be pinned through
the `CELLULARITY_NATIVE_*` environment variables. IPO/LTO is enabled only for
supported optimized builds and is disabled in sanitizer and decoder robustness
configurations.

This also verifies the bounded native diagnostics writer independently: one
owner-only fixed report, expected metadata, and the 4 KiB native size limit.

Run the bounded ASan/UBSan decoder robustness smoke:

```bash
scripts/native/fuzz-smoke.sh
```

Run one decoder fuzzer for a longer local session (findings remain in the
selected corpus directory):

```bash
scripts/native/fuzz.sh release 3600 /tmp/cellularity-release-corpus
scripts/native/fuzz.sh legacy 3600 /tmp/cellularity-legacy-corpus
```

Run a generated simulation benchmark:

```bash
scripts/native/bench-default-random-cells.sh
scripts/native/bench-default-random-cells-detail.sh
```

Run the exact six-scenario behavior baseline and collect the representative
performance signal (wall time, updates/second, peak RSS, and build metadata):

```bash
scripts/native/behavior-baseline.sh
```

Timing is intentionally not a correctness gate. Compare reports from the same
machine and toolchain; establish a hard threshold only after enough stable runs.

Run a benchmark with your own substrate file:

```bash
scripts/native/perf.sh path/to/input.substrate
scripts/native/perf-stat.sh path/to/input.substrate
scripts/native/bench-release.sh path/to/input.substrate
scripts/native/bench-scenario.sh custom path/to/input.substrate
```

Capture a repeatable local baseline from a real substrate, then compare a
later build against its preserved binaries with interleaved A/B runs:

```bash
scripts/native/benchmark-ab.py capture --substrate path/to/input.substrate
scripts/native/benchmark-ab.py compare --baseline path/to/baseline.json
```

The runner uses a fresh process and substrate load for every sample. It rejects
comparisons unless the serialized simulation states match exactly. Reports,
input copies, and baseline binaries stay under the Git-ignored
`.codex/performance/` directory.

Capture the complete throughput baseline (compile-time baseline and
compact-primary variants, Profile-OFF throughput, Profile-ON attribution,
deterministic synthetic matrix, ordered traces, memory probes, APK hashes, and
an optional connected Android target):

```bash
python3 scripts/native/capture-throughput-baseline.py \
  --substrate /absolute/path/to/tardigrade.substrate \
  --android-serial <adb-serial>
```

The report stays in the Git-ignored `.codex/simulation-throughput-baseline/`
directory. Use `--quick` only to verify the capture infrastructure; quick
output is explicitly marked as a smoke run and is not a performance baseline.

Run the isolated persistent-dispatcher laboratory, including the real render
preparation interference workloads:

```bash
python3 scripts/native/dispatcher-lab.py \
  --output-dir .codex/simulation-throughput-plan/c00-dispatcher-lab/run \
  --substrate /absolute/path/to/tardigrade.substrate
```

Android measurements are optional diagnostics and never gate completion or
release. A run never guesses a target: pass the exact serial shown by
`adb devices -l`. Physical devices, emulators, and containerized Android
targets are all accepted; their properties and ABI are recorded with the
result:

```bash
python3 scripts/native/dispatcher-lab.py \
  --output-dir .codex/simulation-throughput-plan/c00-dispatcher-lab/android-run \
  --substrate /absolute/path/to/tardigrade.substrate \
  --android-serial <exact-adb-serial>
```

Optionally capture the C-01 session execution-context lifecycle and
scheduler-name trace on an explicit Android target:

```bash
python3 scripts/native/execution-context-device-trace.py \
  --output-dir .codex/simulation-throughput-plan/c01-execution-context/android-trace \
  --android-serial <exact-adb-serial>
```

The runner never auto-selects a device. It builds an opt-in standalone binary,
materializes the persistent workers for two bind/run/release cycles, validates
their in-process names and floating-point initialization, captures an Android
`sched` trace, and requires the same names in that trace. Device classification
is informational only: no Android trace is required for C-01 acceptance or a
release decision. The trace target is not linked into the application, and the
production default remains zero additional workers.

Default temporary paths:

- build directory: `/tmp/cellularity_native_clang_build`
- ccache directory: `/tmp/cellularity_ccache`
- benchmark output: `/tmp/cellularity_*.json`

If a default directory contains a CMake cache from a different source tree,
the scripts select a source-specific sibling directory and leave the old cache untouched.

Override the build and cache locations when needed:

```bash
CELLULARITY_NATIVE_BUILD_DIR=/tmp/other_build \
CCACHE_DIR=/tmp/other_ccache \
scripts/native/test.sh
```
