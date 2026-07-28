# Editor authoring-friction log — F-1.6 (#766)

Cumulative friction log for the F-1.6 proof-of-usability gate: authoring five
test entities (ant, bird, rock, mushroom, tree) **end-to-end through the
`IRVoxelEditor` GUI interaction harness** (scripted clicks/drags/hotkeys against
the live UI), per the plan in [`.fleet/plans/issue-766.md`](../../.fleet/plans/issue-766.md).

The point of the task is to prove the editor *works well* as an authoring tool —
so this doc records what fought each scripted session, what a human would
struggle with, and every missing tool or ergonomic gap discovered along the way.
Significant findings are filed as child issues (per
[TASK-FILING.md](../agents/TASK-FILING.md)). The doc is cumulative across the
task's PRs; each slice appends.

---

## Harness & tooling findings (cross-entity)

### H-1 — `verify_common.run_capture` crashes on non-UTF-8 subprocess bytes (FIXED, this PR)

**Symptom.** `scripts/gui-verify.py IRVoxelEditor` aborted mid-run on macOS with
`UnicodeDecodeError: 'utf-8' codec can't decode byte 0xd5` — despite the editor
itself running cleanly and emitting all `GUI-ASSERT … result=PASS` lines.

**Root cause.** `verify_common.run_capture` (the shared runner behind
gui-verify / render-verify / cull-verify / light-verify) opened the child's
stdout with `text=True`, i.e. **strict** UTF-8 decoding, and iterated it line by
line. The editor logs enumerated **system audio/MIDI device names verbatim** at
startup (`Device: Apple Inc.: <name>`), and one device on the host was named
`Robert's iPhone`, whose apostrophe is a Mac-Roman `0xd5` (U+2019 right single
quote) — not valid UTF-8. The stray byte raised `UnicodeDecodeError` inside the
`for line in proc.stdout` loop and took down the whole verify run.

**Why it matters for #766.** The `author-entity.py` session runner (PR-1) reuses
this exact `run_capture` flow to drive the editor and parse `GUI-ASSERT` lines.
Any macOS (or other) host with a non-ASCII device name in range would fail every
authoring session before a single assertion was read — an environment-dependent
false failure with a misleading traceback.

