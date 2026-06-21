# Plan: demo — unified rotation harness (extend canvas_stress)

- **Issue:** #1374
- **Model:** opus
- **Date:** 2026-06-21

## Scope

Promote the existing `canvas_stress` demo into the canonical unified rotation
harness, per the human scope direction (issue comment 2026-06-21T04:47Z):

- Add a **labeled side-by-side comparison region** showing one representative
  entity per rotation mode: **GRID-attached spin**, **world-Z-yaw GRID**, and
  **DETACHED SO(3)** — all driven under the shared camera Z-yaw, each tagged
  with an in-scene text label.
- The comparison is a labeled **view**, not a parity assertion. **No
  pixel-parity assertion** between GRID and DETACHED — they are intentionally
  different visuals (GRID re-voxelize = aliased world cells; DETACHED = smooth
  per-canvas SO(3) bake), and `MAIN_CANVAS_SO3` was retired in #1443 (attached
  main-canvas SO(3) *is* the GRID re-voxelize model).
- Keep `canvas_stress`'s committed `so3_*` (+ `revoxelize_solids`) render-verify
  baselines **byte-identical** — it is the permanent detached-canvas regression
  canary (main.cpp:66-91). New rows get their own committed baselines.

Out of scope (dropped from the original ticket by the human re-scope): the
per-mode `PERF_STATS_OVERLAY`. `canvas_stress` already exposes `--auto-profile`
for per-system timing; a dedicated overlay is not required. Do not add it.

## Verified current state

- **Demo:** `creations/demos/canvas_stress/main.cpp` (~1250 lines). Header at
  66-91 declares it the permanent visual regression canary.
- **Rotation modes today:** `RotationMode { GRID=0, DETACHED=1,
  DETACHED_REVOXELIZE=2 }` in
  `engine/prefabs/irreden/common/components/component_rotation_mode.hpp:48-72`;
  carried by `C_RotationMode`. The scene already spawns: a 5×5 GRID main grid
  (959-974), a GRID-mode self-spinning cluster (986-1014), DETACHED_REVOXELIZE
  canary cubes (1016-1070), re-voxelize proof solids (1081-1134), a 12-entity
  orbit ring mixing modes (1136-1246), and an SDF shadow floor (941-952).
  Camera Z-yaw is driven by `SYSTEM_AUTO_YAW_ROTATE`
  (`engine/prefabs/irreden/render/systems/system_auto_yaw_rotate.hpp`), enabled
  by default (`--no-auto-rotate` disables). World-Z-yaw residual deform of GRID
  geometry is the per-axis trixel-canvas path (no new system needed).
- **Spawn grouping:** `enum SpawnGroup` bitmask (163-169) +
  `groupEnabled(group)` (274-276). NOTE: `groupEnabled` returns true when
  `onlyGroups_ == 0` (the default), i.e. **groups are all-on by default** — a
  new group spawns in the default run unless explicitly excluded. This is the
  central constraint (see Approach + Gotchas).
- **Shot manifest:** base `kShots[]` (337-350) = the 5 `so3_*` shots; the demo
  appends `revoxelize_solids`, `revoxelize_solids_zoom`, `orbit_overview`,
  `shadow_overlay_floor`, `revox_coverage` (867-892) **after** the manifest so
  render-verify ignores them. Gated manifest:
  `creations/demos/canvas_stress/test/references/manifest.json` lists 6 shots
  (`so3_*` ×5 + `revoxelize_solids`). `revoxelize_solids_zoom` is intentionally
  excluded for documented round-to-cell speckle (CPU↔GPU float divergence).
- **render-verify invocation is fixed:** `scripts/render-verify.py` runs
  `fleet-run IRCanvasStress --auto-screenshot N` with **no per-manifest args**.
  Shots map to screenshots **by order** (Nth shot → Nth PNG). Any new gated
  shot must therefore be produced by the **default** invocation and appended so
  existing indices don't shift.
