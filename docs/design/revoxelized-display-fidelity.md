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
every sun-facing interior pixel with the lattice's own visibility. The
display draws each face as two triangles and lights each at its geometric
centroid, so the oracle casts one ray per triangle from that centroid toward
the sun (the demo's `kSunDirection`, `(-0.42, -0.60, -0.55)`) through the
destination cells: the triangle is lit when the ray crosses no other
occupied cell. Faces turned away from the sun get no direct light whatever
the overlay says and are excluded. AO is measured separately with
`render-ao-staircase-metric.py`.

The receiver reads the sun texel nearest the centroid, so a trixel whose ray
passes within a fraction of a cell of an occluder can read shadowed at the
terminator. The oracle reports the closest approach of every false-shadow
trixel's ray to an occupied cell; false shadow within
`--terminator-tolerance` (half a cell) is grazing and passes, false shadow
with a clear ray and any missed shadow fail. A conclusive gate also requires
at least one expected shadowed interior as a positive control. All-lit poses
cannot distinguish correct visibility from disabled shadows or a blank capture;
they report `occlusion_exercised: false` and fail the gate as untested, even when
both mismatch counts are zero. Pair the shadow capture with the normals gate.

### Authored casters against resampled casters

Cyan cube, zoom 8, AO off, classified per face by one centre ray (the
measurement that retired the authored-grid caster). The demo's former default
cast the authored cells rotated continuously; the resampled cells are what
the display shows and the receiver reads.

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
where the displayed staircase is open to the sun. The per-face classification
leaves a "missed" column that is its own floor: a face whose centre ray is
blocked while both displayed halves are clear counts as missed in full.

### The resampled casters, per trixel

The same five captures with the resampled casters as the only caster, gated
per trixel. "False / grazing" splits the false-shadow pixels by whether the
trixel's centroid ray passes within half a cell of an occupied cell; the
clearance column is the largest closest approach among them.

| Yaw | Interior px (lit / occluded) | Overlay magenta px | False / grazing | Clearance (cells) | Missed | Gate |
|---:|---|---:|---|---:|---:|---|
| 0 | 335,552 / 0 | 4,096 | 3,136 / 3,136 | 0.014 | 0 | untested occlusion |
| 22.5 | 321,440 / 0 | 0 | 0 / 0 | 0 | 0 | untested occlusion |
| 45 | 272,832 / 7,840 | 19,968 | 7,448 / 7,448 | 0.333 | 0 | pass |
| 67.5 | 178,360 / 24,304 | 33,280 | 1,176 / 1,176 | 0.109 | 0 | pass |
| 90 | 173,264 / 34,496 | 51,200 | 4,704 / 4,704 | 0.125 | 0 | pass |
| 0, authored control | 335,552 / 0 | 169,984 | 130,144 / 129,752 | 0.546 | 0 | fail |
| 45, authored control | 272,832 / 7,840 | 156,160 | 111,720 / 108,976 | 0.667 | 0 | fail |

The near-riser tread band was the per-face floor, not the shadow path: cast
from the trixel centroids, no trixel the lattice expects occluded is lit at
any yaw, and every trixel the lattice expects lit that reads shadowed has a
ray passing within a third of a cell of an occupied cell. Across the four
yaws with any self-shadow, 370 lit trixels have a clearance of 0.35 cells or
more and none of them reads shadowed, while the flips concentrate below 0.15
cells (21 trixels within 0.02 at yaw 0, 8 of them flipped). That is the
signature of the nearest-texel read at a terminator, bounded by the texel's
half-diagonal, and the caster and the surface receiver were both traced with
no defect found: the bake rasterizes the exposed sun-facing faces of the same
cells the display draws, and the receiver compares the receiver plane at the
nearest texel centre with the stored finite face. The authored-caster
captures fail the same gate with false shadow on trixels whose rays clear
every cell by more than half a cell. At zoom 4 the cube's floor shadow is a
clean stepped silhouette. Captures under
`docs/pr-screenshots/claude/million-entity-render-lit-staircase/` (per-face
measurement, both caster modes) and
`docs/pr-screenshots/claude/million-entity-render-resampled-casters/`
(the default casters after the retirement, with the authored-caster lit
captures for comparison).

```sh
fleet-run IRCanvasStress --only revox --focus-revox 1 --no-spin --no-auto-rotate --pivot-origin --no-ao --subdivisions 1 --zoom 8 --debug-overlay shadow --auto-screenshot 10 --sweep-yaw 0 1.57079633 5
python3 scripts/render-revox-face-metric.py <shadow-capture.png> --fixture cube --yaw 45 --shadow-overlay
```

## Decision

The revoxelized display is a faithful projection of the resampled cells: a
lone tread trixel beside a riser is the correct isometric projection of a
diagonal step, and no pixel is owned by the wrong face. The objectionable
look is the nearest-cell resample itself, and under lighting a sun-shadow
caster that is not the displayed geometry: a revoxelized canvas casts from
its resampled cells, as its receiver reads them, and the authored-cell
caster is retired ([the experiment's record](authored-voxel-shadow-faces.md)).
With that caster gone, the measured 45/67.5/90-degree poses pass the
trixel visibility gate. The all-lit 0/22.5-degree poses do not exercise
occlusion. Residual false shadows in this fixture have ray clearances within
a third of a cell, consistent with nearest-texel sampling at a terminator;
no receiver bias is added to hide them.

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
the steps (never a fix per the campaign spirit); a receiver bias or a wider
oracle tolerance to absorb the terminator residual.
