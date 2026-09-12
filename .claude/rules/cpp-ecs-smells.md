---
paths:
  - "engine/**/*.{hpp,cpp,h,cc}"
  - "engine/prefabs/**/system_*.{hpp,cpp}"
  - "creations/**/*.{hpp,cpp,h,cc}"
  - "creations/**/*.lua"
---

> Sweep with `fleet-rules-sweep`, never `rg`/`Grep` rooted at `creations/`
> — see [`README.md`](README.md).

# ECS smell checklist

Diagnostic checklist for review, pre-commit passes, and performance audits.
Rule rationale lives in [`cpp-ecs.md`](cpp-ecs.md),
[`cpp-systems.md`](cpp-systems.md), and `engine/system/CLAUDE.md`.

## Per-entity tick violations

In any system `tick` function:

- **`getComponent<C_Foo>()` / `getComponentOptional<C_Foo>()` on the
  iterating entity.** Fix: add `C_Foo` to the template parameters. Auto-fix
  when the call is unconditional and the component small; flag when it is
  conditional or the component may be absent on some entities.
  - *Carve-out:* a render tick that must visit **all** canvases while only
    some carry an optional per-canvas component (`C_CanvasFogOfWar`,
    `C_CanvasSunShadow`, `C_CanvasLightVolume`) correctly uses
    `getComponentOptional` on the canvas — O(canvases), and the
    template-param fix would drop the canvases without it. Don't flag.
- **`IREntity::singleton<C_Foo>()` family inside `tick`/`endTick`** (not
  `beginTick`). Match `singleton(OrNull|Entity|EntityOrNull)?<` — the
  `…OrNull` spellings are the ones `engine/entity/CLAUDE.md` §"Pre-destroy
  hooks" directs authors to. The cost is a singleton-cache lookup on top of
  the full `getComponent` cost. Fix: resolve once in `beginTick`, cache the
  pointer, read the cache from `tick`/`endTick` — exemplar
  `engine/prefabs/irreden/common/systems/system_modifier_resolve_global.hpp`.
- **`createEntity` / `addComponent` / `removeComponent` / `removeEntity`
  mid-iteration** without the deferred variant.
- **Allocation in the tick body** — `new`, hot `push_back`, `std::string`
  construction, `std::map::operator[]` insertion. Fix: pre-size in
  `beginTick` or at component construction; reuse across frames.
- **A block meant for one specific entity without an entity guard.** It
  carries `entity == specificMember_ &&` in its condition; a comment is not
  a guard. Mirror the sibling blocks' guard.

## System registration

- **New prefab system not in `SystemName`**
  (`engine/system/include/irreden/system/ir_system_types.hpp`). Add a
  `SCREAMING_SNAKE_CASE` entry under the right group comment.
- **`SystemName` entry with a `SYSTEM_` prefix.** Entries are action-first
  (`DISPATCH_LUA_OVERLAP`, not `SYSTEM_DISPATCH_LUA_OVERLAP`).

## Tick-function signatures

Contract in [`cpp-systems.md` §"beginTick / endTick contract"](cpp-systems.md):
`functionBeginTick` / `functionEndTick` are `void()` (no `Archetype&`, no
component params), and both fire on an empty archetype — `endTick` indexing
`ids[]` needs a size guard.

## Component discipline

- **New component not `C_`-prefixed**, or public members without trailing `_`.
- **Component method calling `IREntity::getComponent` / `setComponent` /
  `createEntity` / `setParent` on a different entity** (tier-c per
  `engine/prefabs/CLAUDE.md` §"Component method rules") unless on that
  section's exceptions list; otherwise the logic moves to a system, builder,
  or prefab-scoped namespace.
- **`C_Position3D` read in a render-related system for visual placement** —
  rendered position is `C_PositionGlobal3D`, which `APPLY_POSITION_OFFSET`
  keeps folded with any modifier-driven offset.

## Lua-side ECS bindings

- **Bare `"C_..."` string in a Lua `components = { }` / `excludes = { }`
  table** — must be `IRComponent.C_Name`. Applies to creations calling
  `IRSystem.registerSystem`; bare strings for Lua-defined components (no
  `C_` prefix) are fine.
- **Hand-written `IRTime` / `SystemName` string keys in C++ binding code**
  (`t["NAME"] = ...`). Use `IR_BIND_TIME(NAME)` / `IR_BIND_SYS(NAME)`.
- **C++ binding code checking a Lua string against a fixed value set.**
  Expose the enum as a Lua integer table — [`cpp-lua-enums.md`](cpp-lua-enums.md).
- **A Lua `createEntityBatch*` call whose factory count differs from the
  C++ `registerCreateEntityBatchFunction<...>` arity.** The generated binding
  reads exactly N positional factories and Lua silently drops the rest, so
  an appended component never attaches and the system needing it never
  matches. Adding a factory means extending the C++ registration arity in
  the same change. The return is sol2 container userdata: iterate with `#` /
  `[i]`, never `ipairs` (the vendored LuaJIT has no 5.2 compat); a test stub
  for one of these bindings returns userdata too (`newproxy(true)` with
  `__index`/`__len`), not a plain table.
