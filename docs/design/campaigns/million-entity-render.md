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

### Decisions taken

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
- #3619 (agent-approved): `ir-run --timeout` counts time queued on `ir-acquire`, so a healthy run behind fleet builds reads `ALIVE-TIMEOUT`; `million_controls.py` passes `--timeout 900` until it lands.
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
- #3475 — edge-gradient sun-shadow oracle on the finite caster (a D0 gate;
  approved sonnet, queue-owned — coordinate before touching
  `scripts/render-shadow-metric.py`).

## Now

- **In flight:** none. #3577 and #3581 are approved and await merge, bottom-up.
- **Next:** run the resync first (#3618 describes why): master moved 33 commits
  during the last session. Then #3581's worklist items that make a Release run
  able to witness its own pose and drops (both belong in the profile report),
  and warm-up-excluded frame percentiles in the same report, since the
  objective's p99 row is unreadable without them. Then D1.2, continuous yaw as
  a profiled fixture, a matched-projected-extent arm and the `--yaw`
  perf-matrix axis (#3130, parked `human:owned` for this campaign). A docs PR
  proposes the objective's rotation and zoom parity baselines at a true 45°
  (1.30× at the million control today) for the human to merge or decline. D0
  and D4 are open to the campaign again: the shadow stack merged, so re-read
  `rendering-audit-todo.md` and `render-stack-sanity-review.md` and strike what
  it closed before picking a D0 slice. D2 has its targets from the reference:
  at 45° the light volume, per-axis AO and overflow lighting and sort are the
  largest sampled GPU stages, and the GPU frame alone is 30.4 ms.