- **Text rendering exists but is unused in this demo:**
  `IRRender::renderText(canvas, text, ivec2 pos, Color, wrapWidth)` in
  `engine/render/include/irreden/render/trixel_text.hpp:60-101`, drawing at
  `kGuiTextDistance`. `canvas_stress` draws no in-scene text today.

## Approach (committed — single PR, opus)

Extend the **default** scene; gate nothing behind a runtime flag (render-verify
only runs the default invocation). Protect the canary by **spatial separation**:
place all new content outside the framing of every existing `so3_*` /
`revoxelize_solids` shot, and frame it with **new appended shots**.

1. **Add a `kGroupCompare` spawn group** (next free bit, `1u << 6`) and its
   `--only` name `"compare"` (parse table at 283-289). It spawns in the default
   run like the others (`groupEnabled`), which is what we want — the comparison
   region is a permanent part of the harness.

2. **Spawn the comparison region** in `initEntities()` (a new block after the
   orbit ring, ~1246), gated by `groupEnabled(kGroupCompare)`. Lay out three
   entities in a clean labeled row at a world position **well outside** the
   `so3_*` framings (those are `cameraOffset (0,0)` at zoom 1.0 / 0.65 / 0.45;
   the widest, `so3_grid_cross` at 0.45, must not include the new row — pick the
   row's world offset by checking the existing off-frame content, e.g. the orbit
   ring at radius 200 / `orbit_overview` at 0.32 sits outside the 0.45 frame):
   - **GRID-attached spin** — `C_RotationMode{GRID}` + `C_AutoSpin` about Z (a
     12³ cube, matching the existing gridspin cluster sizing at 986-1014).
   - **world-Z-yaw GRID** — `C_RotationMode{GRID}`, **no** `C_AutoSpin` (static
     entity); its motion comes from the shared camera Z-yaw + per-axis trixel
     deform. This is the row that makes the world-yaw path explicit alongside
     the others.
   - **DETACHED SO(3)** — reuse `spawnDetachedVoxelObject(...)`
     (361-397): `C_RotationMode{DETACHED_REVOXELIZE}` + `C_AutoSpin`.
   Use distinct colors (mirror the existing `kGridSpinColors` / `kDetachedColors`
   convention) so the modes read apart even before labels.

3. **Per-mode in-scene labels.** Render a short label above each comparison
   entity via `IRRender::renderText` into the **world/main canvas** (NOT a
   screen-locked overlay — a screen-locked label renders at a fixed screen
   position in *every* shot, including `so3_*`, and would break the canary).
   World-anchored labels appear only when their region is in frame, so they are
   captured solely by the new compare shots. Position via the iso-projection
   helpers (`engine/math/CLAUDE.md` §"Isometric projection") from each entity's
   world position to the canvas trixel coordinate; do not inline the iso
   equations. Labels: `"GRID spin"`, `"world Z-yaw"`, `"detached SO(3)"`.

4. **Append new shots** to `g_allShots` after the existing appended block
   (after ~892), so existing shot indices are untouched:
   - `compare_yaw0` — cameraOffset framing the comparison row, explicit
     `yaw = 0`. Frames the GRID-attached + world-Z-yaw GRID entities (both
     deterministic) and the labels. **Gated** (added to manifest).
   - `compare_yaw_q` — same framing, explicit `yaw = kQuarterPi`. Exercises the
     off-cardinal world-Z-yaw deform on the static GRID entity. **Gated.**
   - `compare_detached` — framing that includes the DETACHED entity. Set
     explicit yaw. Capture it, then **decide gating empirically**: include it in
     the manifest only if it passes the 99.9%/max_delta gate run-to-run at the
     chosen zoom (as `revoxelize_solids` does at zoom 1); if the re-voxelize
     speckle pushes it over threshold, leave it **out of the manifest** (append
     after the gated shots) and document it as an attach-screenshots inspection
     aid, exactly like `revoxelize_solids_zoom`. Do not bless a non-deterministic
     shot.

