# engine/prefabs/irreden/update/ — physics, animation, particles

Everything that mutates per UPDATE tick: velocity, gravity, drag,
animation, particles, lifetimes, idle/oscillation. Systems here all run
in the `UPDATE` pipeline unless explicitly noted.

## Key components

- `C_Velocity3D` — vec3 in **blocks per second**, not pixels.
- `C_VelocityDrag` — configurable drag, hover damping, post-hover reset.
- `C_Gravity3D` — direction + magnitude.
- `C_AnimationClip` — an array of animation phases.
- `C_ActionAnimation` — playback state for the above.
- `C_ParticleBurst` — one-shot particle spawn config (direction override,
  face bias, gravity mode).
- `C_PeriodicIdle` — sine/easing position offset for drift.
- `C_Lifetime` — integer countdown; destroyed when ≤ 0.
- `C_RotationTarget` — drives local rotation from a normalized control input
  (axis + min/max angle + input value/range + optional easing curve). The
  rotation sibling of `C_GotoEasing3D` (which eases local *position*):
  input-driven, not time-driven — a creation updates `input_` each frame from
  a CC, slider, or any [0,1] signal, and the entity holds the last mapped
  angle when the input is unchanged. Owns the entity's local rotation (writes
  it absolutely); don't pair with `C_AutoSpin` on the same entity. Lua-bindable
  via `component_rotation_target_lua.hpp` — the scalar fields (`input_`,
  `minAngle_`/`maxAngle_`, `inputMin_`/`inputMax_`) are read-write member
  pointers, so a Lua automation lane drives `input_` straight through the
  column view; `axis_` + easing stay constructor-only config.

## Key systems (all UPDATE pipeline)

- `VELOCITY_3D` — applies `velocity * deltaTime(UPDATE)` to position.
- `GRAVITY_3D` — accumulates gravity into velocity.
- `VELOCITY_DRAG` — applies drag, hover damping, post-hover reset.
- `ANIMATION_COLOR` — plays an `AnimationClip` on a voxel set via HSV blending.
- `PARTICLE_SPAWNER` — spawns particles at configured rate via the voxel
  particle entity builder.
- `LIFETIME` — decrements `C_Lifetime`; destroys entities at zero.
- `PERIODIC_IDLE` — advances per-entity `C_PeriodicIdle` timer.
- `PERIODIC_IDLE_POSITION_OFFSET` — upserts the resolved bob value as a
  vec3 ADD modifier on the entity's `C_Modifiers` each tick via
  `upsertBySourceInPlace` (under the `TRANSFORM_TRANSLATION` field).
  Slot key is `(kNullEntity, TRANSFORM_TRANSLATION, ADD)` — one entry per
  bob-eligible entity, updated in place. No `ticksRemaining_` countdown;
  no `MODIFIER_DECAY` dependency. `PROPAGATE_TRANSFORM` later folds the
  resolved value into `C_WorldTransform.translation_` per the SQT
  formula.
- `AUTO_SPIN_LOCAL_TRANSFORM` — composes `quatAxisAngle(axis, rate)` on
  the left of `C_LocalTransform.rotation_` each tick for entities carrying
  `C_AutoSpin` (axis + radians-per-frame). Place before
  `PROPAGATE_TRANSFORM` so the new rotation propagates to `C_WorldTransform`
  in the same tick — and, for GRID-mode entities, drives
  `REBUILD_GRID_VOXELS` re-rasterization downstream.
- `ROTATION_TARGET_LOCAL_TRANSFORM` — maps each `C_RotationTarget`'s
  normalized `input_` (over `[inputMin_, inputMax_]`, through the easing
  curve) onto `[minAngle_, maxAngle_]` and writes it as an **absolute**
  `quatAxisAngle(axis, angle)` into `C_LocalTransform.rotation_`. The
  rotation sibling of `GOTO_3D` (eased local translation). Same ordering
  contract as `AUTO_SPIN_LOCAL_TRANSFORM`: place before `PROPAGATE_TRANSFORM`.
  A zero `axis_` is a no-op (guards the NaN quaternion).
