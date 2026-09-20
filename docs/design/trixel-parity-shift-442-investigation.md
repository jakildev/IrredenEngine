# #442 — trixel→framebuffer parity shift: out of the hover path, both backends

**Issue:** #442 (investigation spike). **Status:** REVISED 2026-08-21 — the
spike's keep-and-document decision froze a real GL defect; the GL gather now
matches Metal (raw color/depth reads, shifted hover compare). REVISED again
2026-09-19 (#3018, architect ruling on PR #3522) — the hover **compare** and
the hover entity-id **read** both moved onto the raw texel; the gather no
longer uses the shift at all in RECTANGULAR display. History of all four
conclusions below.

Records what the parity shift (`trixelFramebufferSamplePosition`,
`ir_iso_common.{glsl,metal}`) is for, which coordinate it applies to, and why
the 2026-07 "GL shifts its color/depth reads; Metal reads raw" asymmetry was a
mis-derivation. The in-source comments at the sites below carry only the
present-tense invariant plus a one-line backref to this file.

## The parity shift

`trixelFramebufferSamplePosition` resolves which of an iso texel-cell's two
diagonal-split triangles a fragment covers, by conditionally decrementing
**`origin.y`** one row (parity bit + a sub-pixel `fract` test). It only ever
adjusts `.y`, never `.x`, and is byte-identical to CPU
`IRMath::pos2DIsoToTriangleIndex` (`ir_math.cpp`).

## Current contract (both backends)

> **Hover identity follows display identity.** The entity id reported for a
> cursor position is the id of the texel whose color that cursor's fragment
> presents (`displayOrigin` in GLSL, `sampleCoord` in Metal). Every fragment
> that participates in the hover write reads the *same* texel, so the
> `HoveredEntityIdBuffer` write is value-identical across writers and needs
> no arbitration. Nothing in the hover path compares in `originShifted`
> space — that shift exists to reconcile the CPU's triangle-lattice index
> with a rectangular display, and it selects a cell straddling two raw
> texels. The shifted index remains the contract of
> `mouseTrixelPositionWorld()` for its other consumers; hover does not
> consume it.

- **Every texture read — color, depth, tier-id and the hover entity id —
  samples the RAW origin.** Both vertex twins
  build **identical** V-flipped `TexCoords` (`vec2(aPos.x, -aPos.y) + 0.5 +
  textureOffset/size` — the GL spelling dates to 2023), and Metal's clip-Y
  negate is cancelled by its own negate in the `framebuffer_to_screen` blit,
  so both backends interpolate the same canvas position for the same final
  screen pixel. The raw sample lands on the correct trixel row on both.
- **The hover COMPARE is raw too.** The CPU supplies the cursor's raw canvas
  texel through `IRRender::mouseCanvasTexelWorld()` — `floor` of the raw
  canvas coordinate, a sibling of `mouseTrixelPositionWorld()` with the same
  frame alignment and no lattice shift — and both gathers gate on
  `floor(displayOrigin) == that`. The hovered fragment set is then exactly
  the fragments that display the cursor's texel; they all read that texel's
  id, so the existing `depth <= hoveredDepth; write` sequence is benign and
  the buffer stays non-atomic. Form shipped: the raw-index gate (not the
  single-writer `gl_FragCoord == cursor pixel` form) — hover only runs on the
  main canvas, which is always `RECTANGULAR` (`displayOrigin == originRaw`;
  private `LOCAL_TRIANGLES` canvases composite with hover disabled), and
  the `_row_above_occupied` fixture reads the voxel under the cursor on every
  frame under both `SubdivisionMode::NONE` and `FULL`.
- **Why the compare could not stay shifted.** `trixelFramebufferSamplePosition`
  decrements `origin.y` for exactly one diagonal-half of every raw texel, so
  the fragment set with `floor(originShifted) == T` is the non-shifting half
  of raw texel `T` **plus** the shifting half of raw texel `T + (0, 1)`. With
  the id read raw, those two halves fetch two different texels' ids into one
  non-atomic slot: the reported id is whichever writer lands last, or (with
  the depth test) the nearer neighbour rather than the texel under the
  cursor. Keeping the read shifted instead reported the row above for every
  fragment the shift fires on (about half, parity bit + `fract` test) — an
  empty row above a voxel's top face reported no entity at all. Neither is a
  contract; the raw compare is.
- **Visible side effect:** the debug hover highlight (`showHoverHighlight`,
  `IRRender::setHoveredTrixelVisible`) now paints the raw rectangular texel
  under the cursor rather than a lattice triangle cell. Standing
  `render-verify` shots run with it off (33/33 unchanged).

**What applying the shift to the color/depth reads does** (the defect
signature, for whoever next suspects this code): a 1-pixel sawtooth on every
iso-diagonal and vertical silhouette edge, plus a garbage dashed line along
the top canvas row (`origin.y - 1` underflowing row 0 into clamp-repeat
texels). Interiors look fine — the two triangles of an interior cell usually
share a face color, so the wrong-triangle read only shows where cell halves
differ (silhouettes, face boundaries, checkerboard content).

## History — how the asymmetry arose and fell

- **#394** applied the shift to Metal's color/depth reads → 1px iso-diagonal
  sawtooth on Metal. **#438** reverted it on Metal only, leaving GL shifting
  all reads.
- **#442 (spike, doc landed 2026-07-04)** documented the residual asymmetry as
  intentional, reasoning from the backends' opposite framebuffer-Y origins
  that GL's raw sample lands one row off. The spike's acceptance was "output
  byte-identical to before" — it froze existing behavior and did **not**
  validate GL's edges pixel-level against a Metal ground truth.
- **2026-08-21 (Windows GL bring-up)**: every archived Windows capture
  (2026-06-25 → 2026-08-21) shows the sawtooth; disabling the GL shift in the
  staged shaders made a 4×-magnified Windows crop **structurally identical to
  the committed `macos-debug` reference** — including the fine per-trixel dash
  pattern on vertical face boundaries, which is the correct appearance — and
  removed the top-row dash artifact. The GL gather was then restructured to
  Metal's raw/shifted split; the full `shape_debug` shot table (21 poses:
  origins, odd offsets, four cardinals, inter-cardinal, pan+pivot) rendered
  clean and the headless GUI test (#2550) passed 30/30, confirming
  hover/pick agreement survives with the hover-only shift.

- **2026-09-18 (#3018, PR #3522 first head)**: the hover entity-id read still
  used the shifted coordinate on both backends, bundled with the compare as
  one claim. The `hover_parity_above_diagonal` shot of `IRShapeDebug
  --gui-test` (`GuiTest::hoveredEntityId`, `HoveredEntityIdBuffer` readback)
  rests the cursor a quarter texel above the diagonal of an isolated voxel's
  top-face texel, where the shifted row is empty canvas: on macOS/Metal the
  shifted read reported `hovered=0` and the raw read reports the voxel; the
  `_below_diagonal` control (shift does not fire) reports the voxel either
  way. That head moved the read raw and kept the compare shifted.
- **2026-09-19 (#3018, architect ruling)**: the Opus recheck showed the
  shifted compare selects a cell spanning two raw texel rows, so a raw read
  under it is a two-writer race on one SSBO slot (measured on Metal: the
  reported id flipped between a voxel and its row-above neighbour from frame
  to frame). Ruling: hover compares raw too (contract block above). The
  `hover_parity_row_above_occupied` shot adds a nearer neighbour voxel one
  canvas row above a second fixture voxel and requires the voxel's id on
  each of the last three live frames: on macOS/Metal the shifted compare
  with a raw read reports the neighbour (`473,473,473`, the depth test hands
  the slot to the nearer writer), the fully shifted master contract reports
  the neighbour every frame too, and the raw compare reports the voxel every
  frame.

Why the #442 derivation was wrong: it modeled the raster-Y difference but not
the **texcoord construction** (identical V-flip on both backends) or the
downstream blit (Metal's second clip-Y negate cancels the first). Net: the
two backends were already texcoord-equivalent, so no per-backend read
asymmetry could be correct — matching what the pixels said all along.

The Linux/GL backend ran the shifted gather over the same period; its
captures carry the same sawtooth (same shader, spec-fixed rasterization —
there is no per-OS freedom in GL's fragment-center mapping). Any GL-host
screenshots or references captured before the fix bake the sawtooth in and
must not be treated as clean baselines.

## Ruled-out candidates (from the original spike, still valid)

- **X-axis / horizontal offset.** The shift touches only `.y`, never `.x`.
- **Float rounding at the cell boundary.** Both backends run the same
  `floor`/`fract` on the same interpolated `TexCoords` in the same precision;
  the defect tracked which coordinate the shift was applied to, not the
  arithmetic.

## In-source sites (trimmed to the invariant + backref)

- `ir_iso_common.glsl` / `metal/ir_iso_common.metal` —
  `trixelFramebufferSamplePosition` definition.
- `f_trixel_to_framebuffer.glsl` / `metal/trixel_to_framebuffer.metal` — the
  gathers (raw `displayOrigin` / `sampleCoord` for every texture read and the
  hover compare; no `originShifted`). One program serves the main,
  background, GUI, and detached entity-canvas composite paths on each backend.
- `ir_render.cpp` — `mouseCanvasTexelWorld()` (the hover index) beside
  `mouseTrixelPositionWorld()` (the lattice index, other consumers);
  `system_trixel_to_framebuffer.hpp` feeds the former into
  `mouseHoveredTriangleIndex_`.
- `creations/demos/shape_debug/main.cpp` — the `hover_parity_*` fixture
  (`--gui-test`): the isolated-voxel pair distinguishes a raw id read from a
  shifted one; `_row_above_occupied` distinguishes a raw compare from a
  shifted one.
- `engine/render/CLAUDE.md` §"Trixel→framebuffer hover: raw texel, no parity
  shift".
