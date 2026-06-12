# Detached-canvas depth default — world-placed by default, screen-locked as overlay opt-out

**Issue:** #1624 (decided post-#1596, architect-approved 2026-06-11).
**Supersedes:** the #1582 Option B "screen-locked overlay is the default"
contract (PR #1583) and the #1553 decision-1 default. The P4b mechanics doc
([`detached-revoxelize-world-light.md`](detached-revoxelize-world-light.md))
remains the reference for *how* world integration works; this doc records the
*default* flip and the migration contract.

## The decision

A detached canvas entity (`RotationMode::DETACHED` / `DETACHED_REVOXELIZE`)
**participates in world depth by default**. The opt-in
`C_EntityCanvas::worldPlaced_` field is replaced by the inverse opt-out:

```cpp
// C_EntityCanvas
bool screenLocked_ = false;   // default: world-placed
```

- **Default (`screenLocked_ == false`)** — `ENTITY_CANVAS_TO_FRAMEBUFFER` sets
  `distanceOffset_ = pos3DtoDistance(roundVec3HalfUp(translation))`, so the
  canvas's pool-centered model distances land in the shared world
  `trixelDistances` band and depth-sort against GRID solids, the floor, and
  each other (the P4b-1 GRID-equivalence, `detached_world_depth_test`).
  `DETACHED_REVOXELIZE` solids additionally **receive** (P4b-2) and **cast**
  (P4b-3) world sun-shadow + light-volume at their world cell origin. Plain
  `DETACHED` (forward-scatter) depth-sorts only: its octahedral-snap face
  deform has no faithful world-pos recovery for lighting, and the mode is on
  the retirement path (#1589).
- **Opt-out (`screenLocked_ == true`)** — the composite keeps
  `distanceOffset_ = 0` and skips receive/cast: a fixed-depth 2D overlay at
  the iso screen position, **byte-identical to the pre-#1624 default**.
  Reserved for genuine overlay cases: HUD props, billboards, floating
  showcases.

## Why the flip is right

- A world-placed `DETACHED_REVOXELIZE` solid, with cast landed (#1596), is
  strictly better than `GRID` for most world objects: cheap per-frame rotation
  (no world re-rasterization) AND full world depth/shadow participation.
- An entity drawn *at a world position* but ignoring its world Z for depth is
  a hybrid that surprises people — e.g. solids reading as sunk through a floor
  they physically intersect (#1620, the IRCanvasStress canaries vs the #1587
  floor band).
- The legitimate screen-locked cases (HUD, billboards, showcases) are a
  minority and are conceptually overlays — the natural opt-*out*, not the
  default. The old opt-in default was a migration artifact of world
  participation landing incrementally (depth #1592 → receive #1617 → cast
  #1626) on top of DETACHED's origin as a pure 2D overlay.

## Migration contract

- **Code that set `worldPlaced_ = true`** — delete the line; world placement
  is the default. The field rename makes stale call sites a **compile error**
  (deliberate: a silent semantic flip under the old field name would be
  worse).
- **Consumers that relied on the screen-locked default** (HUD-style detached
  canvases, board/display surfaces) — set `screenLocked_ = true` (Lua:
  `screenLocked`). The opt-out is byte-identical to the old default.
- **Lua binding** — the `C_EntityCanvas` key `worldPlaced` is replaced by
  `screenLocked`.
- **Demo canary** — `IRCanvasStress --screen-lock-detached` (legacy alias
  `--screen-lock-revox`) opts every detached canvas in the demo back into the
  overlay path at the pre-#1624 layout, reproducing the pre-flip scene
  byte-for-byte; it is the regression canary for the opt-out.

## Invariants (unchanged from P4b)

- World depth, receive, and cast all share one cell convention:
  `roundVec3HalfUp(translation)` / `pos3DtoDistance` (the GRID equivalence).
- The sun-shadow bake only ever reads main-canvas-layout depth sources; a
  foreign model-frame canvas texture is never a bake input (#1640 tracks the
  Metal scratch-indirection gap that constraint encodes).
- `screenLocked_` canvases publish no `detachedWorldReceive_` and are skipped
  by the bake's caster gather — the overlay path stays byte-identical to the
  pre-#1624 default.
