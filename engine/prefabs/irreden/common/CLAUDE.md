# engine/prefabs/irreden/common/

Foundation prefab types for transforms, identity, tags, simulation time, and
generic modifiers. Simulation writers live in `../update/`; this directory
owns data types and header-only service APIs.

## SQT transform pair + propagation

`IREntity::createEntity(...)` always attaches `C_LocalTransform` and
`C_WorldTransform`, unless the caller supplies that same type. Never add a
second copy after creation: duplicate component columns leave one value stale.

`C_LocalTransform` is relative to the entity's `CHILD_OF` parent;
`C_WorldTransform` is the resolved world transform. Both use SQT and the
quaternion layout `IRMath::vec4(qx, qy, qz, qw)`. Identity is
`vec4(0, 0, 0, 1)`; quaternion algebra follows
[`engine/math/CLAUDE.md`](../../../math/CLAUDE.md) § "Quaternions".

Register `PROPAGATE_TRANSFORM` after modifier resolution and before every
consumer of `C_WorldTransform`. It processes parents before children and
composes:

```
world.scale       = parent.scale * local.scale * modifier_scale
world.rotation    = quatMul(parent.rotation, local.rotation)
world.translation = parent.translation
                  + rotateVectorByQuat(parent.scale * local.translation,
                                       parent.rotation)
                  + modifier_translation
```

A missing parent transform means identity. A relation change after propagation
takes effect next frame. Any RENDER-phase writer of `C_LocalTransform` must
also update `C_WorldTransform`, because propagation has already run.

`C_LocalTransform::unbounded_` permits sub-trixel translation only with
`RotationMode::DETACHED`; GRID entities still snap to world cells.

`C_RotationMode` selects one of these representations:

- `GRID`: shared voxel pool and grid-quantized rotation.
- `DETACHED`: a private `C_EntityCanvas`; voxel emit bakes full rotation.
- `DETACHED_REVOXELIZE`: a private pool rebuilt at rotated cell positions.

`IRPrefab::Prefab::spawnPrefab` attaches the mode. Runtime changes go through
`IRPrefab::RotationMode::setMode`; never mutate the component directly.
`IRPrefab::RotationMode::ownsEntityCanvas` is authoritative for canvas
lifecycle. Adding a mode also requires classifying it explicitly in
`PROPAGATE_CANVAS_ROTATION`. Non-prefab entities without `C_RotationMode` are
GRID; creations must register `REBUILD_GRID_VOXELS_IMPLICIT` alongside
`REBUILD_GRID_VOXELS`.

`C_ChunkMembership` is opt-in metadata attached by world-streaming migration,
not by `createEntity`. Its contract is
[`world-streaming.md`](../../../../docs/design/world-streaming.md).

## Simulation-clock contract

The ECS simulation clock pauses and scales independently of the always-running
engine clock. A creation using it must instantiate the `C_SimClock` singleton
and register these UPDATE systems in order:

1. `SIM_CLOCK_ADVANCE`
2. `CYCLE_BOUNDARY_DETECT`
3. `TIMER_FIRE`

`C_Cycle` primes silently when created mid-simulation. Its embedded,
self-clearing boundary event fires on every configured segment crossing;
without breakpoints it fires only on period wrap. `segmentIndex_` always
describes the current segment.

`C_Timer` is one-shot when `intervalTicks_ == 0` and recurring otherwise.
`C_Stopwatch` has no system: `IRSim::stopwatchElapsed` computes elapsed time,
while pause, resume, and reset snapshot the simulation tick.

Use the header-only `IRSim::` service in [`sim_clock.hpp`](sim_clock.hpp) for
clock control and name-keyed cycle, timer, and stopwatch creation and queries.
The Lua surface is `IRSim`. Day-specific behavior belongs in the creation.

## Modifier framework

The detailed design and API discriminators live in
[`modifiers.md`](../../../../docs/design/modifiers.md). Register fields during
initialization, then call `IRPrefab::Modifier::registerResolverPipeline()`
once and splice its returned systems into UPDATE order.

- `registerField`, `registerFieldVec3`, and `registerFieldQuat` create distinct
  typed IDs. A push with the wrong value type is rejected without changing
  resolved storage.
- `push` and `pushGlobal` create transient records; `upsertBySource` creates a
  persistent writer-owned slot keyed by `(source, field, kind)`.
- A system tick that already holds `C_Modifiers&` uses
  `upsertBySourceInPlace`; do not perform an entity lookup for the same row.
- Destroying a source automatically invokes `removeBySource` before its ID can
  recycle. Call it directly when the source persists but its effect ends.
- `applyToField` and the resolver use the same evaluator.
- `applyVec3ModifierTo<TargetComponent, Member>` is only for an additive vec3
  channel with one consumer and no global-modifier requirement. Other channels
  use the structured resolver and `C_ResolvedFields`.

Scalar and vec3 composition selects the latest `OVERRIDE` across globals then
entity records, discards everything before it, applies `ADD`, `MULTIPLY`, and
`SET` in push order, then applies all clamps. Vec3 operations are componentwise.

Quaternion `MULTIPLY` left-multiplies (`modifier * value`); `OVERRIDE` and
`SET` follow scalar ordering. `ADD` and clamp kinds are invalid. Normalize once
after composition only when a modifier changed the value. Lambda modifiers are
scalar-only.

Keep `Modifier`, `ModifierVec3`, and `ModifierQuat` trivially copyable. Stateful
or string-bearing behavior belongs outside `C_Modifiers`. Global exemption is
implemented by include/exclude archetype routing, not per-row branching.

### Open follow-ups (runtime gaps)

Current intentional gaps are tracked in
[`modifier-runtime-gaps.md`](../../../../.fleet/status/modifier-runtime-gaps.md).

## Command-suite contract

`command_suite_registry.hpp` is the data-only definition of the camera and
capture default bindings; do not include command bodies there. Registration
lives in `command_suite_camera.hpp` and `command_suite_capture.hpp` and accepts
`BindingOverrides` for omission or remapping. In-tree callers use the prefab
wrapper, such as `IRPrefab::Camera::registerStandardKeyboardCommands`, rather
than copying a suite or calling its bare registration function. See
[`engine/command/CLAUDE.md`](../../../command/CLAUDE.md).

## Checks

- `fleet-build --target header-checks`
- `python3 scripts/lint_instruction_size.py`
- `python3 scripts/lint_comment_refs.py`
- `fleet-run IRModifierDemo` for modifier behavior
