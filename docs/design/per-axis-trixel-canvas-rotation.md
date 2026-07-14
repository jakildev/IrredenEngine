# Smooth camera Z-yaw via per-axis trixel canvases

> **Store-key update (supersedes the #1310 in-plane store below).** The per-axis
> store is now keyed by the **un-yawed (cardinal) iso pixel**
> `perAxisBase + pos3DtoPos2DIso(facePos)`, recovered with `isoPixelToPos3D`.
> The #1310 **face-local in-plane** `(y,z)/(x,z)/(x,y)` index this replaced is
> collision-free only for a *single connected surface*: it collapses **separate
> objects stacked along the fixed axis** (same in-plane column, different depth)
> onto one cell, dropping the back object's face under camera yaw even though it
> is screen-separated. The un-yawed iso key is the only lossless 2D key (depends
> on all three coords → screen-unique; un-compressed → no #1310 cracks).
> "Store" / "Recover" sections that still describe `faceInPlaneCoords` /
> `faceLocalBase` / `faceOriginFromInPlane` below are the historical #1310 design
> and read as superseded; the §"Mechanism chosen" store/recover steps and the
> §"Recommended store" are current.

Status: **in implementation.** T1 (#1308) and T2 (#1309) have merged; T3
(#1310) is in flight. The framebuffer composite is decided: **Option 4 —
forward-scatter** (see "## Implementation decision" below). This doc is the
architecture for *smooth* continuous world-camera Z-yaw — rotation that
visually interpolates between the 90° cardinals instead of snapping. Read
[`voxel-face-rasterization.md`](voxel-face-rasterization.md) (which faces a
voxel emits) and [`iso-depth-axis-invariant.md`](iso-depth-axis-invariant.md)
first; this builds directly on both.

## Current contract — view-visibility overflow lane (epic #2331, 2026-07-14)

> Supersedes §"Per-axis store occlusion model — engine invariant (established
> #1457, 2026-06-10)" below, and the "two faces … never share a cell" /
> "collide *only* when they share a cardinal screen pixel" claims in
> §"Implementation decision" and §"Mechanism chosen". Those were written for
> a single-object, single-face-set census and are correct as far as they go —
> the cardinal-keyed store's `atomicMin` election really is uncontested
> *within one cell*. What they miss: the store's key is the **un-yawed
> (cardinal)** iso pixel, so the store's one-winner-per-cell census is the
> **cardinal-visible** face set, not the **view-visible** one the live camera
> needs. The two sets differ under residual yaw; see below.

### The two-set model

Two faces separated by a world offset `t·(1,1,1)` (a "coset pair") project to
the **same** un-yawed iso pixel and therefore the **same** store cell —
that part of the historical text is exactly right, and it is why the
un-yawed key is still the correct, lossless-for-*recovery* store key (it is
injective: `(cell, rawDepth) → world` inverts exactly at every yaw). But
"share a cell" only means "compete for one winner slot," not "only one of
them is visible." Under a residual yaw θ the two faces separate on screen by

```
Δiso = t · (−2·sinθ, 2 − 2·cosθ)      // pos3DtoPos2DIsoYawed((1,1,1)·t, θ)
```

— ≈0.35 iso px per unit `t` at 10°, ≈1 px per unit at 30°. Once that
separation exceeds sub-pixel, BOTH members can be view-visible even though
the store elects only one; the coset loser is visible on screen but was
never written anywhere the scatter can read. This needs ≥2 view-visible
faces in one `(1,1,1)` coset to manifest — a single convex object never
triggers it (a plane contains no two points differing by `t·(1,1,1)`), which
is why the T1–T4 per-axis rollout's own dense-cube sweeps never caught it;
the default `voxel_set` wave scene (per-cell single-voxel sets, phase riding
`x+y+z`) is the epic #2331 worst case. The election metric's sign also
inverts past 120° full yaw (`2·cos(visualYaw) + 1 < 0`), so in the
(120°, 240°) quadrant range the store keeps the view-**farthest** coset
member instead of the nearest — worse than a coin flip, not just a coverage
gap.

### The overflow lane (C1 #2333, C2 #2334)

A bounded **overflow lane**, additive and rotating-only, carries exactly
`viewVisible ∖ cardinalWinners` — the set the cardinal store cannot
represent regardless of which election metric it sorts by. It rides the
existing per-axis stage-1 kernel
(`c_voxel_to_trixel_stage_1_body.{glsl,metal}`) as two extra `resolveMode`
passes over the same per-axis face geometry, dispatched per rotating frame
in this order (barrier between each group — the mask must be complete
across ALL axes before any append test, since view visibility competes
across axes, not per-axis):

```
stores ×3 (mode 0)  →  view mask ×3 (mode 2)  →  overflow append ×3 (mode 3)
  →  per-axis {election (mode 1) → stage 2} ×3
```

1. **View mask (mode 2, `viewMaskTap`)** — every per-axis face `atomicMin`s
   its quantized **yawed** depth (`overflowYawedDepthKey`, 1/16-world-unit
   steps, biased into `uint` range) into a scratch region keyed by its
   **yawed** screen cell (`overflowYawedPixel` — the same
   `pos3DtoPos2DIsoYawed` projection the scatter uses, so mask cells and
   scattered quads agree exactly).
2. **Overflow append (mode 3, `overflowAppendTap`)** — a face appends
   `{cardinal cell, encoded distance, colorPacked}` (3 packed uints, the
   exact `(cell, rawDepth)` pair the store would have written plus the raw
   color) to a capped array iff (a) its yawed depth is within
   `kOverflowDepthEpsSteps` (8 steps ≈ half a world unit) of its view-mask
   cell's winner — view-visible, with enough tolerance to absorb
   quantization ties without admitting a genuinely occluded coset loser (the
   nearest coset pair separates by ≥ ~2.7 world units of yawed depth) —
   **and** (b) it is NOT its own cardinal cell's settled store winner (the
   same match test the cell-path recovery already does). An atomic counter
   tracks the append index against a hard cap; on overflow the add is paired
   back off so `instanceCount` settles at exactly `min(appends, cap)`, and a
   drop counter increments for a one-shot CPU warn
   (`warnOverflowDropsIfAny`) — never silent.
3. **Scatter** — `v_peraxis_scatter.glsl` draws a second instanced pass over
   the overflow array (`overflowMode` uniform) after the normal per-cell
   pass, decoding each entry through the **identical** recovery path the
   cell path uses (`isoPixelToPos3D` + the #1458 fractional-offset decode,
   including the #2207 flip bit carried inside the packed distance) — no new
   recovery math. Overflow quads sit two composite-depth tie-bands behind
   the cell-path draw (`vDepth += 16.0 * kScatterCellTieStep`), so they only
   ever fill pixels no cell quad claims — necessary near the 120°/240°
   coset-depth degeneracy, where every coset member ties in view depth and
   an unbiased tie would hand roughly half the surface to unlit entries.
4. **Lighting (C2, `c_light_overflow_faces.{glsl,metal}`)** — a bounded
   compute pass at the tail of `LIGHTING_TO_TRIXEL`, dispatched while the
   sun-shadow map and 128³ light volume are still bound, recovers each
   overflow entry's world position + face normal from the same 3-uint entry
   the scatter reads (`perAxisCellToWorld3D` + `faceOutwardNormal6`) and
   relights it in place (sun cascade + light volume + Lambert, `AO = 1.0`,
   mirroring `c_lighting_to_trixel`'s own world sample) before the scatter
   draws it — so the rotating frame shows lit slivers, not flat albedo.

**Binding.** No new permanent binding. The view-mask + ctrl-block +
overflow-entries scratch rides `kBufferIndex_PerAxisResolveScratch` — the
same transient per-axis-window reuse #2255's winner-id scratch already
established (dead during the store window). C2's relight reuses every
resource the per-axis lighting pass already bound; its one new binding
(`kBufferIndex_OverflowLightingScratch`) lands on a slot dead **during
lighting** specifically — slot 28 there holds the sun-depth map it samples,
not the per-axis resolve scratch (which is live during lighting via
#1435's resolve consumer).

**Gating / cardinal fast path.** Every pass above is gated on per-axis
canvas allocation (`residualYaw != 0`); at `visualYaw == 0` none of it
dispatches, so the cardinal path stays byte-identical — confirmed by
kill-switch A/B (`IR_OVERFLOW_LIGHTING_DISABLE`): 0.92% drift ≈ the ~0.86%
same-config run-to-run non-determinism baseline (#2255), i.e. the feature
contributes ~0 at cardinal.

**Measured cost + cap utilization** (C2, Metal/Apple M4 Max; GL side owes
cross-host smoke as of PR #2388):

| scene / pose | overflow entries | cap | utilization | drops |
|---|---|---|---|---|
| wave, zoom 8, cardinal (yaw 0) | 0 | 437844 | 0% | 0 |
| wave, zoom 8, yaw 0.35 (q0 residual) | ~122k–126k | 437844 | ~29% | 0 |
| wave, zoom 8, yaw-ramp near-cardinal | 0 → 69k | 437844 | ≤16% | 0 |
| cylinder (shape_debug), yaw 0→60° | 0 → 638 | 437844 | <1% | 0 |
| dense_set (convex, no coset collisions) | 0 | — | 0% | 0 |

The relight is one bounded compute dispatch sized to the cap (threads past
the live entry count early-return — the "empty early-return sweeps are
effectively free" cost model,
[`gpu-stage-timing-cost-model.md`](gpu-stage-timing-cost-model.md)); the
`LIGHTING` GPU-stage row stays sub-ms on the wave scene at zoom 8 — not a
measured hotspot. Exact q1–q3 (120°–240°) peak counts were not captured
headlessly for C2 (the four-quadrant pose table fell back to the default
shot table in that measurement run); the numbers above are q0-residual +
near-cardinal only, and the NO-GO decision below rests on the design size
bound rather than a measured far-quadrant peak.

**Yawed-election follow-up: NO-GO (recorded, no follow-up issue filed).**
The epic's optional follow-up — switching the cardinal store's election
metric from the un-yawed `x+y+z` to the yawed depth, to shrink overflow near
the inverted-metric 180° quadrant — is unneeded for correctness now the lane
exists. The view mask's filter already bounds overflow at ≤ view-visible
faces ≈ O(screen cells), independent of quadrant; zero drops were observed
at every measured pose including the inverted-metric far quadrants. The
metric change remains available as a pure perf refinement if a future
measurement finds the cap under real pressure.

**Accepted drift.** Overflow-lit slivers carry no screen-space AO — they own
no per-axis canvas cell, and AO is a canvas-cell-resident quantity. No other
drift from the cell-path lighting model.

### #2207 / #2157 synergy

The overflow entry's packed distance carries the same encoding the cell path
already decodes (`decodeSlot` / `decodeFlipPerAxis`), so a captured entry's
#2207 riser-polarity flip survives through the shared scatter decode with no
extra plumbing. The epic plan flagged this lane as a potential landing zone
for #2207's deferred per-axis dual-emit story — representing a coset pair
where *both* a triplet face and its opposite-polarity riser face are
simultaneously view-visible, which today still costs one slot each and can
still collide. That extension is **not** implemented by D1/C1/C2; noted here
only as a cross-reference so #2207/#2157 (tracked separately) and #2331 stay
reconciled for whoever picks it up.

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
   G-buffer `{ iso-depth, color, entityId }` per cell. **Store: un-yawed
   (cardinal) iso pixel** `perAxisBase + pos3DtoPos2DIso(facePos)` per axis
   canvas. This is the only lossless 2D key: it depends on all three coords, so
   two faces that are screen-separated at the live yaw never share a cell, while
   genuine same-pixel cardinal occlusion (the only legitimate collision)
   resolves by the `rawDepth` `atomicMin`. The two rejected alternatives each
   lose faces (see the store section below): the **yawed** iso index collapses
   the compressed axis, and the **in-plane** `(y,z)/(x,z)/(x,y)` index collapses
   separate objects stacked along the fixed axis.
   > **Superseded (epic #2331, 2026-07-14):** "never share a cell" is true of
   > *cardinal* screen separation, not *live-yaw* separation — two faces
   > separated by `t·(1,1,1)` DO share this cell and DO both go on to be
   > view-visible once residual yaw separates them on screen. See
   > §"Current contract" above for the corrected two-set model and the
   > overflow lane that recovers the dropped member.
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

1. **Stage-1 / Stage-2 per-axis store one cell per face center, indexed by the
   un-yawed (cardinal) iso pixel** (not the `emitDeformedFace` super-sampled
   cluster T2 wrote). The cell is `perAxisBase + pos3DtoPos2DIso(facePos)`.
   `writeDistanceTap(cell, encodeDepthWithFaceFrac(pos3DtoDistance(facePos),
   slot, …))` `atomicMin`s the shared world depth into that cell; Stage-2 writes
   its color + entityId on the depth match. **The `atomicMin` winner per cell IS
   the occlusion resolution** — and because the key is the cardinal iso pixel,
   two faces collide *only* when they share a cardinal screen pixel, i.e. genuine
   cardinal occlusion (the nearer wins, correctly). Both rejected indices lose
   faces that are actually visible:
   - the **yawed** iso index `perAxisBase + roundHalfUp(pos3DtoPos2DIsoYawed(
     facePos, visualYaw))` collapsed distinct faces onto one cell on the
     *compressed* axis (the live yaw foreshortens it), dropping faces as
     **vertical cracks** worsening toward ±45° — and its inverse was singular at
     full `visualYaw` ±120°/±240°;
   - the **in-plane** `(y,z)/(x,z)/(x,y)` index that replaced it is collision-
     free for a *single connected surface* but collapses **separate objects
     stacked along the fixed axis** (same in-plane column, different depth) onto
     one cell — `atomicMin` keeps only the front, so a back object that is
     screen-separated under yaw loses its face to the floor/background (the
     maingrid stacking defect). Neither holds at every yaw for arbitrary content;
     the un-yawed iso key does, because it is un-compressed (cardinal) *and*
     all-three-coords (screen-unique).
   > **Superseded (epic #2331, 2026-07-14):** "collide *only* when they share
   > a cardinal screen pixel, i.e. genuine cardinal occlusion" describes the
   > *cardinal* census correctly but overstates it as the occlusion story for
   > the *live view* — a coset pair sharing this cell can both be
   > view-visible at residual yaw; the store's single winner drops the other
   > one, and it is not "occluded" in any sense the view agrees with. See
   > §"Current contract" above.
2. **The scatter is an instanced draw over the canvas grid** (one instance per
   cell, `drawElementsInstanced` of the shared 6-index quad). The vertex shader:
   reads the cell's stored distance; degenerates (off-screen) if it is the clear
   value; otherwise recovers the **world origin** from the cell by the exact iso
   inverse — `isoPixelToPos3D(cell − perAxisBase, rawDepth)` (with
   `rawDepth = decodeDepthPerAxis(dist) = x+y+z`; the #2207 riser-polarity flip
   rides bit 10, below the depth field). `slot = dist & 3` recovers the
   visible-triplet slot → `faceId = visibleFaceIds[slot] ^ flip`. This recovery
   is **exact at every
   yaw** because the stored index is un-yawed: the singular full-`visualYaw`
   inverse `z = (rawDepth + P(c+s) − Q(c−s)) / (2cosθ+1)` is gone — the live yaw
   is applied only forward, at scatter, by `pos3DtoPos2DIsoYawed`. `perAxisBase`
   (`trixelFrameOffset`) centers the cardinal iso projection on the camera and is
   computed identically by the store, the scatter, and the lighting/AO recovery
   (`perAxisCellToWorld3D`) so all agree on `cell ↔ world` by construction.
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
   stored depth is that plane's iso distance. The recovery (`isoPixelToPos3D`)
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

#### Conservative coverage — the scatter quad must be dilated to ≥ a pixel (#1494)

The forward-scatter draws **one quad per non-empty cell**. Each cell stores a
face at its two in-plane integer coords (pitch-1, face-local), and the quad
spans exactly one in-plane unit, so its `iso∘R` projection lands its `+e_u`
corner on the neighbour cell's origin: in **continuous** space the per-cell
rhombi tile the face gap-free, at any residual, because a *linear* map of a
gap-free unit-cell tiling is gap-free. **That guarantee does not survive
finite-resolution rasterization.** At off-snap residual poses a face can be
foreshortened so its per-cell rhombus collapses to a **sub-pixel-thin sliver**;
under pixel-center coverage the slivers slip between fragment centers and drop
out, leaving regularly-spaced gaps — the "thin vertical sliver"/waffle of #1494
(and the same latent bug on the camera-yaw world scatter, where it is usually
hidden sub-pixel on the much larger world canvas). It is **size-dependent**
(larger on-screen faces tile solid; small / foreshortened ones gap) — the tell
that it is a coverage artifact, **not** a placement-scale error (the
canvas-normalized and direct-iso placements are algebraically equal — the
`axes.size_` factor cancels) and **not** a store-pitch / parity gap (the store
is dense pitch-1, recovery is exact). A blunt `cornerSel × 2` model-space span
*does* fill it but over-draws (wrong silhouette, scales with size, breaks at
`subdivisions > 1`) and is **rejected**.

**Contract:** the scatter vertex shader grows each quad by a fixed
**screen-space** margin (`kScatterDilateMarginPx`, ~0.85 framebuffer px) outward
along **both** of its two screen edge normals (`scatterConservativeDilation` in
`ir_iso_common.{glsl,metal}`), so the thin dimension always spans a fragment
center. Because the margin is a fixed *pixel* amount it is negligible at large
on-screen size (silhouette unchanged) and is independent of subdivision density
(screen-space, not model-space) — the two properties the `×2` span lacks. The
shader needs the framebuffer extent the ortho `mpMatrix` maps into to convert
the pixel margin to NDC; it is uploaded in `FrameDataTrixelToFramebuffer::
scatterFbResolution_`. Both scatter paths — camera-yaw (`v_peraxis_scatter`) and
detached SO(3) (`v_peraxis_scatter_detached`) — share the one helper and the one
contract so they cannot diverge. Identity / at-snap is untouched: the per-axis
canvases are released at a snap, so the scatter (and its dilation) never run on
the byte-identical fast path.

> **Known residual (follow-up):** the dilation closes the intra-face sliver
> waffle but a sparse line of isolated single-pixel holes can remain at **face
> boundaries** on some poses — a distinct, pre-existing structural gap (a few
> cells at the shared edge of two faces do not abut in screen space), not the
> sub-pixel coverage this contract addresses. Tracked separately.

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
   > for the composite, read it as the yawed metric above (at exactly 0°
   > residual yaw the formula is byte-identical to the un-yawed metric; at
   > other cardinals the per-axis scatter is inactive so the formula is
   > never evaluated).
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

## Detached per-entity SO(3) reuses this machinery (epic #1444)

> **RETIRED (#1560).** The detached per-axis forward-scatter described in this
> section (P1–P4, #1463–#1475) **shipped and was then superseded**. Detached
> SO(3) now renders through the **re-voxelize** path (epic #1553, #1555–#1560):
> the entity's rotation is baked into its private pool's voxel CELLS, which then
> rasterize through the normal single-canvas cardinal path + blit — no per-axis
> canvases, no forward-scatter, for detached entities. #1560 deleted the detached
> consumer (`v_peraxis_scatter_detached.{glsl,metal}`,
> `PerAxisScatterDetachedProgram`, `syncAllocationToDetachedEntities`,
> `kMaxDetachedRotatingCanvases`); the **camera / main-canvas** per-axis path
> (T2–T4, below) is untouched. The rest of this section is kept as the design
> record of the retired approach. See the "true-3D path" note under "Detached
> forward-scatter is terminal" below for the shipped re-voxelize model.

The per-entity smooth-rotation mechanism the section above defers to —
**detached canvases** — is itself generalized onto this same three-per-axis
split by epic #1444 (`~/.fleet/plans/issue-1444.md`). Where the camera path has
**one global** Z-yaw split into a cardinal + a residual in `[−π/4, π/4]`, a
rotating detached entity has its **own SO(3)** rotation split into an
**octahedral snap** (one of 24, the cardinal-snap analogue — a cube is
invariant under it) + a **residual** bounded by the octahedral covering radius
(`IRMath::octahedralSnapResidual`). Each detached entity then drives its own
three per-axis canvases off *its* residual, exactly as the main canvas does off
the camera residual. The composite (#1464, P3) is the SO(3) generalization of
the camera path's forward-scatter.

**Reposition helper (#1463, P2).** The camera path repositions voxel centers
with `pos3DtoPos2DIsoYawed(worldPos, visualYaw) = iso(R_z(−yaw)·world)` (1-DOF).
The detached path repositions with the SO(3) companion
`pos3DtoPos2DIsoRotated(modelPos, residual) = iso(R_residual·modelPos)` — same
shape, an arbitrary-axis quaternion in place of the Z-only yaw. Both have GLSL +
Metal mirrors (`rotateByQuat` then the shared iso columns) kept CPU↔GPU
bit-identical; at identity rotation the helper is exactly `pos3DtoPos2DIso`, and
a pure-Z quaternion `qZ(θ)` reduces to `pos3DtoPos2DIsoYawed(·, −θ)` (the
entity-rotation sign is opposite the camera-residual sign, matching
`faceDeformationMatrixSO3`).

**Allocation policy + memory bound (#1463, P2).** `C_PerAxisTrixelCanvases` is
now bundled on **every** voxel-pool canvas by `Prefab<kVoxelPoolCanvas>` (main
canvas + every detached entity), default-constructed inert — a static / cardinal
canvas pays only the component slot, no GPU textures. Two once-per-frame gates in
`VOXEL_TO_TRIXEL_STAGE_1::beginTick` drive the lazy allocation:

- `IRPrefab::PerAxisCanvas::syncAllocationToCameraYaw()` — the **main** canvas,
  gated on the camera residual yaw (unchanged from T1).
- `IRPrefab::PerAxisCanvas::syncAllocationToDetachedEntities()` — every
  **detached** entity (`C_CanvasLocalRotation::isDetached()`), gated on its
  octahedral-snap residual magnitude (`> kDetachedResidualDeadband`). At an
  octahedral snap the single-canvas `faceDeformationMatrixSO3` deform is already
  exact, so the textures stay released and the detached raster is byte-identical.

  **Memory is the bound.** Each allocation is 3 axis canvases × 5 textures
  (color / distance / entity-id / AO / sun-shadow) sized to that entity's
  worst-case trixel canvas, so peak cost scales with the number of
  *simultaneously rotating* detached entities. That is capped at
  `kMaxDetachedRotatingCanvases` (currently 8). **Eviction:** entities
  encountered first in archetype-iteration order win the budget; an entity past
  the cap (or one that returns to a snap) has its textures **released** and keeps
  rendering through the single-canvas octahedral-snap + `faceDeformationMatrixSO3`
  path — graceful degradation (blockier off-snap deform), never a crash or a
  leak. Allocation only transitions on rotation start / stop, so a full spin
  allocates once and frees once.

P2 (#1463) is **infrastructure only** — it stands up and bounds the textures and
adds the reposition helper, but routes no faces into them, so the rendered frame
is byte-identical. P3a (#1464) is the first writer: the model-space face-local
store routes each rotating detached entity's visible faces into its own per-axis
canvases (keyed by `faceInPlaneCoords`, depth `pos3DtoDistance` — the un-rotated
`x+y+z` origin-recovery key), still additive (the single-canvas octahedral emit
keeps rendering). P3b (#1475) is the first reader: the forward-scatter composite.

**P3b — forward-scatter composite (#1475).** `ENTITY_CANVAS_TO_FRAMEBUFFER`
forward-scatters a rotating detached entity's three per-axis canvases straight
to the framebuffer (`PerAxisScatterDetachedProgram` =
`v_peraxis_scatter_detached` + the reused `f_peraxis_scatter`, GL_LESS), the
per-DETACHED-entity analog of the camera path's
`system_trixel_to_framebuffer::drawPerAxisScatter`. Each non-empty cell recovers
its exact model origin via `faceOriginFromInPlane` (the camera-iso baked into
`perAxisBase` cancels), repositions the face corners under the octahedral-snap
**residual** with `pos3DtoPos2DIsoRotated`, and places them at the entity's iso
screen position via the same placement TRS the gather blit uses — so
`perAxisBase` is recovery-only. Composite depth sorts by
`isoDepthAlongAxis(origin, isoDepthAxisModel(residual))` (the reposition's
rotation, **not** the store's `x+y+z` key and **not** the full composed
rotation). `VOXEL_TO_TRIXEL_STAGE_1` retires the off-snap octahedral
single-canvas emit for these entities (no double-draw); the at-snap path
(per-axis released) keeps the single-canvas emit + blit, byte-identical to
master. As with the camera path, detached rotating entities freeze at identity
under `--auto-screenshot`, so the smooth-rotation visual is verified at P5 /
#1466; identity / at-snap is the headless-verifiable byte-identical case.

> **Anti-pattern (rejected — issue-1475 plan, architect 2026-06-02):** do
> **not** "resolve" the per-axis canvases back into the entity's single trixel
> canvas and reuse the unchanged `ENTITY_CANVAS_TO_FRAMEBUFFER` blit. That blit
> is `CanvasToFramebufferProgram` = `f_trixel_to_framebuffer.glsl`, whose
> `main()` de-tiles via `trixelFramebufferSamplePosition` — the
> **single-global-parity gather**. A smooth resolve writes continuous
> `pos3DtoPos2DIsoRotated` centers (mixed parity) into a trixel canvas; reading
> them back through that gather re-introduces the #1256 stripe. The blit cannot
> be both *unchanged* and *smooth*. Forward-scatter to the framebuffer (above)
> is parity-correct by construction — no gather inverse, no single-parity
> assumption — exactly as the camera path's T3 decision (`## Implementation
> decision` above) established.

## Detached forward-scatter is terminal for asymmetric solids (#1551, architect 2026-06-06)

The detached forward-scatter (P3b) is **a 2D image-warp of stored faces, not a
3D re-rasterization of the rotated solid** — and that is a *structural* limit,
not a tuning problem. This section is the source of truth for what the path can
and cannot do, so a future worker does not re-derive it or sink another
band-aid into it. Decided resolving #1551 (epic #1444's re-opened headline);
the firsthand root-cause is PR #1552, the mechanism citations below are
verified against current `master`.

**The mechanism (verified):** the store
(`c_voxel_to_trixel_stage_1.glsl:209-221`) writes the entity's three visible
faces in **undeformed face-local model-space**, keyed by the un-rotated
`x+y+z`. The scatter (`v_peraxis_scatter_detached.glsl`) recovers each face's
exact model origin and forward-maps its corners by
`pos3DtoPos2DIsoRotated(corner, residual) = iso(R_residual·corner)`. Because
that map is **linear**, the whole off-snap composite is a per-face **affine 2D
skew** of the octahedral-snap orientation's stored faces — it never moves a
voxel center in 3D. The store's own rationale states the load-bearing premise
(`system_voxel_to_trixel.hpp:104-108`): *"A cube is invariant under the snap,
so this keeps the per-face skew small enough to stay clean."*

**That premise holds only for the cube — and is false for any asymmetric
solid:**

- **Cube (silhouette-invariant under the octahedral snap):** the skew is a
  genuine view of the rotated cube, so the path is *correct*. Its residual-pose
  defects are a **rasterization-coverage / depth-seam** class — sparse
  forward-map coverage under magnification (the `#1494` dilation, `#1538`
  speckle, `#1499` sliver, `#1544/#1545` jagged-edge fixes all patch exactly
  this) and face-seam mis-sort in the 3-canvas GL_LESS composite. These are
  *closable on this path* — the cube silhouette is the same shape the skew
  produces.
- **Asymmetric solid (not snap-invariant):** the skewed snap-orientation faces
  are the **wrong faces** — at mid-residual the actually-visible face *set* and
  the voxel-center layout differ from the snapped orientation's, and an affine
  skew of stored 2D faces cannot reproduce either. The result reads as a
  2D-skewed cardinal arrangement, never a true 3D-rotated solid. **No
  forward-scatter improvement (better dilation, guaranteed-coverage
  supersampling, full-rotation reposition without the snap) can close this — it
  is wrong by construction**, because the path warps stored 2D faces and never
  re-rasterizes the rotated solid.

### #1551 scope decision

`#1551`'s discriminating DoD test — *"an asymmetric detached solid at ~45°
reads as a true 3D-rotated solid (voxel centers reorganized)"* — is therefore
**unreachable on this path** and is **dropped from #1551**. #1551 is re-scoped
to the **cube-only, consumer-backed** goal: a clean, cohesive 3-face cube at
**every** residual pose + no temporal pop through snap intervals (absorbs
`#1539`), via a guaranteed-coverage forward-scatter (deterministic per-cell
supersampling sized to the destination stretch, retiring the `#1494` dilation
heuristic) + deadband path-switch alignment. The decision rests on a verified
**consumer-reality** check: the only detached *rotating* content in the engine
+ creations today is **cubes** (`canvas_stress`, 10³); any asymmetric detached
entity present in current content is **static** (no auto-spin component). The
true-3D-asymmetric criterion has **no current consumer** — building for it now
would be a foundation ahead of demand.

### The true-3D path (SHIPPED — detached GPU re-voxelize, epic #1553)

> **Status: shipped (#1555–#1560).** What this section described as the deferred
> "true-3D path" is now the **sole** detached SO(3) renderer. Epic #1553 built
> the GPU re-voxelize dispatch (P1/P2), round-to-cell occlusion + aliasing (P3),
> AO/sun/light integration (P4), cross-backend parity audit (P5), and **#1560
> retired the detached forward-scatter** (P6) — deleting
> `v_peraxis_scatter_detached.{glsl,metal}`, `PerAxisScatterDetachedProgram`,
> `syncAllocationToDetachedEntities`, and `kMaxDetachedRotatingCanvases`. The
> per-axis machinery the camera/main-canvas path uses is untouched. The
> remaining text is the original design rationale, now realized.

True-3D detached rotation requires **re-rasterizing the rotated solid in 3D**,
the detached analogue of the attached **GRID re-voxelize** model
([`voxel-face-rasterization.md`](voxel-face-rasterization.md) §"Per-entity
SO(3) on the main canvas — RETIRED (#1443)"): fill a **private per-entity voxel
grid** under the **full** rotation on the GPU, run the ordinary
voxel→trixel→canvas pipeline on it so the **camera** (not a per-entity skew)
drives face selection + deform, then composite that canvas at the entity's
**screen-locked** placement. This is the "GPU re-voxelize (fill cells under the
full rotation)" that `voxel-face-rasterization.md` names as the path for
per-voxel-identity-preserving rotation — generalized from attached to detached.
It was **multi-PR** (re-voxelize dispatch into the private grid → screen-locked
placement of the re-voxelized canvas → per-voxel depth/occlusion → AO/sun/light
integration → Metal parity → render-verify baselines) and **retired the
detached forward-scatter** on completion (#1560). It shipped as epic **#1553**
(`#1396`'s GPU prepass transforms positions of *existing* voxels; the re-voxelize
scatter `c_revoxelize_detached.{glsl,metal}` fills the private pool's cells under
the full rotation, which is the part #1396 did not do).

> **Do not band-aid past cube-clean.** Once #1551's cube-coverage/seam fixes
> land, the forward-scatter path is **done** — further "make the asymmetric
> case work" effort on it is wasted by construction. The next step for
> asymmetric is the re-voxelize epic above, not another scatter heuristic.

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
  (rotation-only lifecycle); the world volume + sun map are NOT per-axis.
  **Per-axis voxels do NOT cast into the shared sun map while rotating (#1370,
  option C — revises the original "+ all three per-axis voxel canvases" bake.)**
  Baking the face-local per-axis canvases into the shared map made a block's own
  mutually-perpendicular faces self/cross-occlude — the face-local
  representation lacks the cardinal path's per-screen-pixel iso-depth-plane
  flattening, so a block's top face shadows its own side faces in the sun
  projection — producing false black side faces at non-cardinal yaw
  (135°/225°/315° worst). As-shipped, `BAKE_SUN_SHADOW_MAP` bakes **only the
  main canvas (SDF/text)** during rotation (`dispatchPerAxisBake` dropped); the
  per-axis voxels still RECEIVE sun shadows (from the main canvas / SDF) and are
  still AO-shaded, they just stop CASTING. Cardinal (`residualYaw == 0`) is
  byte-identical. Restoring faithful per-axis cast-shadows via a trixel /
  screen-space resolve (the north star: one resolved representation both
  detached and per-axis content derive from) is deferred to **#1435**.
  - **DETACHED status (P4 / #1465).** The resolution above is shipped for the
    **camera / main-canvas** path (T4 / #1311). For **detached** entities it is
    designed but **not yet wired**: `ENTITY_CANVAS_TO_FRAMEBUFFER`'s scatter pass
    binds only `colors_`/`distances_` per axis, so a rotating detached entity
    composites raw voxel color (unlit). Receiving world AO / sun-shadow on the
    detached composite (reusing the per-axis `ao_`/`sunShadow_` textures already
    on `C_PerAxisTrixelCanvases`) is tracked by **#1375** (receive) / **#1376**
    (cast). P4 (#1465) confirmed Metal backend parity for the shipped detached
    geometry/depth/scatter (P1 stage-1 `R⁻¹·(1,1,1)` depth + P3b forward-scatter)
    — built clean and hardware-verified on macOS/Metal — and handed the detached
    lighting integration to #1375/#1376 rather than duplicating it here.
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
> `perAxisCellToWorld3D`; `BAKE_SUN_SHADOW_MAP` bakes the main canvas into the
> shared sun map (per-axis voxel casting dropped in #1370 option C — see the
> §"Open decisions" bake note above; deferred to #1435). No framebuffer MRT; no
> per-fragment lighting.

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

## Per-axis store occlusion model — engine invariant (established #1457, 2026-06-10; store-vs-view scope corrected by epic #2331, 2026-07-14)

### Store is collision-free

> **Scope correction (epic #2331, 2026-07-14):** everything in this
> subsection is still true **as a store-cell statement** — one cell, one
> winner, uncontested, and store-time sort-key choice is a no-op at
> voxel-pool granularity. What it does not say, and what #2331 found, is
> that "one winner per store cell" is not the same claim as "no face is
> lost" — the store's cell key is cardinal, the view is yawed, and a coset
> pair sharing one cell can both be view-visible. See §"Current contract"
> near the top of this file for the two-set model and the overflow lane
> that now recovers the coset loser this subsection's `atomicMin` drops.

The per-axis stage-1 `atomicMin` has exactly **one camera-visible exposed
face-voxel per cell**, keyed by its two in-plane world coordinates. The winner
is uncontested — any store-time sort key (un-yawed `x+y+z`, yawed axis depth,
or exact recovered-origin depth) is a no-op at voxel-pool granularity. This
was verified empirically via three independent fix rounds (#1601 per-fragment
composite depth, #1625 repacked yawed sort key, and #1625 v3 exact planar
depth) each measured byte-identical to master at worst-case yaw 67.5°.

**Invariant (narrowed scope, #2331):** do not add per-axis store logic aimed
at resolving occlusion **between two faces that both map to the same store
cell** — no such competition exists; the `atomicMin` winner is correct and
final for that cell. This does NOT mean the store cell set is the complete
view-visible set — the overflow lane (`resolveMode` 2/3, §"Current contract")
is exactly the sanctioned per-axis logic that recovers what the store cell
model structurally cannot represent, and is not a violation of this
invariant: it never contests a cell's stored winner, it appends the
view-visible faces the store never had a cell for.

### Occlusion is decided at the cross-axis composite

The three per-axis canvases (X/Y/Z visible surfaces) forward-scatter their face
quads and depth-test against each other per screen pixel via `vDepth`. If a
depth defect is observed under camera yaw, the locus is the cross-axis
composite, not the per-axis store. Diagnose with `--debug-overlay axis_id`
(pixel colored by winning axis canvas: X=red, Y=green, Z=blue) to confirm or
rule out cross-axis interleave before attempting any fix.

### Canonical occlusion diagnostic for rotated voxel content: `--checkerboard`

`--checkerboard` (per-voxel alternating authored colors, geometry-only) is the
**only reliable** occlusion/geometry diagnostic for the per-axis voxel path at
non-cardinal yaw. A clean checkerboard at worst-case pose (e.g. yaw 67.5°)
with zero scramble constitutes ground truth: no placement, store, recovery,
composite, or lighting defect is present.

**`--depth-color` is structurally misleading at non-cardinal yaw** and must
NOT be used as occlusion evidence on the voxel path. The diagnostic quantizes
hue into 4/3-world-unit bands while voxels step 1 unit per lattice step; at
any non-cardinal yaw the band boundaries (straight skewed lines in world space)
beat against the voxel lattice at near-maximal spatial frequency, and the
per-voxel-constant palette renders that interference as blocky staircase
alternation that reads as front/back confusion — even when geometry is
provably correct. The SDF twin evaluates the same palette **per pixel**
(continuous) and cannot produce this artifact, so SDF-vs-voxel depth-color
side-by-sides are **not** evidence of a voxel-path defect. This was the root
cause of the three-investigation chain #1414 → #1451 → #1457 (see PR #1625).

The `--debug-overlay` flag added in PR #1625 provides the reliable
instrumentation modes for this surface:
- `axis_id` — each pixel colored by the winning axis canvas (X/Y/Z). Coherent
  single-axis regions rule out cross-axis composite interleave.
- `origin` — each winning cell colored by its recovered-origin depth field.
  Smooth output rules out per-axis store or recovery corruption.
- `unlit` — raw stage-2 colors with lighting disabled. Rules out
  AO/light/shadow as the source of any visual artifact.
