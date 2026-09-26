# Cooperative analytical-box face indexing

The first Z workgroup processes the three light-facing box faces uniformly
across its 64 lanes. Lane zero reserves and writes one record. A workgroup
barrier publishes its index; every lane reads that index, then a second barrier
protects it against reuse by the next face. Lane `l` visits flattened tile
indices `l + 64k`, so each tile has exactly one owner. Existing tile atomics
still arbitrate contributions from different faces and boxes.

The scalar and cooperative specializations share geometry, cascade clipping,
record format, capacity checks and tile publication in `ir_sun_face_index`.
Voxel callers retain the original scalar nested loop. Cooperative calls must
provide identical geometry and bounds on all 64 lanes; kernel eligibility,
Z-group selection and degenerate/off-map decisions are workgroup-uniform.
No lane reads another lane's record contents. The existing post-dispatch
storage barrier publishes those records to receiver passes.

## Native experiment

Apple M4 Max, macOS 26.5.2, Metal Debug, AC, 2560×1440. The command is
`python3 scripts/perf/box_shadow_controls.py --rounds 2 --output <fresh-dir>`.
It holds the scene/camera/lighting fixed and interleaves four cases in forward
and reverse order, with 275 frames and two captures per run.

Order: original, standalone Metal prototype, shared implementation, restored
original, final shared implementation. Original is the three-independent-lane
parent at `b872e9592b83dedfb3c88dd95b39cbed10620fd4`. All 40 runs use the same
demo binary. Only runtime shader staging changes. Final staging matches source.
The first shared implementation used a scalar flattened loop; the final one
restores the scalar nested loop, leaving cooperative behavior unchanged.

The table excludes the exploratory prototype: four observations per arm across
32 runs. [Reports, manifests and summaries](cooperative-box-face-index/) retain
all five matrices; `candidate` is the standalone prototype, `final` and
`final-repeat` are the shared implementation.

| Case | Original frame ms (range) | Cooperative frame ms (range) | GPU envelope original / cooperative ms | shapeCastBoxes original / cooperative ms |
|---|---:|---:|---:|---:|
| span18-count1 | 9.480 (9.42–9.57) | 9.518 (9.47–9.56) | 5.325 / 5.647 | 1.441 / 0.185 |
| span128-count1 | 9.557 (9.51–9.60) | 9.545 (9.53–9.57) | 4.961 / 5.645 | 1.012 / 0.116 |
| span512-count1 | 16.790 (16.72–16.86) | 9.580 (9.49–9.66) | 16.008 / 6.452 | 10.883 / 0.432 |
| span18-count65 | 9.538 (9.47–9.57) | 9.502 (9.35–9.56) | 5.348 / 5.386 | 1.241 / 0.306 |

The large-box wall mean improves **42.9%**, with GPU envelope down **59.7%**.
Small cases show essentially unchanged wall time. Their GPU-envelope means
vary, including an increase from 4.961 to 5.645 ms at span 128; this is not a
claim of a GPU-envelope improvement at every size. The box-stage mean falls
in every fixture. Its scope includes indexing and fallback rasterization.

These capture-assisted Debug means include startup and readback stalls;
GPU envelope is not busy time and stage means are not additive frame budgets.
The results qualify this analytical-box workload, not general game throughput
or a million-entity target. Native OpenGL performance remains unmeasured.

## Correctness evidence

- Final shared-implementation timing captures match the original in all
  32 corresponding comparisons (16 per matrix). The exploratory prototype's
  16 captures also match.
- [Nine full-turn mixed-mode pairs](../pr-screenshots/codex/cooperative-box-face-index/README.md)
  have identical RGB pixels. This preserves pre-existing visual artifacts;
  it does not fix them.
- Both production emission adapters match independent box slab intersections
  for 952,576 rays each, and reject missing faces, duplicate groups and geometry
  mutations. All lanes are required to submit all three faces.
- The cooperative adapter executes the production indexer on 64 host threads
  with real atomics and barriers. Explicit tile lists check unique records,
  full-map partitions, saturated lists, exhausted records, off-map/degenerate
  faces, stored geometry and guard words. Missing/duplicate tiles and duplicate
  record reservation mutations fail.
- Barrier-invocation counts reject deletion of either barrier. The host adapter
  uses an atomic shared slot to keep those mutants defined; it does **not**
  deterministically prove every late-reader interleaving or GPU memory model.
  Source review verifies publication and reuse ordering; native backend runs
  remain required. Metal was exercised; native Windows/OpenGL is pending.
- The scalar complete-buffer, capacity and mutation suite still passes.

Both barriers are required: the first publishes the lane-zero index, and the
second prevents a fast lane zero from overwriting it for the next face while a
slow lane is still reading it. Neither changes the face-record or tile budget.
As before, global record exhaustion can change which exact faces are retained
under scheduling changes; fallback coverage remains bounded but pixel identity
is not promised for global exhaustion.

## Remaining work

Conservative face-versus-tile rejection and saturated-list handling remain
separate experiments. This change reduces serial tile-loop latency without
reducing tile reservations. No blur, coverage inflation or receiver bias is
introduced. PR #3804's rendered-center correction overlaps box emission;
preserve that geometry correction when reconciling it with this scheduling.
