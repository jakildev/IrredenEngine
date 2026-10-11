# Rendering and profiling cleanup audit

First pass against master `e1e5b98a3`, after the bounded-light and timing-control
stack merged. Scope: rendering backend/infrastructure, voxel and lighting shader
families, timing collectors, and profiling scripts. Priorities favor shared
contracts with observable behavior over broad renames or a new rendering layer.

## Implemented slice: timestamp bookkeeping

`detail::GpuTimestampRing` owns one implementation of allocation, asynchronous
polling, round-robin reuse and timestamp writes. Both whole-system and substage
timers use it. Pending pairs stay occupied, invalid pairs become reusable without
recording a zero, and completed pairs contribute one sample. The helper adds no
GPU waits and leaves scope boundaries, CPU timing and legacy finish timing intact.
Handle lifetime remains a caller policy: observers release through their device;
the process-lifetime substage timer performs no GPU calls at static destruction.

The accompanying comments distinguish latest resolved invocation from frame total,
and absent samples from zero duration. The [timing contract](gpu-stage-timing-cost-model.md)
remains the canonical interpretation of those rows.

## Implemented slice: complete pooled profiling evidence

The million-control summary reports pooled steady mean and tail percentiles only
when every included report supplies a valid steady series. A missing series marks
that statistic unavailable instead of silently excluding a potentially slower run.
Complete reports retain the same per-report warmup exclusion and pooling. A
mixed complete/missing regression test fails on the previous implementation.

Shared matrix execution and evidence helpers live in
`scripts/perf/profile_matrix.py`, used by the rotation and million-entity CLIs.
Historical summaries and strict timing controls retain their distinct evidence
requirements.

## Implemented slice: renderer resource cleanup

Canvas destruction and backing growth share the texture/Hi-Z release sequence;
growth still allocates replacements first and preserves ancillary buffers. Metal
framebuffer readback delegates its existing synchronous submission to `finish()`,
retaining validation before submission and completed-frame accounting before the
command buffer is renewed. Presentation remains a separate operation.

The resource manager's unused private singleton declarations and commented
accessors are removed. The uncompiled scratch file
`systems/copilot_nonesense.cpp` and its quality-tool exclusion are removed too;
neither provided an engine API.

## Implemented slice: per-axis integer addressing

The AO, sun-shadow, lighting, fog and depth-resolve shader pairs share
`ir_per_axis_cell_dispatch`: workgroup-row flattening and linear-cell pixel
decoding. The finalizer shares their argument offset and tile size. Bounds checks
remain before compacted-list reads, and each wrapper retains its bindings and
route-specific coordinate recovery.

`test_render_per_axis_cell_dispatch.py` executes both helpers through a C++
adapter, enumerates multirow dispatches beyond 65,535 workgroups, checks row-major
pixels and CPU/shader constants, and rejects lost-row/wrong-column mutations.
Native shader execution and captures remain necessary to cover compilation,
binding and rendering behavior. This extraction preserves the merged store-frame
work and does not consolidate distinct face coordinate spaces.

## Implemented slice: lighting-route density state

`LightingRouteScope` changes the route and canvas size and restores borrowed
bindings. It leaves subdivision density untouched: AO, sun shadow, lighting and
fog recover per-axis positions from the store frame and encoded sub-cell fraction.
Fog LOS uses its scale argument only on the cardinal route; overflow lighting
does not read subdivision options, and overflow fog passes scale one.

Store, scatter and screen-depth resolve retain their density handling. In
particular, resolve still needs density for view positions, frame offsets and
microcell footprints; its patch/restore and the shared setter remain.

`IRFogDemo --lighting-density-check` requires either `--peraxis-overflow` or
`--occlusion=high-ground` with `--auto-screenshot`. Its rotated/cardinal/resumed
shots check effective subdivisions above the store cap, parked/live transitions,
and nonempty overflow in the overflow fixture. Fog registration ensures compute
lighting runs instead of presentation lighting. The opt-in diagnostic readback
blocks for GPU completion and is unsuitable for performance measurements.

