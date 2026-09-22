# SDF sphere lighting isolation

Production revision: `33e3906dd147d4eab5c5bc843d03f8f60c41b82f`.
macOS Metal, Apple M4 Max, 2048×1152 full-frame captures. All listed runs
exited CLEAN. No production rendering change is included in this evidence slice.

## Observation

The sphere's speckled region mostly survives disabling shadows. Using the exact
blue albedo `(80,120,240)` in the unlit capture to select its 6,016 pixels,
only **24** pixels differ between shadows enabled (parent
[2131](../box-only-cast/capture-2131.png)) and disabled
([2136](capture-2136.png)). Thus shadow sampling is not the dominant cause of
this pattern. This does not prove that sphere self-shadowing is correct.

The [unlit capture](capture-2139.png) has a single RGB value over that mask.
The [normal overlay](capture-2140.png) maps exactly to the three colors in the
shadow-disabled lit capture:

| Normal overlay RGB | Shadow-disabled lit RGB | Pixels |
|---|---|---:|
| 128,128,0 | 58,86,173 | 1,408 |
| 255,128,128 | 24,36,72 | 1,512 |
| 128,0,128 | 61,91,182 | 3,096 |

Every pixel of the sphere belongs to exactly one row, with no additional lit
color within a normal class. This attributes the visible pattern to face-slot
directional shading in this control, rather than noisy albedo or shadow filtering.
It is not an oracle proving which individual faces should exist.

## Coordinate contract and unresolved geometry

`c_shapes_to_trixel::main` emits three synthetic face slots from each quantized
SDF depth. Its smooth-yaw path emits undeformed view-space diamonds.
`c_compute_sun_shadow::main` inversely rotates those normals by the full visual
yaw. `c_lighting_to_trixel::main` instead selects cardinal world normals from
`visibleFaceIds`. They disagree between cardinal angles on the main SDF canvas.

For a view-space normal `(nx,ny,nz)`, inverse camera yaw gives
`(cos(yaw)*nx - sin(yaw)*ny, sin(yaw)*nx + cos(yaw)*ny, nz)`.
The polarity flip negates the complete normal. This frame conversion alone
does not determine whether the original slot represents a valid surface face.

A temporary change applied that conversion to every non-detached, non-per-axis
canvas at residual yaw. Focused review found the scope too broad: secondary
world-lit canvases can still contain cardinal/deformed geometry. The experiment
was removed. Capture 2138 used the experiment and is excluded from this baseline
evidence; the earlier high-zoom 2137 clipped the sphere and is also excluded.

The next production slice should preserve producer identity and surface data
from the elected sample, then use the same world normal and receiver position
for directional lighting and shadow queries. An analytic surface uses its
geometric gradient; a lattice representation uses its exposed cell-face normal.
For a sphere the analytic outward normal is proportional to surface minus center,
subject to the engine's coordinate convention. Do not substitute that gradient
for intentionally voxelized faces or infer it from a slot alone.

Acceptance needs an independent geometry oracle: verify position lies on the
chosen representation, normal is perpendicular to its face/tangent plane, and
the same transformed normal reaches both lighting consumers. Exercise all slots
and polarities across camera quadrants, secondary canvases, and mixed voxel/SDF
content. A smooth-looking screenshot or a normal-only rotation is insufficient.
This extends the existing [surface-data loss controls](../sdf-receiver-factorial/README.md).

## Reproduction

Build `IRCanvasStress` using `fleet-build --target IRCanvasStress -j 3`.

```sh
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-sphere --pivot-origin --no-spin --no-auto-rotate --no-ao --no-shadows --subdivisions 3 --zoom 2.5 --yaw 0.78539816 --auto-screenshot 6 --sweep-yaw 0.78539816 0.78539816 1
```

Add `--debug-overlay unlit` for 2139 or `--debug-overlay normals` for 2140.
The shadow-enabled parent capture uses the same pose with `--no-shadows`
omitted and a longer screenshot warmup. This static fixture disables spin and
automatic rotation; both report yaw 0.7853982, zoom 2.5 and camera (0,0).

For pixel attribution, load the four images as RGB, select indices where the
unlit image is exactly `(80,120,240)`, count unequal enabled/disabled RGB tuples,
then count `(normal RGB, disabled RGB)` pairs at those indices. The full-frame
files retain floor and shadow context; no filtering or resampling is used.

Remaining work includes the 24 shadow-sensitive sphere pixels, floor shadow
edges, general SDF receiver reconstruction, and explicit sharp sampling. No
visual acceptance threshold or merge-performance hold is relaxed by this result.
