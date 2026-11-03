# Box-only analytic shadow submissions

Baseline: `df7a81d119fb38ea94085596ad6ec4efea950015`.
After: this commit's `system_shapes_to_trixel.hpp` change. macOS Metal,
Apple M4 Max, 2048×1152 captures. All three native runs exited CLEAN.

The per-canvas descriptor scan skips the non-box depth clear, raster, resolve
and bake when every descriptor is a box. Analytic box casting independently
retains its finite-coverage and shadow-enable guards. Mixed submissions keep
the fallback, including non-box descriptors that might not ultimately cast.
No extra buffers, persistent classification state or ECS lookups are added.

## Regression evidence

Every pair below has zero differing RGB pixels across the full frame. Parent
captures and reports live in [analytic-cast-dispatch](../analytic-cast-dispatch/).
These are regression comparisons, not assertions that existing geometry is correct.

| Scene | Parent capture | After capture |
|---|---|---|
| Box, camera yaw 45° | 2124 | [2130](capture-2130.png) |
| Sphere and floor, camera yaw 45° | 2123 | [2131](capture-2131.png) |
| Rotated box and floor, camera yaw 0° | 2126 | [2132](capture-2132.png) |
| Same, camera yaw 90° | 2127 | [2133](capture-2133.png) |
| Same, camera yaw 180° | 2128 | [2134](capture-2134.png) |
| Same, camera yaw 270° | 2129 | [2135](capture-2135.png) |

## GPU measurements

Reports contain 303 frames and 302 valid samples per executed casting stage;
means include startup and GPU timeline stalls, rather than isolated kernel cost.
This is one run per fixture, not an end-to-end throughput claim.

| Box-only stage | Parent mean ms | After mean ms |
|---|---:|---:|
| shapeCastClear | 0.053 | not dispatched |
| shapeCastBoxes | 0.037 | 0.027 |
| shapeCastFallback | 0.035 | not dispatched |
| shapeCastResolve | 0.056 | not dispatched |
| shapeCastBake | 0.125 | not dispatched |

The four removed rows total 0.269 ms in the parent measurement. Their absence
is the optimization; the small change in box timing is not independently proven.
See [box-after.txt](box-after.txt) and parent `small-after.txt`.
[mixed-after.txt](mixed-after.txt) retains all five casting rows, as required.
Its timings are not evidence of a mixed-scene speedup.

## Reproduction

Build with `fleet-build --target IRCanvasStress -j 3`, then run:

```sh
fleet-run --timeout 60 IRCanvasStress --only shadowbox --probe-analytic-box --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 4 --yaw 0.78539816 --auto-profile --auto-screenshot 240 --sweep-yaw 0.78539816 0.78539816 1
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-sphere --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --yaw 0.78539816 --auto-profile --auto-screenshot 240 --sweep-yaw 0.78539816 0.78539816 1
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-box --analytic-box-yaw 0.37 --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-screenshot 6 --sweep-yaw 0 4.71238898 4
```

Save the profile after each profiling run before the next overwrites it.
Full-frame comparisons use Pillow `ImageChops.difference` on RGB conversions,
requiring `getbbox()` to return `None`.

Native build, format-changed, header-checks and the existing box dispatch
partition/mutation test pass. Focused review found no binding/lifecycle blockers.
Windows/OpenGL execution remains pending. Sphere self-shadow speckling,
receiver reconstruction and sharp shadow sampling remain separate open work;
this change neither blurs nor repairs them. SDF ownership cost is still an
independent merge consideration.
