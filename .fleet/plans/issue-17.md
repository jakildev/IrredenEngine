# Plan: add a one-shot query-command primitive (query-based command execution)

- **Issue:** #17
- **Model:** opus (label `fleet:opus`; plan's advisory `Model:` was sonnet, but the
  task touches core `engine/system` + `engine/command` + ECS surface — opus lane)
- **Date:** 2026-07-03 (planned by architect; implemented 2026-07-04)

## Scope

Add a run-once query-execution primitive to `IRSystem` — the run-now counterpart to
`createSystemDynamic`: same archetype-node traversal, no `SystemId`, no pipeline
registration, nothing persists. Then implement `Command<RANDOMIZE_VOXELS>` on top as
the first consumer, honoring the enum's "exclude locked entities" note via a new
`C_Locked` tag component. Lua exposure of the primitive itself is out of scope;
`RANDOMIZE_VOXELS` becomes Lua-reachable for free through the existing `fireByName`
binding.

## Approach

One-shot query surface in `IRSystem` (matches the issue's "running a system tick
function one time" mental model), traversal shared via
`IREntity::queryArchetypeNodesSimple` — the same call `executeSystem` makes.

1. **`engine/system/include/irreden/ir_system.hpp`** — add `executeQueryDynamic`
   (runtime-typed core: `IR_ASSERT_MAIN_THREAD()` + `queryArchetypeNodesSimple` +
   `body(node)` per node, no SystemManager touch) and `executeQuery<Cs...>`
   (compile-time-typed wrapper accepting `Exclude<...>`, resolving columns once per
   node via `getComponentData<Cs>(node)` and row-iterating, dispatching `tick(Cs&...)`
   or `tick(EntityId, Cs&...)` via `std::is_invocable_v`). Caller thread, serial,
   main-thread-only. Bodies use `IREntity::deferred*` for structural changes;
   `executeQuery` does not flush.
2. **`engine/prefabs/irreden/common/components/component_locked.hpp`** (new) — empty
   tag `struct C_Locked {};` mirroring `C_Persistent`.
3. **`engine/prefabs/irreden/voxel/commands/command_randomize_voxels.hpp`** — implement
   `Command<RANDOMIZE_VOXELS>::create()` running
   `IRSystem::executeQuery<C_VoxelSetNew, IRSystem::Exclude<C_Locked>>(...)`; per set,
   `set.editVoxels(...)`: skip alpha-0 voxels, otherwise `IRMath::randomColor()`
   preserving the original alpha.
4. **`engine/command/src/ir_command.cpp`** — include new header; promote
   `RANDOMIZE_VOXELS` to real cases in `bindPrefabCommand` and `fireByName`.
5. **`engine/command/CMakeLists.txt`** — PRIVATE-link `IrredenEngineSystem`.
6. **`engine/command/include/irreden/command/ir_command_types.hpp`** — resolve the
   2023 TODO comment.
7. **`test/ecs/execute_query_test.cpp`** + **`test/ecs/command_randomize_voxels_test.cpp`**
   (new) + register in `test/CMakeLists.txt`.
8. **Docs** — `engine/system/CLAUDE.md` + `engine/command/CLAUDE.md`.

## Acceptance criteria

- `fleet-build --target IrredenEngineTest` green;
  `IrredenEngineTest --gtest_filter='ExecuteQuery*:RandomizeVoxels*'` all pass.
- `execute_query_test`: (a) visits entities across archetypes; (b) `Exclude` skips
  tagged; (c) `EntityId`-form receives correct ids; (d) `executeQueryDynamic` fires
  once per matched node; (e) no registration (system count unchanged, no re-run in
  `executePipeline`).
- `command_randomize_voxels_test` (headless pool + explicit `targetCanvas`): unlocked
  set gets ≥1 RGB byte changed with every alpha preserved; carved voxel stays
  inactive + mask unchanged; locked set byte-identical; fireByName no longer hits the
  unimplemented log path.

## Gotchas

- `randomColor()` returns alpha 255 — preserve original alpha per voxel; skip alpha-0.
- Use `editVoxels`, never raw `voxels_[i]` writes (GRID-rotation mirror resync, #2165).
- Edit BOTH switches in ir_command.cpp (duplicate-case compile error guards one side).
- Main-thread-only, no flush; zero matches is a silent no-op.
- `IrredenEngineSystem` in engine/command's PRIVATE link block.
