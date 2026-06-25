# Rotation perf cliff — profile & cost breakdown (#1963)

**Scopes:** #1961 (perf-parity capstone of #1884). **Profiled:** 2026-06-21,
macOS / Metal (Apple Silicon), `IRPerfGrid` `--mode dense --grid-size 64
--zoom 0.8`. **Method:** `--auto-profile 200` (per-system CPU + per-stage GPU
report at steady state) + the shutdown `profile_report.txt` (avg/min/max across
the run). No production code changed.

## TL;DR

- The cliff is **real, GPU-bound, and a hard binary step** at the residual-yaw
  deadband: exact cardinal (residual 0) → `path=single`; any residual ≥ ~1° →
  `path=peraxis` with full cost. There is no gradual ramp.
- Magnitude at 64³ dense, zoom 0.8: **frame ≈ 7.0 ms (cardinal) → ≈ 11.5 ms
  (off-cardinal), +~4.6 ms (≈1.65×)**.
- **The entire cliff is one stage: `trixelToFb`** (the trixel→framebuffer
  composite), which goes **0.25 ms → 4.9 ms (+4.65 ms)** off-cardinal. Its
  delta accounts for essentially 100% of the frame delta.
- **The voxel→trixel dispatch is NOT the cost.** `voxelStage1` is *flat*
  (~1.2 ms cardinal vs ~0.9 ms per-axis). Three small per-axis canvases ≈ one
  cardinal canvas. **This refutes #1961's hypothesized lever** ("bracket-cache
  the per-axis voxel→trixel raster") — that raster is already cheap.
- Per-frame voxel **upload is ruled out** (static `--wave-amplitude 0` ≈ moving).
  Lighting/AO/shadow stages are flat across the route switch.
- The real lever for #1961 is **`drawPerAxisScatter`** in
  `system_trixel_to_framebuffer.hpp`: a 3-pass forward-scatter depth composite
  over **worst-case-sized** per-axis canvases (`~(2W, W+H)` each).
- Two **profiling-infra gaps** block a finer split *inside* `trixelToFb`
  (composite vs. the per-axis screen-depth resolve) — see "Infra gaps" — and
  must be fixed before #1961's fix can be verified per-stage.

## The cliff (frame time — reliable on both backends)

Default (timestamp-pair) timing, `frame min` (cleanest steady-state):

| Pose (yaw) | route | frame min (ms) |
|---|---|---|
| 0.0 (cardinal) | single | **5.9–7.3** |
| 0.05 (≈2.9° residual) | peraxis | 11.5 |
| π/8 ≈ 0.393 (mid-bracket) | peraxis | 10.0–11.8 |

Cardinal dips below the 8.33 ms (120 Hz) line → **not vsync-masked**; both
costs are real GPU time. The demo's own `RAMP-POSE` log confirms the binary
switch: `residual_deg=0.0` → `path=single` at 0/90/180/270°; `residual_deg=±1°`
and beyond → `path=peraxis`. (Note: the FPS field in the auto-profile line is
unreliable — it disagrees with the frame-time field; use `frame:Xms`.)

## Attribution (per-stage GPU — legacy finish-bracketed timing)

Default timestamp-pair per-stage timing is **not attributable** for this
comparison on Metal: stage timers overlap (the cardinal stage avgs sum to
~18 ms against an 8.6 ms frame — async overlap), and the per-axis encoder
reorganization scrambles which work a timestamp pair brackets (e.g.
`bakeSunShadowMap` swung 2.2 → 0.08 ms, `trixelToFb` 0.5 → 8.4 ms between
runs that share identical lighting). To get a clean, non-overlapping split I
ran with `gpu_stage_timing_legacy = true` (finish-brackets each stage tick,
capturing **all** encoders — including the 3 per-axis dispatches that #1746
otherwise undercounts). `min` across 200 frames, reproduced across runs:

