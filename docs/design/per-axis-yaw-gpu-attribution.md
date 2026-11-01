# Per-axis yaw GPU delta — per-stage attribution (#2281, post-#2273)

Phase-1 measurement record for #2281: *where does the residual yaw-vs-cardinal
GPU frame delta live, per stage?* #2256 attributed a residual delta to the
per-axis lighting/composite stages and closed via PR #2273 (occupied-cell
indirect dispatch), which A/B'd **perf-neutral on Metal** — the machinery
landed, the delta did not recover. This is the measurement-only attribution
that gates the Phase-2 lever decision (a design gate; **no lever is picked
here**).

Read alongside [`gpu-stage-timing-cost-model.md`](gpu-stage-timing-cost-model.md)
— the reading contract for these timers.

## Method

- **Tool:** existing per-system GPU stage timers, drained at shutdown to
  `save_files/profile_report.txt` (`--- GPU stage timing ---` table,
  avg/min/max over the run). No new timers, no engine changes (plan §Phase 1).
- **Driver:** `IRPerfGrid --auto-profile 300` (enables frame timing, resets
  the accumulators, runs 300 frames, dumps the report).
- **Scenes / poses:** two scenes × {cardinal (yaw 0), `--yaw 0.35` rad ≈ 20°,
  non-cardinal → per-axis path active}, **3 repeat runs each** (12 runs).
  - **Scene A** — `--mode voxel_set --zoom 3` (the #2256 header pose; 262 144
    voxels, high zoom).
  - **Scene B** — default wave scene (`IRPerfGrid`, default zoom).
- **Host:** macOS / Metal, shared 4-pane fleet host (not strictly quiet). The
  run-to-run noise bound below is well under every reported delta, so the
  findings hold regardless; absolute ms should not be quoted off-host.
