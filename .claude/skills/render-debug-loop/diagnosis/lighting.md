# Diagnosis: Lighting

`LIGHTING_TO_TRIXEL` is the integration point for AO, directional sun shadows,
flood-fill coloured light propagation, LUT palette shading, and fog of war;
the light volume is computed on the GPU. Pipeline position:
`engine/render/CLAUDE.md`.

## Evaluation pattern

1. Capture a baseline shot set with lighting off — no `C_LightSource` in the
   scene, or `frameData.lightingEnabled_ = 0` in
   `system_lighting_to_trixel.hpp` (the CPU short-circuit that skips the
   per-canvas dispatch).
2. Capture the same shots with lighting on.
3. Diff: lighting-on frames modulate voxel and shape canvas pixels;
   GUI-canvas pixels must be untouched.

Keep the committed references under `creations/demos/<demo>/test/references/`
(`render-verify`) and diff against them rather than eyeballing.

## Symptom lookup

| Symptom | Likely location |
|---|---|
| Lighting pass modulates GUI text/panels | `LIGHTING_TO_TRIXEL` not respecting the GUI-canvas bypass |
| No visible lighting effect | Lighting textures unbound, or `isoPixelToPos3D` returning wrong world coords |
| AO missing or wrong at voxel junctions | `c_compute_voxel_ao.{glsl,metal}` screen-space neighbour taps in `trixelDistances` |
| Shadow direction wrong | Sun-direction uniform vs shadow-map sweep axis in `c_bake_sun_shadow_map` / `c_compute_sun_shadow` |
| Torch doesn't light neighbours | Light-volume seed pass not seeding emissive voxels, or analytic shapes missing from occupancy |
| Cel-shade bands smeared | LUT sampler filter (`GL_LINEAR` instead of `GL_NEAREST`) |
| Fog of war reveals through walls | LOS ray casting in `c_fog_to_trixel` not consulting columnar span lists |
| Lighting fidelity drifts past the static window | `worldOrigin_` not subtracted before the GPU light-volume sample |
| Faces lit from the wrong direction at non-zero camera yaw | Face normal not rotated by `rasterYaw`: `c_lighting_to_trixel.{glsl,metal}` and `c_compute_sun_shadow.{glsl,metal}` must apply `R_z(rasterYaw)` before dotting with the light direction |
| Sun shadow misaligned at non-zero camera yaw | `system_bake_sun_shadow_map.hpp` sweep AABB built in the iso frame at yaw=0 instead of the world frame |

## Automated light/shadow-domain harness

`scripts/light-verify.py` drives the lighting demo family's
`--light-domain-matrix` (zoom × yaw × pan distance), `--light-boundary-sweep`,
and `--hover-sweep` series, parses each shot's `DOMAIN-STATE` log line, asserts
the light-gather boundary contract (never `SKIPPED` while in-window / in-band,
always `SKIPPED` past residual reach; monotone residual fade along the sweep;
anchor invariant to zoom and yaw), and compares each shot against committed
baselines:

```
python3 scripts/light-verify.py                    # verify
python3 scripts/light-verify.py --update-baselines  # bless new references
```

Use it for a suspected light-gather / boundary-clamp regression; the symptom
table diagnoses a failure the harness (or a human) has already found.
