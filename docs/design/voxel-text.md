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