- **Backend note:** Metal only. The per-axis path has a known run-to-run
  nondeterminism at fixed pose (#2255, open) — the repeat-run spread captures
  it.

## Results (mean of 3 runs; ms)

### Scene A — voxel_set --zoom 3

Frame time: **cardinal 23.11 → yaw 81.82 (Δ +58.71)**; run spread ≤ 1.6 ms.

| stage | cardinal | yaw 0.35 | Δ (yaw−card) | yaw run-spread |
|---|---:|---:|---:|---:|
| computeVoxelAO | 0.043 | 64.72 | **+64.68** | 1.40 |
| trixelToFb | 18.11 | 68.23 | **+50.13** | 1.46 |
| voxelStage1 *(bundle)* | 14.87 | 64.86 | **+49.99** | 1.41 |
| computeLightVolume | 17.89 | 3.18 | −14.72 | 0.06 |
| bakeSunShadowMap | 14.78 | 0.09 | −14.69 | 0.00 |
| computeSunShadow | 0.034 | 0.319 | +0.285 | 0.02 |
| lightingToTrixel | 0.036 | 0.093 | +0.057 | 0.00 |
| fogToTrixel | 0.010 | 0.015 | +0.004 | 0.00 |
| resolvePerAxisScreenDepth | — (0 samples) | — (0 samples) | — | — |

### Scene B — default wave scene

Frame time: **cardinal 8.59 → yaw 14.74 (Δ +6.15)**; run spread ≤ 0.26 ms.

| stage | cardinal | yaw 0.35 | Δ (yaw−card) | yaw run-spread |
|---|---:|---:|---:|---:|
| computeVoxelAO | 0.023 | 5.924 | **+5.90** | 0.09 |
| trixelToFb | 5.097 | 9.513 | **+4.42** | 0.08 |
| voxelStage1 *(bundle)* | 1.915 | 6.090 | **+4.18** | 0.07 |
| bakeSunShadowMap | 1.654 | 0.106 | −1.55 | 0.01 |
| computeLightVolume | 4.774 | 3.284 | −1.49 | 0.06 |
| computeSunShadow | 0.018 | 0.126 | +0.107 | 0.02 |
| lightingToTrixel | 0.016 | 0.099 | +0.083 | 0.02 |

## Reading these numbers — the per-stage rows overlap, they are NOT additive

The single trustworthy top-line is **frame time** (Δ +58.7 ms / +6.2 ms). The
per-stage rows must be read through the Metal encoder-window semantics
([cost-model §1](gpu-stage-timing-cost-model.md)): each row spans *[first
encoder start on GPU, last encoder end on GPU]* for that system's whole tick.
When the per-axis work is one back-to-back GPU burst with no intervening CPU
sync, adjacent stages' windows **overlap** and each reads roughly *"my start →
end of the whole burst."* Two direct symptoms in the data:

- **Yaw:** `voxelStage1` (65.8), `computeVoxelAO` (65.6), `trixelToFb` (69.2)
  read *near-identical windows with matching min/max* — and their deltas sum
  to ~165 ms against a **+58.7 ms** frame delta. They are measuring one shared
  ~65 ms per-axis burst, not three separable costs.
- **The "drops" are re-attribution, not real work.** `bakeSunShadowMap`
  (14.8→0.09) and `computeLightVolume` (17.9→3.2) do not get *cheaper* under
  yaw — the sun-shadow bake / 128³ light volume are pose-independent. At
  cardinal their windows happened to swallow the GPU backlog; under yaw the
  per-axis stages open earlier windows that claim it, so bake/lightVolume fall
  back to their true (small) isolated cost.

So the honest per-stage conclusion is a **band, not a line item**: the whole
yaw delta lives in the per-axis fan-out burst — the `voxelStage1` bundle
(per-axis raster ×3 into the X/Y/Z canvases), `computeVoxelAO` (per-axis AO
textures), and `trixelToFb` (per-axis depth-tested composite). The current
whole-tick timers cannot split that burst further, and `voxelStage1` is a
bundle row regardless ([cost-model §1 rule 2](gpu-stage-timing-cost-model.md)).

## Findings

1. **The yaw delta is real, large, and reproducible** — ≈3.5× frame time at
   zoom 3, ≈1.7× at default zoom, run-to-run spread far below the delta. It did
   **not** recover after #2273. Not a "not reproducible" close.
2. **It is the per-axis fan-out burst**, measured as one overlapping ~65 ms
   window across `voxelStage1` + `computeVoxelAO` + `trixelToFb`. No single one
   of those rows is separately trustworthy here.
3. **Coverage/occupied-cell-bound, not invocation-count-bound.** Every mover
   scales ~10–12× between Scene B (default zoom) and Scene A (zoom 3), tracking
   on-screen coverage × subdivision density — the same conclusion the
   cost-model doc reached for #2256, now confirmed at two poses.
4. **`resolvePerAxisScreenDepth` is not a mover** — 0 samples in the perf_grid
   pipeline at either pose (its per-axis screen-depth resolve is not a
   separately-tagged system in this scene; folded into the burst above). This
   matches the #2271 finding that the resolve sweep reads ~0.
5. **Secondary infra finding for #2280:** the whole-tick encoder-boundary
   timers systematically mis-attribute cost between adjacent stages of a
   sync-free GPU burst (several adjacent rows read near-identical inflated
   windows — cardinal: `voxelStage1`/`bake`/`lightVolume`/`trixelToFb` all
   ~15–18 ms; yaw: the three above all ~65 ms). Any per-axis sub-stage scopes
   #2280 adds must bracket their own encoders to be separable.

## Phase-2 recommendation (design gate — architect decides)

Per the plan and [cost-model §3 rule 1](gpu-stage-timing-cost-model.md)
("attribute before re-architecting"), **do not pick a per-stage lever from
this table** — the three big rows are one unseparated burst, and the
cost-model doc has now twice refuted picking a lever off a whole-tick / mostly-
empty read (#2266, #2271). The band-level attribution points at the per-axis
**fan-out** (three canvases + their AO) as the structural driver; the candidate
levers, each with the decisive probe it needs *first*:

- **Collapse the 3 per-axis passes toward union coverage** (one pass over the
  union of occupied cells instead of three per-axis sweeps). Probe: sub-stage
  scopes (#2280 pattern extended to the per-axis burst) to confirm the 3×
  fan-out — not per-cell body cost — is the driver.
- **Reduce per-axis AO cost.** Probe: force a uniform body reduction in the
  per-axis AO kernel and diff `computeVoxelAO` (#2266 method) — confirm the AO
  body, not its dispatch grid, holds the cost before touching it.
- **Reduce per-axis raster cost.** Blocked on #2280 — `voxelStage1` is a bundle
  and cannot attribute to the per-axis raster sub-dispatch until intra-tick
  scopes exist.

Also weigh **whether to pursue at all**: the delta only appears at non-cardinal
residual yaw (a transient during rotation, the per-axis canvases free at the
cardinal), and #2255 (per-axis nondeterminism) is still open on the same
surface. Cardinal / static scenes are byte-identical and unaffected.

## Phase 2 direction (architect)

Ruling on the Phase-1 escalation above. Both blockers the recommendation cited
have cleared since the escalation, so Phase 2 proceeds — **neither pick a lever
from the band nor defer** — and its first step is the finer attribution that is
now possible.

**What changed after the escalation:**

- **#2280 closed** (merged as PR #2288): the `voxelStage1` sub-stage rows
  (compact / clear / stage-1 / stage-2) are live. Lever (c)'s stated blocker is
  gone, and lever (a)'s prerequisite ("sub-stage scopes on the burst first") is
  satisfied.
- **#2255 closed** (merged as PR #2272 — deterministic per-axis color winner +
  scatter tie band): rotated-pose byte-identity verification is now available,
  which any Phase-2 lever needs for its A/B evidence. The "defer while #2255 is
  open on the same surface" option is moot.

**Decision:**

1. **This PR merges as-is** — docs-only; the Phase-1 measurement is valid and
   the band-not-line-item caveat is honestly documented.
2. **Phase 2 continues on #2281** (issue stays open, as planned) in a follow-up
   PR, and it is **measurement-first**:
   - Re-run the attribution matrix (both scenes, cardinal vs `--yaw 0.35`, 3
     repeats) **with the #2288 sub-stage rows** so the `voxelStage1` bundle
     splits into compact / clear / stage-1 / stage-2.
   - Run the **#2266-method uniform body-reduction A/B on the per-axis AO
     kernel** to separate dispatch/fan-out cost from AO body cost.
   - That data picks the lever mechanically:
     - fan-out/dispatch-dominated → lever (a): collapse the 3 per-axis passes
       toward union coverage;
     - AO-body-dominated → lever (b): reduce per-axis AO body cost;
     - stage-1-raster-dominated → lever (c), now measurable per sub-row.
3. **No lever implementation before that split is recorded on the issue.** The
   [cost-model §3 rule 1](gpu-stage-timing-cost-model.md) attribute-before-re-
   architecting rule stands: a whole-tick / mostly-empty read has been refuted
   twice (#2266, #2271), and picking (a)/(b)/(c) from an overlapping ~65 ms band
   would be a third.

**Not deferring on the merits:** the delta is user-visible (~3.5× frame time at
zoom 3 during any rotation transient = dropped frames on every rotation), and
the measurement step Phase 2 needs is cheap now that the #2288 tooling merged.

## Phase 2 — separated per-axis burst attribution (#2281, 2026-07-20)

Instrumentation: `GpuSubStageScope` brackets on every rotating-only dispatch
group (the #2280 mechanism, extended). New registry rows: `voxelPerAxisStore`
(phase A: clears + cardinal stores ×3), `voxelPerAxisOverflow` (phases B+C:
view mask ×3 + overflow append ×3), `voxelPerAxisFinalize` (phase D: winner
election + stage-2 ×3), `perAxisCellCompact`, `computeVoxelAoPerAxis`,
`lightingPerAxis`, `lightingOverflow`, `perAxisScatter` — and
`COMPUTE_VOXEL_AO` / `LIGHTING_TO_TRIXEL` / `TRIXEL_TO_FRAMEBUFFER` are
untagged from the per-system observer with their rows narrowed to the
main-canvas dispatch only (the #2280 `voxelStage1` precedent). Cardinal
`IRShapeDebug` full shot table byte-identical pre/post (28/28, max_delta 0);
every per-axis row reads 0.0 at cardinal, so row deltas ARE the burst.

Matrix (macOS/Metal, M4 Max; `IRPerfGrid --auto-profile 300 --no-overlay`;
avg over 3 repeats; spread = max−min of per-run avgs, well under every quoted
delta):

### voxel_set `--zoom 3` — frame 14.92 → 28.16 ms (+13.24)

| stage | cardinal | yaw 0.35 | Δ |
|---|---|---|---|
| voxelStage1 + voxelStage2 (single-canvas raster) | 3.61 | 0.00 | −3.61 |
| voxelPerAxisStore | 0.00 | 6.91 | +6.91 |
| voxelPerAxisOverflow | 0.00 | 6.37 | +6.37 |
| voxelPerAxisFinalize | 0.00 | 5.60 | +5.60 |
| perAxisScatter | 0.00 | 2.19 | +2.19 |
| trixelToFb (gather, SDF/text fall-through) | 0.04 | 1.53 | +1.50 |
| perAxisCellCompact + AO + lighting + overflow relight | 0.00 | 0.20 | +0.20 |
| computeLightVolume | 7.56 | 4.83 | −2.73 |
| bakeSunShadowMap | 4.82 | 18.59 | +13.78 * |

### wave (default) — frame 8.60 → 20.36 ms (+11.77)

| stage | cardinal | yaw 0.35 | Δ |
|---|---|---|---|
| voxelStage1 + voxelStage2 | 1.32 | 0.00 | −1.32 |
| voxelPerAxisStore | 0.00 | 5.27 | +5.27 |
| voxelPerAxisOverflow | 0.00 | 3.27 | +3.27 |
| voxelPerAxisFinalize | 0.00 | 2.93 | +2.93 |
| perAxisScatter | 0.00 | 2.59 | +2.59 |
| trixelToFb (gather) | 0.04 | 1.93 | +1.88 |
| perAxisCellCompact + AO + lighting + overflow relight | 0.00 | 0.21 | +0.21 |
| bakeSunShadowMap | 1.50 | 11.22 | +9.72 * |

\* **The bake row's yaw swing is an overlap artifact, not real cost.** The
stage rows are genuine GPU-timeline windows and still overlap across systems
(voxel_set yaw rows sum ≈ 46 ms against a 28 ms frame). The decisive
counter-probe: `--no-sun-shadows` moves the yaw frame only 20.2 → 18.9 ms
(−1.3 ms) while moving cardinal 8.6 → 5.9 ms — the sun path's real marginal
cost at yaw is ~1.3 ms; the bake's whole-tick bracket absorbs concurrent
per-axis work. (perf_grid also registers no
`RESOLVE_PER_AXIS_SCREEN_DEPTH`, so per-axis voxels do not cast shadows
there at all — a demo-pipeline drift vs the five demos that do register it;
adding it would raise the yaw baseline and should ride its own change.)

### Findings

1. **The burst is the stage-1 rotating kernel family, full stop.** Store +
   overflow + finalize ≈ 18.9 ms (voxel_set) / 11.5 ms (wave) — ~85 % of the
   attributable delta. Per-axis AO and lighting are ~0.05 ms combined
   (occupied-cell-bound, exactly as the #2273 result predicted); the scatter
   draw is a bounded 2.2–2.6 ms.
2. **The same per-axis face geometry is dispatched five times per axis** —
   store (mode 0), view mask (mode 2), overflow append (mode 3), winner
   election (mode 1), stage-2 — 15 sweeps of the compacted face list per
   rotating frame, all through one runtime-`resolveMode`-branched kernel.
   The overflow lane's two extra phases (B+C) alone cost 6.4 / 3.3 ms — a
   third of the burst — matching the frame-level
   `IR_PERAXIS_OVERFLOW_DISABLE` kill-switch probe (~2.9 ms on wave).
3. **Candidate lever** (plan option (a), now with row-level evidence): reduce
   the pass count over the face list — fold the view-mask tap into the store
   pass (both write disjoint scratch; the mask needs only the yawed
   projection the store already computes), and/or compile-time-specialize
   `resolveMode` into per-pass kernel variants to remove the 4-way
   predication tax from the hottest kernel (the #2325 `IR_FEEDER_PASS` a′
   precedent measured a 16 % tax for ONE such uniform branch). Election +
   stage-2 fusion is constrained by the winner-scratch serial reuse across
   axes (#2255) — evaluate last.
4. **Pursue-at-all:** the delta exists only while the camera is actively
   rotating (canvases release at every cardinal), but it is user-visible on
   every rotation: voxel_set 69 → 37 FPS, wave 120 → 50 FPS. Given the
   standing yaw-parity goal, the lever is worth a design-gated follow-up.

Lever implementation is a **separate task** behind the design gate, per the
Phase-2 plan.

## Small dense scene (GL, #4722)

The two scenes above are large. This one is small: the cost of a rotating
frame is the lighting of per-axis and overflow faces, not raster fan-out.

**Fixture.** `creations/demos/perf_grid/configs/perf/small_dense_yaw.lua`: a
540×960 window and canvas at zoom 2, `voxel_set` with grid 24 (13,824 voxels),
spacing 1, the per-cell wave at amplitude 5, profiling and GPU stage timing
on. Rotating arm: `--yaw 0.0017453293 --yaw-step 0.0017453293` (one quadrant
over 900 frames). Cardinal arm: `--yaw 0`. Both arms:
`--auto-profile 900 --no-overlay`, through `fleet-run`, so every run holds the
quiet-window benchmark lease; every quoted run reported `RESULT=CLEAN`.

**Host.** Windows 10 native, MSYS2 mingw64 Debug build (`windows-debug`),
OpenGL on an NVIDIA GeForce RTX 3060 Ti (driver 32.0.15.8180). Master is
`d72f88820`. Frame figures are the report's steady average (first 225 frames
excluded); stage rows are the `--- GPU stage timing ---` averages over the
run. Arms were interleaved and alternated in order per round; spread is the
maximum minus the minimum of the per-run averages.

### Attribution (master, 6 runs per arm)

| row | rotating | spread | cardinal | spread |
|---|---:|---:|---:|---:|
| steady frame avg | 11.63 | 0.29 | 2.46 | 0.20 |
| `computeSunShadow` | 2.533 | 0.027 | 1.038 | 0.011 |
| `lightingOverflow` | 4.859 | 0.285 | — (0 samples) | |
| `voxelPerAxisFinalize` | 0.714 | 0.019 | — (0 samples) | |
| `voxelPerAxisOverflow` | 0.576 | 0.023 | — (0 samples) | |
| `voxelPerAxisStore` | 0.349 | 0.003 | — (0 samples) | |
| `buildLightOcclusionGrid` | 0.192 | 0.002 | 0.165 | 0.002 |
| `bakeSunShadowMap` | 0.046 | 0.001 | 0.035 | 0.001 |

Cull stats on both arms: visible 13,824 of 13,824; axis entries 41,472
rotating, 0 cardinal. A separate `IR_OVERFLOW_COUNT_LOG=1` rotating run (its
timings not used) logged 344 frames of overflow entries: 34,635 to 37,680,
median 35,907, about 3.7 times the motivating scene's 10.2k. The two sun-query
rows hold 7.39 ms of the 11.63 ms rotating frame; the cardinal frame is not
GPU-bound (removing 0.9 ms of `computeSunShadow` moves it by 0.27 ms).

### Disarm probes (master, rotating arm, one run each)

Staged probes edit `ir_sun_shadow_sample.glsl` in the run's own staged
`shaders/` directory and restore it from the pristine copy afterwards (md5
`938c26eb…` before and after).

| probe | steady frame avg | `computeSunShadow` | `lightingOverflow` |
|---|---:|---:|---:|
| master (6-run mean) | 11.63 | 2.533 | 4.859 |
| `--no-sun-shadows` | 3.71 | 0.036 | 0.015 |
| staged `hasSourceFaces = false` | 3.44 | 0.031 | 0.016 |
| staged `queryHierarchy = false` | 3.68 | 0.193 | 0.172 |
| `IR_OVERFLOW_LIGHTING_DISABLE=1` | 5.97 | 2.358 | — |
| cardinal, staged `queryHierarchy = false` | 2.19 | 0.140 | — |

Without the hierarchy query the two rows sum to 0.365 ms, 4.9% of master's
7.39 ms: the crowded-tile hierarchy recovery is about 95% of both rows and
86% of the cardinal `computeSunShadow`. Leaves held eight records in
reservation order, which follows bake dispatch and atomics, so every leaf's
bounds spanned the scene and a receiver with no blocker tested every record.

### Lever: spatial leaf order

The hierarchy now orders its leaves by Morton cell (16×16×16 cells of the
record extent in sun-U, sun-V and sun-depth) with a GPU counting sort: an
unordered build for the extent, then count, scan and scatter passes, then the
queryable build ([leaf order](sun-face-index-capacity.md#leaf-order)). Six
interleaved base/head pairs per arm:

| row | rotating base | rotating head | paired Δ (ub95) | cardinal base | cardinal head | paired Δ (lb95..ub95) |
|---|---:|---:|---:|---:|---:|---:|
| steady frame avg | 11.63 | 4.82 | −6.81 (−6.70) | 2.457 | 2.395 | −0.062 (−0.206..+0.083) |
| frame avg | 10.92 | 4.87 | −6.05 (−5.90) | 2.628 | 2.603 | −0.025 (−0.154..+0.104) |
| `computeSunShadow` | 2.533 | 0.456 | −2.078 | 1.038 | 0.264 | −0.774 |
| `lightingOverflow` | 4.859 | 0.846 | −4.013 | — | — | |
| `bakeSunShadowMap` | 0.046 | 0.110 | +0.065 | 0.035 | 0.105 | +0.070 |
| `voxelSunFaces` | 0.084 | 0.083 | −0.000 | 0.090 | 0.090 | −0.000 |
| `buildLightOcclusionGrid` | 0.192 | 0.183 | −0.010 | 0.165 | 0.164 | −0.001 |

The rotating frame drops 59%: the two sun-query rows fall from 7.39 to
1.30 ms, against 0.065 ms of extra build. The cardinal frame's head mean is
0.975 of base. The bake system's CPU tick rises by about 0.02 ms (three more
dispatches). A first version sorted full 30-bit Morton keys through the
per-axis overflow sorter: its rows fell slightly further (0.437 and 0.785 ms)
but `bakeSunShadowMap` rose 0.35 ms, and its fourteen extra dispatches added
0.11 ms of CPU per frame, enough to put the CPU-bound cardinal frame at 1.037
of base.

**Exactness.** Leaf membership changes cost, never answers. The live wave is
not byte-identical run to run, so the capture fixture is static. With
`--wave-freeze`, master with `queryHierarchy = false` captured the same bytes
as master at yaw 0.3 and 0.6, so that fixture never reaches the hierarchy in
the captured frame. The fixture used is
`configs/perf/small_dense_lattice.lua` (the same scene on a spacing-2 lattice,
wave off), captured at frame 60 (`--capture-frame 60 --exit-after-frames 75
--no-overlay`) at yaw 0.3, 0.6, 0.9, 1.2 and 0. Two master runs matched byte for byte at
every pose; the `queryHierarchy = false` ablation changed yaw 0.9
(`render-compare --per-pixel-tol 0`: max delta 51); the head matched master at
every pose (max delta 0). The frozen-wave captures also matched master on the
head.

**Standard matrix.** `scripts/perf/perf_grid_matrix.sh` (12 cells, the
262,144-voxel grid, yaw 0) against master: no cell's frame average regressed
past the comparator's 10% gate (−37.1% to +4.4%, eight cells beyond −5%), and
`compare_perf_runs.py` exits 0. On that grid the pool grows to its 131,072-record
ceiling; `computeSunShadow` falls from 5.2 to 0.65 ms at zoom 1 and from 20.4 to
1.65 ms at zoom 8, and `bakeSunShadowMap` rises 0.12 ms. `buildLightOcclusionGrid`,
which runs before the bake, also read about 0.2 ms higher in nine of twelve
head cells than in either master run; its cause is unattributed.

### Metal planning reading (M4 Max, vsync-quantized)

Planner probe on master `f9e76bdfa`, before the record-pool growth. Metal
frames are quantized at 8.33 ms and its stage rows overlap (cost-model §1), so
these rank movers and do not give GL magnitudes.

| arm | steady frame avg | `computeSunShadow` | `lightingOverflow` |
|---|---:|---:|---:|
| rotating, master (two runs) | 20.08 / 19.06 | 2.67 / 2.66 | 5.46 / 5.50 |
| `--no-sun-shadows` | 8.33 | 0.05 | 0.66 |
| staged `hasSourceFaces = false` | 12.67 | 0.08 | 0.99 |
| staged `queryHierarchy = false` | 16.23 | 0.41 | 1.32 |
| `IR_OVERFLOW_LIGHTING_DISABLE=1` | 15.58 | 3.13 | — |
| cardinal (`--yaw 0`) | 8.35 | 1.66 | — |
