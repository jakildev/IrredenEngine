# Camera Z-yaw rotation pivot

Source of truth for how camera Z-yaw chooses the point it rotates about, and
the offset math that pins it. Consumed by `IRRender::getEffectiveCameraIso`
(`engine/render/src/ir_render.cpp`) and the camera input systems.

## The contract

The composite places a world point on screen at
`screen_iso(W) = pos3DtoPos2DIsoYawed(W, yaw) + getEffectiveCameraIso()`.
To keep a chosen focus `F` at a fixed screen position as the camera Z-yaws
(rotation in place), the camera offset must **cancel** `F`'s yaw-induced canvas
drift. That single formula lives in one place:

```
IRMath::cameraYawPivotOffset(cameraIso, F, yaw)
    = cameraIso - pos3DtoPos2DIsoYawed(F, yaw) + pos3DtoPos2DIso(F)
```

It yields `screen(F) = cameraIso + pos3DtoPos2DIso(F)`, which is independent of
`yaw` — `F` is pinned. At `yaw == 0` it returns `cameraIso` (the no-rotate fast
path, byte-identical to `ORIGIN` mode). **Never inline this formula** — call the
helper.

## Pivot modes

`RotationPivotMode::CAMERA_CENTER` (the engine default) picks `F`:

1. **Screen center (default).** `F` is the world point under the EXACT viewport
   center, at the depth the content there actually renders at (#2547):
   `isoPixelToPos3D(viewCenterIso, d)`, where `viewCenterIso = canvasSize/2 -
   trixelOriginOffsetZ1(canvasSize) - cameraIso` (derive: a world point lands at
   screen center when `pos3DtoPos2DIso(W) + cameraIso == canvasCenterIso`) and
   `d` is the **iso depth** (`x+y+z`, NOT `z` — `isoPixelToPos3D`'s third
   argument is an iso depth) read back from the composite depth attachment at
   the viewport-center pixel. A background center pixel falls back to `d = 0`,
   the pre-#2547 behavior.

   **Latch policy.** `RenderManager::updateDefaultRotationPivotFocus` runs once
   per frame from `beginFrame`, ahead of the RENDER pipeline, so every stage in
   a frame reads ONE focus. It re-derives only while `visualYaw` is unchanged
   between frames (a `kResidualYawDeadband`-snapped residual counts as settled)
   AND the previous frame rendered the current pan/zoom — the depth attachment
   it reads belongs to the previous frame, so a derive is only sound one frame
   after the camera settles. While yaw moves the latch is HELD: that is what
   pins the pre-rotation center content through the whole rotation, identically
   for a mouse drag, a key, or a programmatic `setYaw` (auto-screenshot needs no
   gesture plumbing). Steady state and continuous panning both do ZERO readbacks
   — a readback costs a full GPU flush, so it fires only on the settled frame
   after a pan/zoom change.

   The offset is the drift-cancel `cameraYawPivotOffset` form
   above — NOT a bare `pos3DtoPos2DIsoYawed(F, yaw)`, which leaves a yaw-varying
   residual that swings a panned scene in an arc (the latent #1352 bug;
   un-panned both forms collapse to 0, so canvas_stress — which never pans —
   never caught it, but shape_debug's panned pivot shots did).

   Do **not** use the mirrored legacy #1352 point `isoPixelToPos3D(cameraIso, 0)`:
   it is the viewport center reflected across the canvas origin, so it pivots about
   the wrong point and the panned scene swings the opposite way.

   The viewport-center focus is ~1 trixel off the canvas origin even un-panned, so
   `getEffectiveCameraIso() != getCameraPosition2DIso()` whenever yaw != 0. For the
   DETACHED entity-canvas composite to pivot WITH the GRID/world content (rather
   than drift, the canvas_stress canary jitter behind #1942 → #1944), the composite
   reads `getEffectiveCameraIso()` for its screen placement too
   (`system_entity_canvas_to_framebuffer.hpp`); its de-tile gather parity stays on
   the entity's fixed world iso. Detached + GRID now share one pivot.
2. **Cursor (`Ctrl+Shift+middle-drag`).** `System<CAMERA_MOUSE_ROTATE>` captures
   the world point under the cursor at drag start
   (`IRRender::mouseWorldPos3DAtIsoDepth(0)`) and sets it as an explicit focus via
   `IRRender::setRotationPivotFocus`. The drag reverts to the screen-center
   default on release.

`RotationPivotMode::ORIGIN` skips the correction (offset == `cameraIso`); Z-yaw
pivots about the world origin.

An explicit focus set by any caller (`setRotationPivotFocus`, #1921/#1927)
overrides the default — used by the cursor mode and by `shape_debug
--pivot-focus-demo`.

## Empirically verified

The **explicit-focus** path: `shape_debug --pivot-focus-demo` pins an explicit
focus on a pillar at world (8,−8,10) (off-origin, z>0 — the hard case) and sweeps
yaw. The pillar's centroid holds the exact screen center (measured 1279.5,720.5 vs
center 1280,720) while the ring of markers orbits it; a broken pivot drifts
hundreds of px (#1926 measured 1024px). See
`docs/pr-screenshots/claude/1926-camera-pivot-screen-center/`.

The **screen-center default** path (the viewport-center fix above): `shape_debug`
panned to cameraIso (16,16) and yawed 0/90/180/270/45 holds screen-center content
fixed — at yaw 180 a landmark at screen-offset (−Δx,−Δy) from center maps to
(+Δx,+Δy), an exact point-reflection through screen center. Before the fix the
panned scene swung off-frame (yaw 0 vs yaw 180 differ 15.9%). yaw 0 stays
byte-identical (fast path). canvas_stress (un-panned, with the detached
composite now on the effective offset) is byte-identical at yaw 0 and differs only
0.21% at yaw 45 — the small whole-composition pivot shift, with detached + GRID
moving together (vs the 1.3–3.4% detached *drift* that #1942 alone caused).

Corollary: a scene that "swings" under rotation now means the **content is laid
out off-center** (not a pivot bug) — pin a focus (cursor mode, or a
content-centroid focus) to rotate it in place. (Before this fix the default pivot
itself swung a panned scene; that path is now correct.)

## Known deviations (2026-07, epic #2544)

The `scripts/pivot-verify.py` harness (isolated cylinder probe +
`jitter_probe --stationary`; no reference images) enumerates three defects
in the contract above — all invisible at cardinal yaw 0, so the "Empirically
verified" section below remains true for what it measured while the pivot is
still wrong under rotation. #2545 (deviation 1) and #2546 (deviation 3) are
now fixed; #2547 (deviation 2) remains open on master. Fixed entries are
retained below until the epic closes:

1. **Half-cell rotation-anchor mismatch — FIXED (#2545).** The voxel raster
   rotated content about `position + (0.5,0.5,0.5)` while the SDF path and
   the CPU pivot/picking math rotate about the exact `position`; under yaw
   the voxel layer orbited any pinned focus ~1 iso px and the voxel/SDF
   layers counter-rotated apart. The settled convention — **an entity's
   `position` is the rotation anchor for every render path** — is now
   enforced in the raster: the smooth per-axis route projects cell positions
   through `pos3DtoPos2DIsoYawedCellAnchor` / `yawedIsoDistanceCellAnchor`
   (`ir_iso_common.{glsl,metal}` — the lower-corner cell lattice shifted half
   a cell so the rendered mass rotates about the authored lattice; exact
   no-op at yaw 0 since `iso(0.5,0.5,0.5) == (0,0)`), and the settled
   cardinal store keeps the **plain** `rotateCardinalZ` position — the
   former `cardinalLowerCornerShift` add (and its undo in
   `trixelCanvasPixelToWorld3D`) was this bug's cardinal form and is retired
   from the store/cull/resolve chain. Exact world positions (SDF centers,
   entity translations, the pivot math itself) keep the un-anchored
   projections.
2. **Default focus depth — mechanism landed, CONTRACT OPEN (#2547).** The
   pinned set of the drift-cancel offset is the vertical column
   `{W : W.xy == F.xy}`. `F` was the **iso-depth-0** point under the viewport
   center, so content at screen center at another depth sat on the center iso
   ray, off the pinned column by (t, t) in xy, and orbited — 336 px at z=10,
   zoom 4 on master, 320 px once #2545's half-cell landed.

   The depth-aware derivation above now pins the point the depth buffer
   reports under the center pixel, verified exact (derived focus
   `(19.167, -4.833, 7.167)` at iso depth 21.5 == the analytic ray/cylinder
   surface intersection, to 3 decimals). **But a depth buffer yields the
   SURFACE under the center pixel, while `pivot-verify` scores the
   whole-silhouette CENTROID of an extended probe.** Pinning a column on a
   rigid body's surface orbits its centroid by 2r, which the harness's metric
   converts to `16·(δx+δy)` px — ~22.6·r px at zoom 4. Measured: `center-depth`
   320 → 92 px, `center-column` 0.9 → 76 px (both within 2% of that model) for
   the radius-4 probe. No probe radius above ~0.07 world units can pass a
   1.5 px centroid gate, so this is structural, not a tuning gap: the gate
   encodes "pin the content's axis", the mechanism delivers "pin the surface
   point". Epic Phase 4 (#2548) latches the clicked SURFACE point
   (`castVoxelRay`'s `worldHitPos_`), which points at the surface contract
   being the intended one — pending an architect ruling on whether to
   re-ground the two blocks' oracle or change the mechanism.
3. **Per-axis registration offset — FIXED (#2546).** With (1) compensated,
   every residual-yaw frame rendered the voxel scene a constant ≈1 iso px
   (per axis, zoom-scaled) off the cardinal frames. Root cause: the
   forward-scatter's SCREEN re-projection anchored on the per-axis STORE
   origin `perAxisBase` (= `trixelOriginOffsetZ1(canvasSize) + floor(cameraIso)`),
   whose `trixelOriginOffsetZ1` carries the trixel grid's `(-1,-1)` sub-pixel
   LATTICE alignment — a canvas-storage convention the `ij - perAxisBase`
   recovery needs, but not a screen offset. The scatter emits true face quads
   (no trixel-grid gather), so that lattice alignment rode into the on-screen
   placement while the cardinal gather's focus carried none. Fix: anchor the
   re-projection on the canvas geometric CENTER (`canvasSize/2`, exactly
   `perAxisBase + (1,1)` back from the storage origin) in
   `v_peraxis_scatter.glsl` / `metal/peraxis_scatter.metal`; recovery keeps
   the store anchor unchanged, so cardinal frames stay byte-identical (the
   scatter runs only at non-cardinal yaw). Depth (`yawedIsoDistanceCellAnchor`)
   and the store/RESOLVE/overflow paths are untouched — pure screen-XY
   registration.

Fix chain and acceptance gates: epic #2544 (P1 #2545 → P2 #2546 → P3 #2547 →
P4 #2548, cursor-pivot true-depth latch + indicator). Each child flips its
pivot-verify block(s) to PINNED; this section shrinks as they land.

## History

- #1352 / #1362 — first focus-pivot (panned-and-rotated correctness).
- #1921 / #1927 — explicit point-of-interest focus (`setRotationPivotFocus`).
- #1926 / #1942 — the `cameraYawPivotOffset` helper + the cursor pivot mode.
- #1944 — reverted #1942's "exact viewport-center default": the `viewCenter` is
  ~1 trixel off the canvas origin, so it shifted effective-offset world content
  relative to the **raw**-offset detached entities (canvas_stress rotation jitter).
  The revert also dropped the `cameraYawPivotOffset` wrapper, leaving the latent
  #1352 panned-swing bug.
- #1944 follow-up (this change) — restored #1942's exact viewport-center focus
  AND the drift-cancel wrapper, and completed the documented prerequisite: the
  detached entity-canvas composite now consumes `getEffectiveCameraIso()` for
  placement, so detached + GRID share the corrected pivot. Fixes the panned-scene
  rotation swing (shape_debug) without reintroducing the detached drift (#1944).
