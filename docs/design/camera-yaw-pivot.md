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

1. **Screen center (default).** `F` is the z=0 world point under screen center,
   `isoPixelToPos3D(cameraIso, 0)` (the legacy #1352 point). Un-panned this is the
   world origin — the point where the GRID/world content and the DETACHED entity
   canvases coincide (the detached composite places entities at
   `floor(getCameraPosition2DIso()) + isoYawed(world)`, NOT the effective offset).
   So the origin is the content center for alignment, and the scene rotates about
   it. **Do not** pivot about the `visibleIsoViewport` viewCenter
   (`canvasSize/2 - trixelOriginOffsetZ1`): that point is ~1 trixel off the
   content center, so feeding it as the default focus shifts the effective-offset
   world content relative to the raw-offset detached entities — yaw-varying — which
   the canvas_stress detached canary surfaces as rotation jitter (#1942 → #1944).
   Pivoting about a viewport point at its true center would require the detached
   path to consume `getEffectiveCameraIso()` too (follow-up).
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

The pivot **math is correct** (proven, not assumed): `shape_debug
--pivot-focus-demo` pins an explicit focus on a pillar at world (8,−8,10)
(off-origin, z>0 — the hard case) and sweeps yaw. The pillar's centroid holds
the exact screen center (measured 1279.5,720.5 vs center 1280,720) while the
ring of markers orbits it; a broken pivot drifts hundreds of px (#1926 measured
1024px). See `docs/pr-screenshots/claude/1926-camera-pivot-screen-center/`.

Corollary: when a scene appears to "swing" under rotation, the pivot is correct
and the **content is laid out off-center** — pin a focus (cursor mode, or a
content-centroid focus) to rotate it in place.

## History

- #1352 / #1362 — first focus-pivot (panned-and-rotated correctness).
- #1921 / #1927 — explicit point-of-interest focus (`setRotationPivotFocus`).
- #1926 / #1942 — the `cameraYawPivotOffset` helper + the cursor pivot mode.
- #1944 — reverted #1942's "exact viewport-center default": the `viewCenter` is
  ~1 trixel off the content center, so it shifted effective-offset world content
  relative to the raw-offset detached entities (canvas_stress rotation jitter).
  The default is the legacy origin-based screen-center point again.
