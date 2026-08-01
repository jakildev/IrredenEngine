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

reconciled-through: PR #2585 merge (2026-08-01T04:43:21Z, master `4c4554d5`) —
P3 (#2547) reconciled; #2669 adopted (flow c)
proposal-pending: **OPEN** — `## STEWARD PROPOSAL 2026-08-01`
https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5149886413
(one question: #2669's default-pivot latch-update policy / contract amendment).
`fleet:steward-proposal` applied to the umbrella; its removal is the re-fire
edge. The **prior** package (2026-07-28) was answered by the architect ruling
https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5106383295
(package: issuecomment-5100516659) and distributed 2026-07-29 as A2/A3 + the
`## Steward direction` on PR #2585.

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2545 | merged | #2562 | plan | 2026-07-28 (PR #2562 merge) |
| #2546 | merged | #2576 (`fleet:needs-windows-smoke` open) | plan | 2026-07-29 (PR #2576 merge) |
| #2547 | merged | #2585 (`fleet:needs-windows-smoke` + `fleet:needs-human` open) | plan + A1–A2 | 2026-08-01 (PR #2585 merge) |
| #2548 | open — PR #2659 in flight (`fleet:semantic-conflict`) | #2659 (open) | plan + A5 | 2026-08-01 (PR #2585 merge) |
| #2669 | open — **adopted 2026-08-01 (flow c)**, blocked on the pending ruling | — | stub | 2026-08-01 (adoption) |

A3 and A4 are epic-scope (the whole-epic verification bar) and are not listed per
child; they bind every row above at close-out. A4 supersedes A3's block set and
verdict vocabulary.

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
- D4 (2026-07-28): The default `CAMERA_CENTER` pivot owes contract **(A)** — it
  pins **the surface point under the viewport-center pixel**, not the content's
  axis. Rationale of record is pivot-source consistency: Phase 4 (#2548) latches
  a clicked *surface* point, so an axis-pinning default would fork the meaning of
  "rotate" by how the pivot was acquired; (A) is also the only depth-honest
  option ("the axis of the content" is not well-formed for terrain, floors, or
  merged voxel fields). An extended body swinging about its near surface is
  therefore correct behavior, not a defect. Literal spin-in-place, if ever
  wanted, is a separate **object-pivot mode** (pick → entity →
  transform/centroid) layered on top of the surface latch — its own child issue,
  never a replacement. — source: architect ruling
  https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5106383295
  §1; realized by amendment A2.
- D5 (2026-07-28): Under D4, re-grounding the `center-depth` and `center-column`
  `pivot-verify` gates onto a **pinned-point oracle** is authorized **inside PR
  #2585 itself** — an accepted mechanism that shifts a measured signal re-grounds
  the stale gate in the same PR; shipping red is not acceptable, and a gate
  unreachable by construction (≤1.5px needs r ≤ ~0.07 world units) is worse. The
  whole-silhouette-centroid oracle is retired for the default-pivot passes only;
  `focus-ctr` / `focus-off` / `focus-ctr-sdf` are untouched. — source: architect
  ruling §2; realized by amendments A2 (child gates) and A3 (epic criteria).
- D6 (2026-07-29): Phase 2's residual per-axis offset was **not** a rounding-
  function mismatch. §Phase 2 (C) named "`roundHalfUp` vs `floor` mismatch is the
  prime suspect"; the shipped root cause is that the forward scatter's screen
  re-projection built `cornerIso` from the per-axis **store** anchor
  `perAxisBase` (`trixelOriginOffsetZ1(canvasSize) + floor(cameraIso)`), whose
  `(-1,-1)` term is the trixel grid's sub-pixel **lattice alignment** — a
  canvas-storage convention that the `ij - perAxisBase` recovery depends on but
  which must not ride into on-screen placement. Fix: anchor the re-projection on
  the canvas geometric center (`canvasSize/2` = `perAxisBase + (1,1)`), recovery
  untouched. **Standing consequence:** store anchor and screen anchor are now
  distinct concepts on the per-axis route — Phase 4 and any future scatter work
  must not re-conflate them. — source: PR #2576 §Root cause / §Fix.
- D7 (2026-08-01): The default-pivot gate as **shipped** is the per-shot
  `[pivot-focus-assert]` pinned-point check (derived focus vs the analytic
  ray/surface intersection over the probe's own carve constants — `IRMath::SDF::evaluate`
  + `effectiveParams`, the same call the spawn makes, so oracle and geometry
  cannot drift; tolerance 0.6 world units = one iso-depth unit of composite
  quantization), plus a **`center-axis`** block as the visual net. The ruling's
  second form — a small crop about the center pixel — was **built, measured, and
  rejected**: on a rotationally-symmetric probe the pinned surface point rotates
  onto the limb at ±90°, so the crop straddles the silhouette edge (**42 px** of
  crop-centroid drift on `center-depth` at zoom 4, 180 px crop; `center-column`
  leaves ~15 px of margin), and the effect is scale-invariant so no crop size
  fixes it. `center-axis` puts the probe's axis **on** the viewport-center ray
  with its near cap at the ray's entry step, so the derived surface point *is*
  the probe's axis point and a bias-free derive maps the silhouette onto itself —
  the derived-focus twin of `focus-ctr`, isolating the residual to one number.
  `center-column` / `center-depth` keep their geometry (they are what
  *demonstrates* the contract) but their silhouette deviation is **reported, not
  gated**, and they emit `FOCUS-OK` rather than `PINNED`. **Not an escalation:**
  D5's substance (re-ground the default-pivot gates inside #2585) is honored, and
  A2's "prefer both forms" was the ruling's own word — the substitution is
  carried by a measurement, not a preference. Realized by A4. — source: PR #2585
  §"Contract ratified + gate re-grounded" (§"Deviation from the direction, with
  the measurement that forced it") and §Test plan.

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
- 2026-07-29: **#2546 (P2) merged via PR #2576** (master `dce3d104`) — checklist
  ticked. Scope-drift audit: in scope (both scatter shader twins,
  `docs/design/camera-yaw-pivot.md`, refreshed references), but the **mechanism
  differs from the plan's stated hypothesis** — §Phase 2 (C) named a
  `roundHalfUp`/`floor` mismatch; the real cause was the store-anchor lattice
  alignment riding into screen placement (recorded as **D6**). No Decision
  contradicted. The PR also added a child plan file `.fleet/plans/issue-2546.md`
  (worker-authored, 2026-07-24) — the first child of this epic with its own plan;
  the epic plan stays the authority for cross-phase text.
- 2026-07-29: **#2546 acceptance-criteria audit** (not just scope) — 1, 3, 4 met
  as written (pivot-verify `focus-ctr`/`focus-off`/`center-column` PINNED ≤1.31px
  at zoom 4 **and** 8; 19 cardinal shape_debug shots + all crops byte-identical,
  `max_delta` 0; `RESULT=CLEAN`). **Criterion 2 is discharged by a different
  instrument than its wording:** it asks for pan/yaw jitter sweeps to "stay
  SMOOTH", and the PR reports the sweeps **bit-identical to master** at zoom
  2/4/8 with the residual sub-pixel JITTER verdicts unchanged, which the PR
  attributes to the "pre-existing #2427/#2346 class". Bit-identity is the correct
  no-regression discharge for *this* PR — it did not shift the signal.
  **Unresolved, and close-out must not paper over it:** #2427 and #2346 are both
  **CLOSED**, and #2427 closed `COMPLETED` (2026-07-21) on an acceptance
  criterion of "reversals=0, residual ≤1.5px at zoom 2/4/8" — i.e. **SMOOTH**.
  If PR #2576's reading is right, either that gate regressed after #2427 closed
  or a distinct residual survives it; **either way no open issue owns the
  residual**, and §Verification (whole epic) / §Closing criteria still demand
  "pan/yaw jitter sweeps SMOOTH". Steward has not run the probe (docs-only lane),
  so this is recorded as a discrepancy to verify, **not** as an asserted
  regression. Close-out must resolve it — re-run the sweeps and either cite a
  SMOOTH verdict or file an owner for the residual and restate the criterion.
  Flagged on the umbrella.
- 2026-07-29: reference-refresh check against Phase 2 acceptance 3 — the 10
  refreshed macOS references are 2 shape_debug yaw-45 shots
  (`zoom4_yaw45_inter_cardinal`, `zoom4_pan16_yaw45_pivot`) and 8 canvas_stress
  auto-rotating-pose shots (`compare_yaw0`, `compare_yaw_q`, `revoxelize_solids`,
  `so3_*`). No settled shape_debug yaw-0 reference was touched, so acceptance 3
  holds by the same reasoning recorded for PR #2562 (canvas_stress `yaw0`-named
  shots are not settled yaw-0 poses — `autoRotate_` defaults true).
- 2026-07-29: cross-host — PR #2576 carries `fleet:needs-windows-smoke`; its
  test-plan line "build + smoke IRShapeDebug on Linux/OpenGL (GLSL twin
  unverified here)" is unchecked. The GLSL twin of the scatter fix is therefore
  **unverified on any OpenGL host**; the smoke lane owns it. Close-out must cite
  that verdict. Not an invisible gap — the label is applied.
- 2026-07-29: downstream siblings re-validated against PR #2576. **No stale plan
  text.** #2547's Phase 3 surfaces (`DepthProbe`, `isoPixelToPos3D`,
  `cameraYawPivotOffset`) are untouched by #2576, which changed only the scatter
  screen anchor; PR #2585 is stacked on #2576's branch and so already contains
  it, and A2's `center-column` re-grounding is authored against the
  **post-#2576** baseline (PINNED ≤1.31px) — correct as written. #2548's Phase 4
  surfaces (`castVoxelRay`/`worldHitPos_`, `system_camera_mouse_rotate.hpp`) are
  likewise untouched; its only delta is D6's standing consequence (store anchor ≠
  screen anchor on the per-axis route) and the A3 criteria restatement.
  Skip-guard not engaged — #2585 carries neither `fleet:merger-cooldown` nor
  `fleet:stacked-rebase`. **Branch note (merger's lane, not the steward's):**
  #2585's base `claude/2546-peraxis-composite-registration` merged, so #2585
  needs re-targeting onto master.
