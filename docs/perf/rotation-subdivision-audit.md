# Rotation and subdivision profiling audit

Native Metal, Apple M4 Max, macOS, Debug build, IRPerfGrid 64³ voxel workload.
These measurements establish current bottlenecks; they are not an optimization
before/after comparison or evidence of rotation parity. The four matrix cells
use the same executable SHA-256, recorded in each manifest, with three fresh
300-frame runs per cell. Defaults retain the animated per-cell wave, lighting
and shadows. The CPU report includes startup and fixed-update scheduling.

Full CPU/GPU reports, commands, source state, binary hashes and diagnostic
excerpts are in [rotation-subdivision-audit/](rotation-subdivision-audit/).
`scripts/perf/repeat_profile.py` retains full native logs locally too.

| Camera yaw | Zoom / effective subdivision | Frame mean ms | Run mean range ms |
|---|---|---:|---:|
| 0° | 1 / 1 | 10.387 | 9.930–11.280 |
| 45° | 1 / 1 | 22.813 | 22.710–22.940 |
| 0° | 4 / 4 | 14.720 | 14.410–15.030 |
| 45° | 4 / 4 | 30.253 | 29.680–31.070 |

All cells use FULL mode, base subdivisions 1, grid size 64. Fixed framebuffer
resolution does not imply fixed intermediate work. At zoom 4, the cardinal
cull still reports approximately 255K candidates out of 262K voxels; the
rotated counter reports approximately 777K entries across three axis lists.
That rotated numerator counts repeated face candidates, not unique voxels.

## Visual acceptance before further optimization

Plain detached presentation retains projected source faces through the final
quad draw. The rotated single-voxel, adjacent-pair, frame and octahedron now have
independent silhouette checks; usable single-voxel face interiors also check
normal ownership. The [source-face contract](../design/trixel-face-reconstruction-validation.md)
records the distinction from lattice consistency. Remaining visual work is
placement/depth, attachment/motion coverage, and revoxelized self-shadow/AO patches.
Keep that work ahead of performance changes; no blur-based silhouette correction.

## Bottleneck evidence

At 45°, zoom 4, the GPU stage means across three runs are 6.763 ms for per-axis
storage, 4.382 ms for overflow, 5.572 ms for finalization and 3.059 ms for
scattering. Light-volume computation is 4.812 ms; finite voxel casting is
0.670 ms. These are sampled invocation durations, not automatically additive
frame totals. The current one-slot Metal timer skips repeated same-frame
invocations until its command buffer completes.

The CPU's `UpdateVoxelSetChildren` costs about 2 ms per update invocation;
`PropagateTransform` about 0.45–0.48 ms. The last rotated zoom-4 run has 539
update calls over 300 render frames, versus 179 in the last cardinal zoom-1
run. Fixed-step catch-up amplifies CPU work when GPU rendering slows. Compare
per-call cost and total cost per rendered frame separately.

This establishes CPU scope timing and GPU encoder-boundary measurement from
the native tools. It does not establish CPU sampling call stacks, GPU hardware
occupancy, bandwidth attribution or an instruction-level shader profile.

## Measurement defects corrected

The report parser accepted only legacy three-column GPU rows. Current reports
contain average, minimum, maximum and sample count; those rows were silently
lost. Both formats now parse, with regression coverage.

Metal previously accepted an unwritten start timestamp of zero followed by a
valid absolute end timestamp. One such startup sample produced a stage maximum
of roughly 1.63 billion milliseconds and contaminated its mean. The retained
invalid report is diagnostic evidence, not a baseline. The repaired rotated
zoom-1 depth-resolve row is about 0.028 ms with 298 valid samples.

Polling now distinguishes pending, ready and invalid. It gates reads on command
buffer completion, rejects zero/stale/reversed boundaries and releases invalid
slots without inserting zero durations. It also prevents stale same-frame
readback when a stage is invoked repeatedly. Completed command buffers are
released during presentation even if the corresponding timer becomes dormant.
The table uses the four `final-grid64-*` directories, captured with the final
backend lifetime cleanup. A subsequent invalid-end freshness guard (also
tracking the prior start boundary) passed the retained 120-frame
`freshness-guard-smoke` run. Earlier `valid-grid64-*` captures retain the initial
audit before lifetime cleanup; their differing ranges are not an isolated
before/after speedup measurement.

A first measured optimization removes unused rotated micro-slices without changing
scene pixels; see [rotated face dispatch density](per-axis-dispatch-density.md).
The remaining work below still applies.

## Proposed optimization TODO

The [world-scale visibility plan](world-scale-visibility.md) defines the million
simple-entity target, unbounded orthographic viewport depth, spatial grouping,
and the separation between render visibility and simulation cadence.

