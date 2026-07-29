# Plan: voxel: GRID entity without `C_RotationMode` silently renders its authored rotation as identity

- **Issue:** #2376
- **Model:** opus — the mechanics below are fully specified, but the change flips render behavior engine-wide and the acceptance harness (parity-probe extension) needs judgment; overrides the body's `[sonnet]` hint
- **Date:** 2026-07-20

## Verified current state (repro confirmed)

- `system_rebuild_grid_voxels.hpp` `create()` registers `registerSystem<REBUILD_GRID_VOXELS, C_VoxelSetNew, C_WorldTransform, C_RotationMode>` — an entity without `C_RotationMode` never matches the archetype, so only the translate-only baseline from `UPDATE_VOXEL_SET_CHILDREN` (query: `C_VoxelSetNew, C_WorldTransform`, no mode requirement) writes its world cells. Authored rotation/scale silently drop.
- `component_rotation_mode.hpp`'s own header documents the OPPOSITE contract: "Entities without `C_RotationMode` are implicitly GRID — consumers default to GRID when the component is absent." Consumer audit: `IRPrefab::RotationMode::setMode` honors it (`getComponentOptional`, defaults GRID, `rotation_mode.hpp:51`); `REBUILD_GRID_VOXELS` is the one archetype-gated violator. The rotation drivers (`AUTO_SPIN_LOCAL_TRANSFORM`, `ROTATION_TARGET_LOCAL_TRANSFORM`) query only `C_LocalTransform + C_AutoSpin`/`C_RotationTarget` — they author rotations that then never render. `SYSTEM_REBUILD_DETACHED_VOXELS` requires an explicit non-GRID mode by definition. Single fix site.
- The Lua spawn path is already safe: `prefab_api.cpp:266` always attaches `C_RotationMode`. The gap is C++ ad-hoc `createEntity` callers (demos, editors, test scaffolding). Confirmed repro: detached_probe's seeded GRID parity twin spawned without the component and rasterized at identity, mis-measuring the revox anchor error (#2349; the fix hand-attached the component — `creations/demos/detached_probe/main.cpp:231` documents it).
- The ECS supports exclusion query arms: `Exclude<Tags...>` (`ir_system_types.hpp:253`), already used by `CanvasToFramebuffer` (`Exclude<C_DetachedCanvas>`), `MODIFIER_RESOLVE_GLOBAL` (`Exclude<C_NoGlobalModifiers>`), and `command_randomize_voxels` (`Exclude<C_Locked>`).
- No optional-own-component tick form exists — the `std::optional<...*>` signature is the cross-entity relation form only (`ir_system.hpp`), so one system cannot cover both present- and absent-component archetypes.

## Scope

Issue option 1 (implicit-GRID default), via a second query arm: a twin system `REBUILD_GRID_VOXELS_IMPLICIT` whose query is `<C_VoxelSetNew, C_WorldTransform, Exclude<C_RotationMode>>` running the exact GRID body. Absence-of-component then renders identically to explicit `C_RotationMode{GRID}` — the documented contract becomes true by construction. Option 2 (diagnostic) becomes unnecessary; option 3 (docs) rides along as comment/CLAUDE.md updates.

Rejected alternatives: auto-attaching the component at the seed pass (`SYSTEM_SEED_STAGED_VOXELS`) is a mid-iteration structural change (the ECS footgun), and there is no C++ creation choke point (demos call variadic `createEntity` directly).

## Approach

