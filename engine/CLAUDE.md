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

## `SystemName` enum is authoritative

Every prefab system that uses the `System<NAME>::create()` template pattern
must have its `NAME` added to the `SystemName` enum in
`engine/system/include/irreden/ir_system_types.hpp`. If you add a new prefab
system under `engine/prefabs/**/systems/` and the enum value doesn't exist,
the specialization won't link. Add the enum value **first**.

## System-owned state lives on `System<N>` itself

Prefer the **member-on-`System<N>`** form for system-owned state — declare
the params as fields on the `System<N>` specialization, hooks (`tick`,
`beginTick`, `endTick`, `relationTick`) as named member functions, and
register via `IRSystem::registerSystem<N, Components...>("Name")`. The
explicit `Params` + `setSystemParams` form remains supported as an escape
hatch. See [`engine/system/CLAUDE.md`](system/CLAUDE.md) "Per-system
parameters" for both shapes and the migration story.

## Lua-driven ECS uses build-time codegen by default

CODEGEN is the production runtime for Lua-defined components and systems
— the build-time tool at `cmake/lua_codegen/` emits typed C++ struct +
`IRSystem::createSystem<...>` specialisations from `.lua` schemas, so
per-row work runs at native speed. EVAL (LuaJIT-backed sol2 dispatch via
`bindLuaDrivenEcs()`) is the dev-iteration opt-out: per-system via
`mode = "eval"` in the Lua source, or per-creation via
`-DIR_LUA_ECS_DEFAULT_MODE=EVAL` for a hot-reloadable build flavor. See
`engine/script/CLAUDE.md` "Per-system mode override + CODEGEN/EVAL
coexistence" for the DSL subset, the per-system override, the
CMake flag, and the hot-reload-only-in-EVAL contract.

## `createEntity` auto-attaches transform components

`IREntity::createEntity(...)` implicitly adds `C_PositionGlobal3D`,
`C_LocalTransform`, and `C_WorldTransform` to every entity, whether
you asked for them or not. The free function detects when the caller
supplies one of these types explicitly and skips the matching default
so the caller-provided value lands cleanly. Legacy systems read
`C_PositionGlobal3D` (updated by `GLOBAL_POSITION_3D` +
`APPLY_POSITION_OFFSET`); the canonical SQT path reads
`C_WorldTransform` (updated by `PROPAGATE_TRANSFORM` from
`C_LocalTransform` composed with the parent chain — see
`engine/prefabs/irreden/common/CLAUDE.md` "SQT transform pair +
propagation").

Per-frame additive offsets (idle bob, gizmo nudges, future per-frame
perturbations) travel through the modifier framework's vec3 fields:
`POSITION_OFFSET_3D` (folded into `C_PositionGlobal3D` by
`APPLY_POSITION_OFFSET`) and the SQT-side `TRANSFORM_TRANSLATION` /
`TRANSFORM_SCALE` (folded into `C_WorldTransform` by
`PROPAGATE_TRANSFORM`). Entities that don't push such offsets don't
need `C_Modifiers`.

## Manager globals

Each manager (`EntityManager`, `SystemManager`, `RenderManager`, etc.) is
stored as a global pointer (`g_entityManager`, `g_systemManager`, ...) set by
`World`'s constructor and cleared by its destructor. The `ir_<module>.hpp`
entry points wrap access via free functions (`IREntity::getEntityManager()`).
**Do not hold references or raw pointers to managers across frames outside
of World's lifetime.**
