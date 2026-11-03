# Main-canvas continuous-yaw lighting normals

Parent: `c2a028c64d1e1ec11e0af0ca951bb418f62d047e`. After: this commit.
macOS Metal, Apple M4 Max, 2048×1152. All capture runs exited CLEAN.

During residual camera yaw, the main canvas emits SDF samples in the continuous
view frame. Its shadow lookup already inversely rotates slot normals by visual
yaw, but directional lighting used a cardinal face-ID lookup. The lighting UBO
now explicitly identifies the main canvas; only its non-detached, non-per-axis
residual-yaw pass applies the same inverse rotation and encoded polarity.
Other canvas routes retain their existing normal interpretation.

The UBO grows from 64 to 80 bytes with no per-pixel allocation. It is uploaded
in full once per lit canvas, replacing the once-per-frame upload. Full uploads
avoid Metal's preservation blit for partial writes after prior buffer use.
This adds per-canvas submission work; no throughput improvement is claimed.

## Evidence

| Control | Before | After | Result |
|---|---|---|---|
| Sphere normals, yaw 45° | [2140](../sdf-lighting-normal-frame/capture-2140.png) | [2149](capture-2149.png) | Exact expected inverse-yaw color mapping over the full frame |
| Sphere directional shading, shadows disabled | [2136](../sdf-lighting-normal-frame/capture-2136.png) | [2148](capture-2148.png) | Lighting uses the corrected world normals; synthetic face pattern remains |
| Rotated box/floor at cardinal yaws | [2132–2135](../box-only-cast/README.md) | 2143–2146 | All four full RGB frames identical |
| GRID + detached comparison, yaw 45° | Not captured | [2147](capture-2147.png) | Native smoke only; not visual acceptance of its existing stripes |

At 45°, the expected side normals are `(-sqrt(.5),-sqrt(.5),0)` and
`(sqrt(.5),-sqrt(.5),0)`. Their encoded colors are `(37,37,128)` and
`(218,37,128)`. The whole normal image matches the parent after substituting
these two classes, with zero unexpected pixels; the `(128,128,0)` Z class and
background stay unchanged. The unchanged unlit sphere mask contains 6,016 pixels:
3,096 and 1,512 in the two side classes, and 1,408 in Z.

`test_render_lighting_normal_frame.py` executes the actual CPU selector and
both shader branches/helpers against an independent double-precision inverse
camera rotation. It covers 32 yaw positions and ±0.00001 perturbations, all
three slots, both polarities, main/secondary canvases, detached and per-axis
routes, and exact cardinals. Reversed yaw, dropped main-canvas/per-axis gates,
and dropped polarity each fail the oracle. The decode helper is stubbed to
supply each polarity; this suite does not revalidate the packed depth format.

Native build, format-changed, header-checks, ruff and all 33 render harness
suites pass. Focused review found the partial-upload preservation cost; the
full-upload correction is included. Windows/OpenGL native execution is pending.

## Recipes

Build with `fleet-build --target IRCanvasStress -j 3`.

```sh
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-sphere --pivot-origin --no-spin --no-auto-rotate --no-ao --no-shadows --subdivisions 3 --zoom 2.5 --yaw 0.78539816 --auto-screenshot 6 --sweep-yaw 0.78539816 0.78539816 1
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-box --analytic-box-yaw 0.37 --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-screenshot 6 --sweep-yaw 0 4.71238898 4
fleet-run --timeout 60 IRCanvasStress --only compare --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 1 --yaw 0.78539816 --auto-screenshot 6 --sweep-yaw 0.78539816 0.78539816 1
```

Add `--debug-overlay normals` to the sphere recipe for the normal oracle.
Full-frame RGB comparisons use Pillow without filtering or resampling.

This fixes a coordinate-frame disagreement, not SDF surface reconstruction.
True hit position/normal, finite receiver coverage, the sphere's synthetic
face pattern and sharp shadow edges remain open. No blur, normal averaging,
shadow-bias change or threshold relaxation is included.
