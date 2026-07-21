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

## Phase 0 mechanism probe — status: NOT YET RUN (next slice)

Phase 0 (per the plan) extends the editor shot table with four probes that gate
the rest of the task. Groundwork located this slice, to be implemented next:

1. **Keyboard→command dispatch** — inject Ctrl+S; assert
   `data/editor_scene/scene_frame_0.vxs` exists post-run (runner-side file check).
   *Lowest risk; no coordinate mapping needed.*
2. **World→screen mapping accuracy** — the crux. Compute screen px for ~8
   ground-plane cells' top faces at one fixed zoom/yaw → `MOVE` + `PICKS_VOXEL`
   each; expect 8/8 PASS. Reference forward-projection:
   `worldToScreen(vec3)` at
   `engine/prefabs/irreden/render/systems/system_debug_overlay.hpp:181`
   (uses `pos3DtoPos2DIso` + `IRRender::getCameraPosition2DIso()` +
   `getTriangleStepSizeScreen()` + viewport). **Gotcha:** GUI panels render on
   the full-resolution GUI canvas (`setGuiCanvasFullResolution`, gui_scale=2)
   while the scene lives in game-canvas space — the `GuiInputEvent.screenPx_`
   window coordinate and the `worldToScreen` game-canvas output are *not* the
   same space; the mapping must reconcile them. The seed ground plane is at
   `z == size_.z - 1` (`main.cpp` `fillPlane(2, set.size_.z-1, …)`), the
   editable set is 16³ by default at `kEditableSceneOrigin`.
3. **Drag stroke** — `PRESS` + per-frame `MOVE`s + `RELEASE` → box-fill fires;
   verify by hovering a newly placed voxel (`PICKS_VOXEL` occupancy proxy).
4. **A/D overload measurement** — inject `A`; expect frame count 2 and record the
   camera delta (A/D frame-add/duplicate share the WASD camera bindings, no
   modifier guard — `main.cpp:2130-2155` vs `2290,2326`).

**Bail path (opus judgment).** If probe (2) cannot hit target cells reliably at
*any* zoom (GUI-canvas scaling / rounding), STOP — post the measurements on #766
and design-block. Do **not** fall back to hand-tuned pixel constants for five
entities; that defeats the "author by really using the editor" premise.
