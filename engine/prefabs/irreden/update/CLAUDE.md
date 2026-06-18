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
- `C_OverlapContactBatch` (#1817) — **singleton** component holding this
  frame's confirmed overlap pairs (`std::vector<ContactPair>`, each with both
  entity ids + both collision layers + a normal). `COLLISION_NOTE_PLATFORM`
  appends; `DISPATCH_LUA_OVERLAP` reads then clears. It is the producer's
  **opt-in switch**: emission only happens when the singleton exists, and it is
  created by `DISPATCH_LUA_OVERLAP::create()` — so creations that only
  want the per-entity `C_ContactEvent` never pay for pair emission. Reached
  anywhere via `IREntity::singleton<C_OverlapContactBatch>()` /
  `singletonOrNull<...>()`, which is how the producer and consumer share the
  vector without a cross-system SystemId handoff. `ContactPair` is co-located in
  `component_overlap_contact_batch.hpp`.

## Key systems (all UPDATE pipeline)

- `VELOCITY_3D` — applies `velocity * deltaTime(UPDATE)` to position.
- `GRAVITY_3D` — accumulates gravity into velocity.
- `VELOCITY_DRAG` — applies drag, hover damping, post-hover reset.
- `ANIMATION_COLOR` — plays an `AnimationClip` on a voxel set via HSV blending.
- `PARTICLE_SPAWNER` — spawns particles at configured rate via the voxel
  particle entity builder.
- `LIFETIME` — decrements `C_Lifetime`; destroys entities at zero.
- `COLLISION_NOTE_PLATFORM` (collision domain) — broad+narrow-phase AABB
  overlap detector. Marks per-entity `C_ContactEvent` (single contact) AND,
  when the `C_OverlapContactBatch` singleton exists, appends every overlapping
  pair (canonical `a_ < b_`, both layers stamped) to it. The pair scan drops
  the first-contact `break` so all overlaps are emitted (D3); the
  `C_ContactEvent` write stays first-contact-only, so existing consumers are
  byte-identical and non-opted-in creations keep the fast break path.
- `DISPATCH_LUA_OVERLAP` (#1817) — turns `C_OverlapContactBatch` into
  Lua `IRCollision.onOverlapEnter/onOverlapExit` callbacks. Owns the
  pair-level enter/exit state machine (prev/cur pair maps, reused across
  frames) — accurate when one entity touches several others, unlike the
  per-entity `C_ContactEvent`. Holds the `sol::protected_function` handler
  registry keyed by layer pair (session lifetime, freed before the sol::state
  per `world.hpp`'s manager-block ordering). **Register after
  `COLLISION_NOTE_PLATFORM`** in the pipeline; it consumes + clears the batch
  in its own tick (the `IRCollision.*` Lua surface lives in
  `engine/script/CLAUDE.md`; the runnable proof is
  `creations/demos/collision_overlap_demo`).
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
  to IRJob workers via `IRJob::parallelFor`. #1804 made the per-level
  dispatch split by **row range**, not just by archetype: a level is
  flattened into row-chunks so a single dominant archetype (e.g. a
  262K-entity grid in one node) splits across workers instead of
  composing serially on one. Output is bit-identical to the serial
  path (disjoint rows, each entity writes only its own
  `C_WorldTransform[i]`). Cost is O(N) total compose work with
  O(passes × archetypes) topology bookkeeping, where passes is bounded
  by the deepest parent chain. See
  `docs/perf-reports/threading_propagate_transform.md`.

## Commands

- `command_toggle_periodic_idle_pause.hpp` — pause/resume drift motion
  globally.

## Free functions

- None in this domain currently. The domain-root free-function pattern (a
  set of pure functions, not a component/system/command, living in the
  domain root) is exemplified by
  `engine/prefabs/irreden/spatial/spatial_query.hpp`.

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
