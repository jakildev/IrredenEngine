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

`creations/editors/voxel_editor/main.cpp` ships two assertion shots in its
`kGuiTestShots[]` table:

| Shot | Label | Assertions |
|---|---|---|
| 4 | `editor_gui_assert` | HOVERS(layer_list), CLICK_FIRES(layer_list), CHECKBOX(layer_visible, true), SLIDER_VALUE(fps_slider, fps, 0.5) |
| 5 | `editor_pick_voxel` | PICKS_VOXEL(ivec3(-1,-1,-1)) |
