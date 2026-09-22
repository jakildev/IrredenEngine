#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <irreden/ir_job.hpp>

#include <memory>
#include <thread>

namespace enki {
class TaskScheduler;
}

namespace IRJob {

/// Owns the enkiTS `TaskScheduler` and the worker-pool lifetime.
///
/// One instance lives as a member of `IREngine::World` (constructed
/// after `WorldConfig` and before any module that wants to schedule
/// work). The ctor sets `g_jobManager`; the dtor clears it after
/// stopping the scheduler so callers that race against shutdown see
/// a null manager rather than a half-destructed pool.
///
/// Worker count comes from `WorldConfig::worker_thread_count`
/// (overridable per run with `--worker-threads`).
/// `kAutoWorkerCount` (`-1` on the Lua surface) means "auto" — the
/// manager picks `max(1, hardware_concurrency() - 2)`, capped on
/// Apple Silicon to the P-core count to avoid enkiTS spinning idle
/// on E-cores. `kInlineSerialWorkerCount` (`0`) means no pool at all:
/// no enkiTS scheduler is created and every `IRJob` dispatch runs on
/// the calling thread. That is the true serial arm a threading
/// benchmark measures against — a one-worker pool still has two
/// executors, since enkiTS pumps tasks on the waiting thread.
class JobManager {
  public:
    /// Sentinel WorldConfig value meaning "let the manager pick".
    static constexpr int kAutoWorkerCount = -1;
    /// WorldConfig value requesting inline-serial execution: no worker
    /// threads, no scheduler, every dispatch on the calling thread.
    /// Only an explicit request reaches it — auto never resolves to it.
    static constexpr int kInlineSerialWorkerCount = 0;

    explicit JobManager(int requestedWorkerCount);
    ~JobManager();

    JobManager(const JobManager &) = delete;
    JobManager &operator=(const JobManager &) = delete;

    /// Number of worker threads created (does NOT count the main
    /// thread). At least 1 for every resolved pool; `0` exactly when
    /// inline-serial mode was requested.
    int workerCount() const {
        return m_workerCount;
    }

    /// True when no worker pool exists and every `IRJob` dispatch runs
    /// on the calling thread. `g_jobManager`, `isMainThread()` and
    /// `workerId()` stay valid in this mode; only `scheduler()` is
    /// unavailable.
    bool isInlineSerial() const {
        return m_workerCount == kInlineSerialWorkerCount;
    }

    bool isMainThread() const;

    /// `0` if the caller is the main thread; `1..workerCount()` for
    /// workers. Returns `0` for unknown threads as well — callers
    /// that need to distinguish must combine with `isMainThread()`.
    int workerId() const;

    /// Direct access for the free functions in `ir_job.hpp`. Not
    /// intended for engine code outside this module.
    ///
    /// Precondition: `!isInlineSerial()` — there is no scheduler in
    /// inline-serial mode. Every dispatch entry point takes its inline
    /// branch before reaching here, and this accessor stays assert-free
    /// because it sits on the per-dispatch path, where a debug-build
    /// `IR_ASSERT` is measurable.
    enki::TaskScheduler &scheduler() {
        return *m_scheduler;
    }

  private:
    /// Resolves a requested worker count into the actual count after
    /// clamping (inline-serial passthrough, auto-detect,
    /// hardware_concurrency floor of 1, macOS P-core cap). Logs the
    /// decision once.
    static int resolveWorkerCount(int requested);

    std::unique_ptr<enki::TaskScheduler> m_scheduler;
    std::thread::id m_mainThreadId;
    int m_workerCount;
};

} // namespace IRJob

#endif /* JOB_MANAGER_H */