**Fix (in-scope harness gap).** `run_capture` now decodes with
`encoding="utf-8", errors="replace"`. The verify harnesses only ever grep for
ASCII markers (`GUI-ASSERT` / `PASS` / `FAIL` / screenshot paths), so replacing an
undecodable byte with U+FFFD in an unrelated device-name log line is loss-free.
Pre-existing gap (predates #766, shipped with `verify_common.py` in #2461);
fixed here because it blocks the runner this task builds on.

**Verified.** `gui-verify.py IRVoxelEditor` → `[gui-verify] 5/5 assertions
passed`, exit 0, on macOS/Metal.

---

## Platform-viability de-risk (macOS/Metal)

Before writing any session code, the plan's premise — that the fleet can author
these entities headlessly through the GUI harness, and that this works on macOS —
was confirmed empirically against current `origin/master`:

- **`IRVoxelEditor` builds clean on macOS/Metal** (no warnings; `fleet-build
  --target IRVoxelEditor`).
- **The GUI-test harness runs green headless on macOS.** All five existing
  assertions PASS: `HOVERS`, `CLICK_FIRES`, `CHECKBOX`, `SLIDER_VALUE`, and —
  the load-bearing one for this task — **`PICKS_VOXEL`** (`voxel=(-1,-1,-1)` for
  the empty-space pick baseline). This proves scripted mouse `MOVE`/`PRESS`/
  `RELEASE` injection drives the real UI, the picking/ray path is live, and the
  assertion evaluator reads true editor state.
- Confirms the plan's gotcha that "the assertion path is CPU-state,
  backend-agnostic; author + verify on either host" — macOS is a valid authoring
  host for this task; a Linux-GL parity run stays a smoke nice-to-have, not a gate.

---

## Phase 0 mechanism probe — status: RUN, GATE PASSED (macOS/Metal)

Phase 0 added probe shots to the editor GUI-test shot table (`editor_probe_*`,
appended after the stable framings so existing labels + the `editor_pick_voxel`
regression baseline are untouched). **Result: the auto-authoring premise holds.**
`gui-verify.py IRVoxelEditor` → `13/13 assertions passed`, exit 0, on
macOS/Metal. Findings per probe:

### P0-1 — keyboard→command dispatch (PASS)
Injected Ctrl+S (`PRESS LeftControl`, `PRESS S`, releases; Ctrl leads S by two
frames so the modifier is held when the S press drains). The editor logged
`Scene saved to data/editor_scene/scene` and wrote `scene_frame_0.vxs` (~52 KB).
Scripted keyboard **and modifier chords** drive the real command dispatch — the
runner clears stale `.vxs` pre-run and file-checks post-run.

### P0-2 — world→screen mapping accuracy (PASS — the gate)
This is the crux, and it passed 8/8. Key results:

- **The mapping is `IRRender::worldPos3DToMouseScreenPx(vec3)` (new, this PR),
  the exact inverse of the picking chain `mouseWorldPos3DAtIsoDepth`** — NOT the
  debug-overlay `worldToScreen` the groundwork cited. `worldToScreen` omits the
  letterbox offset, the framebuffer buffer-correction (`kSizeExtraPixelBuffer`),
  and the canvas-centre reference term that the *picking* read-chain applies, so
  it is off by a constant. The inverse reuses the picking chain's own live
  getters (zoom, iso offset, main-canvas size, step size, letterbox, buffer),
  so screen↔world stays consistent across backends and camera state with **zero
  hand-tuned constants** (the bail-path anti-pattern is avoided).
- **`GuiInputEvent.screenPx_` is window pixels, top-left origin, and reads back
  byte-identically** (`injPx == cursor`) — no retina/DPI scaling confound on
  the injected cursor. The GUI-canvas full-resolution setting only affects
  widget hitboxes; scene picking goes through the output-view/game-canvas chain,
  which `worldPos3DToMouseScreenPx` mirrors.
- **Vertical picking needs zoom ≥ 2.** At zoom 1.0 the iso step is `(2,1)` px
  per iso unit — only **1 px per iso-Y unit**, so the cell-centre half-offset
  is below integer-pixel resolution and `floor()` tips to the neighbouring iso
  column (a 1-cell miss). At zoom 2.0 the step doubles to `(4,2)` and all 8
  cells land on the exact target column. The probe shots therefore run at zoom
  2.0. **Authoring implication:** the SessionBuilder must author at zoom ≥ 2 for
  reliable per-cell vertical aim (or accept ±1-cell tolerance and aim at exposed
  faces, where placement-adjacent absorbs it).
- **Mapping accuracy is an iso-*column* property, asserted with a new
  `PICKS_ISO_COLUMN` GuiTest kind (this PR).** The seed scene is NOT just the
  ground plane: it also carries the skeleton rig voxel set (`31×3×3`) + joints,
  which `VOXEL_PICKING` walks as active geometry and which occlude the ground
  plane along many columns. So a click aimed at a ground cell correctly hits its
  iso column but returns whatever voxel is front-most on it — not necessarily
  the ground cell. Depth/occlusion is scene state, not a mapping fact, so the
  honest test compares iso projections (`pos3DtoPos2DIso(hit) ==
  pos3DtoPos2DIso(target)`). Every probe pick differed from its target by an
  exact multiple of the `(1,1,1)` view-ray direction (iso-invariant) —
  conclusive proof the mapping hits the intended column. In real authoring the
  builder aims at *exposed* faces (front-most by construction), where the
  stricter `PICKS_VOXEL` will be exact.

### P0-3 — drag stroke — DEFERRED to the session-infrastructure slice
The press→per-frame-MOVE→release → box-fill path is verifiable with the same
harness, but a placement-verify depends on box-fill's placement geometry (where
voxels land relative to the ray-hit face), which is cleaner to pin down once the
`worldPos3DToMouseScreenPx` primitive (validated here) drives real placement.
Sequenced as the first task of the session-infrastructure slice, not a gate.

### P0-4 — A/D binding overload (PASS, + bug found)
One injected `A` press logged `Added blank frame` — confirming the plain-PRESSED
`A` binding both adds an animation frame **and** starts a camera-left move (no
modifier guard). Sessions must re-establish the camera after any A/D frame op;
the per-shot camera re-apply already handles this. **Bug surfaced:** the add-frame
log line (`main.cpp` ~2314) uses printf `%d` inside the fmt-style `IR_LOG_INFO`,
so it prints the literal `Added blank frame %d / %d`. Pre-existing, cosmetic
(log-only); filed as a follow-up, not fixed in this de-risk PR.

