# ShapeDebug shadow-footprint evidence

The capture fixture contains grounded voxel-box and analytic-SDF versions of a
cone and torus on the same non-blocking receiver. Ambient occlusion is disabled,
and each fixed camera pose is captured once with sun shadows and once with
`--no-shadows`. The metric classifies receiver pixels darkened by the paired
capture, so caster pixels and the background cannot enter the observed mask.

- Source: branch `claude/4274-shadow-footprint-accuracy`, measured fixture revision
  `ae28cc30731dc8bbbb89d59c9bdf75df4e26f2de`, parent revision
  `01752d9aabb12245ef4dd87ab4d9aa15049f99c4`
- Backend: Metal, Apple M4 Max, macOS 26.5.2 arm64, `macos-debug`
- Output: 2560x1440 (2x the 1280x720 logical canvas)
- Lighting: normalized sun `(0.349128, 0.847883, -0.399004)`, receiver top
  `z=4`, AO disabled
- Geometry: 233 occupied cone boxes, 504 occupied torus boxes; analytic
  expectations come directly from the cone and torus signed-distance fields

## Commands

```bash
fleet-run IRShapeDebug --auto-screenshot 10 --shadow-footprint-probe --no-ao
fleet-run IRShapeDebug --auto-screenshot 10 --shadow-footprint-probe --no-ao --no-shadows
python3 scripts/render-shape-shadow-footprint.py <shadowed.png> \
  --unshadowed <no-shadows.png> --yaw <0-or-pi-over-4> --diagnostic <output.png>
python3 scripts/tests/test_render_shape_shadow_footprint.py
```

Both demo invocations exited zero with `ir-run: RESULT=CLEAN exe=IRShapeDebug
exit=0`. The four captured outputs inherit that clean-exit verdict as follows:

| Pose | Shadows | Invocation result |
| --- | --- | --- |
| yaw 0° | on | `RESULT=CLEAN`, exit 0 |
| yaw 45° | on | `RESULT=CLEAN`, exit 0 |
| yaw 0° | off | `RESULT=CLEAN`, exit 0 |
| yaw 45° | off | `RESULT=CLEAN`, exit 0 |

The metric uses exact ray/AABB intersections for every occupied voxel box and
independently samples the analytic surface along the sun ray. Tolerance is two
pixels; a shape passes when both missing and excess ratios are at most 15%.

| Pose | Caster | Expected px | Observed px | Missing | Excess | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| yaw 0° | cone voxel | 35,917 | 34,733 | 5.75% | 4.64% | pass |
| yaw 0° | cone SDF | 25,696 | 58,719 | 0.00% | 53.01% | fail |
| yaw 0° | torus voxel | 43,739 | 46,743 | 2.70% | 6.93% | pass |
| yaw 0° | torus SDF | 16,184 | 38,769 | 0.00% | 56.22% | fail |
| yaw 45° | cone voxel | 36,070 | 31,202 | 12.71% | 1.11% | pass |
| yaw 45° | cone SDF | 20,260 | 23,874 | 8.41% | 20.79% | fail |
| yaw 45° | torus voxel | 44,563 | 47,892 | 1.46% | 6.94% | pass |
| yaw 45° | torus SDF | 7,568 | 6,352 | 26.04% | 12.96% | fail |

Every expected region is nonempty. The test suite also substitutes 1.35x and
0.65x geometry: each shape at both yaws exceeds the same 15% gate, proving the
oracle rejects deliberately wrong-sized controls.

## Classification

The voxel casters pass at both cardinal and non-cardinal yaw while the SDF
casters fail on the same receiver and filter. That paired control localizes the
discrepancy to the analytic caster path rather than the common receiver or
filter.

At yaw 0°, the 53.01% cone excess and 56.22% torus excess overlap the
exact-cardinal analytic-caster continuity defect owned by #4193 / PR #4258.
Those numbers are evidence for that active task, not a second continuity fix;
the residual footprint must be measured again after #4193 lands. At yaw 45°,
the cone has 20.79% excess while the torus has 26.04% missing coverage, so the
non-cardinal result is unresolved rather than one shared geometry failure. The
next probe is to capture each analytic caster alone at yaw 45° and measure it
in an isolated ROI, removing cross-caster attribution before comparing its
projected SDF surface with the observed shadow.

Observed shadow pixels are currently assigned to the nearest expected centroid.
The yaw-0 shadows overlap, so SDF excess can be redistributed between shapes;
the voxel controls remain within 7%, but the per-SDF excess split is not an
independent attribution. The bounded repair here is the reproducible fixture,
independent oracle, and explicit next probe. The broader exact-receiver contract
remains with #4231.

Diagnostic colors are green for agreement, cyan for missing shadow, and red
for excess shadow.

- [yaw 0° crop](yaw0-crop.png), [full shadows-on](yaw0-shadows-on.png),
  [shadows-off](yaw0-shadows-off.png), [diagnostic](yaw0-diagnostic.png)
- [yaw 45° crop](yaw45-crop.png), [full shadows-on](yaw45-shadows-on.png),
  [shadows-off](yaw45-shadows-off.png), [diagnostic](yaw45-diagnostic.png)