| GPU stage | cardinal min (ms) | per-axis min (ms) | Δ |
|---|---|---|---|
| **trixelToFb** | **0.25** | **4.91** | **+4.66** |
| voxelStage1 (voxel→trixel, 1 vs 3 dispatch) | 1.20 | 0.91 | −0.29 |
| computeLightVolume | 3.15 | 3.14 | ~0 |
| computeVoxelAO | 0.12 | 0.27 | +0.15 |
| computeSunShadow | 0.10 | 0.26 | +0.16 |
| lightingToTrixel | 0.12 | 0.24 | +0.12 |
| bakeSunShadowMap | 0.15 | 0.20 | +0.05 |
| textToTrixel, fogToTrixel, fbToScreen, … | small | small | ~0 |
| **Frame min** | **~7.0** | **~11.7** | **+4.69** |

`trixelToFb`'s +4.66 ms ≈ the +4.69 ms frame delta. **The cliff is the
per-axis framebuffer composite, full stop.** `trixelToFb` is identical
(4.906 ms min) at both yaw=0.05 and yaw=π/8 → the cost is the route, not the
rotation angle (binary step, not a ramp).

## What the cliff is NOT

- **Not the 1→3 voxel→trixel dispatch count.** `voxelStage1` is flat/slightly
  cheaper off-cardinal. Each per-axis canvas is a subset; 3 small dispatches ≈
  1 cardinal dispatch. Bracket-caching this raster (the #1961 hypothesis) saves
  ~0 ms.
- **Not GPU upload / cache invalidation.** Static voxels (`--wave-amplitude 0`)
  give the same cliff as moving (cardinal 6.25 vs 6.53 ms; per-axis 10.29 vs
  10.39 ms). Per-frame voxel re-upload is not on the critical path.
- **Not lighting / AO / shadow / sky.** Those stages are flat across the route
  switch (same scene, same lights).
- **Not CPU-bound.** The workload is GPU-bound (the CPU `render` scope ≈ the
  whole frame = GPU wait). CPU dispatch-encode overhead does rise on the
  per-axis path — `SingleVoxelToCanvasFirst` CPU 0.16 → 1.13 ms (3× clear +
  rebind + encode) — but it is hidden under the GPU wait, not additive to frame
  time.

## Root cause

Off-cardinal, `SYSTEM_TRIXEL_TO_FRAMEBUFFER` replaces the single cardinal
gather draw on the main canvas with **`drawPerAxisScatter`** (T3 / #1310,
`engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp:187-289`):
a **three-pass forward-scatter depth composite** of the X/Y/Z per-axis canvases,
one vertex per source texel, GL_LESS depth-compositing the front face per pixel.
The per-axis canvases are allocated at **worst-case size**
(`IRMath::perAxisTrixelCanvasWorstCaseSize → ~(2W, W+H)`, *larger* than the main
canvas). So the composite goes from "1 gather over the main canvas" to "3
scatters over 3 oversized canvases with per-pixel depth resolution" — ~20× the
stage cost.

The per-axis screen-depth **resolve** (`SYSTEM_RESOLVE_PER_AXIS_SCREEN_DEPTH`)
also runs only off-cardinal; its GPU cost is currently folded into the
`trixelToFb` bracket because it is untimed (see infra gap #2). So "+4.66 ms"
is *composite + resolve* combined; splitting them needs the infra fix.

## Infra gaps found (prerequisites for a finer split / for verifying #1961)

1. **#1746 — timestamp-pair `voxelStage1` undercounts multi-encoder stages.**
   Confirmed empirically: in default timing the per-axis `voxelStage1` reads
   *lower* than cardinal (0.70 vs 2.3 ms) — impossible for a 3-dispatch path;
   only the first of 3 encoders is bracketed. Legacy finish-timing works around
   it but serializes the GPU (not a throughput option).

2. **`resolvePerAxisScreenDepth` is tagged but absent from the stage registry →
   silently untimed (both backends).**
   `system_resolve_per_axis_screen_depth.hpp:204` calls
   `tagGpuStage(systemId, "resolvePerAxisScreenDepth")`, but the registry
   (`gpu_stage_timing.hpp:230`) has no row of that name — it has
   `screenSpaceResidualRotate` instead, which **no system tags** (dead, always
   0.0; only the overlay references it as "SCREEN-ROT"). `tagGpuStage` is a
   silent no-op on an unknown name, so the per-axis resolve stage is never
   recorded. Looks like a rename that updated the system but not the registry
   row (or vice versa). Fixing it (rename the registry row to
   `resolvePerAxisScreenDepth`, or retag the system) makes the resolve stage
   observable and lets #1961 separate composite cost from resolve cost. Filed
   as #1965. **Resolved (#1965):** the dead `screenSpaceResidualRotate`
   registry row (field + overlay label) was renamed to
   `resolvePerAxisScreenDepth`, completing the half-done rename so the system's
   existing tag now resolves and the stage records on both backends.

## Scoping #1961

- **Drop the hypothesized lever.** "Bracket-cache the per-axis voxel→trixel
  raster, re-run only the cheap 2D deformation" targets `voxelStage1`, which is
  already flat. It will not move the cliff.
- **Target `drawPerAxisScatter` / the per-axis composite.** Candidate levers
  for #1961 to evaluate (this spike scopes, it does not prescribe):
  - **Tighten per-axis canvas footprint.** The scatter draws over worst-case
    `~(2W, W+H)` textures × 3. Sizing to the actually-occupied region (the
    per-bracket footprint is yaw-stable) would cut overdraw directly.
  - **Cull empty per-axis texels** before the scatter, or move from
    forward-scatter to a gather composite that only touches covered pixels.
  - **Fold the per-axis resolve into the composite** (one pass instead of
    resolve-then-composite), once gap #2 makes its cost visible.
- **Route-0 consolidation is a correctness call, not a speed one.** Because
  `voxelStage1` is flat, route 0's cardinal special-case buys ~0 GPU time; its
  value is byte-identical cardinal output, not perf. "Remove route 0" would not
  speed anything — keep it for correctness.
- **Order of operations:** land #1746 + the registry-name fix (gap #2) first so
  the composite-vs-resolve split inside `trixelToFb` is measurable and #1961's
  fix can be verified per-stage; otherwise the fix can only be checked against
  the coarse frame-time delta.

## Resolution (#1961) — per-axis empty-cell compaction (composite consumer)

Landed in PR #2007. The lever is the design doc's "compute compaction pre-pass"
(`per-axis-trixel-canvas-rotation.md` §149-159): a `beginTick` compute prelude
(`c_per_axis_cell_compact.{glsl,metal}`) scans each per-axis distance canvas,
appends only the **occupied** cell indices into a per-axis SSBO region, and
writes indirect instanced-draw args. `drawPerAxisScatter` then issues
`drawElementsInstancedIndirect` over that compacted list instead of sweeping the
full worst-case `size.x*size.y` grid (mostly empty). Route 0 (cardinal) is
untouched — the compaction path is taken only while the per-axis canvases are
allocated (rotating).

**Measured before/after** (macOS/Metal, M-series, 64³ dense, `--zoom 0.8`,
`--auto-profile 200`, **avg over 200 frames**, re-measured on master `de496a45`
vs the PR on the same host; two runs per cell, both shown stable). Compared
**master vs PR at the same pose** so the occlusion-cull ratio is held constant
(see the reframing note below — a raw cardinal-vs-off-cardinal column is no
longer apples-to-apples):

| stage / frame (avg) | pose | master | #2007 | Δ |
|---|---|---|---|---|
| **trixelToFb** | off-cardinal (yaw 0.39, ~91 % culled) | **8.94 ms** | **0.31 ms** | **−8.6 ms** |
| **Frame**      | off-cardinal | **11.93 ms** | **8.86 ms** | **−3.07 ms** |
| trixelToFb     | cardinal (yaw 0, ~0 % culled) | 5.28 ms | 4.68 ms | ~0 (route 0 untouched) |
| Frame          | cardinal | 8.60 ms | 8.57 ms | ~0 |

**The rotation cliff is closed.** On master the off-cardinal frame costs
**+3.3 ms** over cardinal (11.93 vs 8.60 ms); with the compaction it costs
**+0.3 ms** (8.86 vs 8.57 ms) — off-cardinal returns to cardinal parity. The
entire win is in `trixelToFb`: the per-axis forward-scatter sweeps the full
worst-case canvas **regardless of how many voxels survive occlusion cull**
(8.94 ms even at 91 % culled), so compacting the draw to occupied cells only
(0.31 ms) removes the variable cost. Cardinal output stays **byte-identical**
(route 0 untouched) — shape_debug render-verify cardinals are max_delta 0.

> **Reframing vs the #1963 baseline (cardinal 0.25 → off-cardinal 4.91 ms).**
> That baseline predates the occlusion-cull pre-pass (#1798/#1799), which now
> culls ~91 % of the 64³ grid at this off-cardinal pose but ~0 % at cardinal
> (occupancy depends on the view angle). So the two poses no longer render the
> same voxel count, and "cardinal vs off-cardinal" mixes the cull delta with
> the scatter delta — at cardinal the cardinal-gather now pays for ~262 k
> un-culled voxels (5.3 ms), at off-cardinal the scatter pays for the empty
> worst-case sweep (8.9 ms). The clean isolation of the compaction's effect is
> **master vs PR at the same pose** (the table above), not cardinal vs
> off-cardinal. The cliff #1961 set out to close — off-cardinal costing more
> than cardinal — is real (+3.3 ms) and closed (+0.3 ms).

**Composite consumer only — resolve consumer deferred.** The plan scoped one
pre-pass feeding *two* consumers (composite + the per-axis screen-depth
resolve). PR #2007 ships the **composite** consumer, which the data above shows
closes the entire measured cliff: the resolve pass never surfaces as its own GPU
stage and the frame floor already returns to cardinal without touching it. The
**resolve** consumer (plan step 3 — `dispatchComputeIndirect` over the same
compacted list in `system_resolve_per_axis_screen_depth.hpp`) is the plan's
sanctioned natural split; it is tracked as low-priority follow-up #2015 (an
attribution/future-proofing improvement, not a perf need).

**Residual per-axis non-determinism (accepted).** The compaction appends cells
in non-deterministic GPU-scheduling order, so where two occupied cells'
`#1494`/`#1883` dilation-margin slivers overlap a pixel at the same plane depth,
the draw order picks the winner. After rebasing onto #1937/PR #2013 (analytic
edge-aware coverage became the authority over the dilation margin), this shrank
to sub-perceptual: `zoom4_yaw45_inter_cardinal` is now **byte-identical
run-to-run**, and `zoom4_pan16_yaw45_pivot` jitters by **max_delta 7, 100.0 %
match** run-to-run — comfortably inside render-verify thresholds (≤ 64 / ≥ 99.9 %),
so both shots stay in the gate rather than being excluded. This is the same
class of accepted per-axis non-determinism the re-voxelize path documents
(round-to-cell speckle), and it is **self-resolving**: C3 #1939 retires the
dilation-margin tower entirely, removing the tie source.

## Repro

```bash
fleet-build --target IRPerfGrid
# cardinal vs off-cardinal frame time (default timing):
fleet-run IRPerfGrid --mode dense --grid-size 64 --zoom 0.8 --yaw 0      --auto-profile 200 --no-overlay
fleet-run IRPerfGrid --mode dense --grid-size 64 --zoom 0.8 --yaw 0.3927 --auto-profile 200 --no-overlay
# per-stage attribution: set gpu_stage_timing_legacy = true in the staged
#   scripts/config.lua, run the same two poses, read
#   build/creations/demos/perf_grid/save_files/profile_report.txt
# route classification: --yaw-ramp --auto-screenshot, grep 'RAMP-POSE'
```

Notes: macOS CoreMIDI init intermittently aborts at startup (error -304),
unrelated to the demo — re-run. `--grid-size 96` hits an engine assertion;
64³ is the supported profiling size and matches the #1882/#1883 harness +
`scripts/dev/perf-grid-rotate-sweep` default.
