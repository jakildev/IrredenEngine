#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <irreden/ir_jobs.hpp>

#include <memory>
#include <thread>

namespace enki {
class TaskScheduler;
}

namespace IRJobs {

/// Owns the enkiTS `TaskScheduler` and the worker-pool lifetime.
///
/// One instance lives as a member of `IREngine::World` (constructed
/// after `WorldConfig` and before any module that wants to schedule
/// work). The ctor sets `g_jobManager`; the dtor clears it after
/// stopping the scheduler so callers that race against shutdown see
/// a null manager rather than a half-destructed pool.
///
/// Worker count comes from `WorldConfig::worker_thread_count`.
/// `kAutoWorkerCount` (`-1` on the Lua surface) means "auto" — the
/// manager picks `max(1, hardware_concurrency() - 2)`, capped on
/// Apple Silicon to the P-core count to avoid enkiTS spinning idle
/// on E-cores.
class JobManager {
  public:
    /// Sentinel WorldConfig value meaning "let the manager pick".
    static constexpr int kAutoWorkerCount = -1;

    explicit JobManager(int requestedWorkerCount);
    ~JobManager();

    JobManager(const JobManager &) = delete;
    JobManager &operator=(const JobManager &) = delete;

    /// Number of worker threads created (does NOT count the main
    /// thread). Will be at least 1 — a single-worker pool is still
    /// useful for offloading from the main thread.
    int workerCount() const { return m_workerCount; }

    bool isMainThread() const;

    /// `0` if the caller is the main thread; `1..workerCount()` for
    /// workers. Returns `0` for unknown threads as well — callers
    /// that need to distinguish must combine with `isMainThread()`.
    int workerId() const;

    /// Direct access for the free functions in `ir_jobs.hpp`. Not
    /// intended for engine code outside this module.
    enki::TaskScheduler &scheduler() { return *m_scheduler; }

  private:
    /// Resolves a requested worker count into the actual count after
    /// clamping (auto-detect, hardware_concurrency floor of 1,
    /// macOS P-core cap). Logs the decision once.
    static int resolveWorkerCount(int requested);

    std::unique_ptr<enki::TaskScheduler> m_scheduler;
    std::thread::id m_mainThreadId;
    int m_workerCount;
};

} // namespace IRJobs

#endif /* JOB_MANAGER_H */
