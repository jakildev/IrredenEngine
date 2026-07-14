---
paths:
  - "engine/**/*.{hpp,cpp,h,cc}"
  - "engine/prefabs/**/system_*.{hpp,cpp}"
  - "creations/**/*.{hpp,cpp,h,cc}"
  - "creations/**/*.lua"
---

# ECS smell checklist

Diagnostic checklist for catching common ECS pattern violations. Use during
code review, pre-commit passes, and performance audits. Full rule rationale
lives in [`.claude/rules/cpp-ecs.md`](cpp-ecs.md) and `engine/system/CLAUDE.md`.

## Per-entity tick violations

In any system `tick` function:

- **`getComponent<C_Foo>()` or `getComponentOptional<C_Foo>()`** on the
  iterating entity. Fix: add `C_Foo` to the system's template parameters so
  it arrives via dense-column iteration. Auto-fix when the call is
  unconditional and the component is small; flag when the call is conditional
  or the component may not exist on every entity in the archetype.
  - *Carve-out — per-canvas `getComponentOptional` in a canvas-iterating
    tick.* A render tick that must visit **all** canvas entities while only
    **some** carry an optional per-canvas component (`C_CanvasFogOfWar`,
    `C_CanvasSunShadow`, `C_CanvasLightVolume`) correctly uses
    `getComponentOptional` on the iterating canvas — it is O(canvases), not
    the O(voxels) per-voxel footgun, and the template-param fix does **not**
    apply (it would restrict iteration to canvases that *have* the component,
    dropping the rest). This is the accepted canvas-iteration pattern; don't
    flag it.

- **`createEntity`, `addComponent`, `removeComponent`, or `removeEntity`**
  mid-iteration without the deferred variant. Fix: use
  `IREntity::deferredCreate` / `deferredSetComponent` etc.; let
  `flushStructuralChanges` run at pipeline end.

- **Allocation in the tick body** -- `new`, `std::vector::push_back` on a hot
  vector, `std::string` construction/concatenation, `std::map::operator[]`
  insertion. Fix: pre-size in `beginTick` (or at component construction for
  member buffers) and reuse across frames.

- **Tick block scoped to one specific entity without an entity guard.** In a
  system that iterates multiple entities, a block meant for only one of them
  (a particular canvas, a singleton-ish entity) must carry
  `entity == specificMember_ &&` in its condition -- a comment saying
  "requires entity X" is not a guard. Mirror the guard the sibling blocks in
  the same tick already use.

## System registration

- **New prefab system not added to `SystemName`** in
  `engine/system/include/irreden/system/ir_system_types.hpp`. Fix: add a
  `SCREAMING_SNAKE_CASE` entry under the appropriate group comment.

- **`SystemName` entry carries a `SYSTEM_` prefix.** Entries are action-first
  with no `SYSTEM_` prefix (`DISPATCH_LUA_OVERLAP`, not
  `SYSTEM_DISPATCH_LUA_OVERLAP`) — every existing entry follows this shape.
  A new enum line whose first token is `SYSTEM_` is the smell.

## Tick-function signatures

- **`functionBeginTick` / `functionEndTick` with `Archetype&` or component
  parameters** -- they must be `void()`. Entity data is only valid inside the
  per-entity `tick`; `begin`/`endTick` are for once-per-frame setup/flush.

- **`endTick` reads `ids[0]` or indexes `ids` without a `ids.size() == 0`
  guard** -- `begin`/`endTick` fire even when the archetype is empty.

## Component discipline

- **New component not `C_`-prefixed**, or public members without trailing `_`.

- **Component method calls `IREntity::getComponent` / `setComponent` /
  `createEntity` / `setParent` on a different entity** (tier-c violation per
  `engine/prefabs/CLAUDE.md`). Confirm the method is on the documented
  exceptions list in `cpp-ecs.md`; otherwise move logic to a system, builder,
  or prefab-scoped namespace.

- **`C_Position3D` read in a render-related system for visual placement** --
  rendered position must be `C_PositionGlobal3D`, which `APPLY_POSITION_OFFSET`
  keeps folded with any modifier-driven offset.

## Lua-side ECS bindings

- **Bare C++ component name in a Lua `components = { "C_..." }` or
  `excludes = { "C_..." }` table** -- must use `IRComponent.C_Name`, not the
  string `"C_Name"`. Applies only in creations that call
  `IRSystem.registerSystem`; bare strings for Lua-defined components (no `C_`
  prefix) are fine.

- **Hand-written `IRTime` / `SystemName` string keys** in new C++ binding code
  (`t["NAME"] = ...`). Use `IR_BIND_TIME(NAME)` / `IR_BIND_SYS(NAME)` so the
  key is derived from the enum and stays in sync.

- **C++ binding code checking a Lua string name against a fixed value set**
  (`if (s == "GRID") ... else if (s == "DETACHED")`). Expose the C++ enum as
  a Lua integer table (`IRComponent.RotationMode.{GRID,DETACHED}`) and accept
  the integer at the binding boundary. See
  [`cpp-lua-enums.md`](cpp-lua-enums.md) for the full pattern and
  allowlist.

- **A Lua `createEntityBatch*` call whose factory-argument count differs
  from the C++ `registerCreateEntityBatchFunction<...>` registration arity**
  for that name. The generated binding
  (`wrapCreateEntityBatchWithFunctions<...>`) reads exactly N positional
  factories and Lua silently ignores args past a C function's fixed arity —
  so "add a component by appending one more factory" (correct for C++
  `createEntity` spawns) silently no-ops: no error, the component never
  attaches, and a system whose archetype requires it just never matches.
  Adding a factory means extending the C++ registration's template arity in
  the same change.
