# Epic plan — camera Z-yaw pivot correctness: one rotation-anchor convention + depth-aware default focus

**Objective back-link:** none (interactive architect session, human-directed)
**Author:** opus-architect, 2026-07-22 interactive session with the human.
**Evidence:** `scripts/pivot-verify.py` (new deterministic harness, tooling PR opens alongside this epic) — baseline on master, macos-debug/Metal, zoom 4, threshold 1.5px:

| pass | verdict | dev_x(px) | dev_y(px) |
|---|---|---|---|
| focus-ctr@z4 (voxel) | DRIFT | 29.13 | 16.00 |
| focus-ctr-sdf@z4 | DRIFT | 2.00 | 2.00 |
| focus-off@z4 (voxel) | DRIFT | 29.13 | 16.00 |
| center-column@z4 (voxel) | DRIFT | 29.13 | 16.00 |
| center-depth@z4 (voxel) | DRIFT | 336.00 | 336.00 |

## Problem

The camera Z-yaw pivot (#1352 → #1944 family, `docs/design/camera-yaw-pivot.md`)
does not hold what it promises. Three independently-measured defects, isolated
by the new `pivot-verify` harness (single voxel/SDF cylinder probe,
whole-silhouette threshold centroid, `jitter_probe --stationary`):

- **[A] Half-cell rotation-anchor mismatch between the voxel raster and everything else.**
  Pinning an explicit focus at the probe's exact center leaves a residual orbit whose
  cardinal signature solves to a constant world displacement of exactly
  `(0.5, 0.5, 0.5)`: re-running with the focus at `center + (0.5,0.5,0.5)` pins all four
  cardinals to dev (0.00, 0.00) while the SDF twin then orbits by the mirror image.
  So the voxel raster rotates content about `position + (0.5,0.5,0.5)` while the SDF
  path (and the CPU pivot/picking math) rotates about the exact `position`. The
  displacement rides the iso depth axis (`iso(0.5,0.5,0.5) == (0,0)`), so it is
  invisible at cardinal yaw 0 — references stay green — and rotates into a ~1-iso-px
  orbit under yaw, with the voxel and SDF layers counter-rotating apart (the
  user-visible "SDF shapes jitter against voxels while rotating").
- **[B] Default CAMERA_CENTER pivot pins the wrong depth.** The pinned set of the
  drift-cancel offset is the vertical column `{W : W.xy == F.xy}`, and the default
  focus is `isoPixelToPos3D(viewCenterIso, 0)` — the **iso-depth-0** point under the
  viewport center (the code comment says "z = 0", also wrong). Content AT screen
  center at any other depth sits on the center iso RAY, displaced from the pinned
  column by `(t, t)` in xy (t = depth offset / 3), and orbits: measured 336.00px at
  z=10, zoom 4 (= 2·(10.5+10.5) iso units at 180°, exact to the pixel including the
  [A] half-cell). This is the reported "rotation origin does not follow the screen
  center after moving the camera".
- **[C] Per-axis composite registration offset under an active pivot correction.**
  With [A] compensated (half-cell-shifted focus), cardinals pin exactly but EVERY
  residual-yaw frame renders the voxel scene at a constant ≈(−1, −1.1) iso px offset
  from the cardinal frames (measured (−16, −9)px at zoom 4 across residual yaws of
  every sign/bracket). SDF probe is clean (2px), so it is voxel-per-axis-path-only —
  the effective camera offset is non-integer whenever yaw ≠ 0, and some per-axis
  stage rounds/floors it under a different convention than the cardinal gather
  (the #1944 "apply the correction before per-stage rounding, identical convention
  across stages" gotcha, regressed or never fully honored on the per-axis route).

Fix order matters: [B]'s acceptance gate (center-depth PINNED) is polluted by
[A]+[C]'s ~29px residual until those land, and [C] is only measurable once [A]'s
orbit is gone. So the chain is A → C → B → D.

## Phases

### Phase 1 (A) — render: unify the voxel-raster rotation anchor with the exact-position convention

One rotation-anchor convention engine-wide: **an entity's `position` is the point
the scene rotates about; rendered voxel mass must agree with the SDF path and the
CPU math.** Two candidate mechanisms (worker picks after reading the kernels, the
decision criterion is below):

1. Kernel-side: in the voxel raster's yawed reposition chain
   (`c_voxel_to_trixel_stage_1/2` emit anchor, `v_peraxis_scatter` corner
   reconstruction, `c_voxel_visibility_compact` yawed iso, overflow-lane taps,
   `RESOLVE_PER_AXIS_SCREEN_DEPTH`), rotate positions in the cell-CENTER frame:
   yaw-project `(p + h) − h`-style so the rotating point is `p`, not `p + h`
   (h = (0.5,0.5,0.5) or the per-face equivalent). GLSL + Metal twins together.
2. Convention-side: declare `position + h` the canonical rotation anchor; add the
   half-cell to `cameraYawPivotOffset` callers AND shift the SDF shader's yawed
   reposition (`c_shapes_to_trixel`) + the picking inverse by the same h.

Decision criterion: whichever preserves **cardinal yaw-0 byte-identity** (both do
in principle — h projects to zero at yaw 0) with the SMALLER consumer blast radius,
verified by grepping every `pos3DtoPos2DIsoYawed` call site (CPU + GLSL + MSL).
Candidate 1 is the semantically-honest fix (authoring places voxel centers at
integer positions — `SDF::evaluateGrid` samples cell centers at integer offsets —
so the raster is the deviant); candidate 2 is smaller but enshrines the half-cell
and still has to touch every non-raster consumer, INCLUDING picking under yaw.

Acceptance:
1. `python3 scripts/pivot-verify.py --blocks focus-ctr,focus-off,center-column`
   — the voxel blocks' **cardinal-yaw frames** (0, π/2, π, 3π/2) pin to ≤1.5px
   (add a `--cardinals-only` switch to pivot-verify.py: score the cardinal subset;
   full-sweep PINNED is Phase 2's gate, not this one's).
2. focus-ctr-sdf stays ≤2px AND a mixed voxel+SDF scene at the same center shows
   no relative layer drift at cardinals (extend the harness or eyeball via
   `--pivot-focus-demo`).
3. Cardinal fast path byte-identical: shape_debug render-verify suite green at
   yaw-0 shots (`img_diff` = 0 against current references for non-yaw shots).
4. `jitter_probe` gates stay SMOOTH: `--pan-sweep`, `--yaw-sweep` repro lines from
   `engine/render/CLAUDE.md` §temporal stability.
5. Clean exits everywhere (ir-run RESULT=CLEAN).

### Phase 2 (C) — render: per-axis composite registration under non-integer effective camera offset

Blocked by Phase 1. Localize where the per-axis route (perAxisBase_ /
faceLocalAnchor / scatter corner math / trixel_to_framebuffer pan) rounds the
non-integer effective offset differently from the cardinal gather, and unify the
convention (float-correct BEFORE per-stage rounding, same rounding function both
routes — `roundHalfUp` vs `floor` mismatch is the prime suspect).

Acceptance:
1. Full-sweep `pivot-verify.py --blocks focus-ctr,focus-off,center-column` PINNED
   (all 9 yaws, ≤1.5px) at zoom 4 AND zoom 8.
2. Pan/yaw jitter sweeps stay SMOOTH (no regression of #1944/#2427).
3. Cardinal byte-identity preserved (yaw-0 references).

### Phase 3 (B) — render: depth-aware CAMERA_CENTER default focus

Blocked by Phase 2. Design decision (architect, this session): the default pivot
should pin **the content actually under the viewport center at its rendered
depth**, not the iso-depth-0 point:

- Derive the default focus depth from the composite depth at the viewport-center
  pixel (the `IRPrefab::DepthProbe` readback primitive, #1910 — main framebuffer
  depth decodes to shared trixel-distance units; background/no-hit falls back to
  the current iso-depth-0 point).
- **Latch policy (deterministic, gesture-free):** re-derive the focus only while
  the camera yaw is NOT changing (yaw stable at the last-settled value); while
  consecutive frames change `visualYaw`, keep the latched focus. This pins the
  pre-rotation center content through a rotation regardless of whether the yaw
  came from a mouse drag, a key, or a programmatic `setYaw` (auto-screenshot
  works with no gesture plumbing). Pan re-derives immediately (panning moves what
  is under the center; the pivot should follow).
- Per-frame readback cost: gate the readback on "yaw settled AND (pan or zoom
  changed since last derive)" — steady state does zero readbacks.
- Keep `RotationPivotMode::ORIGIN` and explicit `setRotationPivotFocus` overrides
  unchanged. Update `docs/design/camera-yaw-pivot.md` (contract section + the
  "z = 0" mis-statements) and the `ir_render.cpp:61` comment in the same PR.

Acceptance:
1. `pivot-verify.py --blocks center-depth` PINNED (≤1.5px, all 9 yaws, zoom 4+8).
2. center-column block STILL pinned when the center pixel is background (fallback
   path) — add a background-fallback shot variant if needed.
3. Interactive sanity: pan over an elevated shape in shape_debug, Alt+drag rotate
   — the shape under the crosshair rotates in place (attach before/after GIF or
   screenshot pair to the PR).
4. Cardinal byte-identity at yaw 0 (the correction is identity there by
   construction).

### Phase 4 (D) — cursor-pivot rotate: true-depth click latch + visual pivot indicator + shape_debug toggle

Blocked by Phase 3. The Ctrl+Shift+middle-drag cursor pivot
(`system_camera_mouse_rotate.hpp`) exists but latches
`mouseWorldPos3DAtIsoDepth(0)` — an iso-depth-0 point with the same wrong-depth
defect as [B] — and gives no visual feedback. Deliver:

1. Latch the clicked SURFACE point: `IRPrefab::Picking::castVoxelRay` hit
   (`worldHitPos_`), falling back to the depth-probe center derivation (Phase 3
   helper) on miss.
2. Visual pivot indicator: a small always-on-top marker entity (SDF sphere or
   voxel gizmo via the existing shape flags — behavior-named, e.g.
   `SHAPE_FLAG_XRAY_OCCLUDED`) spawned at the latched pivot for the duration of
   the drag (and a short fade after release), so the pivot point is visible while
   debugging. Prefab-scoped (`IRPrefab::Camera` or a small pivot-indicator
   prefab), NOT a RenderManager feature field.
3. shape_debug: a runtime-toggleable rotation-pivot mode cycle (screen-center ↔
   cursor-latched) on a key, logged to console, so the modes can be compared live
   without restarting. (The broader "standard demo help overlay / settings menu"
   is filed separately — this is just the one demo toggle.)

Acceptance:
1. Clicking a voxel surface and dragging rotates the scene about the clicked
   point (the clicked feature holds its screen position — verify with a
   pivot-verify-style stationary capture at 3+ yaws, extend the harness with a
   `cursor-latch` block driven via `setRotationPivotFocus` + castVoxelRay).
2. Indicator appears at the clicked point, is visible over occluding geometry,
   and despawns after release.
3. No behavior change when the mode is untouched (defaults preserved; existing
   camera-control GUI tests green — run `gui-verify` if applicable).

## Cross-system audit

Shared resource: the yawed reposition convention (`pos3DtoPos2DIsoYawed` call
sites) and the effective-camera-offset rounding. Consumers: stage-1/2 kernels,
visibility compact, per-axis scatter + resolve, overflow lane, shapes_to_trixel,
sun-shadow bake recovery, lighting recovery (`perAxisCellToWorld3DSubCell`),
picking inverses, detached composite placement. Phase 1's PR must list, per call
site, whether it rotates a cell-center or an exact position after the change.
In-flight PRs to reconcile: #2475 (occlusion cull domain — reads yawed cull
viewports), #2537 (shader dedup — touches the same kernel family; Phase 1 should
land after or rebase over it).

