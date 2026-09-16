# Million-entity capacity audit

The existing configurable voxel pool supports more than 64³ entries. Setting
`config.voxel_pool_edge = 128` before engine initialization allocates 2,097,152
slots. IRPerfGrid successfully creates 100³ independent single-voxel entities;
the profile reports 1,000,177 total entities including engine/demo entities.
No pool clamp or allocation failure occurred. This establishes capacity, not
60 fps or complete rotated rendering.

## Measurements

Apple M4 Max, Metal Debug, FULL mode, base subdivisions 1, zoom 4, frozen
per-cell wave, default lights/shadows, GPU profiling enabled. Two 300-frame
runs per row, same executable and shader fingerprints. Each report includes
initial rendered frames; these are not warm-up-excluded steady-state samples.

| Voxel entities | Pool edge | Yaw | Frame mean ms (run range) | Per-run p95 range ms | Per-run p99 range ms |
|---:|---:|---:|---:|---:|---:|
| 262,144 | 64 | 45° | 17.540 (17.530–17.550) | 17.84–17.98 | 22.94–24.07 |
| 262,144 | 128 | 45° | 19.165 (19.130–19.200) | 19.90–19.92 | 24.22–25.69 |
| 1,000,000 | 128 | 0° | 32.710 (32.460–32.960) | 33.53–34.32 | 36.23–36.92 |
| 1,000,000 | 128 | 45° | 44.705 (44.400–45.010) | 47.89–50.94 | 58.96–60.85 |

Runs were grouped in this order: million/45°, 262K/large-pool/45°,
million/0°, a screenshot sweep, then 262K/default-pool/45°. The larger pool
shows a roughly 1.63 ms frame difference at the same population in this local
sample. Repeat interleaved controls before attributing that difference to a
particular allocation or upload path. Increasing grid size changes projected
extent; the million-vs-262K comparison does not hold screen coverage constant.

Raw reports, manifests, actual entity counts, diagnostic excerpts and exact
config snapshots/hashes are in [million-entity-capacity/](million-entity-capacity/).
The report script fingerprints shaders/binary but not runtime Lua, so the
separate config snapshots are essential provenance. The pool128 config applies
to every case except `defaultpool64-yaw45`. Source head is `8085ce7bf`; only the
ignored runtime config was temporarily overridden, then restored.

The [overflow demand follow-up](overflow-demand-capacity.md) corrects the
capacity limit below and records its memory and timing costs. This audit
retains the original incomplete-coverage measurements.

## The next capacity limit is face overflow

Both million/45° runs report **671,737 dropped overflow entries**, with capacity
524,288. Those timings describe incomplete rotated coverage and must not be
used as a quality-preserving performance result. About 900K unique candidates
remain after culling, generating about 2.70M axis-list entries. The framebuffer
does not yet bound all intermediate work.

`C_PerAxisTrixelCanvases::allocate` sizes overflow from canvas area rather than
pool size or proven live face demand. Raising voxel capacity therefore does
not raise this limit. The screen-cell heuristic is not a guarantee that every
appendable face fits. The prior no-pressure conclusion in the
[per-axis design](../design/per-axis-trixel-canvas-rotation.md) does not extend
to this workload.

The retained 0°/40° screenshots are diagnostic views from a 15-pose yaw sweep
(`--mode voxel_set --grid-size 100 --wave-freeze --zoom 4 --yaw-ramp
--yaw-ramp-wave --auto-screenshot 6`), captures 264 and 269. The zoomed scene
extends beyond the frame; the HUD remains visible and is not a pixel oracle.
They establish the rendered workload, not correctness of every voxel or the
cause of each visible pattern. The sweep is not a continuous-motion benchmark.

## Follow-up implementation order

1. Make overflow capacity correctness explicit before claiming further gains.
   Compare conservative demand sizing, measured growth, and paged/segmented
   face storage. A delayed counter alone cannot guarantee the first frame of a
   camera jump; define a complete fallback or same-frame capacity handling.
   Retain canonical draw order and prove zero drops across stress sweeps.
2. Measure GPU allocation/peak memory and capacity-sized clearing, upload and
   sorting work. Prefer live-count dispatch and spatial rejection before
   permanently inflating every buffer. A bigger pool is a useful control,
   not the final streaming or residency architecture.
3. Compare spatially grouped versus scattered allocation at identical geometry.
   Introduce world-region pages or multiple pools only with stable handles,
   attachment ownership, movement/migration, and shadow coverage accounted for.
4. Repeat corrected-coverage runs with independently moving entities, matched
   projected extent, continuous yaw, Release, and profiling disabled. Frozen
   data still incurs update traversal; these runs do not measure reduced-cadence
   world simulation. Full-frame GPU accounting remains separate from sampled
   invocation durations; do not sum the GPU rows into a frame budget.
