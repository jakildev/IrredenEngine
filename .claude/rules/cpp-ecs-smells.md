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

- **`createEntity`, `addComponent`, `removeComponent`, or `removeEntity`**
  mid-iteration without the deferred variant. Fix: use
  `IREntity::deferredCreate` / `deferredSetComponent` etc.; let
  `flushStructuralChanges` run at pipeline end.

- **Allocation in the tick body** -- `new`, `std::vector::push_back` on a hot
  vector, `std::string` construction/concatenation, `std::map::operator[]`
  insertion. Fix: pre-size in `beginTick` (or at component construction for
  member buffers) and reuse across frames.

## System registration

- **New prefab system not added to `SystemName`** in
  `engine/system/include/irreden/system/ir_system_types.hpp`. Fix: add a
  `SCREAMING_SNAKE_CASE` entry under the appropriate group comment.

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
