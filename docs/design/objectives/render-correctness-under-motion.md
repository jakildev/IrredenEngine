# Objective: rendering stays correct and temporally stable under motion

**Status:** active

## Outcome
Camera motion (yaw / zoom / pan) and entity rotation produce artifact-free,
temporally stable output, and every stability claim is machine-gated — a
regression in crawl, cull, or rotation correctness fails a harness, not an
eyeball.

## Done means
- [ ] All 8 SDF shapes pass the jitter gate (`MAX_JITTER=4.0`) in
  `shape-rotate-jitter-sweep` on `origin/master` — today 2/8 fail on the SDF
  path: the curved-SDF depth-band family (curved_panel 14.25, ellipsoid 7.90 —
  #1920).
- [ ] The voxel-path per-shape jitter table is gated (captured 2026-10-27 in
  `docs/design/jitter-validation-harness.md`, all 8 passing; the gate waits on
  the table being stable).
- [ ] An OpenGL/Linux jitter baseline exists and the gate runs on both
  backends, not only Metal/macOS.
- [ ] Picking works during rotation — the per-axis scatter path writes the
  hovered-id SSBO (`docs/design/per-axis-trixel-canvas-rotation.md`).
- [x] Per-axis rotating voxels cast into the shared sun map again without
  the self-occlusion artifact (#1435); scatter-quad conservative-coverage
  waffle resolved (#1494).
- [x] Cull-freeze validation stays green: yaw-time culling never drops
  on-screen content (`IRShapeDebug --cull-validate`) — macOS/Metal on
  2026-10-27: two consecutive 12/12 PASS runs, with all five state-guard arms
  PASS.

## Non-goals
Frame-rate / perf targets (tracked by the perf gate, not this objective);
LOD strategy; new projection modes.

## Current state
The measurement culture is in place: `docs/design/jitter-validation-harness.md`
with `scripts/dev/shape-rotate-jitter-sweep` and
`scripts/render-jitter-metric.py` pins temporal second-difference under a
1°/step 360° sweep (the metric is unit-tested in CI); `scripts/cull-verify.py`
proves yaw culling against a frozen wide viewport and asserts the freeze
itself; `render-verify` / `light-verify` gate stills. Measured on master
`ee85797c0` (macOS / Metal, 2026-10-27):

- Jitter, rows 1 and 2: the sweep as committed had stopped tracking its
  subject. The engine's default yaw pivot is the camera centre, so the
  origin-placed shape orbited out of the centred crop and the metric's
  interior collapsed; #4781 / PR #4785 pins `--pivot-origin` on every run and
  re-measures. The pivot-pinned table
  (`docs/design/jitter-validation-harness.md` § Baseline) passes 14 of 16
  SDF + voxel runs. The SDF path reads box 0.69, sphere 0.42, cylinder 0.31,
  cone 0.11, torus 0.23 and wedge 3.73 PASS, ellipsoid 7.90 and curved_panel
  14.25 FAIL (gate 4.0): the hard-edge seam family is fixed since the June
  baseline and the curved-SDF depth-band family is not; #1920 and #1973 were
  closed not-planned, so that family has no owner. The voxel path passes all
  eight (box 0.69, sphere 3.79, cylinder 2.56, ellipsoid 2.34, cone 1.97,
  torus 3.05, wedge 0.83, curved_panel 0.64), three of them within 1.5 of the
  gate with a third to two thirds of their surface flickering; nothing gates
  that table yet. Rows 1 and 2 stay open.
- Cull, row 6: `cull-verify` was red on `ee85797c0` — 12 of 12 live poses
  differed from their frozen-cull twins (match 74.6–99.7%) while the
  freeze-guard arms passed (#4780). PR #4784 restored pose parity the same
  day: two consecutive 12/12 PASS runs with all five state-guard arms PASS,
  so row 6 verifies.
- canvas_stress: `canvas-stress-rotate-jitter-sweep` passes at 1.44 (the June
  baseline read 1.19).

Rows 3 (a GL baseline and a CI gate) and 4 (the hovered-id write in the
per-axis scatter, `per-axis-trixel-canvas-rotation.md`) have no tree evidence
and no open issue. Row 5 verifies: the per-axis sun-shadow resolve landed
with PR #1453 and the conservative-coverage waffle fix with PR #1523, whose
dilation mechanism PR #3980's exact finite-face scatter later superseded.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-06-02 | #1435 / PR #1453 | per-axis sun-shadow resolve under camera Z-yaw |
| 2026-06-05 | #1494 / PR #1523 | conservative-coverage waffle fix; its dilation mechanism was superseded by PR #3980's exact finite-face scatter on 2026-10-01 |
| 2026-06-21 | #1883 / PRs #1907, #1917; #1922 / PR #1928 | seam crawl fixed and corner spikes routed to epic #1933; the jitter harness and its 4/8 SDF baseline |
| 2026-06-23 | #1920; #1954 / PR #1972 | curved-SDF shimmer closed not-planned (routed to #1973, also closed not-planned on 2026-06-29); canvas_stress baseline 1.19 |
| 2026-07-20 | — | objective seeded |
| 2026-09-13 | PRs #3210, #3349 | cull-verify relative-only contract and freeze guard; last green native run (12/12) before the 2026-10-27 red |
| 2026-10-27 | sweep; #4780, #4781 | re-verified against master `ee85797c0`; jitter re-measured with the pivot pinned (SDF 6/8, voxel 8/8), cull-verify red, canvas_stress 1.44; row 5 ticked; the harness pivot defect and the cull red filed |
| 2026-10-27 | #4780 / PR #4784 | cull-validation pose parity restored; two consecutive 12/12 PASS runs. Row 6 verifies. |
| 2026-10-27 | #4781 / PR #4785 | `shape-rotate-jitter-sweep` pins `--pivot-origin`; the harness-doc baseline table is re-measured on both render paths (14 of 16 pass) |
