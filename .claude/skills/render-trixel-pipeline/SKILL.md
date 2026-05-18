---
name: render-trixel-pipeline
description: >-
  Work with the Irreden Engine render pipeline: voxel-to-trixel stages, canvas
  textures, trixel compositing, framebuffer output, shaders, camera, and
  coordinate systems. Use when the user wants to modify rendering, add shader
  stages, work with canvases, adjust camera/viewport, or understand the
  isometric projection math.
---

# Render / Trixel Pipeline

Read `engine/render/CLAUDE.md` first — it has the authoritative pipeline
diagram, gotchas (hardcoded bind points, distance-clear semantics, canvas
destruction ordering), and the SDF-vs-voxel-pool parity rules. For isometric
projection equations, see `engine/math/CLAUDE.md` §"Isometric projection — the
equations".

## Two-stage depth split

Stage 1 (`c_voxel_to_trixel_stage_1.glsl`) writes only depth via
`imageAtomicMin`. Stage 2 (`c_voxel_to_trixel_stage_2.glsl`) reads that depth
before writing color and entity IDs. Never merge or reorder these two — the
`imageAtomicMin` in Stage 1 is the only thing that resolves overdraw; if Stage
2 runs first, every voxel writes unconditionally and the color pass is garbage.

A minimal creation needs exactly four systems in the RENDER pipeline:
`VOXEL_TO_TRIXEL_STAGE_1`, `VOXEL_TO_TRIXEL_STAGE_2`,
`TRIXEL_TO_FRAMEBUFFER`, `FRAMEBUFFER_TO_SCREEN`. All other systems
(lighting passes, SHAPES_TO_TRIXEL, TRIXEL_TO_TRIXEL, etc.) are optional
overlays inserted between Stage 2 and TRIXEL_TO_FRAMEBUFFER.

## Trixel compositing (CHILD_OF)

`TRIXEL_TO_TRIXEL` uses `CHILD_OF` relations. For each child-canvas →
parent-canvas pair the system calls:

1. `beginTick` — binds the shader, sets camera offset in frame data.
2. `relationTick` — binds parent canvas textures to image slots 0 and 1.
3. `tick` — binds child canvas textures to slots 2 and 3, uploads frame data, dispatches.

Canvas entities without `CHILD_OF` do not participate in `TRIXEL_TO_TRIXEL` -- they render independently into their own textures.

## Adding a new render stage

1. Write the GLSL shader in `engine/render/src/shaders/`.
2. Add a `SystemName` enum entry in `ir_system_types.hpp`.
3. Create a system header under `engine/prefabs/irreden/render/systems/`.
4. In `create()`: create named GPU resources and set up the compute dispatch or draw call.
5. Register in the creation's RENDER pipeline in dependency order relative to the stages above.