### Reusable primitives this slice lands for the session infrastructure
- `IRRender::worldPos3DToMouseScreenPx(vec3)` — world voxel → click pixel; the
  aiming primitive the `SessionBuilder` needs.
- `IRPrefab::GuiTest::picksIsoColumn(...)` / `AssertKind::PICKS_ISO_COLUMN` —
  occlusion-robust mapping assertion for the harness.
- The `editor_probe_*` shot pattern (runtime-computed `MOVE` pixels filled in
  `onGuiAssertFrame` before the same-tick event injection, so the aim uses the
  shot's live camera state — no init-time viewport-timing fragility).

---

## Part 2 — session infrastructure + first entities

Builds on the Phase 0 primitives above. Sequence: (a) editor-authoring
prerequisites → (b) `SessionBuilder` + `--gui-session` + `scripts/author-entity.py`
runner → (c) the five entities (rock first). This slice appends as it lands.

### 2a — `--scene-size W H D` (LANDED)

The ant needs a 20³ grid and the tree ~26 tall, so the editable scene size is
now a runtime arg instead of the constexpr 16³. `kEditableSceneSize` /
`kEditableSceneOrigin` became `g_editableSceneSize` / `g_editableSceneOrigin`,
set in `main()` after the engine arg parse. `deriveSceneOrigin(size)` keeps the
scene centred in X/Y (`origin.{x,y} = -size.{x,y}/2`) and pins the seed ground
plane (local `z == size.z-1`) at **world z == 3 for any height**
(`origin.z = -(size.z-4)`), so authoring recipes and probe cells stay
height-agnostic — the ground never moves under the camera as the scene grows.

Friction notes for the session slice:
- **The seed ground plane is a saved slab.** `fillPlane(2, size.z-1, …)` puts a
  full gray layer in the scene; a clean entity save wants it erased first. Confirms
  the plan's call for an **erase-fill mode** (2b) to clear the slab and carve —
  single-voxel right-click is the only erase today, so clearing a 20×20 slab by
  hand is ~400 clicks.
- **The `editor_pick_voxel` baseline is 16³-specific** (a hardcoded expected
  voxel at a hardcoded pixel), so it is skipped under `--scene-size`. The
  `editor_probe_*` mapping shots are size-robust — their target z derives from
  `g_editableSceneSize.z-1` — and pass at 20³ (`gui-verify` default 13/13; a
  direct `--scene-size 20 20 20` run is CLEAN with all 8 mapping probes PASS).

### 2b — erase-fill mode (LANDED)

The left-click place / box / line / face-fill gestures now ERASE instead of
place while **erase-fill mode** is on (toggled with **V**; reported in the
fill-mode status label as an `ERASE ` prefix, e.g. `ERASE BOX`). Each fill path
passes `place = false` and aims at the hit voxel itself rather than the empty
cell adjacent to the hit face; right-click single-voxel erase is unchanged. This
fills the Phase-1 gap where a single-voxel right-click was the only erase —
clearing the seeded ground slab by hand was ~one right-click per cell — and is
the carve primitive the session-authoring recipes need.

Verified by a new `editor_probe_erase` GUI-test shot: synthetic **V** flips the
mode ON and the capture-frame assertion confirms `g_eraseMode` is set and the
status label reads `ERASE BOX` (`gui-verify.py IRVoxelEditor` → 14/14, macOS/
Metal). The check is **occlusion-free** (no scene click) by design — see the
findings below for why a scripted-erase-removes-a-voxel check is deferred.

Findings surfaced while trying to verify the erase functionally through a
scripted click on the seed scene (all inform the Part 2c SessionBuilder):

- **F-2b-1 — the posed starter rig blankets the central ground plane.** The
  F-2.5 starter rig (a skinned 31×3×3 bar) projects across a wide iso band at
  z=2, one step toward the camera from the ground plane at z=3, so a click aimed
  at almost any interior ground cell hits a rig voxel first, not the ground.
  Empirically every interior + edge column tried (world (0,4), (0,7), (4,-2))
  returned the rig entity. Authoring the ground directly by click is only
  reliable off the rig's iso footprint — which the SessionBuilder must model
  (or the recipe must clear/reframe first).
- **F-2b-2 — skinned voxel sets are not click-erasable.** Even when the click
  lands squarely on the rig, the edit is a silent no-op: the rig is skinned, so
  the pick's *world* voxel position doesn't map back to a static local index
  (`worldVoxelToLocal` subtracts the set's `C_WorldTransform` origin, which the
  per-bone skin transform doesn't honour). So place/erase on a skinned set finds
  no cell and drops the edit. Editing skinned content by clicking the deformed
  result is not supported today; the authoring recipes operate on the static
  editable set only.
- **F-2b-3 — perimeter gizmos consume clicks over a wide screen area.** The
  reference gizmos are screen-space-sized, so their handles occupy a large pixel
  radius; a scripted click aimed at a ground cell near a gizmo (e.g. world
  (-4,-4) vs the scale gizmo at (-12,-12)) is swallowed by `GIZMO_DRAG` before
  the place/erase system sees it. Session shots must aim clear of the gizmo
  screen footprints.
- **Fixed in passing:** the `V` toggle's log line used a printf `%s` inside the
  fmt-style `IR_LOG_INFO` (printed the literal `%s`); corrected to `{}` — same
  class as the A/D add-frame log bug already filed as #2491.

### 2c — SessionBuilder + `--gui-session` + the P0-3 drag stroke (LANDED)

The authoring spine. A **recipe** names cells; `SessionBuilder`
(`creations/editors/voxel_editor/session_builder.hpp`) works out which face of
which already-placed voxel to click, aims the cursor with
`worldPos3DToMouseScreenPx`, and emits the MOVE / PRESS / RELEASE stream the
GUI-test harness replays against the live UI. No recipe touches voxel storage —
every voxel lands because a scripted click ran the editor's own place/erase path.
`--gui-session <name>` selects one (`sessions.hpp` holds the registry);
`gui-verify.py` now forwards target args after a `--` separator, so a session
runs through the standard runner:

```
python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session drag_probe
```

**Shadow occupancy model.** `OccupancyModel` mirrors the editable set as the
recipe grows it and replays `castVoxelRay`'s front-to-back walk over that mirror,
so an aim that would be occluded is caught while the recipe is being *built*
rather than as a mystery FAIL — or a silent no-op — at run time. An unaimable
gesture aborts the run with the offending cell named, because a session that
quietly drops an op authors the wrong entity and saves it anyway.

**Positive-fire assertions.** Each segment asserts the *live* set's occupancy at
its capture frame, so a swallowed gesture fails loudly. These go through a new
`AssertKind::PREDICATE` (`gui_test_assertions.hpp`) — a creation-supplied
`bool(context, actual)` — so creation-specific state checks reuse the harness's
single `GUI-ASSERT` emitter instead of hand-rolling the log line. The 2b erase
probe moved onto it, retiring the one hand-rolled emitter that existed.

**Result: `drag_probe` is 11/11 PASS on macOS/Metal** (`gui-verify` exit 0), and
the standing shot table is unchanged at 14/14. The session also passes at
`--scene-size 20 20 20` (cells derive from the live scene dims), so the ant's
20³ framing is covered. **P0-3 — the drag stroke deferred out of Phase 0 — is
resolved**: press → move → release commits a four-cell box fill, verified by
per-cell occupancy rather than a hover proxy.

Findings:

- **F-2c-1 — the shipped editor's own reference scene makes the ground plane
  unauthorable. Measured, not inferred.** Running `drag_probe` against the
  default scene: **every** gesture is swallowed — place FAIL, all four drag cells
  empty, and the aim assertion reports `voxel=(-1,-1,2)`, a *shape* hit. Root
  cause: `castVoxelRay` tests SDF shapes before voxel sets at each depth step,
  and a shape hit returns `faceNormal_ == (0,0,0)`, which every branch of the
  place/erase driver drops (`if (hit && hit->faceNormal_ != ivec3(0))`). The demo
  floor slab alone (`vec4(40,40,1)` at world z=2, so it spans z ∈ [1,3]) covers
  the whole seeded ground plane at z=3 from the camera side. So this is not a
  harness artifact — **a human clicking the ground in the shipped editor is
  clicking through a slab that eats the event**, with no feedback. Sessions
  therefore build the scene without the demo furniture (floor slab, axis bars,
  centre cube, perimeter gizmos, starter rig, satellite sets); with it removed
  the same recipe is 11/11. This supersedes F-2b-1's diagnosis: the starter rig
  is *a* central-ground occluder, but the floor slab is the total one. Filed as a
  child issue — the editor should either exclude non-editable reference shapes
  from picking or fall back to the voxel-set hit behind them.
- **F-2c-2 — face-accurate aiming needs zoom ≥ 3; P0-2's zoom ≥ 2 floor only
  covers the column.** A face-centre aim sits half a column-spacing off the voxel
  centre in screen space, so below zoom 3 it rounds onto the *neighbouring* iso
  column and the click edits the wrong cell. Measured by sweeping `kSessionZoom`
  against this session: zoom 2 → 2 FAIL (the side-face erase aim lands on the
  adjacent ground column, `voxel=(0,1,3)` vs target `(0,0,2)`); zoom 3 / 4 / 8 →
  11/11. Sessions author at **zoom 4** (floor plus one step of margin). Entity
  recipes must not zoom out below that for a wider framing.
- **F-2c-3 — one screenshot per segment is the session cost knob.** The harness
  captures a PNG per shot, and a segment is a shot. The four-segment probe is
  cheap; a per-op segmentation of the ant would be thousands of captures. Ops
  therefore pack into frame offsets *within* a segment, and a segment boundary is
  only spent where the recipe wants an assertion checkpoint or a camera re-apply
  (after A/D, per P0-4).
- **F-2c-4 — an erase gesture is not diagnosable from occupancy alone.** The
  first `drag_probe` run failed the carve with nothing but "cell still occupied",
  which is equally consistent with a bad aim, an occluded ray, or the gesture
  never reaching the erase path. Splitting it into a `hover` + `PICKS_VOXEL`
  segment followed by the click named the cause in one run (the aim was on the
  wrong column). `hover` / `expectPick` are now part of the op vocabulary; entity
  recipes should arm a novel gesture with a pick assertion before trusting it.

### 2d — `author-entity.py` + ROCK (LANDED)

The runner and the first committed entity. `scripts/author-entity.py` replays a
`--gui-session <name>` recipe through the same `run_capture` flow `gui-verify`
uses, then: parses `GUI-ASSERT` (fails on any FAIL / zero asserts), confirms the
editor wrote `scene_frame_*.vxs`, **re-runs and byte-compares the saved set**
(the acceptance's determinism gate), and copies the frames to
`assets/voxel/entities/<entity>[_frame_N].vxs(+.json)`. The `GUI-ASSERT` regex +
pass/fail table moved to `verify_common` so the runner and `gui-verify` share one
parser.

The **ROCK** — an irregular, no-symmetry, single-layer blob — is authored by:
clearing the seeded ground slab down to a 5×5 central footprint (four erase-mode
box drags), building three asymmetric layers on that footprint, and carving two
base corners. `author-entity.py rock` → **18/18 GUI-ASSERT PASS, determinism gate
byte-identical, `rock.vxs` = 65 occupied voxels** in a compact 5×5×5 bounds
(footprint 25 + base 23 + mid 12 + cap 4 + peak 1). Renders as a small rock in
`IRShapeDebug --load-vxs`.

The two open questions from the 2c handoff are answered:

- **The ground slab erases with a `dragBox` sweep in erase mode — no
  select-all-plane affordance needed.** Four erase boxes frame the footprint;
  each drag clears a whole grid-AABB strip from just its two corner clicks
  (`applyFillAABB` fills the AABB between the hit cells, so cells *inside* the
  box are cleared without being individually clicked). Run **first**, while the
  ground is a single flat layer — once rock voxels sit above the plane they
  occlude the `(1,1,1)` march to the cells diagonally behind them.
- **`save()` lands at `<exe-dir>/data/editor_scene/scene_frame_N.vxs`.** ir-run
  `cd`s into the exe dir before launch, so the DENSE save (`kSceneSaveDir`,
  exe-relative) resolves there; the runner finds the exe under the build dir and
  reads that directory.

Findings:

- **F-2d-1 — the left GUI panels do NOT occlude the ground plane in session
  mode (at zoom 4, 16³).** The 2b/2c notes flagged the left-column panels and
  perimeter gizmos as click-swallowers, so the recipe instrumented all four
  ground corners + two edge midpoints with `cleared` checks. **All eight
  passed** — the entire 16×16 plane was reachable, including the far corners.
  Session mode strips the furniture (F-2c-1) and the panels sit clear of the
  centred scene's screen footprint at the authoring zoom, so a recipe can erase
  or place anywhere on the plane. (A much larger `--scene-size` could still push
  the plane edges under the panels; re-instrument if an entity needs one.)
- **F-2d-2 — a pick assertion must fire in a segment *before* the click that
  edits its own target.** The first rock draft put the carve's `hover` +
  `expectPick` in the same segment as the erase `click`; assertions evaluate at
  the segment's capture frame, which is *after* the click removed the voxel, so
  `castVoxelRay` fell through to the footprint cell behind it and the pick read
  the wrong voxel while the carve itself passed. Splitting the arm (hover + pick)
  from the erase — the pattern `drag_probe` already used — fixes it. A generic
  rule for entity recipes: an `expectPick` that names a cell an upcoming edit
  will change belongs in its own segment ahead of that edit.
- **F-2d-3 — an erased voxel keeps its RGB with alpha zeroed; the DENSE `.vxs`
  stores every slot.** `rock.vxs` is 50 KB (all 4096 slots × 12 B) but only 65
  slots are `alpha != 0`; the renderer (and the VRLE "empty when alpha==0"
  convention) skip the rest. An occupancy audit of a saved asset must test the
  packed-RGBA alpha byte, not "record is non-zero" (the erased slots retain a
  non-zero colour). Not an editor defect — just the format's empty encoding.

### 2e — MUSHROOM (symmetry fix) — NEXT

The plan's PR-2: wire `applyMirrors` into the edit paths (the F-1.2 regression,
`symmetry.hpp:24` still has zero call sites) and author the mushroom with X+Y
mirrors + two layers as the fix's positive-fire regression test. The runner,
erase mode, and the aiming spine are all in place; the mushroom adds mirror +
layer ops (`tapKey` for X/Y/K already exists on the `Builder`).

### M-2 — `-y`-face single-click place no-ops at the cardinal (yaw-0) camera (#2575)

Surfaced authoring the mushroom (2e). Growing a voxel outward by clicking the
exposed face of a neighbour, at the editor's cardinal (yaw-0) camera and
authoring zoom (`kSessionZoom = 4`):

- A `-x`-face or `-z`-face single click places the intended neighbour every time.
- A `-y`-face single click **no-ops** — the intended `-y` neighbour is never
  placed, with no feedback.

| primary click | anchored face | result |
|---|---|---|
| `(6,7,11)` | `-x` of `(7,7,11)` | placed ✓ |
| `(7,6,11)` | `-y` of `(7,7,11)` | **empty ✗** |
| `(7,5,11)` | `-y` of `(7,6,11)` | **empty ✗** |

**Root cause — the picker's iso-pixel floor collapses `-x` and `-y` face aims
onto the same pixel at cardinal yaw.** The place path is `hit.voxelPos_ +
hit.faceNormal_` (`voxel_editor/main.cpp`), where `hit.faceNormal_` comes from
`IRPrefab::Picking::detail::voxelHitFaceNormal(worldHitPos - voxelCenter)` — the
dominant axis of the ray's entry offset, with tie-break priority **x > y > z**.
The cursor→ray inverse (`IRRender::mouseWorldPos3DAtIsoDepth`) **floors** the iso
pixel to the integer lattice: `isoPixelToPos3D(floor(canvasIso.x),
floor(canvasIso.y), depth)`, and the forward aim (`worldPos3DToMouseScreenPx`)
adds `+0.5` (aim-at-cell-centre). The iso projection is `iso.x = -x + y`,
`iso.y = -x - y + 2z`.

For a face aim `center + normal·kFaceAimDepth` (`kFaceAimDepth = 0.4`), the
recovered floored iso pixel is `floor(pos3DtoPos2DIso(aim) + 0.5)`:

| face of `(7,7,11)` | aim | `pos3DtoPos2DIso(aim)` | floored pixel |
|---|---|---|---|
| `-x` | `(6.6, 7, 11)` | `(0.4, 8.4)` | **`(0, 8)`** |
| `-y` | `(7, 6.6, 11)` | `(-0.4, 8.4)` | **`(0, 8)`** |
| `-z` | `(7, 7, 10.6)` | `(0, 7.2)` | `(0, 7)` |

The `-x` and `-y` aims land on the **identical** pixel `(0, 8)` — which is also
the voxel *centre's* pixel — because their iso offsets (`±0.4` in `iso.x`, from
the coefficient-1 `x`/`y` terms) are smaller than the `+0.5` cell-centre bias, so
the floor snaps both back to the centre column. `-z` escapes only because
`iso.y`'s `2z` coefficient turns a `-0.4` z-offset into a `-0.8` iso shift, large
enough to cross the floor boundary onto a distinct column. On the collapsed
centre column the ray marches through the voxel centre and enters at the
`(1,1,1)` corner, so `worldHitPos - voxelCenter` is **parallel to `(1,1,1)`** —
`|Δx| = |Δy| = |Δz|` — a three-way tie that `voxelHitFaceNormal` resolves to
`-x`. That is *correct* for a `-x` click (coincidentally) and *wrong* for a `-y`
click (it should be `(0,-1,0)`), so the `-y` neighbour cell stays empty and the
edit is dropped. (For `cx != cy`, e.g. `-y` of `(7,6,11)`, the aim collapses onto
that voxel's own centre pixel `(-1, 9)` the same way — the offset is still
parallel to `(1,1,1)`, so the tie still resolves to `-x`.)

**A/B repro (no editor run required).** `A = -x` aim `(6.6,7,11)` and `B = -y`
aim `(7,6.6,11)` both reduce to floored iso pixel `(0,8)` under
`floor(pos3DtoPos2DIso(aim) + (0.5,0.5))`. The picker receives byte-identical
input for A and B, so it *cannot* return different results for them — the `-y`
distinction is destroyed at the floor, not merely mis-resolved downstream.

**This is a genuine projection/quantization limitation, not a fixable
face-derivation bug.** At the picker's integer-iso-pixel resolution a voxel's
`-x` and `-y` faces map to the same picking pixel at cardinal yaw, so no
tie-break rule or `Δ`-based heuristic can separate them (the inputs are equal).
Disambiguating would require sub-iso-pixel picking precision — a change to the
`mouseWorldPos3DAtIsoDepth` / `worldPos3DToMouseScreenPx` round-trip contract
that every editor picking consumer (live click, hover, gizmo drag, the session
shadow model) depends on — out of scope for this friction fix and a design-level
change to the load-bearing picker.

**Authoring path (per #2575 acceptance).**

- **Hand-authoring:** rotate the camera one cardinal step with `Q`/`E` before
  the click. Under a `±90°` yaw the target's `-y` face presents as an `-x`- or
  `-z`-facing face, which the picker resolves cleanly; place, then rotate back.
- **Symmetric solids:** take `-y` thickness from the **Y mirror** rather than a
  raw `-y` click — the mushroom (2e) does exactly this (cap grown in `-x`/`-z`,
  y-thickness mirrored). The F-1.2 mirror path fans a `-x`/`-z` primary click out
  to both Y sides, so no `-y` primary click is needed.
- **Session recipes:** `aimToPlace` already tries `-x` before `-y`, so any target
  with a `+x`-side or `+z`-side anchor is placed through a clean face
  automatically; only a target whose *sole* occupied anchor is on the `+y` side
  hits this wall.

**Recommended follow-up (#2578, agent-approved lane).** The session shadow model
(`OccupancyModel::pick`, `session_builder.hpp`) marches the *exact* aim ray, so
it does **not** reproduce the picker's iso-pixel floor and greenlights a `-y` aim
whose live click misfires — the silent-no-op source. A hardening pass should make
`pick` reproduce the floored-iso march + `voxelHitFaceNormal`, and have
`aimToPlace` reject an aim whose predicted face normal disagrees with the
intended one (so it either finds a disambiguable anchor or records an
`unreachable` recipe error, instead of emitting a click that no-ops).
