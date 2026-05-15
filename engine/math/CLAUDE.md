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

Entity-positioning helpers — each returns a single `vec3` for one `index`.
Call in a loop from `0` to `count - 1` to place a group of entities:

- `layoutGridCentered(index, count, columns, spacingPrimary, spacingSecondary, plane, depth)` — rectangular grid.
- `layoutZigZagCentered(index, count, itemsPerZag, spacingPrimary, spacingSecondary, plane, depth)` — zig-zag variant.
- `layoutZigZagPath(index, count, itemsPerSegment, spacingPrimary, spacingSecondary, plane, depth)` — zig-zag along a path.
- `layoutCircle(index, count, radius, startAngleRad, plane, depth)` — ring.
- `layoutSquareSpiral(index, spacing, plane, depth)` — outward square spiral.
- `layoutHelix(index, count, radius, turns, heightSpan, axis)` — vertical spiral; `turns` is full rotations, `heightSpan` is total rise.
- `layoutPathTangentArcs(index, count, radius, blocksPerArc, zStep, axis, startAngleRad, invert)` — along a parametric arc path.

Helpers that take `PlaneIso` (most of them): double-check the plane axis —
`XY` vs `YZ` is the #1 bug source.

## Color

- `colorToColorHSV(Color) → ColorHSV` / `colorHSVToColor(ColorHSV) → Color` — convert between `Color` (u8 RGBA) and `ColorHSV` (float HSV).
- `hsvToRgb(vec3 hsv) → vec3` — raw float HSV → RGB (no `Color` wrapping).
- `lerpColor(a, b, t)` / `lerpHSV(a, b, t)` — interpolation.
- `applyHSVOffset(base, hsvDelta)` — shift hue/saturation/value on a
  packed RGBA color.
- `IRColors::kBlack/kWhite/kRed/...` — canned constants.
- `color_palettes.hpp` — compile-time palette arrays (no file I/O).
- `color.hpp` — sorting (`sortByHue`).

## Physics

`physics.hpp` — ballistic helpers for voxel-scale jumps:

- `impulseForHeight(targetHeight, gravity)` → initial v0.
- `flightTimeForHeight(...)`.
- `heightForImpulse(...)`.
- `isTunnelingSafe(maxVelocity, dt, colliderThicknessA, colliderThicknessB)` — returns true when both colliders are thick enough that a body moving at `maxVelocity` cannot pass through in a single timestep `dt`.

Use these; don't re-derive the kinematics inline.

## Quaternions

Quaternions are stored as `IRMath::vec4(qx, qy, qz, qw)` — the same
layout used by `IRComponents::Joint::rotation_` and
`IRAsset::RigJoint::rotation_` (identity is `vec4(0, 0, 0, 1)`, i.e.
`.w` is the scalar).

- `IRMath::quatMul(a, b) → vec4` — Hamilton product; in column-vector
  convention, rotates by `b` first then `a` (bone usage: `quatMul(parent_world, local)`).
- `IRMath::rotateVectorByQuat(v, q) → vec3` — rotate a vec3 by a
  unit quaternion. Same algebra as `glm::rotate(quat, vec3)`.

These are the helpers `IRPrefab::Rig::worldTransformForBindPoint`
uses to walk a joint chain when resolving `IREntity.bindPoint(entity, name)`.

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

