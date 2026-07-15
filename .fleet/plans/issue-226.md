# Plan: ECS multithreading epic execution (engine #226)

## Context

Engine **#226** is the umbrella design for CPU multithreading of ECS
system execution. The issue body is already a comprehensive design —
this plan is about **how the epic ships**: phase ordering, ticket
filing, migration cascades through existing C++ systems, and integration
with the just-merged Lua-driven ECS stack (#293, complete 2026-05-12).

Survey of current state, with deltas from the issue's assumptions:

- **Lua-driven ECS is done.** CODEGEN-default + EVAL-LuaJIT both
  production (PRs #595/596/597/598/599, all merged 2026-05-10–12).
  This adds a Layer that wasn't in the original #226 design:
  codegen-emitted systems need to pass `Concurrency` through from
  Lua spec to the emitted `createSystem<...>` call, and EVAL-mode
  systems must be forced to `MAIN_THREAD` regardless of the spec
  (sol2 isn't thread-safe).
- **The cliff is already documented.** Issue **#1052** (filed
  2026-05-22) records `IRPerfGrid` at 262K entities → ~20ms/frame
  UPDATE, blocking the 100fps target. Phase 0 of #226 ("build a
  benchmark to prove the cliff") is largely redundant — the cliff
  is `perf_grid` itself, and the existing `perf_grid_matrix.sh`
  harness drives it.
- **Perf-gate CI exists.** `.github/workflows/perf-gate.yml` runs
  `perf_grid_matrix.sh`, compares to baseline in
  `docs/perf/baseline_latest/`, fails on >10% regression. Phase 2
  acceptance ("≥2× UPDATE throughput") just needs `worker_threads`
  added as a matrix axis, not a new harness.
- **`registerSystem<SystemName, Components...>` member-on-System
  pattern already landed** (engine #580). Both threading
  (`Concurrency` annotation) and codegen-emitted systems should
  target this shape, not the older explicit-Params boilerplate.
- **Per-system `flushStructuralChanges`** confirmed
  (`engine/system/src/system_manager.cpp:159`). Issue assumption
  holds — fences are per-system, parallel-for chunks stay inside
  the fence interval.
- **No `engine/job/`** today. Clean slate. Vendor convention
  (`engine/render/third_party/metal-cpp`, `engine/script/third_party/luajit`)
  matches the planned `engine/job/third_party/enkiTS/`.

## Recommended approach: phased rollout, six tickets

The original issue lists Phases 0–5. This plan adjusts that to
**T-220 through T-225** with three shape changes:

1. **Phase 0 collapses** into a small "thread-utilization columns
   in the existing perf-gate" task — the cliff is already measured.
2. **A new Phase 2.5 (`T-223`)** wires codegen to emit
   `Concurrency` annotations and forces EVAL→`MAIN_THREAD`. Without
   this, every Lua-defined system silently runs serial after Phase
   2 lands, which would be a regression for the codegen ergonomics
   the team just shipped.
3. **Phase 4 stays at #4 in the sequence, not earlier.** The
   per-worker-deferred-buffer pattern is documented in the original
   issue and is liveable; promoting Phase 4 forward would block
   #226 closing on extra design.

### T-220 — Perf-gate thread baseline + benchmark scale-up `[sonnet]`

Replaces original "Phase 0".

- Add `worker_threads` axis to `scripts/perf/perf_grid_matrix.sh`
  (values: `0, 1, hw_concurrency-2`). Currently the matrix sweeps
  zoom × subdivision; this adds a third dimension. `0` means "main
  thread only" (today's behavior) and is the baseline for
  before/after compare.
- Extend `perf_grid` `WorldConfig` to allow `entity_count_override`
  so the matrix can run a 4K / 32K / 262K sweep without rebuilding.
- Augment the report-emitter in `scripts/perf/` to surface
  per-system UPDATE time and (post-Phase 1) per-worker utilization.
- File the resulting numbers as `docs/perf-reports/threading_baseline.md`.
  This becomes the document #226 acceptance criterion #2 compares
  against.
- **Does not touch any threading code.** It's the measurement
  surface that Phases 1+ use to validate. Sonnet-scoped on purpose:
  this is shell + Python + WorldConfig field.

Acceptance: `perf-gate.yml` runs the threading baseline cell on
master and posts a `docs/perf-reports/threading_baseline.md` with
{4K, 32K, 262K} × {0, 1, 6} measurements. All cells run serial
today (no threading yet); the numbers are the pre-Phase-2 baseline.

### T-221 — `engine/job/` foundation + access-derivation traits `[opus]`

Original Phase 1, scoped per the issue.

**Job module:**

- Vendor enkiTS at `engine/job/third_party/enkiTS/` (zlib license,
  ~2K LOC). Single CMakeLists.txt in `third_party/` configures the
  static lib; outer `engine/job/CMakeLists.txt` links it.
- Public API: `IRJobs::parallelFor(begin, end, grainSize, fn)`,
  `IRJobs::run(name, fn)`, `IRJobs::isMainThread()`,
  `IRJobs::workerId()`, `IRJobs::workerCount()`,
  `IRJobs::pinTo(workerId, fn)`.
- Worker startup hook: register thread with `easy_profiler`
  (`EASY_THREAD("worker N")`) + seed thread-local RNG. Wired by
  `World` so the pool is alive between `World::start()` and
  `World::stop()`.
- Add `worker_thread_count` to `WorldConfig` (default:
  `max(1, hardware_concurrency() - 2)`).

**Access derivation:**

- Land `SystemAccess` struct + `deriveAccessFromSignature<TickFn>()`
  trait + tag types (`Spawns`, `Destroys`, `MainThread`,
  `AlsoReads<...>`, `AlsoWrites<...>`, `ParallelSafe`) in
  `engine/system/include/irreden/system/system_access.hpp`.
- The trait walks the lambda parameter pack:
  `const T&` → `reads.insert`, `T&` → `writes.insert`, `EntityId` →
  `uses_entity_id = true`, batch-form `(const std::vector<T>&, ...)`
  → same const rule + `is_batch_form = true`.
- **Unused at this phase.** Unit-tested in `IrredenEngineTest` with
  every signature shape documented in `engine/system/CLAUDE.md`.

**Macos efficiency-core note:** on Apple Silicon, default
`worker_thread_count` should cap at P-core count to avoid enkiTS
spinning idle on E-cores. Detect via `sysctlbyname("hw.perflevel0.physicalcpu")`.
Log the choice at startup. (Open question in the original issue —
this resolves it for the default; users can override.)

Acceptance: `engine/job/` builds clean on `linux-debug` +
`macos-debug`. Unit tests for `deriveAccessFromSignature` cover all
three tick signature forms (per-component, per-entity-id, batch).
T-220's baseline cells still pass on `perf-gate.yml` (no perf
regression from linking enkiTS).

### T-222 — `Concurrency::PARALLEL_FOR` + single-system validation `[opus]`

Original Phase 2.

- Extend `createSystem<Components...>(name, concurrency, ...)`
  signature with a `Concurrency` enum parameter (`SERIAL`,
  `PARALLEL_FOR`, `MAIN_THREAD`). Default `SERIAL` keeps every
  existing system unchanged.
- In `SystemManager::executeSystem`, for `PARALLEL_FOR`: dispatch
  `IRJobs::parallelFor(0, node->length_, grainSize, ...)` per
  archetype node. `grainSize` starts at 512 rows, tunable via
  `createSystem` arg.
- `beginTick` and `endTick` always serial on main thread.
- Wire the single-system safety check at registration: if
  `concurrency == PARALLEL_FOR` and the derived `SystemAccess`
  has `uses_entity_id == true` AND no `ParallelSafe` tag, FATAL
  with a clear message naming the system + the offending param.
- Add `IR_ASSERT_MAIN_THREAD()` macro (no-op in release builds)
  to manager entry points: `RenderManager`, `IRRender::*`,
  `AudioManager`, `IRAudio::*`, `IRVideo::*`, `LuaScript::*`,
  every sol2 binding entry. Catches the lambda-body-escapes case
  the trait can't see.

**Migration:** port three well-isolated systems as proof of concept:
`VELOCITY_3D`, `VELOCITY_DRAG`, `ANIMATION_COLOR`. Each is a
one-line annotation change. Re-run `perf_grid_matrix.sh` with
worker_threads={0, 6}, file `docs/perf-reports/threading_phase2.md`
showing the speedup.

Document the safety model in `engine/system/CLAUDE.md`, replacing
the existing "Systems run sequentially — no parallelism" line with
the type-derived contract.

Acceptance: `perf_grid_matrix.sh` 262K cell shows ≥2× UPDATE
throughput at `worker_threads=6` vs `worker_threads=0`. Three
ported systems regress nothing in `IrredenEngineTest`.

### T-223 — Codegen + EVAL threading integration `[opus]` *(new)*

The piece the original issue doesn't cover, surfaced by the
Lua-driven ECS epic merging in the interim.

**Codegen path** (`cmake/lua_codegen/`):

- Extend the DSL: `IRSystem.registerSystem({ ..., concurrency =
  "serial" | "parallel_for" | "main_thread" })`. Default `"serial"`.
- Codegen tool reads the field and emits the appropriate
  `Concurrency::PARALLEL_FOR` enum into the generated
  `createSystem<...>` call.
- For systems missing the field, default `SERIAL` (no behavior
  change for any Lua spec that doesn't opt in).
- The access-derivation trait runs automatically on the
  codegen-emitted lambda — same machinery as hand-written C++
  systems, no Lua-specific code path needed for validation.

**EVAL path** (`engine/script/src/lua_script.cpp`,
`bindLuaDrivenEcs`):

- The runtime registration helper that EVAL uses to register a
  Lua-defined system **overrides** any `concurrency` field to
  `Concurrency::MAIN_THREAD` and logs a one-time warning if the
  Lua spec asked for `parallel_for`. sol2 is not thread-safe;
  EVAL-mode parallel-for is structurally impossible until sol2
  per-state isolation lands (out of scope for this epic).
- Document in `engine/script/CLAUDE.md` "Lua runtime + ECS
  modes" section: codegen-mode systems can opt into parallel,
  EVAL-mode systems cannot.

**Migration:** update `creations/demos/lua_perf_grid/` to set
`concurrency = "parallel_for"` on its wave-system spec. Re-run
the perf measurement; expect parity with the C++ `perf_grid`
parallel case (the whole point of CODEGEN was native perf, so
threading must compose).

Acceptance: `lua_perf_grid` (CODEGEN mode) at 262K entities with
`worker_threads=6` matches `perf_grid` (C++) within ±10%.
`lua_perf_grid` (EVAL mode) refuses to honor `parallel_for` and
warns clearly.

Depends on T-221 + T-222.

### T-224 — Pipeline groups + cross-system validation `[opus]`

Original Phase 3.

- Add `registerPipelineGroups(IRTime::UPDATE, {...})` API; existing
  `registerPipeline` keeps working.
- Cross-system access-conflict check at `World::start()`: for every
  pair `(a, b)` in the same group, FATAL if writes intersect each
  other's reads/writes, or if either is `MAIN_THREAD`, or if both
  have `mutates_archetype_graph` (the last lifts in T-225).
- Migrate the engine UPDATE pipeline declaration to use groups
  where the validator accepts them. Likely groups:
  `{velocity, drag, gravity}` → `{globalPosition}` → `{lifetime}` →
  ... (the actual partition emerges from the validator's accept
  list, not a guess up front).
- Benchmark + file `docs/perf-reports/threading_phase3.md`.

Acceptance: validator rejects a hand-constructed conflicting group
(unit test in `IrredenEngineTest`). UPDATE pipeline reorganized
without breaking gameplay; perf shows additional speedup beyond
T-222 from cross-system parallelism.

### T-225 — Thread-safe deferred mutations `[opus]`

Original Phase 4. Unblocks the "two spawners in same group" check
in T-224 + removes the per-worker boilerplate.

- Make `setComponentDeferred`, `removeComponentDeferred`,
  `markEntityForDeletion`, `createEntity` callable from worker
  threads via per-worker staging buffers in `EntityManager`.
- Drain happens in `flushStructuralChanges()` (already a serial
  fence point — no new sync primitive needed).
- Lift the `mutates_archetype_graph` restriction in T-224's
  validator.

Acceptance: a stress test that spawns 10K entities from a
`PARALLEL_FOR` system across all workers produces the same
archetype graph as the serial baseline. T-224's validator now
accepts `{spawnerA, spawnerB}` as a parallel group.

### T-226 — Async asset loading (deferred) `[opus]`

Original Phase 5. **Filed but not in the closing-criteria chain**
for #226. The umbrella issue closes when T-220–T-225 land; T-226
is an independent follow-on that uses the infrastructure.

- Pin worker `N-1` for I/O via `IRJobs::pinTo(N-1, ...)`.
- `IRAsset::loadTextureAsync(path) -> AssetHandle<C_Texture>`.
- Removes main-thread blocking on disk reads.

## Architecture (deltas from the issue body)

The issue body is the architecture reference. Only deltas:

- **Codegen + threading integration** (T-223 in this plan) is a
  new ticket the issue didn't cover. The Lua spec needs a
  `concurrency` field; the codegen tool propagates it; the EVAL
  runtime overrides it to `MAIN_THREAD`. See T-223 for details.
- **Apple Silicon P/E-core cap** — default `worker_thread_count`
  caps at P-core count on macOS, not raw `hardware_concurrency()`.
  Resolved in T-221 as part of the WorldConfig field.
- **Perf-gate integration** — not a new design, but ensures the
  ≥2× acceptance criterion uses the existing CI gate rather than
  a one-off measurement that ages out.

## Migration notes

**For hand-written C++ systems:** the default stays `SERIAL`, so
zero systems break. Opt-in `PARALLEL_FOR` happens system-by-system
as someone profiles them and decides the speedup is worth the
review surface. The first three (`VELOCITY_3D`, `VELOCITY_DRAG`,
`ANIMATION_COLOR`) ride in with T-222 as proof of concept.

**For Lua codegen-defined systems:** same default-serial behavior.
Adding `concurrency = "parallel_for"` to a Lua spec is a one-line
diff. Re-codegen runs at build time, so no runtime opt-in needed.

**For Lua EVAL-defined systems:** the runtime forces
`MAIN_THREAD`. Authors get a startup warning if they asked for
parallel. This is the right default — sol2 thread-safety isn't
this epic's problem to solve.

**For systems with `EntityId` first param:** these must add
`ParallelSafe{}` tag if they want `PARALLEL_FOR`. The validator
errors otherwise. Most existing `EntityId`-form systems use the
entity to call `IREntity::*` mutators — those will need to
either (a) move the mutator call into a per-worker buffer, or
(b) stay `SERIAL`. T-225 makes (a) ergonomic.

**For batch-form systems** (`std::vector<T>&` over whole column):
stay `SERIAL` by default. The whole-column handle is opaque to
the validator — opt-in only after author audit. The trait flags
batch-form on the SystemAccess so the validator can warn.

## Existing code to reuse

- **`engine/system/src/system_manager.cpp:88-160`** — `executeSystem`
  is the dispatch point. Adding the parallel-for branch is a
  template specialization-style change, not a rewrite.
- **`engine/system/include/irreden/ir_system.hpp:185-209`** —
  `registerSystem<SystemName, Components...>` member-on-System
  pattern (from #580). New `Concurrency` field lives on the
  member-on-System spec, not a separate function.
- **`engine/profile/src/cpu_profiler.cpp:60`** — `EASY_MAIN_THREAD`
  registration. Worker startup hook calls
  `EASY_THREAD("worker N")` symmetrically.
- **`scripts/perf/perf_grid_matrix.sh`** — matrix harness. Add
  `worker_threads` axis; everything else (baseline diff,
  regression label) keeps working.
- **`.github/workflows/perf-gate.yml`** — CI gate. T-222
  acceptance hooks directly into this; no new workflow needed.
- **`engine/world/include/irreden/world/config.hpp`** —
  `WorldConfig`. Add `worker_thread_count` field next to
  `profiling_enabled`.
- **`engine/script/src/lua_script.cpp` `bindLuaDrivenEcs`** —
  the runtime EVAL registration path. T-223 adds the
  `concurrency` override + warning here.
- **`cmake/lua_codegen/`** — codegen tool from T-106/107. T-223
  extends its DSL parser + C++ emitter to handle the
  `concurrency` field.

## Doc updates (folded into the ticket landing each surface)

- `engine/job/CLAUDE.md` — new, lands with T-221. Documents
  enkiTS choice, public API, worker model, macOS P-core cap.
- `engine/system/CLAUDE.md` — amended in T-221 (access traits),
  T-222 (Concurrency enum + IR_ASSERT_MAIN_THREAD), T-224
  (pipeline groups + validator).
- `engine/script/CLAUDE.md` — amended in T-223 ("Lua runtime +
  ECS modes" section gains the `concurrency` field + EVAL
  override note).
- `engine/world/CLAUDE.md` — amended in T-221 (WorldConfig
  `worker_thread_count`).
- `docs/perf-reports/threading_baseline.md` — landed by T-220.
- `docs/perf-reports/threading_phase2.md` — landed by T-222.
- `docs/perf-reports/threading_phase3.md` — landed by T-224.

## Dependency chain

```
T-220 (sonnet, perf baseline)  ─┐
                                ├─> T-222 (opus, PARALLEL_FOR) ─┬─> T-223 (opus, codegen integration)
T-221 (opus, job + traits)  ───┘                                ├─> T-224 (opus, pipeline groups) ─> T-225 (opus, deferred bus)
                                                                │
                                                                └─> T-226 (opus, async assets — optional follow-on)
```

T-220 + T-221 land in parallel (no shared files). T-222 depends
on T-221 only. T-223 depends on both T-221 (traits) and T-222
(Concurrency enum). T-224 + T-225 are sequential after T-222.

## Closing #226

The umbrella issue closes when T-220–T-225 ship and the
`perf-gate.yml` 262K cell shows ≥2× UPDATE throughput at
`worker_threads = hw_concurrency() - 2` vs `worker_threads = 0`.
T-226 is filed but not gating.

## Verification

After all six tickets land:

- `fleet-build --target IRPerfGrid` clean.
- `scripts/perf/perf_grid_matrix.sh --threading-baseline` produces
  a report with `worker_threads ∈ {0, 1, hw-2}` × `entity_count ∈
  {4K, 32K, 262K}` cells, all passing the regression gate.
- `IrredenEngineTest` 100% pass.
- Codegen `lua_perf_grid` at 262K + `worker_threads=hw-2` within
  ±10% of C++ `perf_grid` same cell.
- EVAL `lua_perf_grid` with `concurrency = "parallel_for"` in
  the spec warns and runs serial.
- A unit test for `deriveAccessFromSignature` covers all three
  tick signature forms.
- A unit test for the cross-system validator rejects a
  hand-constructed conflicting group.
- `IR_ASSERT_MAIN_THREAD` fires (debug-only) on any sol2 /
  IRRender call from a worker thread.

## Out of scope (intentional)

- Sol2 thread-safety / per-Lua-state isolation. EVAL-mode
  parallel-for stays blocked until that lands; it's a separate
  epic.
- Auto-grouping (topo-sort over SystemAccess to derive parallel
  groups from a flat system list). Mentioned in #226 as a Phase
  6+ follow-on; defer until manual groups prove insufficient.
- GPU command-buffer threading. Render systems stay
  `MAIN_THREAD`; OpenGL/Metal context affinity is a separate
  problem.
- Lua API for `parallelFor`. Sol2 calls from workers are unsafe;
  exposing `parallelFor` to Lua would invite footguns.
- Removing the existing `VideoRecorder` / `VideoManager` /
  RtAudio / RtMidi threads in favor of the enkiTS pool. They're
  callback-driven and isolated; consolidating them would add
  risk for no perf win.

## What I can do in this session, post-approval

Cross-repo info-isolation: I cannot edit engine files from a
game-repo session. I CAN:

1. Post an epic-execution-plan comment on engine **#226**
   summarizing the six-ticket breakdown + dependency chain.
2. File T-220, T-221, T-222, T-223, T-224, T-225 (and optionally
   T-226) as engine GitHub issues with `fleet:task` labels and
   the acceptance criteria above.
3. Cross-link #1052 in the T-220 issue body as the existing
   cliff measurement.

Implementation lives in engine sessions, picked up by engine
workers after `human:approved` triage.

---

## Steward ledger

reconciled-through: 2026-07-15 (first steward claim — heal + close-out audit)
proposal-pending: none

### Children

All nine verified shipped: closing PR `state=MERGED` **and** the deliverable
confirmed present on `origin/master` (`state=CLOSED/COMPLETED` alone was not
treated as evidence).

| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #1067 | merged | #1081 | `T-220.md` | 2026-07-15 |
| #1068 | merged | #1086 | `T-221.md` | 2026-07-15 |
| #1069 | merged | #1097 | `T-222.md` | 2026-07-15 |
| #1070 | merged | #1121 | `T-223.md` | 2026-07-15 |
| #1071 | merged | #1104 | `T-224.md` | 2026-07-15 |
| #1072 | merged | #1109 | `T-225.md` | 2026-07-15 |
| #1073 | merged | #1205 | — | 2026-07-15 |
| #1195 | merged | #1203 | — | 2026-07-15 |
| #1196 | merged | #1212 | — | 2026-07-15 |

### Decisions

- D1 (2026-07-15): Epic membership is these nine children — the seven from the
  architect's "Epic execution plan" comment table (#1067–#1073) plus #1195/#1196,
  filed mid-epic as T-332 follow-ups and carrying `**Part of epic:** #226`
  back-refs. Umbrella had no `## Children` checklist since filing 2026-04-19
  (pre-protocol); healed on this claim — source: umbrella comment 2026-05-23
  "Six child tickets — closing path" table + back-ref sweep.
- D2 (2026-07-15): #1071 (Phase 3) **did** ship — via PR #1104
  (`5f5df6d7`, 2026-05-23, "T-224: system: pipeline groups + cross-system access
  validation"), not the PR the issue links. Its `closedByPullRequestsReferences`
  returns **#1181 (CLOSED, unmerged)** — a later duplicate attempt (T-362) that
  was correctly abandoned because #1104 had already landed the scope.
  Deliverable confirmed: `registerPipelineGroups` in
  `engine/system/include/irreden/ir_system.hpp`.
- D3 (2026-07-15): #1073 (Phase 5, async assets — non-gating) shipped via PR
  #1205 ("T-368: render: async texture loading API + World icon load POC",
  merged 2026-05-27T16:51:20Z; issue closed 16:51:22Z). The PR carries no
  `Closes #1073` keyword, which is why the link query returns empty. Scope
  landed as `engine/render/{include/irreden/render,src}/async_texture.*` rather
  than the design's `IRAsset::loadTextureAsync` — an in-scope module placement
  change, not a scope drift.
- D4 (2026-07-15): **The umbrella does not close this iteration.** Acceptance
  criterion 2 (≥2× UPDATE-pipeline throughput) is unmet and was explicitly
  deferred, never re-run. Whether to run the measurement or accept-and-close is
  the human's call, not the steward's — source:
  `docs/perf-reports/threading_propagate_transform.md` §Follow-ups: "A
  `--threading-baseline` run against `origin/master` at grid-size 64 is the
  authoritative source for the ≥2× target. Defer until the perf-CI
  hardware-fingerprinted baseline (#1100) is online so the comparison is
  hardware-stable."
- D5 (2026-07-15): D4's deferral blocker has **cleared** — #1100
  ("tools: ir-perf-grid + hardware-fingerprinted baselines") closed COMPLETED
  2026-05-24T01:14:01Z, three days *before* the report that deferred to it
  merged (PR #1203, 2026-05-27). The measurement is unblocked and simply was
  never run; no committed report supersedes the deferral.

### Events

- 2026-07-15: First steward claim of this umbrella (filed 2026-04-19, unmanaged
  for ~12 weeks). It emitted no steward trigger in its entire lifetime: the
  projection skips epics with an empty checklist AND no repo-side plan file,
  and every child was already closed, so the adopt scan (open issues only) had
  no surface either. Surfaced by a manual `Part of epic:` back-ref sweep.
- 2026-07-15: Healed `## Children` to the nine children of D1, all ticked.
- 2026-07-15: Synced the architect plan from device-local staging
  (`~/.fleet/plans/issue-226.md`, authored 2026-05-22) into the repo — it had
  never been committed, so the epic's plan of record existed on exactly one
  machine. This ledger is appended to it.
- 2026-07-15: Close-out audit — 9/9 children shipped; criteria 1, 3, 4 met;
  criterion 2 unmet (D4). Parked `fleet:needs-human` rather than closing, so the
  now-healed checklist's close-out trigger does not re-fire as a no-op each
  iteration.
- 2026-07-15: Doc-drift noted, not fixed (steward writes no code):
  `scripts/perf/perf_grid_matrix.sh:104-106` still reads "worker_threads is
  stored for cell-ID differentiation; the exe stubs it until T-221 wires
  enkiTS". T-221 (#1068) landed 2026-05-23 and the axis is live — the exe
  consumes it (`creations/demos/perf_grid/main.cpp:758`) and `WorldConfig::
  worker_thread_count` feeds the pool (`engine/world/src/world.cpp:32`).
