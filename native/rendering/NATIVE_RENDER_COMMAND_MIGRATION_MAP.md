# Native render command migration map

This document began as the V-23 inventory of every production call that
submitted an arbitrary managed closure through `queueRenderTask`. V-24 has now
migrated microscope input to one synchronous typed batch, and V-25 has migrated
Genome Preview state/interactions plus both file-preview captures to native
typed endpoints. The rows below record the preserved semantics. The surface
host compatibility method and Vulkan arbitrary-task command were removed in
V-26B; the boundary verifier rejects any reintroduced closure call site.

## Capacity evidence

The retained V-19 raw reports expose queued/executed task totals, not
simultaneous queue depth. The largest characterized leg is Vulkan Observe drag
with 196 queued tasks (GLES has 194). V-23 therefore uses the complete 196-task
leg as a conservative high-water upper bound. Applying the required policy,
`nextPowerOfTwo(max(128, 2 * 196))`, gives
`kGenomePreviewInteractionCapacity = 512`. This is intentionally looser than a
claimed depth measurement, remains below the hard cap 1024, and is not
recomputed during V-25.

The V-19 capture scenario is one save request for each file kind; no raw trace
recorded overlapping captures. Both typed stores therefore apply the policy
minimum of two slots (`nextPowerOfTwo(max(2, 2 * 1))`). Overflow is an explicit
typed rejection. Genome capture input bytes have a separate exact budget of
`2 * kGenomeDraftMaximumWireBytes`; substrate requests retain only a token.

## Migrated command paths

| Source operation | Current owner / producer | Current consumer | Ordering and coalescing | Cancellation and lifetime | Completion/publication | Typed replacement |
|---|---|---|---|---|---|---|
| `GenomePreviewView.restoreViewport` | View; Android UI thread | GLES GL thread or Vulkan preview worker | FIFO relative to later editor interactions; never coalesced | Dropped when the surface host closes; three copied floats | No direct result | `GenomePreviewRestoreViewport` in `GenomePreviewInteractionQueue`; viewport token is published after application |
| `GenomePreviewView.update` | View; Android UI thread | Native `GenomePreviewEndpoint` before a frame | Content and view publish independently and coalesce latest-only | Latest state survives a missing surface; endpoint close rejects late updates | GFA2 acknowledgment separates applied revisions from the atomically presented content/view/interaction tuple | Implemented with independent latest content/view snapshots in `GenomePreviewRetainedState`; selected gene/grid/tree are view state |
| `GenomePreviewView.captureGhost` | View; Android UI thread | Preview render owner | Strict FIFO after prior content/view/viewport state; no coalescing | Cancelled by endpoint/queue close | Native ghost state mutation | `GenomePreviewCaptureGhost` with immutable content/view revision fence |
| `GenomePreviewView.clearGhost` | View; Android UI thread | Preview render owner | Strict FIFO with capture and viewport operations; no coalescing | Cancelled by endpoint/queue close | Native ghost state mutation | `GenomePreviewClearGhost` with immutable content/view revision fence |
| `GenomePreviewView.handleMove` | View gesture listener; Android UI thread | Preview render owner | Every delta is ordered; V-25 must not coalesce floating-point operations | Cancelled by controller/attachment generation or close | GLES observes the viewport in its draw callback; Vulkan reads one coherent GFA2 snapshot before View disposal, without steady-state polling | `GenomePreviewMoveViewport`; result token identifies viewport publication |
| `GenomePreviewView.handleZoom` | View gesture listener; Android UI thread | Preview render owner | Every scale/center operation is ordered; no initial coalescing | Cancelled by controller/attachment generation or close | GLES observes the viewport in its draw callback; Vulkan reads one coherent GFA2 snapshot before View disposal, without steady-state polling | `GenomePreviewZoomViewport`; result token identifies viewport publication |
| `GenomePreviewView.handleTap` | View gesture listener; Android UI thread | Preview render owner | Ordered after all earlier viewport/content state | Cancelled by controller/attachment generation or close | Selected gene posted to UI when non-negative | `GenomePreviewSelectGene`; result token identifies selection publication |
| `GenomePreviewView.enqueueGenomePreviewCapture` | Storage worker via coordinator | Preview render owner | Independent of visible editor interaction FIFO; one-shot admission order | Timeout/cancel/close marks request directly; immutable copied GenomeDraft payload | Exactly one completed/failed/cancelled request result | `GenomePreviewCaptureRequestStore`; separate slot and input-byte budgets |
| `AndroidMicroscopeView.enqueueSubstratePreviewCapture` | Serialized simulation save worker via coordinator | Durable microscope render endpoint | Independent one-shot token, ordered by admission | Endpoint lease plus surface attachment generation rejects replacement/loss/cancel/close without borrowing a raw controller handle | Exactly one completed/failed/cancelled request result | `SubstratePreviewCaptureRequestStore` |
| Former `AndroidMicroscopeView.submit(List)`; now `submitCollectedInput` | Gesture interpreter and detector; Android UI thread | One synchronous JNI dispatch applies presentation commands and forwards world commands to the existing simulation-owner mailbox | All commands produced by one `MotionEvent` remain in one CIB1 batch and execute in record order; Pan/Zoom publish one final viewport | Attachment epoch rejects stale input; resync enqueues one existing `TouchCancel` per active pointer before detach | Caller-owned CIR1 publishes per-command statuses, aggregate camera flags and revisions without a managed callback | Implemented by V-24 with `AndroidMicroscopeInputBatchBuilder`; no second microscope queue |

## Fixed ownership

- `GenomePreviewRetainedState` has one ingress mutex and two independent
  `LatestValueExchange` instances. It owns latest immutable content and view
  snapshots but performs no decode, simulation, or drawing.
- `GenomePreviewInteractionQueue` is the only new ordered editor FIFO. Each
  dependent command holds shared immutable content/view fences, so
  `state A -> command -> state B` executes the command against A while B is
  immediately available as latest state.
- `GenomePreviewFrameRequestFlag` coalesces adjacent wakeups without storing
  payload or calling a managed callback; the render owner consumes it once.
- `GenomePreviewCaptureRequestStore` and
  `SubstratePreviewCaptureRequestStore` are separate bounded one-shot stores.
  Cancellation never requires a free FIFO slot.
- `GenomePreviewEndpoint` is the sole render-side owner of retained Genome
  Preview state, ordered interactions and file-capture admission.
- `SubstratePreviewCaptureEndpoint` owns substrate file-capture admission and
  attachment cancellation; both capture kinds share the PCA1/PCR1 request-state
  contract without a managed render closure.
- Microscope touch/tool commands remain owned by the existing
  `MicroscopeCommandMailbox`.
- Surface attach/resize/detach/resume/pause remain solely in
  `VulkanClearFrameRenderThread`; its command queue accepts only typed
  lifecycle, frame, diagnostics and test-barrier messages.
- V-23 adds no JNI method and no second production endpoint/state owner.
- V-24 adds one fixed-layout microscope input submit JNI method plus compact
  presentation-motion/resync accessors; it does not add another world mailbox.

## Microscope input descriptor

V-26B owns exactly one JNI descriptor:

```text
(JJLjava/nio/ByteBuffer;Ljava/nio/ByteBuffer;J)V
```

The first `J` is the durable render-endpoint handle, the second is the expected
surface token, and the final `J` is the retained placement-genome revision.
The genome payload is published once when selection changes rather than copied
through every Tap. CIB1/CIR1 remain unchanged; a stale placement revision
rejects only the Tap record with wire status `12`.
