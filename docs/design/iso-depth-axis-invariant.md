# Orthographic view and cardinal storage frames

The engine keeps two distinct coordinate frames. Cardinal storage preserves
integer voxel-face addresses. Display projection determines the pixels and
continuous depth of the current camera view. A camera rotation changes the
latter without changing authored voxels, local canvases or hierarchy transforms.

## Projection and inverse

The unrotated projection rows are `X=(-1,1,0)`, `Y=(-1,-1,2)` and
`D=(1,1,1)`. They are mutually orthogonal with squared lengths 2, 6 and 3.
For a unit view-to-world quaternion `q`, rotate all three rows by `q`.
A world point `p` projects to `(dot(p,X), dot(p,Y))` with depth `dot(p,D)`.
The exact inverse is:

```
p = X * (screenIso.x / 2) + Y * (screenIso.y / 6) + D * (depth / 3)
```

This inverse exists for every camera rotation. The historical yaw restriction
was a restriction of raster and reconstruction consumers, not a mathematical
restriction of orthographic projection. World up is negative Z.

## Compatibility and spatial view

Spatial view is opt-in through `Camera::setSpatialViewEnabled`. With it off,
the existing cardinal/yaw producers, projection helpers and depth quantization
retain their behavior. The voxel editor enables spatial view by default;
`--legacy-view` supports comparisons against the compatibility path.

With spatial view enabled:

- GRID retains cardinal world-face storage. The compositor projects finite face
  corners through the full quaternion and chooses the visible signed faces,
  including positive Z for underside views.
- Detached canvases retain their camera-relative local bake. Their owner placement
  and depth offset use the full world-view basis; private raster density still
  rescales into the shared framebuffer depth domain.
- SDF bounds, query rays, analytic intersections and receiver reconstruction use
  the same basis. Lighting directions and light-occlusion geometry stay in world
  coordinates. Camera rotation does not rotate the light.
- Picking uses the displayed ray direction. Chunk culling projects cached world
  bounds through the current basis; camera movement does not rebuild voxel bounds.
- Authoring primitives project through the same camera and test the existing
  world depth buffer. They never write hover IDs, lighting or authored voxels.

The half-cell anchor in voxel-face storage is a raster convention. Raw world
projection helpers do not silently subtract it. Consumers decoding stored cell
corners must restore their storage-frame anchor before using world-view APIs.

## Navigation and validation

[Editor spatial navigation](editor-spatial-navigation.md) documents the upright
orbit, deliberate roll, cursor-anchored zoom and platform controls.
[Editor validation](editor-validation.md) owns native replay commands. The
`spatial_navigation` session checks intermediate azimuths and elevations,
underside views, near-pole clamps, roll, unchanged authoring state, gesture
ownership, and rendered signed faces against picking.

`IRCanvasStress --spatial-view --elevation RADIANS --roll RADIANS` applies the
same full camera to mixed GRID, detached and SDF content. Its existing yaw
sweeps retain that elevation and roll for every shot.