## Verification (whole epic)

`python3 scripts/pivot-verify.py` all passes PINNED at zoom 4 and 8 on both
backends (macOS/Metal + Linux/GL), shape_debug + canvas_stress render-verify
suites green, pan/yaw jitter sweeps SMOOTH, clean exits.

## Steward ledger

reconciled-through: PR #2562 merge (2026-07-28)
proposal-pending: https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5100516659

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2545 | merged | #2562 | plan | 2026-07-28 (PR #2562 merge) |
| #2546 | in-progress | #2576 (approved, awaiting Windows smoke) | plan | 2026-07-28 (PR #2562 merge) |
| #2547 | in-progress (design-proposed) | #2585 | plan + A1 | 2026-07-28 (PR #2562 merge) |
| #2548 | open | — | plan | 2026-07-28 (PR #2562 merge) |

### Decisions
<!-- entries: D<n> (<YYYY-MM-DD>): <decision> — source: <link>  (numbered scheme per epic-steward-protocol.md §Decisions; escalation rules reference decisions by D-id) -->
- D1 (2026-07-28): One rotation-anchor convention engine-wide — an entity's exact
  `position` is the point the scene rotates about under camera Z-yaw, for every
  render path; the voxel raster (previously `position + (0.5,0.5,0.5)`) is the
  deviant and was moved, not the SDF/CPU path. Phase 1 candidate **1**
  (kernel-side) was selected over candidate 2. — source: §Phase 1 (A) opening
  sentence + decision criterion; realized by PR #2562.
