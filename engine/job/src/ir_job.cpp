#include <irreden/ir_job.hpp>
#include <irreden/job/job_manager.hpp>

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <TaskScheduler.h>

#include <cstdint>
#include <string>

namespace IRJob {

namespace detail {
// Forward decl — defined in job_manager.cpp. The free functions need
// to call `registerSelf` from each task entry so the worker's
// thread_local state is initialized exactly once per OS thread.
void registerSelf(enki::TaskScheduler &scheduler);

extern thread_local int t_workerId;
extern thread_local bool t_registered;
} // namespace detail

void parallelFor(
    int begin, int end, int grainSize, const std::function<void(int rangeBegin, int rangeEnd)> &fn
) {
    IR_ASSERT(g_jobManager != nullptr, "IRJob::parallelFor: no active JobManager");
    IR_ASSERT(
        g_jobManager->isMainThread(),
        "IRJob::parallelFor: must be called from the main thread; nested worker dispatch is not "
        "supported in T-221"
    );
    if (begin >= end) {
        return;
    }
    const int range = end - begin;
    const uint32_t grain = static_cast<uint32_t>(IRMath::max(1, grainSize));

    enki::TaskScheduler &scheduler = g_jobManager->scheduler();

    // Capture `fn` by reference — `parallelFor` is blocking, so the
    // caller's std::function outlives the task. begin/end go in by
    // value (cheap).
    enki::TaskSet task(
        static_cast<uint32_t>(range),
        [&fn, begin](enki::TaskSetPartition r, uint32_t /*threadnum*/) {
            // r.start / r.end are offsets into [0, range); shift back
            // into the caller's coordinate space.
            detail::registerSelf(g_jobManager->scheduler());
            fn(begin + static_cast<int>(r.start), begin + static_cast<int>(r.end));
        }
    );
    task.m_MinRange = grain;
    scheduler.AddTaskSetToPipe(&task);
    scheduler.WaitforTask(&task);
}

void parallelForAutoGrain(
    int totalItems, const std::function<void(int begin, int end)> &fn, const ParallelTuning &tuning
) {
    if (totalItems <= 0) {
        return;
    }
    // Serial when there's no pool or the work-set is too small to amortize
    // dispatch overhead. Runs on the calling (main) thread — matches the
    // contract `parallelChunks` and PROPAGATE_TRANSFORM use.
    const bool runParallel = g_jobManager != nullptr && totalItems >= tuning.minItemsToParallelize_;
    if (!runParallel) {
        fn(0, totalItems);
        return;
    }

    // Aim for ~tasksPerWorker_ tasks per worker, floored at minChunk_ rows,
    // and let enkiTS partition [0, totalItems) at that grain.
    const int workers = IRMath::max(1, workerCount());
    const int targetTasks = IRMath::max(1, workers * tuning.tasksPerWorker_);
    const int chunkRows = IRMath::max(
        tuning.minChunk_,
        static_cast<int>((static_cast<int64_t>(totalItems) + targetTasks - 1) / targetTasks)
    );
    parallelFor(0, totalItems, chunkRows, fn);
}

void parallelChunks(
    std::span<const int> nodeLengths,
    std::vector<RowChunk> &scratch,
    const std::function<void(int nodeIndex, int rowBegin, int rowEnd)> &fn,
    const ParallelTuning &tuning
) {
    const int n = static_cast<int>(nodeLengths.size());
    if (n == 0) {
        return;
    }

    int64_t totalRows = 0;
    for (int len : nodeLengths) {
        totalRows += len;
    }

    // Parallelize on either threshold: many nodes (the inter-node path)
    // OR enough total rows (the intra-node path — a single large node
    // still fans out). Below both, the level composes serially.
    const bool runParallel = g_jobManager != nullptr &&
                             (n >= tuning.minNodes_ || totalRows >= tuning.minItemsToParallelize_);

    if (!runParallel) {
        for (int i = 0; i < n; ++i) {
            fn(i, 0, nodeLengths[i]);
        }
        return;
    }

    const int workers = IRMath::max(1, workerCount());
    const int targetTasks = IRMath::max(1, workers * tuning.tasksPerWorker_);
    // Chunk size derived from total work: nodes below it stay whole
    // (preserving node-granularity for many-small-node levels); larger
    // nodes split into several row-range chunks so one dominant node
    // fans out across workers.
    const int chunkRows = IRMath::max(
        tuning.minChunk_,
        static_cast<int>((totalRows + targetTasks - 1) / targetTasks)
    );

    scratch.clear();
    for (int i = 0; i < n; ++i) {
        const int len = nodeLengths[i];
        for (int rowBegin = 0; rowBegin < len; rowBegin += chunkRows) {
            scratch.push_back({i, rowBegin, IRMath::min(rowBegin + chunkRows, len)});
        }
    }

    const int numChunks = static_cast<int>(scratch.size());
    const int chunkGrain = IRMath::max(1, (numChunks + targetTasks - 1) / targetTasks);
    parallelFor(0, numChunks, chunkGrain, [&scratch, &fn](int chunkBegin, int chunkEnd) {
        for (int c = chunkBegin; c < chunkEnd; ++c) {
            const RowChunk &chunk = scratch[c];
            fn(chunk.nodeIndex_, chunk.rowBegin_, chunk.rowEnd_);
        }
    });
}

void run(std::string_view name, const std::function<void()> &fn) {
    IR_ASSERT(g_jobManager != nullptr, "IRJob::run: no active JobManager");
    IR_ASSERT(g_jobManager->isMainThread(), "IRJob::run: must be called from the main thread");
    enki::TaskScheduler &scheduler = g_jobManager->scheduler();

    // Capture name by value (small string copy is cheap; the task
    // outlives this stack frame only briefly under WaitforTask, but
    // copying keeps the semantics simple).
    std::string label(name);
    enki::TaskSet task(
        1u,
        [&fn, label = std::move(label)](enki::TaskSetPartition, uint32_t /*threadnum*/) {
            detail::registerSelf(g_jobManager->scheduler());
            IR_PROFILE_BLOCK(label.c_str(), IR_PROFILER_COLOR_SYSTEMS);
            fn();
        }
    );
    task.m_MinRange = 1;
    scheduler.AddTaskSetToPipe(&task);
    scheduler.WaitforTask(&task);
}

void pinTo(int workerId, const std::function<void()> &fn) {
    IR_ASSERT(g_jobManager != nullptr, "IRJob::pinTo: no active JobManager");
    IR_ASSERT(g_jobManager->isMainThread(), "IRJob::pinTo: must be called from the main thread");
    IR_ASSERT(
        workerId >= 1 && workerId <= g_jobManager->workerCount(),
        "IRJob::pinTo: workerId out of range [1, workerCount()]"
    );
    enki::TaskScheduler &scheduler = g_jobManager->scheduler();

    enki::LambdaPinnedTask task(static_cast<uint32_t>(workerId), [&fn]() {
        detail::registerSelf(g_jobManager->scheduler());
        fn();
    });
    scheduler.AddPinnedTask(&task);
    scheduler.WaitforTask(&task);
}

bool isMainThread() {
    if (g_jobManager == nullptr) {
        // No pool exists — the calling thread IS the only thread,
        // and treating it as main matches the contract: callers
        // gating asserts on this should pass without a pool.
        return true;
    }
    return g_jobManager->isMainThread();
}

int workerId() {
    if (g_jobManager == nullptr) {
        return 0;
    }
    return g_jobManager->workerId();
}

int workerCount() {
    if (g_jobManager == nullptr) {
        return 0;
    }
    return g_jobManager->workerCount();
}

std::mt19937 &workerRng() {
    // Forwarder so existing callers keep working; the per-thread RNG
    // storage now lives in `engine/math/` (`IRMath::threadRng`) so
    // `IRMath::random*` callers share the same mt19937 state.
    return IRMath::threadRng();
}

} // namespace IRJob
