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
extern thread_local std::mt19937 t_workerRng;
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
    return detail::t_workerRng;
}

} // namespace IRJob