- D2 (2026-07-28): The settled-cardinal `cardinalLowerCornerShift` (`s_k`, k=1..3)
  is removed from the stores AND symmetrically from the recovery inverse
  `trixelCanvasPixelToWorld3D`, so every (pixel, depth) → world round-trip
  (lighting, AO, fog, sun-shadow bake/receive) recovers identical world positions
  while the canvas pixel where content sits moves at cardinals 1–3. Cardinal
  yaw-**0** output is unchanged. — source: PR #2562 §What + §Cross-system audit.
- D3 (2026-07-28): CPU picking (`mouseWorldPos3DAtIsoDepth` /
  `worldPos3DToMouseScreenPx`) never applied `s_k`, so after D2 the raster
  *agrees* with picking at cardinals 1–3 with zero picking changes. Phase 4's
  `castVoxelRay` surface latch inherits this agreement — do not re-derive a
  picking compensation for it. — source: PR #2562 §What, final bullet.

### Events
- 2026-07-22: filed via file-epic
- 2026-07-24: epic plan doc + ledger landed (PR #2555); pivot-verify harness
  landed 2026-07-26 (PR #2553).
- 2026-07-28: **#2545 (P1) merged via PR #2562** — checklist ticked. Scope-drift
  audit: in scope, candidate 1 as the plan's decision criterion allows (D1). Two
  in-scope deltas beyond the literal "yaw-project the half cell" text, both
  recorded above: the `s_k` removal + symmetric recovery-inverse drop (D2) and
  the resulting picking agreement (D3). Reference refresh in that PR is confined
  to **yaw** shots and therefore does NOT breach Phase 1 acceptance 3
  ("`img_diff` = 0 … for non-yaw shots") / 4:
  - shape_debug: `zoom4_yaw45_inter_cardinal`, `zoom4_yaw90/180/270`,
    `zoom8_yaw180`, `zoom4_pan16_yaw45/90/180_pivot` — every refreshed ref is a
    yaw≠0 pose. The suite's settled yaw-0 refs (`zoom4_origin`, `zoom8_origin`,
    `zoom4_pan16_yaw0_pivot`, the `__crop_*` variants) were **not** touched,
    which is the positive evidence for cardinal-0 byte-identity.
  - canvas_stress: `compare_yaw0`, `compare_yaw_q`, `so3_*`, `revoxelize_solids`
    — the `yaw0`-named ones are not settled yaw-0 poses. `autoRotate_` defaults
    true (`creations/demos/canvas_stress/main.cpp`) and the manifest's `compare`
    extra_run passes only `--only compare` (unlike the `floor_selfshadow` /
    `shadow_overlay_floor` runs, which do pass `--no-auto-rotate --no-spin`), so
    `AUTO_YAW_ROTATE` is live through the 60-frame settle window that
    `applyShotCameraState` opens *after* it writes the shot's
    `yawRadians_ = 0` (`engine/video/src/auto_screenshot.cpp`).
- 2026-07-28: downstream siblings re-validated against PR #2562. No stale plan
  text found: #2546's plan targets the residual-yaw offset, which D1/D2 do not
  touch, and its PR #2576 is already authored on top; #2547's `DepthProbe` /
  `isoPixelToPos3D` / `cameraYawPivotOffset` references all survive (the
  composite depth key moved to the cell anchor, and PR #2585 is stacked above
  that change); #2548's premise is strengthened by D3, not invalidated. Skip-guard
  not engaged — #2576 carries neither `fleet:merger-cooldown` nor
  `fleet:stacked-rebase`.
- 2026-07-28: **#2547 / PR #2585 design triage.** One question derivable
  (amendment A1), one novel → parked `fleet:design-proposed` and escalated as
  this iteration's proposal package (`fleet:steward-proposal` on the umbrella;
  see `proposal-pending` above). Steward direction:
  https://github.com/jakildev/IrredenEngine/pull/2585#issuecomment-5100511180

