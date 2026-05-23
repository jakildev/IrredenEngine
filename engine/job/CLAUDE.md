# engine/job/ — IRJobs worker pool

Phase 1 of the multithreading epic (#226). The module stands up an
enkiTS-backed worker pool that `World` owns for its lifetime; no
engine system schedules on it yet — T-222 is the first consumer.

## Entry point

`engine/job/include/irreden/ir_jobs.hpp` — `IRJobs::parallelFor`,
`IRJobs::run`, `IRJobs::pinTo`, `IRJobs::isMainThread`,
`IRJobs::workerId`, `IRJobs::workerCount`, `IRJobs::workerRng`. The
global pointer `g_jobManager` follows the same `g_*Manager` pattern
as `g_entityManager` — set by `JobManager`'s ctor, cleared by its
dtor, valid only between `World` construction and destruction.

The `JobManager` class itself lives at
`engine/job/include/irreden/job/job_manager.hpp` — engine-internal
shape. Creations only need the umbrella header.

## Worker count resolution

The pool size comes from `WorldConfig::worker_thread_count`. `-1`
(the default in `data/configs/default.irconf`) means "auto":
`max(1, hardware_concurrency() - 2)`. On Apple Silicon the auto
value is then capped to the P-core count via
`sysctlbyname("hw.perflevel0.physicalcpu", ...)` so enkiTS workers
don't spin on E-cores. The cap decision is logged at INFO at
startup so cross-machine reports surface the resolved count.

Override the config field only for benchmarking. The auto-resolved
value is what production runs against.

## Why enkiTS

- Lightweight (~2K LOC), permissive zlib license.
- Lock-free MPMC task queue + work-stealing scheduler — the same
  design we'd reach for if we wrote one from scratch.
- Mature: doug binks' library has been in production use across
  several engines for a decade.

The vendor wrapper at `engine/job/third_party/enkiTS/` pins the
upstream tag via the `IR_ENKITS_GIT_TAG` cache variable (default
`v1.11`). Override at configure time for a fully reproducible
build. The wrapper forces `CXX_STANDARD 17` on the enkiTS target
specifically because v1.11's `TaskScheduler.cpp` uses
`std::is_pod` (removed in C++23, which the rest of the engine
builds at). Drop the override once we pin to a revision that has
migrated off `is_pod`.

## Thread-local state

Workers register their easy_profiler thread name on first task
entry (`ir-worker-N`) via `::profiler::registerThread`, which is
idempotent per OS thread. The same first-touch path seeds a
per-worker `thread_local std::mt19937` from the worker id, so any
system that needs a deterministic per-worker RNG can pull
`IRJobs::workerRng()` instead of carrying its own thread-local
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
  are callback-driven and isolated; consolidating them into IRJobs
  is explicitly out of scope.

## Gotchas

- **`IRJobs::run` may execute on the calling thread.** enkiTS'
  `WaitforTask` actively pumps work, so a single `TaskSet` of size
  1 can land on the dispatching thread if the workers are busy or
  the body finishes before they wake. Use `pinTo(workerId, ...)`
  when the body MUST run on a worker (e.g. for `IR_ASSERT(!
  IRJobs::isMainThread())` to fire).
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
