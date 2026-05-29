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
   wins per framebuffer pixel (shared world-space `pos3DtoDistance` metric →
   valid). With forward scatter this is the per-pixel framebuffer depth test
   over the 3 scattered canvases — not a per-voxel competition.
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

- **Lighting / AO placement.** AO samples neighbor faces. With three layers it
  either runs **post-composite** on the resolved framebuffer (winning face-id +
  normal carried through the composite — preferred) or per-canvas (duplicated
  work). Pick post-composite unless a blocker emerges.
- **Memory / allocation policy.** Three worst-case textures during rotation;
  gate allocation on `residualYaw != 0` so static scenes pay nothing. Define
  the allocate/free churn behavior at rotation start/stop.
- **`min-trixel-size` value.** ≈ 1 framebuffer pixel is the starting point;
  tune against seam-free coverage vs texture size.
- **Per-canvas basis derivation.** Exact mapping from `residualYaw` (and
  cardinal) to each canvas's basis + extent; reuse `faceDeformationMatrix`.

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
