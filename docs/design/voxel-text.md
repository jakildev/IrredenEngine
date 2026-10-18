# Voxel text

World-space titles made of voxel letters, lit and shadowed with the rest of a
scene, that read as horizontally as the isometric view allows.

| Surface | Where |
|---|---|
| Glyph set | `engine/render/include/irreden/render/voxel_font.hpp` (`IRRender::getVoxelGlyph`) |
| Builder | `engine/prefabs/irreden/render/voxel_text.hpp` (`IRPrefab::VoxelText::create`) |
| Lua | `IRText.createVoxel` / `IRText.removeVoxel`, installed by `LuaScript::bindLuaText()` (`lua_voxel_text_bindings.hpp`) |
| Fixture | `IRShapeDebug --voxel-text-sample --voxel-text-yaw <deg>` (`voxel_text_yaw{0,45,90}` render-verify rows) |
| Worked example | `creations/demos/default/voxel_title.lua` |

## Screen mapping

Measured on rendered voxel output at camera yaw 0: world `+X` draws up-left and
`+Y` up-right, so screen-right is `+iso.x` (`IRMath::pos3DtoPos2DIso`) and a
word reads left to right when its columns step along world `(-1, 1, 0)`. Up
is `-z`. A word stepped along `(1, -1, 0)` draws as its mirror image in reverse
order. Derive screen-right from this, not from a screen-space helper's sign
convention.

One reading-diagonal voxel column projects twice as wide as one voxel row is
tall, which is why a bitmap row is two voxel rows (Decision 3).

## Contract

### 1. Letters are authored on the camera-facing lattice plane

Each letter is an unmanaged GRID `C_VoxelSetNew` with an identity rotation, a
unit scale and an integer translation. Its voxels are placed straight onto the
lattice plane the camera sees head-on, so the glyph renders upright and
pixel-exact without any rotation. Letters are lit, shadowed and
fog-classified like any other voxel set.

Rejected: an axis-aligned glyph turned by a `C_LocalTransform` yaw. GRID
inverse-mapping resamples the columns at 45° (some fall between samples) and
`REBUILD_GRID_VOXELS` re-rasterizes a rotated set every frame. Also rejected:
`DETACHED` rotation, which costs a private canvas per letter and a separate
lighting path.

Consequences: rotating or scaling the title root resamples every letter each
frame and is unsupported. Letters face the camera yaw at build time; after the
camera turns, re-face a title by removing and rebuilding it.

### 2. Facing is an 8-way snap of the camera yaw

`snappedYaw` is the yaw rounded to a multiple of π/4 (`detail::yawOctant`).
View→world is `R_z(+yaw)` (`engine/math/CLAUDE.md` §"Rotation primitives").

- Reading direction `r̂ = R_z(+snappedYaw)·(-1, 1, 0)`, reduced to its lattice
  step: `(-1, 1, 0)` at 0, `(-1, 0, 0)` at π/4, `(-1, -1, 0)` at π/2. It is
  computed with `rotateCardinalZInvI` from `(-1, 1, 0)` (even octants) or
  `(-1, 0, 0)` (odd octants).
- Depth normal `n̂ = R_z(−π/2)·r̂` points one layer away from the camera:
  `(1, 1, 0)` at yaw 0.

At an odd octant the letter faces the camera exactly but each column is one
axis step instead of one diagonal step, so letters draw about 0.71× as wide.
Snapping to the four cardinals instead would foreshorten letters to 0.71 at a
45° camera; the 8-way snap keeps every face within 22.5° of the camera.

`VoxelTextOptions::cameraYaw_` carries the yaw explicitly; absent, `create`
reads `IRPrefab::Camera::getYaw()`, which needs the named `camera` entity.
Lua always reads the live camera.

### 3. Glyph set

5×7 bitmaps with 1-voxel strokes, one per character of the trixel fonts:
the same 63 characters, the same lowercase aliases, and the same `?` for any
other byte. No two characters share a bitmap. `kVoxelGlyphStepX = 6` (one
column of spacing) and `kVoxelGlyphRowVoxels = 2`. The header follows the
trixel headers' layout: constants, `makeVoxelGlyph`, named `kVoxelGlyph_*`
literals and a character switch.

The trixel bitmaps are not reused: their 2-wide stems and parity rules exist
for the triangle lattice and read bold on voxels.

### 4. Emission

At integer `scale` `s`, a bitmap pixel at column `j` and row `i` (row 0 at the
top) emits `s` columns by `2s` voxel rows in each of `depth` layers.