- 2026-07-29: **proposal answered and distributed.** Architect ruling
  https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5106383295
  answered both questions (contract (A); gate re-grounding authorized in #2585)
  and removed `fleet:steward-proposal` — the re-fire edge. Recorded as D4/D5,
  realized as amendments **A2** (child #2547 gates + superseded "spins in place"
  wording) and **A3** (whole-epic verification bar). PR #2585 design-unblocked.
- 2026-08-01: **#2547 (P3) merged via PR #2585** (master `4c4554d5`, base
  `master` — the re-targeting flagged 07-29 was done) — checklist ticked.
  Scope-drift audit, three deltas, **no Decision contradicted by the code**:
  - **A2's second gate form was built, measured, and replaced.** Recorded as
    **D7** above; restated for the epic bar as **A4** below. The one real
    deviation from a steward amendment this iteration, and it arrives with the
    measurement that forced it.
  - **Verdict vocabulary changed.** `center-column`, `center-depth` and the new
    `center-axis` emit **`FOCUS-OK`**, not `PINNED`. A3 anticipated the oracle
    swap but predates both the vocabulary and the block-set growth
    (`background-center` and `center-axis` are new blocks). The umbrella's
    §Closing criteria sentence "all passes PINNED (≤1.5px)" is therefore
    unreachable as literally worded — **A4** is the restatement of record; the
    umbrella body was **not** edited (the steward's body carve-out covers the
    `## Children` checklist only).
  - **Additive, in scope.** Readback + `enc` decode relocated into
    `IRRender::readbackCompositeDepth` / `decodeCompositeDepth`, with
    `IRPrefab::DepthProbe` keeping its public surface and delegating (verified on
    master: `depth_probe.hpp:59,77`); new `IRRender::getDefaultRotationPivotFocus()`
    (`ir_render.hpp:347`); new `test/render/camera_pan_pivot_test.cpp` (5 tests,
    including a negative control pinning the broken world-point latch's exact
    `(20,30)` shift). Post-review amendment `c883c9e1` latches the iso **depth**,
    not the world point — `isoPixelToPos3D`'s depth argument shifts along
    `(1,1,1)`, which projects to `(0,0)`, so the focus's iso projection is
    depth-invariant and `IRMath::cameraMoveRelativeToYaw`'s pan identity holds at
    every latched depth.
  - **A1 discharged as amended.** A1 required a purpose-built background-center
    block rather than holding `center-column` green; `background-center` shipped,
    derive returns `(12,-12,0)` exactly on all 9 shots, PINNED 0.91/1.21 px at
    zoom 4 and 0.94/1.25 at zoom 8.
