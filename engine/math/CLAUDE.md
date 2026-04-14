# engine/math/ — IRMath

GLM aliases, isometric projection, layout helpers, easing, color, ballistic
physics, and random utilities. Everything here is namespace `IRMath::` and
dependency-free beyond GLM.

## GLM alias rule

Use `IRMath::vec2/3/4`, `ivec2/3/4`, `uvec2/3/4`, `u8vec2/3/4`, `mat2/3/4`
— never raw `glm::` names. The aliases are defined in `ir_math_types.hpp`
and re-exported through `ir_math.hpp`.

## Isometric projection — the equations

```
pos3D → isoPos (vec3 → vec2):
    iso.x = -x + y
    iso.y = -x - y + 2z

depth (vec3 → int):
    distance = x + y + z          (higher = further from camera)
```

Helpers:

- `IRMath::pos3DtoPos2DIso(pos3D)` — the projection above.
- `IRMath::pos3DtoPos2DScreen(pos3D)` — scales + sign-flips the iso result
  for screen coordinates (backend-dependent Y direction).
- `IRMath::pos3DtoDistance(pos3D)` — the depth scalar.
- `IRMath::isoDepthShift(pos, d)` — returns `pos + (d, d, d)`, which shifts
  depth without affecting the 2D projection. Used for stacked depth layers
  at the same apparent position.

**Never inline these equations in system code.** Always call the helpers
so there's one place to fix a coordinate-system bug.

## Layout helpers

Entity-positioning helpers that return `std::vector<vec3>` or similar:

- `layoutGridCentered(count, spacing, plane)` — grid of N positions on a
  `PlaneIso`.
- `layoutZigZagCentered(...)` — zig-zag variant.
- `layoutCircle(count, radius, plane)` — ring.
- `layoutHelix(count, radius, pitch, axis)` — vertical spiral.
- `layoutPathTangentArcs(...)` — along a parametric path with tangent-
  aligned rotation.

All take a `PlaneIso` selector. Double-check the plane axis — `XY` vs `YZ`
is the #1 bug source.

## Color

- `hsvToRgb` / `rgbToHsv` — convert between Color and ColorHSV.
- `lerpColor(a, b, t)` / `lerpHSV(a, b, t)` — interpolation.
- `applyHSVOffset(base, hsvDelta)` — shift hue/saturation/value on a
  packed RGBA color.
- `IRColors::kBlack/kWhite/kRed/...` — canned constants.
- `color_palettes.hpp` — load palette files from the asset path.
- `color.hpp` — sorting (`sortByHue`).

## Physics

`physics.hpp` — ballistic helpers for voxel-scale jumps:

- `impulseForHeight(targetHeight, gravity)` → initial v0.
- `flightTimeForHeight(...)`.
- `heightForImpulse(...)`.
- `isTunnelingSafe(posA, posB, radius)` — checks whether a discrete-step
  motion segment would pass through a solid cell.

Use these; don't re-derive the kinematics inline.

## Random

- `randomBool()`, `randomInt(min, max)`, `randomFloat(min, max)`.
- `randomVec<vec3>(min, max)`, `randomColor()`, `randomColor(palette)`.

Uses `std::rand`; deterministic if you seed.

## Gotchas

- **Don't mix 3D and iso coordinates without a helper.** Raw arithmetic on
  `vec3` and `vec2` iso positions is a common bug — always use the named
  conversion.
- **`PlaneIso` axis swaps are silent.** Swapping `XY` and `YZ` produces a
  valid render but the depth axis is wrong. Test at zoom 1 to catch it.
- **Voxel subdivisions matter.** In `SMOOTH` render mode, positions are
  multiplied by `subdivisions`, changing the effective step in iso space.
- **`IREasingFunctions` enum is not 1:1 with GLM.** Not all GLM easing
  functions are exposed; check `easing_functions.hpp` before assuming.

