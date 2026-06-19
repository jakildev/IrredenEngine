#ifndef IR_JOB_H
#define IR_JOB_H

#include <irreden/job/ir_job_types.hpp>

#include <functional>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace IRJob {

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

/// Tunables for the auto-grain / row-range chunk planner
/// (`parallelForAutoGrain`, `parallelChunks`). Defaults match the
/// hand-tuned PROPAGATE_TRANSFORM dispatch from #1804 — that system's
/// inline heuristic is the prototype these helpers generalize, so the
/// defaults reproduce its behavior exactly.
struct ParallelTuning {
    /// Fan out once the work-set reaches this many items (rows). Below
    /// it (and, for `parallelChunks`, below `minNodes_`) the work runs
    /// serially on the calling thread — per-task dispatch overhead isn't
    /// worth it for small work-sets.
    int minItemsToParallelize_ = 4096;
    /// `parallelChunks` only: fan out once the node list reaches this
    /// many nodes, even if the row total is under
    /// `minItemsToParallelize_` (the many-small-node path). The flat
    /// `parallelForAutoGrain` is the single-node case, so this never
    /// trips there.
    int minNodes_ = 8;
    /// A node longer than the planned chunk size splits into several
    /// row-range chunks; a shorter node stays whole (one chunk),
    /// preserving node-granularity for many-small-node levels. Acts as
    /// the floor on the auto-computed chunk size.
    int minChunk_ = 2048;
    /// Aim for ~`tasksPerWorker_ × workerCount()` tasks so enkiTS can
    /// load-balance without flooding the queue.
    int tasksPerWorker_ = 2;
};

/// One row-range of one node: rows `[rowBegin_, rowEnd_)` of the node at
/// `nodeIndex_` in the caller's node list. `parallelChunks` fills the
/// caller-owned scratch buffer with these and dispatches them across
/// the pool.
struct RowChunk {
    int nodeIndex_;
    int rowBegin_;
    int rowEnd_;
};

/// Auto-grain flat parallel-for with serial fallback. Runs
/// `fn(begin, end)` over disjoint partitions of `[0, totalItems)`, each
/// at least `tuning.minChunk` rows, sized so the pool sees roughly
/// `tuning.tasksPerWorker × workerCount()` tasks it can load-balance.
/// Falls back to a single serial `fn(0, totalItems)` when there is no
/// worker pool or `totalItems < tuning.minItemsToParallelize`. The
/// caller owns write-disjointness across ranges.
void parallelForAutoGrain(
    int totalItems,
    const std::function<void(int begin, int end)> &fn,
    const ParallelTuning &tuning = {}
);

/// Row-range chunk planner with serial fallback — the generalized form
/// of PROPAGATE_TRANSFORM's per-level dispatch (#1804). Splits a list of
/// nodes (node `i` is `nodeLengths[i]` rows long) across the worker
/// pool: a node longer than the planned chunk size fans out into
/// several row-range chunks, while a shorter node stays whole, so a
/// single dominant node parallelizes without the many-small-node case
/// losing node-granularity. Runs `fn(nodeIndex, rowBegin, rowEnd)` per
/// chunk on a worker.
///
/// Falls back to a serial pass — `fn(i, 0, nodeLengths[i])` for each
/// node in order, on the calling thread — when there is no worker pool
/// or the level is below BOTH `tuning.minNodes` and
/// `tuning.minItemsToParallelize`. `scratch` is a caller-owned buffer
/// the planner fills with the chunk list; pass the same vector every
/// frame so the planning stays allocation-free on the hot path. Ranges
/// are always disjoint; the caller owns write-disjointness across them.
void parallelChunks(
    std::span<const int> nodeLengths,
    std::vector<RowChunk> &scratch,
    const std::function<void(int nodeIndex, int rowBegin, int rowEnd)> &fn,
    const ParallelTuning &tuning = {}
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
