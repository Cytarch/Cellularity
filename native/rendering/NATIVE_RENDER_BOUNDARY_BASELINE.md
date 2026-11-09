# Native render boundary baseline

This document fixes the measured Android managed/native render boundary and
records the V-20 transaction collapse, V-21 publication split, V-22
Environment Field Preview state endpoint, V-25 retained Genome Preview
endpoint with typed one-shot captures, and the V-28 native Vulkan render
worker. Later stages may reduce the listed calls further, but must update this
document and `scripts/verify-native-render-boundary.py` together.

## Counting rules

- A logical frame starts at `GLSurfaceView.Renderer.onDrawFrame` for GLES or at
  `NativeVulkanRenderWorker::renderRequestedFrame` consuming a requested frame on
  `CellularityVk` for Vulkan.
- Vulkan managed frame requests use one `nativeRequestFrame` JNI call per
  request. Adjacent requests can coalesce in the native desired-state flag;
  native draw and present do not make a per-frame controller JNI call.
- JNI calls used only for surface creation, resize, teardown, settings, or
  explicit diagnostics are not included in the steady logical-frame count.
- A runtime-publication poll is counted as an independent status operation,
  even when native returns no new revision; it is not part of a logical frame.
- An Environment Field Preview state change is a separate UI-domain publish;
  every preview frame consumes the retained native state with one draw call.
- Offscreen file capture is a separate transaction. The ordinary onscreen frame
  requested after capture is counted independently.
- The counters report attempted calls that pass the managed bridge guards during
  a live profile. Closed/stale owners are outside the baseline scenarios.

## Current call graph

| Surface family | Managed owner and execution thread | JNI sequence for one logical operation | Current count |
|---|---|---|---:|
| GLES microscope | `AndroidGlRenderer` on the `GLSurfaceView` EGL thread | `nativeDrawMicroscope` | 1/frame |
| GLES microscope, No Render | `AndroidGlRenderer` on the `GLSurfaceView` EGL thread | `nativeClearMicroscopeNoRender` | 1/frame |
| Vulkan microscope | `VulkanClearFrameView` on the Android UI thread; `NativeVulkanRenderWorker` on `CellularityVk` | `NativeVulkanRenderWorkerJni.nativeRequestFrame`; native `MicroscopeRenderFrameSource::renderFrame` | 1/request; requests coalesce, 0 draw JNI/frame |
| Vulkan microscope, No Render | `VulkanClearFrameView` on the Android UI thread; `NativeVulkanRenderWorker` on `CellularityVk` | `NativeVulkanRenderWorkerJni.nativeRequestFrame`; native `MicroscopeRenderFrameSource::renderFrame` | 1/request; requests coalesce, 0 draw JNI/frame |
| Microscope runtime publication | Session-owned serial publication dispatcher, independent of a surface | `nativePollMicroscopeRuntimePublication` | 1/poll at 50 ms visible or 100 ms hidden/no-render |
| GLES Genome Preview | GLES on-demand `GLSurfaceView` EGL thread | `nativeDrawGenomePreview` | 1/frame |
| Vulkan Genome Preview | `VulkanClearFrameView` on the Android UI thread; `NativeVulkanRenderWorker` on `CellularityVk` | `NativeVulkanRenderWorkerJni.nativeRequestFrame`; native `GenomePreviewRenderFrameSource::renderFrame` | 1/request; requests coalesce, 0 draw JNI/frame |
| GLES Environment Field Preview | GLES on-demand `GLSurfaceView` EGL thread | `nativeDrawEnvironmentFieldPreview` reading retained native state | 1/frame |
| Vulkan Environment Field Preview | `VulkanClearFrameView` on the Android UI thread; `NativeVulkanRenderWorker` on `CellularityVk` | `NativeVulkanRenderWorkerJni.nativeRequestFrame`; native `EnvironmentFieldPreviewRenderFrameSource::renderFrame` reading retained state | 1/request; requests coalesce, 0 draw JNI/frame |
| Environment Field Preview state change | Android UI thread through the controller lifetime guard | `nativePublishEnvironmentFieldPreviewState` | 1/changed state; duplicate UI state is suppressed before JNI |
| Genome file capture | Storage worker admits and awaits; active GLES EGL or Vulkan render thread executes | `NativePreviewCaptureBridge.nativeEnqueueGenome`, `nativeAwait`, `nativeDestroy`; optional `nativeCancel` | 1 enqueue + 1 await + 1 destroy/capture |
| Substrate file capture | Storage worker admits and awaits; active GLES EGL or Vulkan render thread executes | `NativePreviewCaptureBridge.nativeEnqueueSubstrate`, `nativeAwait`, `nativeDestroy`; optional `nativeCancel` | 1 enqueue + 1 await + 1 destroy/capture |

