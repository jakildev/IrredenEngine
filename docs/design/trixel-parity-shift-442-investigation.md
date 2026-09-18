# #442 — trixel→framebuffer parity shift: hover-only, both backends

**Issue:** #442 (investigation spike). **Status:** REVISED 2026-08-21 — the
spike's keep-and-document decision froze a real GL defect; the GL gather now
matches Metal (raw color/depth reads, shifted hover compare). REVISED again
2026-09-18 (#3018) — the hover entity-id **read** moved onto the raw texel
too; only the hover **compare** keeps the shift. History of all three
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

- **Every texture read — color, depth, tier-id and the hover entity id —
  samples the RAW origin.** Both vertex twins
  build **identical** V-flipped `TexCoords` (`vec2(aPos.x, -aPos.y) + 0.5 +
  textureOffset/size` — the GL spelling dates to 2023), and Metal's clip-Y
  negate is cancelled by its own negate in the `framebuffer_to_screen` blit,
  so both backends interpolate the same canvas position for the same final
  screen pixel. The raw sample lands on the correct trixel row on both.
- **The hover COMPARE coordinate IS shifted, on both backends.** It must
  match CPU `mouseTrixelPositionWorld()` → `pos2DIsoToTriangleIndex`
  (computed independently of GPU raster-Y), so shift-space is where the
  GPU fragment and the CPU index meet. Both gathers therefore compute
  `originRaw` (every texture read) and `originShifted` (the compare only)
  separately. The shift is not a property of the canvas storage: the stage-2
  writers store color and entity id at the same texel, so once a fragment has
  passed the raw color/depth hover gate, the id it reports has to come from
  that same raw texel. Reading it at the shifted coordinate made the reported
  id come from one row up for every fragment the shift fires on (about half of
  them, parity bit + `fract` test) — an empty row above a voxel's top face
  reported no entity at all.

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

- **2026-09-18 (#3018)**: the hover entity-id read still used the shifted
  coordinate on both backends, bundled with the compare as one claim. The
  `hover_parity_above_diagonal` shot of `IRShapeDebug --gui-test`
  (`GuiTest::hoveredEntityId`, `HoveredEntityIdBuffer` readback) rests the
  cursor a quarter texel above the diagonal of an isolated voxel's top-face
  texel, where the shifted row is empty canvas: on macOS/Metal the shifted
  read reported `hovered=0` and the raw read reports the voxel; the
  `_below_diagonal` control (shift does not fire) reports the voxel either
  way. Both gathers now read the id at the raw texel. The compare is
  untouched — moving it off shift-space would break the CPU/GPU index match
  the shot's control relies on.

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
  gathers (raw `displayOrigin` / `sampleCoord` for every texture read,
  `originShifted` for the hover compare only). One program serves the main,
  background, GUI, and detached entity-canvas composite paths on each backend.
- `creations/demos/shape_debug/main.cpp` — the `hover_parity_*` fixture
  (`--gui-test`), the only check that distinguishes the raw id read from the
  shifted one.
- `engine/render/CLAUDE.md` §"Trixel→framebuffer hover parity shift".
