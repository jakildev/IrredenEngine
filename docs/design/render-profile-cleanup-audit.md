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

## Lifecycle investigations requiring their own fixes

- The observer registration cache retains a pointer after its SystemManager dies;
  repeated stage registration can replace allocated handles without releasing them.
  Test repeated registration and multiple Worlds before changing ownership.
- Metal texture clears have two pattern-buffer caches with different atomic-scratch
  and encoder-timing behavior. Define queued pattern snapshot/order semantics and
  test changing patterns before consolidating them.
- Shared profiling matrix helpers live in the rotation CLI module. A neutral
  module could clarify ownership once needed; preserve the distinct evidence
  requirements of historical summaries and strict timing controls.

The timing-attribution experiment remains a separate measurement task. Cleanup
does not establish a dense-scene speedup, fix inherited visual artifacts, or
qualify million-entity throughput.
