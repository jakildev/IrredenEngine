# Campaign: million-entity-render

Objective: [`objectives/million-entity-render.md`](../objectives/million-entity-render.md).
Protocol: [`docs/agents/campaign-protocol.md`](../../agents/campaign-protocol.md).
Seeded 2026-09-18 from the Codex session worklists
([`rendering-audit-todo.md`](../rendering-audit-todo.md),
[`docs/perf/rotation-subdivision-audit.md`](../../perf/rotation-subdivision-audit.md),
[`docs/perf/world-scale-visibility.md`](../../perf/world-scale-visibility.md));
those files stay the canonical detail. This file orders the directions,
records decisions and carries the slice in flight.

## Spirit

- Voxels are reconstructed as trixel faces on 2D canvases; rotation is a
  distortion of those canvases. No meshes, no sub-trixel geometry, and the
  distance/color/id trixel texture contract stays so lighting, shadow and
  AO stages keep working unchanged.
- Clean projected edges come from geometry. Blur, bias, inflated coverage
  or averaged normals are never a fix; raw rectangular trixel data is a
  debug view only, and every voxel path presents through the fragment
  reconstruction with one shared triangle-orientation convention.
- Lighting is geometric: what a face receives depends on what occludes it,
  itself or other objects, not on which iso face it is. AO is local ambient
  visibility; sun visibility and Lambert are separate terms.
- Cost tracks useful screen coverage. Rotation and zoom parity are targets;
  a fixed framebuffer does not bound intermediate work by itself, so reject
  work before generating it.
- Visibility is an unbounded orthographic prism; chunks are rejected by
  projected bounds under yaw; simulation cadence, shadow relevance and
  residency are separate decisions from render visibility.
- No claim without a control: full-frame GPU accounting, fingerprints,
  repeated runs, and a pixel-identity capture set per optimization PR. A
  small native scene never proves fleet-scale throughput.
- Both backends: GLSL and Metal twins land together; an OpenGL runtime gap
  is stated, not assumed away.

## Directions

Ordered; earlier directions gate later claims. A direction is done when
its objective Done-means rows verify on `origin/master`.

