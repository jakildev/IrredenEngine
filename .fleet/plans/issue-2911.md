## Plan: voxel: GROUND anchor on a DETACHED_REVOXELIZE canvas orbits its feet with no guard

- **Issue:** #2911
- **Model:** opus
- **Date:** 2026-09-06

### Verified current state (read against the actual code paths, 2026-09-06, master `53973faa`)

**1. The defect mechanism is confirmed by source, and there is no in-tree repro.** `C_VoxelSetNew`'s enum ctor bakes `anchorOffset(GROUND, size)` — z origin `-(size.z - 0.5)` — into `positions_` (`component_voxel_set.hpp:180-190`). Two consumers then treat the pool origin as the body center: `System<REBUILD_DETACHED_VOXELS>::tick` rotates `composed = local + offset` about the origin through `GridRotation::anchoredCellForDetachedVoxel` and seeds a once-per-pool cull bound of `max|composed|` (`system_rebuild_detached_voxels.hpp:101-108`); `DetachedRevoxelize::detail::seedResidentLocals` builds the GPU inverse-resample grid and the dest cube about the same origin (`detached_revoxelize.hpp:75-96, 153-165`). Both carry the comment "one centered voxel set per private pool"; neither checks it. The per-voxel `halfCellAnchor` uniformity assert passes for GROUND exactly as the issue says (uniform z residual of -0.5). No GROUND set is allocated into a detached pool anywhere in the tree — the sole production GROUND user is `day_cycle/main.cpp:277`, a GRID entity — so the symptom is latent, not reproduced; the guard's test creates the first such fixture.

**2. The issue's proposed home, `IRPrefab::RotationMode::setMode`, cannot see the defect — and neither can `spawnPrefab`.** Both allocate the canvas through `IRPrefab::EntityCanvas::create`, which calls `Prefab<kTrixelCanvas>::create` — textures + size + name **only**, no `C_VoxelPool` and no `C_CanvasLocalRotation` (`entity_trixel_canvas.hpp:15-19`, `entity_canvas.hpp:41-46`). Neither site ever binds a voxel set to that canvas's pool. In `spawnPrefab` the DENSE `C_VoxelSetNew` is attached *after* the mode (`prefab_api.cpp:269` vs `:379`) through `DenseVoxel::toComponent`, whose ctor allocates from the **active** (main) canvas, never the detached one. A guard at either site would compare `anchor_` against a canvas the set will never render through: a vacuous gate that reads as coverage. The exhaustive candidate set for "who binds a set to a detached pool" is: the `targetCanvas` ctor argument (C++ and the Lua 4-arg `C_VoxelSetNew.new`), and the post-load `attachToCanvas(canvas)` seed pass — all three land in `seedIntoPool` / the enum ctor in `voxel/`. Checked by grep on `createWithVoxelPool`, `EntityCanvas::create`, `attachToCanvas`, and `DETACHED_REVOXELIZE` across `engine/`, `creations/demos/`, `test/`.

