#ifndef IR_JOB_H
#define IR_JOB_H

#include <functional>
#include <random>
#include <string_view>

namespace IRJob {

class JobManager;

/// Worker-pool task surface.
///
/// Phase 1 of the multithreading epic (#226). The pool is constructed
/// by `World` from `WorldConfig::worker_thread_count` and torn down
/// with `World`. No engine system actually runs on workers yet — T-222
/// is the first consumer; T-225 covers thread-safe deferred entity
/// mutations. Until then, every API here is a foundation surface
/// validated by the unit-test smoke suite only.

/// Splits `[begin, end)` into chunks of at most `grainSize` and runs
/// `fn(rangeBegin, rangeEnd)` for each chunk on a worker thread.
/// Blocks until every chunk has finished.
///
/// `grainSize <= 0` is normalized to 1; an empty range is a no-op.
/// Safe to call from the main thread; calling from a worker would
/// deadlock under enkiTS and is asserted.
void parallelFor(
    int begin, int end, int grainSize, const std::function<void(int rangeBegin, int rangeEnd)> &fn
);

/// Fires a single named task on a worker and blocks until it finishes.
/// `name` is used for the easy_profiler block label on the worker.
void run(std::string_view name, const std::function<void()> &fn);

/// Pins a single task to a specific worker thread (`workerId` is
/// 1-based — `1` runs on enkiTS thread 1 (the first worker thread);
/// `0` is the main thread in enkiTS numbering and is rejected).
/// Blocks until the task finishes.
void pinTo(int workerId, const std::function<void()> &fn);

/// True when called from the thread that constructed the active
/// `JobManager` (main thread). Used by `IR_ASSERT_MAIN_THREAD` in
/// T-222 and by any code that must refuse to run on a worker.
bool isMainThread();

/// Returns the calling thread's worker id. The main thread returns
/// `0`; worker threads return `1..workerCount()`. Useful for diagnostic
/// logging from inside `parallelFor` bodies.
int workerId();

/// Total number of worker threads the scheduler manages. Does NOT
/// include the main thread. Returns `0` if no `JobManager` exists.
int workerCount();

/// Per-worker thread-local RNG, seeded at thread start from the
/// worker id (deterministic across runs for the same worker count).
/// Returning by reference lets a caller advance the state in-place.
/// The main thread also gets one, seeded from `0`.
std::mt19937 &workerRng();

} // namespace IRJob

/// Global pointer to the active JobManager. Set by `JobManager`'s ctor,
/// cleared by its dtor. Same `g_*Manager` pattern as
/// `g_entityManager` / `g_systemManager`. Valid only between
/// `World` construction and destruction; do not store across frames
/// outside `World`'s lifetime.
extern IRJob::JobManager *g_jobManager;

#endif /* IR_JOB_H */
