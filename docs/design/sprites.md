# 2D sprite rendering

Companion design note for the sprite epic (#14). v1 specs the data
model, pipeline placement, and depth semantics for a flat-2D sprite
layer that renders alongside the trixel pipeline. Implementation lands
across five child tasks (T-087 establishes the data model; the
remaining four ship the loader, render pass, animation system, and
demo); this note is the authoritative reference for the cross-task
invariants.

## TL;DR

- Sprites are **screen-composite**, not trixel content.
- They draw at the `FRAMEBUFFER_TO_SCREEN` pipeline stage via a sibling
  system `SPRITES_TO_SCREEN` (child task 3), bypassing the entire
  voxel → trixel → framebuffer machinery.
- One iso-projected depth per sprite. Sort order between sprites is a
  per-entity scalar, not a per-pixel depth-buffer problem.
- Anchor defaults to `{0.5, 0.0}` — bottom-center.
- v1 ships sprites composited above the main canvas and below the GUI
  canvas; cross-canvas z-sort is explicitly Part 2.

## Why screen-composite, not trixel content

The trixel pipeline rasterizes a 3D voxel field through an isometric
projection into a fixed-resolution canvas, then blits the canvas to the
screen with camera pan/zoom. That is the right machinery for blocky
voxel content but the wrong one for textured 2D quads:

- A trixel canvas writes per-pixel `(color, distance, entityId)` triplets
  via `imageAtomicMin` keyed by depth. A sprite has one depth for the
  whole quad, so it would race per-pixel against itself.
- Sprite sub-pixel filtering, alpha blending, and arbitrary sub-rect UV
  sampling all require a fragment-shader path; the trixel canvas is a
  compute-shader path.
- Lighting, AO, and fog of war operate on the canvas. Sprites should
  bypass all of those passes — their look is owned by the sprite's
  texture, not by the lighting state of the world cell underneath.

Drawing sprites alongside the canvas blit (the
`FRAMEBUFFER_TO_SCREEN` pass that already textures a quad with the
fully-composited canvas) keeps each system focused on one shape of
content.

## Pipeline placement

The render pipeline (per `engine/render/CLAUDE.md`) ends with the canvas
chain:

```
…  →  TRIXEL_TO_FRAMEBUFFER  →  FRAMEBUFFER_TO_SCREEN  →  end of frame
```

Sprites slot in as a sibling system at the same stage as
`FRAMEBUFFER_TO_SCREEN`, drawing **after** the main canvas's blit and
**before** the GUI canvas's blit:

```
… → main FRAMEBUFFER_TO_SCREEN
  → SPRITES_TO_SCREEN              ← new system, one draw per frame
  → gui FRAMEBUFFER_TO_SCREEN
```

Both passes write to the default framebuffer; the order is enforced by
the prefab's pipeline registration in child task 3, not by any
canvas-target indirection.

## Component model

```
C_Sprite                  C_SpriteSheet (optional)         C_SpriteAnimation (optional)
┌───────────────────┐    ┌───────────────────────────┐    ┌─────────────────────────┐
│ textureHandle     │    │ textureHandle             │    │ animationIndex          │
│ size              │    │ atlasSizePx               │    │ frameIndex / elapsed    │
│ uvRect            │ ←  │ frames[]                  │  → │ loop mode / speed       │
│ anchor            │    │ animations[]              │    │                         │
│ tint              │    │   (NamedAnimation rows)   │    └─────────────────────────┘
└───────────────────┘    │ findAnimationIndex(name)  │
                         └───────────────────────────┘
   per-instance              per-asset (one per sheet)         per-instance, optional
```

The runtime animation component stores `animationIndex` (resolved once
via `findAnimationIndex` when the animation is set) and indexes
`C_SpriteSheet.animations_[i]` per tick — string lookup never happens
on the playback hot path.

`C_Sprite` is the only component a static (non-animated) sprite needs.
`C_SpriteSheet` lives on the same entity (or a separate "asset" entity
referenced via a relation; child task 2 picks the layering) and owns
the atlas's CPU-side metadata. `C_SpriteAnimation` (child task 4) is
the runtime animation state; the animation system writes back into
`C_Sprite.uvRect` each tick.

### v1 depth model

Each sprite reads `C_PositionGlobal3D` (auto-added on every entity).
`APPLY_POSITION_OFFSET` has already folded any modifier-driven offset
into globalPos earlier in the UPDATE tick, so depth =
`pos3DtoDistance(global)` — exactly what the trixel pipeline uses to
sort voxels. Sort order is back-to-front; alpha blending requires it.

### Anchor

`C_Sprite.anchor_` is in local UV space:

- `{0.5, 0.0}` — bottom-center (default; matches an iso character whose
  feet are at the world position).
- `{0.5, 0.5}` — center.
- `{0.0, 0.0}` — top-left.

Quad origin = `isoProject(global.pos_) - anchor * size`.

### Non-iso seam

Future creations may render in a non-isometric projection (e.g. flat 2D
side-scroller). The seam to that future is documented here so the v1
implementation doesn't bake the iso projection in:

- A future `C_Position2D` + `C_Depth` pair replaces the
  `C_PositionGlobal3D` input. The sprite system reads either form and
  resolves to a `(screenX, screenY, depth)` triple — same downstream
  sort-key shape, different inputs.
- The animation system, atlas loader, and shader code are **invariant**
  to the choice — they touch UVs, frames, and tints, not screen
  position.

v1 implements only the `Global → iso-projection` path. The seam is a
one-function-per-projection swap, not a refactor.

## Sort scope (v1)

- Sort sprites against each other by iso depth. Strict back-to-front;
  ties broken by entity id for determinism.
- Do NOT sort sprites against canvas framebuffers. The main canvas
  always draws below sprites; the GUI canvas always draws above.
  Cross-layer sort is Part 2 and would require a different output
  topology (a depth buffer shared across passes).

## Drawing model (v1, child task 3)

One instanced draw per frame:

- CPU iterates `(C_Sprite, C_PositionGlobal3D)`, computes iso depth
  from `global` (any modifier-driven offset has already been folded in
  by `APPLY_POSITION_OFFSET`), sorts the entity-id array.
- CPU builds a per-instance buffer in sort order, packing
  `{ mat4 model, vec4 uvRect, uint textureSlot, vec4 tint }` per sprite.
- GPU issues `drawArraysInstanced` against the shared `QuadVAOArrays`
  (6-vert unit quad). A new `SpritesToScreenProgram` fragment shader
  samples the bound atlas at `uvRect`.
- v1 binds one atlas per draw call. Multi-sheet batching (texture
  array, bindless) is a follow-up.

GLSL and MSL shaders ship in lockstep per the `backend-parity` rule.

## Lighting interaction

Sprites bypass all lighting passes. They composite onto the framebuffer
after the trixel pipeline has fully resolved lighting, so adding a
sprite to a dimly-lit world does not darken the sprite. Creations that
want a sprite to react to local lighting should pre-tint its texture or
bake lighting into the sprite via `C_Sprite.tint_`.

## What's explicitly out of scope (v1)

- Sprite vs canvas z-sort (Part 2; needs shared depth buffer).
- Atlas merging / bindless textures (one sheet per draw call is fine
  for v1's instance counts).
- Animation blending / crossfade (single active animation per
  `C_SpriteAnimation`; switching is a hard cut).
- Sprite-driven physics or collision (sprites are visual only;
  hitboxes belong on a separate component).
- Dynamic atlas repacking (atlases are loaded once, treated as
  immutable).

## Cross-task contract summary

| Owner          | Owns                                              |
|----------------|---------------------------------------------------|
| Child 1 (T-087)| `C_Sprite`, `C_SpriteSheet` definitions, this note, Lua-binding stubs |
| Child 2        | Sheet loader, asset format (PNG + sidecar), populating `C_SpriteSheet` |
| Child 3        | `SPRITES_TO_SCREEN` system, GLSL+MSL shaders, instanced draw |
| Child 4        | `C_SpriteAnimation` + animation system, named-animation playback API |
| Child 5        | Lua bindings (full surface) + `sprite_demo` creation |

Each child PR's diff stays local to its row above. Components from
child 1 are a stable contract — don't quietly add fields in later
children without revising this note.
