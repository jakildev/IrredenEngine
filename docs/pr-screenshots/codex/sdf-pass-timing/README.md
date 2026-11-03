# SDF dispatch timing

## Scope

Five non-nested GPU rows replace the historical whole-system `shapePass1`
measurement: `shapeOwnerClear`, `shapeDepth`, `shapeOwnerElect`, `shapePublish`,
and `shapeSunCast`. The old public fields remain unwritten for compatibility.
Metal buffer fills now use the timestamp-aware blit helper. CPU `shapeEncode`
covers the shape system's end hook through the scope histogram; it excludes
begin/tick descriptor gathering. The ordinary per-system CPU report remains.

This is instrumentation, not a rendering or performance optimization. No shader,
visibility, projection, owner arbitration or shadow-filtering behavior changes.

## Native measurements

Apple M4 Max, Metal, 2560×1440. Single main canvas. Every listed row has 302
valid GPU samples from 303 frames. The witness confirms fixed yaw, including
warmup. Averages include startup; these are not warmup-excluded distributions.

| GPU row, average ms | Boxes yaw 0 | Boxes yaw 45° | Sphere + shadows yaw 45° | Sphere repeat |
|---|---:|---:|---:|---:|
| shapeOwnerClear | 0.051 | 0.055 | 0.024 | 0.040 |
| shapeDepth | 0.188 | 0.382 | 0.790 | 0.619 |
| shapeOwnerElect | 0.184 | 0.520 | 0.491 | 0.395 |
| shapePublish | 0.354 | 0.494 | 0.412 | 0.363 |
| shapeSunCast | no samples | no samples | 3.024 | 2.744 |

The sphere's casting bundle is consistently the largest measured stage in these
two runs. It includes finite boxes, non-box fallback rasterization, depth resolve
and map bake; this does not isolate which of those operations dominates. The box
fixture has shadows disabled and cannot establish casting cost. No million-entity
or cross-host extrapolation is justified.

These scopes exclude CPU preparation, canvas initialization clears and intervals
between scopes. Their sum need not reproduce the historical whole-system GPU
interval. Timestamp boundary effects and run variability remain; use the raw
reports' min/max and counts. Multi-canvas rows are sampled invocations, not
frame totals. Native OpenGL validation remains pending.

## Pixel preservation and review

- Full-frame RGB: cardinal capture 2115 equals prior production capture 2110
  in `../sdf-surface-reuse/`; pre-blit-fix sweep 2112/2113 also matched 2110/2111.
- Timing-on capture 2116 equals timing-off capture 2117, exactly. Capture 2118,
  with the initial yaw fixed as well, is also identical to 2117.
- Captures 2119/2120 show the shadowed sphere timing workload. Existing sphere
  surface artifacts are not certified or fixed by this instrumentation.
- Native build and header checks pass. Existing SDF winner suite: three tests pass.
- Independent review identified the raw Metal fill encoder bypassing timestamp
  attachment. Routing it through `createBlitEncoder` changes the clear row from
  zero valid samples to 302, the positive control for this correction.

Timing enabled:

![Timing on](capture-2118.png)

Timing disabled:

![Timing off](capture-2117.png)

## Recipes

```sh
fleet-build --target IRCanvasStress -j 3
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-box --probe-sdf-depth-tie --pivot-origin --no-spin --no-auto-rotate --no-shadows --no-ao --subdivisions 1 --zoom 4 --auto-profile --auto-screenshot 240 --sweep-yaw 0 0 1
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-box --probe-sdf-depth-tie --pivot-origin --no-spin --no-auto-rotate --no-shadows --no-ao --subdivisions 1 --zoom 4 --yaw 0.78539816 --auto-profile --auto-screenshot 240 --sweep-yaw 0.78539816 0.78539816 1
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-sphere --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --yaw 0.78539816 --auto-profile --auto-screenshot 240 --sweep-yaw 0.78539816 0.78539816 1
```

For the timing-off comparison omit `--auto-profile`. Captures 2116/2117 also
omitted the initial `--yaw`: their final pixels match, but their warmup was at
zero, so their aggregate profiles are excluded from the fixed-pose table.
A capture sweep sets the shot pose after warmup; always set `--yaw` too when
profiling a static nonzero angle, and require zero travel in the run witness.
