# engine/job/ — IRJob worker pool

Phase 1 of the multithreading epic (#226). The module stands up an
enkiTS-backed worker pool that `World` owns for its lifetime.
`PROPAGATE_TRANSFORM` and the `SystemManager` `PARALLEL_FOR` fan-out
are the first consumers; the auto-grain dispatch helpers below (#1900)
consolidate the boilerplate they share.

## Entry point

`engine/job/include/irreden/ir_job.hpp` — `IRJob::parallelFor`,
`IRJob::parallelForAutoGrain`, `IRJob::parallelChunks`, `IRJob::run`,
`IRJob::pinTo`, `IRJob::isMainThread`, `IRJob::workerId`,
`IRJob::workerCount`, `IRJob::workerRng`. The
global pointer `g_jobManager` follows the same `g_*Manager` pattern
as `g_entityManager` — set by `JobManager`'s ctor, cleared by its
dtor, valid only between `World` construction and destruction.

The `JobManager` class itself lives at
`engine/job/include/irreden/job/job_manager.hpp` — engine-internal
shape. Creations only need the umbrella header.

`IRJob::WorkerBlockQueue<T>` (`job/worker_block_queue.hpp`) is the
staging queue for a `PARALLEL_FOR` tick that defers work to `endTick`:
`reset(populationBound)` in `beginTick`, `push` from the tick, `forEach`
after the join. Nothing on the tick path allocates, and executors claim
slots in blocks rather than contending on one atomic per entry.

## Auto-grain dispatch helpers

`parallelFor` is the raw primitive — the caller picks the grain.
`parallelForAutoGrain(totalItems, fn, tuning)` and
`parallelChunks(nodeLengths, scratch, fn, tuning)` (#1900) wrap it with
the fan-out-vs-serial decision, a `~workerCount × tasksPerWorker` grain
target, and a serial fallback so each consumer stops re-deriving that
boilerplate. `parallelChunks` additionally splits a single dominant
node across workers by row range while keeping small nodes whole — the
generalized form of `PROPAGATE_TRANSFORM`'s per-level dispatch.

- Tunables live in `IRJob::ParallelTuning`
  (`minItemsToParallelize_`, `minNodes_`, `minChunk_`,
  `tasksPerWorker_`); the defaults reproduce the original
  PROPAGATE_TRANSFORM constants, so a default-constructed tuning is
  bit-identical to the hand-rolled dispatch it replaced.
- Both fall back to a single serial pass when there is no worker pool
  (`g_jobManager == nullptr`, or inline-serial mode) or the work-set is
  below threshold (the same null-pool contract `parallelFor` asserts on
  — these helpers tolerate it instead, since their whole job is the
  parallel-vs-serial decision).
- Callers own write-disjointness across the dispatched ranges, and
  own the reused `scratch` buffer (`std::vector<IRJob::RowChunk>`) so
  per-frame planning stays allocation-free on the hot path.

## Worker count resolution

The pool size comes from `WorldConfig::worker_thread_count`, which the
engine-common `--worker-threads` arg overrides per run (precedence:
defaults < `config.lua` < `--config-preset` < `--worker-threads`).

- `-1` (the default in `data/configs/default.irconf`) means "auto":
  `max(1, hardware_concurrency() - 2)`. On Apple Silicon the auto
  value is then capped to the P-core count via
  `sysctlbyname("hw.perflevel0.physicalcpu", ...)` so enkiTS workers
  don't spin on E-cores.
- `0` means **inline-serial**: no enkiTS scheduler is created,
  `workerCount()` is `0`, `isInlineSerial()` is true, and every
  dispatch entry point runs its callable on the calling thread. Auto
  never resolves to it — only an explicit request does.
- `N >= 1` is an N-worker pool, after the hardware and P-core caps.

`JobManager: started with <n> worker threads` is logged at INFO in
every mode (inline-serial appends a suffix), so one grep reads the
resolved count out of a benchmark cell's log. The P-core cap logs its
own line when it fires.

Override the config field only for benchmarking. The auto-resolved
value is what production runs against.

**Inline-serial is the real serial floor.** A one-worker pool has
*two* executors: `WaitforTask` pumps tasks on the calling thread while
the worker runs them. Benchmarking a speedup against `1` understates
it; `0` is the arm with a single executor.

## Why enkiTS

- Lightweight (~2K LOC), permissive zlib license.
- Lock-free MPMC task queue + work-stealing scheduler — the same
  design we'd reach for if we wrote one from scratch.
- Mature: doug binks' library has been in production use across
  several engines for a decade.

The vendor wrapper at `engine/job/third_party/enkiTS/` pins the
upstream revision via the `IR_ENKITS_GIT_TAG` cache variable. The
default is the v1.11 commit SHA (`6ffccbdb…`) with
`GIT_SHALLOW FALSE`, which is reproducible by default. Override at
configure time with a branch/tag (and set `GIT_SHALLOW TRUE` in
`FetchContent_Declare`) when you want a mutable reference. The
wrapper forces `CXX_STANDARD 17` on the enkiTS target specifically
because v1.11's `TaskScheduler.cpp` uses `std::is_pod` (removed in
C++23, which the rest of the engine builds at). Drop the override
once we pin to a revision that has migrated off `is_pod`.

## Thread-local state

Workers register their easy_profiler thread name on first task
entry (`ir-worker-N`) via `::profiler::registerThread`, which is
idempotent per OS thread. The same first-touch path seeds a
per-worker `thread_local std::mt19937` from the worker id, so any
system that needs a deterministic per-worker RNG can pull
`IRJob::workerRng()` instead of carrying its own thread-local
random state. The main thread is seeded from id `0` at
`JobManager` construction.

## Contract

- **Main thread dispatches; workers execute.** Every public free
  function asserts `g_jobManager->isMainThread()` at entry. Nested
  worker dispatch (a task scheduling another task) is rejected
  until T-222 lands the cross-system access validator that makes it
  safe.
- **Background tasks don't outlive `World`.** Same rule as every
  other manager singleton — `World`'s dtor waits for the scheduler
  to drain and clears `g_jobManager` before any other manager
  begins shutdown.
- **Manager singletons are NOT thread-safe from worker bodies in
  Phase 1.** Don't pre-wrap `g_entityManager` / `g_systemManager`
  for thread safety here — T-225 owns the deferred-mutation surface
  for worker writes; the right pattern is "queue, flush on main."
- **`std::thread` users (`VideoRecorder`, RtMidi / RtAudio
  callbacks, future audio threads) stay on `std::thread`.** Those
  are callback-driven and isolated; consolidating them into IRJob
  is explicitly out of scope.

## Gotchas

- **`IRJob::run` may execute on the calling thread.** enkiTS'
  `WaitforTask` actively pumps work, so a single `TaskSet` of size
  1 can land on the dispatching thread if the workers are busy or
  the body finishes before they wake. Use `pinTo(workerId, ...)`
  when the body MUST run on a worker (e.g. for `IR_ASSERT(!
  IRJob::isMainThread())` to fire).
- **`parallelFor`'s callback is `(int rangeBegin, int rangeEnd)`,
  half-open.** Matches the iteration convention used everywhere else
  in the engine; do not assume enkiTS' raw `TaskSetPartition.start`
  / `.end` are exposed.
- **`workerId()` returns 0 for main, 1..N for workers, 0 for
  unknown threads.** Combine with `isMainThread()` when the
  distinction matters.
- **`g_jobManager == nullptr` is a valid state.** Unit tests and
  startup-error paths may run with no active pool; the free
  functions return safe defaults rather than crashing. `parallelFor`
  / `run` / `pinTo` assert because there's nothing meaningful to do
  without a pool.
- **Inline-serial is NOT a null manager.** `g_jobManager` is set,
  `isMainThread()` is true and `workerId()` is `0`, so
  `IR_ASSERT_MAIN_THREAD` and the `workerCount() + 1` per-worker
  staging sizing behave exactly as with a pool (one slot). Only
  `scheduler()` is unavailable — it asserts — and `pinTo` has no
  valid target, so its `[1, workerCount()]` assert always fires.