### GLES microscope

```text
Android / Choreographer or GLSurfaceView cadence
  -> GlesMicroscopeSurfaceView.requestFrame / continuous render mode
  -> AndroidGlRenderer.onDrawFrame                         [EGL thread]
  -> MicroscopeRenderer.onRenderFrame
  -> NativeCellularityControllerBridge
       -> nativeDrawMicroscope or nativeClearMicroscopeNoRender
  -> return to GLSurfaceView; Android performs EGL swap later

AndroidMicroscopeView status timer                         [main thread]
  -> session publication dispatcher
  -> nativePollMicroscopeRuntimePublication                [cellularity.so]
  -> decode immutable publication
  -> generation-checked UI delivery                        [main thread]
```

`cellularity.so` cannot observe the eventual Android-owned EGL swap result.
The callback is therefore the last observable app-owned boundary for GLES.

### Vulkan microscope

```text
Android / Choreographer request                              [UI thread]
  -> VulkanMicroscopeSurfaceHost / VulkanClearFrameView
  -> NativeVulkanRenderWorkerBridge.requestFrame
  -> NativeVulkanRenderWorkerJni.nativeRequestFrame
  -> coalesced native desired-frame state

NativeVulkanRenderWorker::renderRequestedFrame               [CellularityVk]
  -> MicroscopeRenderFrameSource::renderFrame
  -> NativeCellularityController::drawMicroscopeForCurrentMode
  -> borrowed Vulkan RenderSurfaceSession adapter
  -> acquire, submit and present
  -> packed native RenderFrameResult; native retry/status state

AndroidMicroscopeView status timer                         [main thread]
  -> session publication dispatcher
  -> nativePollMicroscopeRuntimePublication                [cellularity.so]
  -> generation-checked UI delivery                        [main thread]
```

The only steady managed-to-native frame request is `nativeRequestFrame`. The
controller's native draw returns the packed outcome through `SourceFrameResult`;
the Vulkan adapter performs acquire, submit and present before the native frame
source returns. No mutable last-result JNI side channel remains.

### Genome Preview

```text
GenomePreviewView
  -> publish immutable content or view state                [UI thread]
  -> enqueue typed viewport/select/ghost interaction        [UI thread]
  -> native GenomePreviewEndpoint                           [retained latest + FIFO]
  -> backend frame callback
       GLES: nativeDrawGenomePreview
       Vulkan: NativeVulkanRenderWorker
         -> GenomePreviewRenderFrameSource::renderFrame
         -> GenomePreviewEndpoint::draw
         -> applyPendingOnRenderOwner
         -> draw retained native state
         -> publish GFA2 only after primary onscreen present
  -> read GFA2 through typed endpoint handle                [Vulkan reveal gate]
  -> read GFA2 once before View disposal                    [viewport retention]
```

Content and view revisions are independent. `selectedGeneIndex`, grid/tree,
viewport and ghost state are native-owned; draw has no frame-varying managed
argument. Surface recreation preserves retained content and does not resimulate
it. Cell count, selection and viewport are returned in the revision-keyed frame
acknowledgment. The Vulkan first-frame gate keeps the Surface covered until the
GFA2 presented tuple reaches the newest managed targets; offscreen capture does
not advance that tuple. Vulkan does not poll GFA2 in steady state. The View
takes one additional coherent acknowledgment snapshot before disposal so the
native-owned viewport can be restored before the next Surface is attached.

### Environment Field Preview

