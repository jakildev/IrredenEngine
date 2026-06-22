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

1. **Screen center (default).** `F` is the z=0 world point under the EXACT
   viewport center: `isoPixelToPos3D(viewCenterIso, 0)`, where `viewCenterIso =
   canvasSize/2 - trixelOriginOffsetZ1(canvasSize) - cameraIso` (derive: a world
   point lands at screen center when `pos3DtoPos2DIso(W) + cameraIso ==
   canvasCenterIso`). The offset is the drift-cancel `cameraYawPivotOffset` form
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
