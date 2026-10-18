# GUI canvas trixel display

The GUI canvas stores one texel per trixel: text glyphs, widget fills and
borders, disc and line draws. Its texels are iso-shaped (two framebuffer
pixels wide, one tall), and the canvas displays as lattice triangles, the
`LOCAL_TRIANGLES` layout the render manager stamps on it. This document owns
the display geometry, the two composite paths, the lattice-aware glyph
scaling, and the rules the trixel font is drawn under.

## Lattice geometry

With canvas-local parity `m` (`localTrixelOriginParity`), texel `(X, Y)` with
`(X + Y + m)` even displays as the LEFT-pointing triangle with vertices
`(X+1, Y-1)`, `(X+1, Y+1)`, `(X, Y)`; odd displays as the RIGHT-pointing
triangle `(X, Y-1)`, `(X, Y+1)`, `(X+1, Y)`. A triangle is one texel wide and
two rows tall, centred on its texel's row. On screen a texel is `2 × scale`
pixels wide and `1 × scale` tall, so a triangle is `2 × scale` pixels on both
axes: close to equilateral, with diagonal edges at ±26.6°.

The three edges of a triangle are shared with its horizontal neighbor (the
vertical edge) and the texels directly above and below it (the two diagonals).
Lattice lines are `x = integer`, `x + y = even` and `x - y = even` in texel
units; every integer `x` is a mirror axis.

Consequences for anything authored on this canvas:

- Two adjacent texels `(c, r)`, `(c+1, r)` with `(c + r + m)` even form a
  solid diamond. With the opposite parity they are two outer halves that touch
  only at a point.
- A column of texels is a solid strip with straight vertical sides. A single
  row of texels is a chain of diamonds touching at their tips, so a 1-row
  horizontal line reads as dotted; 2-row bars are solid zigzag ribbons.
- A 2-wide vertical stroke is solid and ends in a point where its left column
  and end row have equal parity, in a notch otherwise.
- A diamond chain stepping one column per row is a straight 26.6° band; a
  chain of 3-row stem segments stepping one column per three rows is a
  straight ~56° band. Stepping one column per two rows flips parity and
  breaks the chain.
- An isolated texel is a lone triangle: a spur if filled, a bite if empty.

`parityAlignedPosition` keeps every text origin on even parity, and the glyph
steps (8 × 14) are even, so these relations hold identically for every glyph
on every line.

## Composite paths

**Screen composite (default).** `GUI_CANVAS_TO_SCREEN` draws the GUI canvas
onto the default framebuffer after `FRAMEBUFFER_TO_SCREEN` and
`SPRITE_TO_SCREEN`, over the quad the main framebuffer occupies on screen
(`resolutionPlusBuffer × outputScale`, viewport-centred, no camera sub-pixel
residual). GUI trixel coordinates, hit testing and `guiTrixelToScreenPx` are
unchanged; only the resolution the triangles are rasterized at differs. The
fragment selects the display triangle for its canvas position and computes the
signed screen-pixel distance to the triangle's three edges. Above effective
display scale 1 it box-filters coverage along the nearest edge against the
texel across it, in premultiplied colour. Effective display scale is
`min(texelScreenPx.x / 2, texelScreenPx.y)`, so anisotropic STRETCH output uses
the limiting axis.

At effective scale 1 or below, the selected lattice triangle receives full
coverage. A scale-1 texel covers 2x1 screen pixels and its triangle is 2x2;
there are only two owned pixel centres. Nearest-edge filtering would make each
exposed owned sample about 0.724 foreground and the adjacent sample about
0.276 foreground, leaving neither a solid stroke nor a clean neighbour. Hard
ownership is the minimum-footprint resolve: it retains the lattice and makes
one-texel strokes solid. Scale 2 and larger retain the box filter unchanged.
Two adjacent texels of the same colour show no seam in either branch. The main
gather skips the GUI canvas
whenever this pass is registered, so a creation that never registers it keeps
the framebuffer path below.

**Framebuffer composite (`gui_pixelated = true`).** The main gather
composites the GUI canvas into the game-resolution framebuffer as
`LOCAL_TRIANGLES`, and it upscales with the world. A triangle is then
`2 × guiScale` framebuffer pixels: at `gui_scale = 1` each triangle is a 2×2
pixel diagonal dash, so this path is only a readable trixel display from
`gui_scale = 2` up, where the pixel-snapped triangles read as pixel art.

