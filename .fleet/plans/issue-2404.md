## Plan: SystemManager: per-system update cadence (run-every-N-ticks + dt-scaled reduced-rate execution)

- **Issue:** #2404
- **Model:** opus — the approach below is committed; implementation is a bounded single-module scheduler change + tests, but it touches the core pipeline dispatch path so it stays above sonnet.
- **Date:** 2026-07-14

### Scope

A per-system execution-cadence facility in the SystemManager pipeline: a system declares (at registration) or sets (at runtime) "run every N phase ticks"; off-cadence ticks skip the system's entire dispatch (no `beginTick`, no node query, no per-entity iteration); on-cadence executions can read how many phase ticks — and how much fixed-step time — elapsed since their previous execution. Generic engine capability only; throttle policy stays in consuming creations.

### Verified current state (master `5374cf78`, 2026-07-14)

- `SystemManager::executePipeline` (`engine/system/src/system_manager.cpp:334`) runs every group and every member unconditionally; the only per-system dispatch knobs are `Concurrency` + `grainSize` (parallel vectors `m_concurrency` / `m_grainSize` emplaced in `createSystem`). **No cadence/throttle mechanism exists in the scheduler** — verified by grep across `engine/` for cadence/throttle/every-N/sub-rate shapes: the only hits are in-body counters private to individual systems (`system_perf_stats_overlay.hpp` text-refresh throttle, `system_voxel_to_trixel.hpp` log throttles), which still pay full dispatch + iteration — exactly the pattern this issue replaces with real dispatch savings.
- Phase ticks: `World::gameLoop` calls `executePipeline(INPUT)` once per frame, `executePipeline(UPDATE)` once per `shouldUpdate()` fixed step (0..maxTicks catch-up per frame), `executePipeline(RENDER)` once per frame. `IRTime::tick()` (= `EventProfiler<UPDATE>::fixedStepCount`) counts UPDATE ticks only; SystemManager has no per-event execution counter today.
- `IRTime::deltaTime(UPDATE)` is the constant fixed step (1/kFPS, const-after-ctor, worker-readable). For UPDATE-phase systems, "accumulated fixed-step delta since previous execution" ≡ elapsedPhaseTicks × `deltaTime(UPDATE)` exactly — no new accumulator infrastructure needed in `engine/time`; this reuses the existing `IRTime` fixed-step contract as the issue asks.
- Registration-metadata pattern to mirror: `registerSystem<N>` detects `static constexpr Concurrency kConcurrency` / `int kGrainSize` via concepts (`HasConcurrencyMember` / `HasGrainSizeMember`, `ir_system.hpp`); the free-function `createSystem` takes trailing `concurrency, grainSize` params.
- Per-system timing (`TimingAccum.callCount_` / `totalEntityCount_`, gated on `setTimingEnabled`) already provides the measurement surface for "executed K times over M ticks" and "iteration scales with 1/N".
- Sibling / in-flight reconciliation: no open PR touches `engine/system` (checked all 7 open engine PRs — render/docs/fleet/math surfaces). #226 (threading epic) is complementary by design — T-224 groups parallelize *within* a tick; cadence decides whether a system dispatches at all on a given tick. The gate below runs on the main thread before group fan-out, so there is no interaction with the group validator (a throttled member validates identically for the ticks it does run). Render LOD (`LOD_UPDATE` / `C_ActiveLodLevel`) is orthogonal per the issue. The pausable sim clock (`IRSim::tick()`) is deliberately NOT the cadence timebase — cadence counts pipeline executions; sim-time-aware policies compose on top via the runtime setter.

### Approach

Committed design: **integer divisor is the stored primitive; elapsed-ticks comparison (not modulo); gating lives in `executePipeline` only.**

