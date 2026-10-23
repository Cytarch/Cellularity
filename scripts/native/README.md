# Native C++ tools

The host build uses Clang, Ninja, lld, CMake, and zlib. Renderer shader validation uses `glslangValidator`. If ccache is installed, the scripts use it automatically.

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
