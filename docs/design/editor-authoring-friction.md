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

### 2c — SessionBuilder, `--gui-session`, runner, ROCK — NEXT

Remaining Part 2 work, in order: `SessionBuilder` compiling authoring recipes to
`GuiInputEvent` streams via `worldPos3DToMouseScreenPx` **with a shadow
occupancy model** so it aims at exposed faces and steers clear of the rig / gizmo
occluders documented above (F-2b-1..3); `--gui-session <name>` selection;
`scripts/author-entity.py` (run → parse `GUI-ASSERT` → verify saves → copy to
`assets/voxel/entities/` → re-run byte-compare); then the ROCK session (sequence
the P0-3 drag-stroke probe here, and add the functional carve/erase asset checks
this slice's occlusion-free label check stops short of). Note: vertical picking
needs **zoom ≥ 2** (see P0-2) — session shots that pick by column must honour it.
The erase-fill mode (2b) is the carve primitive these recipes drive.