- A letter's path point `P` is the bottom row of its front layer, at emitted
  column `mid = (5s − 1) / 2` (integer division).
- Emitted column `c` sits at `P + (c − mid)·r̂`; emitted row `q`, counted from
  the bottom, at `z = P.z − q`; layer `k` adds `k·n̂`.
- Each printable letter is one child entity whose CORNER-anchored set is sized
  to the letter's cell box (`detail::letterBox`). The child's translation is
  `P + box.min`, relative to the root.

A letter therefore stands on its path. With `depth > 1`, layers stepped along
a diagonal `n̂` touch only along edges, and each layer draws one voxel row
higher than the one in front of it.

### 5. Path forms

The `path` option:

- **Absent:** the default heading `R_z(−π/4)·r̂`. It is world `+Y` at yaw 0
  and rises to the right on screen.
- **A number:** a constant heading in radians about world `+Z`, with 0 = `+X`.
- **A list of numbers:** heading `i` steps from letter `i` to letter `i + 1`,
  and a short list repeats its last entry.
- **A list of points** (`{x, y, z}` tables, `vec3` or `ivec3`): the world
  vertices of a polyline that starts at `origin`. Letters past its end
  continue along the last segment.

The form is decided by element type; there are no string modes. A mix of
numbers and points, an empty list, a non-numeric element, or points that never
leave the origin raise.

**A path that runs left on screen reads backwards.** Letters always face the
camera with `r̂`, so on a segment whose `Δiso.x` is negative the letters are
placed right to left and the word reads reversed. This is the contract, not a
defect. A path that must read left to right at camera yaws 0, 45 and 90 keeps
every heading strictly between 3π/4 and 5π/4.

### 6. Advance

From one letter's path point, the next stands at the shortest arc length along
the path where the projected offset (`pos3DtoPos2DIsoYawed` at the snapped
yaw) reaches either of two thresholds:

- **horizontal:** `|Δiso.x| ≥ kVoxelGlyphStepX · s` reading columns, where one
  reading column is the projected `|iso.x|` of one `r̂` step: 2 at
  even octants, √2 at odd ones;
- **vertical:** `|Δiso.y| ≥` one glyph height plus one bitmap row, in voxel
  rows (`(7 + 1) · 2 · s`), each row projecting to 2 iso units.

Points are walked in floating point and each letter's point is snapped with
`IRMath::roundVec3HalfUp`. Exact per-letter advances: the default path at yaw 0
steps `(0, 12s, 0)`; a 3π/4 heading steps `6s·(-1, 1, 0)`; heading π steps
`(−12s, 0, 0)`; the default path at yaw π/4 steps `(−6s, 6s, 0)`.

A space advances without an entity. Titles are single-line; a newline is an
error.

### 7. C++ surface

`IRPrefab::VoxelText::create(text, VoxelTextOptions)` returns the root entity.
`VoxelTextOptions` holds `origin_` (rounded half up to a cell), `color_`,
`depth_` (1..`kMaxDepth`), `scale_` (1..`kMaxScale`), `path_`, `cameraYaw_` and
`canvas_` (null means the active canvas). The root carries only
`C_LocalTransform`; letters are `CHILD_OF` it. The placement helpers the tests
drive live in `IRPrefab::VoxelText::detail`.

`create` changes archetypes eagerly, so it is setup work, never called inside a
system tick. Letters follow the root's translation only with
`PROPAGATE_TRANSFORM` before `UPDATE_VOXEL_SET_CHILDREN` in UPDATE.

### 8. Lua surface

```lua
local root, letters = IRText.createVoxel(text, {
    origin = { 0, 0, 0 },   -- {x, y, z}, vec3 or ivec3
    color = Color.new(255, 255, 255, 255),  -- or {r, g, b[, a]}
    depth = 2,
    scale = 1,
    path = 3 * math.pi / 4, -- see Decision 5
    canvas = canvasId,      -- an entity carrying a C_VoxelPool
})
IRText.removeVoxel(root)
```

- `letters` counts the letter entities: every character except a space.
- Every invalid argument, an unknown option, or a newline raises
  `IRText.createVoxel: ...` before any entity exists.
- `removeVoxel` destroys the tree. It is there so a creation that binds only
  `bindLuaText()` can remove a title without `bindLuaDrivenEcs()`.
- Both verbs change archetypes eagerly, so a Lua system tick wraps them in
  `IREntity.deferredCall`.
- The scale option is not named `size`, which would collide with `IRText.Size`.
- The binding extends `IRText` without replacing it, so a creation's own
  `IRText` entries survive.
