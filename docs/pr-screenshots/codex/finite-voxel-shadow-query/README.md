# Finite voxel-face shadow queries

Parent: `aa0445fe171b4df5ed46ebf1aae25f192a7f8345`. Native Metal on Apple M4 Max.
Captures are 2560×1440, unfiltered full frames. All retained runs exited CLEAN.

GRID and revoxelized casters retain their projected face corner and two edges
in the same bounded index as rigid source casters. Complete tiles intersect
those finite quads at the receiver fragment instead of extrapolating a map
sample. All indexed voxel casters write the fallback depth layer; analytic and
legacy casters keep the primary layer, so an exact voxel miss cannot erase an
independent blocker. Overflow retains the existing plane-aware fallback and its
world/camera face markers. Off-map faces allocate no records. No blur, bias,
coverage inflation, buffer growth or new per-entity CPU work is added.

## Strict edge evidence

GRID caster onto continuous source-face floor, yaws 0/90/180/270, fixed pivot,
zoom 2, base subdivisions 1, measured GRID density 2, screenshot iso scale 8/4.
The existing one-pixel Chebyshev boundary tolerance is unchanged.

| Yaw | Parent missing / excess | Final missing / excess | Parent | Final |
|---|---|---|---|---|
| 0 | 1 / 1 | 0 / 0 | [2150](capture-2150.png) | [2164](capture-2164.png) |
| 90 | 2 / 7 | 0 / 0 | [2151](capture-2151.png) | [2165](capture-2165.png) |
| 180 | 13 / 15 | 0 / 0 | [2152](capture-2152.png) | [2166](capture-2166.png) |
| 270 | 6 / 24 | 0 / 0 | [2153](capture-2153.png) | [2167](capture-2167.png) |

All four final frames equal the initial index experiment's 2154–2157 byte for
byte in RGB after off-map allocation culling. Metric logs are retained. The
visible test mask is image-derived; its pixel count can change with shading,
so counts are per-frame acceptance results rather than a fixed-mask subtraction.

The revoxelized caster onto the same source floor improves but is **not accepted**:

| Yaw | Parent missing / excess | Indexed missing / excess | Parent | Indexed |
|---|---|---|---|---|
| 0 | 2 / 8 | 0 / 0 | [2168](capture-2168.png) | [2160](capture-2160.png) |
| 90 | 4 / 0 | 0 / 0 | [2169](capture-2169.png) | [2161](capture-2161.png) |
| 180 | 997 / 0 | 971 / 0 | [2170](capture-2170.png) | [2162](capture-2162.png) |
| 270 | 674 / 13 | 645 / 2 | [2171](capture-2171.png) | [2163](capture-2163.png) |

These indexed captures precede off-map culling. The footprint oracle uses its
existing camera-frame center-rounding model at private density 1. Reconcile that
model with actual inverse-resampled occupancy before attributing the remaining
large mismatch solely to casting; neither the expected hull nor its tolerance
has been altered to pass. This PR does not certify arbitrary rotated occupancy.

## Capacity, cost and limitations

The index still holds 65,536 records and 64 faces per tile. GRID/revoxelized
faces now compete with rigid-source faces, so denser scenes can lose previously
exact source queries. GPU allocation order can affect which tiles overflow.
This bounded sparse-scene improvement is not a scalable exact-shadow guarantee.
Overflow/temporal quality and representative population profiling remain pending.

Full stress scene at yaw 45°, zoom 1, base subdivisions 1, frozen spin, no AO:
182 GPU timing samples per run. `voxelSunFaces` mean: parent 0.036 ms, initial
experiment 0.070 ms, final 0.069 ms. Reports and full scene captures 2159
(parent), 2158 (initial), and 2172 (final) are retained. These single runs measure
local added cost only; they are not a controlled population benchmark or FPS
claim. The final run's whole-scene timing is noisier. Existing striped geometry
and SDF-floor shadow artifacts remain visible; this scene is context, not an
all-mode visual acceptance gate.

SDF/GRID raster-origin receivers retain their existing sampling. Their continuous
winning surface geometry must reach fragment presentation before they can get
the same exact edge result. Analytic casters also still use the primary map path.
Windows/OpenGL native validation remains pending.

## Deterministic checks

`test_render_source_face_index.py` executes the actual GLSL and Metal indexers
with serial atomic stubs against independent tile enumeration, including both
cascades, reflected/reversed edges, exact recorded ray depth, degenerate/off-map
faces, 64/65 tile candidates, and 65,536/65,537 records. Whole-buffer canaries
catch writes outside the record/list allocations. Six mutations per backend
must fail. GPU concurrency, caster transforms and raster writes are not emulated.

The mixed-layer test executes both production surface loops for world, camera
and rigid-source markers; complete misses skip only the indexed layer, overflow
retains it, and independent blockers behind an indexed miss survive. Existing
source-face projection and layout tests remain in the render suite.

Native IRCanvasStress build, header-checks, format-changed, ruff and all 34
render harness suites pass. Focused review found the off-map quota waste; the
lazy-allocation fix and deterministic regression controls are included.

## Recipes

```sh
fleet-build --target IRCanvasStress -j 3
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-grid --probe-floor-mode source --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 2 --auto-screenshot 6 --sweep-yaw 0 4.71238898 4
python3 scripts/render-shadow-box-metric.py --grid --effective-subdivisions 2 --iso-scale 8 4 --strict-edges docs/pr-screenshots/codex/finite-voxel-shadow-query/capture-2164.png docs/pr-screenshots/codex/finite-voxel-shadow-query/capture-2165.png docs/pr-screenshots/codex/finite-voxel-shadow-query/capture-2166.png docs/pr-screenshots/codex/finite-voxel-shadow-query/capture-2167.png
fleet-run --timeout 60 IRCanvasStress --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 1 --yaw 0.78539816 --auto-profile --auto-screenshot 120 --sweep-yaw 0.78539816 0.78539816 1
```

For revoxelized captures remove `--probe-grid`; metric removes `--grid` and uses
`--effective-subdivisions 1`. The published revoxelized metrics are expected to
fail at 180/270 degrees. No threshold relaxation is justified by this change.