## Amendments

<!-- append-only; newest wins where it contradicts older plan text. Format per
     docs/agents/epic-steward-protocol.md §"Plan amendments (append-only)". -->

### A1 — 2026-07-28 — child #2547 (Phase 3 / B) — trigger: design-block triage of PR #2585

- **Decision:** Phase 3 acceptance criterion 2 is discharged by a **purpose-built
  background-center variant** of the harness — center pixel on background, the
  derive must fall back to `isoPixelToPos3D(viewCenterIso, 0)`, and that block
  must pin ≤1.5px across the sweep. It is **not** discharged by holding the
  existing `center-column` block green: `center-column`'s center pixel lands on
  the probe at iso depth 7, so that block never exercised the fallback path the
  criterion is about.
- **Supersedes:** the implicit reading of §Phase 3 (B) Acceptance 2 that
  `center-column` is the vehicle for the fallback check. The criterion's subject
  (the background-fallback path) is unchanged.
- **Acceptance criteria:** §Phase 3 (B) Acceptance 2 now reads: "a
  background-center block pins ≤1.5px across the sweep with the derive falling
  back to `isoPixelToPos3D(viewCenterIso, 0)`." Whether `center-column` itself
  may go red is **not** settled here — it is a consequence of the surface-vs-axis
  contract ruling and rides the proposal package.
- **By:** epic-steward — source: §Phase 3 (B) Acceptance 2, "add a
  background-fallback shot variant **if needed**"; the parallel clause in #2547
  §Acceptance criteria, "add a background-fallback variant to the harness **if
  needed**".
