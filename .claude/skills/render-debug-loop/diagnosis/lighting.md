# Diagnosis: Lighting

The lighting stack is mostly built out. The `LIGHTING_TO_TRIXEL` pipeline stage (T-011, PR #185) is the integration point; AO (T-012, PR #197), directional sun shadows (T-013/T-070), flood-fill colored light propagation (T-014, PR #232), LUT palette shading (T-015, PR #198), and fog of war (T-016, PR #238) all run through it. The light volume itself was rewritten on the GPU in PR #448. Outstanding refactors: T-091 migrates AO off `OccupancyGridBuffer` to sample neighbour pixels in `trixelDistances`, and T-094 camera-anchors the GPU light volume so fidelity holds past the static window. See `engine/render/CLAUDE.md` for pipeline position.

## Evaluation pattern

1. Capture a **baseline** screenshot set with lighting disabled (no `C_LightSource` in scene, or set `frameData.lightingEnabled_ = 0` in `system_lighting_to_trixel.hpp` — that's the CPU short-circuit that skips the per-canvas dispatch).
2. Capture the same shots with lighting enabled.
3. Diff: lighting-on frames should modulate voxel and shape canvas pixels; **GUI-canvas pixels must be untouched** (T-011 invariant).

## Symptom lookup

| Symptom | Likely location |
|---------|----------------|
| Lighting pass modulates GUI text/panels | `LIGHTING_TO_TRIXEL` not respecting GUI-canvas bypass |
| No visible lighting effect | Lighting textures unbound, or `isoPixelToPos3D` returning wrong world coords |
| AO missing or wrong at voxel junctions | `c_compute_voxel_ao.{glsl,metal}` neighbour sampling — `OccupancyGridBuffer` indexing today; `trixelDistances` face-tangent taps once T-091 lands |
| Shadow direction wrong | Sun-direction uniform vs. shadow-map sweep axis mismatch in `c_bake_sun_shadow_map` / `c_compute_sun_shadow` |
| Torch doesn't light neighbours | Light-volume seed pass not seeding emissive voxels, or analytic shapes missing from occupancy |
| Cel-shade bands smeared | LUT sampler filter mode (`GL_LINEAR` instead of `GL_NEAREST`) |
| Fog of war reveals through walls | LOS ray casting in `c_fog_to_trixel` not consulting columnar span lists |
| Lighting fidelity drifts past static window | Missing camera-anchor on GPU light volume (T-094) — `worldOrigin_` not subtracted before volume sample |
| Faces lit from wrong direction at non-zero camera yaw | Face normal not rotated by `rasterYaw` in lighting / shadow shaders (#1218). Check `c_lighting_to_trixel.{glsl,metal}` and `c_compute_sun_shadow.{glsl,metal}` apply `R_z(rasterYaw)` to the per-face normal before dotting with sun / light direction. |
| Sun shadow misaligned at non-zero camera yaw | Shadow-map sweep AABB built in mismatched coordinate frame (#1220). Check `system_bake_sun_shadow_map.hpp` AABB construction is in world frame, not in iso frame derived under yaw=0. |

## Baseline-diff screenshots

For each lighting phase, a "lighting on vs. off" pair is worth keeping in `docs/render-baselines/` as a reference. When a regression appears, diff new screenshots against the baseline rather than eyeballing.

## Automated light/shadow-domain harness (V3, #2317)

`scripts/light-verify.py` drives the lighting demo family's
`--light-domain-matrix` (zoom x yaw x pan-distance), `--light-boundary-sweep`
(#2310), and `--hover-sweep` auto-screenshot series, parses each shot's
`DOMAIN-STATE` log line (#2315 V1's `logDomainState` hook), and asserts the
light-gather boundary contract (never `SKIPPED` while in-window/band, always
`SKIPPED` once out of residual reach; monotone residual fade along the
boundary sweep; anchor invariant to zoom/yaw) plus a per-shot image compare
against committed baselines. Run it the same way as `cull-verify.py`:

```
python3 scripts/light-verify.py                    # verify
python3 scripts/light-verify.py --update-baselines  # bless new references
```

Use this instead of the manual "Evaluation pattern" above when checking for
a light-gather / boundary-clamp regression specifically — the symptom table
above is for diagnosing a failure this harness (or a human) already found.
