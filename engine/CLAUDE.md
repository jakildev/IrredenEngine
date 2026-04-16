# engine/

Core engine static libraries. Everything here is shared by every creation.

## Module include discipline

- `engine/include/irreden/ir_engine.hpp` is the **single top-level entry
  point** for creations. It constructs the `World` and drives init.
- Each module also exposes its own `ir_*.hpp` header at
  `engine/<module>/include/irreden/ir_<module>.hpp`.
- **Creations must only include `ir_*.hpp` entry points**, never internal
  module headers. If you're tempted to include `engine/render/include/
  irreden/render/shader.hpp` from a creation, the API is wrong — either
  re-expose what you need through `ir_render.hpp` or flag it in a PR.
- Inside the engine itself, modules may freely include each other's
  internal headers, but keep dependency direction clear: low-level
  modules (`common/`, `math/`, `profile/`) do not depend on high-level
  ones (`render/`, `world/`).

## Layer map (bottom-up)

```
common/   — shared primitives (ir_constants, ir_platform)
math/     — GLM aliases, iso projection, easing, color, physics
profile/  — easy_profiler + spdlog logging wrappers
time/     — TimeManager + EventProfiler (fixed-step loop)

entity/   — IRECS archetype store, EntityManager, Archetype, relations
system/   — SystemManager, pipeline scheduler, SystemName enum
command/  — CommandManager, input-triggered action binding

input/    — InputManager, GLFW keyboard/mouse/gamepad polling
window/   — IRGLFWWindow, OpenGL context bring-up
render/   — RenderManager, RenderingResourceManager, trixel pipeline, shaders
audio/    — AudioManager, MIDI in/out (RtMidi, RtAudio)
video/    — VideoRecorder, ffmpeg encoder, screenshot path
script/   — LuaScript, sol2 bindings, kHasLuaBinding<T> traits

asset/    — trixel texture + SDF load/save
world/    — World class, owns all managers above

prefabs/  — header-only C_* / System<> / Command<> definitions, grouped
            by domain: common/ update/ voxel/ input/ render/ audio/ video/
```

Read the `CLAUDE.md` inside each subdirectory for the module-specific story.

## `SystemName` enum is authoritative

Every prefab system that uses the `System<NAME>::create()` template pattern
must have its `NAME` added to the `SystemName` enum in
`engine/system/include/irreden/ir_system_types.hpp`. If you add a new prefab
system under `engine/prefabs/**/systems/` and the enum value doesn't exist,
the specialization won't link. Add the enum value **first**.

## `createEntity` always adds position components

`IREntity::createEntity(...)` implicitly adds `C_PositionGlobal3D` and
`C_PositionOffset3D` to every entity, whether you asked for them or not. The
rendered position is `global + offset`. You cannot opt out. Systems that
want an entity's actual draw position should read `C_PositionGlobal3D +
C_PositionOffset3D`, not `C_Position3D`.

## Manager globals

Each manager (`EntityManager`, `SystemManager`, `RenderManager`, etc.) is
stored as a global pointer (`g_entityManager`, `g_systemManager`, ...) set by
`World`'s constructor and cleared by its destructor. The `ir_<module>.hpp`
entry points wrap access via free functions (`IREntity::getEntityManager()`).
**Do not hold references or raw pointers to managers across frames outside
of World's lifetime.**
