## Plan: test: backfill cadence coverage — Lua binding surface + System<N> kCadence spec-member path (#2425 follow-up)

- **Issue:** #2450
- **Model:** sonnet — tests against a merged, documented API; every file, fixture, expected value, and pitfall is pinned below
- **Date:** 2026-07-29

### Scope

Close the two coverage gaps #2425 (per-system cadence, #2404) shipped with, exactly as the pre-merge coverage-request comment on PR #2425 enumerated them:

1. The six `IRSystem.*` Lua cadence bindings (`lua_pipeline_bindings.hpp`) — set/get round-trip, a throttled Lua-registered system observably firing 1-in-N with `getAccumulatedTicks` read from its tick body, and both throw paths.
2. The `System<N>` spec-member detection path (`kCadence` / `kCadenceOffset` via `registerSystem<N>`), including the absent-members defaults.

One task, one PR. No production-code behavior changes.

### Verified current state (2026-07-29, origin/master 06883364)

- The six bindings live at `engine/script/include/irreden/script/lua_pipeline_bindings.hpp:348-391`. The two throw paths are binding-side guards: `cadence < 1` throws at :357-358, `offset < 0` at :370-373. Note the deliberate asymmetry (flagged and accepted in #2425's review): Lua throws, while the C++ manager *normalizes* (cadence 0 → 1; offset reduced mod cadence into `[0, cadence)`). Tests must assert the throw on the Lua seam, not normalization.
- Spec-member detection: concepts + `cadenceOf` / `cadenceOffsetOf` detectors at `engine/system/include/irreden/ir_system.hpp:333-361`, consumed by `registerSystem<N, Cs...>` at :425-430. **Zero tests exercise it** — `grep -rn "kCadence|cadenceOf" test/` returns nothing, and all 13 `SystemCadenceTest` cases construct via the `createSystem` free-function trailing params. The issue's negative claim holds.
- `accumulatedDeltaTime` (`ir_system.hpp:700-710`) multiplies `getAccumulatedTicks` by `IRTime::deltaTime(UPDATE)`, which asserts on a null `g_timeManager` (`engine/time/src/ir_time.cpp:7-10`). A bare `IRTime::TimeManager` constructs standalone — no window/GL, ctor just stamps the global (`engine/time/src/time_manager.cpp:8-16`) — and `deltaTime<UPDATE>()` returns the *fixed* step `1.0 / IRConstants::kFPS` (`time_manager.cpp:44-46`, `event_profiler.hpp:21`), so the value is deterministic in a unit test with a TimeManager fixture member.
- Precedents: `test/system/register_system_test.cpp:19-67` (test-reserved `System<N>` specs in `namespace IRSystem` + readback via `getSystemParams<System<N>>`, :104-105); `test/script/lua_pipeline_register_test.cpp:28-40` (Lua fixture shape) and :381-420 (attaching a Lua component to an entity and proving a Lua system ticks under `executePipeline`); `test/CLAUDE.md` §"Lua seam tests" (fixture member order is load-bearing).
- `SystemName` tail (`engine/system/include/irreden/system/ir_system_types.hpp:227-231`) ends with the test-reserved block `TEST_REGISTER_SYSTEM_A/B`. Test-reserved entries are **not** mirrored into the Lua `IR_BIND_SYS` table (verified: no `TEST_*` among the 88 entries), so the new entries below don't touch the binding table.
- Both target test files are already registered in `test/CMakeLists.txt` (:78, :90) — **no CMake edit needed** (deliberate: open PR #2573 touches `test/CMakeLists.txt`; this plan avoids that conflict surface entirely).

No phase-0 probe needed: no phase below rests on a runtime mechanism claim — every premise above was source-verified, and expected fire patterns are derived from the gate semantics the existing 13 tests already pin (join seeds `lastRun = counter + offset`; due when `now >= lastRun + cadence`).

### Approach

**1. Two test-reserved `SystemName` entries** — `engine/system/include/irreden/system/ir_system_types.hpp`, appended inside the existing "Reserved for tests" block (comma after `TEST_REGISTER_SYSTEM_B`):

```cpp
    TEST_CADENCE_SPEC_MEMBER,   // declares kCadence / kCadenceOffset
    TEST_CADENCE_SPEC_DEFAULT   // declares neither (defaults path)
```

The issue's "No engine-source changes expected" misses this one necessity: a `System<N>` specialization requires its own `SystemName` entry (engine/CLAUDE.md §"SystemName enum is authoritative"; precedent `TEST_REGISTER_SYSTEM_A/B`). Additive, test-reserved, not Lua-bound.

**2. Spec-member tests** — append to `test/system/system_cadence_test.cpp` (the cadence home; keeps `register_system_test.cpp` untouched, which open PR #2596 is editing). After the existing anonymous namespace, add a `namespace IRSystem` block (mirroring `register_system_test.cpp`'s layout) with two specs:

- `System<TEST_CADENCE_SPEC_MEMBER>`: `static constexpr std::uint32_t kCadence = 3; static constexpr std::uint32_t kCadenceOffset = 1;`, an `int execCount_ = 0;` bumped in `beginTick()` (fires once per due execution even with zero matched entities — the suite's established counting convention), a trivial `tick(C_CadA &)`, and `create()` returning `registerSystem<TEST_CADENCE_SPEC_MEMBER, C_CadA>("CadenceSpecMember")`.
- `System<TEST_CADENCE_SPEC_DEFAULT>`: same shape, **no** cadence members.

Then two `SystemCadenceTest` cases (fixture is visible across the TU):

- `SpecMemberCadenceDetectedAndDrivesGate` — `IRSystem::createSystem<TEST_CADENCE_SPEC_MEMBER>()`, register into UPDATE, assert `getSystemCadence(sys) == 3u` and `getSystemCadenceOffset(sys) == 1u`, run 12 `executePipeline(UPDATE)` calls, read the instance via `getSystemParams<System<TEST_CADENCE_SPEC_MEMBER>>(sys)` and assert `execCount_ == 3` (join seeds `lastRun = 0 + 1`; fires at phase ticks 4, 7, 10 — same arithmetic `OffsetStaggersSiblings` pins for cadence 2 / offset 1).
- `SpecWithoutCadenceMembersDefaultsToEveryTick` — `createSystem<TEST_CADENCE_SPEC_DEFAULT>()`, assert getters return 1u / 0u, run 5 ticks, assert `execCount_ == 5`.

**3. Lua seam tests** — append to `test/script/lua_pipeline_register_test.cpp` under a **new** fixture (existing tests untouched):

```cpp
class LuaCadenceTest : public testing::Test {
  protected:
    LuaCadenceTest() { m_lua.bindLuaDrivenEcs(); }
    IRScript::LuaScript m_lua;            // FIRST → destroyed last (test/CLAUDE.md)
    IRTime::TimeManager m_time_manager;   // makes accumulatedDeltaTime assert-free + deterministic
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};
```

Five cases:

- `SetGetRoundTripThroughLua` — register a Lua system (Lua-defined marker component, empty tick), then via `safe_script`: `setSystemCadence(sysId, 4)` → `getSystemCadence == 4`; `setSystemCadenceOffset(sysId, 3)` → `getSystemCadenceOffset == 3`; then `setSystemCadence(sysId, 5)` + `setSystemCadenceOffset(sysId, 7)` → `getSystemCadenceOffset == 2` (manager normalization passes through the seam intact).
- `ThrottledLuaSystemFiresOneInNAndReadsAccumulatedTicks` — Lua system whose tick body bumps `g_tickCount` and stores `IRSystem.getAccumulatedTicks(sysId)` into `g_acc` (capture `sysId` as a Lua local upvalue assigned at registration); `IRSystem.registerPipeline(IRTime.UPDATE, { sysId })` from Lua; attach the marker to an entity (`IREntity.addLuaComponent(LuaEntity.new(g_entity), Marker)` — precedent :409-415; **without a matched entity the archetype-batched body never runs**); `IRSystem.setSystemCadence(sysId, 3)`; then 9 × `m_system_manager.executePipeline(IRTime::Events::UPDATE)` from C++. Assert `g_tickCount == 3` (fires at phase ticks 3, 6, 9) and `g_acc == 3`.
- `AccumulatedDeltaTimeScalesTicksByFixedStep` — after an identical throttled run, `IRSystem.accumulatedDeltaTime(sysId)` returned to C++ `EXPECT_DOUBLE_EQ`s `3.0 / static_cast<double>(IRConstants::kFPS)`.
- `CadenceBelowOneRaisesLuaError` — `safe_script("IRSystem.setSystemCadence(sysId, 0)", sol::script_pass_on_error)` → `EXPECT_FALSE(result.valid())`; optionally assert the message contains `"cadence must be >= 1"`.
- `NegativeOffsetRaisesLuaError` — same shape for `setSystemCadenceOffset(sysId, -1)`, message `"offset must be >= 0"`.

Coverage map for the six functions: `setSystemCadence` (round-trip + throttle + throw), `getSystemCadence` (round-trip), `setSystemCadenceOffset` (round-trip + throw), `getSystemCadenceOffset` (round-trip + normalization), `getAccumulatedTicks` (read from inside the throttled tick body), `accumulatedDeltaTime` (fixed-step scaling).

**4. Plan file** — commit this plan as `.fleet/plans/issue-2450.md` in the implementation PR's first commit (#1932).

### Affected files

- `engine/system/include/irreden/system/ir_system_types.hpp` — +2 test-reserved `SystemName` entries in the existing reserved block
- `test/system/system_cadence_test.cpp` — two `System<N>` specs + 2 tests
- `test/script/lua_pipeline_register_test.cpp` — `LuaCadenceTest` fixture + 5 tests
- `.fleet/plans/issue-2450.md` — this plan (first commit)

### Acceptance criteria (positive-fire)

- All six Lua cadence functions exercised through the sol2 seam, with the throttled Lua system **observably firing** (`g_tickCount == 3` over 9 ticks — a count > 0, not a pass-at-default) and `g_acc == 3` read from inside its tick body; both throw paths surface as invalid `safe_script` results.
- The spec-member path **observably drives the gate**: detected 3/1 via public getters *and* 3 fires in 12 ticks read back from the spec instance; the defaults spec reports 1/0 and fires every tick.
- Full suite green locally: `fleet-build --target IrredenEngineTest && fleet-run IrredenEngineTest` (spot-check with `--gtest_filter='SystemCadenceTest.*:LuaCadenceTest.*'` while iterating); CI (quality.yml) green on the PR.

### Gotchas

- **Fixture member order is load-bearing** — `LuaScript` first, managers after (`test/CLAUDE.md` §"Lua seam tests"); backwards order crashes at fixture teardown, not at an assertion.
- **`accumulatedDeltaTime` asserts without a TimeManager** — that's why `LuaCadenceTest` carries one. Do *not* add a TimeManager to `system_cadence_test.cpp`; the spec tests only need `getAccumulatedTicks`-free counting.
- **Set cadence before offset** in round-trips — `setSystemCadenceOffset` reduces into `[0, cadence)` against the *current* cadence.
- **Lua-vs-C++ guard asymmetry is deliberate** (accepted #2425 review nit): don't "align" the C++ side to throw, and don't expect Lua `cadence = 0` to normalize.
- **The Lua throttle test needs a matched entity** — a dynamic system's body fires per matched archetype; zero entities = zero invocations = a vacuously green test.
- **In-flight PR reconciliation:** #2596 edits `ir_system_types.hpp` / `ir_system.hpp` / `register_system_test.cpp` (SystemId miss sentinel) — this plan deliberately avoids `register_system_test.cpp`; if #2596 merges first, the 2-line enum addition rebases trivially (insertion point is the reserved tail block). #2573 edits `test/CMakeLists.txt` — this plan makes no CMake edit. #2608 touches only `lua_component_codegen_test.cpp`. No other open PR touches these files.
- **Non-vacuity check** (the bar #2425's own history set): while iterating, perturb one expected constant per new test (e.g. build once with `kCadence = 1`) and confirm the paired test goes red before finalizing.