0. **CanvasStress source-face coverage:** the purple frame (orbit 7) and lime
   octahedron (orbit 3) use plain `DETACHED`; the green striped cube (canary 1)
   uses `DETACHED_REVOXELIZE`. The source-face producer retains complete
   faces through GPU record lighting and continuous quad presentation, with
   independent geometry gates and retained failing lattice controls. See [the investigation and evidence](../design/detached-projected-face-coverage.md).
   The green cube already reconstructs triangles; its alternating staircase
   normals remain when AO and shadows are disabled and must not be flattened.
   Proposed follow-ups: quantify the remaining green self-shadow/AO patches;
   fix private SDF/voxel mixed-canvas density, recentering and scratch lifecycle;
   extend placement/attachment/motion and multi-face depth oracles;
   measure the source-face path's sorting, raster and lighting costs. Plain detached world shadow
   casting/receiving remains unsupported. OpenGL visual validation is pending.

1. [Unique retained candidates and axis entries](voxel-cull-work-units.md) now
   have separate counters and producer-matched readback. Add generated subdivision
   samples, occupied cells, overflow entries and scratch bytes. Separate useful
   visible work from shadow-caster work.
2. [Frozen controls](rotation-controls.md) now cover fixed zoom/varying density,
   one-degree yaw, matched projected block extents and culling toggles. Extend
   repeatable timing to continuous sweeps and entity revoxelization. The original
   four cells keep the scene and framebuffer fixed, not projected area.
3. Profile culling before subdivision expansion. Reject hidden/off-screen face
   work early enough to avoid generating it, while retaining off-screen geometry
   whose shadows can reach visible receivers. Compare existing culling toggles
   before introducing another culling structure. Existing yaw-aware chunk
   projection has no viewport depth cutoff; new mask regressions cover deep
   viewing rays and full-turn reentry. Measure allocation-group bound inflation
   before introducing spatial hierarchy or changing pool layout.
4. [Frozen coverage arbitration](../design/frozen-scatter-flicker.md) resolves the
   observed exact/margin flicker. [Overflow face deduplication](overflow-face-dedup.md)
   now passes stable controls and reduces scatter work. Retain same-class ties
   and continuous-camera temporal checks as follow-ups. [Current-frame sort dispatch](current-frame-overflow-sort.md)
   removes the unsorted empty-to-nonempty transition, bounds GPU work to the live
   span, and bounds CPU stage encoding to a prior-count hint with headroom.
   Continue reducing repeated per-axis storage/finalization work using measured
   counters; [GPU body controls](gpu-cost-attribution.md) isolate sorting and
   propagation as useful targets; the tested shadow/AO arithmetic changes showed
   no gain; preserve finite
   geometry, trixel reconstruction and shadow coverage.
5. [Light-volume candidate pruning](light-volume-candidate-pruning.md) skips
   occlusion reads for neighbors that cannot win, with identical lighting-demo
   captures. Continue measuring subdivision-dependent light-volume and
   finite-caster bounds, density and memory traffic before changing either
   representation.
6. [Native CPU sampling](native-cpu-sampling.md) now distinguishes presentation
   waiting from active update/bounds/upload stacks. [Contiguous update spans](voxel-update-spans.md)
   are now batched. [Mixed upload saturation and Metal copy ordering](static-upload-saturation.md)
   preserve GPU-owned positions. Measure fragmented copy overhead next; preserve
   fixed-workload controls and per-call versus per-frame counts.
7. Establish Release and OpenGL controls, repeated run ranges and profiling-off
   overhead. [Metal full-frame GPU accounting](gpu-frame-accounting.md) now covers
   all submissions, including screenshot splits, independently of sampled stages.
   Add an independent OpenGL frame-query ring; do not sum sampled stage rows.
   Keep screenshots as correctness gates for each optimization PR.
8. Establish capacity-verified scaling to 100³ simple entities and report tail
   latency as well as means. The [million-entity capacity audit](million-entity-capacity.md)
   verifies creation in a 128³ pool. [Conservative overflow demand sizing](overflow-demand-capacity.md)
   eliminates observed drops and establishes a more expensive correctness baseline.
   [Live-count overflow lighting](live-overflow-lighting.md) removes capacity-sized
   lighting dispatch without a demonstrated frame-time gain. Measure memory and
   remaining command-encoding and sorting costs before choosing growth or paged pools.
   Keep asynchronous diagnostic snapshots as a separate OpenGL follow-up: the
   existing retired-frame read is a memcpy on Metal but may synchronize on GL.
   Include continuous camera motion and independent entity motion; keep viewport
   rejection separate from simulation interest.
9. The completed-frame million control averages about three updates per rendered
   frame. Profile reduced/staggered simulation cadence, accumulated-time correctness,
   attachment transforms, interpolation and bounded catch-up/promotion costs.
   Audit large-coordinate/depth representation limits independently of the
   unbounded-depth viewport policy.

The desired outcome is cost that tracks useful screen coverage under rotation
and zoom. Near-cardinal parity is a performance target, not a promise supported
by the current implementation or these measurements.
