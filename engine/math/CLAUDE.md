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

**World +z points DOWN on screen.** Increasing `z` increases `iso.y`, which
renders lower; "up" is `-z` (compare canvas_stress: its floor sits at a
LARGER z than the solids standing on it). Author voxel models accordingly —
a "head at +z" humanoid or an "apex at +z" pyramid renders upside-down.
Three independent demos have made exactly this mistake; check here first
when a model reads vertically inverted.

Helpers:

- `IRMath::pos3DtoPos2DIso(pos3D)` — the projection above.
- `IRMath::pos3DtoPos2DScreen(pos3D)` — scales the iso result and maps it to
  screen coordinates: it **unconditionally negates iso X** (`screen.x =
  -iso.x`, so increasing `iso.x` is screen-*left*; world `+x` → screen-right,
  world `+y` → screen-left) and applies the backend-dependent Y sign. The X
  negation is invisible in the equations above — never derive on-screen X
  direction from `pos3DtoPos2DIso` alone.
- `IRMath::pos3DtoDistance(pos3D)` — the depth scalar.
- `IRMath::isoDepthShift(pos, d)` — returns `pos + (d, d, d)`, which shifts
  depth without affecting the 2D projection. Used for stacked depth layers
  at the same apparent position.
- `IRMath::isoDepthAxisModel(rotation)` / `IRMath::isoDepthAlongAxis(pos, axis)`
  — the entity-rotated generalization of the depth scalar. A DETACHED entity
  rasters in model space, so its per-voxel occlusion projects onto
  `R⁻¹·(1,1,1)` (the model-frame depth axis) rather than the fixed world
  (1,1,1); at identity the axis is exactly (1,1,1) so it collapses to
  `pos3DtoDistance`. GPU mirror `isoDepthAlongAxis` in `ir_iso_common.{glsl,metal}`.
  See the design doc's entity-rotation carve-out (#1462).
