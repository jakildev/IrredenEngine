---
name: increase-complexity
description: >-
  Grows a demo or creation additively by scanning the engine's system catalog
  against what the creation already registers and proposing or applying new
  entity types, higher entity counts, animation, particles, lighting, or
  interactivity — never removing existing features. Use when the user says
  "increase complexity", "make more impressive", "add more systems", "make
  number go up", "make it fancier", "add more to this demo", "auto-grow this
  demo", or names a concrete addition such as "add particles", "add
  animation", "add lighting", or "add more entities".
---

# increase-complexity

Additive only: no existing system or entity is removed or restructured, and no
line outside the addition changes. Does not create demos (`create-creation`),
engine systems (`ecs-prefab-creator`), or optimise (`optimize` — note a
frame-rate drop in the report and let the user decide). In a gitignored private
creation, read its `CLAUDE.md` first and stop before any git operation — the
creation has its own git.

## 1. Target

Argument (`/increase-complexity shape_debug`), else the creation containing the
cwd (`creations/demos/<name>/`, `creations/editors/<name>/`), else one named by
the branch, else ask and offer the `creations/demos/` list.

## 2. Inventory

Read `main.cpp` / `main_lua.cpp` (system registration and `initEntities()`) and
`main.lua` (systems table, entity creation). Record the registered
`SystemName` set, the entity shapes and counts, and existing motion / animation
/ colour / interactivity, in one sentence.

## 3. Candidates

Diff `engine/system/include/irreden/system/ir_system_types.hpp` (grouped by its
comment headers) against the registered set. For each unused candidate read
`engine/prefabs/irreden/<domain>/systems/system_<snake_name>.hpp` for the
components it requires, its `create()` arguments, and its visual effect.

Priority by visual impact:

1. Motion / animation — `PERIODIC_IDLE`, `PERIODIC_IDLE_POSITION_OFFSET`,
   `AUTO_YAW_ROTATE`, `VELOCITY_3D`, `GRAVITY_3D`, `ANIMATION_COLOR`.
2. Particles — `PARTICLE_SPAWNER`, `SPAWN_GLOW`,
   `RENDER_STATELESS_PARTICLES_TO_TRIXEL`.
3. Lighting — extra light sources, fog variation.
4. More entities — scale the count in `initEntities()`.
5. Interactivity — `HITBOX_MOUSE_TEST`, `SPRING_PLATFORM`.

Skip systems whose prerequisites the demo lacks (`MIDI_*` without a MIDI
device, `CHUNK_RESIDENCY_*` in a single-chunk demo, GPU-compute systems
without the matching SSBO pipeline).

## 4. Propose

A numbered menu of 1–4 options, each one line with an estimated line count,
plus `all`:

```
Proposals for <demo-name>:
  1. Idle bounce — register PERIODIC_IDLE + PERIODIC_IDLE_POSITION_OFFSET
     on existing shape entities; shapes bob at a sine frequency. ~6 lines.
  2. Auto-rotation — register AUTO_YAW_ROTATE on the camera. 1 line.
  all — apply all of the above.
```

A concrete request ("add particles") or "all" / "just do it" skips the menu.

## 5. Apply

- **Systems** — append `IRSystem::createSystem<IRSystem::NAME>()` after the
  last call in the existing `registerPipeline()` block for that stage (UPDATE,
  RENDER, INPUT), matching indentation; add the header beside the other
  `// SYSTEMS` includes. Parameterised systems use their own
  `System<N>::create(arg)` — the header has the signature.
- **Components** a new system needs go on the existing `createEntity()` calls
  in `initEntities()` (header beside `// COMPONENTS`); never a new entity just
  to carry a component that belongs on an existing one.
- **New entity types** (emitter, glow source, trigger) — a new `createEntity()`
  after the existing block, with a one-line comment.
- **Entity count** — extend the loop bounds or add a second loop, 2–4× unless
  the user says otherwise.
- **Lua** — add system names to the `systems = {}` table and entity tables to
  the creation table in `main.lua`, preferring `IRSystem.SystemName.X` over
  strings.

## 6. Build and run

```
fleet-build --target <ExecutableName>
fleet-run --timeout 15 <ExecutableName>
```

Fix build errors minimally (include, argument type, component, pipeline stage);
after three failed attempts stop and report rather than refactor. Done when the
demo runs 15 s without a crash or error output.

## 7. Report

**Added** (systems, entity delta), **Before / after** counts, the build + run
command, and **Deferred** proposals with the reason (prerequisite missing, user
declined, build failure). Offer — do not run — a before/after capture with
`fleet-run <Name> --auto-screenshot 10` when the demo supports it.

## Quick reference: high-value pairs

System names are exact `SystemName` values; confirm component names in the
system header before use.

| Goal | Register | Add to entity |
|---|---|---|
| Idle bounce | `PERIODIC_IDLE`, `PERIODIC_IDLE_POSITION_OFFSET` | `C_PeriodicIdle`, `C_Modifiers` |
| Camera auto-rotation | `AUTO_YAW_ROTATE` (`System<AUTO_YAW_ROTATE>::create(rad)`) | none — iterates the `C_Camera` entity |
| Velocity motion | `VELOCITY_3D`, `PROPAGATE_TRANSFORM` | `C_Velocity3D` |
| Gravity | `GRAVITY_3D` | `C_Gravity3D`, `C_Velocity3D` |
| Colour animation | `ANIMATION_COLOR` | `C_ActionAnimation`, `C_AnimColorState`, `C_VoxelSetNew` (see header) |
| Mouse hover glow | `HITBOX_MOUSE_TEST`, `SPAWN_GLOW` | `C_HitBox2D` |

Idle-bounce order: `PERIODIC_IDLE` → `PERIODIC_IDLE_POSITION_OFFSET` → modifier
resolver → `PROPAGATE_TRANSFORM` (already registered in most demos).
