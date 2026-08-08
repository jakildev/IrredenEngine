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

reconciled-through: ruling distribution 2026-08-08 (architect ruling
2026-08-05T01:37:22Z on the 2026-08-01 package → **D11**, **A7**, and
`issue-2669.md` **A2**). Code-side unchanged: PR #2659 merge
(2026-08-04T17:56:47Z, master `e640a5b1`) — P4 (#2548) reconciled, and no child
has merged since. **#2669 is the sole open child**; as of the ruling it is no
longer design-blocked — it is a planned, implementable task whose only remaining
gate is human triage (it carries **no labels**, so nothing queues it). The
close-out Findings F2–F6 gate independently.
proposal-pending: **none** — the 2026-08-01 package
https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5149886413
was **answered 2026-08-05** by the architect ruling
https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5186529073,
`fleet:steward-proposal` removed at 2026-08-05T01:37:24Z (the re-fire edge), and
**distributed 2026-08-08** as D11 + A7 here and A2 on `issue-2669.md`. The
distribution lagged the answer by three days: the re-fire edge is defined over
`design_prs[]`, and #2669 has no PR, so no projection trigger fires for an
**issue-scoped** ruling. Fed back. The **prior** package (2026-07-28) was
answered by the architect ruling
https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5106383295
(package: issuecomment-5100516659) and distributed 2026-07-29 as A2/A3 + the
`## Steward direction` on PR #2585.

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2545 | merged | #2562 | plan | 2026-07-28 (PR #2562 merge) |
| #2546 | merged | #2576 | plan | 2026-07-29 (PR #2576 merge) |
| #2547 | merged | #2585 | plan + A1–A2 | 2026-08-01 (PR #2585 merge) |
| #2548 | merged | #2659 | epic §Phase 4 + A5 (no child file) | 2026-08-04 (PR #2659 merge) |
| #2669 | open — **adopted 2026-08-01 (flow c)**; ruling landed 2026-08-05, plan is real, **unlabeled → not queued** | — | plan (A2; was stub + A1) | 2026-08-08 (ruling distribution) |

The PR column above was carrying `fleet:needs-windows-smoke` / `fleet:needs-human`
on the #2546 and #2547 rows. Those are volatile merge/review labels, which
`docs/agents/epic-steward-protocol.md` §"The Steward ledger" (#2398) excludes from
this column; they are dropped here. The state they encoded is not lost — **F5**
owns the "no OpenGL host has verified any phase of this epic" close-out gate and
names all three PRs.

A3, A4 and A6 are epic-scope (the whole-epic verification bar) and are not listed
per child; they bind every row above at close-out. A4 supersedes A3's block set
and verdict vocabulary; A6 supersedes A4's.

**Plan column, #2545–#2548:** these four children have **no `issue-<N>.md` file**
— their plan of record is §Phase 1–4 of *this* file, and their amendments (A1,
A2, A5) are in this file's §Amendments. Earlier revisions of this row read
`plan + A5`, which the protocol's ledger-backing rule
(`docs/agents/epic-steward-protocol.md` §"Flow b — post-merge follow-up" step 4,
#2571) reads as a claim that `.fleet/plans/issue-2548.md` exists with an `### A5`
heading. It does not, and #2548 is now merged, so materializing a stub for a
closed child would be noise — the column is restated instead. #2546 is the one
exception: it has a worker-authored `.fleet/plans/issue-2546.md`.

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
- D8 (2026-08-04): the Phase 4 **`cursor-latch`** block is scored on the
  pinned-point `[pivot-focus-assert]` oracle, **not** the whole-silhouette
  centroid — superseding A5's acceptance note, which put it in the `PINNED`
  ≤1.5 px group on the reasoning that an explicit `setRotationPivotFocus` makes
  it a `focus-ctr` twin. The refuting fact is what the latched focus *is*:
  `castVoxelRay` returns a **surface** hit while the analytic oracle predicts the
  winning voxel's **centre**, so the latched point sits off the probe's axis by up
  to the cell half-diagonal `sqrt(3)/2 ≈ 0.87` — and on this geometry the iso ray
  enters through the cell corner, so the bound is *tight* (0.87 measured against a
  1.0-world-unit tolerance, not D7's 0.6). An off-axis pin has no bias-free
  silhouette mapping, which is why the block first read DRIFT at 16 px under the
  centroid gate while its on-axis `center-axis` twin reads 12 px for the
  composite-depth-quantization reason A3 already records. Verified on master:
  `scripts/pivot-verify.py` `FOCUS_ASSERT_BLOCKS = DEFAULT_PIVOT_BLOCKS | {"cursor-latch"}`
  (line 84) and `CENTROID_GATED_BLOCKS = {"focus-ctr", "focus-off", "background-center"}`
  (line 89). **Not an escalation** — same shape as D7: A5's *substance*
  (`cursor-latch` proves the cursor resolves the right focus; the latched focus is
  byte-identical across all 9 shots) is honored and only its scoring choice is
  replaced, by a measurement rather than a preference. — source: PR #2659
  §Verification ("Why `FOCUS-OK` and not `PINNED`" / "Tolerance is 1.0 world unit,
  not 0.6").
- D9 (2026-08-04): the **cursor** pivot latches a **world point**
  (`IRRender::setRotationPivotFocus(CursorPivot::resolveFocusWorld(...))`,
  `system_camera_mouse_rotate.hpp:70`), and **A5 item 2's "latch the iso DEPTH,
  not the world point" does not extend to it.** A5's warning binds the *default*
  pivot's continuously re-derived focus, whose staleness window spans arbitrary
  camera motion (P3's `c883c9e1`); the cursor latch is **gesture-scoped** — taken
  on the mouse-DOWN edge (`:67-70`) and cleared by `clearRotationPivotFocus()` on
  release (`:93`) — and **no pan can begin inside that gesture**:
  `System<CAMERA_MOUSE_PAN>` only starts on the `middlePressed` *edge* with Ctrl
  not held (`system_camera_mouse_pan.hpp:29-32`), so even releasing Ctrl mid-drag
  cannot open one. There is therefore no window in which the latched world point
  can break `IRMath::cameraMoveRelativeToYaw`'s pan identity.
  `test/render/camera_pan_pivot_test.cpp`'s `WorldPointLatchBreaksThePanIdentity`
  remains the guard for the **default** path and is untouched by #2659. Standing
  consequence for #2669 and any future pivot work: "world point vs latched depth"
  is decided by **how long the focus is held across camera motion**, not by which
  pivot source acquired it. — source: verified on `origin/master` at the four
  cited call sites; A5 item 2; PR #2585 §"Post-review amendments".
- D10 (2026-08-04): §Phase 4 bullet 2's parenthetical "**and a short fade after
  release**" is **cut**, and acceptance 2's "despawns after release" is discharged
  by **hide, not destroy** (`hideIndicator` clears `SHAPE_FLAG_VISIBLE`;
  `SHAPES_TO_TRIXEL` skips a shape without that bit, so the marker renders zero
  pixels and the next drag reuses the entity). The fade is not implementable as a
  marker tweak on this render path, which is a fact about the shader rather than a
  preference: the winning-fragment path stores opaquely
  (`imageStore(triangleCanvasColors, canvasPixel, baseColor)`,
  `engine/render/src/shaders/c_shapes_to_trixel.glsl:1047`) and the **only** alpha
  blend in the pass is the hard-coded `kXrayOccludedAlpha = 0.25` on the branch
  where an xray fragment *loses* the depth contest (`:99, 1053-1063`), so ramping
  `C_ShapeDescriptor::color_.a` produces zero visible change for an unoccluded
  marker. A real fade needs a new per-shape alpha-blend capability on the winning
  path in **both** the GLSL and Metal twins plus backend-parity verification — a
  render-pipeline feature, filed free-standing (see F6). — source: PR #2659
  §"Acceptance evidence"; shader claim re-verified on `origin/master`.
- D11 (2026-08-05): **the default pivot's depth latch re-derives at rotation
  start** — option 2 of #2669's decision surface, ratified. The latch keeps
  #2585's pan/zoom-scoped derives and gains one more edge: the first frame yaw
  changes, whose previous frame was still and so holds a valid depth attachment.
  Three binding conditions: **gesture-start only** (no per-frame derives inside a
  continuous rotation; if that one flush later measures objectionable it is a new
  issue with its own measurement, not a re-litigation); **contract (A) is
  amended** from "pan/zoom-scoped latch" to "pan/zoom-scoped latch +
  rotation-start re-derive", with `docs/design/camera-yaw-pivot.md` naming both
  consequences it resolves — (a) pan-settle pop, masked inside the rotation that
  follows, and (b) post-rotate staleness, gone; and **verification must move the
  camera between derives** (a green sweep of the existing `pivot-verify.py` blocks
  is not evidence on this question — see F3). Grounds of record are the steward's
  two, adopted verbatim: cost asymmetry against option 3 (`depth_probe.hpp`
  documents the full-flush cost as the reason the probe stays single-pixel and
  debug-gated, so an N-flush fixed-point loop would build a per-derive production
  path on a primitive documented as too expensive for one), and pivot-source
  consistency with **D4** — the cursor latch already re-acquires at gesture start,
  so option 2 is that policy applied to the default pivot while option 1 would
  leave the two sources with different staleness behaviour, the exact fork D4 was
  chosen to avoid. This **supersedes D4's silence** on latch lifetime rather than
  D4 itself: D4 fixed *what* the default pivot pins (the surface point under the
  centre pixel); D11 fixes *when* that pin is refreshed. Consistent with **D9**
  ("world point vs latched depth is decided by how long the focus is held across
  camera motion"): shortening the default latch's lifetime to a gesture does not
  license relaxing its iso-depth latch, because the pan/zoom window remains.
  — source: architect ruling
  https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5186529073
  (answers the 2026-08-01 STEWARD PROPOSAL); distributed as A7 here and A2 on
  `.fleet/plans/issue-2669.md`.

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
- 2026-08-04: **#2548 (P4) merged via PR #2659** (2026-08-04T17:56:47Z, master
  `e640a5b1`, base `master` — the #2585 stack dependency discharged itself when
  the parent merged) — checklist ticked. **Every child except #2669 is now
  closed.** Scope-drift audit, three deltas, **no Decision contradicted**; two
  steward *amendment* clauses were deviated from, both with a measurement, both
  recorded above:
  - **A5 item 1 honored exactly.** The `castVoxelRay`-miss fallback is
    `IRRender::getDefaultRotationPivotFocus()` (`cursor_pivot.hpp:66`) — the
    `IRRender::` entry point A5 named, not a prefab-side re-derivation, so the
    #1960 N-tier decode is not forked.
  - **A5's `cursor-latch` scoring note superseded → D8.** The block ships on the
    pinned-point oracle, not the silhouette gate, because a `castVoxelRay`
    surface hit is off-axis by the cell half-diagonal. The author put it in
    `CENTROID_GATED_BLOCKS` first, measured DRIFT at 16 px, and moved it — the
    deviation arrives with the measurement that forced it.
  - **A5 item 2's "latch the depth, not the world point" does not bind the cursor
    path → D9.** The latch is gesture-scoped and no pan can start inside the
    gesture, so the pan-identity window A5 was protecting does not exist here.
    This is a scope correction to A5, not a defect in #2659.
  - **§Phase 4 bullet 2's fade cut → D10**, with the shader fact that forces it;
    the unowned capability is filed free-standing as **#2869** (see F6).
  - **In scope, additive:** `IRPrefab::CursorPivot` (`cursor_pivot.hpp`, Pattern B
    — nothing added to `RenderManager`), the `latchByDefault_` chord swap +
    `shape_debug` F9 binding (§Phase 4 bullet 3), the `cursor-latch` harness
    block, and `MainThread` on `registerSystem<CAMERA_MOUSE_ROTATE, ...>` (a
    review nit: `endTick` creates an entity and immediately `getComponent`s it).
    Review follow-up **#2681** (system-held indicator ids dangle across
    `resetGameplay`) is open and covers the pre-existing
    `system_perf_stats_overlay.hpp` twin as well — it is *not* a close-out gate
    for this epic, which is why it is not in Findings.
- 2026-08-04: **F1 discharged.** #2645 closed 2026-08-04T04:03:55Z via PR #2648,
  and it closed with exactly the measurement F1 demanded rather than the
  structural inference F1 refused to accept: `focus-ctr-sdf`'s `dev_x` is
  **flat at 2.00 px across zoom 1→16** while the voxel twin tracks 0.94–0.99,
  which rules out a rotation-anchor delta (a world-space offset must scale with
  zoom) and identifies the destination-grid quantum (1280x720 into a 2560x1440
  HiDPI framebuffer, `outputScaleFactor == 2`). Corroborated by the independent
  #2469 `--yaw-sweep` measurement already in `engine/render/CLAUDE.md`. The twin
  is now REPORTED, never gated (`SDF_GATED = False`, `pivot-verify.py:102`) —
  which is a *decision*, not a green: close-out cites the ungating and its
  measurement, not a PINNED verdict that will never exist for that block.
- 2026-08-04: downstream sibling re-validated against PR #2659 → **A1 on
  `.fleet/plans/issue-2669.md`**. #2669 is the only open child; it has no PR, so
  the merger-cooldown skip-guard has nothing to evaluate. Its stub's "One
  correction to carry into pickup" (`updateDefaultRotationPivotFocus`, not
  `…Depth`) survives — #2659 touched no `render_manager` surface.
- 2026-08-08: **the 2026-08-01 proposal was answered on 2026-08-05 and sat
  undistributed for three days.** The architect ratified option 2
  (issuecomment-5186529073) and removed `fleet:steward-proposal` at
  2026-08-05T01:37:24Z — the protocol's re-fire edge — but that edge is defined
  over the projection's `design_prs[]`, and **#2669 has no PR**. An issue-scoped
  ruling therefore fires no trigger at all: this umbrella's projection row read
  `[4/5]` with zero pending triggers for three days while a ratified,
  distributable answer sat on the thread. Found by reading `proposal-pending` in
  this ledger against the umbrella's live labels, not by any trigger. Distributed
  this iteration: **D11**, **A7**, and A2 on `.fleet/plans/issue-2669.md`; the
  `## Plan status` line there flips STUB → PLANNED and the do-not-claim gate is
  lifted. Fed back to `~/.fleet/feedback/epic-steward.md`.
- 2026-08-08: **#2669 is now the epic's only child gate, and it is a *triage*
  gate, not a design one.** With the ruling distributed it is an implementable
  task with a real plan and five acceptance criteria — but it carries **no
  labels at all** (filed 2026-07-30, unlabeled per `TASK-FILING.md`; adopted onto
  the checklist 2026-08-01 without ever being triaged), so `fleet-queue-ingest`
  never sees it and no worker can pick it. Nine days unlabeled. Raised on the
  umbrella; the steward does not stamp `human:approved` or class labels.

### Findings (close-out gate — beyond the checklist)
- **F1 — DISCHARGED 2026-08-04 (#2645 closed via PR #2648).** Was: `focus-ctr-sdf`
  is the only red pass, and its "pre-existing" attribution is inferred from
  structure rather than measured. #2648 supplied the measurement — `dev_x` flat at
  2.00 px across zoom 1→16 (a rotation-anchor delta must scale with zoom; a
  destination-grid quantization floor cannot), corroborated by the independent
  #2469 `--yaw-sweep` number in `engine/render/CLAUDE.md` — and un-gated the twin
  (`SDF_GATED = False`). **What close-out must cite is that decision plus its
  measurement, not a green verdict:** the 2.00 px reading is unchanged, so
  §Closing criteria's literal "all passes PINNED" stays unreachable for this
  block. A6 is the restatement of record.
- **F2 — #2641 is a systematic derive bias with cross-backend risk.** The derive
  reads exactly one iso-depth unit deep on camera-facing cap entries
  (`center-column`, `center-axis`); the lateral entry is within half a unit and
  the background fallback is exact. That `(1/3,1/3,1/3)` world-unit displacement
  is the *entire* 12 px (zoom 4) / 22 px (zoom 8) residual `center-axis` measures
  — still a ~12× improvement on the pre-#2547 focus's 150 px in the same
  configuration. Leading hypothesis is the trixel→framebuffer parity shift
  applied to the depth read on GL but not Metal, so §Closing criteria's "on both
  backends" is the binding clause.
- **F3 — RULED 2026-08-05 (D11 / A7); the *testing* clause survives as the
  close-out gate, and it is now stronger than a steward note.** Was: #2669 is
  unresolved and adopted. The design question is answered — option 2, re-derive at
  rotation start — but the ruling **adopts this finding's testing clause as an
  explicit close-out condition** ("no current `pivot-verify.py` block can observe
  either failure mode, so a green existing sweep is not evidence on this
  question"), so F3 does not discharge with the ruling. It discharges when a guard
  that pans-then-rotates exists and passes. #2669 remains open and now carries
  that guard as its acceptance criterion 4. Original text, still accurate: the
  guard has to **move the camera between derives**,
  which no current `pivot-verify.py` block does — close-out must not accept a
  green sweep as evidence on this question. **Re-checked 2026-08-04 against the
  new `cursor-latch` block: F3 stands.** That block parks a synthetic cursor on
  one pixel, latches **once**, and holds the latch across a 9-yaw sweep with the
  camera fixed — it proves the latch *holds*, which is the opposite of moving the
  camera between derives. `test/render/camera_pan_pivot_test.cpp` remains the
  closest existing vehicle.
- **F4 (standing, unchanged) — the jitter residual still has no owner.** #2427
  closed COMPLETED on a SMOOTH criterion and #2346 is closed, yet §Verification
  (whole epic) / §Closing criteria still demand SMOOTH pan/yaw sweeps. Recorded
  2026-07-29; nothing this iteration changes it.
- **F5 (standing, widened again 2026-08-04) — the OpenGL host has verified none of
  this.** #2576, #2585 and now **#2659** all carry `fleet:needs-windows-smoke` —
  every phase of this epic that touched a shader or a GPU readback is unverified
  on any OpenGL host, and P4 adds the indicator's `SHAPE_FLAG_XRAY_OCCLUDED` path
  (whose only alpha blend, per D10, is GLSL-side `kXrayOccludedAlpha`) to that
  set. #2641 is where the backend question lands. Labels are applied, so this is
  tracked, not invisible. §Closing criteria's "on both backends" is the binding
  clause and **cannot be discharged from macOS** — a Windows/Linux smoke verdict
  on all three PRs is a close-out input.
- **F6 (new 2026-08-04) — the cursor-pivot indicator's release fade has no
  implementation path, and now has an owner.** §Phase 4 bullet 2 asked for "a
  short fade after release"; it is cut per **D10** because `SHAPES_TO_TRIXEL`
  stores the winning fragment opaquely and exposes no per-shape alpha blend.
  Filed free-standing (unlabeled, human-triage — it is a **new capability**, not
  defect-shaped) as **#2869**. **Not a close-out gate:** the observable half of
  acceptance 2 (marker gone after release) is met, and no closing criterion names
  the fade. It is here so close-out records the criterion as *amended and owned*
  rather than silently met.

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

### A6 — 2026-08-04 — epic #2544 (whole-epic verification) — trigger: PR #2659 merged (P4 / #2548)

- **Decision:** the whole-epic verification bar, restated for the block set and
  verdict vocabulary as they ship after Phase 4. `python3 scripts/pivot-verify.py`
  now runs **eight** blocks under **three** different gates, and "all passes
  PINNED" names a verdict that four of them will never emit:

  | block | gate as shipped | passing verdict |
  |---|---|---|
  | `focus-ctr`, `focus-off`, `background-center` | whole-silhouette centroid, ≤1.5 px (`CENTROID_GATED_BLOCKS`) | `PINNED` |
  | `focus-ctr-sdf` | **ungated** (`SDF_GATED = False`, #2645/#2648) | `REPORT` |
  | `center-column`, `center-depth`, `center-axis` | `[pivot-focus-assert]` pinned-point, 0.6 world units | `FOCUS-OK` |
  | `cursor-latch` | `[pivot-focus-assert]` pinned-point, **1.0** world units (D8) | `FOCUS-OK` |

  The epic is verified when **every block is green under the gate it ships with**,
  at zoom 4 and 8, on **both** backends — plus the shape_debug and canvas_stress
  `render-verify` suites green, pan/yaw jitter sweeps SMOOTH (F4 still owns that
  clause), and clean exits. A silhouette number reported by an ungated or
  pinned-point-gated block is **evidence, not a gate**; close-out cites the
  decision that ungated it (D5/D7/D8, and #2648's zoom-invariance measurement for
  the SDF twin) rather than a verdict string.
- **Supersedes:** A4's block set and verdict table (adds `cursor-latch`; records
  the SDF twin's ungating, which post-dates A4), and A5's acceptance note placing
  `cursor-latch` in the `PINNED` ≤1.5 px group (D8). §Verification (whole epic)'s
  "all passes PINNED at zoom 4 and 8 on both backends" and the umbrella body's
  identical §Closing criteria sentence are the text this replaces — the umbrella
  body is **not** edited (the steward's carve-out covers the `## Children`
  checklist only), so this amendment is the restatement of record for close-out.
- **Acceptance criteria:** unchanged in substance. The two clauses A4 left open
  are unchanged and still bind: **both backends** (F5 — no OpenGL host has
  verified any phase of this epic) and **SMOOTH jitter sweeps** (F4 — no open
  issue owns the residual).
- **By:** epic-steward — source: PR #2659 §Verification; PR #2648 §Summary
  (zoom 1→16 table); verified on `origin/master` at `scripts/pivot-verify.py:75`
  (block list), `:84` (`FOCUS_ASSERT_BLOCKS`), `:89` (`CENTROID_GATED_BLOCKS`),
  `:102` (`SDF_GATED`); ledger D5, D7, D8.

### A7 — 2026-08-08 — epic #2544 (ratified contract + whole-epic verification) — trigger: proposal answered (architect ruling 2026-08-05)

- **Decision:** the epic's ratified **contract (A)** is amended, and the
  whole-epic verification bar gains one clause.

  **Contract.** D4 ratified that the default `CAMERA_CENTER` pivot pins *the
  surface point under the viewport-center pixel*. It said nothing about how long
  that pin survives camera motion, and #2585 shipped a **pan/zoom-scoped latch**
  by implementation choice rather than by ruling. Per **D11**, contract (A) now
  reads: *pan/zoom-scoped latch **+ rotation-start re-derive*** — the latch
  additionally re-derives on the first frame yaw changes. Gesture-start only; no
  per-frame derives inside a continuous rotation.

  **Verification.** §Verification (whole epic) / §Closing criteria gain a clause
  that A6's eight-block table cannot express, because the ruling states it as a
  property of the *harness* rather than of a block's verdict:

  > A green `pivot-verify.py` sweep is **not evidence** on the latch-update
  > question. Every block in that harness holds the camera fixed between derives,
  > so none of them can observe either failure mode the contract amendment
  > resolves. Before this epic closes, a guard must **pan** — changing the height
  > under the crosshair — **then rotate**, and assert the pivot depth was
  > re-derived at rotation start.

  This is the ruling's own third condition, adopted verbatim as a close-out
  condition, and it is the same clause **F3** has carried since 2026-08-01. F3
  therefore does **not** discharge with the ruling; it discharges with the guard.
  `test/render/camera_pan_pivot_test.cpp` is the closest existing vehicle; either
  it or a new `pivot-verify.py` block is a **new** artifact — re-checked against
  P4's `cursor-latch` block on 2026-08-04 and again here, no shipped block moves
  the camera between derives.
- **Supersedes:** D4's silence on latch lifetime, and §Phase 3 (B)'s / #2585's
  implicit "pan/zoom-scoped" reading of contract (A) — both now read with D11's
  rotation-start edge. A6's block table and verdict vocabulary are **untouched**
  and still bind; this amendment adds a clause A6 does not contain rather than
  restating it. The umbrella body is **not** edited (the carve-out covers the
  `## Children` checklist only), so this amendment plus A6 are the restatement of
  record for close-out.
- **Acceptance criteria:** A6's are unchanged and still bind (both backends — F5;
  SMOOTH jitter sweeps — F4). **Added:** the pan-then-rotate guard above (F3).
  The child that carries it is **#2669**, whose acceptance criteria are now
  enumerated in `.fleet/plans/issue-2669.md` **A2** — note that #2669 is
  unlabeled and therefore unqueued, so this criterion currently has a plan and no
  path to a worker.
- **By:** epic-steward — source: architect ruling
  https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5186529073
  §§"Conditions attached to the ruling" (all three); ledger D4, D9, D11; F3;
  distributed to the child as `.fleet/plans/issue-2669.md` A2.