- `IRMath::pos3DtoPos2DIsoYawed(worldPos, visualYaw)` — continuous-rotation
  iso reposition: `iso(R_z(−yaw)·world)` (1-DOF, smooth camera Z-yaw, #1308).
  GPU mirror in `ir_iso_common.{glsl,metal}`, kept CPU↔GPU bit-identical. See
  [`docs/design/per-axis-trixel-canvas-rotation.md`](../../docs/design/per-axis-trixel-canvas-rotation.md).
  (Its SO(3) companion `pos3DtoPos2DIsoRotated` — `iso(R·model)` for a detached
  entity's octahedral-snap residual, #1463 — was removed in #2194 once #1560
  retired its sole consumer, the forward-scatter composite.)

**Never inline these equations in system code.** Always call the helpers
so there's one place to fix a coordinate-system bug.

The (1,1,1) iso-depth axis is the load-bearing geometry behind the
closed-form `isoPixelToPos3D` inverse and every "sum of components =
depth" shortcut in picking / hitbox / gizmo / SDF cull. World-camera
rotation around any axis other than Z silently breaks those shortcuts;
see [`docs/design/iso-depth-axis-invariant.md`](../../docs/design/iso-depth-axis-invariant.md)
for the full consumer map and the cost of widening to SO(3).

## Rotation primitives (Z-yaw)

All camera-yaw and rotation math goes through this surface — never inline the
formulas. The convention is world→view = R\_z(−yaw), so view→world = R\_z(+yaw).

**Cardinal helpers** (snapped to multiples of π/2, `CardinalIndex` enum):

- `IRMath::rasterYawCardinalIndex(rasterYaw)` — snaps a rasterYaw to a
  `CardinalIndex` (0..3). CPU mirror of the shader `rasterYawCardinalIndex`.
- `IRMath::cardinalYawCosSin(cardinalIndex)` — returns `(cos, sin)` for the
  snapped angle. GPU mirror: `cardinalYawCosSin` in `ir_iso_common.glsl`.
- `IRMath::rotateCardinalZ(v, cardinalIndex)` — world→view rotation R\_z(−rasterYaw)
  at a cardinal snap. Integer and float overloads. GPU mirror: `rotateCardinalZ`.
- `IRMath::rotateCardinalZInv(v, cardinalIndex)` — view→world R\_z(+rasterYaw).
  GPU mirror: `rotateCardinalZInv`.

**Continuous helpers** (full continuous yaw):

- `IRMath::pos3DtoPos2DIsoYawed(worldPos, visualYaw)` — iso projection under
  a continuous Z-yaw: equivalent to `pos3DtoPos2DIso(R_z(−yaw) · worldPos)`.
  GPU mirror: `pos3DtoPos2DIsoYawed` in `ir_iso_common.glsl`.
- `IRMath::yawGrownIsoHalfExtent(halfExtent, cosYaw, sinYaw)` — conservative
  XY expansion of an AABB swept under yaw (for cull bounds). GPU mirror:
  `yawGrownIsoHalfExtent`.
- `IRMath::faceDeformationMatrix(face, residualYaw)` — 2×2 matrix that maps a
  face's un-yawed iso-pixel offset to its offset under a residual yaw
  (residualYaw ∈ [−π/4, π/4]). GPU mirror: `faceDeformationMatrix`.
- `IRMath::cameraMoveRelativeToYaw(isoDelta, visualYaw)` — returns the
  `cameraIso` delta that produces an on-screen shift equal to `isoDelta`
  in `CAMERA_CENTER` pivot mode (solves the 2×2 iso-projection system;
  identity at yaw=0; degenerate-guard at yaw=±2π/3). **CAMERA_CENTER only**
  — in `ORIGIN` mode use `isoDelta` directly.
  Use in pan systems (gate on pivot mode — ORIGIN mode passes `0` so the call
  collapses to the identity and doesn't regress on non-yaw paths):
  ```cpp
  const float panYaw =
      IRRender::getRotationPivotMode() == IRRender::RotationPivotMode::CAMERA_CENTER
          ? IRPrefab::Camera::getYaw()
          : 0.0f;
  camPos.pos_ = dragStart + cameraMoveRelativeToYaw(isoDelta, panYaw);
  ```

**Split helpers** (live in `engine/prefabs/irreden/render/camera.hpp`):

- `IRPrefab::Camera::computeYawSplit(visualYaw)` — returns `{rasterYaw, residualYaw}`:
  rasterYaw is the nearest π/2 cardinal; residualYaw is the leftover in [−π/4, π/4].
  Used by the trixel rasterizer and the per-axis canvas composite.
- `IRPrefab::Camera::getYaw()` / `getYawSplit()` — live camera yaw reads.

**Invariant.** At `visualYaw == 0` every helper collapses to its un-yawed
equivalent and produces byte-identical results to the pre-yaw path.

## Layout helpers

`layout.hpp` — grid, zigzag, circle, spiral, helix, and arc-path placement helpers. Each returns a `vec3` for index `i` in `[0, count)`, called in a loop. Gotcha: most helpers take a `PlaneIso` argument — `XY` vs `YZ` axis swap is the #1 source of wrong depth and is silent at compile time.

## Color

`color.hpp` — HSV conversion (`colorToColorHSV`/`colorHSVToColor`), interpolation (`lerpColor`/`lerpHSV`), and `applyHSVOffset`. Gotcha: `applyHSVOffset` operates on packed u8 RGBA — don't mix with float HSV in the same expression. `IRColors::k*` are canned constants; `color_palettes.hpp` has compile-time palette arrays.

## Physics

`physics.hpp` — ballistic helpers (`impulseForHeight`, `flightTimeForHeight`, `heightForImpulse`) and `isTunnelingSafe`. Use these; don't re-derive kinematics inline.

## Quaternions

Stored as `vec4(qx, qy, qz, qw)` — `.w` is the scalar (identity: `(0,0,0,1)`). `quatMul(a, b)` applies `b` then `a` — in joint chains: `quatMul(parentWorld, local)`. `rotateVectorByQuat(v, q)` is equivalent to `glm::rotate(quat, vec3)`.

## SQT transforms

`IRMath::SQT` (scale / quat-rotation / translation) is the math-layer value type mirroring the prefab `C_LocalTransform` / `C_WorldTransform` layout (which `engine/math/` can't name). `sqtToMat4(sqt)` builds `T·R·S`; `sqtCompose(parent, child)` reproduces `SYSTEM_PROPAGATE_TRANSFORM`'s composition so a chain folded in math matches the propagated `C_WorldTransform`; `sqtInverse(sqt)` is the analytic inverse (exact for uniform/rigid scale — the bind-pose domain). Used by `IRPrefab::Skeleton::skinMatrix`. **`sqtCompose`/`sqtInverse` are matrix-exact only for uniform scale** — non-uniform scale isn't closed under TRS; reach for `mat4` directly there.

## Random

`randomBool/Int/Float` and `randomVec<T>/randomColor` route through a
per-thread `std::mt19937` (`IRMath::threadRng()`), so concurrent calls
from IRJob workers do not race. Threads default to a seed of `0`;
`IRJob::JobManager` reseeds the main thread and each worker from its
enkiTS id so cross-run determinism holds for a fixed worker count.
Non-job threads can seed explicitly via `IRMath::seedThreadRng(seed)`.

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