```text
DensityFieldPreviewView.update
  -> exhaustive target/type wire mapping
  -> nativePublishEnvironmentFieldPreviewState
  -> validated immutable EnvironmentFieldPreviewState
  -> atomic latest-only native retained pointer

next render callback
  GLES: nativeDrawEnvironmentFieldPreview
  Vulkan: nativeRequestFrame                         [UI thread]
          -> NativeVulkanRenderWorker              [CellularityVk]
          -> EnvironmentFieldPreviewRenderFrameSource::renderFrame
  -> retained state + current native viewport
  -> production EnvironmentFieldPreviewRenderer
```

The persistence-oriented `nativeEnvironmentState` and
`nativeApplyEnvironmentPatch` methods remain authoritative Environment editor
operations, but are no longer render-frame entry points. GLES and Vulkan
consume the same command and production renderer. Surface recreation keeps the
retained state and rebuilds only viewport-dependent command fields.

### File previews

```text
managed save coordinator
  -> NativePreviewCaptureBridge enqueue                     [storage worker]
  -> bounded native one-shot request store
  -> production offscreen renderer on the active render owner
  -> NativePreviewCaptureBridge.nativeAwait                 [storage worker]
  -> PCR1 metadata + 512 x 512 x RGBA8 byte[]
  -> Android Bitmap / PNG or WebP encoder
```

Genome capture owns an immutable genome payload independent from visible editor
state; substrate capture owns a preview token and attachment generation. Both
use the production renderer. Timeout is non-consuming and is followed by typed
cancel; terminal payload delivery is exactly once. Raw readback and encoder
work are not an onscreen frame. V-29 owns any later transport/codec change.

## Additional boundary traffic

- Microscope runtime publication is surface-independent and permits at most one
  poll in flight. Visible Observe/Freeze/Incubate use the native 50 ms contract;
  hidden and No Render use 100 ms. Detached, paused, and released views do not
  poll. Reload/save lock contention skips a tick instead of blocking either the
  main thread or the session worker.
- Each accepted Zoom command reads `nativeViewportState`, returning a new
  `float[]`. Active-camera cadence also reads the same array every vsync.
- One two-pointer MOVE commonly produces two touch moves, one pan, one zoom,
  four command JNI calls, and one viewport snapshot after accepted zoom.
- V-26B removed the surface-host `queueRenderTask` compatibility method and the
  Vulkan arbitrary-task command. Production frame requests are typed; no managed
  render closure crosses the boundary.
- Ordinary Vulkan frame completion does not cross JNI. The native worker sends a
  coalesced wakeup only for surface reattachment, recoverable failure or terminal
  failure; managed code then reads compact status through
  `nativeReadCompactStatus`. Explicit diagnostics use the tokenized
  `nativeRequestDiagnostics` / `nativeReadDiagnosticsChunk` /
  `nativeReleaseDiagnostics` transaction. Failure snapshots are read as
  revision-addressed diagnostics chunks.
- The standalone Vulkan diagnostic activity uses the same native worker with
  `VulkanDemoFrameProducer`; production microscope and preview delegates use
  their typed native frame sources. None requires a per-frame managed demo draw
  JNI call.

## Native source ownership at V-28