1. `engine/system/include/irreden/system/ir_system_types.hpp` — add `REBUILD_GRID_VOXELS_IMPLICIT` to `SystemName`, adjacent to `REBUILD_GRID_VOXELS` (~line 98).
2. `engine/prefabs/irreden/voxel/systems/system_rebuild_grid_voxels.hpp` — add `System<REBUILD_GRID_VOXELS_IMPLICIT>` in the same header, by composition, NOT by refactoring the hot body:
   - member `System<REBUILD_GRID_VOXELS> impl_;` (twin owns its own scratch through it);
   - `beginTick()` delegates to `impl_.beginTick()`;
   - `tick(C_VoxelSetNew &set, const C_WorldTransform &wt)` delegates to `impl_.tick(set, wt, kImplicitGrid)` where `kImplicitGrid` is a `constexpr C_RotationMode` constructed as GRID;
   - `create()` returns `registerSystem<REBUILD_GRID_VOXELS_IMPLICIT, C_VoxelSetNew, C_WorldTransform, Exclude<C_RotationMode>>("RebuildGridVoxelsImplicit")`.
   Keep the default SERIAL concurrency — do NOT add `ParallelSafe` (the body is not audited for it; `UPDATE_VOXEL_SET_CHILDREN`'s audit does not transfer).
3. `engine/script/include/irreden/script/lua_pipeline_bindings.hpp` — add `IR_BIND_SYS(REBUILD_GRID_VOXELS_IMPLICIT)` next to the existing bind (~line 126).
4. Register the twin immediately after `REBUILD_GRID_VOXELS` in every pipeline that registers the main system, keeping them adjacent, twin second. Order between main and twin is immaterial (disjoint archetypes).
5. Docs (option 3 rides along): `component_rotation_mode.hpp` header comment — implicit-GRID is now enforced by `REBUILD_GRID_VOXELS_IMPLICIT`, name it; `engine/prefabs/irreden/voxel/CLAUDE.md` — pair-registration rule ("wherever REBUILD_GRID_VOXELS registers, register the implicit twin next to it"); `detached_probe/main.cpp:231` — the "REQUIRED" comment becomes stale, reword (keep the explicit component on that totem; absence is covered by the new acceptance totem below).
6. Acceptance harness — extend detached_probe's parity-anchor architecture: add a seeded 45°-Z GRID totem WITHOUT `C_RotationMode` (new anchor bucket alongside the existing 0 = identity anchor / 3 = seeded anchor), at its own position. Per measurement, assert its centroid equals the with-component seeded anchor (bucket 3), and that both differ from the identity-pose prediction. Log one `DOMAIN-STATE grid-default-parity` line; reuse the probe's existing non-zero-exit fail path.

## Affected files

- `engine/system/include/irreden/system/ir_system_types.hpp` — 1 enum entry
- `engine/prefabs/irreden/voxel/systems/system_rebuild_grid_voxels.hpp` — twin `System<>` by composition
- `engine/script/include/irreden/script/lua_pipeline_bindings.hpp` — 1 bind line
- `engine/prefabs/irreden/common/components/component_rotation_mode.hpp` — doc comment
- `engine/prefabs/irreden/voxel/CLAUDE.md` — pair-registration convention
- every `creations/demos/*/main.cpp` that registers `REBUILD_GRID_VOXELS` — pipeline pair-registration; detached_probe additionally gets the no-component acceptance totem + comment reword

## Acceptance criteria

- detached_probe run logs `DOMAIN-STATE grid-default-parity` PASS: no-component seeded totem centroid == explicit-GRID seeded totem centroid, and both != the identity-pose prediction. **Positive-fire:** this assert FAILS without the fix (the no-component totem rasterizes at identity), so the check observably exercises the new arm.
- Existing behavior byte-identical for entities that carry `C_RotationMode` — the twin's `Exclude` archetype can't match them; shape_debug / render-verify references unchanged.
- Build green: `fleet-build --target IRShapeDebug` + the detached_probe target; probe run exits 0.

## Gotchas

- **Behavior flip by design:** any existing creation that authored a non-identity rotation on a component-less voxel set previously rendered it at identity and will now render it rotated. That is the fix, but it can change existing visuals in creations not audited here (including private ones) — hence the human plan-review hold.
- Per-frame cost: component-less sets now pay the same cull-gated per-frame re-rasterize that explicit-GRID sets already pay (identity arm when unrotated). No new cost class, but scenes with many static component-less sets get a real CPU delta; the cull gate bounds it to visible sets.
- Both system instances resolve the cull viewport in `beginTick()` — duplicated tiny work, acceptable; do not "optimize" by sharing state across systems.
- The twin must live in the same header so every demo that can name `REBUILD_GRID_VOXELS` can name the twin without a new include.
- `IRPrefab::RotationMode::setMode` on a component-less entity treats current mode as GRID (unchanged) — a GRID→DETACHED transition adds the component and the entity migrates archetypes out of the twin into the main system automatically. No special handling needed; do not add any.

## Architect approach sign-off (2026-07-28)

Plan reviewed in the human-cued architect triage session: sound per PLANNING-PROTOCOL step-2 rigor (verified premises, single committed approach, positive-fire acceptance). Approach sign-off granted — queue on the normal ingest path.
