# engine/math/ — IRMath

GLM aliases, isometric projection, layout helpers, easing, color, ballistic
physics, and random utilities. Everything here is namespace `IRMath::` and
dependency-free beyond GLM.

## GLM alias rule

`IRMath::` aliases only — never raw `glm::`. Full substitution table and rationale in `.claude/rules/cpp-math.md`.

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

The (1,1,1) iso-depth axis is the load-bearing geometry behind the
closed-form `isoPixelToPos3D` inverse and every "sum of components =
depth" shortcut in picking / hitbox / gizmo / SDF cull. World-camera
rotation around any axis other than Z silently breaks those shortcuts;
see [`docs/design/iso-depth-axis-invariant.md`](../../docs/design/iso-depth-axis-invariant.md)
for the full consumer map and the cost of widening to SO(3).

## Layout helpers

`layout.hpp` — grid, zigzag, circle, spiral, helix, and arc-path placement helpers. Each returns a `vec3` for index `i` in `[0, count)`, called in a loop. Gotcha: most helpers take a `PlaneIso` argument — `XY` vs `YZ` axis swap is the #1 source of wrong depth and is silent at compile time.

## Color

`color.hpp` — HSV conversion (`colorToColorHSV`/`colorHSVToColor`), interpolation (`lerpColor`/`lerpHSV`), and `applyHSVOffset`. Gotcha: `applyHSVOffset` operates on packed u8 RGBA — don't mix with float HSV in the same expression. `IRColors::k*` are canned constants; `color_palettes.hpp` has compile-time palette arrays.

## Physics

`physics.hpp` — ballistic helpers (`impulseForHeight`, `flightTimeForHeight`, `heightForImpulse`) and `isTunnelingSafe`. Use these; don't re-derive kinematics inline.

## Quaternions

Stored as `vec4(qx, qy, qz, qw)` — `.w` is the scalar (identity: `(0,0,0,1)`). `quatMul(a, b)` applies `b` then `a` — in joint chains: `quatMul(parentWorld, local)`. `rotateVectorByQuat(v, q)` is equivalent to `glm::rotate(quat, vec3)`.

## Random

`randomBool/Int/Float` and `randomVec<T>/randomColor` — uses `std::rand`; seed for determinism.

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

