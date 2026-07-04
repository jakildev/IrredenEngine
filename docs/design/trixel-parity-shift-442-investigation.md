# #442 — GL-vs-Metal trixel→framebuffer parity-shift asymmetry

**Issue:** #442 (investigation spike). **Status:** RESOLVED — keep-and-document,
no follow-up fix ticket filed (see ## Decision).

**Host:** macOS / Metal + Linux / GL (both backends read against source; no
runtime change — output is byte-identical to before the spike).

Records *why* the `TRIXEL_TO_FRAMEBUFFER` gather applies the parity-row shift
(`trixelFramebufferSamplePosition`) to its color/depth reads on **GL** but reads
them **raw** on **Metal**, and the reconciliation decision. The in-source
comments at the five sites below carry only the present-tense invariant plus a
one-line backref to this file.

## The parity shift

`trixelFramebufferSamplePosition` (`ir_iso_common.{glsl,metal}`) resolves which
of an iso texel-cell's two diagonal-split triangles a fragment covers, by
conditionally decrementing **`origin.y`** one row (parity bit + a sub-pixel
`fract` test). It only ever adjusts `.y`, never `.x`, and is byte-identical to
CPU `IRMath::pos2DIsoToTriangleIndex` (`ir_math.cpp`) — the picking/hover path
reuses it so GPU and CPU agree on which trixel the mouse is over.

## The asymmetry (canonical why)

Both backends build **identical** per-vertex `TexCoords`, but the vertex clip
position differs: GLSL emits `mpMatrix * aPos` unflipped, while every Metal
`*_to_*` pass negates `out.position.y`. That negate is the standard adapter for
the opposite framebuffer-Y origins — OpenGL's default framebuffer is
**bottom-left** (`gl_FragCoord.y` increases up), Metal's render target is
**top-left** (`position.y` increases down) — so one GL-authored `mpMatrix`
renders right-side-up on both (see `engine/render/CLAUDE.md` §"Metal negates
clip `position.y`; GL does not").

Under those opposite conventions the **same** screen pixel interpolates a
`TexCoords.y` whose floor/fract land it on **opposite** sides of the cell's
diagonal split — a one-row difference exactly at that boundary:

- GL's raw sample lands on the row that needs the `-1` correction, so GL applies
  the shift to all color/depth/id reads.
- Metal's flipped raster already lands the raw sample on the correct row (its
  raster supplies the equivalent one-row correction implicitly), so Metal reads
  color/depth from the **raw** origin.

Applying the GL shift on Metal over-corrects by one row → the 1px iso-diagonal
sawtooth that **#394** introduced and **#438** reverted.

### Ruled-out candidates

- **X-axis / horizontal offset.** The shift touches only `.y`, never `.x`. A
  cause in the horizontal indexing (or in the `originModifier` parity math,
  which is symmetric in x/y) would have shown an `.x` component. The pure-`.y`
  signature is the tell that the cause is the raster-Y origin.
- **Float rounding at the cell boundary.** Both backends run the same
  `floor`/`fract` on the same interpolated `TexCoords` in the same precision;
  swapping the shift sign or the parity branch on Metal did not remove the
  sawtooth (only reintroduced it elsewhere), which a rounding cause would not do.
  The defect tracks the raster-Y convention, not the arithmetic.

## Decision: keep-and-document, not reconcile

Both backends read the **correct** trixel for their own raster convention;
neither samples the wrong one, so this is **not a latent bug** — per the spike's
acceptance criteria, no follow-up fix ticket is filed. Aligning both to one
indexing convention would mean fighting one backend's native framebuffer-Y
origin (re-opening the #394/#438 regression surface) for zero correctness gain.

**Picking is the one shared exception.** Both backends apply the shift to the
*hover* coordinate, because it must match CPU `mouseTrixelPositionWorld()` →
`pos2DIsoToTriangleIndex` (computed independently of GPU raster-Y), even though
only GL applies it to the color/depth gather. On Metal this is why the gather
site computes `originRaw` (color/depth) and `originShifted` (hover id) separately.

## In-source sites (trimmed to the invariant + backref)

- `ir_iso_common.glsl` / `metal/ir_iso_common.metal` —
  `trixelFramebufferSamplePosition` definition.
- `f_trixel_to_framebuffer.glsl` / `metal/trixel_to_framebuffer.metal` — the two
  gather call sites.
- `engine/render/CLAUDE.md` §"Trixel→framebuffer parity shift (GL-only)".
