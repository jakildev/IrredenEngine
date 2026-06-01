# Smooth camera Z-yaw via per-axis trixel canvases

Status: **in implementation.** T1 (#1308) and T2 (#1309) have merged; T3
(#1310) is in flight. The framebuffer composite is decided: **Option 4 —
forward-scatter** (see "## Implementation decision" below). This doc is the
architecture for *smooth* continuous world-camera Z-yaw — rotation that
visually interpolates between the 90° cardinals instead of snapping. Read
[`voxel-face-rasterization.md`](voxel-face-rasterization.md) (which faces a
voxel emits) and [`iso-depth-axis-invariant.md`](iso-depth-axis-invariant.md)
first; this builds directly on both.

## Implementation decision (T3 / #1310) — forward-scatter composite

During T3 the empirical sweep (`fleet-run IRShapeDebug --spin-yaw
--auto-screenshot`) confirmed the design doc's flagged "#1 risk": the
per-canvas trixel→framebuffer **parity**. T2 correctly bakes the continuous
center reposition (`roundHalfUp(pos3DtoPos2DIsoYawed(worldPos, visualYaw))`)
per-voxel at write time, so trixel centers land on **mixed-parity** iso cells.
The original stage-2 plan (a gather expansion with the single-global-parity
de-tiling `trixelFramebufferSamplePosition`) cannot de-tile mixed-parity
content → the #1256 stripe/checkerboard at every inter-cardinal yaw. Three
candidate fixes were costed; the decision is **Option 4**:

**Keep T2's per-voxel reposition (it is correct and necessary). Change only
the trixel→framebuffer expansion from a parity-broken _gather_ to a _forward
scatter_:**

1. **Per-axis canvas stays at trixel resolution** (tracks zoom — coarse at
   high zoom; NOT framebuffer resolution). It is a per-axis visible-face
   G-buffer `{ iso-depth, color, entityId }` per cell. **Recommended store:
   face-local in-plane integer coords** (X-canvas → (y,z), Y → (x,z),
   Z → (x,y)) so the grid is a plain regular lattice and parity is a non-issue
   by construction. (Iso-position indexing also works with scatter; face-local
   is the cleaner store.)
2. **Stage 1 (write):** each exposed visible face (visible-triplet × exposed
   mask) `atomicMin`s its shared world-space `pos3DtoDistance` into its axis
   canvas cell; store the depth winner's color + entityId. **Voxel-vs-voxel
   occlusion resolves here, at trixel/face granularity, once** — same order as
   today, preserving the trixel-level occlusion win.
3. **Stage 2 (composite):** forward-scatter each non-empty canvas cell as its
   **true deformed face footprint** — a quad whose 4 corners are the projected
   cube-face corners `P(θ)·(face corner)` (px recovered from stored depth:
   `px = depth − inPlaneA − inPlaneB + 0.5`). Rasterize with the shared
   framebuffer depth buffer (GL_LESS); write color + winning entity-id where it
   wins. The per-pixel depth test composites only the **3 canvases**, not a
   per-voxel competition. **Drop `trixelFramebufferSamplePosition` on this path
   → no single-global-parity assumption → the #1256 stripe class cannot
   occur.**
4. **Cardinal fast path unchanged:** at `residualYaw == 0` the per-axis
   canvases release and the existing single-canvas diamond path runs
   **byte-identical**. Only `residualYaw != 0` takes the scatter path.

The scatter **mechanism** (instanced quads over the trixel grid vs. a compute
`imageStore` splat vs. a forward primitive over a full-screen pass) is an
implementation choice — pick and **measure**. The firm contract is: forward,
true deformed footprint, depth competition at trixel granularity, no parity
inverse.

#### Mechanism chosen (worker, T3 #1310) — canvas-cell scatter with depth-recovered face quads

The implementation scatters **from the per-axis canvas cells**, not from the
compacted voxel list. Rationale: the compacted-voxel SSBO + indirect-args
buffer are *shared* across every canvas's Stage-1 dispatch, so by the time
`TRIXEL_TO_FRAMEBUFFER` runs they hold whichever canvas ran last — not
necessarily the main canvas. The per-axis canvas **textures**, by contrast, are
owned by the main canvas and persist to the framebuffer stage (the same reason
the old gather could read them). So the canvas is the durable G-buffer.

Concretely:

1. **Stage-1 / Stage-2 per-axis store one cell per face center, indexed
   face-locally** (not the `emitDeformedFace` super-sampled cluster T2 wrote).
   The cell is the face's two in-plane world axes — X-canvas → `(y,z)`, Y →
   `(x,z)`, Z → `(x,y)` (`faceInPlaneCoords`) — offset by a camera-tracking
   `faceLocalBase`. `writeDistanceTap(base, encodeDepthWithFace(
   pos3DtoDistance(worldOrigin), slot))` `atomicMin`s the shared world depth into
   that cell; Stage-2 writes its color + entityId on the depth match. **The
   `atomicMin` winner per cell IS the occlusion resolution** — every non-empty
   cell holds the nearest exposed face on its in-plane column. The face-local
   lattice is **dense and collision-free at every yaw** — this is the design's
   §"Recommended store", and the reason it is mandatory rather than optional:
   the originally-shipped iso-position index `perAxisBase + roundHalfUp(
   pos3DtoPos2DIsoYawed(worldOrigin, visualYaw))` collapsed distinct faces onto
   one cell on the compressed axis (the iso projection foreshortens it), so
   `atomicMin` dropped all but one and the dropped faces' footprints showed
   background as **vertical cracks** through the cube (worsening toward ±45°).
2. **The scatter is an instanced draw over the canvas grid** (one instance per
   cell, `drawElementsInstanced` of the shared 6-index quad). The vertex shader:
   reads the cell's stored distance; degenerates (off-screen) if it is the clear
   value; otherwise recovers the **world origin** from the cell by an exact
   integer subtraction — `inPlane = cell − faceLocalBase`, third axis =
   `rawDepth − inPlane.x − inPlane.y` (`faceOriginFromInPlane`, with
   `rawDepth = dist >> 2 = x+y+z`). `slot = dist & 3` recovers the visible-triplet
   slot → `faceId = visibleFaceIds[slot]`. This recovery is **exact and
   trig-free**, so it has neither failure of the iso-inverse it replaces: that
   inverse `z = (rawDepth + P(c+s) − Q(c−s)) / (2cosθ+1)` was singular at the
   **full** `visualYaw` values ±120° / ±240° (denominator zero → garbage origins,
   a speckled cube), because the store keys on full `visualYaw`, not the residual
   the `≥ 1+√2` bound assumes. `faceLocalBase` centers the store on the camera —
   `faceLocalAnchor` recovers the screen-center world voxel via the **un-yawed**
   `isoPixelToPos3D` (never singular) and is computed identically by the store
   and the scatter from the matching `perAxisBase` + canvas size.
3. **The 4 quad corners are the projected face corners** — `perAxisBase +
   pos3DtoPos2DIsoYawed(facePlanePos + inPlaneCornerOffset, visualYaw)` for the
   four in-plane corner offsets of the face's two world axes (`faceSpanCorner`),
   then through the same canvas→clip `mpMatrix` the gather used. Because
   `pos3DtoPos2DIsoYawed` is linear, this is exactly the "P(θ)·(face corner)"
   footprint with the deform implicit — no `faceDeform` matrix needed at the
   framebuffer. Depth is the cell's `normalizeDistance(dist)` (constant per face
   = trixel-granular). Color + entityId come from the cell's color/id textures.
   **No gather, no parity inverse** ⇒ the #1256 stripe class cannot occur.

   **Polarity is applied exactly once, in the store.** The store
   (`c_voxel_to_trixel_stage_{1,2}`) bakes the face plane into the stored cell
   via `faceMicroPositionFixed6` — a POS face stores the high-side plane
   (`origin + 1` on the fixed axis), a NEG face the low-side plane — and the
   stored depth is that plane's iso distance. The recovery (`faceOriginFromInPlane`)
   therefore returns the **face plane**, and `faceSpanCorner` only spans the two
   in-plane axes (it must NOT re-add the polarity). The original cut applied the
   polarity offset a second time in the scatter (a per-`faceId` `+1`); since the
   subdivided store already baked it, POS faces were drawn one micro-cell past
   the plane — a ~1px dark back-face seam between each POS face and its NEG/Z
   neighbours at every non-cardinal yaw of cardinals 1/2/3 (cardinal 0's
   all-NEG triplet was unaffected). Storing the face plane + spanning without
   polarity is the single-application fix.
4. **Cost:** v1 instances over all `size.x·size.y` cells and degenerates empties
   in the vertex shader. If the perf gate (camera parked at ~30°, high zoom)
   shows the empty-cell vertex-shader sweep dominating, the measured follow-up is
   a compute compaction pre-pass that appends non-empty cell indices + writes
   indirect draw args, shrinking the instance count to ≈ visible faces. Recorded
   here so the optimization is scoped, not silent.

#### Status — landed in #1310 (this PR) vs deferred

**Landed + validated (Metal, `--spin-yaw`/`--yaw` sweeps on `IRShapeDebug`):**
voxel forward-scatter composite (the cube renders as a solid, correctly-deformed
3D iso cube at every yaw across a full rotation — verified at 30 / 60 / 120 / 240°
zoomed close-ups); **zero parity / stripe / checkerboard artifacts** (the #1256
class, satisfied by construction). The **face-local in-plane store** (point 1
above) is what makes the faces solid: the first cut shipped the iso-position
index, which dropped compressed-axis faces (vertical cracks through each face)
and went singular at ±120° / ±240° (a speckled cube); the face-local store has
neither failure by construction. A follow-up pass closed the **back-face seam**
— a ~1px dark gap between each POS face and its neighbours at non-cardinal yaw
(cardinals 1/2/3) — by applying the face polarity offset exactly once: the store
bakes the face plane, the scatter spans the in-plane axes only (`faceSpanCorner`,
no second `+1`); see point 3 above. Verified across a full 24-shot zoom-8
rotation: seam-region pixels are the only delta at every inter-cardinal frame
(≤0.13%), and the output is byte-identical at all four cardinals and ±45°
brackets. **Byte-identical at the cardinal fast path**
(`residualYaw == 0` releases the per-axis canvases and runs the unchanged
single-canvas gather — the store/scatter change touches only the `perAxisRoute`
branch); cull-in-yawed-space at both **chunk** (`rebuildChunkBounds`) and
**voxel** (`c_voxel_visibility_compact`, GL + Metal) granularity, gated so the
cardinal path is untouched. The single main canvas's voxel pass is skipped while
rotating so its SDF / text / overlay content composites alongside the smooth
voxels with no double-draw.

**Perf gate — PASSED (`--auto-profile 300`, `IRShapeDebug`, Metal, GPU stage
timing).** Camera parked at a cardinal vs. parked at a non-cardinal 30° (the
sustained steady-state, not just a sweep), at high zoom (8). The composite adds
a **bounded ~0.075 ms/frame**: `voxelStage1` (per-axis G-buffer atomicMin
occlusion) 0.022 → 0.062 ms and `trixelToFb` (forward scatter) 0.044 → 0.078 ms;
every other stage is within noise and `shapePass1` (0.34 ms, SDF) stays the
dominant GPU stage. Frame time is vsync-capped at 120 Hz (p50 8.33 ms) in both —
no per-pixel-per-voxel cliff. Voxel cull stats are identical across yaw
(1062/7820 visible). This confirms the cost model: depth winning stays
trixel-granular, so a non-cardinal rest is a bounded constant factor, not a perf
cliff. `optimize` found no actionable hotspot — the empty-cell instanced vertex
sweep degenerates after a single `texelFetch` + branch and does **not** dominate
(0.078 ms), so the compute-compaction pre-pass below stays correctly deferred.

**Deferred (documented follow-ups, not regressions):**
- ~~**SDF smooth rotation.**~~ **Resolved by T5 (#1345).** `SHAPES_TO_TRIXEL`
  shapes now rotate by the full continuous `visualYaw` (continuous center
  reposition + continuous-yaw surface query) and write the shared world-space
  `x+y+z` depth, so they glide between cardinals and composite by depth with the
  three voxel canvases. SDF needs **no** three-canvas split — it solves the
  surface analytically per pixel, so the architect's "analytic per-shape yaw"
  option was taken instead of the split. Gated per-canvas (main world canvas
  only) so `residualYaw==0` stays byte-identical.
- **±45° rebracket polish.** At the exact bracket edge the swept axis goes edge-on
  as designed; tighten the zero-width face-identity swap under continuous sweep.
- **Lighting / AO on the composite (T4 / #1311).** ~~During rotation the composite
  shows raw voxel color.~~ **Landed in T4 (#1311):** the per-axis voxel canvases
  are lit (AO + sun-shadow + light-volume + Lambert) at trixel resolution before
  the scatter composites them, so rotating voxels show full lighting. See the
  §"Open decisions" → "Lighting / AO placement" resolution.
- **Picking during rotation.** Winning entity-id from the composite — the gather
  fast path still resolves hover at every cardinal; the scatter does not yet
  write the hovered-id SSBO.
- **Compute-compaction pre-pass** (append non-empty cell indices + indirect
  draw args to shrink the scatter instance count). Measured-deferred: the perf
  gate above shows the empty-cell sweep does not dominate, so this is a future
  optimization only if a heavier scene flags it (see Cost above). The perf gate
  itself + `optimize` are **done** (results in the Landed block).

### Rejected alternatives

- **Option 1 — basis-at-expansion (write cardinal even-parity content, apply
  the per-canvas affine + reposition at the trixel→framebuffer pass).
  Geometrically unsound.** A single uniform 2×2 affine of the *collapsed*
  cardinal iso image cannot reproduce true smooth yaw: residual yaw is a 3D
  rotation and the iso projection is rank-deficient (depth collapsed), so the
  best uniform affine fixing a canvas's in-plane axes **mis-places the depth
  (z) axis** by `0.52 / 1.04 / 1.53` iso-px per z-step at φ = 15° / 30° / 45°
  (a one-voxel-tall wall shears visibly wrong). Algebraically
  `D_Z = R(−φ) ⟹ D_Z·(0,2) = (2sinφ, 2cosφ) ≠ (0,2)`. The continuous center
  reposition must stay **per-voxel at write time** — exactly what merged T2
  does. T2's reposition is *not* the bug.
- **Option 3 — basis-aware inverse de-tiling (keep mixed-parity content; make
  `f_trixel_to_framebuffer` de-tile per-canvas via `D`/its inverse). Rejected,
  same root cause.** The de-tiling is a *gather* inverse with one global parity
  bit; with per-voxel continuous centers the screen position carries a
  depth-dependent parallax term (`px·dx(θ)`) no uniform-affine inverse can
  undo.
- **Option 2 — even-parity-snapped reposition. Documented fallback.** Snap the
  per-axis reposition to the nearest even-parity iso cell, keep the existing
  diamond de-tiling. Parity-correct, smallest change, but centers step at
  √2-iso-px (visible stepping at high zoom) — fails the strict "continuous"
  goal. Ship Option 4; keep Option 2 as the escape hatch if rotation-time
  scatter cost proves unacceptable, and note the tradeoff if taken.

### Cost model (perf constraint — must hold)

- Depth winning (voxel occlusion) stays at **trixel/face granularity** in
  stage 1 (`atomicMin`).
- The framebuffer pass is **O(screen pixels)**, a bounded 3-canvas composite —
  not a per-voxel per-pixel competition.
- Extra cost (deformed-footprint fill + 3-canvas overhead) is paid **only while
  rotating**; zero extra at a cardinal.
- **Intrinsic caveat:** at non-cardinal yaw the occlusion axis
  `R_z(θ)·(1,1,1)` is not a lattice direction, so perfectly-stacked voxels no
  longer collapse to one cell — a bounded sliver of occluded faces survives
  stage-1 culling to the framebuffer depth test. Bounded by **visible surface
  area, not volume** (exposed mask), rotation-only. Acceptable; Option 2
  shrinks it further. Measure cardinal vs. mid-rotation frame time at high zoom
  and run `optimize` on the touched stages.

## Problem

Continuous camera yaw is plumbed end to end (`Camera::setYaw` →
`computeYawSplit` → `rasterYaw` cardinal + `residualYaw ∈ [−π/4, π/4]`), and
the per-face *shape* deformation is implemented (`faceDeformationMatrix` →
`faceDeform_[slot]` UBO → `emitDeformedFace`, T-293 / #1257 / #1262 / #1278).

But the rotation **snaps to 90° increments** because the raster applies the
residual only to the *face shape*, never to the *voxel center*:

```glsl
base  = pos3DtoPos2DIso( rotateCardinalZ(p, cardinalIndex) + cardinalLowerCornerShift )  // INTEGER, cardinal-snapped
emitDeformedFace(base, D = faceDeform[slot])   // D skews the diamond AROUND the fixed base
```

So within a ±45° bracket every voxel center is frozen at its cardinal screen
position and only the faces skew; at 45° the whole layout snaps 90°. The
continuous reprojection function (`pos3DtoPos2DIsoYawed(worldPos, visualYaw)`)
exists in the shaders but has **zero callers for positions**.

The history: T-058/T-322 *did* swing centers (rotated the whole canvas image
in screen space) — smooth but bilinear-blurry. T-293 replaced it with crisp
per-face deform but gave up the center swing. **Nobody has shipped crisp *and*
smooth-centered**, because the obvious fix — reproject every voxel center
continuously on the *one* shared canvas — re-introduces tiling seams.

## Why one canvas can't do it

The integer trixel raster only tiles gap-free when voxel centers land on the
**even-parity integer iso lattice** (`(iso.x + iso.y) & 1 == 0`). The 2×3
diamonds interlock on that lattice. Under residual yaw the three visible faces
deform *differently* — one axis's faces go **skinny** (compressed), one
**stretched**, the Z-face **rotates** — so for any given screen pixel the
occupant could be an X-face, a Y-face, or the Z-face, each at a different
scale. Forcing all three onto one canvas makes their supersampling taps
collide on shared pixels: they fight, and you get seams / checkerboard / the
#1256 lattice artifact.

**The existing residual face-deform is exactly this single-canvas
approximation.** `faceDeformationMatrix` → `faceDeform_[slot]` →
`emitDeformedFace` (T-293 / #1262) deforms each face's diamond on the one shared
grid, anchored at the cardinal-snapped center — three independent deformations
forced through one fixed parity/tiling. It degrades worst toward ±45° and is a
prime suspect for the inter-cardinal artifacts. So this work is **part
replacement, not pure addition**: the `faceDeformationMatrix` *math* is reused
as each axis-canvas's uniform basis, but the single-canvas application is
removed. A cheap, independently-useful first step is to **confirm empirically**
what the current deform actually renders across a continuous sweep on fresh
`master` (`--spin-yaw`, native ROI crops) — to separate the single-canvas
mismatch from any other bug before the rework lands.

## Core idea: three per-axis trixel canvases

Split the voxel→trixel raster into **three trixel canvases, one per face
axis** (X / Y / Z — each holds whichever ± face of its pair is currently
visible). Each canvas carries **one uniform deformation** for all voxels, so
each is internally gap-free (a uniform affine map of a regular sub-lattice is
still a regular lattice). The three are unified only at the framebuffer, by
depth.

```
voxel ─┬─ X-face ─→ X-axis trixel canvas  (uniform D_X, "skinny" basis)
       ├─ Y-face ─→ Y-axis trixel canvas  (uniform D_Y, "stretched" basis)
       └─ Z-face ─→ Z-axis trixel canvas  (uniform D_Z, in-plane-rotated basis)
                         │
                three trixel→framebuffer passes (each with ITS OWN basis)
                         │
              framebuffer depth composite: per pixel, nearest distance wins
```

_Basis labels ("skinny", "stretched", "in-plane-rotated") describe each canvas at
non-zero residualYaw. At `residualYaw == 0` all three collapse to the same cardinal
basis — the fast path in §Pipeline._

When *drawing a voxel*, each visible face is routed to its axis-canvas; that
canvas decides **which trixel the face occupies** from a calculation on the
camera rotation and the voxel's world position, using that canvas's own
deformed geometry.

## Per-canvas geometry varies; depth does not

- **Geometry (basis + extent) is per-canvas and yaw-dependent.** Each canvas
  is a uniform affine map (`D_X` skinny, `D_Y` stretched, `D_Z` rotated) of
  the voxel lattice. The trixel→framebuffer **basis (slopes)** and the grid
  **width/depth** therefore differ per canvas and change with yaw — the
  stretched axis's trixels expand to larger framebuffer footprints and its
  grid spans more; the skinny axis's span less. This is the key departure from
  today's single fixed grid: **three independently-sized, independently-sloped
  grids.**
- **Depth is the shared invariant.** The value written per trixel is
  `pos3DtoDistance` of the voxel-face's world position — one world-space
  scalar, computed identically regardless of which axis-canvas it lands on. It
  does **not** vary per canvas. That is what makes the framebuffer composite
  valid: overlay the three canvases, take the nearest distance per framebuffer
  pixel, and the correct face wins. Holds for **yaw only** — Z-yaw preserves
  the iso-depth axis so the metric stays comparable; pitch/roll would break it
  (see iso-depth-axis-invariant.md).

## Pipeline

1. **Stage 1 — voxel → three axis canvases.** Camera yaw θ and the visible
   triplet are known. For each voxel, route each visible face to its
   axis-canvas; compute the owning trixel from θ + world position via that
   canvas's geometry; write **shared `pos3DtoDistance` + color**. Voxel centers
   are **continuously repositioned** (`pos3DtoPos2DIsoYawed`) — now safe,
   because within a canvas the deform is one consistent affine.
2. **Stage 2 — forward-scatter composite (superseded by the Option 4 decision
   above).** Each non-empty canvas cell is **forward-scattered** as its true
   deformed face footprint (a quad of projected cube-face corners
   `P(θ)·corner`), depth-tested into the **shared framebuffer depth buffer**;
   color + winning entity-id are written where it wins. This replaces the
   original gather wording ("each canvas expands its trixels … using its own
   basis") — a gather inverse assumes a single global parity and stripes on
   mixed-parity content (#1256). Forward scatter writes each cell to its exact
   footprint, so there is **no parity inverse** and the stripe class cannot
   occur. See "## Implementation decision" for the firm contract.
3. **Stage 3 — winner resolution.** Nearest depth across the three canvases
   wins per framebuffer pixel. With forward scatter this is the per-pixel
   framebuffer depth test over the 3 scattered canvases — not a per-voxel
   competition.

   > **⚑ Depth-metric correction (#1370).** The composite depth must be the
   > iso-depth in the **yawed** frame — `pos3DtoDistance(R_z(-visualYaw)·world)`
   > = `x(cosφ−sinφ) + y(sinφ+cosφ) + z` — **not** the un-yawed world `x+y+z`
   > that the rest of this doc (and the original implementation) describes. The
   > scatter projects screen position with the full `visualYaw`
   > (`pos3DtoPos2DIsoYawed`), so ordering by the un-yawed `x+y+z` diverges from
   > the on-screen placement as residual yaw grows — a low/back surface (the
   > ground platform) then wins the depth test against geometry above it near
   > the ±45° bracket. The fix re-derives the composite depth from the recovered
   > origin rotated by `R_z(-visualYaw)` in **both** the per-axis voxel scatter
   > (`v_peraxis_scatter` / `peraxis_scatter.metal`) and the SDF smooth-yaw path
   > (`c_shapes_to_trixel`), so the two stay co-sorted. The stored face-local
   > `rawDepth` (the origin-recovery key) is unchanged; only the composite depth
   > is rotated. Where this doc says "shared world-space `x+y+z` / `pos3DtoDistance`"
   > for the composite, read it as the yawed metric above (identical at
   > cardinals, where `R_z(-visualYaw)` permutes `x+y+z`).
4. **Fast path.** At `residualYaw == 0` the three canvases collapse to today's
   single uniform grid → byte-identical to current behavior, zero extra cost
   when not rotating.

## Bounded textures + minimum on-screen trixel size

The canvases are dynamic (geometry changes with yaw), so the textures are
allocated **once at the worst-case size** and reused — never reallocated
per-frame. The bound:

- Residual is capped at ±45° (beyond that we rebracket), so the
  **stretched / rotated** axis maxes at **√2× the cardinal extent** — bounded.
  (At φ=45°, `faceDeformationMatrix`'s stretched column has length √2; the
  Z-face rotation's bounding box grows by √2.)
- The **skinny** axis is the unbounded risk: as a face goes edge-on its
  on-screen width → 0, so naively covering the screen needs → ∞ trixels. The
  floor is a **minimum on-screen trixel size** (≈ 1 framebuffer pixel): once a
  trixel would render below it, the face is edge-on and its screen area is
  **already covered by the other two visible faces** (the hexagonal voxel
  footprint is always fully covered by the triplet), so the canvas resolution
  is capped there with no gap.

So each axis-canvas texture is sized once to `√2 × cardinal extent` in
footprint and `screen-resolution / min-trixel-size` in density. Bounded,
finite, no per-frame reallocation. The min-trixel-size value is a tuning
decision (see Open decisions).

## Face-jump at the rebracket

As θ sweeps through ±45° the cardinal flips and the **visible ± face on the
swept axis swaps** (e.g. the X-axis canvas: −X face shrinking toward edge-on →
+X face arriving edge-on). This is clean **by construction**: the swap instant
is exactly when both faces are edge-on (zero on-screen contribution), so the
outgoing face has shrunk to nothing and the incoming grows from nothing — no
pop. The min-trixel-size cap and the rebracket coincide. The implementation
inherits the existing rebracket machinery (`rasterYaw` cardinal snap, residual
sign flip, `cardinalLowerCornerShift`); the new requirement is that each
axis-canvas's **face identity swaps at the zero-width instant**, validated
under continuous rotation (#1271 `--spin-yaw`).

## Parity in the trixel→framebuffer conversion (resolved by forward scatter)

This was flagged as the **#1 implementation risk**, and T3's empirical sweep
confirmed it: the single-canvas trixel raster lives on the **even-parity iso
sublattice** (`(iso.x + iso.y) & 1 == 0` — the set integer voxels project to;
see the SDF/voxel parity note in `engine/render/CLAUDE.md`), and the gather
de-tiling (`trixelFramebufferSamplePosition`) assumes a single global parity.
T2's per-voxel continuous reposition lands centers on **mixed-parity** cells,
which the gather cannot de-tile → the #1256 checkerboard / lattice artifact.

**The Option 4 forward-scatter resolves parity _by construction_.** Each
non-empty canvas cell is scattered forward to its exact deformed footprint, so:

- there is **no gather inverse** and therefore **no single-global-parity
  assumption** — the stripe/checkerboard/double-write class cannot occur,
  regardless of where the per-voxel reposition lands a center;
- with the recommended **face-local in-plane store** (X→(y,z), Y→(x,z),
  Z→(x,y)) the per-axis grid is a plain regular lattice, so parity is a
  non-issue in the store as well as the composite;
- CPU and GPU must still classify half-integer positions identically — use
  `roundHalfUp`, never `glm::round` (the CPU/GPU consistency rule in
  `.claude/rules/cpp-math.md`) — for the depth-px recovery
  (`px = depth − inPlaneA − inPlaneB + 0.5`) and footprint-corner projection.

Parity is now a **regression check**, not an open design risk: still validate
with continuous-rotation sweeps + native-resolution ROI crops inspected for
checkerboard / lattice / seam artifacts (`render-debug-loop`, `tools/img_diff`)
so a future change can't silently reintroduce a gather inverse.

## Relationship to per-entity SO(3) (#1272)

This split is clean for the **camera** because there is **one global
rotation** — every voxel's X-face shares `D_X`, so each axis-canvas has a
single uniform deform. **Per-entity SO(3) (#1272) is a different beast:** each
entity rotates independently, so faces on the same axis would carry *different*
deforms and the uniform-canvas property breaks. Per-entity smooth rotation
therefore stays on its own mechanism — detached canvases (true smooth, own
render target) or the octahedral-snap + per-entity residual main-canvas path
scoped in #1300. **Camera-smooth-yaw and per-entity-smooth-rotation are two
different mechanisms**; do not try to force #1272 through the three-axis-canvas
split.

## Open decisions (resolve during implementation)

- **Lighting / AO placement. — RESOLVED (T4 / #1311): trixel-level per-axis.**
  Light each of the three per-axis voxel canvases *before* the framebuffer
  scatter composites it — run AO + sun-shadow + light-volume + `LIGHTING_TO_TRIXEL`
  over each axis canvas, gated on `residualYaw != 0`. The framebuffer-resolution
  **MRT G-buffer / post-composite per-fragment** approach (the pre-implementation
  findings note below) is **rejected**: the engine keeps lighting at trixel
  resolution (hard product constraint — no fragment-rate lighting). The two
  cross-canvas maps stay **world-space and SHARED** — the sun-shadow depth SSBO
  and the 128³ light volume are sampled by world-pos reconstructed per-axis
  (`perAxisCellToWorld3D`, the exact `faceOriginFromInPlane` inverse), never from
  a resolved framebuffer (which would hit the singular screen→world iso-yawed
  inverse). AO is same-axis ⇒ runs correctly on each axis canvas's in-plane
  lattice. 3× AO + 3× sun-shadow textures live on `C_PerAxisTrixelCanvases`
  (rotation-only lifecycle); the world volume + sun map are NOT per-axis. The
  sun-shadow bake reads main (SDF/text) **+** all three per-axis voxel canvases
  into the shared world sun map (pre-composite `atomicMin`), so voxels and SDF
  shadow each other under rotation.
- **Memory / allocation policy.** Three worst-case textures during rotation;
  gate allocation on `residualYaw != 0` so static scenes pay nothing. Define
  the allocate/free churn behavior at rotation start/stop.
- **`min-trixel-size` value.** ≈ 1 framebuffer pixel is the starting point;
  tune against seam-free coverage vs texture size.
- **Per-canvas basis derivation.** Exact mapping from `residualYaw` (and
  cardinal) to each canvas's basis + extent; reuse `faceDeformationMatrix`.

### T4 (#1311) implementation note — lighting placement requires a structural pipeline change (worker findings, pre-implementation)

> **SUPERSEDED by the §"Open decisions" → "Lighting / AO placement" resolution
> above (architect direction, 2026-05-30).** This note proposed a framebuffer
> MRT G-buffer + post-composite per-fragment pass; that was **rejected** in
> favour of trixel-level per-axis lighting (the per-axis voxel canvases are lit
> before the scatter; the sun-shadow SSBO + 128³ light volume stay world-space
> and shared). The pipeline map below is accurate and is what made the per-axis
> approach clean; only its *recommended mechanism* (pieces 1–3) is obsolete.
> As-shipped: AO + sun-shadow + light-volume + `LIGHTING_TO_TRIXEL` run over each
> per-axis canvas (`perAxisRoute != 0`), reconstructing world-pos via
> `perAxisCellToWorld3D`; `BAKE_SUN_SHADOW_MAP` bakes main + 3 per-axis into the
> shared sun map. No framebuffer MRT; no per-fragment lighting.

Mapping the current pipeline against T4's "AO / sun-shadow / lighting on the
resolved composite" scope surfaced that **every lighting stage today runs
pre-composite, in canvas/iso space** — none is post-composite:

- `COMPUTE_VOXEL_AO`, `COMPUTE_SUN_SHADOW`, `COMPUTE_LIGHT_VOLUME`,
  `LIGHTING_TO_TRIXEL` all read the main canvas `trixelDistances` (binding 0,
  r32i) + `visibleFaceIds[slot]` and write canvas-resolution textures
  (`canvasAO`, `canvasSunShadow`) consumed by `LIGHTING_TO_TRIXEL`, which
  modulates the canvas color **before** `TRIXEL_TO_FRAMEBUFFER` runs.
- `BAKE_SUN_SHADOW_MAP` projects the main canvas `trixelDistances` into the
  sun-aligned depth map (slot 28) — also pre-composite, canvas-space.
- During rotation the main canvas's voxel pass is skipped (T3), so the main
  canvas `trixelDistances` holds **only SDF/text** — the smooth voxels live in
  the per-axis canvases and reach the framebuffer via the scatter, carrying no
  lighting. That is the "raw voxel color while rotating" symptom T4 fixes.

So there is **no post-composite lighting infrastructure to extend** — T4 builds
it. Three distinct pieces fall out:

1. **G-buffer producer (new).** The main framebuffer is single color + depth
   (`engine/render/include/irreden/render/framebuffer.hpp`; `C_TrixelCanvasFramebuffer`).
   Post-composite lighting needs an **MRT G-buffer** — recommend one extra
   attachment carrying per winning pixel: **world position** (the scatter
   already recovers the face-plane `origin` via `faceOriginFromInPlane`; write
   it flat) + **faceId (0..5) + material-tag (voxel/sdf/text/bg)**. Storing
   world-pos directly sidesteps the singular screen→world iso-yawed inverse
   (`z = (rawDepth + …)/(2cosθ+1)`, zero at full visualYaw ±120°/±240°) the
   gather path can't use — AO neighbor sampling and light-volume/sun-shadow
   projection then run in framebuffer space off the stored world-pos. Both
   the scatter (`*_peraxis_scatter`) and the cardinal gather
   (`*_trixel_to_framebuffer`, which composites SDF/text during rotation) write
   it, **on both GL and Metal**. MRT on the main framebuffer is the highest-risk
   sub-change (Metal render-pass + pipeline-state color-attachment count/format
   must match every framebuffer-writing shader).
2. **Generic lighting consumer (new).** One post-composite compute pass reading
   the G-buffer: directional (`faceOutwardNormal6(faceId)` Lambert) + light-volume
   (`texture(lightVolume, worldPos→coord)`) + AO (the four-tangent neighbour walk
   from `c_compute_voxel_ao.glsl`, retargeted to framebuffer pixels with
   `pos3DtoPos2DIsoYawed` offsets, **gated on the voxel material-tag** so SDF
   pixels take the ambient default per #1345). Gated on `residualYaw != 0`; the
   cardinal path keeps its byte-identical per-canvas lighting untouched.
3. **Sun-shadow on the composite (structural reorder).** The issue's "side
   effects to evaluate" flags this: `BAKE_SUN_SHADOW_MAP` runs **pre-composite**,
   so during rotation it bakes only SDF/text, not the smooth voxels. Making the
   bake "consume the composite" means **reordering it after the framebuffer
   composite** (bake from the resolved world-pos G-buffer) — a render-pipeline
   ordering change that interacts with the cardinal lighting order and must keep
   `residualYaw == 0` byte-identical.

Piece 3 is the open call: whether T4 ships as **one large PR** (heavier than the
T3 #1336 baseline of 51 files / ~1.1k lines, plus new MRT infra) or **splits**
into **T4a** (pieces 1+2 — G-buffer + directional/AO/light-volume) and **T4b**
(piece 3 — sun-shadow bake reordered onto the composite) is an architect
decomposition decision (and the bake-reorder mechanism wants the architect's
direction). Resolve before implementation lands.

## Decomposition (implementation tickets)

Stacked; each blocks on the previous and on this design doc's PR merging
(architect docs-first discipline). All `[opus]` — hottest compute path +
parity correctness.

- **T1 — per-axis canvas infrastructure** + bounded worst-case texture sizing +
  min-trixel-size cap + `residualYaw == 0` fast-path collapse to the single
  canvas (byte-identical guarantee).
- **T2 — Stage-1 routing + per-canvas deformed geometry** + continuous center
  reposition (`pos3DtoPos2DIsoYawed`); write shared `pos3DtoDistance` + color
  per axis-canvas.
- **T3 — forward-scatter trixel→framebuffer composite** (Option 4) +
  face-jump/rebracket cleanliness; the scatter (no parity inverse) is the core
  of this ticket. Also widen the cull to the rotated √2 footprint (off-center
  geometry is otherwise culled from the cardinal-snapped viewport — the
  "most objects missing during rotation" symptom).
- **T4 — AO / lighting on the resolved composite** (winning face-id / entity-id
  carried through the scatter; no double-rotation drift).
- **T5 — SDF shapes under the composite (#1345, landed).** `SHAPES_TO_TRIXEL`
  rotates by the full continuous `visualYaw` and writes the shared world-space
  `x+y+z` depth so analytic SDF shapes glide between cardinals and composite by
  depth alongside the three voxel canvases. No three-canvas split for SDF (it
  solves analytically per pixel); stacked on T3 and independent of T4. Writing
  the SDF normal + material into T4's composite G-buffer (for post-composite
  lighting of SDF) is deferred until that G-buffer exists.

## Cull is NOT the "missing objects" cause — composite/canvas-write is (#1310 finding)

**Empirically established (2026-05-29, IRShapeDebug `--spin-yaw --auto-screenshot
12 --zoom 4`, Metal):** force-disabling the cull entirely during rotation
(every chunk visible + per-voxel compaction cull relaxed to a pass-through) does
**not** restore the off-center objects — the inter-cardinal frame still shows
only the ~2 screen-centre objects. So the architect's addendum-#2 hypothesis
("the missing objects is a cull bug") does not hold for the **current** scaffold:
the off-center voxels are already compacted, and the bottleneck that hides them
is the **SUPERSEDED gather composite / per-axis canvas write** (`drawPerAxisComposite`
+ the stage-1 per-axis canvas placement), not the cull. The forward-scatter
stage-2 (Option 4) is what makes them appear, because it scatters each per-axis
cell to its true deformed screen footprint instead of gather-de-tiling a single
zoomed canvas.

**Cull-in-yawed-space is still required — as a prerequisite, landed *with* the
scatter (not before).** Once the scatter shows off-center geometry, those voxels
must actually reach the per-axis canvases, which means the **compaction** cull
must include them. Today's cull (CPU chunk mask `buildChunkVisibilityMask` +
GPU per-voxel test in `c_voxel_visibility_compact`) both compute
**cardinal-snapped** iso (`pos3DtoPos2DIso(rotateCardinalZ(...))`), so with the
scatter in place they would re-drop off-center voxels. The fix is to compute the
cull from the **same `pos3DtoPos2DIsoYawed`** the raster uses, at **both** chunk
(CPU, `rebuildChunkBounds`) and voxel (GPU compact shader, **both backends**)
granularity, widened by the deformed-face √2 footprint, gated on the per-axis
canvases being allocated so the cardinal fast path stays byte-identical. Land
this together with the scatter so it is visually verifiable (a standalone cull
change produces no observable difference while the gather composite is the
limiter — confirmed above).

## What to verify when implementing

1. **Parity.** No checkerboard / lattice / seam / double-write per canvas at
   *every* yaw in the bracket — native ROI crops, not downscaled full-frames.
   Satisfied **by construction** under forward scatter (no gather inverse);
   the sweep is now a regression check.
2. **Smoothness.** Centers swing continuously by distance from the rotation
   center; no 90° layout snap. `--spin-yaw` continuous sweep.
3. **Rebracket.** Face-jump is invisible (swap at zero width); no pop at ±45°.
4. **Byte-identical at cardinal.** `residualYaw == 0` matches current master
   pixel-for-pixel (fast path).
5. **Bounded memory.** Textures sized once to the worst case; no per-frame
   reallocation; no unbounded growth as a face goes edge-on.
6. **Depth composite.** Correct occlusion across the three canvases under
   rotation (shared world-space `pos3DtoDistance` metric).
7. **Perf gate.** Depth competition stays at trixel granularity (stage-1
   `atomicMin`); cardinal vs. mid-rotation frame time at high zoom differs only
   by the bounded fill/composite overhead, not a per-pixel-per-voxel blowup.
   Run `optimize` on the touched stages.
8. **Arbitrary topology.** Disconnected clusters, holes, lone voxels, and
   non-convex shapes all render correctly — no solidity assumption. Add a
   non-solid arrangement to the verification sweep.
9. **Cull coverage.** Off-center geometry that the rotated √2 footprint reaches
   is not culled from the cardinal-snapped viewport (all on-screen objects
   composite during rotation, not just the screen-center ones).
