# Voxel text on the GUI canvas

Isometric voxel lettering for GUIs: each character is a small block of voxels
seen from the fixed iso camera, drawn on the GUI canvas's triangle lattice
with its own colour per face, and the letters sit side by side along
screen-horizontal lines like ordinary text. Only the letter is isometric —
a word never follows an iso path.

Code: [`voxel_text.hpp`](../../engine/render/include/irreden/render/voxel_text.hpp)
(layout), [`voxel_font.hpp`](../../engine/render/include/irreden/render/voxel_font.hpp)
(5×7 glyphs), [`gui_text_batch.hpp`](../../engine/prefabs/irreden/render/gui_text_batch.hpp)
(`expandVoxelTextToCommands`). Display contract of the GUI canvas:
[gui-trixel-display.md](gui-trixel-display.md).

## Using it

A free-text entity (`C_TextSegment` + `C_GuiPosition` + `C_GuiElement` +
`C_TextStyle`) draws voxel letters when `C_TextStyle::voxel_.voxelSize_ > 0`:

```cpp
IREntity::createEntity(
    C_TextSegment{"SCORE 120"},
    C_GuiPosition{8, 8},
    C_GuiElement{},
    C_TextStyle::voxel(IRRender::shadedVoxelFaceColors(Color{235, 200, 90, 255}), 2)
);
```

```lua
local style = C_TextStyle.new()
style.voxelSize = 2                          -- 0 = flat trixel glyphs
style.voxelDepth = 1
style.voxelFacing = IRText.VoxelFacing.CAMERA
style.faceX = Color.new(220, 60, 60, 255)    -- or style:shadeFaces(top)
style.faceY = Color.new(60, 200, 80, 255)
style.faceZ = Color.new(70, 110, 235, 255)
```

`TEXT_TO_TRIXEL` renders it, so any creation that shows free text shows voxel
text with no pipeline change. Alignment and the box fields apply per line;
`color_`, `fontSize_`, `textSize_` and `wrapWidth_` do not (`'\n'` breaks
lines).

## Decisions

1. **Faces keep their world roles.** A voxel shows its top (Z, −z) as a
   diamond, its X face (−x) on the left and its Y face (−y) on the right —
   the same faces a world voxel shows at camera yaw 0. `VoxelFaceColors`
   holds one colour each; `shadedVoxelFaceColors(top)` derives the sides.
2. **Three facings.** `CAMERA` puts the glyph in the plane spanning world
   (1,−1,0) and z, so it reads upright and unsheared, every lit pixel a cube
   showing both sides and a top. `X_FACE` / `Y_FACE` print the glyph on that
   face's plane, so it reads sheared down / up to the right. Depth extrudes
   away from the camera.
3. **Pixels stay square on screen.** One screen-horizontal voxel step is two
   lattice columns, twice as wide as a voxel row is tall, so a camera-facing
   glyph row is `kVoxelGlyphRowVoxels` (2) voxel rows; the sheared facings
   step one column per glyph column and keep one row.
4. **Occlusion is resolved on the CPU.** A glyph's unit voxels are painted
   farthest first (depth `x + y + z`) into a per-glyph face map, so the GPU
   writes only visible texels at the one GUI-text distance.
5. **Size is lattice scaling.** `voxelSize_ = s` scales the unit face map on
   the triangle lattice exactly as the text shader scales a glyph texel, which
   equals rasterizing cubes of edge `s` (`VoxelText.LatticeScaleEqualsTheBiggerCube`).
6. **No new shader.** The face map is covered by same-face rectangles of at
   most one glyph cell (6×11), each drawn as one `GlyphDrawCommand` over a
   solid cell appended to the font buffer, in the same dispatch as flat text,
   on both backends. Every glyph box, gap and line step is even, so each
   glyph's lattice parity matches its local origin.
