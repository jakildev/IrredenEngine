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