- 2026-08-01: **close-out gate grew three items** (see Findings F1–F3 below):
  **#2645** (the only currently-red pass), **#2641** (systematic derive bias,
  cross-backend risk), **#2669** (contract amendment — adopted, and this
  iteration's proposal package).
- 2026-08-01: **#2669 adopted onto the checklist (flow c).** It carries
  `**Part of epic:** #2544` and `**Blocked by:** #2547` and was absent from the
  checklist. `fleet-validate-stack 2544 --state all --check-checklist` reports
  **all 5 children PASS** with #2669 as the sole `missing-from-checklist` drift
  item — stack accepted, adoption stands. Plan **stub** committed
  (`.fleet/plans/issue-2669.md`): it must not be claimed before the ruling. It is
  worker-filed at the Opus final reviewer's explicit direction, not a steward
  debt-capture stub, so the pending-human adoption skip-guard does not apply.
- 2026-08-01: **proposal package raised** —
  https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5149886413,
  one question (#2669's latch-update policy). Novel: it amends the contract D4
  ratified, so no sentence in the plan, the ledger, or
  `docs/design/camera-yaw-pivot.md` decides it. Steward recommendation is
  **option 2** (re-derive at rotation start) on two grounds the issue itself does
  not make — (i) each readback is a full GPU flush and
  `engine/prefabs/irreden/render/depth_probe.hpp:32-35` names that cost as the
  reason to keep the probe "strictly debug-gated and single-pixel", which option
  3's N-flush per-derive loop contradicts on the very primitive it calls; (ii)
  D4's *own* rationale of record was pivot-source consistency with Phase 4, and
  Phase 4's cursor latch re-acquires depth at gesture start by construction, so
  option 2 is that same policy for the default pivot while option 1 leaves the
  two pivot sources with different staleness behavior — the fork D4 was chosen to
  avoid. `fleet:steward-proposal` applied.
- 2026-08-01: downstream sibling re-validated against PR #2585 → **A5** (P4 /
  #2548). **Skip-guard did not fire:** #2548's open PR #2659 carries
  `fleet:semantic-conflict`, `fleet:authored-on-macos`,
  `fleet:needs-windows-smoke` — neither `fleet:merger-cooldown` nor
  `fleet:stacked-rebase`, the two labels the guard names. Flagged on the umbrella
  anyway, because that PR is *in flight* and its author should read A5 before the
  next push. #2669's ruling is a live dependency on P4's own latch policy.

### Findings (close-out gate — beyond the checklist)
- **F1 — #2645 is the only currently-red `pivot-verify` pass.** `focus-ctr-sdf`
  DRIFT 2.00/2.00 px at both zooms while its voxel twin pins at 0.94/1.27.
  §Closing criteria demands every pass green, so it gates close-out regardless of
  attribution. **Its own body states that "pre-existing" is inferred from
  structure (the explicit-focus path short-circuits the #2547 derive), not
  measured against an `origin/master` build** — close-out must not cite it as
  pre-existing without that measurement.
- **F2 — #2641 is a systematic derive bias with cross-backend risk.** The derive
  reads exactly one iso-depth unit deep on camera-facing cap entries
  (`center-column`, `center-axis`); the lateral entry is within half a unit and
  the background fallback is exact. That `(1/3,1/3,1/3)` world-unit displacement
  is the *entire* 12 px (zoom 4) / 22 px (zoom 8) residual `center-axis` measures
  — still a ~12× improvement on the pre-#2547 focus's 150 px in the same
  configuration. Leading hypothesis is the trixel→framebuffer parity shift
  applied to the depth read on GL but not Metal, so §Closing criteria's "on both
  backends" is the binding clause.
- **F3 — #2669 is unresolved and adopted.** See the proposal package. Note its
  last word on testing: the guard has to **move the camera between derives**,
  which no current `pivot-verify.py` block does — close-out must not accept a
  green sweep as evidence on this question.
- **F4 (standing, unchanged) — the jitter residual still has no owner.** #2427
  closed COMPLETED on a SMOOTH criterion and #2346 is closed, yet §Verification
  (whole epic) / §Closing criteria still demand SMOOTH pan/yaw sweeps. Recorded
  2026-07-29; nothing this iteration changes it.
- **F5 (standing, widened) — the OpenGL host has verified none of this.** Both
  #2576 and now #2585 carry `fleet:needs-windows-smoke`; the GLSL twin of the
  Phase-2 scatter fix and the Phase-3 composite-depth readback are unverified on
  any OpenGL host. #2641 is where the backend question lands. Labels are applied,
  so this is tracked, not invisible.

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

### A2 — 2026-07-29 — child #2547 (Phase 3 / B) — trigger: proposal answered (architect ruling on #2544)

- **Decision:** the default `CAMERA_CENTER` pivot owes contract **(A)** — it pins
  **the surface point under the viewport-center pixel** (the composite-depth
  readback), not the axis/centroid of the content under it. Two consequences the
  worker may act on directly:
  1. **An extended body swinging about its near surface is correct behavior**,
     not a residual defect. The centroid orbit that remains at r=4 is what (A)
     specifies; do not add a compensation pass chasing it.
  2. **Re-grounding `center-depth` and `center-column` onto a pinned-point oracle
     is authorized inside PR #2585 itself.** Prefer **both** forms per the
     ruling: assert the derived focus against the analytic ray-surface value (the
     sharp check) **and** score a small crop about the center pixel (the visual
     regression net). `center-column`'s old PINNED 0.94/1.27 was measuring the
     defect this phase fixes — its center pixel hits the probe at iso depth 7, so
     the derive moving the focus off the depth-0 anchor is correct under (A).
     `focus-ctr` / `focus-off` / `focus-ctr-sdf` are **untouched** — they are not
     default-pivot passes and keep the existing whole-silhouette oracle.
  A literal "spins in place" pivot is a separate **object-pivot mode** (pick →
  entity → transform/centroid) layered on the surface latch — its own child
  issue if ever wanted, never a replacement. Do not absorb it into #2547.
- **Supersedes:**
  - §Phase 3 (B) Acceptance 1 — "`pivot-verify.py --blocks center-depth` PINNED
    (≤1.5px, all 9 yaws, zoom 4+8)" now means **pinned-point-oracle** ≤1.5px, not
    whole-silhouette-centroid ≤1.5px. Under the centroid oracle the criterion is
    unreachable by construction (≤1.5px needs r ≤ ~0.07 world units).
  - §Phase 3 (B) Acceptance 3 — "the shape under the crosshair rotates in place"
    is superseded by "**the surface point under the crosshair holds its screen
    position**". Same substitution applies to the twin sentences in child issue
    #2547's §Scope ("what I'm looking at spins in place regardless of scene
    elevation") and §Acceptance criteria ("the shape under the center rotates in
    place"): those were authored under the point-probe approximation, where
    surface ≡ axis and the arithmetic works exactly — descriptive of that case,
    not a contract for extended bodies. **The issue body is not edited** (steward
    scope); this amendment governs, and workers read the plan newest-first.
  - Nothing in A1 — A1's background-fallback vehicle stands unchanged, and A1's
    explicitly-deferred question ("whether `center-column` itself may go red") is
    what this amendment settles.
- **Acceptance criteria:** §Phase 3 (B) Acceptance now reads:
  1. `pivot-verify.py --blocks center-depth` PINNED ≤1.5px (all 9 yaws, zoom 4+8)
     **against the pinned-point oracle** — analytic ray-surface assert plus a
     center-pixel crop score.
  2. unchanged (A1's background-center block).
  3. Interactive sanity: pan over an elevated shape in shape_debug, Alt+drag
     rotate — **the surface point under the crosshair holds its screen position**
     (attach before/after screenshot pair to the PR).
  4. unchanged (cardinal byte-identity at yaw 0).
  Plus: `center-column` re-grounded onto the same pinned-point oracle in this PR,
  and the re-grounding documented in the PR body so the gate change is auditable.
- **By:** epic-steward — source: architect ruling
  https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5106383295
  §1 ("**1. Contract: (A) — the surface point under the viewport-center pixel.**
  Confirmed … pivot-source consistency"; "treat it as descriptive of that case,
  not a contract for extended bodies. Amend the wording accordingly.") and §2
  ("**2. Gate re-grounding: authorized, in #2585 itself.** … prefer **both**
  forms … `center-column`: re-ground the same way."). Recorded as ledger D4/D5.

### A3 — 2026-07-29 — epic #2544 (whole-epic verification) — trigger: proposal answered (architect ruling on #2544)

- **Decision:** the whole-epic verification bar is restated so gate and criteria
  cannot contradict: `pivot-verify.py` all passes PINNED ≤1.5px at zoom 4 and 8
  on both backends, where the **default-pivot passes (`center-depth`,
  `center-column`) are scored against the pinned-point oracle** and the remaining
  passes keep their existing oracle. The rest of §Verification (whole epic) —
  shape_debug + canvas_stress render-verify suites green, pan/yaw jitter sweeps
  SMOOTH, clean exits — is unchanged.
- **Supersedes:** the unqualified "all passes PINNED (≤1.5px)" reading in
  §Verification (whole epic) of this plan, and the identically-worded sentence in
  the umbrella issue #2544 §Closing criteria. **The umbrella body is not edited**
  — the steward's body carve-out covers the `## Children` checklist only; this
  amendment is the restatement of record and is linked from the umbrella thread.
- **Acceptance criteria:** unchanged in count; only the oracle for the two
  default-pivot passes is restated. Close-out for this epic must cite the
  pinned-point-oracle results for `center-depth` / `center-column`, not centroid
  numbers.
- **By:** epic-steward — source: architect ruling
  https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5106383295
  §2, "Restate the umbrella §Closing criteria in the same docs pass ('all passes
  PINNED (≤1.5px)' → pinned-point-oracle ≤1.5px for the default-pivot passes), so
  gate and criteria cannot contradict." Recorded as ledger D5.

### A4 — 2026-08-01 — epic #2544 (whole-epic verification) — trigger: PR #2585 merged (P3 / #2547)

- **Decision:** the whole-epic verification bar is restated a second time to
  match the gate that actually shipped (D7). `python3 scripts/pivot-verify.py
  --zoom 4 --zoom 8` on both backends, scored per block:
  - **Silhouette-gated, `PINNED` ≤1.5 px** — `focus-ctr`, `focus-off`,
    `focus-ctr-sdf`, and the new **`background-center`**. Each rotates its probe
    about a point on the probe's own axis, so the silhouette maps onto itself and
    the centroid gate is exact. These are the passes whose verdict word is still
    `PINNED`.
  - **Pinned-point-gated, `FOCUS-OK`** — `center-column`, `center-depth`, and the
    new **`center-axis`**. Gated on the per-shot `[pivot-focus-assert]` line
    (derived focus vs analytic ray/surface, tolerance 0.6 world units) **and** on
    the latched focus not moving mid-sweep, with `view_held` as a checkable
    precondition. Their silhouette deviation is **reported, not gated** — a
    non-zero `dev_x`/`dev_y` on these three is not a failure, and close-out must
    not read one as such.
  - `center-axis` is the visual regression net. The center-pixel **crop** the
    2026-07-28 ruling §2 preferred is **retired unbuilt-as-a-gate** — see D7 for
    the measurement (42 px of crop-centroid drift, scale-invariant).
- **Supersedes:**
  - **A3** — which restated the bar as "all passes PINNED ≤1.5px … where the
    default-pivot passes are scored against the pinned-point oracle". A3's oracle
    substitution stands; what it could not know is that (a) the default-pivot
    passes stopped emitting the word `PINNED` at all, and (b) the block set grew
    by two. A4 is the current bar; where they disagree, A4 wins.
  - **A2 §Acceptance 1's** "analytic ray-surface assert **plus** a center-pixel
    crop score" — the second conjunct is retired per D7. A2's first conjunct and
    everything else in A2 stand unchanged.
  - the unqualified "all passes PINNED (≤1.5px)" sentence in **umbrella #2544
    §Closing criteria** (and the identical sentence in §Verification (whole
    epic) of this plan). **The umbrella body is not edited** — the steward's
    carve-out covers the `## Children` checklist only; this amendment is the
    restatement of record and is linked from the umbrella thread.
- **Acceptance criteria:** unchanged in substance and count. The remainder of
  §Verification (whole epic) — shape_debug + canvas_stress render-verify suites
  green, pan/yaw jitter sweeps SMOOTH, clean exits — is untouched. Close-out
  must additionally clear F1–F5 in the Findings section above: #2645 (red pass),
  #2641 (derive bias, both backends), #2669 (contract ruling), the unowned
  jitter residual, and an OpenGL-host verdict for #2576 + #2585.
- **By:** epic-steward — source: D7 above; PR #2585 §"Contract ratified + gate
  re-grounded" (the per-block "New oracle" list and the "Deviation from the
  direction, with the measurement that forced it" paragraph) and §Test plan (the
  verdict column: `PINNED` for `focus-ctr`/`focus-off`/`background-center`,
  `FOCUS-OK` for `center-column`/`center-depth`/`center-axis`, `DRIFT` for
  `focus-ctr-sdf`).

### A5 — 2026-08-01 — child #2548 (Phase 4 / D) — trigger: PR #2585 merged (P3 / #2547)

- **Decision:** two things Phase 4 must take from what P3 shipped, plus one live
  dependency.
  1. **The "Phase 3 helper" now has a name and a layer.** §Phase 4 bullet 1's
     fallback — "the depth-probe center derivation (Phase 3 helper)" on a
     `castVoxelRay` miss — resolves to **`IRRender::readbackCompositeDepth(px)`
     + `IRRender::decodeCompositeDepth(rawDist)`** (`ir_render.hpp:201`,
     `ir_render_types.hpp:390`), or **`IRRender::getDefaultRotationPivotFocus()`**
     (`ir_render.hpp:347`) when the already-latched default focus is what is
     wanted. It is **not** a prefab-side derivation: the pivot runs inside
     `engine/render`, upstream of `IRPrefab::DepthProbe`, so implementing it
     prefab-side would invert the dependency and fork the #1960 N-tier decode.
     `IRPrefab::DepthProbe` keeps its public surface and **delegates**
     (`depth_probe.hpp:59,77`), so existing `DepthProbe` call sites are
     unaffected — but new pivot-path code belongs on the `IRRender::` entry
     points.
  2. **Latch the iso DEPTH, not the world point.** P3's post-review amendment
     `c883c9e1` fixed exactly this class of bug: latching the derived *point*
     froze `getEffectiveCameraIso`'s dependence on the live `cameraIso`, which is
     the derivative `IRMath::cameraMoveRelativeToYaw` inverts, so a middle-drag
     pan at non-zero yaw stopped tracking the cursor. `isoPixelToPos3D`'s depth
     argument shifts along `(1,1,1)`, which projects to `(0,0)`, so the focus's
     iso projection is depth-invariant and the pan identity survives at every
     latched depth. `test/render/camera_pan_pivot_test.cpp` carries the negative
     control (`WorldPointLatchBreaksThePanIdentity`, pinning the broken `(20,30)`
     shift) — a Phase 4 cursor latch that stores a world point re-introduces the
     same defect, and that test is the guard that should catch it.
  3. **#2669 is a live dependency, not background reading.** The default pivot's
     latch-update policy is under a pending ruling (proposal package,
     2026-08-01). Option 2 ("re-derive at rotation start") is the policy Phase 4
     already has by construction, so it is the outcome that costs Phase 4
     nothing; option 3 ("iterate to the fixed point") would put an N-readback
     loop on a per-derive path Phase 4 also touches. **Do not hand-roll a latch
     policy for the cursor pivot ahead of the ruling** — re-acquire on click, as
     §Phase 4 bullet 1 already says, and let the ruling settle the default
     pivot's behavior.
- **Supersedes:** §Phase 4 bullet 1's "the depth-probe center derivation (Phase 3
  helper)" only, which named a helper that did not exist yet and implied the
  wrong layer. Bullets 2 and 3 (the indicator entity, the shape_debug mode
  cycle) and all three §Phase 4 acceptance criteria are unchanged.
- **Acceptance criteria:** unchanged. Note that acceptance 1 asks for a
  `cursor-latch` block "driven via `setRotationPivotFocus` + castVoxelRay" —
  `setRotationPivotFocus` short-circuits the #2547 derive entirely
  (`updateDefaultRotationPivotFocus` early-returns when `m_hasRotationPivotFocus`
  is set), so that block is scored on the **silhouette** oracle like `focus-ctr`,
  not on `[pivot-focus-assert]`. Per A4 it belongs in the `PINNED` ≤1.5 px group.
- **Confirmed still valid (not stale):** **D3** — CPU picking never applied
  `s_k`, so the raster agrees with picking at cardinals 1–3 with zero picking
  changes; do not re-derive a picking compensation for `castVoxelRay`. **D6's
  standing consequence** — store anchor ≠ screen anchor on the per-axis route;
  PR #2585 touched neither. §Phase 4's other named surfaces
  (`system_camera_mouse_rotate.hpp`, `worldHitPos_`) are untouched by #2585.
- **By:** epic-steward — source: PR #2585 §Summary (bullets 1–3 and the
  background-fallback bullet) and §"Post-review amendments" (`c883c9e1`);
  verified on `origin/master` at `engine/render/include/irreden/ir_render.hpp:201,347`,
  `engine/render/include/irreden/render/ir_render_types.hpp:390`,
  `engine/prefabs/irreden/render/depth_probe.hpp:37-44,59,77`; ledger D3, D6, D7
  and issue #2669 §"Decision surface".
