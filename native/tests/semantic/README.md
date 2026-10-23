# Simulation semantic contract

This suite freezes the current Level 1 simulation transition semantics before
the interaction pipeline is reorganized or parallelized. Passing only the
final-state digest is not sufficient: a reordered transient interaction can
produce the same state in one step and a different trajectory later.

Level 2 is a separate candidate-versus-reference gate for optimizations that
change only floating-point reduction order. It does not replace or relax the
Level 1 golden.

## Contract layers

Each fixture records three independent streams and one normalized final state:

| Stream | Frozen behavior |
|---|---|
| `sweep` | Oriented exact-pair order and row phase. The two encoded row IDs distinguish same-row from next-row pairs. |
| `endpoint` | Ordered directed admissions, independent endpoint capacity, skipped pairs, containment, fertilization targets, and dead-contact cleanup. |
| `apply` | Cell-major directed intent order, active typed handler, RNG consumption, topology changes, ciliocyte queue/application, and cell lifetime events. |

The normalized state begins with the canonical substrate snapshot and adds
non-serialized transition state needed by later stages: update stage, contact
cache freshness, global RNG state, derived cell fields, ordered contacts,
collision intents, and pending ciliocyte interactions. Runtime cell IDs are
intentionally excluded because they are reassigned by loading.

All numeric trace fields have explicit widths. Floating-point values are
encoded by their IEEE bit patterns in little-endian canonical order. The suite
does not hash object padding or use `std::hash`.

## Level 2 relaxed-reduction contract

`cellularity_relaxed_semantic_contract_tests` keeps event kinds, integer and
boolean event fields, topology, RNG, entity identities, contacts, typed order,
and other discrete state exact. Floating-point event fields are projected out
of its dedicated trace. Numeric world state is compared over 32 updates with
fixed absolute/relative tolerances; a further 1024 updates enforce finite and
bounded state, valid topology, and exact discrete behavior. Incubate-style
`update(false)` render-source frames are compared against the reference at
initial, short-horizon, and periodic long-run checkpoints.

The checked-in smoke candidate compares the global-edge and Cell-owned signal
kernels. Future relaxed candidates plug into the same independently-created
world pair and must not widen tolerances to hide a discrete or formula-input
change.

## Fixtures

| Fixture | Boundary protected by the fixture |
|---|---|
| `pair_geometry` | Equal-X/cell-index tie, row boundary, orientation, zero distance, and exact tangency. |
| `endpoint_capacity` | Independent one-sided and two-sided first-32 admission. |
| `containment_cleanup` | Containment chains, competing killers, and cleanup of references to dead cells. |
| `fertilization_overwrite` | Multiple target overwrite, mutual resolution, and RNG order. |
| `fertilization_before_death` | Fertilization precedence over containment death. |
| `fertilization_incoming_dead_target` | Three-Cell fertilization removes a third-party Ciliocyte contact before ordinary, typed, or impulse apply. |
| `typed_virocyte_visibility` | A typed mutation is visible to a later intent in the same apply pass. |
| `virocyte_rng_before_safety` | Infection RNG is consumed before keratinocyte safety is checked. |
| `typed_glueocyte` | Ordered topology mutation. |
| `typed_devorocyte` | Ordered mass transfer. |
| `typed_secrocyte` | Ordered secretion effect. |
| `typed_ciliocyte_capacity` | Cell-contact order, boundary-last order, and the full 32 + 1 queue capacity. |
| `topology_capacity_swap_remove` | Link capacity, rejection, swap-remove, and boundary-link mutation. |
| `render_contact_cache` | Invalidation and freshness transitions of the render contact cache. |
| `split_copy_remove_compaction` | Split, copy, death, removal, topology repair, and active-index compaction. |
| `save_load_equal_x_tie` | Equal-X ordering remains identical after runtime identities are reassigned by save/load. |

Every fixture runs twice before the golden comparison. The save/load fixture
also compares source and decoded worlds directly and first proves that their
runtime identities differ.

## Trace storage and production isolation

The sink keeps a streaming SHA-256 and event count for each stream. It retains
at most 512 encoded events per stream for first-divergence diagnostics; it
never grows an unbounded event list. A fixture that exceeds that diagnostic
bound fails instead of silently weakening its error report.

Instrumentation is compiled only into the host-only
`cellularity_simulation_semantic` target. With
`CELLULARITY_SEMANTIC_TRACE` absent, call sites preprocess to no-ops. The
`cellularity_semantic_trace_isolation` test inspects both archives and rejects
trace symbols in the public simulation library.

## Running and intentional changes

Run the contract through CTest or directly:

```sh
cellularity_semantic_contract_tests
cellularity_relaxed_semantic_contract_tests
```

To inspect a proposed new baseline:

```sh
cellularity_semantic_contract_tests --emit-baseline
```

Do not replace `semantic-contract-v1.tsv` merely to make a refactor pass. A
changed stream is either a Level 1 regression or an explicitly reviewed Level
2 behavior change. In the latter case, document the new rule and create a new
contract version so the compatibility decision remains visible.
