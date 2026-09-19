# Revoxelized display fidelity

A `DETACHED_REVOXELIZE` canvas rotates a solid by resampling it: every
destination cell of the rotated bound inverse-maps to one authored cell
(`revoxSourceCellForDest`, the anchored round-half-up map), and the raster
displays those camera-aligned cells through local triangle reconstruction.
Lit captures of the CanvasStress proof cubes show lone single-trixel treads
inside broad side faces and a tooth pattern along every edge. This document
records the gate that decides whether such a pixel is the true projection of
the resampled cells or a wrong trixel owner, and the decision it supports.

## The gate

`scripts/render-revox-face-metric.py` derives its expectation from the
resample path itself, never from the authored solid: the fixture's centered
authored cells, the camera-composed rotation
`quatInverse(R_z(yaw)) * entityRotation`, and the anchored inverse map over
the destination cube. Each occupied destination cell's three camera-facing
faces are projected as parallelograms in painter order; every interior pixel's
expected owner face is compared with the `--debug-overlay normals` colour, and
the silhouette with the background, inside a one-framebuffer-pixel boundary
band. Placement is the framebuffer centre with no registration. A rounding tie
inside `1e-3` of a half-integer is counted (`tie_cells`) so a float32
disagreement between the CPU model and the kernel is visible when it occurs.

```sh
fleet-run IRCanvasStress --only revox --focus-revox 1 --no-spin --no-auto-rotate --pivot-origin --no-ao --no-shadows --subdivisions 1 --zoom 8 --debug-overlay normals --auto-screenshot 10 --sweep-yaw 0 1.57079633 5
python3 scripts/render-revox-face-metric.py <capture.png> --fixture cube --yaw 45 --diagnostic-prefix /tmp/revox45
python3 scripts/tests/test_render_revox_face_metric.py
```

`--fixture grounded` is the purple cube (`--focus-revox 2`), `--fixture
lprism` the carved multi-colour solid (`--focus-revox 0`), `--upright` the
demo's `--probe-upright` identity pose. The metric does not measure fidelity
to the rotated authored solid, lighting, depth, or picking.

## Evidence

Apple M4 Max, Metal, `macos-debug`, 2560x1440, zoom 8, effective private
density 1, AO and shadows off. Retained under
`docs/pr-screenshots/claude/million-entity-render-face-parity/`.

| Fixture | Yaw | Occupied cells | Expected = observed pixels | Missing / extra / wrong face |
|---|---:|---:|---:|---|
| cyan cube (index 1) | 0 | 1724 | 438,272 | 0 / 0 / 0 |
| cyan cube | 22.5 | 1734 | 419,840 | 0 / 0 / 0 |
| cyan cube | 45 | 1716 | 366,592 | 0 / 0 / 0 |
| cyan cube | 67.5 | 1720 | 398,336 | 0 / 0 / 0 |
| cyan cube | 90 | 1724 | 409,600 | 0 / 0 / 0 |
| purple cube (index 2) | 0 | 1724 | 419,840 | 0 / 0 / 0 |
| purple cube | 22.5 | 1732 | 378,880 | 0 / 0 / 0 |
| purple cube | 45 | 1722 | 358,400 | 0 / 0 / 0 |
| purple cube | 67.5 | 1732 | 359,424 | 0 / 0 / 0 |
| purple cube | 90 | 1724 | 395,264 | 0 / 0 / 0 |
| upright cube | 0, 45 | 1728 | 442,368 / 375,808 | 0 / 0 / 0 |

Every capture's foreground pixel count equals the expectation exactly. The
authored cube has 1728 cells; the nearest-cell resample retains 1716 to 1734.

Controls on the cyan yaw-45 capture: the wrong fixture (163,852 wrong-face
pixels), the wrong yaw (a 22.5° model, 263,458) and the identity model
(149,335) all fail;
the `--debug-raw-trixels` rectangular display fails with 55,511 extra and
148,854 wrong-face pixels. The hermetic suite fails a one-pixel face swap, a
hole, a spike, a blank frame and a wrong pose.

