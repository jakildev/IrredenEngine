# engine/prefabs/irreden/update/ — physics, animation, particles

Everything that mutates per UPDATE tick: velocity, gravity, drag,
animation, particles, lifetimes, idle/oscillation. Systems here all run
in the `UPDATE` pipeline unless explicitly noted.

## Key components

- `component_velocity_3d.hpp` — `C_Velocity3D`, vec3 in **blocks per
  second**, not pixels.
- `component_velocity_drag.hpp` — `C_VelocityDrag`, configurable drag,
  hover damping, post-hover reset.
- `component_gravity_3d.hpp` — `C_Gravity3D`, direction + magnitude.
- `component_animation_clip.hpp` — `C_AnimationClip`, an array of
  animation phases.
- `component_action_animation.hpp` — `C_ActionAnimation`, playback
  state for the above.
- `component_particle_burst.hpp` — one-shot particle spawn config
  (direction override, face bias, gravity mode).
- `component_periodic_idle.hpp` — `C_PeriodicIdle`, sine/easing
  position offset for drift.
- `component_lifetime.hpp` — `C_Lifetime`, integer countdown; destroyed
  when ≤ 0.

## Key systems (all UPDATE pipeline)

- `system_velocity.hpp` — `VELOCITY_3D`. Applies `velocity *
  deltaTime(UPDATE)` to position.
- `system_gravity_3d.hpp` — `GRAVITY_3D`. Accumulates gravity into
  velocity.
- `system_velocity_drag.hpp` — `VELOCITY_DRAG`. Applies drag, hover
  damping, post-hover reset.
- `system_animation_color.hpp` — `ANIMATION_COLOR`. Plays an
  `AnimationClip` on a voxel set via HSV blending.
- `system_particle_spawner.hpp` — spawns particles at configured rate
  via `entity_voxel_particle` builder.
- `system_lifetime.hpp` — decrements `C_Lifetime`; destroys entities at
  zero.
- `system_periodic_idle.hpp` — drives `C_PositionOffset3D` via sine /
  easing for drift/hover.

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