- `PROPAGATE_TRANSFORM` — walks the `CHILD_OF` parent chain in
  topological order and writes `C_WorldTransform` from each entity's
  `C_LocalTransform` composed with the parent chain. Modifier-resolved
  `TRANSFORM_TRANSLATION` / `TRANSFORM_SCALE` (vec3) fold into the
  world transform per the SQT formula in
  `engine/prefabs/irreden/common/CLAUDE.md` "SQT transform pair +
  propagation". Place after the modifier resolver pipeline and before
  any consumer (render, gizmo, physics) that reads
  `C_WorldTransform`. T-378 partitions the archetype set into
  per-depth levels (cached across frames; the partition rebuilds only
  on archetype-graph changes); within each level composition fans out
  to IRJob workers. #1804 made the per-level dispatch split by **row
  range**, not just by archetype: a level is flattened into row-chunks
  so a single dominant archetype (e.g. a 262K-entity grid in one node)
  splits across workers instead of composing serially on one. #1900
  hoisted that fan-out / chunk-sizing / serial-fallback policy out of
  the system and into `IRJob::parallelChunks` (tuned via
  `IRJob::ParallelTuning`, whose defaults *are* this system's original
  hand-tuned constants); the system now just mirrors each level's
  per-node row counts into a reused buffer and hands them to the
  planner. Output is bit-identical to the serial path (disjoint rows,
  each entity writes only its own `C_WorldTransform[i]`). Cost is O(N)
  total compose work with O(passes × archetypes) topology bookkeeping,
  where passes is bounded by the deepest parent chain. See
  `docs/perf-reports/threading_propagate_transform.md`.

## Commands

- `command_toggle_periodic_idle_pause.hpp` — pause/resume drift motion
  globally.

## Free functions

- None in this domain currently. The domain-root free-function pattern (a
  set of pure functions, not a component/system/command, living in the
  domain root) is exemplified by
  `engine/prefabs/irreden/spatial/spatial_query.hpp`.

## Collision overlap events (#1817)

AABB collider overlap detection feeding both a per-entity contact event and a
Lua-facing overlap callback surface. Three systems + one buffer component:

- `COLLISION_NOTE_PLATFORM` — broad+narrow AABB overlap scan over the
  `C_WorldTransform + C_ColliderIso3DAABB + C_CollisionLayer + C_ContactEvent`
  archetype. Two additive outputs: (1) per-entity `C_ContactEvent`
  (first-overlapping-partner-wins; consumed by `system_spring_platform` /
  `system_contact_*`), and (2) a layer-stamped `ContactPair` vector in the
  `C_CollisionPairBuffer` singleton — every confirmed overlap, emitted once per
  unordered pair (canonical `a_ < b_`). The producer owns the buffer lifecycle:
  its `beginTick` clears the singleton, so a pipeline that runs it without the
  dispatch consumer still clears every frame.
- `COLLISION_DISPATCH_LUA_OVERLAP` — iterates the `C_CollisionPairBuffer`
  singleton, diffs this frame's pairs against last frame's, and fires the
  registered Lua enter/exit handlers (pair-level enter/exit state machine —
  handles an entity touching several others in one frame). Handlers are keyed by
  the unordered collision-layer pair and stored as plain `std::function`, so
  this prefab header stays sol-free; the `IRCollision` Lua binding
  (`engine/script/.../lua_collision_bindings.hpp`) does the sol wrapping. Place
  it AFTER `COLLISION_NOTE_PLATFORM` in the UPDATE pipeline.
- `COLLISION_EVENT_CLEAR` — resets the per-entity `C_ContactEvent` enter/stay/
  exit flags each frame. Unrelated to the pair buffer (which the producer
  clears).
- `C_CollisionPairBuffer` (`component_collision_pair_buffer.hpp`) — singleton
  component holding `std::vector<ContactPair>`; the cross-system handoff from
  producer to dispatch. Cleared+refilled each frame (capacity retained).

The Lua surface (`IRCollision.onOverlapEnter` / `onOverlapExit`, the `Layer`
table, pipeline-order requirement, enter/exit semantics) is documented in
`engine/script/CLAUDE.md` §"Collision overlap events". `C_CollisionLayer`'s
`layer_` is a single bitmask bit per entity; `collidesWithMask_` gates which
other layers it pairs with (checked both directions). `COLLISION_NOTE_PLATFORM`
is misnamed (music-demo origin) but IS the general AABB overlap detector.

## Gotchas

- **Velocity is in blocks/second.** `system_velocity.hpp` multiplies by
  `deltaTime(UPDATE)` (wall-clock dt, not the fixed step). Use sensible
  magnitudes or drift looks jittery.
- **Order matters between gravity, drag, velocity.** The canonical
  pipeline order is `GRAVITY_3D → VELOCITY_DRAG → VELOCITY_3D` so drag
  sees gravity contribution. If a creation inserts a custom system
  between these, check the order.
- **`C_VelocityDrag` hover state is fragile.** The "post-hover reset"
  field zeros velocity once per hover cycle. Don't poke it mid-flight or
  the entity twitches.
- **Particle spawner allocates voxels.** `entity_voxel_particle` calls
  `allocateVoxels()` per spawn. At high spawn rates, the voxel pool
  can exhaust. Check pool capacity before cranking the rate.
- **Lifetime + ephemeral pattern.** The common idiom
  `createEntity({..., C_Lifetime{1}})` creates a one-frame entity,
  picked up by the next tick's systems, then destroyed. Use this for
  events like "MIDI note out" or "mouse scroll".
