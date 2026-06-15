---
name: gui-verify
description: >-
  Behavioral GUI test harness for Irreden Engine creations. Builds a creation,
  runs it headless with the P3 GUI-test shot table, parses per-assertion
  PASS/FAIL from the result log, and exits non-zero on failure. Use after any
  GUI interaction change (hover routing, click dispatch, picking) to catch
  behavioral regressions without a human in the loop.
---

# GUI Verify

Thin wrapper over the shared flow at
[`docs/agents/skills/gui-verify.md`](../../../docs/agents/skills/gui-verify.md).
Read that doc for the full step sequence, anti-patterns, and image-comparison
integration notes.

## Deltas (engine repo)

| Delta key | Engine value |
|---|---|
| **runner** | `scripts/gui-verify.py` |
| **build tool** | `fleet-build` |
| **run tool** | `fleet-run` |
| **warmup default** | `10` |

## Quick reference

```
# Run GUI tests for the voxel editor (builds first):
python3 scripts/gui-verify.py IRVoxelEditor

# Skip the build step:
python3 scripts/gui-verify.py IRVoxelEditor --no-build

# Increase warmup frames if assertions are timing-sensitive:
python3 scripts/gui-verify.py IRVoxelEditor --warmup-frames 20
```

## Worked example: voxel editor

`creations/editors/voxel_editor/main.cpp` ships two assertion shots (shots 4
and 5 in the `kGuiTestShots[]` table):

| Shot index | Label | Assertions |
|---|---|---|
| 4 | `editor_gui_assert` | HOVERS(layer_list), CLICK_FIRES(layer_list), CHECKBOX(layer_visible, true), SLIDER_VALUE(fps_slider, fps, 0.5) |
| 5 | `editor_pick_voxel` | PICKS_VOXEL(ivec3(-1,-1,-1)) |

Run:

```
python3 scripts/gui-verify.py IRVoxelEditor
```

Expected: all assertions PASS; non-zero exit and a failing-assertion list if
any state is wrong after synthetic input injection.

## Adding GUI tests to a new creation

See [`docs/agents/skills/gui-verify.md`](../../../docs/agents/skills/gui-verify.md)
§ "Adding a first GUI test to a creation".
