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
  `upsertBySourceInPlace` (under the `POSITION_OFFSET_3D` field).
  Slot key is `(entity, POSITION_OFFSET_3D, ADD)` — one entry per
  bob-eligible entity, updated in place. No `ticksRemaining_` countdown;
  no `MODIFIER_DECAY` dependency. `APPLY_POSITION_OFFSET` later folds the
  composed value into `C_PositionGlobal3D`.
- `PROPAGATE_TRANSFORM` — walks the `CHILD_OF` parent chain in
  topological order and writes `C_WorldTransform` from each entity's
  `C_LocalTransform` composed with the parent chain. Modifier-resolved
  `TRANSFORM_TRANSLATION` / `TRANSFORM_SCALE` (vec3) fold into the
  world transform per the SQT formula in
  `engine/prefabs/irreden/common/CLAUDE.md` "SQT transform pair +
  propagation". Place after the modifier resolver pipeline and before
  any consumer (render, gizmo, physics) that reads
  `C_WorldTransform`. Cost is O(N + passes × archetypes) where passes
  is bounded by the deepest parent chain.

## Commands

- `command_toggle_periodic_idle_pause.hpp` — pause/resume drift motion
  globally.

## Free functions

- `engine/prefabs/irreden/update/nav_query.hpp` — grid navigation helper
  functions (not a system or component). Lives in the domain root
  because it's a set of pure functions.

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
