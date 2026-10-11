# Continuous camera zoom

The main camera has two zoom policies. Under the default, **snapped** policy
every zoom write lands on a power of two, a backing texel always spans whole
framebuffer pixels, and rendered output is what it has always been. Under the
opt-in **continuous** policy the zoom is any value in
`[kTrixelCanvasZoomMin, kTrixelCanvasZoomMax]`, reads back exactly as written,
and the composite places content so that neither a pan nor a zoom re-rounds
texel edges from frame to frame.

## Surface

| Layer | Entry point |
|---|---|
| Policy | `IRPrefab::Camera::setZoomContinuous(bool)` / `isZoomContinuous()` — state is `C_Camera::continuousZoom_` |
| Value | `IRRender::setCameraZoom(float)` / `getCameraZoom()` — unchanged API; the setter snaps only under the snapped policy |
| Steps | `IRPrefab::Camera::zoomIn()` / `zoomOut()` — what the zoom commands and `CAMERA_SCROLL_ZOOM` call |
| Lua | `IRRender.setCameraZoom`, `getCameraZoom`, `setCameraZoomContinuous`, `isCameraZoomContinuous` |

Enabling keeps the stored zoom. Disabling snaps each stored axis to a power of
two on the spot, so the snapped invariant never waits for the next write.
Either direction drops the carried raster phase.

The policy belongs to the main camera only. `C_ZoomLevel::zoomIn` / `zoomOut`,
the background pattern, secondary viewports, and the `C_ZoomLevel` Lua
usertype keep rounding. Under the snapped policy the prefab steps call the
component methods unchanged, so a directly written anisotropic or
non-power-of-two zoom steps exactly as before.

## Two readers of one zoom

Display zoom `z` and raster density `S` are different quantities.

- **Display zoom** is read exactly by everything that converts between iso
  and screen space or sizes what is visible: the triangle step, picking, both
  pan systems, hitboxes, sprite anchors, cull viewports, backing coverage, the
  per-axis cap, LOD, the gizmo. `calcTriangleStepSizeScreen` is
  `z * (2, 1) * outputScale` with no integer conversion, so the forward and
  inverse conversions stay exact inverses at a fractional `z`.
- **Raster density** is the one whole-number reader:
  `voxelRenderEffectiveSubdivisions`. In `FULL` mode the main camera rounds
  the zoom factor to nearest under the snapped policy and **up** under the
  continuous one (`ZoomDensityRounding`), so one backing texel never spans
  more than its own framebuffer pixels and the nearest-texel gather only ever
  minifies. Explicit-zoom callers (secondary viewports) stay on nearest.

Passing `S` where coverage expects `z` clips the view; passing `z` where
storage expects `S` breaks the integer backing contract. The logical canvas
size never follows the zoom.

## Placement

Let `c` be the effective camera iso, `p = z * (2, 1)` the framebuffer pitch of
one iso unit, `t = p / S` the pitch of one backing texel, and `a` in `[0, 1)`
a per-axis **raster phase** carried from frame to frame. With `H = c * p - a`:

```
gather translation   g = (floor(H) + a - floor(c * S) * t) * kIsoToScreenSign
screen residual      r = floor(fract(H) * kIsoToScreenSign * outputScale)
detached camera term     floor(H) + a - floor(c) * p        (Y-up)
```

The raster has already moved the canvas `floor(c * S)` texels, so the gather
translation is what lands content on `floor(H) + a + w * p` for a world iso
coordinate `w`; the final upscale adds `fract(H)`. `IRMath::CameraRasterPhase`
and the `cameraRaster*` helpers in `ir_math.hpp` are the single implementation.

Why each term has the shape it has:

- **`floor(H)` is the whole camera offset, not the sub-cell part.** At a
  fractional pitch `floor(c) * p` has a fraction of its own. Splitting only
  `fract(c)` leaves that fraction owned by neither stage, and each texel edge
  rounds it independently at every cell crossing.
- **The gather subtracts every texel the raster consumed.** Using `floor(H)`
  alone counts the sub-cell texels twice.
- **The phase is carried, not computed.** Pan rigidity needs the rounding
  phase of the framebuffer grid to be the same at every camera position; zoom
  stability needs it to follow `c * p` as the pitch changes. No function of
  the current `(c, z)` does both. On a frame whose pitch changed,
  `a = fract(a + c_prev * (p - p_prev))`, which holds `H` fixed up to a whole
  pixel: an edge `d` cells from the view centre sits at `d * p - fract(H)` and
  scales about the centre. A pan leaves `a` alone and moves `floor(H)` in
  whole pixels.
- **Double precision.** `c * p` passes 10^5 for a far-panned camera.