The [capture evidence](../pr-screenshots/codex/lighting-density-cleanup/README.md)
compares these controls and mixed main/detached canvases before and after the
two-write removal. These are end-to-end regression checks, not isolated kernel
proofs or a claim that every inherited visual artifact is resolved.

## Receiver-edge validation

The receiver-edge investigation found a producer/consumer mismatch: smooth-yaw
solid BOX emissions carry world-face slots, while the strict-query miss path
treated them as view-space slots. The receiver specialization now preserves the
emitted world normal through its existing lighting carrier without changing the
recovered receiver position. The corrected normal also changes the normal bias
applied during shadow lookup. Other producers retain their distinct slot convention.

GPU probes ruled out fog reveal as the cause of the three white-panel blocks:
their grid state is fully visible and their incoming color is already 92.
The floor's final-fragment normal is -Y, confirmed by a GPU diagnostic and an
independent slab oracle; its Lambert factor predicts the current brighter color.
The original built-in normal overlay followed only the compute route and could
disagree with finite fragment lighting. The normals diagnostic now shares the
beauty path's eligibility and finite query; unsupported and fog paths retain
their compute normal colors.
The [diagnostic controls](../pr-screenshots/codex/final-surface-normal-diagnostics/README.md)
compare normal output with the isolated final-fragment probe and preserve all
seventeen CanvasStress/explored-fog beauty frames byte-for-byte.
The [receiver-edge evidence](../pr-screenshots/codex/receiver-edge-validation/README.md)
records the probes, analytical fixtures and before/after controls.

The three inherited macOS reference outliers are reconciled with the existing
surface policy after signed-face proofs: floor edges select -Y; the fog panel's
emitter and finite recovery both select -X. Native probes establish the actual
normal and incoming fog color. Thresholds remain unchanged, and the original
references remain in the receiver-edge evidence for review.

The explored-decay fixture is a fog state/channel test, not a floor-contact or
SDF/voxel geometry-parity test. Its SDF panels use full BOX sizes (4,4,1) at z=2;
the floor uses (96,96,2) at z=5. At density d the analytical panel bottom is
z=2+0.5/d and the floor top is z=4.5-0.5/d, leaving a positive gap. The authored
fog square of ±4 conservatively encloses the smaller SDF panel, while the voxel
source panels intentionally occupy a larger footprint and include a positive
channel margin for recovered boundary columns. Source comments state these
contracts without changing geometry, channels, camera poses or references.

The cleanup incorporates the merged store-frame, fog LOS, SDF receiver/normal
and explored-state work through master `a08e0a44a`. The shared dispatch helper
lives in both new AO bodies, preserving their smooth-yaw specialization.
Consolidating integer addressing is distinct from merging view/model/world-space
transforms or changing quantization and binding restoration.

## Implemented slice: fallback shadow and diagnostic cleanup

The legacy depth bake consumes single-canvas encoded depth throughout: main
SDF/text input follows visual yaw; per-axis and detached input first resolve to
cardinal layout. Producer completion and lighting restoration publish route zero.
Both shader backends therefore use `decodeDepthSingle` directly and omit raw
per-axis reconstruction and its include. The shared frame ABI stays unchanged.
Per-axis resolve still disables coverage splats; detached resolve retains them.
This does not change the default finite-face caster path or shadow softness.

Source-face shadow diagnostics use the existing `surfaceShadowDebugColor` helper.
`test_render_legacy_shadow_bake.py` executes extracted producer restoration, bake
orchestration and both shader entry bodies with recording adapters. Its poisoned
route inputs, 256 CPU configurations, 540 shader cases per backend and 21 mutation
controls check encoding, dispatch selection, yaw and splat restoration. Projection
math, device bindings and real buffer ABI remain outside that adapter's proof.
The test shares brace extraction with the existing visibility-routing controls.

[Native capture evidence](../pr-screenshots/codex/shadow-fallback-cleanup/README.md)
compares legacy beauty, legacy shadow diagnostics and default-path shadow diagnostics
at cardinal and non-cardinal yaw. These are behavior-preservation controls;
legacy point-scatter artifacts remain outside this cleanup.

