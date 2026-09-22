# Shared continuous SDF box intersection

This is a geometry prerequisite, not an SDF floor-edge fix. The integer SDF
producer forwards to the shared continuous interval helper. Its normal is the
signed box entry plane, not a display slot. Fractional queries remain available
for future fragment reception; they are not wired into the fragment yet.

## Native preservation controls

macOS Metal, IRCanvasStress, 2560×1440, actual subdivision 1. Parent is
`9dff0abe18e52d6ac9863922cad28f9f4e0f632d`. Parent shader assets were loaded into
the same binary; final shader assets were restored afterward. All eight paired
full-frame RGB images are identical (zero changed pixels). Existing jagged floor
shadows are deliberately visible and remain unresolved.

| Yaw degrees | Parent | Shared helper |
|---|---|---|
| 0 | [2197](capture-2197.png) | [2201](capture-2201.png) |
| 90 | [2198](capture-2198.png) | [2202](capture-2202.png) |
| 180 | [2199](capture-2199.png) | [2203](capture-2203.png) |
| 270 | [2200](capture-2200.png) | [2204](capture-2204.png) |
| 22.5 | [2209](capture-2209.png) | [2205](capture-2205.png) |
| 112.5 | [2210](capture-2210.png) | [2206](capture-2206.png) |
| 202.5 | [2211](capture-2211.png) | [2207](capture-2207.png) |
| 292.5 | [2212](capture-2212.png) | [2208](capture-2208.png) |

```sh
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 2 --auto-screenshot 6 --sweep-yaw 0 4.71238898 4
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 2 --auto-screenshot 6 --sweep-yaw 0.39269908 5.10508806 4
```

## Deterministic geometry controls

`python3 scripts/tests/test_render_sdf_surface_contract.py` executes actual GLSL
and Metal helper bodies against an independent double-precision world-ray oracle.
Each backend covers 480,000 integer/fractional rays, densities 1/2/4/8 and a full
turn with near-angle perturbations. Hit/miss, interval, finite bounds and signed
normals pass, including explicit X/Y/Z corner ownership. Five mutations per
backend are rejected. The full render harness passes all 34 suites.

No native OpenGL, arbitrary object rotation, receiver metadata lifetime,
fragment lighting, dense-scene performance or final shadow-edge acceptance is
claimed here. See [the remaining integration contract](../../../design/sdf-receiver-geometry.md#fragment-integration-still-pending).
