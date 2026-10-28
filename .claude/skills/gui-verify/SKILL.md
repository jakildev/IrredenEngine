---
name: gui-verify
description: >-
  Behavioral GUI test harness for Irreden Engine creations — builds a
  creation, runs it headless with the P3 GUI-test shot table, parses
  per-assertion PASS/FAIL from the result log, and exits non-zero on
  failure. Use after any GUI interaction change (hover routing, click
  dispatch, picking) to catch behavioral regressions without a human in
  the loop.
---

# gui-verify (Irreden Engine)

**The flow lives in [`docs/agents/skills/gui-verify.md`](../../../docs/agents/skills/gui-verify.md).**
Read it first, then apply the deltas below.

## Deltas (engine repo)

| Delta key | Engine value |
|---|---|
| **runner** | `scripts/gui-verify.py` |
| **build tool** | `fleet-build` |
| **run tool** | `fleet-run` |
| **warmup default** | `10` |

## Quick reference

```
python3 scripts/gui-verify.py IRVoxelEditor                      # builds first
python3 scripts/gui-verify.py IRVoxelEditor --no-build
python3 scripts/gui-verify.py IRVoxelEditor --warmup-frames 20   # timing-sensitive assertions
```

A run that would outlive the tool window (a Codex command window) starts
as the `gui-verify` job profile instead
([`FLEET-RUNTIME.md § Long-running jobs`](../../../docs/agents/FLEET-RUNTIME.md#long-running-jobs));
its evidence is the later terminal `wait`.

## Participants

Every creation that registers `IRPrefab::GuiTest` assertions
(`git grep -l 'IRPrefab::GuiTest' -- creations`). The target arg is the CMake
executable; flags after `--` reach the creation. Without a session flag a run
drives the creation's default shot table.

| Creation | Target | Shots / sessions registered in | Invocation |
|---|---|---|---|
| `editors/voxel_editor` | `IRVoxelEditor` | `main.cpp` `kGuiTestShots[]` (default); `session_builder.hpp` (named sessions) | `python3 scripts/gui-verify.py IRVoxelEditor` / `-- --gui-session drag_probe` |
| `demos/lua_widgets` | `IRLuaWidgetsDemo` | `main_lua.cpp` `kGuiTestShots[]` | `python3 scripts/gui-verify.py IRLuaWidgetsDemo` |
| `demos/ui_widgets` | `IRUIWidgetsDemo` | `main.cpp` `kShots[]` | `python3 scripts/gui-verify.py IRUIWidgetsDemo` |
| `demos/shape_debug` | `IRShapeDebug` | `main.cpp` `kHelpOverlayGuiShots`, `kContinuousZoomGuiShots`, `kCullEvictShots` | `python3 scripts/gui-verify.py IRShapeDebug -- --gui-test` (or `--continuous-zoom-gui-test`, `--cull-evict-test`) |
| `demos/canvas_stress` | `IRCanvasStress` | `main.cpp` `g_hoverCanaryShot` | `python3 scripts/gui-verify.py IRCanvasStress -- --gui-session hover_canary` |
| `editors/font_maker` | `IRFontMaker` | `main.cpp` `kIdleShots` (default), `kTableShots`, `kEditShots`, `kSampleShots` | `python3 scripts/gui-verify.py IRFontMaker` / `-- --font-maker-test table` (`edit`, `sample`) |
| `launcher` | `IRLauncher` | `main_lua.cpp` `sessionShots()` (`kMainShots` default, one table per session) | `python3 scripts/gui-verify.py IRLauncher` / `-- --gui-session catalog_tree` |

## Worked example: voxel editor

`creations/editors/voxel_editor/main.cpp` ships two assertion shots in its
`kGuiTestShots[]` table:

| Shot | Label | Assertions |
|---|---|---|
| 4 | `editor_gui_assert` | HOVERS(layer_list), CLICK_FIRES(layer_list), CHECKBOX(layer_visible, true), SLIDER_VALUE(fps_slider, fps, 0.5) |
| 5 | `editor_pick_voxel` | PICKS_VOXEL(ivec3(-1,-1,-1)) |
