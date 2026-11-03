# Analytical-box lane scheduling: visual control

Metal Debug, Apple M4 Max, macOS 26.5.2, 2560×1440. Both arms use the
rebuilt candidate demo. Before substitutes only the original GLSL/Metal box
caster shaders from `fde978618642d90fa1ec9fb722075e847d276a76`; after restores
the candidate source shaders. Runtime staging was restored after capture.

```bash
fleet-run IRCanvasStress --only shadowbox,floor,revox,rigid \
  --probe-analytic-box --no-spin --no-auto-rotate --pivot-origin \
  --zoom 2 --auto-screenshot 6 --sweep-yaw 0 5.585053606 9
```

`before-yawN.png` and `after-yawN.png` cover 0°, 40°, …, 320°. All nine
pairs have identical RGB pixels. Original capture IDs were 2467–2475;
candidate IDs were 2476–2484. The change schedules three existing faces
on independent lanes; it does not change projection or smooth existing edges.

| Before, 40° | After, 40° |
|---|---|
| ![Before](before-yaw40.png) | ![After](after-yaw40.png) |

[Performance controls and limitations](../../../perf/box-shadow-index-lanes.md).