| Source | CMake owner | Final DSO |
|---|---|---|
| `jni/NativeRenderJni.cpp` | `CELLULARITY_JNI_SOURCES` | `cellularity.so` |
| `jni/NativeMicroscopeJni.cpp` | `CELLULARITY_JNI_SOURCES` | `cellularity.so` |
| `jni/NativeGenomePreviewJni.cpp` | `CELLULARITY_JNI_SOURCES` | `cellularity.so` |
| `jni/NativePreviewCaptureJni.cpp` | `CELLULARITY_JNI_SOURCES` | `cellularity.so` |
| `jni/NativePreviewCaptureRequestHandle.cpp` | `CELLULARITY_JNI_SOURCES` | `cellularity.so` |
| `jni/NativeControllerJni.cpp` | `CELLULARITY_JNI_SOURCES` | `cellularity.so` |
| `jni/NativeRenderProfilingJni.cpp` | `CELLULARITY_JNI_SOURCES` | `cellularity.so` |
| `jni/NativeVulkanRenderWorkerJni.cpp` | `CELLULARITY_JNI_SOURCES` | `cellularity.so` |
| `app/render/NativeVulkanRenderWorker.cpp` | `cellularity_render_worker` | `cellularity.so` |
| `app/render/sources/MicroscopeRenderFrameSource.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/render/sources/GenomePreviewRenderFrameSource.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/render/sources/EnvironmentFieldPreviewRenderFrameSource.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/render/sources/VulkanDemoFrameProducer.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/genome_preview/GenomePreviewEndpoint.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/microscope/SubstratePreviewCaptureEndpoint.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/render/preview/NativePreviewCaptureLayouts.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/render/preview/NativePreviewCaptureRequestState.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/EnvironmentFieldPreviewEndpoint.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/EnvironmentFieldPreviewState.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/NativeRenderSession.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `app/microscope/NativeMicroscopeRuntime.cpp` | `CELLULARITY_RENDERING_SOURCES` | `cellularity.so` |
| `render/backend/vulkan/VulkanBackendAbiExports.cpp` | `CELLULARITY_VULKAN_BACKEND_SOURCES` in `native/cmake/VulkanBackendSources.cmake` | `cellularity_vulkan.so` |

`cellularity.so` must not acquire a Vulkan loader dependency. The validator
checks this source split and the existing profiling compile flag.

## Representative profile matrix

Each capture uses one APK hash and records its exact git revision/dirty diff,
ADB command log, display dimensions, backend, scene, duration, and scenario.

| Scenario id | Required action | Backends |
|---|---|---|
| `freeze-static` | Freeze, no input | GLES + Vulkan |
| `observe-idle` | Observe, no input | GLES + Vulkan |
| `observe-drag` | Observe with repeated one-pointer camera drag | GLES + Vulkan |
| `observe-pinch` | Observe with repeated pinch zoom | GLES + Vulkan |
| `observe-fling` | Observe with repeated fling and settle | GLES + Vulkan |
| `incubate-idle` | Incubate, no camera input | GLES + Vulkan |
| `incubate-camera` | Incubate with camera movement | GLES + Vulkan |
| `incubate-no-render` | Incubate No Render, no input | GLES + Vulkan |
| `genome-ordinary` | Genome editor ordinary preview | GLES + Vulkan |
| `genome-grid` | Genome editor grid enabled | GLES + Vulkan |
| `genome-tree` | Genome editor tree enabled | GLES + Vulkan |
| `genome-ghost` | Capture and clear ghost | GLES + Vulkan |
| `genome-editor-input` | Move, zoom, select, and change selected gene | GLES + Vulkan |
| `environment-density` | Drag density field controls | GLES + Vulkan |
| `environment-salinity` | Drag salinity field controls | GLES + Vulkan |
| `environment-food-lipid` | Drag food-lipid field controls | GLES + Vulkan |
| `environment-gravity` | Drag gravity field controls | GLES + Vulkan |
| `genome-save` | Save one genome with production preview | Active backend |
| `substrate-save` | Save one substrate with production preview | Active backend |

Raw reports belong under `.codex/native-render-boundary/v19/` and are never
committed. Use only an explicit Waydroid or physical-device serial:

```text
scripts/profile-android-render-backend.py \
  --serial <waydroid-serial> \
  --backend vulkan \
  --scene tardigrade.substrate \
  --scenario observe-drag \
  --duration 30 \
  --camera-sweeps 12 \
  --apk <diagnostic-apk> \
  --output .codex/native-render-boundary/v19/vulkan-observe-drag.json
```

For every capture compare callback p50/p95/p99, JNI calls per callback,
managed allocated bytes per callback/input event, input-to-native-accept,
native-accept-to-present, full-diagnostics cost, task counts, memory deltas,
and simulation UPS. Results characterize one device and are not runtime policy.

The ART allocation counter is process-wide: its normalized frame/input values
are a repeatable A/B signal for an otherwise identical scenario, not an exact
attribution of every byte to the render boundary. A normalized allocation
value is `-1` when the runtime counter is unavailable or the selected capture
contains no event of that kind.

Repository and built-Release checks use the same canonical validator:

```text
scripts/verify-native-render-boundary.py
scripts/verify-native-render-boundary.py \
  --release-apk app/build/outputs/apk/release/app-release-unsigned.apk
```

The APK leg verifies that R8 removed the V-19 managed counter storage and
report markers; diagnostic variants retain them intentionally.
