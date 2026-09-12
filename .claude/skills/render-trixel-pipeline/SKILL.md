---
name: render-trixel-pipeline
description: >-
  Works with the Irreden Engine render pipeline — voxel-to-trixel stages,
  canvas textures, trixel compositing, framebuffer output, shaders, camera,
  and coordinate systems. Use when the user wants to modify rendering, add a
  shader stage, work with canvases, adjust the camera or viewport, or
  understand the isometric projection math.
---

# Render / Trixel Pipeline

Read [`engine/render/CLAUDE.md`](../../../engine/render/CLAUDE.md) first — the
pipeline diagram, gotchas (hardcoded bind points, distance-clear semantics,
canvas destruction ordering), and the SDF-vs-voxel-pool parity rules. Isometric
projection equations: [`engine/math/CLAUDE.md`](../../../engine/math/CLAUDE.md)
§"Isometric projection — the equations".

## Two-stage depth split

Stage 1 (`c_voxel_to_trixel_stage_1.glsl`) writes only depth via
`imageAtomicMin`; stage 2 (`c_voxel_to_trixel_stage_2.glsl`) reads that depth
before writing colour and entity IDs. The `imageAtomicMin` in stage 1 is what
resolves overdraw — stage 2 first means every voxel writes unconditionally.
Both dispatches run inside the single `VOXEL_TO_TRIXEL_STAGE_1` system: one
per-canvas tick does compact → stage 1 → stage 2, which keeps each canvas
atomic over the shared voxel SSBOs in multi-canvas scenes.

A minimal creation registers exactly `VOXEL_TO_TRIXEL_STAGE_1`,
`TRIXEL_TO_FRAMEBUFFER`, `FRAMEBUFFER_TO_SCREEN` in RENDER; lighting passes,
`SHAPES_TO_TRIXEL`, `TRIXEL_TO_TRIXEL`, and the rest are optional overlays
inserted between the first two.

## Trixel compositing (CHILD_OF)

`TRIXEL_TO_TRIXEL` composites each child canvas into its `CHILD_OF` parent:
`beginTick` binds the shader and sets the camera offset in frame data;
`relationTick` binds the parent canvas textures to image slots 0 and 1; `tick`
binds the child canvas to slots 2 and 3, uploads frame data, and dispatches.
Canvases without `CHILD_OF` render independently into their own textures.

## Adding a render stage

1. Write the shader in `engine/render/src/shaders/` (and its `.metal`
   counterpart — `backend-parity` skill).
2. Add the `SystemName` enum entry in `ir_system_types.hpp`.
3. Add the system header under `engine/prefabs/irreden/render/systems/`;
   `create()` creates the named GPU resources and sets up the dispatch or draw.
4. Register it in the creation's RENDER pipeline in dependency order relative
   to the stages above.