5. **Capture + commit baselines.** Run
   `python3 scripts/render-verify.py --target IRCanvasStress` (NO
   `--update-references`) **first** — the 6 existing manifest shots must still
   PASS byte-identical, proving the new content stayed out of their frames. Only
   then add the new gated shots to `manifest.json` (same order as the shot
   table) and run with `--update-references --force` to capture the new
   `macos-debug/` baselines; commit them. Linux baselines follow on a Linux host
   via the normal `fleet:needs-linux-smoke` flow (the manifest already has
   `macos-debug/` + `linux-debug/` subdirs).

6. **Use `attach-screenshots`** for the PR body (this is a render PR) and
   `render-debug-loop` to visually confirm the comparison reads correctly.

## Affected files

- `creations/demos/canvas_stress/main.cpp` — `kGroupCompare` enum bit + `--only`
  name; comparison-region spawn block in `initEntities`; per-mode `renderText`
  labels; new appended shots in the `g_allShots` build.
- `creations/demos/canvas_stress/test/references/manifest.json` — add the new
  **gated** compare shot labels (in shot-table order); extend `notes`.
- `creations/demos/canvas_stress/test/references/macos-debug/*.png` — new
  baseline PNGs for the gated compare shots (author host = macOS).
- (Linux baselines `test/references/linux-debug/*.png` captured later by a Linux
  fleet agent — not this PR.)
- No engine changes expected: `renderText`, `RotationMode`, `C_AutoSpin`,
  `SYSTEM_AUTO_YAW_ROTATE`, and the per-axis world-yaw path all already exist.

## Acceptance criteria

- One scene shows GRID-attached spin / world-Z-yaw GRID / DETACHED SO(3)
  side-by-side under shared camera Z-yaw, each with a legible in-scene label.
- `render-verify --target IRCanvasStress` (no `--update-references`): the 6
  existing manifest shots PASS **byte-identical** (canary intact).
- New deterministic compare shots (`compare_yaw0`, `compare_yaw_q`) have
  committed `macos-debug` baselines and pass on a clean re-run; the detached
  compare shot is either gated (if deterministic at its zoom) or documented as
  excluded (if speckle exceeds threshold).
- **No** pixel-parity assertion between GRID and DETACHED anywhere.
- PR body embeds before/after via `attach-screenshots`.

## Gotchas

- **`--only` groups are all-on by default** (`groupEnabled` returns true when
  `onlyGroups_ == 0`). A new group spawns in the default run — intended here,
  but it means the canary protection comes from **spatial placement + shot
  framing**, not from gating. Verify `so3_*` byte-identity with render-verify
  before committing anything.
- **Never use a screen-locked overlay canvas for the labels** — it would render
  at a fixed screen position in every shot (including `so3_*`) and break the
  canary. World-anchored labels only.
- **Detached / re-voxelize shots carry documented round-to-cell speckle**
  (sub-cell CPU↔GPU float divergence). `revoxelize_solids_zoom` is excluded for
  exactly this. Gate the detached compare shot only if it empirically holds the
  99.9% match gate run-to-run; otherwise append it out-of-manifest.
- **Shot order is the render-verify contract** (Nth shot → Nth PNG). Append new
  shots; never insert ahead of existing ones, or every downstream baseline
  silently misaligns.
- **IRMath only** for any vector / iso-projection math (no `glm::`/`std::`); use
  the named iso helpers, never inline the projection equations.
- **No `getComponent` in tick paths** — the spawn block is one-time init, so
  this is low-risk, but follow the standard ECS discipline if any per-frame
  label-position update is added (prefer computing label position once at spawn
  if the layout is static).
- `revoxelize_solids` (zoom 1, detached) IS in the manifest and passes — so a
  moderate-zoom detached compare shot may also be giftable; let the empirical
  gate decide, don't assume.
