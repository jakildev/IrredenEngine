#include <irreden/job/job_manager.hpp>

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <TaskScheduler.h>
#include <easy/profiler.h>

#include <cstdio>
#include <cstdint>
#include <random>
#include <thread>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

IRJobs::JobManager *g_jobManager = nullptr;

namespace IRJobs {

namespace detail {

// Per-worker thread-local state. enkiTS spawns N worker threads,
// numbered 1..N. The main thread keeps id 0 and matches `m_mainThreadId`
// in the JobManager.
//
// We register the thread on first task entry rather than via enkiTS'
// profilerCallbacks struct so the code is robust to upstream config
// renames — every API path through parallelFor/run/pinTo funnels
// through the same task body, and that body calls `registerSelf()`
// once.
thread_local int t_workerId = 0;
thread_local std::mt19937 t_workerRng{0u};
thread_local bool t_registered = false;
thread_local char t_workerName[32] = "main";

void registerSelf(enki::TaskScheduler &scheduler) {
    if (t_registered) {
        return;
    }
    const uint32_t enkiThreadNum = scheduler.GetThreadNum();
    t_workerId = static_cast<int>(enkiThreadNum);
    t_workerRng.seed(enkiThreadNum);
    std::snprintf(t_workerName, sizeof(t_workerName), "ir-worker-%u", enkiThreadNum);
    // registerThread() is idempotent per OS thread (only the first
    // call wins, per easy_profiler header docs).
    ::profiler::registerThread(t_workerName);
    t_registered = true;
}

} // namespace detail

namespace {

#ifdef __APPLE__
/// Apple Silicon performance-core count via sysctl. Returns the
/// detected P-core count or `-1` if the sysctl key is absent (Intel
/// Macs, older OS releases). Caller treats `-1` as "no cap".
int querySysctlPCoreCount() {
    int pcoreCount = 0;
    size_t sz = sizeof(pcoreCount);
    if (sysctlbyname("hw.perflevel0.physicalcpu", &pcoreCount, &sz, nullptr, 0) != 0) {
        return -1;
    }
    return pcoreCount > 0 ? pcoreCount : -1;
}
#endif

} // namespace

int JobManager::resolveWorkerCount(int requested) {
    const unsigned int hw = std::thread::hardware_concurrency();
    const int hwCapped = (hw == 0) ? 1 : static_cast<int>(hw);

    int target;
    if (requested == kAutoWorkerCount) {
        // Default heuristic: leave 2 cores for the main thread + OS
        // noise. enkiTS' worker threads spin while idle, so over-
        // provisioning steals cycles from the renderer.
        target = IRMath::max(1, hwCapped - 2);
    } else {
        target = IRMath::max(1, requested);
    }

#ifdef __APPLE__
    const int pcores = querySysctlPCoreCount();
    if (pcores > 0 && target > pcores) {
        IRE_LOG_INFO(
            "JobManager: capping worker count {} -> {} (Apple Silicon P-core limit)",
            target,
            pcores
        );
        target = pcores;
    }
#endif

    if (target > hwCapped) {
        target = hwCapped;
    }
    return IRMath::max(1, target);
}

JobManager::JobManager(int requestedWorkerCount)
    : m_scheduler{std::make_unique<enki::TaskScheduler>()}
    , m_mainThreadId{std::this_thread::get_id()}
    , m_workerCount{resolveWorkerCount(requestedWorkerCount)} {
    enki::TaskSchedulerConfig config;
    // numTaskThreadsToCreate is the worker count NOT including the
    // calling (main) thread. enkiTS' GetNumTaskThreads() returns
    // numTaskThreadsToCreate + 1 to include main.
    config.numTaskThreadsToCreate = static_cast<uint32_t>(m_workerCount);
    m_scheduler->Initialize(config);

    // Seed the main thread's RNG explicitly — workers seed themselves
    // on first task entry via `detail::registerSelf`.
    detail::t_workerId = 0;
    detail::t_workerRng.seed(0u);
    detail::t_registered = true;
    std::snprintf(detail::t_workerName, sizeof(detail::t_workerName), "main");

    g_jobManager = this;
    IRE_LOG_INFO("JobManager: started with {} worker threads", m_workerCount);
}

JobManager::~JobManager() {
    // WaitforAllAndShutdown drains the work queue then joins every
    // worker. Must run before clearing g_jobManager so any task still
    // in flight observes a valid manager pointer.
    if (m_scheduler) {
        m_scheduler->WaitforAllAndShutdown();
    }
    if (g_jobManager == this) {
        g_jobManager = nullptr;
    }
}

bool JobManager::isMainThread() const {
    return std::this_thread::get_id() == m_mainThreadId;
}

int JobManager::workerId() const {
    if (isMainThread()) {
        return 0;
    }
    return detail::t_workerId;
}

} // namespace IRJobs