1. **State (`system_manager.hpp`).** Parallel vectors emplaced in `createSystem` / `createSystemDynamic` alongside `m_concurrency`: `m_cadence` (`uint32_t`, 1 = every tick, 0 normalized to 1), `m_cadenceOffset` (`uint32_t`, initial phase 0..cadence-1), `m_lastRunTick` (`uint64_t`), `m_accumulatedTicks` (`uint64_t`). Plus `std::array<uint64_t, IRTime::END + 1> m_eventTickCounts{}` — SystemManager-owned per-event execution counters (self-contained; works for INPUT/RENDER phases too; unit-testable without a TimeManager). Plus a reusable `std::vector<SystemId> m_dueScratch` for multi-system group filtering (reserved once; no per-frame allocation).
2. **Gate (`system_manager.cpp` `executePipeline`).** Increment `m_eventTickCounts[event]` once at entry → `now`. A system is *due* iff `cadence <= 1 || now >= m_lastRunTick[id] + cadence` (addition, not `now % N` and not `now - lastRun` — the additive form is underflow-safe with a nonzero offset seed and re-phases correctly when the cadence changes at runtime, so call count over M ticks is ⌊M/N⌋ ±1). Singleton group: if not due, skip the observer fires + `executeSystem` (the per-group `flushStructuralChanges` still runs unconditionally). Multi-system group: filter due members into `m_dueScratch` on the main thread before fan-out; skip the `IRJob::parallelFor` entirely when none are due. For each due system, before dispatch (main thread): `m_accumulatedTicks[id] = now - m_lastRunTick[id]`, then `m_lastRunTick[id] = now`. Workers never write cadence state; tick bodies may read it (written-before-dispatch, read-only during execution — same thread-safety argument as the fixed dt).
3. **Late-join stamping.** `registerPipelineGroups` / `appendToPipeline` / `insertSingletonGroupRelativeTo` stamp each newly-listed system's `m_lastRunTick = m_eventTickCounts[event] + offset`, so a system joining a live pipeline measures elapsed from its join point — not from counter zero (otherwise its first accumulated delta spans the pipeline's whole lifetime) — and its offset staggers the initial phase relative to co-registered siblings.
4. **Runtime API (`system_manager.hpp` + `ir_system.hpp` free functions).** `setSystemCadence(SystemId, uint32_t)` (normalizes 0→1; takes effect next phase tick, no re-registration, re-phases from the last run) and `getSystemCadence(SystemId)`. `setSystemCadenceOffset(SystemId, uint32_t)` / `getSystemCadenceOffset(SystemId)` (amendment 1: honor offset from the runtime setter). `getAccumulatedTicks(SystemId)` — phase ticks covered by the current/most-recent execution (≥1 once it has run). Convenience `IRSystem::accumulatedDeltaTime(SystemId)` = `getAccumulatedTicks(id) * IRTime::deltaTime(IRTime::UPDATE)` — exact for UPDATE-phase systems (fixed step); documented UPDATE-phase-only (RENDER dt is wall-clock-variable; RENDER-phase consumers use raw ticks). The issue's "or a target sub-rate" is satisfied as sugar, not a second scheduling mode: `IRSystem::cadenceFromRate(targetHz)` returns `max(1, round(kFPS / targetHz))`.
5. **Registration metadata (`ir_system.hpp`).** Trailing `uint32_t cadence = 1, uint32_t offset = 0` params on the free-function `createSystem` (after `grainSize`) and on `SystemManager::createSystem` (before `accessDescriptor`; the wrapper is its only positional caller). `registerSystem<N>` detects `static constexpr uint32_t kCadence` / `kCadenceOffset` via `HasCadenceMember` / `HasCadenceOffsetMember` concepts + `cadenceOf<T>()` / `cadenceOffsetOf<T>()`, mirroring `kGrainSize`. `createSystemDynamic` gains trailing `cadence = 1, offset = 0` for symmetry.
6. **Lua binding (`lua_pipeline_bindings.hpp`).** `IRSystem.setSystemCadence(sysId, n)` / `IRSystem.getSystemCadence(sysId)` / `IRSystem.setSystemCadenceOffset(sysId, o)` / `IRSystem.getSystemCadenceOffset(sysId)` alongside the pipeline-composition bindings — the surface consuming creations build dynamic throttle policies on. Amendment 2: also bind `IRSystem.getAccumulatedTicks(sysId)` and `IRSystem.accumulatedDeltaTime(sysId)` so a Lua-registered throttled system (EVAL/CODEGEN) can read the numerically-correct-at-reduced-rate delta (issue deliverable 3), same UPDATE-phase-only caveat as the C++ convenience.
7. **`executeSystem` stays ungated.** Cadence is a pipeline-scheduling property; direct `executeSystem` / `executeQuery` calls always run. Keeps "unset = unchanged" airtight and manual-invocation semantics intact.
8. **Docs.** `engine/system/CLAUDE.md` gains a "Per-system cadence" section: declaration forms, the accumulated-delta contract, and skip semantics (`beginTick`/`endTick` also skip; observers don't fire on skipped ticks, so GPU-stage timing rows hold stale samples on skip frames).

One task, one PR — core + tests + Lua binding + CLAUDE.md are one bounded surface; no carve-offs.

### Amendments (accepted in-thread, folded into this plan)

1. **Phase offset.** Optional `offset` (initial phase, 0..cadence-1) alongside the cadence — registration metadata (`kCadenceOffset` / `createSystem` trailing param) + honored by the runtime setter (`setSystemCadenceOffset`). K sibling cold-tier buckets at cadence K registered together now stagger across K consecutive ticks instead of spiking on the same tick.
2. **Lua exposure of the accumulated delta.** Bind `IRSystem.getAccumulatedTicks(sysId)` and `IRSystem.accumulatedDeltaTime(sysId)` in addition to the cadence getter/setter, so a Lua-registered throttled system can read the accumulated delta and stay numerically correct at the reduced rate.

Explicitly NOT this issue: deferred component add/remove from an EVAL Lua tick (tier-migration systems) — a separate #2286-adjacent gap, filed by the consumer if confirmed.

### Affected files

- `engine/system/include/irreden/system/system_manager.hpp` — cadence/offset/lastRun/accumulated vectors, per-event counters, due-scratch, `createSystem` params, runtime API
- `engine/system/src/system_manager.cpp` — `executePipeline` gate + bookkeeping, late-join stamping, `createSystemDynamic` params, runtime-API impls
- `engine/system/include/irreden/ir_system.hpp` — `createSystem` trailing params, `kCadence`/`kCadenceOffset` detection, free functions (`setSystemCadence` / `getSystemCadence` / `setSystemCadenceOffset` / `getSystemCadenceOffset` / `getAccumulatedTicks` / `accumulatedDeltaTime` / `cadenceFromRate`)
- `engine/script/include/irreden/script/lua_pipeline_bindings.hpp` — Lua setter/getter + accumulated-delta bindings
- `test/system/system_cadence_test.cpp` — new; fixture pattern per `test/system/pipeline_groups_test.cpp`
- `test/CMakeLists.txt` — register the new test
- `engine/system/CLAUDE.md` — cadence section

### Acceptance criteria

Concrete versions of the issue's five:

1. A cadence-N system executes ⌊M/N⌋ (±1) times over M `executePipeline` calls (counting tick body; cross-checked against `TimingAccum.callCount_` with timing enabled).
2. The sum of per-execution `getAccumulatedTicks` across a mid-run `setSystemCadence` change equals total elapsed phase ticks, and each execution's value matches its actual gap (e.g. 3,3,3 → change to 5 → 5,5); `accumulatedDeltaTime` = that × fixed dt.
3. Off-cadence ticks fire no `beginTick`/`tick`/`endTick`, and per-entity visits scale ~1/N (`totalEntityCount_` or a visit counter).
4. Default path unchanged: full existing suite green (`pipeline_groups_test`, `system_concurrency_test`, `register_system_test` untouched); the cadence-1 fast path is one load + compare.
5. Mixed-cadence multi-system group: only due members dispatch on a given tick (use the `g_jobManager == nullptr` serial fallback for a deterministic unit test); runtime mutability covered by 2.

### Gotchas

- Stamp `m_lastRunTick` when a system joins a live pipeline (step 3) — without it, the first accumulated delta after a late `appendToPipeline` spans the whole run.
- Skips must NOT skip the per-group `flushStructuralChanges` — deferred structural changes queued by other systems still flush on the existing cadence.
- The due-filter for parallel groups runs on the main thread (reused scratch vector, no allocation in the frame loop); do not move the check inside the worker lambda.
- The due comparison is `now >= lastRun + cadence` (addition), NOT `now - lastRun >= cadence` — the offset seed makes `lastRun > now` on early ticks, and the subtraction form underflows `uint64_t` there. Accumulated (`now - lastRun`) is computed only at fire time, where `now >= lastRun` holds.
- `IRTime::Events` is a C-style enum (`UPDATE, RENDER, INPUT, START, END`) — size the counter array `END + 1`; no hashing.
- `kCadence` goes on the `System<N>` specialization as a constexpr member (like `kGrainSize`), NOT in the component pack — tag types in the `registerSystem` pack have a known filtering gap.
- Perf overlay / GPU-stage rows show stale samples for throttled systems on skip frames — expected; note it in CLAUDE.md rather than special-casing the observer.
- Purely additive public surface — no engine API removals.