The plain `DETACHED` orbit frame (index 7) passes
`render-source-face-metric.py --shape frame` at the same five yaws with zero
missing or extra silhouette pixels; that gate does not check interior face
boundaries of a multi-voxel solid.

## Direct sun on the resampled staircase

The same script, with `--shadow-overlay`, reads a `--debug-overlay shadow`
capture of the same pose (magenta = any direct-sun occlusion) and compares
every sun-facing interior pixel with the lattice's own visibility: a face is
lit when a ray from its centre toward the sun (the demo's `kSunDirection`,
`(-0.42, -0.60, -0.55)`) crosses no other occupied destination cell. Faces
turned away from the sun get no direct light whatever the overlay says and
are excluded. AO is measured separately with `render-ao-staircase-metric.py`.

Cyan cube, zoom 8, AO off, both caster modes of `BAKE_SUN_SHADOW_MAP` (the
demo's default `--source-face-shadows` casts the authored cells rotated
continuously; `--voxel-face-shadows` casts the resampled cells the display
shows):

| Yaw | Sun-facing interior px (lit / occluded) | Authored casters: false / missed | Resampled casters: false / missed |
|---:|---|---|---|
| 0 | 372,700 / 24,696 | 146,995 / 15,680 | 4,051 / 24,696 |
| 22.5 | 381,098 / 4,312 | 144,554 / 784 | 0 / 4,312 |
| 45 | 317,757 / 9,800 | 131,392 / 1,176 | 9,022 / 1,960 |
| 67.5 | 210,548 / 24,304 | 75,336 / 392 | 1,379 / 0 |
| 90 | 211,166 / 37,667 | 86,972 / 0 | 5,456 / 0 |

"False" is expected lit but shadowed; "missed" is expected occluded but lit.
Only the caster geometry differs between the two columns, so the tooth
pattern in the lit captures is the authored caster cutting through the
resampled receiver: whole half-faces across the cube read as self-shadowed
where the displayed staircase is open to the sun. With resampled casters
false shadow falls 15 to 150 times, and what remains is the near-riser tread
band at 0 and 22.5° that the receiver leaves lit (every occluded pixel the
lattice expects) and a few tread tips at 45°. AO at yaw 45 with the default
casters darkens 6.9% of the cube by at most 5.9% (`occluded_frac 0.0689`,
`max_darkening 0.0588`), so it is not the source of the look. Captures under
`docs/pr-screenshots/claude/million-entity-render-lit-staircase/`.

```sh
fleet-run IRCanvasStress --only revox --focus-revox 1 --no-spin --no-auto-rotate --pivot-origin --no-ao --subdivisions 1 --zoom 8 --debug-overlay shadow --auto-screenshot 10 --sweep-yaw 0 1.57079633 5
python3 scripts/render-revox-face-metric.py <shadow-capture.png> --fixture cube --yaw 45 --shadow-overlay
```

## Decision

The revoxelized display is a faithful projection of the resampled cells: a
lone tread trixel beside a riser is the correct isometric projection of a
diagonal step, and no pixel is owned by the wrong face. The objectionable
look is the nearest-cell resample itself, and under lighting a sun-shadow
caster that is not the displayed geometry: a revoxelized canvas must cast
from its resampled cells, as its receiver reads them, and the authored-cell
caster is retired for that path. The tread band behind each riser that the
receiver still leaves lit is the remaining receiver-side item.

Presentation stays a per-object choice through `RotationMode`: plain
`DETACHED` projects the authored source faces for a smooth rotated solid;
`DETACHED_REVOXELIZE` is for content that should read as voxelized after
rotation, and it sorts, casts and receives on the world convention. A
creation that wants the proof cubes smooth switches them to `DETACHED`; the
engine does not smooth the resample.

Rejected: reconstructing source faces from resampled occupancy (the
information is gone at the resample, and the source-face path already exists);
resampling at a finer destination lattice (cell count grows with the cube of
the density and the result is still a staircase; a zoom-dependent level of
detail is a D7 decision); averaged normals, blurred AO or dilated coverage on
the steps (never a fix per the campaign spirit). The remaining revoxelized
work is the caster switch and the near-riser receiver band measured above.