## Implemented slice: stage observer lifetime

Stage registration finds the observer in the current SystemManager's owned list.
Clear, unregister and manager destruction therefore invalidate lookup without a
process-wide pointer cache. Identical tags preserve pending queries; a changed tag
releases the old ring through its recorded device and resets unpublished samples
before creating the new ring. Frame dispatch and timing boundaries stay unchanged.

The timestamp tests cover duplicate tags, relabeling pending samples, partial
allocations, two live managers, clearing and reconstructing the manager at the same
address. Generic lookup tests cover derived types, first-match order, misses,
unregistration and the main-thread guard. These isolate manager/resource lifetime;
they do not establish a full multi-World engine restart contract or a speedup.

All 28 focused timestamp/registration tests pass. Restoring the parent observer
implementation makes the five new timestamp regressions fail; removing the lookup
thread guard makes its worker rejection test fail. The
[native controls](../pr-screenshots/codex/gpu-observer-lifetime/README.md) compare
rendered frames with stage timing enabled and disabled.

## Implemented slice: Metal clear-source snapshots

Both texture-clear APIs share the device-owned per-texture source cache. Matching
size and pixel bytes reuse the current source, including null versus explicit zero
patterns. A changed pattern gets a replacement buffer; the old source retires
through the existing deferred-release queue while a frame is active. Queued clears
therefore retain their own values instead of reading a later CPU overwrite or the
first value ever supplied. Texture destruction still removes its cache entry.

The common helper owns pattern storage only. Device clears retain timed encoders
and R32I atomic-scratch mirroring; texture clears retain their existing untimed
encoder and startup upload paths. Neither path adds a GPU wait. Constant per-frame
clears reuse their allocation; changing patterns can allocate until the frame
drains, so this is a correctness repair and consolidation, not a measured speedup.

Native tests queue distinct patterns and copy each result before submitting once.
They cover both APIs, alternating APIs, 4/8/16-byte pixels, null clears, R32I scratch
and partial uploads. All five ordering regressions fail against the parent backend.
The [native captures](../pr-screenshots/codex/metal-clear-snapshots/README.md)
retain unchanged normal and shadow-overlay output for the frozen scene.

## GRID span-cap classification

`GridSpanCoverageTest` drives the real GRID rebuild tick against an independent
45-degree Y inverse-transform oracle. The frozen CanvasStress orbit-6 solid
occupies 1740 destination cells: 610 boundary cells and 1130 interior cells.
Its 1728-slot span retains all 610 boundary cells; the 12 omitted cells each
have all six neighbors in the full occupancy. Every emitted face mask matches
that full occupancy, including neighbors omitted from the span. Consequently,
the omission neither removes exterior faces nor exposes artificial interior
faces in this fixture. This is a geometry/mask result, not proof that volume
sampling, AO, or arbitrary transforms are unaffected.

The capacity limit can still remove visible geometry. A centered 12×1×12 plate
at the same rotation covers 145 cells, all boundary, but has only 144 slots.
The negative control demonstrates one missing surface cell. Carving that same
plate from a 12³ allocated box retains all 145 cells and their face masks.
These controls distinguish surface loss from interior omission without changing
allocation policy or treating the generic overflow warning as harmless.

The independent [gather sampling checks](trixel-gather-sampling.md) cover the
frozen orbit-6 displayed normals and silhouettes at four cardinal camera yaws
and 135 degrees. Those native checks and this CPU producer check cover different
stages; neither substitutes for shadow-receiver or light-volume validation.

## Remaining investigations

- Extend source-face classification to frozen CanvasStress at 45-degree camera
  yaw; the gather checks cover a different set of camera angles.
- Define a destination-capacity policy for exact-fit thin or scaled GRID sets
  whose boundary cells exceed their authored span. The orbit-6 result above
  resolves the observed 12-cell warning's exterior geometry, not this broader
  allocation limit or the lighting consequences of missing interior occupancy.

The timing-attribution experiment remains a separate measurement task. Cleanup
does not establish a dense-scene speedup, fix inherited visual artifacts, or
qualify million-entity throughput.