With `a = 0` and a pitch whose backing texel spans whole framebuffer pixels
(a power-of-two zoom at one base subdivision) the three terms equal the
snapped `cameraSubPixelOffsets` split. After a zoom excursion with a pan in
the middle the phase is generally non-zero, so the placement at a power of two
can sit a uniform sub-pixel offset from the snapped one; shape and rigidity
match, absolute offset need not. Toggling the policy resets it.

## One sample per frame

`TRIXEL_TO_FRAMEBUFFER::beginTick` calls `IRPrefab::Camera::prepareZoomFrame`
once, after the camera controls and ahead of every stage that places
camera-following content. It advances the phase and publishes the frame's
`(c, p, a)` in `C_CameraZoomFrameState`. The main gather, `FRAMEBUFFER_TO_SCREEN`,
the sprite grid, and `ENTITY_CANVAS_TO_FRAMEBUFFER` read that sample through
`IRPrefab::Camera::zoomFrame` / `screenResidual` and never advance it.

`zoomFrame` hands a sample out only when it was published in the current
frame, for the pose the caller is about to place with. The frame identity is
the RENDER event tick (`IRSystem::getEventTickCount`), which advances at the
top of each RENDER pass: `prepareZoomFrame` stamps it on the sample and
`zoomFrame` compares it. A pipeline with no `TRIXEL_TO_FRAMEBUFFER` therefore
gets `nullptr` even when the camera has not moved since a frame that did
publish, and so does a consumer that runs after a camera write. Either stage
places with the snapped formula. The stamp gates readers only: the carried
phase stays, and the next prepare advances from the last frame that had one. A
reader outside the RENDER pipeline runs between two passes and sees the sample
of the frame on screen.

Only a canvas that follows **both** the camera's position and its zoom takes
the continuous translation. The GUI canvas ignores the camera; handed the
frame's sample it would shift by the carried phase every time the zoom moved.

The rotated-view scatter anchors on `floor(c)` whole cells and adds the
camera's sub-cell offset itself. Under the continuous policy that offset is
the detached camera term above, so voxels, the SDF and overlay content the
single-canvas gather composites during rotation, and detached canvases all
sit on `floor(H) + a` and the residual is the remainder of one quantity.
Under the snapped policy the scatter keeps its unrounded sub-cell offset;
the residual then counts the sub-pixel fraction a second time, and a
silhouette edge steps back one screen pixel under a pan at a non-cardinal
yaw. The snapped rotated path is left as it is so its references do not move.

## Persistence

`C_Camera` is at save version 2: the policy flag. A version-1 row is the raw
image of an empty struct, one byte of no defined value, so its migrator
discards the byte and restores the snapped policy. `C_CameraZoomFrameState` is
opted out: the phase is re-derived every frame and any starting phase places
content at the same total offset. The engine's own camera is `C_Persistent`
and never enters a snapshot; its policy, zoom, and phase survive
`resetGameplay` in place.

## Trade-offs

- **Nearest minification shimmers.** Between density steps a backing texel
  covers `z / S` in `(0.5, 1]` framebuffer pixels and the gather drops texels
  as the ratio changes. A filtered resolve would need an ownership rule for
  depth, entity id and alpha across a silhouette; there is none.
- **Density steps at each integer zoom.** At `z = 2.01` the canvas rasters at
  three texels per unit, not two: more fill, and a larger retained backing the
  first time. Later pulses reuse the capacity.
- **Hi-Z reuse pauses while the zoom moves.** The occlusion-lag source is
  stale on any frame whose zoom differs from the last, so a pulse or dolly
  runs with the chunk cull off until the zoom settles.
- **`NONE` and `POSITION_ONLY` ignore the zoom for density**, so at a
  fractional zoom a texel there spans a fractional pixel count and magnifies.
  `FULL` is the default.

## Validators

- `fleet-run IrredenEngineTest --gtest_filter='*CameraZoom*:*CameraSubPixel*:*TriangleStep*'`
  — the policy, the density rule, and rasterized-edge sweeps (pan, off-origin
  zoom, both) with the zero-phase and density-scaled splits as negative
  controls. An edge at framebuffer position `X` is drawn at pixel
  `round(X)` and then at screen pixel `round(X) * outputScale + r`; the oracle
  skips samples within `1e-9` of a pixel centre, where the last bit of the
  arithmetic picks the side.
- `python3 scripts/render-continuous-zoom-metric.py zoom | pan | jitter` — the
  same properties read off `IRShapeDebug` captures of a calibration cube,
  with the snapped policy as the control.
- `python3 scripts/gui-verify.py IRShapeDebug -- --continuous-zoom-gui-test`
  — hover and drag-pan picking at a continuous zoom of 2.5.
- `python3 scripts/render-verify.py --all` — the snapped policy is unchanged.