**3. Every in-tree detached set lives on a different entity from its `C_RotationMode` owner.** All eight `canvas_stress` spawns, `detached_probe`, and `fog_demo` do `createWithVoxelPool(...)`, then `createEntity(C_LocalTransform{0}, C_VoxelSetNew{size, color, /*center*/ true, canvas.canvasEntity_})`, then a separate `createEntity(C_LocalTransform{worldPos}, C_RotationMode{DETACHED_REVOXELIZE}, canvas)` (e.g. `canvas_stress/main.cpp:530-544`). So a set-side ctor guard cannot know whether the pool will be re-voxelized (DETACHED vs DETACHED_REVOXELIZE are told apart only by the owner's mode, which reaches the canvas per tick as `C_CanvasLocalRotation::reVoxelize_`), and a mode-side guard cannot find the set.

**4. The one site that holds both facts is the consumer, and it is already in `voxel/`.** `System<REBUILD_DETACHED_VOXELS>` ticks the canvas entity's `C_VoxelPool` + `C_CanvasLocalRotation`, early-returns on `!reVoxelize_`, and has a once-per-pool block (`!pool.hasStaticReVoxelizeBound()`) that already scans every composed local. Every authoring path — C++ `targetCanvas`, Lua 4-arg ctor, `attachToCanvas`, any future `setMode`-then-attach — ends with voxels in a re-voxelize pool that this block scans on the pool's first frame (its own comment: this UPDATE tick runs *before* the render-side GPU seed). The header already includes `render/components/component_canvas_local_rotation.hpp`, so **no new layering edge is introduced**; the `common/ → voxel/` question in the issue body is moot. (For the record, that edge already exists transitively: `common/rotation_mode.hpp` includes `render/entity_canvas.hpp`, which includes `voxel/components/component_voxel_pool.hpp`; `common/command_suite_*.hpp` and `common/*_lua.hpp` include `render/`, `video/`, `input/`, `script/`.)

**5. The geometric check is sharp at every size.** The precondition both consumers assume is "pool origin == body center", i.e. per axis `minComposed + maxComposed == 0`. CENTER: exactly 0 on every axis (the `-(s-1)/2` offset is symmetric; float-exact, all values are multiples of 0.5). GROUND: z sum = `-size.z` (≤ -1 at size 1). CORNER: sum = `size - 1` per axis (≥ 1 at size 2; a 1-cell CORNER set *is* centered and correctly passes). A tolerance of 0.5 separates them with margin. The scan runs over the allocated slot count (`C_VoxelPool::getLiveVoxelCount()` returns the allocation high-water `m_voxelPoolIndex`, `component_voxel_pool.hpp:250`), not the active mask, so a carved CENTER solid (the L-prism, every orbit shape) stays symmetric — the existing bound-seed loop iterates the same span.

**6. What "assert in debug / error log" can mean here.** `IR_ASSERT` logs at critical and throws `std::runtime_error` in debug; under `IR_RELEASE` **both** `IR_ASSERT` and every `IRE_LOG_*` macro expand to nothing (`ir_profile.hpp:125-150`). So the diagnostic is the debug assert, whose own critical log line is the error log; a release-visible diagnostic would need a change to the profile macros — an engine-wide contract, out of scope. Per #2439 (`engine/profile/CLAUDE.md`), the condition calls a named pure helper so no local escapes the macro in release.

**7. Headless testability is established.** A singleton pipeline group executes on the calling thread (`system_manager.cpp:467`), and there is no `catch` anywhere in `engine/system/`, so a tick assert surfaces as `std::runtime_error` from `executePipeline` — `EXPECT_THROW(..., std::runtime_error)` is the existing harness (`system_concurrency_test.cpp:246-259`, `entity_manager_test.cpp:240`). The rebuild tick is headless-safe: pure `GridRotation` math plus `recomputeFaceOccupancyOnCells`, no render manager. `registerSystem` resolves the system's concurrency to SERIAL (no `kConcurrency` on the spec). The headless canvas shape is `createEntity(C_VoxelPool{ivec3(16,16,16)}, C_CanvasLocalRotation{})` (`voxel_set_anchor_test.cpp:58` plus the rotation component), and the 4-arg `C_VoxelSetNew` ctor allocates into it with no `RenderManager` (`lua_entity_anchor_test.cpp`, `voxel_set_anchor_test.cpp`).

**8. Sibling / in-flight reconciliation.** No open engine PR touches `system_rebuild_detached_voxels.hpp`, `detached_revoxelize.hpp`, `grid_rotation.hpp`, `entity_anchor.hpp`, `component_voxel_set.hpp`, or `voxel/CLAUDE.md` (all 13 open PRs' file lists checked). Blocker #2563 closed 2026-08-08. No `.fleet/plans/issue-2911.md` exists on master.

### Scope

One guard, at the consumer that knows the pool is re-voxelized, on the geometric invariant the resample and the cull bound both assume; a headless positive-fire test with a CENTER control; the `voxel/CLAUDE.md` update the issue asks for. The guard deliberately fires on **any non-centered re-voxelize pool**, not on the GROUND enum alone: CORNER orbits about its min corner by the identical mechanism and the bound is equally wrong for it. The anchor enum is the likely cause; the centered pool is the actual precondition, and the check names both anchors in its message.

Out of scope: making GROUND *work* on a detached canvas (an offset pivot + offset bound — a separate design), a release-build diagnostic (verified state 6), and plain `DETACHED` forward-scatter canvases (retirement path, #1589; the tick early-returns on `!reVoxelize_`, and the test pins that).

### Approach

No phase-0 probe: nothing here rests on a measurement. The two premises the approach depends on — "both consumers rotate about the pool origin and assume it is the body center" and "`setMode` / `spawnPrefab` never bind a set to a pool" — are read directly from the cited lines and were checked against the full candidate set (verified state 1, 2, 4).

**Step 1 — pure helper in `engine/prefabs/irreden/voxel/grid_rotation.hpp`**, beside `halfCellAnchor` (the header that declares itself the ONE home of the detached derivation, and that unit tests already exercise headless). Keep the header render-neutral — it includes only `ir_math.hpp` and `component_world_transform.hpp`, and `IRRender::VoxelGpuPosition` lives in `engine/render/` — by taking the composed position through a callable rather than a span of the render struct:

```cpp
/// Per-axis (min + max) of a pool's composed locals over slots [0, n) — twice
/// the body's center relative to the pool origin. Zero on every axis iff the
/// pool origin is the body center: the precondition the re-voxelize resample
/// and REBUILD_DETACHED_VOXELS' cull bound both assume (#2911).
/// `composedAt(i)` returns `local + offset` for slot i, the operand the GPU rotates.
template <typename ComposedAt>
inline IRMath::vec3 poolOriginAsymmetry(int n, ComposedAt &&composedAt);

/// True when every axis of poolOriginAsymmetry is within `tolerance` of zero.
/// 0.5 separates CENTER (exactly 0) from the nearest violator (GROUND at
/// size.z == 1 and CORNER at size == 2 both measure ±1).
template <typename ComposedAt>
inline bool poolIsOriginCentered(int n, ComposedAt &&composedAt, float tolerance = 0.5f);
```

`n == 0` returns `vec3(0)` / `true` (nothing to be off-center; the tick already returns on `liveCount <= 0`). Use `IRMath::min` / `IRMath::max` / `IRMath::abs` only (cpp-math).

**Step 2 — the guard in `System<REBUILD_DETACHED_VOXELS>::tick`**, inside `if (!pool.hasStaticReVoxelizeBound())` and **before** `setStaticReVoxelizeBound`, so a failing pool is never seeded with a bound (keeps the test's "not seeded" check meaningful; in release the block simply proceeds as today):

```cpp
IR_ASSERT(
    IRPrefab::GridRotation::poolIsOriginCentered(safeCount, composedAt),
    "REBUILD_DETACHED_VOXELS: re-voxelize pool is not origin-centered — per-axis "
    "(min + max) of composed locals = ({},{},{}). A GROUND or CORNER C_VoxelSetNew was "
    "allocated into a DETACHED_REVOXELIZE canvas; the resample rotates about the pool "
    "origin, so this solid would orbit its anchor instead of spinning in place. Author "
    "the set CENTER (#2911).",
    IRPrefab::GridRotation::poolOriginAsymmetry(safeCount, composedAt).x,
    IRPrefab::GridRotation::poolOriginAsymmetry(safeCount, composedAt).y,
    IRPrefab::GridRotation::poolOriginAsymmetry(safeCount, composedAt).z
);
```

where `composedAt` is a lambda `[&](int i) { return localPositions[i].pos_ + localOffsets[i]; }` — a lambda object, not a hoisted result, so nothing is unused under `IR_RELEASE` (#2439; the repeated helper calls are once per pool lifetime). Add `#include <irreden/ir_profile.hpp>` explicitly (today `IR_ASSERT` reaches the header transitively). Update the file header's "INVARIANT: one centered voxel set per private pool" paragraph to say the invariant is now enforced by this block and what fires. Coverage is exactly the bound's: once per pool lifetime; a second set allocated after the bound seeded is the pre-existing static-bound gap, unchanged by this task.

**Step 3 — `test/ecs/rebuild_detached_voxels_guard_test.cpp`** (new; registered by hand in `test/CMakeLists.txt` under the `ecs/` group — no glob). Fixture: `IREntity::EntityManager m_entity_manager;` then `IRSystem::SystemManager m_system_manager;` (that order); `makeReVoxelizeCanvas(bool reVoxelize)` creates `C_VoxelPool{ivec3(16,16,16)} + C_CanvasLocalRotation{}` and sets `reVoxelize_` and `rotation_ = vec4(0,0,0,1)` through `getComponent` (test code, not a tick); `registerRebuildSystem()` does `System<REBUILD_DETACHED_VOXELS>::create()` + `registerPipeline(IRTime::Events::UPDATE, {sysId})`, mirroring `chunk_membership_migration_test.cpp:33-40`. Arms:

- `GroundSetInReVoxelizePoolFiresTheGuard` — `C_VoxelSetNew{ivec3(2,2,2), color, EntityAnchor::GROUND, canvas}` on its own entity; `EXPECT_THROW(m_system_manager.executePipeline(IRTime::Events::UPDATE), std::runtime_error)`; then `EXPECT_FALSE(pool.hasStaticReVoxelizeBound())`.
- `CenterSetInReVoxelizePoolIsSilent` — the positive control (issue AC 2), run twice: `EntityAnchor::CENTER`, and the legacy `true` arm every demo spells. `EXPECT_NO_THROW`, then `EXPECT_TRUE(pool.hasStaticReVoxelizeBound())` — proves the tick reached the guarded block, so the silence is not vacuous.
- `CornerSetInReVoxelizePoolFiresTheGuard` (2×2×2 fires) and `SingleCellCornerSetIsCentered` (1×1×1 silent) — pin the widened scope and the tolerance edge.
- `CarvedCenterSetStaysCentered` — CENTER 4×4×4, `carve` deactivating the `x > 0 && y > 0` quadrant (the canvas_stress L-prism), silent — pins allocated-slot semantics.
- `GroundSetOnANonReVoxelizeCanvasIsOutOfScope` — same GROUND set, `reVoxelize_ = false`, `EXPECT_NO_THROW` — pins the scope boundary.
- `PoolOriginAsymmetryMatchesEachAnchor` — the pure helper over the set's own `positions_` / `positionOffsets_` spans for sizes `{1,1,1}`, `{2,2,2}`, `{3,3,3}`, `{2,4,2}`, `{6,6,12}`: CENTER → `(0,0,0)` with `EXPECT_FLOAT_EQ` (exactness is the point), GROUND → `(0, 0, -size.z)`, CORNER → `size - 1` per axis. Odd and even sizes so half-integer parity is covered.

File header comment records why this is the only guard site (verified state 2–4) so the next reader does not re-open `setMode`.

**Step 4 — docs.** `engine/prefabs/irreden/voxel/CLAUDE.md` §"Entity anchor": replace the paragraph that begins "The detached case is **unguarded today and silent**" with the guard's actual behavior — site (`REBUILD_DETACHED_VOXELS`, once per pool on its first re-voxelize frame), trigger (any non-origin-centered pool: GROUND and CORNER both fire, CENTER is silent), that the message reports the per-axis asymmetry, that it is a debug assert and a release no-op like every engine diagnostic, and that `setMode` / `spawnPrefab` were rejected as guard sites because they never bind a set to a pool. Drop the "first `common/` → `voxel/` include" sentence. `entity_anchor.hpp:33-34`: append "(guarded at the re-voxelize consumer, #2911)". `system_rebuild_detached_voxels.hpp` header: as in Step 2.

**Step 5 — verify.** `fleet-build --target IrredenEngineTest`; `fleet-run IrredenEngineTest --gtest_filter='*RebuildDetachedVoxelsGuard*:*VoxelSetAnchor*:*LuaEntityAnchor*'`. GL no-regression (not the positive-fire gate): `fleet-build --target IRCanvasStress` + `fleet-run IRCanvasStress --auto-screenshot` and `--only interpenetrate` (MODE 1 rotating), `IRDetachedProbe --auto-screenshot` — every in-tree detached set is CENTER, so a debug build must run to completion with no assert and captures byte-identical to master. `cmake --build <build-dir> --target header-checks` green.

### Affected files

- `engine/prefabs/irreden/voxel/grid_rotation.hpp` — add `poolOriginAsymmetry` / `poolIsOriginCentered` (callable-based, render-neutral).
- `engine/prefabs/irreden/voxel/systems/system_rebuild_detached_voxels.hpp` — the `IR_ASSERT` in the once-per-pool bound-seed block, before `setStaticReVoxelizeBound`; explicit `ir_profile.hpp` include; header INVARIANT paragraph.
- `test/ecs/rebuild_detached_voxels_guard_test.cpp` — new (seven arms above).
- `test/CMakeLists.txt` — register the new source under `ecs/`.
- `engine/prefabs/irreden/voxel/CLAUDE.md` — §"Entity anchor" paragraph (issue AC 3).
- `engine/prefabs/irreden/common/components/entity_anchor.hpp` — header-comment pointer.
- `.fleet/plans/issue-2911.md` — this plan, as the PR's first commit.

### Acceptance criteria

1. **Positive fire.** `RebuildDetachedVoxelsGuard.GroundSetInReVoxelizePoolFiresTheGuard` throws `std::runtime_error` out of `executePipeline(UPDATE)` on the headless fixture, and the pool has no static bound afterward. This is the one path every re-voxelize pool traverses, so it covers the C++ `targetCanvas` spawn, the Lua 4-arg spawn, and the `attachToCanvas` seed alike; the issue's "runtime transition" and "spawn" paths never bind a pool (verified state 2), so there is no separate site to fire — the test file's header says so.
2. **Positive control.** `CenterSetInReVoxelizePoolIsSilent` passes for both spellings with the bound seeded afterward; `GroundSetOnANonReVoxelizeCanvasIsOutOfScope` passes silent.
3. **Widening pinned.** CORNER 2³ fires; CORNER 1³ is silent; carved CENTER is silent.
4. **Helper values** as listed, `EXPECT_FLOAT_EQ` on CENTER.
5. **Docs.** `voxel/CLAUDE.md` §"Entity anchor" no longer says "unguarded today"; it names the guard site, trigger, message, and release behavior.
6. **No GL regression.** `IRCanvasStress --auto-screenshot`, `IRCanvasStress --only interpenetrate --auto-screenshot`, and `IRDetachedProbe --auto-screenshot` complete in a debug build with no assert; captures unchanged vs master.
7. `header-checks` green; the diff introduces no `glm::` / `std::` math outside `engine/math/`.

### Gotchas

- **Do not hoist the asymmetry into a local to feed the assert** (#2439): under `IR_RELEASE` the whole macro vanishes and the local is unused. The lambda is the only thing outside the macro, and a lambda object is not "unused".
- **Assert before `setStaticReVoxelizeBound`**, not after — otherwise the test's "not seeded" check is meaningless and a debug run that catches the throw would have a half-initialized pool.
- **Scan `safeCount` (allocated slots), never the active mask** — carves must not break symmetry; the seed loop and the bound loop both iterate the same span.
- **The tolerance is 0.5 on purpose.** CENTER is exactly 0; the nearest violator is ±1. Neither an epsilon (pointless — values are multiples of 0.5) nor anything ≥ 1 (would pass GROUND at size 1 and CORNER at size 2) is correct.
- **Do not try to name the offending set or its anchor enum in the message.** The system sees only the pool. Adding `C_Name` to the archetype filter would drop unnamed canvases from the tick; switching to the entity-id tick form for a one-shot diagnostic is churn. "GROUND or CORNER" plus the three numbers is the honest message.
- **Do not put the guard in `seedResidentLocals`** (`render/`): it needs live GPU buffers and cannot run headless, and the rebuild tick runs first on a pool's first frame anyway.
- **Do not add a guard to `setMode` or `spawnPrefab`** — verified state 2: there is no pool binding there to check. A gate that passes by construction reads as coverage and is worse than none.
- **Fixture order and quats.** `EntityManager` before `SystemManager`; `C_CanvasLocalRotation::rotation_` must be the identity `(0,0,0,1)` in the fixture — the all-zero sentinel is not a rotation, and the mask recompute uses `rotation_` even though `reVoxelize_` is what gates the tick.
- **Register the test source in `test/CMakeLists.txt`** — there is no glob; a forgotten entry compiles nothing and reads as a green suite.
- **Plans are engine-public** — no game terminology (none needed here).
