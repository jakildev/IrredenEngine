# ShapeDebug shadow-footprint evidence

The capture fixture contains grounded voxel-box and analytic-SDF versions of a
cone and torus on the same non-blocking receiver. Ambient occlusion is disabled,
and each fixed camera pose is captured once with sun shadows and once with
`--no-shadows`. The metric classifies receiver pixels darkened by the paired
capture, so caster pixels and the background cannot enter the observed mask.

- Source: branch `claude/4274-shadow-footprint-accuracy`, measured fixture revision
  `0dc489fa46085a09a9ce61a14765d2ef2ae61173`, parent revision
  `1eefc4c632e80db9e89c85ec20b52b623230374e`
- Backend: Metal, Apple M4 Max, macOS 26.5.2 arm64, `macos-debug`
- Output: 2560x1440 (2x the 1280x720 logical canvas)
- Lighting: normalized sun `(0.349128, 0.847883, -0.399004)`, receiver top
  `z=4.375`, AO disabled
- Geometry: 233 occupied cone boxes, 504 occupied torus boxes; analytic
  expectations use the renderer's effective density 4 and its corresponding
  `SDF <= 0.125` surface threshold

## Commands

```bash
fleet-run IRShapeDebug --auto-screenshot 10 --shadow-footprint-probe --no-ao
fleet-run IRShapeDebug --auto-screenshot 10 --shadow-footprint-probe --no-ao --no-shadows
python3 scripts/render-shape-shadow-footprint.py <shadowed.png> \
  --unshadowed <no-shadows.png> --yaw <0-or-pi-over-4> \
  --effective-subdivisions 4 --diagnostic <output.png>
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

The fixture log derives the receiver from its rendered BOX contract at capture
time: center `z=5`, size `z=2`, effective subdivisions 4, top `z=4.375`.
The metric uses that plane, exact ray/AABB intersections for every occupied
voxel box, and the renderer's density-scaled analytic representation threshold
(`0.5 / 4 = 0.125`) for both ray hits and conservative bounds. Tolerance is two
pixels; a shape passes when both missing and excess ratios are at most 15%.

| Pose | Caster | Expected px | Observed px | Missing | Excess | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| yaw 0° | cone voxel | 33,740 | 33,833 | 4.11% | 3.91% | pass |
| yaw 0° | cone SDF | 27,673 | 59,619 | 0.78% | 51.01% | fail |
| yaw 0° | torus voxel | 47,771 | 46,478 | 2.86% | 0.63% | pass |
| yaw 0° | torus SDF | 19,713 | 39,034 | 0.00% | 47.32% | fail |
| yaw 45° | cone voxel | 33,192 | 30,645 | 8.23% | 0.70% | pass |
| yaw 45° | cone SDF | 22,866 | 24,431 | 8.29% | 16.14% | fail |
| yaw 45° | torus voxel | 48,403 | 47,714 | 0.73% | 0.31% | pass |
| yaw 45° | torus SDF | 9,121 | 6,530 | 24.92% | 5.21% | fail |

Every expected region is nonempty. The focused suite pins the density-derived
receiver plane and the nonzero analytic boundary, including a torus point with
positive zero-level SDF that the renderer includes at density 4. It also
substitutes 1.35x and 0.65x geometry: each shape at both yaws exceeds the same
15% gate, and the nominal/wrong-size PNG pair returns 0/1 through the CLI.

## Classification

The voxel casters pass at both cardinal and non-cardinal yaw while the SDF
casters fail. This remains unresolved rather than excluding the common receiver
or filter: the representations have different silhouettes, the observed masks
overlap, and the 15% voxel pass gate is not an identity proof. The corrected
oracle now treats the renderer's density-expanded analytic surface as an
intended representation difference instead of silently counting it as caster
error.

At yaw 0°, the 51.01% cone excess and 47.32% torus excess overlap the
exact-cardinal analytic-caster continuity defect owned by #4193 / PR #4258.
Those numbers are evidence for that active task, not a second continuity fix;
the residual footprint must be measured again after #4193 lands. At yaw 45°,
the cone has 16.14% excess while the torus has 24.92% missing coverage, so the
non-cardinal result is unresolved rather than one shared geometry failure. The
next probe is to capture each analytic caster alone at yaw 45° and measure it
in an isolated ROI, removing cross-caster attribution before comparing its
projected SDF surface with the observed shadow.

Observed shadow pixels are currently assigned to the nearest expected centroid.
The yaw-0 shadows overlap, so SDF excess can be redistributed between shapes;
the voxel controls remain within the 15% gate, but neither that result nor the
per-SDF split is an independent attribution. The bounded repair here is the
reproducible fixture, corrected oracle, and explicit next probe. The broader
exact-receiver contract remains with #4231.

Diagnostic colors are green for agreement, cyan for missing shadow, and red
for excess shadow.

- [yaw 0° crop](yaw0-crop.png), [full shadows-on](yaw0-shadows-on.png),
  [shadows-off](yaw0-shadows-off.png), [diagnostic](yaw0-diagnostic.png)
- [yaw 45° crop](yaw45-crop.png), [full shadows-on](yaw45-shadows-on.png),
  [shadows-off](yaw45-shadows-off.png), [diagnostic](yaw45-diagnostic.png)
