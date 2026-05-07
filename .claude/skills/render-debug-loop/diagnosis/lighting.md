# Diagnosis: Lighting (T-011 onward)

The lighting stack is being built out in phases — the `LIGHTING_TO_TRIXEL` pipeline stage has landed (T-011, PR #185); AO (T-012), directional shadows (T-013), flood-fill propagation (T-014), LUT palette (T-015), and fog of war (T-016) are the incoming phases. See `engine/render/CLAUDE.md` for pipeline position.

Populate this section as phases land. For now, the evaluation pattern:

1. Capture a **baseline** screenshot set with lighting disabled (no `C_LightSource` in scene, or set `frameData.lightingEnabled_ = 0` in `system_lighting_to_trixel.hpp` — that's the CPU short-circuit that skips the per-canvas dispatch).
2. Capture the same shots with lighting enabled.
3. Diff: lighting-on frames should modulate voxel and shape canvas pixels; **GUI-canvas pixels must be untouched** (T-011 invariant).

## Symptom lookup (to be expanded)

| Symptom | Likely location |
|---------|----------------|
| Lighting pass modulates GUI text/panels | `LIGHTING_TO_TRIXEL` not respecting GUI-canvas bypass |
| No visible lighting effect | Lighting textures unbound, or `isoPixelToPos3D` returning wrong world coords |
| AO missing at voxel junctions (T-012) | `computeAO` neighbor-sampling indices against the 3D occupancy grid |
| Shadow direction wrong (T-013) | Sun-direction uniform vs. shadow-map sweep axis mismatch |
| Torch doesn't light neighbors (T-014) | BFS frontier not seeding emissive voxels, or occupancy grid missing analytic shapes |
| Cel-shade bands smeared (T-015) | LUT sampler filter mode (GL_LINEAR instead of GL_NEAREST) |
| Fog of war reveals through walls (T-016) | LOS ray casting not consulting columnar span lists |

## Baseline-diff screenshots

For each lighting phase, a "lighting on vs. off" pair is worth keeping in `docs/render-baselines/` as a reference. When a regression appears, diff new screenshots against the baseline rather than eyeballing.