7. **Fixed glyph boxes.** Every glyph of a style shares one box (the full 5×7
   cell's projection), so letters advance by a constant and share a
   baseline; ' ' advances without drawing and an unknown character draws
   as '?'.

## Visual check

`IRShapeDebug --voxel-text-sample --auto-screenshot` captures every facing,
explicit and shaded face colours, two sizes and a centred two-line block.

## Superseded approach — world-space titles (PR #4588 first revision, `9d4865c75`)

The first revision of the PR that landed this document built the feature as
world-space titles: `IRPrefab::VoxelText::create` made one GRID voxel-set
letter per character on the lattice plane facing the camera (8-way yaw
snap), laid along a world-axis path, a heading list or a polyline, with
`IRText.createVoxel` / `IRText.removeVoxel`, the `VoxelTextPlacement.*` and
`LuaVoxelText.*` tests, three yaw render-verify rows and a `voxel_title.lua`
example in `IRCreationDefault`. That head was approved and conflict-resolved
against master `d1b6abe33` before the feature was redirected to the GUI form
above. The world-space builder is not queued work; nothing on master
references it.

If a scene-space title (text made of lit, shadowed world voxels) is ever
wanted, start from `9d4865c75` rather than from scratch: its placement
helpers (reading direction, default heading, path-point placement), both
test files, the `--voxel-text-yaw` fixture with its clip-gated manifest rows,
and the world-space decisions below carry over. The glyph set in
`voxel_font.hpp` is shared with the GUI form, so the builder consumes it
unchanged (that revision's Decision 3), and the GUI contract in this document
does not change: a world-space builder is a separate prefab and Lua binding
beside it. `IRPrefab::Camera::getYaw()` and `IREntity.destroyTree` still back
the yaw snap and `removeVoxel`.

### World-space decisions (numbered as in `9d4865c75`)

At camera yaw 0 world `+X` draws up-left and `+Y` up-right, so a word reads
left to right when its columns step along world `(-1, 1, 0)`; up is `-z`.

- **W1. Letters are authored on the camera-facing lattice plane.** Each letter
  is an unmanaged GRID `C_VoxelSetNew` with identity rotation, unit scale and
  an integer translation, lit, shadowed and fog-classified like any voxel set.
  Rejected: a `C_LocalTransform` yaw on an axis-aligned glyph (GRID resampling
  drops columns at 45° and re-rasterizes every frame) and `DETACHED` rotation
  (a private canvas per letter). Rotating or scaling the root is unsupported;
  after the camera turns, rebuild the title to re-face it.
- **W2. Facing is an 8-way snap of the camera yaw.** `snappedYaw` rounds to a
  multiple of π/4; view→world is `R_z(+yaw)`. Reading direction
  `r̂ = R_z(+snappedYaw)·(-1, 1, 0)` reduced to its lattice step (`(-1, 1, 0)`
  at 0, `(-1, 0, 0)` at π/4, via `rotateCardinalZInvI`); depth normal
  `n̂ = R_z(−π/2)·r̂`, `(1, 1, 0)` at yaw 0. Odd octants draw letters about
  0.71× as wide; the snap keeps every face within 22.5° of the camera. The yaw
  comes from `VoxelTextOptions::cameraYaw_`, else `IRPrefab::Camera::getYaw()`;
  Lua always reads the live camera.
- **W4. Emission.** At integer scale `s`, bitmap pixel `(j, i)` emits `s`
  columns by `2s` voxel rows in each of `depth` layers. The letter's path point
  `P` is the bottom row of its front layer at emitted column
  `mid = (5s − 1) / 2` (integer division); column `c` sits at
  `P + (c − mid)·r̂`, row `q` from the bottom at `z = P.z − q`, layer `k` adds
  `k·n̂`. Each printable letter is one child entity whose CORNER-anchored set is
  sized to its cell box, translated `P + box.min` relative to the root.
- **W5. Path forms, decided by element type.** Absent: the default heading
  `R_z(−π/4)·r̂` (world `+Y` at yaw 0). A number: a constant heading in radians
  about `+Z`, 0 = `+X`. A list of numbers: heading `i` steps letter `i` to
  `i + 1`, a short list repeating its last entry. A list of points: a polyline
  from `origin`, continued along its last segment. Mixed or non-numeric
  elements, an empty list, or points that never leave the origin raise. A
  segment running left on screen (`Δiso.x < 0`) reads backwards by contract;
  headings strictly between 3π/4 and 5π/4 read left to right at yaws 0, 45, 90.
- **W6. Advance.** The next letter stands at the shortest arc length where the
  projected offset (`pos3DtoPos2DIsoYawed` at the snapped yaw) reaches
  `|Δiso.x| ≥ kVoxelGlyphStepX · s` reading columns (one column is 2 iso units
  at even octants, √2 at odd) or `|Δiso.y| ≥ (7 + 1) · 2 · s` voxel rows of 2
  iso units each. Points walk in floating point and snap with
  `IRMath::roundVec3HalfUp`. Exact steps: default path at yaw 0 `(0, 12s, 0)`;
  heading 3π/4 `6s·(-1, 1, 0)`; heading π `(−12s, 0, 0)`; default path at yaw
  π/4 `(−6s, 6s, 0)`. A space advances without an entity; a newline raises.
- **W7. C++ surface.** `IRPrefab::VoxelText::create(text, VoxelTextOptions)`
  returns the root, which carries only `C_LocalTransform`; letters are
  `CHILD_OF` it. Options: `origin_` (rounded half up), `color_`, `depth_`
  (1..`kMaxDepth`), `scale_` (1..`kMaxScale`), `path_`, `cameraYaw_`, `canvas_`
  (null = active canvas). Placement helpers live in
  `IRPrefab::VoxelText::detail`. `create` changes archetypes eagerly — setup
  work, never inside a tick — and letters follow the root only with
  `PROPAGATE_TRANSFORM` before `UPDATE_VOXEL_SET_CHILDREN` in UPDATE.
- **W8. Lua surface.** `local root, letters = IRText.createVoxel(text,
  { origin, color, depth, scale, path, canvas })` and
  `IRText.removeVoxel(root)`. `letters` counts every character except spaces.
  Any invalid argument, unknown option or newline raises
  `IRText.createVoxel: ...` before any entity exists. `removeVoxel` destroys
  the tree so a creation binding only `bindLuaText()` needs no
  `bindLuaDrivenEcs()`. Both verbs change archetypes eagerly, so a Lua tick
  wraps them in `IREntity.deferredCall`. The option is `scale`, not `size`
  (which collides with `IRText.Size`), and the binding extends `IRText`
  without replacing it.
