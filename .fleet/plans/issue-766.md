<!--
Plan file for #766 (per #1932 — committed as the first commit of the
implementer's PR). This is the `## Plan` comment posted on issue #766 on
2026-07-15 and cleared `fleet:plan-review` SOUND the same day (fleet-plan-lint
exit 0; verified-current-state spot-checked 9/9 against origin/master). Reproduced
verbatim below; the issue thread is the canonical source.
-->

## Plan: editor: F-1.6 — first authored entities via GUI harness (ant, bird, rock, mushroom, tree)

- **Issue:** #766
- **Model:** opus (all PRs)
- **Date:** 2026-07-15

### Verified current state (all on current `origin/master`)

**Harness (epic #1793, shipped):**
- The GUI-test harness is already wired to `IRVoxelEditor`: 6-shot `kGuiTestShots[]` + assertion tables (`creations/editors/voxel_editor/main.cpp:328-346, 2086-2098, 3263-3277`), `GUI-ASSERT` PASS/FAIL lines parsed by `scripts/gui-verify.py`.
- Input vocabulary (`GuiInputEvent`, `engine/video/include/irreden/video/auto_screenshot.hpp:163-169`): MOVE (screen px), PRESS/RELEASE, SCROLL. `button_` is the **unified keyboard+mouse `KeyMouseButtons` enum** (`engine/input/include/irreden/input/ir_input_types.hpp:45`), and injected buttons drive the same state machine as GLFW (`input_manager.cpp:195-213`), with modifier chords derived from held-button state (`ir_input.cpp:22-40`) — so **hotkeys and Ctrl/Shift chords are injectable today**. Gaps: **no text/char injection** (text widgets untypeable headless), **drag strokes expressible but never yet exercised** (PRESS + per-frame MOVEs + RELEASE), no drag/save-path assertion kinds, camera is per-shot static framing only.
- Projection math for computing click coordinates exists: `IRMath::pos3DtoPos2DIsoYawed` (`engine/math/include/irreden/ir_math.hpp:670`), composite screen mapping documented at `ir_math.hpp:1044-1070`, working reference `worldToScreen(vec3)` in `engine/prefabs/irreden/render/systems/system_debug_overlay.hpp:181`.

**Editor surface:**
- Tools: single place (left click, lands adjacent to the ray-hit face — **no free-space placement**; seed surface is the pre-filled ground plane at z=15, `main.cpp:2943-2946`), erase (right-click press, single voxel only), box-fill (left-drag), line-fill (Shift+left-drag), face-flood (Ctrl+left-click), SDF bake panel, loft. No brush sizes. Full hotkey sheet logged at `main.cpp:1100-1119`.
- Save/load is **hotkey-only, no dialog**: Ctrl+S writes one DENSE `.vxs` per animation frame to the hardcoded `data/editor_scene/scene_frame_{N}.vxs` (`scene_io.hpp:37-38, 61-63, 126-169`); `.vxs.json` sidecars auto-emitted (write-only, regenerated); frame 0 META carries fps/loop/frame-count/symmetry/layer table.
- **Defect (F-1.2 regression): symmetry mirroring is never applied to edits.** `applyMirrors` (`creations/editors/voxel_editor/symmetry.hpp:24`) has **zero call sites** in the tree; the X/Y/Z toggles change only the status label and saved META. Ant + mushroom acceptance requires working mirrors. Probable regression window: the #2166 carve-site migration (c0863e88); implementer should confirm.
- **A/D key overload:** frame add / duplicate are plain-PRESSED bindings (`main.cpp:2290, 2326`) with no modifier guard, colliding with the WASD camera bindings (`main.cpp:2130-2155`) — one A press adds a frame AND nudges the camera. Deterministic, but sessions must re-establish camera after frame ops (per-shot camera re-apply handles this).
- **Editable scene is constexpr 16³** (`main.cpp:123`) — too small for the ant (20³) and tree (~24 tall).
- Symmetry modes are X/Y/Z mirror planes only (no rotational mode) — mushroom uses X+Y mirrors as the 4-fold radial approximation the issue already allows.
- Layers: create (`K`/`+`), select (`[`/`]`/click), visibility (`H`/checkbox); **no rename UI** (would need text entry — also blocked by the no-char-injection gap). Auto-names satisfy the "distinct layers" acceptance structurally.
- Determinism: no rand/wall-clock in the editor; animation playback (`P`) uses render deltaTime → **keep paused, step frames with Left/Right**; never inject Escape (closes window); erase must be a quick right press/release (right-drag rotates the camera).

**Asset side:**
- `IRShapeDebug --load-vxs` loads a single DENSE file, frame 0, static (`creations/demos/shape_debug/main.cpp:1513-1534`); **no voxel frame playback exists anywhere in the engine** (`dense_bridge.hpp:41-63` drops `frames_`/`layers_`/`meta_`). The bird's playback acceptance needs a small demo-side addition.
- No `.vxs` committed anywhere; no `assets/voxel/` yet (existing convention: `assets/demos/<name>/`). This task establishes `assets/voxel/entities/`.

**Sibling/in-flight reconciliation:** #604 sub-tasks 1.1–1.5 all shipped (#762/#764/#765 closed); no open PR touches the editor, harness, or shape_debug surfaces (checked open-PR list + `git log` on those paths). No cross-system audit section: nothing shared is deleted or migrated.

### Approach

**Core design — recipes compiled to real input.** Each entity gets a checked-in **authoring session**: a compact recipe (place voxel at V with palette color c / box-fill V1..V2 / toggle X mirror / add layer / duplicate frame / save) that a `SessionBuilder` expands **at startup** into `GuiInputEvent` streams — real cursor moves, presses, releases, and hotkey chords, aimed via the engine's own world→screen mapping (`pos3DtoPos2DIsoYawed` + camera state per segment). Every edit flows through the live UI paths (picking, tools, palette clicks, layer widgets, frame keys, Ctrl+S). **No direct writes to voxel storage, no scene_io calls outside the editor's own Ctrl+S handler.** The builder keeps a shadow occupancy model solely to aim clicks at exposed faces (the planning a human's eyes do); interleaved `PICKS_VOXEL`/hover assertions after each segment make the run FAIL fast if actual editor state diverges from the recipe — divergence is exactly the editor-defect signal this proof exists to surface. Sessions are selected with a new `--gui-session <name>` editor flag and live as source files under `creations/editors/voxel_editor/test/sessions/`.

**Phase 0 — mechanism probe (first commit of PR-1).** Extend the existing shot table with probe shots; expected readings gate the rest:
1. *Keyboard→command dispatch:* inject Ctrl+S; expect `data/editor_scene/scene_frame_0.vxs` to exist post-run (runner-side file check).
2. *Mapping accuracy:* computed screen px for ~8 ground-plane cells' top faces at one fixed zoom/yaw → MOVE + `PICKS_VOXEL(expected)` each. Expect 8/8 PASS.
3. *Drag stroke:* PRESS + MOVEs + RELEASE across 4 cells → box-fill fires; verify by hovering a newly placed voxel (`PICKS_VOXEL` as occupancy proxy).
4. *A/D overload measurement:* inject A; expect frame count 2 and record the camera delta.
**Bail path:** if (2) cannot hit target cells reliably at any zoom (GUI-canvas scaling/rounding), STOP — post the measurements on #766 and design-block. Do not fall back to hand-tuned pixel constants for five entities.

**PR-1 — session infrastructure + rock.**
- Editor: `--scene-size X Y Z` arg (IRArgs pattern as in shape_debug), default 16³ unchanged.
- Editor: **erase-fill mode** — a mode toggle (suggest `V`) flipping place↔erase semantics of the click/box/line/face fill paths, reported in the status label. Required to clear the seeded ground slab before save and to carve the rock; also fills a genuine Phase-1 gap (single-voxel right-click is the only erase today).
- `SessionBuilder` + session registry + `--gui-session <name>`; segments map to shots so camera re-applies deterministically (and after A/D).
- Runner `scripts/author-entity.py` (reusing the gui-verify flow): build → run session → parse `GUI-ASSERT` → verify save files → copy `scene_frame_*.vxs(+.json)` to `assets/voxel/entities/<entity>[_frame_N].vxs` → **re-run and byte-compare** the saved set (determinism gate).
- **ROCK** session (no symmetry/layers/frames): box-fill blob + erase-carve irregularities + ground-slab erase + Ctrl+S. Commit assets, ShapeDebug screenshot.
- Start the cumulative friction doc `docs/design/editor-authoring-friction.md`.

**PR-2 — symmetry fix + mushroom.**
- Wire `applyMirrors` into the place/erase/fill paths (per-axis offsets honored; mirrored voxels join the same undo record). This fixes the F-1.2 regression; the mushroom session is its positive-fire regression test (a mirror-side voxel is hover-asserted).
- **MUSHROOM** session: X+Y mirrors, 2 layers (stem=default, cap added via `K`), visibility checkbox exercised, save + reload (Ctrl+O) round-trip check.

**PR-3 — ant.**
- **ANT** session at `--scene-size 20 20 20`: X mirror; layers head/thorax/abdomen/legs (`K`, `[`/`]`); six legs as 3 mirrored pairs; the largest session, exercising everything from PR-1/2.

**PR-4 — bird + tree + playback + closeout.**
- shape_debug: extend `--load-vxs` to detect `_frame_*.vxs` siblings, load all frames, swap voxel-set contents on a fixed tick cadence plus a step mode for screenshots. Demo-side only; no new engine system, `dense_bridge` stays frame-agnostic.
- **BIRD** session (default 16³): frame 0 wings-up → `D` duplicate → edit wings-down → set FPS slider → save while paused. Two frames ⇒ two `.vxs`+sidecar pairs; the acceptance's "pairs" reads per frame file for the bird.
- **TREE** session at `--scene-size 16 16 26`: trunk line-fills, foliage box-fills + carve.
- Final friction doc pass; file child issues (unlabeled, per TASK-FILING.md) for each significant finding. Already known candidates: F-1.2 mirror regression backstop test, A/D binding overload ergonomics, no layer-rename UI + no char-injection path, ground-plane-in-asset design (proper backdrop/seed surface vs saved slab — PR-1's erase workaround is honest but the design question deserves its own ticket).

**Task shape: one task, four sequential PRs** (the issue body explicitly allows grouping). Only PR-4 carries `Closes #766`; PR-1..3 say "Part n/4 of #766". The committed plan file + cumulative friction doc carry state so any worker resumes between PRs. No file-epic decomposition: #766 is already a leaf of epic #604, and the phases share one surface — a chain of sibling issues would add bookkeeping without parallelism (each phase strictly depends on the last).

### Affected files
- `creations/editors/voxel_editor/main.cpp` — `--scene-size`/`--gui-session` args; erase-mode wiring; mirror application at edit call sites; session shot-table integration
- `creations/editors/voxel_editor/symmetry.hpp` — `applyMirrors` goes live (offsets, undo integration)
- `creations/editors/voxel_editor/test/sessions/*` (new) — SessionBuilder + five entity recipes
- `creations/editors/voxel_editor/CMakeLists.txt` — session TUs if split out of main.cpp
- `scripts/author-entity.py` (new) — session runner: run, verify saves, copy to assets, determinism byte-compare
- `creations/demos/shape_debug/main.cpp` — multi-frame `--load-vxs` + step mode
- `assets/voxel/entities/*` (new) — the five committed entities
- `docs/design/editor-authoring-friction.md` (new) — cumulative friction log
- `.fleet/plans/issue-766.md` — this plan, first commit of PR-1

### Acceptance criteria (positive-fire)
- Each session run emits `GUI-ASSERT` PASS lines (runner enforces count > 0) including per-segment pick/occupancy assertions; `author-entity.py` exits 0 per entity.
- Determinism: an immediate re-run produces a byte-identical saved `.vxs` set (runner compares; same host+backend).
- Five entities committed under `assets/voxel/entities/`; each loads in `IRShapeDebug`; screenshots in the PRs. Bird: two screenshots at different playback steps that visibly differ (frame delta is the positive fire; byte-identical shots FAIL).
- Mushroom/ant sessions PASS **only** with mirroring applied — a mirror-side voxel is asserted, so the PR-2 fix positively fires.
- Friction doc has ≥1 substantive entry per entity; child issues filed for significant findings.

### Gotchas
- Never inject Escape; keep animation paused (playback is deltaTime-driven); erase = quick right press/release (right-drag rotates camera); start a new shot after any A/D press (camera re-apply).
- GUI panels render on the full-resolution GUI canvas (`setGuiCanvasFullResolution`, gui_scale=2) while the scene lives in game-canvas space — the mapping helper must treat panel-space and scene-space clicks separately (existing checked-in shots prove panel clicking; probe (2) proves scene space).
- Sessions are frame-scheduled (one event-frame per render frame): the ant session may run thousands of frames — set the runner timeout accordingly and log per-segment progress.
- Save paths resolve against the exe run dir (`data/editor_scene/`) — the runner must resolve ir-run's cwd and clear stale saves pre-run.
- `.vxs.json` sidecars are write-only/regenerated — commit exactly as produced, never hand-edit.
- macOS/Metal: the assertion path is CPU-state, backend-agnostic; author + verify on either host. A Linux-GL parity run is a smoke nice-to-have, not a gate.