A full-resolution GUI canvas (`setGuiCanvasFullResolution`) has square 1:1
texels; the render manager stamps it `RECTANGULAR` and both paths display it
as plain texels.

**One owner for the choice.** `IRPrefab::GuiCanvas` (`gui_canvas.hpp`) holds the
pixelated setting on the `C_GuiCanvasDisplay` singleton (`setPixelated` /
`isPixelated`; `World` applies the `gui_pixelated` config key through it) and
answers `compositesOnScreen()`: true only when the setting is off and
`GUI_CANVAS_TO_SCREEN` is registered. The gather and the screen pass each latch
that answer in `beginTick`, so exactly one of them draws the canvas in a frame.
`test/render/gui_canvas_routing_test.cpp` pins the three states.

## Font size on the lattice

`fontSize` scales a glyph about the text origin. A lattice triangle scaled by
`s` is a lattice triangle of side `s`, tiled by `s²` unit triangles: one texel
in the apex column, three in the next, `2k + 1` in the k-th column out from
the apex, all centred on the source row. `c_text_to_trixel` rasters that set
when a command carries `kGlyphStyleLatticeScale` (stamped from the canvas's
layout at upload), so scaled text keeps the triangle look instead of turning
each glyph texel into a square block; the rectangular layout keeps block
scaling. The condition on the origin parity does not depend on `s`.

## The font

Glyph bodies are six columns wide (`kGlyphWidth`), 11 rows tall, drawn under
the rules above: 2-wide stems in the column slots 0, 2 and 4 that start and
end on even rows, 2-row bars, diamond-chain diagonals, pointed arches for
round letters, no isolated texels. Row 0's triangles reach one row above the
bitmap and row 10's one below, which the 3-row line spacing (`kGlyphStepY =
14`) covers. The design-rule comment at the top of `trixel_font.hpp` is the
contract a new glyph is checked against.

### The small set

A second set (`TextSize::SMALL`, `trixel_font_small.hpp`) draws the same 63
characters in a 4×7 cell with step 6×10. Every text draw takes a `TextSize`
(default `LARGE`), and `glyphMetrics(size)` owns each set's cell, spacing and
step. `fontSize` scales either set the same way.

- **Why 4×7.** Both steps stay even, so every glyph origin keeps the same
  parity. A 5-wide body would centre on x = 2.5, which is not a lattice mirror
  axis, so its symmetric glyphs would render lopsided; a 4-wide body mirrors
  about x = 2. Three separated 1-row bars need rows 0, 3 and 6, so 6 rows
  cannot draw E, S, 8 or 2. Line spacing below 3 rows makes adjacent lines
  touch, so 10 is the smallest step Y: a panel fits 1.4× the rows of the large
  set.
- **Art rules.** The comment at the top of `trixel_font_small.hpp` holds
  them. Strokes are 1 texel wide (a column of texels is a solid strip).
  Full-width bars sit on even rows, since odd rows pair only columns 1-2.
  Diagonals are diamond chains. No glyph has a lone triangle, and no two
  characters share a bitmap. `test/render/trixel_font_test.cpp` checks
  coverage, lone triangles and duplicate bitmaps for both sets.
- **GPU layout.** `FontDataBuffer` holds the large block (128 glyphs × 11
  rows), then the small block (128 × 7). A `GlyphDrawCommand` names its
  glyph's first row (`firstFontRow`), and `styleFlags` bits 16-19 / 20-23 carry
  the cell width / height. `c_text_to_trixel` reads the cell from the command,
  so neither shader holds a glyph-set constant. It runs one thread per texel
  of the large cell and drops the threads outside a command's cell.

`scripts/trixel-font-preview.py` renders the header's glyphs exactly as the
screen composite displays them (`--mode smooth --gui-scale 1 --output-scale
1`; both scales default to 1 and `--upscale` only nearest-magnifies the
result), as the framebuffer path shows them at a given `gui_scale` (`--mode
pixelated --fb-scale 2·guiScale`), or as plain texels (`--mode rect`); run it
before and after editing a glyph. `--set small` reads
`trixel_font_small.hpp`.