| # | Direction | Draws from | Exit |
|---|---|---|---|
| D0.0 | The CanvasStress revoxelized cubes (index 1 cyan `{70,210,210}`, index 2 purple `{210,120,255}`) and the plain-detached orbit frame (index 7 `{150,90,235}`) show tooth and half-face patterns in lit captures. Decided: `render-revox-face-metric.py` projects each fixture's own inverse-resampled cells and matches every capture pixel-for-pixel at five yaws, so the display owns no wrong face and the look is the nearest-cell resample plus lighting on its concave steps; presentation stays the per-object `RotationMode` choice ([`revoxelized-display-fidelity.md`](../revoxelized-display-fidelity.md)) | `trixel-face-reconstruction-validation.md` § Deterministic gates, `revoxelized-display-fidelity.md`, `c_revoxelize_detached.glsl` | done: the revox gate passes both cubes at five yaws (zero missing, extra or wrong-face pixels) and the frame passes the source-face silhouette gate at five yaws; the lit staircase response moves to D0's revoxelized self-shadow/AO item |
| D0 | Close the remaining visual gates: mixed private SDF/voxel canvas density, placement, owner rotation and atomic-depth lifecycle (done for canvases whose rendered cell offset is zero: [`mixed-private-canvas-lifecycle.md`](../mixed-private-canvas-lifecycle.md); the revoxelized half-cell phase in the shape pass is open); SDF BOX display extent, and the SDF marker's raw-texel display and continuous-yaw dilation in a private canvas (the strict footprint of `render-mixed-canvas-metric.py`) versus receiver and analytic caster extent across subdivisions and yaw; SDF surface face ownership audit; revoxelized self-shadow/AO patches without flattening real staircase normals; deterministic face-connectivity and triangle-parity tooling; the fog cut-face column's view→world compensation for world-placed detached casters (#3534; the receive and cast-scatter halves landed in #3366) | `voxel-and-sdf-rendering.md` § Current acceptance work 2–5, `rendering-audit-todo.md` | new gates in `render-verify` / `scripts/render-*-metric.py`; nine-yaw CanvasStress and ShapeDebug probes pass |
| D1 | Honest million controls: committed `million` preset and `repeat_profile` recipe; Release and profiling-off arms; continuous yaw; independent entity motion; matched projected extent; tail latency and memory; a `--yaw` perf-matrix axis (#3130) | `world-scale-visibility.md` step 1, `rotation-subdivision-audit.md` 2, 7, 8 | one table under `docs/perf/` every later PR diffs against |
| D2 | Bound intermediate work: overflow sort at scale preserving `(cell, distance, color)` order (#3129; #3467, now only the `voxel_pool_edge` upper clamp — the 2-D fill dispatch landed in #3465); live-count clears and dispatches; per-axis storage and finalization dedup; scratch-byte and occupied-cell counters; paged versus larger pools decided from measured residency | `rotation-subdivision-audit.md` 1, 4, 8; `gpu-cost-attribution.md` next experiments | GPU frame time at the million control drops with pixel-identical captures |
| D3 | Hierarchical visibility: visited/admitted chunk counters; bound inflation of allocation-slot groups measured; spatial regrouping or a region index over occupied space; conservative light-directed caster region; unbounded depth and full-turn tests stay green | `world-scale-visibility.md` steps 2–3, `rotation-subdivision-audit.md` 3 | admitted candidates track projected coverage; the depth-cutoff suite passes |
| D4 | Lighting bounds: light-volume active tiles that keep off-screen lights and blockers; finite caster reach; AO cost by region; quality options only after dominant costs are known | `rotation-subdivision-audit.md` 5, `light-volume-candidate-pruning.md` | lighting cost bounded independently of entity count, lighting-demo captures identical |
| D5 | Simulation cadence: fixed-step catch-up amplification; staggered updates; presentation interpolation; `UpdateVoxelSetChildren` and transform propagation cost; bounded promotion under camera pans | `world-scale-visibility.md` step 6, `rotation-subdivision-audit.md` 9 | ≤ 1 update per rendered frame at the million control with correct motion |
| D6 | Cross-backend and gate: OpenGL runs of D0 gates and the million control; GL frame-query ring; the CI perf gate gating (#3471; #2817 closed as shipped by #2997); Release baselines committed | `rotation-subdivision-audit.md` 7, objectives `backend-parity`, `machine-verified-behavior` | both backends measured, gate red on regression |
| D7 | Structural decision: LOD or voxel mip at zoom-out and the scatter-versus-gather call, made from D1–D4 data by the decision rule of #1808 (closed 2026-09-19, superseded by this campaign) | #1808 D3, `lod-strategy.md` | a design doc with numbers, then its own campaign or epic |

D1 slices touch only tooling and docs, so they may interleave with D0.

## Working agreement

- One slice, one PR, one question. Measurement before mechanism.
- Fixtures: `IRPerfGrid --mode voxel_set --grid-size 100 --wave-freeze --zoom 4`
  with the pool-128 runtime config; the nine-yaw `IRCanvasStress` capture
  set; `IRShapeDebug` probes; the `lighting` demo for propagation changes.
- Every render PR: `attach-screenshots` stills, full frames plus crops, and
  the RGB-identity comparison of the nine CanvasStress poses. Every perf
  PR: `repeat_profile.py` tables with fingerprints, full-frame GPU
  accounting, and the direction it advances named in the body.
- Checkpoint every four to six open PRs; approve only after a fresh-context
  reviewer pass with findings addressed.
- A gate that can pass on a blank capture, an unlit frame or a reworded log
  line is not a gate: every new metric or counter ships with a positive
  control that fails when the thing it measures is absent.
- Before picking a slice, read `fleet-campaign-status`'s other-lanes section
  and stay out of files an open stack owns; the split in force is recorded
  under Decisions taken.
- Follow-ups outside the current direction are filed with
  `**Objective:** million-entity-render` and listed below.

## Contributor lanes

The interactive Codex session in `codex-shadow-render` participates via
`fleet:campaign-million-entity-render`; membership does not transfer ownership.
Its canonical detail stays in [rendering-audit-todo.md](../rendering-audit-todo.md).

- **Open work:** #3632 (continuous source-face sunlight) and #3635 (cascade
  receiver coordinates), both claimed. #3632 needs merge-conflict resolution
  and investigation of the Linux perf run that produced no profile report.
  #3635 is not approved: the strict floor edge at 180° worsens from 12/266 to
  19/356 missing/excess pixels. Neither is accepted as a floor-edge fix.
- **Next owned scope:** exact SDF receiver geometry and finite projected
  boundaries through presentation. Smoothness or steps must follow the selected
  caster and receiver geometry, without blur, inflated footprints or compensating
  bias. Preserve the strict gates and validate mixed render-mode pairs.
- **Coordination:** the driver is progressing its profiling stack; consult its
  latest open campaign PR for its current `## Now` before editing shared files.
  #3644 supplies the every-iteration reconciliation workflow; explicit membership
  extends that workflow to interactive contributors, rather than replacing it.

## Ledger

| Date | Entry |
|---|---|
| 2026-09-14 | Stack merged: shadow reception, finite face casting, receiver alignment, rotation/subdivision audit (#3386–#3439) |
| 2026-09-15 | Stack merged: update spans, CPU stacks, GPU-write preservation, overflow dedup, depth ties (#3442–#3449) |
| 2026-09-17 | Stack merged: light-volume pruning, yaw visibility tests, million capacity, overflow demand sizing, live-count lighting and sort, GPU frame accounting, source-face presentation (#3450–#3477); geometric AO (#3484) |
| 2026-09-18 | Campaign seeded; the Codex session's in-flight item (mixed private canvas lifecycle) becomes D0's first slice |
| 2026-09-18 | Human screenshots of the two purple cubes and the cyan cube added as D0.0 and moved ahead of the private-canvas lifecycle. First read of the zoom-8, AO-off, shadow-off captures at yaw 45°: the purple cube (index 2) is a clean column staircase; the cyan cube (index 1) shows lone single-trixel treads inside a field of one side face. A step cell at (x+1, y, z+1) sits two depth units nearer and its hexagon covers exactly the right half of the lower cell's top rhombus, so a lone tread trixel beside a riser is the correct projection of a diagonal step; the revoxelize kernel is nearest-cell inverse sampling (`revoxSourceCellForDest`), so a rotated solid is a staircase by construction. Hypothesis to prove with the oracle: the display is faithful and the objectionable look is the resampling plus AO/shadow on every concave step |
| 2026-09-18 | Located, not fixed: `system_shapes_to_trixel.hpp` `endTick` clears a non-main canvas unconditionally and re-anchors it to the first shape's world position before drawing, so a voxel set sharing that private canvas loses its color, depth and ids and the two producers disagree on placement (D0 lifecycle item) |
| 2026-09-18 | D0.0 closed as faithful display: `scripts/render-revox-face-metric.py` (resampled-cell oracle, float32 quaternion path, painter-ordered parallelograms, one-pixel band) matches the cyan and purple cubes at 0/22.5/45/67.5/90° with expected pixel count equal to observed in every capture (1716–1734 occupied cells of 1728 authored); the upright control passes, and the wrong-fixture, wrong-yaw, identity-model and `--debug-raw-trixels` controls fail with 10⁵-class wrong-face counts. The orbit frame passes `render-source-face-metric.py --shape frame` at the same five yaws. Evidence: `docs/design/revoxelized-display-fidelity.md`, captures under `docs/pr-screenshots/claude/million-entity-render-face-parity/`. `--focus-revox <index>` added to IRCanvasStress to center one proof solid |
| 2026-09-18 | D0.1 closed: `SHAPES_TO_TRIXEL` rasters a shape on an entity canvas in the owner's model frame (owner-relative offset, no camera term, the canvas's rendered density, continuous yaw off the cardinals) and keeps a voxel-rastered canvas instead of clearing it; `IRSystem::clearCanvasAndDistances` moved to `canvas_clear.hpp` so a shape-only canvas resets through the voxel pass's sentinel + Metal scratch mirror. Gate `scripts/render-mixed-canvas-metric.py` (frame exact outside a two-texel marker guard, marker centroid within one texel, `--control` raster survival, `--strict` footprint): before, the second marker sat eight texels off at yaw 0 and both markers left the framebuffer at yaw 45; after, five yaws pass with centroids within a third of a texel, and the revoxelized-sphere control shows 0 differing pixels against the marker-free scene where the unconditional-clear experiment shows 235,823. The SDF marker's own display (raw 2x3 texel rectangle, dilation to 2.1× area under continuous yaw) is recorded as the open strict footprint for the SDF display item |
| 2026-09-19 | Checkpoint 1: #3519, #3530, #3542 reviewed by fresh-context reviewers plus the render and ECS invariant audits on the engine diff. Findings applied on each branch: the wrong-yaw control names its yaw and the clipped path is pinned (D0.0); the mixed-canvas contract is scoped to zero-phase canvases, the ownerless branch resets through the shared clear, the smooth-yaw comments name the gate, the modifier demo includes the helper directly (D0.1); the dead DDA nudge is gone, the overlay's magenta count is reported, the near-riser band is attributed to the shadow path rather than the receiver, and the per-face noise floor is stated (D0.2). Stack rebased onto master via `gh stack sync`; master's own instruction-size lint was red (three docs over budget) and the two non-gated ones are trimmed in #3543. Merge order bottom-up: #3519 → #3530 → #3542 |
| 2026-09-18 | D0.2 measured: `render-revox-face-metric.py --shadow-overlay` casts a ray from every sun-facing resampled face toward the sun through the destination lattice and compares with the shadow overlay. Authored-cell casting (the demo default) marks 75k–147k false self-shadow pixels on the cyan cube at every yaw; resampled-cell casting marks 0–9k, with the near-riser tread band (24,696 px at yaw 0) left lit by the shadow path (caster or receiver, traced in D0.3). AO darkens 6.9% of the cube by at most 5.9%. The lit tooth pattern is the caster/receiver geometry mismatch, not the staircase; evidence in `revoxelized-display-fidelity.md` § Direct sun |
| 2026-09-19 | D0.3 closed: `BAKE_SUN_SHADOW_MAP::bakeVoxelFaces` casts every voxel canvas from the cells it rasterizes; the authored-grid caster mode (`useSource`, the 96-byte frame's source-grid fields, binding 9 in the GLSL and Metal kernels, `sourceFaceCoverage_`) is gone and `--source-face-shadows` / `--voxel-face-shadows` are accepted and ignored (byte-identical captures). The sun oracle casts from each displayed triangle's centroid, the point the lighting pass samples, and classifies false shadow by the ray's closest approach to an occupied cell: with that, the near-riser tread band is the per-face floor (missed shadow 0 at all five yaws), and every remaining false-shadow trixel grazes an occupied cell within a third of a cell while 370 trixels with clearance ≥ 0.35 never flip, the signature of the nearest-texel read at a terminator. Caster and surface receiver traced with no defect. The authored-caster captures fail the same gate (clear-ray false shadow at 0.55–0.67 cells). Floor shadow at zoom 4 is a clean stepped silhouette. `render-verify --target IRCanvasStress` already fails 9 of 11 on the pre-change tree (references from 2026-08-03 predate the September stacks), so the re-bless is filed as #3552 instead of bundled |
| 2026-09-19 | D0.4 closed: a shape on a revoxelized entity canvas is a lattice occupant. `SHAPES_TO_TRIXEL` drops the canvas's half-cell phase (`renderedCellOffset_`, rotated into the world frame) from each shape's owner offset before the raster's per-axis rounding, so the shape lands on the same integer + phase lattice the voxels display on and the composite's phase places it; a shape centred on a cell is pixel-exact, one between cells shows at the nearest cell. Fixtures: `--focus-revox 3` (12x12x11 mixed-parity box, `--parity-extent` for the one-even-axis variant), `--mixed-shape-at`, `--focus-offset`; the mixed-canvas metric gains `--fixture/--markers/--owner` with the voxel expectation from the new `render_revox_lattice.py` (hoisted from the revox oracle, per-axis extents) and each marker expected at its nearest lattice cell. At the cardinals the parity box passes the strict footprint on-lattice, a quarter cell above a cell, a quarter cell below one (before: rounded a whole cell away, two iso rows, 7,680 wrong-owner px), with a translated owner, and as the one-even-axis box; the zero-phase orbit frame is byte-identical to D0.1. Two mechanisms were measured and rejected: shifting the raster by the phase's iso projection plus a depth bias (exact centroid, but an odd texel row flips the local-triangle parity and every hexagon became a bow tie, 1,696 px at yaw 0), and no phase at all (right for half the off-lattice positions by the round-half-up tie rule, a whole cell off for the other half). Off the cardinals the shape pass rounds the iso projection and paints analytical 2x3 diamonds at both parities (area 1.67), which is D0.5 |
| 2026-09-19 | D0.5 closed: a density-1 shape on an entity canvas is a lattice occupant at every yaw. The shape frame data's pad word becomes `latticeShapes`; under smooth camera yaw the kernels (GLSL + Metal) take `snapLatticeWalkYawed`, the integer lattice walk with the SDF query rotated by the continuous yaw, anchored on the snapped view cell (the CPU tile builder anchors the same way), with the cardinal-style cell depth and the plain 2x3 emit; the analytical smooth path, which sampled every iso pixel of both parities and painted a 2x3 diamond per hit (area 1.67), is left to the main canvas and to subdivided shapes. Strict footprint on the parity box at 22.5/45/67.5 (asymmetric marker; the symmetric one at exactly 45 is a float32/float64 rounding tie of the fixture), with a translated owner and on the one-even-axis box. The orbit frame's marker drops to one cell (area 1.00) but a source-face canvas composites raw texels, the remaining SDF display item |
| 2026-09-20 | Stack merged: D0.0–D0.5 (#3519, #3530, #3542, #3554, #3556, #3559) reached master with the other lane's sanity review stacked on top (#3560, [`render-stack-sanity-review.md`](../render-stack-sanity-review.md)). That review corrected three things in this campaign's work, and the rows above are read with them: the D0.5 yawed lattice walk discarded `SHAPE_FLAG_HOLLOW` (fixed in both kernels by #3560; a five-cell hollow box is byte-identical before and after, so the fixtures here could not have shown it); the D0.2/D0.3 sun oracle accepted a blank capture at yaw 22.5 because no interior was expected in shadow, so that pose's "missed shadow 0" was vacuous until #3560 required an occluded interior as a positive control; and the mixed-canvas docs claimed unit-marker and all-yaw coverage beyond the measured unit box. Its five visual follow-ups (plain detached SDF face reconstruction, symmetric half-cell ties, hollow coverage on the cardinal walk, a terminator tolerance derived from the sun-map footprint, reference refresh after geometry checks) are D0's open list |
| 2026-09-20 | Other lane in flight: #3561–#3568 (partial-face ray explanation, shadow plane-sample agreement, floor shadow edge metric, caster/receiver mode matrix, rigid source-face rotation, casting and reception). It owns `creations/demos/canvas_stress/main.cpp`, the sun-shadow kernels and baker, `c_lighting_to_trixel`, the `render-*-metric.py` oracles and the audit worklist docs while open. Checked against this campaign's landed decisions: #3567 casts source faces only for plain rigid `DETACHED` objects and keeps resampled occupancy for revoxelized casters (the D0.3 decision), and #3562 narrows the terminator residual the sun oracle tolerates. #3562 and #3568 state population-scale cost as unmeasured; the D1.1 table is the control for it |
| 2026-09-20 | D1.0 closed: `IRPerfGrid --yaw` is radians, as its help text, every recipe, 70 committed arms in 17 evidence sets and `rotation_controls.py` assume. It had been handed to `IRRender::setCameraVisualYaw`, which takes degrees, so `--yaw 0.785398163` was a 0.785° pose and every IRPerfGrid row labelled 45° under `docs/perf/` (and the objective's 2.06× rotation-parity baseline) was measured there. Control: before the fix `--yaw 90` runs the cardinal gather path and `--yaw 1.5707963` the per-axis scatter path; after it they swap, and the run logs its effective yaw in degrees. Before/after differences inside each document stand, since both arms sat at the same pose; statements about 45° itself do not. At 64³ the two poses happen to read close (20.16 vs 19.45 ms on one binary), but frame time varies with pose (58.3° reads 14.90 ms and 116.6° 13.11 ms in the same session, with different visible counts and no matched-extent arm, so this is not evidence of a yaw-dependent scatter cost), and one rotated pose does not characterise rotation. The objective's 1.33× zoom-parity baseline and three `docs/design/` profiles were taken the same way. `IRCanvasStress`, `IRShapeDebug` and `ir_voxel_yaw` always took radians, so D0's captures are unaffected. Evidence: [`docs/perf/perf-grid-yaw-unit.md`](../../perf/perf-grid-yaw-unit.md). Found while measuring D1.1, whose first, grouped arms also showed host drift larger than the arm differences, a `-O3` Debug tree and a host on battery; those are D1.1's to record with the arms that show them |
| 2026-09-20 | D1.1 closed: `configs/perf/million.lua` and `million-profiling-off.lua` carry the scene and both profiling keys (`config.voxel_pool_edge = 128` through the pre-init pass, which reads the preset after `config.lua` and opens it by the same path `World` and the creation do); `*-release` configure presets build into `build-release/`; `million_controls.py` runs build tree × stage profiling × pose interleaved, prints per-round means and its own conditions, verifies a round before summarising it, and refuses a moved binary, shader set, script set or power source, a case that is not the million scene on the arm its name says, and builds that cull one pose to different counts; `repeat_profile.py` records the tree, `CMAKE_BUILD_TYPE` and `host_power` and reports p95, p99 and updates per frame. Reference ([`million-controls.md`](../../perf/million-controls.md), AC power, head `058b495b3` on master `35b7defc8`, 24 clean runs): Release with stage profiling off reads **34.30 ms at 0° and 44.60 ms at a true 45°**, GPU frame 22.4 and 30.4 ms, 2.1 and 2.7 fixed updates per rendered frame; rotated to cardinal is 1.30×; Release is 0.7 ms under Debug; round spread is 0.3–3.4 ms. At 45° the largest sampled GPU stages are `computeLightVolume` 22.5, `computeVoxelAoPerAxis` 20.4 and `lightingOverflow` 10.8 ms. Conditions found on the way: grouped arms drift by more than the differences they test; the Debug tree gives `-O3` to first-party translation units only (284 of 568), so Release is not just Debug without asserts; `IR_RELEASE` compiles out every log macro, so a Release run cannot witness its pose or an overflow drop (both now read unverified, never 0); p99 over a 300-frame window is a startup statistic; and on a draining battery the same 0° scene read 33.7 then 52.9 ms, which the AC reference (34.7 ms for that arm) fits and does not prove |
| 2026-09-20 | Resync at wrap-up, 31 commits after the session's base: the other lane's whole shadow stack merged (#3561, #3562, #3564–#3568, #3572, #3589), so the lane split below is spent and D0 and D4 are open to the campaign again, from `render-stack-sanity-review.md`'s five follow-ups and a fresh read of `rendering-audit-todo.md`. The human's triage (#3570) refined D0, D2, D6 and D7 in this file and parked #3130 and #1923 `human:owned` for the campaign. Closed by others: #3552 (references refreshed in #3588), #3600 (`getTable`, fixed by #3603; #3581 calls that guarded site twice and needed no change), #3475 (#3595), and both CI perf-gate issues the objective names, #2817 and #3471 (#3597), which leaves D6's gate item to verify rather than build. #3584 clamped `voxel_pool_edge` in the function #3581 edits; both changes are kept. #3577 and #3581 were rebased onto `35b7defc8` with the rebase guard (199 and 861 added lines before and after, none dropped), and the million matrix was restarted on that base because the merged sampler changes execute inside the fixture. None of this was surfaced by the protocol: the first reconciliation came from the human mid-turn and #3600 from an accidental search hit, which is #3618 |
| 2026-09-20 | Checkpoint 2: #3577 and #3581 each reviewed by a fresh-context reviewer (read-only, asked for every guard what input makes it pass that should not); neither diff touches render or ECS code. #3577: needs-fix with no blockers; every should-fix applied (three more affected design docs and the objective's 1.33× baseline named, the flag's history stated correctly, the unsupported yaw-dependent-cost claim withdrawn, README exception, the gate no longer passes NaN or crashes on a malformed value, pose lines committed); approved. #3581: one blocker (the head predated the `getTable` fix, so the new pre-init read aborted on any preset without a `config` table; gone with the rebase and re-verified with a `perf_grid`-only and a missing preset) and eleven should-fix. Applied: the bare-filename path disagreement between the three preset readers, explicit profiling keys, verify-before-summarise, the per-case scene, arm and build assertions, the conditions header, the `run_rounds` test, the recipe's configure line, and every overstated claim in the docs. Left as its worklist, recorded in the PR body: a C++ test for the pre-init pass, the preset directory outside the fingerprints, pose and drop count in the profile report so Release can witness them, the drop count as a maximum instead of a line count, timestamps and battery level in manifests, and the 0.9 GB profiler dump the Debug profiling-on arm writes at exit. Merge order bottom-up: #3577 → #3581 |
| 2026-09-21 | Resync at startup: `fleet-campaign-status` read `SUPERSEDED` (#3577 and #3581 merged) and `--apply` parked the branch on master. Other lanes since the last base: #3613 moved shaders this fixture runs (`ir_projected_face`, the sun-face query layout, `ir_iso_common`), so the AC reference's shader fingerprint is stale and the three-round table is owed again; #3620 and #3610 are oracle and test changes; #3632 (audit worklist, face-reconstruction validation, the source-shadow oracle) and #3629 (`c_shapes_to_trixel`, `ir_iso_common`, the render `CLAUDE.md`) are open and the campaign stays out of their files. #3619 closed by #3627 |
| 2026-09-21 | D1.2a closed: the profile report is a run's own witness. `VOXEL_TO_TRIXEL_STAGE_1` records the yaw it renders at and the overflow ctrl block it already reads into `IRRender::renderRunWitness()` every frame, stage profiling on or off, and the report gains a `Run witness` section (yaw first, last and travelled, zoom, overflow max entries, max dropped, cap, frames sampled), a `Steady frame time` line over the frames after the first quarter, and the frame series. `repeat_profile.py` reads the witness and not the log: it refuses a wrong first or last pose, a camera that moved during a static pose, any dropped entry, a rotated pose that never sampled the lane and a report with no witness, and it pools steady frames across runs for the tail; `million_controls.py` re-verifies every arm from its report. Controls, each on a real binary: four static poses witnessed by a Release build; `overflowCap_` forced to 65,536 reads **565,306 dropped** (630,842 − 65,536) and is refused, where the old warning-line count read 2; the shot-table ramp reads 267.000° travelled through the ±180° seam; a 60-frame run reads p99 102.09 ms all-frames and 20.97 ms steady. At the million control all eight arms vouch for themselves, Release stage-profiling-off included: 45.000°, **0 dropped, 2,208,000 peak entries of an 8,388,608 cap (26%, 25 of 96 MiB)**, which is D2's first residency number. The round's milliseconds are not a reference: the fleet held the host at a load of 7 to 19 and the same frozen scene read 36.8 then 45.5 ms with its frame minimum unchanged, so manifests now carry `host_load_1m`, `host_cpus`, start time and battery charge, and the lock's blind spot is #3638. Evidence: [`million-controls.md`](../../perf/million-controls.md) § Witnessed round, § The run witness and its controls |
| 2026-09-21 | Lane split, again: the open stack #3632 → #3635 (tagged for this campaign, another author) and #3629 own the lighting and shadow kernels, `c_shapes_to_trixel`, `ir_iso_common`, the stage-2 body and `rendering-audit-todo.md`, which is every open D0 item's file, so the campaign stays on D1 and takes D0 up again when they merge |
| 2026-09-21 | D1.2b closed: continuous yaw is a profiled fixture. `IRPerfGrid --yaw-step <radians>` renders frame N at `--yaw + (N − 1) × step`, per rendered frame and as an absolute yaw, so every run renders the same poses; `repeat_profile.py` checks first pose, last pose and travelled arc from the witness (a 300-frame turn reads 0.000 → −1.200, 358.800° travelled, exactly 299 × 1.2°), `million_controls.py` runs it as a third pose, and the report gains a per-frame update-tick series. **A driven yaw pins its pivot at the grid centre** (`--pivot-origin` for a static pose, on every matrix arm): with the default pivot, which is derived from the surface under the viewport centre when a rotation starts from a settled yaw of zero and is otherwise the iso-depth-0 fallback, the part of the world a yaw shows depends on how the run began, and the first unpinned sweeps read as engine findings that do not survive the pin (a first quadrant falling from 43 to 21 ms, a 602 to 739 ms frame at 91.2°, then 65 frames at the 8-update clamp as an effect of 165 ms frames, the update systems costing 2.5 ms a tick). What the 91.2° frame is was not established; the latch's policy and its test say it is not a re-derive. Pinned, at the million control in Release with stage profiling off (host load 2.7 to 4.5, not reference milliseconds; [`continuous-yaw-sweep.md`](../../perf/continuous-yaw-sweep.md)): **zero overflow drops across all 300 poses of a full turn**, peak 2,208,000 of 8,388,608 entries at exactly 45°, which is the objective's drop row read on Metal; away from two special poses rotation costs the same at every yaw (quadrant medians 39.1 to 41.3 ms); **the frame after a cardinal costs 78 to 92 ms and the frame on an exact diagonal 54 to 57 ms against about 41, and each is its sweep's p99** (78 to 88 ms through the cardinals, about 46 without those three frames; 57 ms half a step off them, where the sweep lands on the diagonals and the overflow lane more than doubles); and the fixed updates are worth about 3 ms of a 41 ms frame (37.88 ms at a one-update clamp) |
| 2026-09-21 | D1.2c closed as a measurement: with the per-axis canvases left resident and live, the pinned sweep's long frame after a cardinal goes away. `PerAxisCanvas::Allocate` and `::Release` are in the report's CPU phase table (1.3 and 0.7 to 1.1 ms a call), and with the release skipped by a local, never committed patch the first rotated frame after each cardinal reads **46 to 49 ms where it read 77 to 93**, six crossings of six in two runs each, and the p99 falls from 77 to 56 to 59 ms. The experiment removes two things at once, the re-allocation and the re-entry into the per-axis path (`isAllocated()` never turns false, so the cardinal frame itself runs per-axis at 56 to 60 ms), so it does not say which of them costs; the net saving is about 19 ms a crossing and nothing over the turn, and the extra time is inside neither the calls nor the GPU command-buffer span (42 to 48 ms in all arms). The mechanism is the arm that separates them. This slice was first drawn through the unpinned fixture and concluded the opposite from a 452 ms frame that belonged to the unpinned sweep; the branch was rebuilt on the corrected parent and the experiment re-run. Evidence: [`continuous-yaw-sweep.md`](../../perf/continuous-yaw-sweep.md) § The crossing frame, split |
| 2026-09-21 | D1.2d closed: the control for the pivot pin is a fixture. `IRPerfGrid --yaw-first-frame` renders frame 1 at a pose of its own, `--capture-frame` takes one screenshot, `--default-pivot` keeps the engine's pivot under a driven yaw (contradictory pivot flags and a second yaw writer are refused at startup), the report's witness gains `Camera pivot: explicit focus on N of M frames`, and `repeat_profile.py` checks the first-frame pose, its jump and the pivot the flags ask for, and refuses a capture frame inside a timed run. One pose (46.8°, held for 74 frames, captured after frame 60) from two first frames: with the default pivot the captures match on 80.04% of pixels, the scene translated about 330 pixels, 482,966 against 626,223 candidates at the held view and 50.0% against 62.9% of the screen lit; pinned they are **byte-identical** with 909,433 and 89.8%. It was built to decide which of two "visible sets" was right and showed there were two views; coverage and candidates move together, which is the spirit's coverage rule and not a cull difference. Evidence: [`continuous-yaw-sweep.md`](../../perf/continuous-yaw-sweep.md) § A driven yaw pins its pivot, `continuous-yaw-sweep/pivot-identity/` |
| 2026-09-21 | Checkpoint 3: #3641, #3645, #3650 and #3653 each reviewed by a fresh-context reviewer (read-only, asked for every guard what input makes it pass that should not, and told to recompute every table from the committed reports), plus the render and ECS invariant audits over the stack's C++. No blockers anywhere; the audits found no shader, binding, layout or dispatch change and one shared nit (the witness's main-thread-only contract, now stated). The reviews changed what three of the four PRs claim. #3641: the rotated-pose guard used 1° where the engine goes per-axis above 1e-4 rad, so the 0.785° pose of the old unit bug passed unsampled (now the engine's deadband, pinned by a test); the host load was read before the run queued on the lock, minutes early (now as the run returns); any shot table drives the camera, not only `--yaw-ramp`; a pose line with no samples read 0° and not absent; zoom was witnessed and never guarded; the series was unbounded; none of the controls had an artifact (now committed), and "eleven minutes" was four. #3645: the stated cause of the unpinned sweep's long frame, a pivot re-latch on the cardinal, is contradicted by the latch's policy and its test, and the "catch-up spiral" by the run's own 2.5 ms update ticks; both are withdrawn and the unpinned behaviour is described and not explained (#3652 corrected to match). It also found the sweep's second tail, the exact diagonals at 54 to 57 ms, which is the p99 the doc had used as its clean baseline; the sweep check now verifies the frame count, models travel with the witness's wrap and does not demand overflow samples of a cardinals-only sweep. #3650: the release-disabled experiment removes the re-allocation and the path re-entry together, saves about 19 ms a crossing and nothing over the turn, and the time is inside neither the calls nor the GPU span, so the claim is narrowed to "the long frame goes away when the sets stay resident and live" and the mechanism is named as the separating arm. #3653: `overflow_failure` failed runs the tool calls unpredictable; contradictory pivot flags resolved silently and nothing witnessed the pivot, so a mislabelled arm could not be refused (now a startup refusal, a witness line and a tool check); the capture frame sat inside the timed window; medians and counts were relabelled and the captures committed full-size so their hashes can be checked. Left as worklist in the PR bodies: a unit test for `World`'s witness wiring and the two voxel-pass call sites, and the matrix's sweep arm end to end across two trees. Parent branches were merged into their children, not rebased. Merge order bottom-up: #3641 → #3645 → #3650 → #3653 |
| 2026-09-21 | Resync at startup (Loop step 0): `fleet-campaign-status` read SUPERSEDED (#3641, #3645, #3650 and #3653 merged) and `--apply` parked the branch on master `79ca9e3c6`. Outside PRs read since the last base, and what each corrects in merged campaign work: #3644 and #3654 (the protocol's every-iteration resync, `fleet:campaign-<slug>` membership, this file's `## Contributor lanes`) change the procedure this session follows and no measurement; #3647 (Pillow pinned in `render-harness-tests`, `render_metric_util.py`'s docstring only) leaves every campaign oracle's behaviour unchanged; #3642, #3648 and #3649 (fleet-claim and the GraphQL gate) and #3651 (fog_demo references) touch no campaign fixture: no effect. #3560, stacked on the D0 stack, was reconciled on 2026-09-20. Open other-lane PRs: #3629 (approved, awaiting merge: the fog carrier bit in `c_shapes_to_trixel`, `ir_iso_common`, the stage-2 body, `component_triangle_canvas_textures.hpp` and the render prefab `CLAUDE.md`), #3632 → #3635 (the Codex lane's source-face lighting and cascade receiver stack; #3635 reads CONFLICTING against its parent) and #3643 (`fleet:wip`, design-proposed: a rotation-scoped default pivot acquired in the source frame, in `default_pivot_latch.hpp`, `system_trixel_to_framebuffer.hpp` and `camera-yaw-pivot.md`, the engine's answer to #3652's question). None edits `per_axis_canvas.hpp` or `component_per_axis_trixel_canvases.hpp`. If #3643 lands, D1.2d's default-pivot rows describe the latch as it was; the pinned rows and every timing table are pivot-pinned and stand |
<<<<<<< HEAD
| 2026-09-21 | D1.2e closed: the crossing frame was the re-allocation. `C_PerAxisTrixelCanvases` parks its set on a cardinal frame (`park`: the live fields swap into `parked_`, so `isAllocated()` reads false and all seven readers take the fast path unchanged), swaps it back on the next rotated frame (`unpark`) and frees it after `IRPrefab::PerAxisCanvas::kParkedCardinalFrames` (120) consecutive cardinal frames; the policy is the pure `lifecycleStep` function, pinned by nine headless tests, and the report gains `PerAxisCanvas::Park` / `::Unpark` rows so a run vouches for the lifecycle it took. Interleaved against master's Release binary on the pinned sweep (battery, host load about 3): the first rotated frame after 90°, 180° and 270° reads **42.7 to 48.1 ms against 80.5 to 97.5**, the cardinal frame stays on the fast path (35 to 40 ms; Park 3, Unpark 3, Release 0, Allocate 1), the steady p99 falls from 93.0 and 80.5 to 48.1 and 49.4 ms and no longer moves when the three crossing frames are removed (0.4 and 0.0 ms against 44.9 and 29.8), a crossing's three frames cost 124 to 127 ms against 174 to 184, and the turn's steady mean sits inside the spread (41.35 to 42.90 across the four arms). The path re-entry costs nothing the split experiment could not exclude; what cost 40 to 55 ms was freeing and re-creating the set, of which the timed calls are 2.5 ms. Identity: a cardinal frame with the parked set resident is byte-identical to a never-allocated one and to master's; the first unparked frame (91.2°) is byte-identical to the pose held from frame 1; `render-verify --target IRCanvasStress` passes 11 of 11 at maximum delta 0; the nine-yaw `--sweep-yaw 0 6.2831853 9` set is byte-identical before and after at all nine poses. Found on the way: master's own first rotated frame on a fresh allocation differs from the settled pose (32,300 pixels by 1 to 7, 304 by 32 to 54), which parking removes for a crossing and not for the first frame of a turn; filed as #3660 (agent-approved, defect verified, fix not) with the sort's lagged entry count as the hypothesis. Evidence: [`continuous-yaw-sweep.md`](../../perf/continuous-yaw-sweep.md) § The crossing frame, with the set parked |
| 2026-09-21 | Resync at the top of the D1.2f iteration, stacked on #3665 (the doc's ledger and `## Now` ride forward on the stack, so a slice off master would conflict with it at merge): verdict `stacked`, no new merges, the lane split above unchanged. Issues naming files this stack changes, read and reconciled: #3661 (one reveal model for fog of war; cites the render prefab `CLAUDE.md` for its fog bullets, which this stack does not touch — no effect, and the campaign stays out of the fog lane), #3580 (master's instruction-size check red on `engine/render/CLAUDE.md` at 209 of 200 lines and `engine/prefabs/irreden/render/CLAUDE.md` at 208; this stack's edit to the latter holds it at 208, neither trimming nor growing the offender — the trim is #3580's), #3463 (epic close-out residuals in the voxel and command `CLAUDE.md`s; no overlap with this stack beyond the render prefab `CLAUDE.md` it cites — no effect), and #3660, this campaign's own follow-up from the D1.2e row. A fleet reviewer claimed #3665 and declined it on `fleet:wip` within minutes of its opening (its comment names the label), which is the stand-off the protocol asks for |
| 2026-09-21 | D1.2f closed as a measurement: the exact diagonal is a band. Five pinned static poses (44.4°, 44.9°, 45.0°, 45.1°, 45.6°), stage profiling on, two interleaved rounds on the million scene: a tenth of a degree from 45° already costs 9 ms over 44.4° in the first round (52.8 and 52.9 against 43.9) and 11 to 16.6 in the second, and the exact diagonal 16.6 and 24.5 ms over 44.4° (60.6 and 68.5); the shoulders read alike in count and in the first round's time, the second round's excess sits on the band poses at a lower host load (a GPU power or thermal response on battery is the alternative the data does not exclude), the GPU envelope carries about four fifths of the rise and the fixed-update catch-up the rest (2.65 → 3.6 and 4.0 updates a frame). The one counter that moves is the overflow lane, 578,826 entries at 44.4°, 1,751,439 a tenth of a degree off the diagonal and 2,208,000 on it (bit-identical across rounds), while visible candidates and per-axis entries are the same to 0.005% at every pose; the cost is not linear in the count (7.6 to 7.9 ms per million entries to the shoulders, 17 from shoulder to peak). The three stages that read the lane rise with it (append and sort 7.4 → 17.7, scatter 2.0 → 7.4, relight 12.4 → 17.5 ms per sampled invocation), and so do two that never read it (light volume, per-axis AO), the rows summing to three times the envelope, so the attribution is by the lane and its consumers and not by the rows. The 1.2° sweep steps over the band; how far it extends past ±0.1° is not measured. Found in review: the sort's encoded span is a power-of-two staircase in the lane count, 2,097,152 at 44.4°, 4,194,304 a tenth of a degree off the diagonal and the full 8,388,608 cap on it, which fits the overflow row to a millisecond and predicts the marginal-cost doubling; it is code, checkable at two poses whose counts bracket 2,097,152. Why the lane itself steps on the pose is not established (a coset tie it is not: those sit at 120° and 240°, further apart than the epsilon). Evidence: [`diagonal-pose-cost.md`](../../perf/diagonal-pose-cost.md) |
=======
| 2026-09-21 | D1.2e closed: the crossing frame was the set being freed and re-created. `C_PerAxisTrixelCanvases` parks its set on a cardinal frame (`park`: the live fields swap into `parked_`, so `isAllocated()` reads false and all seven readers take the fast path unchanged), swaps it back on the next rotated frame (`unpark`) and frees it after `IRPrefab::PerAxisCanvas::kParkedCardinalFrames` (120) consecutive cardinal frames; the policy is the pure `lifecycleStep` function, pinned by ten headless tests (nine in Release), and the report gains `PerAxisCanvas::Park` / `::Unpark` rows so a run vouches for the lifecycle it took. Interleaved against master's Release binary on the pinned sweep (battery, host load about 3): the first rotated frame after 90°, 180° and 270° reads **42.7 to 48.1 ms against 80.5 to 97.5**, the cardinal frame stays on the fast path (35 to 40 ms; Park 3, Unpark 3, Release 0, Allocate 1), the steady p99 falls from 93.0 and 80.5 to 48.1 and 49.4 ms and no longer moves when the three crossing frames are removed (0.4 and 0.0 ms against 44.9 and 29.8), a crossing's three frames cost 124 to 127 ms against 174 to 184, and the turn's steady mean sits inside the spread (41.35 to 42.90 across the four arms). The path re-entry costs nothing the split experiment could not exclude; what cost 40 to 55 ms was freeing and re-creating the set, of which the timed calls are 2.5 ms, and the experiment does not say which of the free and the create a driver charges to the next frame. Identity: a cardinal frame with the parked set resident is byte-identical to a never-allocated one and to master's; the first unparked frame (91.2°) is byte-identical to the pose held from frame 1; `render-verify --target IRCanvasStress` passes 11 of 11 at maximum delta 0; the nine-yaw `--sweep-yaw 0 6.2831853 9` set is byte-identical before and after at all nine poses. Found on the way: master's own first rotated frame on a fresh allocation differs from the settled pose (32,300 pixels by 1 to 7, 304 by 32 to 54), which parking removes for a crossing and not for the first frame of a turn; filed as #3660 (agent-approved, defect verified, fix not) with the sort's lagged entry count as the hypothesis. Evidence: [`continuous-yaw-sweep.md`](../../perf/continuous-yaw-sweep.md) § The crossing frame, with the set parked |
>>>>>>> claude/million-entity-render-parked-per-axis-set

### Decisions taken

- 2026-09-21: the per-axis set is parked inside the component by swapping the
  live fields into a second `PerAxisCanvasStore`, so `isAllocated()` stays the
  handle test its seven readers already make and none of them changes. The
  parked set is freed after 120 consecutive cardinal frames: a frame count and
  not a duration, so every run of a sweep takes the same lifecycle, and longer
  than any capture suite's settle (60 frames), so the suites exercise the
  unpark. Rejected: a `live_` flag beside the handles (a second state that can
  drift from them, the shape the no-dirty-flags rule bans); widening the
  readers' predicate to `isAllocated() && rotating` (edits three files other
  lanes hold open, for no gain over parking); a wall-clock window (a 40 ms
  fixture would free in 50 frames and a 16 ms one hold for 120); holding the
  set until the canvas resizes (the cardinal memory contract says a settled
  scene pays nothing).
- 2026-09-21 (lane split in force for this slice): #3629 owns
  `c_shapes_to_trixel`, `ir_iso_common` and `c_voxel_to_trixel_stage_2_body`
  (GLSL and Metal), `component_triangle_canvas_textures.hpp` and the fog bullet
  of `engine/prefabs/irreden/render/CLAUDE.md`; #3632 and #3635 own the
  lighting, sun-shadow and source-face kernels, `system_bake_sun_shadow_map.hpp`,
  `system_entity_canvas_to_framebuffer.hpp`, `ir_render_types.hpp` and the audit
  worklist docs; #3643 owns the default pivot latch,
  `system_trixel_to_framebuffer.hpp`, `system_camera_mouse_rotate.hpp` and
  `camera-yaw-pivot.md`. The campaign keeps `per_axis_canvas.hpp`,
  `component_per_axis_trixel_canvases.hpp`, `gpu_stage_timing.hpp`, the report
  in `world.cpp`, `perf_grid/main.cpp`, `docs/perf/` and the per-axis design
  doc. The one shared file is the render prefab `CLAUDE.md`: this slice rewrites
  its per-axis bullet and #3629 its fog bullet, disjoint hunks that merge
  mechanically. The seven readers of `isAllocated()` are untouched, three of
  them in files the other lanes own, which is why the parked set lives inside
  the component.
- 2026-09-21: the crossing mechanism is its own slice, after a checkpoint. It
  changes the lifecycle of a render component seven systems read, three of
  them in files another lane has open, so it keeps `isAllocated()` meaning
  "the per-axis path is live" and parks the resident sets inside
  `C_PerAxisTrixelCanvases`. It is also the experiment's missing arm: sets
  resident, cardinal frame on the fast path. Rejected: shipping it inside the
  measurement slice (one contract change a PR, and the measurement had just
  been wrong once), and widening the predicate to `isAllocated() && rotating`
  across the seven readers (it edits the other lane's files for no gain over
  parking).
- 2026-09-21: a conclusion drawn through a fixture is re-derived when the
  fixture changes, not patched. D1.2c's first version said the crossing was
  not the re-allocation; the control that said so ran on the unpinned sweep.
  The branch was rebuilt on the corrected parent and the experiment re-run;
  the old head is not kept in the tree.
- 2026-09-21: the sweep steps per rendered frame to an absolute yaw. A
  per-second rate makes a slow run render fewer, wider-spaced poses than a fast
  one, so two arms of one comparison would not render the same frames and the
  culled counts would stop fingerprinting the scene; an incremental
  `rotateYaw` accumulates float error the pose check would have to forgive.
  Rejected with them: reusing `--yaw-ramp` (a shot table that jumps 70°
  between regions and settles 16 frames at each).
- 2026-09-21: a perf fixture that drives the yaw pins the pivot at the grid
  centre, and the matrix passes `--pivot-origin` on every arm. The default
  pivot is right for a person turning the camera and makes a fixture's view
  depend on how the run began. Rejected: the first version of this
  decision, which started the sweep half a step off the cardinals to dodge
  what looked like a crossing hitch (it dodged the symptom and kept the
  history-dependent view); leaving static `--yaw` arms on the default pivot
  (the matrix's 45° and sweep arms would frame different scenes); and changing
  the default pivot's latch policy here (an engine camera behaviour with its
  own tests and users, recorded as a question and not decided by a fixture).
  The matrix's sweep goes through the cardinals, because a real turn crosses
  them and the crossing frames are its tail.
- 2026-09-21: the profile report is the witness for a run's pose and overflow
  loss, and the log is not. `VOXEL_TO_TRIXEL_STAGE_1` records the yaw it
  renders at and the overflow ctrl block it already reads, every frame, and
  `World` writes them with the report, so a Release, stage-profiling-off run
  vouches for itself. Rejected: keeping the log check beside it (two
  witnesses that can disagree, one of which is absent in the build the
  objective names), having IRPerfGrid write its own pose file (the pose is
  then what the demo asked for, not what the voxel pass rendered), and a
  permanent `--overflow-cap` knob for the drop control (a debug flag that
  makes a shipping run lossy; the control is a recorded local patch).
- 2026-09-21: the steady frame line excludes the first quarter of the recorded
  frames, the share IRPerfGrid's auto-profile mean already discards, and the
  report carries the frame series so a tool can pool steady frames across runs
  and a reader can check the rule against the transient. Rejected: a warm-up
  count passed to `enableFrameTiming` (every caller would pick its own and
  tables would stop comparing), and dropping the all-frames line (every
  committed report and the CI perf gate read it).
- 2026-09-21: no million table is committed from a host the fleet is loading.
  The witnessed round in `million-controls.md` is evidence that every arm
  vouches for itself and is labelled with its load; the three-round reference
  on the post-#3613 shaders stays owed until the host is quiet (#3638).
  Rejected: committing the contaminated rounds as the new reference (a D2
  delta of a few ms would be read against rounds that moved 9 ms on their
  own), and holding the benchmark lock for 25 minutes against fleet builds to
  get them.
- 2026-09-20 (human ruling): a battery run is acceptable for the million
  reference when it is recorded. The host stays in high-power mode, every
  manifest carries `host_power`, and the matrix stops if the source changes.
  This replaces the campaign's first call, which was to commit no table from
  a battery run after the same 0° scene read 33.7 then 52.9 ms within half an
  hour. The per-round column stays in the committed summary so a drifting
  host is visible in the table itself, and an AC run is added beside it when
  one is available.
- 2026-09-20: `IRPerfGrid --yaw` becomes radians rather than relabelling the
  flag as degrees: the help text, every committed command line and every
  other demo already say radians, so the fix makes the recorded commands mean
  what their tables claim from now on. The old rows are not rewritten; the
  erratum names the pose they were taken at and the true-45° control beside
  it. Rejected: editing seventeen evidence sets' labels to 0.785° (the commands
  stay wrong for the next reader who copies one), and removing
  `setCameraVisualYaw` (an engine API an out-of-tree creation may call).
- 2026-09-20: million-control arms are interleaved round-robin with the power
  source recorded in the manifest, because grouped arms on this host drift by
  more than the differences they were meant to show. Rejected: cooldown
  sleeps between grouped arms (they shrink the drift without showing it).
- 2026-09-20 (spent the same day, when the stack merged): while #3561–#3568
  are open the campaign works D1 and then D2
  (perf tooling, `perf_grid`, `docs/perf/`, the overflow sort and per-axis
  storage) and leaves D0's remaining items and D4 alone: both edit files that
  stack owns. D0 resumes from `render-stack-sanity-review.md`'s follow-up list
  and a re-read of `rendering-audit-todo.md` once the stack merges, and any
  item that stack closed is struck rather than redone. The million control is
  re-run on master after it merges, because its sampler and receiver changes
  execute inside the fixture. Rejected: stacking campaign slices on the other
  lane's branches (its history is its own to rewrite), and carrying D0 edits
  to `canvas_stress/main.cpp` in parallel (#3565, #3566 and #3567 edit it in
  sequence, so a campaign edit would conflict with each as it lands).
- 2026-09-18: D0 stays ahead of D2–D5 (visual correctness before
  optimization, as the audit worklist orders it); D1 interleaves because
  it changes no render code.
- 2026-09-18: a revoxelized canvas casts sun shadow from its resampled cells,
  the geometry its receiver reads and its display shows; the authored-cell
  caster (`--source-face-shadows`) produces false self-shadow on every tread
  and is retired for that path in the next slice. Rejected: keeping authored
  casters for a smoother floor shadow (the object on screen is the staircase,
  and the campaign spirit puts receiver, caster and visible face on one
  geometry), and any receiver bias that hides the mismatch.
- 2026-09-19: under smooth camera yaw a density-1 shape on an entity canvas
  is voxelized by the lattice walk with a yawed SDF query, not sampled
  analytically: the canvas's voxels are lattice cells at every yaw, so the
  shape is the hexagons of the cells it covers. Rejected: keeping the
  analytical smooth path with a parity filter (its per-pixel surface depth is
  not a lattice cell's, so the emitted diamonds still straddle cells), and
  folding the yawed walk into the cardinal one (the cardinal walk's integer
  rotation keeps it bit-exact; a float rotation there would perturb the
  main canvas's cardinal fast path).
- 2026-09-19: a shape on a revoxelized canvas voxelizes onto the canvas
  lattice (integer + phase) and the composite places the cell; the shape
  pass drops the phase from the owner offset so the raster's per-axis
  rounding picks the nearest lattice cell. Rejected: a frame offset by the
  phase's iso projection plus a depth bias (exact centroid, parity-flipped
  triangles: a hexagon cannot sit at an odd texel row of the trixel
  lattice), and a sub-trixel SDF query to place shapes between cells (a
  voxel canvas has no half cells; a shape between cells shows at the nearest
  one, as a voxel would).
- 2026-09-19: the sun oracle tolerates false shadow on a trixel whose
  centroid ray passes within half a cell of an occupied cell (the sun map's
  nearest-texel read at a terminator; the observed maximum is a third of a
  cell) and fails on any clear-ray false shadow or missed shadow. Rejected:
  a receiver-side bias to hide the residual, and a tighter tolerance derived
  from the sun texel size, which the oracle cannot read from a capture.
- 2026-09-19: the stale canvas_stress reference set is not re-blessed inside
  a campaign PR: 9 of 11 checks fail before the D0.3 change and the diff is
  the September stacks' shading, so a bless here would hide the campaign's
  own effect under a month of drift. Filed as #3552 for a master-based bless.
- 2026-09-18: a shape on an entity canvas follows the voxel producer's frame
  (owner-relative, canvas density, continuous yaw, no clear over a rastered
  canvas) rather than a shape-owned frame. Rejected: keeping the first-shape
  anchor with a density divide (still wrong under yaw and for a second
  shape), a new owner-translation field on `C_CanvasLocalRotation` (a
  once-per-frame owner scan in the shape pass needs no component change), and
  moving the clear into the composite (the producer that writes first owns
  the reset).
- 2026-09-18: the revoxelized cubes keep their nearest-cell appearance.
  The oracle shows the display is exact, so the tooth look is the resample
  plus AO/shadow on real concave steps; presentation is the per-object
  `RotationMode` choice (plain `DETACHED` for smooth source faces) and the
  engine does not smooth, dilate or average the steps. Rejected: rebuilding
  source faces from resampled occupancy, a finer destination lattice (a D7
  level-of-detail question), and any normal or AO blur.

### Follow-ups filed

- PR #3543 (fix-forward, off master): trims `engine/video/CLAUDE.md` and `docs/agents/fleet-labels-reference.md` under their instruction-size budgets; `.claude/commands/role-worker.md` (306 of 303) stays a human edit.
- #3583: `engine/render/CLAUDE.md` (209) and `engine/prefabs/irreden/render/CLAUDE.md` (208) are over their 200-line instruction-size budgets on master, so the check is red on every open PR. Filed with the per-commit growth (mostly #3522's hover contract) instead of trimmed here: the files state contracts the open shadow stack is changing, and #3526 already edits one of them.
- #3618 (agent-approved, plan posted): campaigns resume from their own memory and never reconcile with work done outside them; a resync step every Loop iteration and `fleet-campaign-status` verdicts that say what to do. Filed at the human's direction from this session's five incidents.
- #3619 (closed by #3627): `ir-run --timeout` counted time queued on `ir-acquire`; it now counts from the lock's acquisition.
- #3652 (unlabeled, a human decision): deriving the default yaw pivot at a non-zero yaw changes the framing, so one yaw shows different views depending on how the run began (a still camera that starts rotated changes view once it settles). The perf fixture pins its pivot; whether a derive should preserve framing is the engine's question, with the two-first-frames capture as the repro. The unpinned sweep's long frame at 91.2° is recorded there as unexplained.
- #3638 (unlabeled, a human decision): the benchmark lock excludes cooperating builds only. A live fleet held this host at a load of 19 during a locked million-control run and the same frozen scene read 36.8 then 45.5 ms with its frame minimum unchanged. Until it is decided, a reference table needs a quiet host, and every manifest carries `host_load_1m`.
- #3626 (agent-approved): `fleet-tests` red on master since #3575; with #3583, two checks are red on every open PR for reasons no author can fix.
- #3552 (closed by #3588): `IRCanvasStress` macos-debug render-verify references stale since the September render stacks; re-blessed on master.
- #3534 — fog cut-face column view→world compensation for world-placed
  detached casters (D0 residual of #3342; filed 2026-09-19, awaiting
  approval).
- #1923 — class-2 float iso-depth and projection sites in
  `v_peraxis_scatter.glsl`, `c_shapes_to_trixel.glsl` and their Metal twins
  (D0 housekeeping slice; parked `human:owned` for this campaign, closes
  with the slice).
- #3130 — `--yaw` perf-matrix axis (D1 slice; parked `human:owned` for this
  campaign).
- #3475 (closed by #3595) — edge-gradient sun-shadow oracle on the finite
  caster (a D0 gate).

## Now

- **In flight:** slice D1.2f, the exact-diagonal band (this PR, stacked on
  #3665, the parked per-axis set; both `fleet:wip` until the next checkpoint).
- **Next:** run the resync first. Then the band's separating experiment: the
  sort-disabled and overflow-albedo-only probes at 45.0° against 44.4°,
  interleaved, so the diagonal's 13 to 19 ms of GPU envelope is split between
  the append and sort, the scatter and the relight by full-frame controls; and
  the entries-by-depth-delta histogram that says whether the lane's growth on
  the diagonal is exact ties inside its 8-step epsilon. Then #3660, the first
  rotated frame of a turn (a local patch forcing the sort's encoded span to
  the cap decides whether it is the sort). Owed and blocked on a **quiet
  host** (#3638; ask the human for a window at the checkpoint, and read
  `host_load_1m` before believing a table): the three-round million reference
  on master's current shaders with every arm pinned and the sweep arm, and the
  docs PR proposing the objective's rotation and zoom parity baselines at a
  true 45°. Still on the D1 list: a matched-projected-extent arm (the pin is
  most of it), a longer window for the tail, the `--yaw` perf-matrix axis
  (#3130, parked `human:owned` for this campaign), and the million-preset
  worklist (a C++ test for the pre-init pass on a preset, the preset directory
  inside the fingerprints, the 0.9 GB profiler dump). D0 and D4 wait on
  #3632, #3635 and #3629. D2 has its targets and three residency numbers: at
  45° the light volume, per-axis AO and overflow lighting and sort are the
  largest sampled GPU stages, the GPU frame alone is 28 to 30 ms, and the
  overflow lane peaks at 26% of its 96 MiB on an exact diagonal, 21% a tenth
  of a degree off it and 7% at 44.4°.
