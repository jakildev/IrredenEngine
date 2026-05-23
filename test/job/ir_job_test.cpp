#include <gtest/gtest.h>

#include <irreden/ir_job.hpp>
#include <irreden/job/job_manager.hpp>

#include <atomic>
#include <thread>
#include <vector>

namespace {

class IRJobFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        // Each test runs against a freshly-constructed JobManager so
        // worker-count assertions don't leak between tests. Two workers
        // is enough to exercise the parallel-for split without
        // depending on the host's hardware_concurrency.
        m_jobs = std::make_unique<IRJob::JobManager>(2);
    }

    void TearDown() override {
        m_jobs.reset();
    }

    std::unique_ptr<IRJob::JobManager> m_jobs;
};

} // namespace

TEST_F(IRJobFixture, WorkerCountMatchesRequested) {
    EXPECT_EQ(IRJob::workerCount(), 2);
    EXPECT_NE(g_jobManager, nullptr);
    EXPECT_EQ(g_jobManager->workerCount(), 2);
}

TEST_F(IRJobFixture, IsMainThreadReturnsTrueOnConstructingThread) {
    EXPECT_TRUE(IRJob::isMainThread());
    EXPECT_EQ(IRJob::workerId(), 0);
}

TEST_F(IRJobFixture, ParallelForCoversEntireRangeExactlyOnce) {
    constexpr int kCount = 1024;
    std::atomic<int> visits{0};
    std::vector<std::atomic<int>> hits(kCount);
    for (auto &h : hits) {
        h.store(0);
    }

    IRJob::parallelFor(0, kCount, 256, [&](int rangeBegin, int rangeEnd) {
        for (int i = rangeBegin; i < rangeEnd; ++i) {
            hits[i].fetch_add(1, std::memory_order_relaxed);
        }
        visits.fetch_add(rangeEnd - rangeBegin, std::memory_order_relaxed);
    });

    EXPECT_EQ(visits.load(), kCount);
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(hits[i].load(), 1) << "index " << i << " visited " << hits[i].load() << " times";
    }
}

TEST_F(IRJobFixture, ParallelForEmptyRangeIsNoOp) {
    int sentinel = 0;
    IRJob::parallelFor(0, 0, 1, [&](int, int) { ++sentinel; });
    EXPECT_EQ(sentinel, 0);
}

TEST_F(IRJobFixture, ParallelForRunsOnAtLeastOneWorker) {
    // Probabilistic guarantee, not a structural invariant: enkiTS
    // work-stealing allows the main thread to pump all chunks via
    // WaitforTask before either worker wakes. In practice with grain=1
    // and 2 active workers this is astronomically unlikely.
    //
    // Strategy: gate every chunk body on a barrier released only after
    // `startedCount >= 2` tasks are in-flight. A separate releaser
    // thread flips the barrier; `parallelFor` drives WaitforTask on
    // main. The union of observed worker ids must then include at
    // least one non-zero id (`1` or `2`), proving at least one chunk
    // executed off the main thread.
    std::atomic<int> startedCount{0};
    std::atomic<bool> release{false};
    std::atomic<int> workerSet{0};
    constexpr int kCount = 32;

    auto body = [&](int rangeBegin, int rangeEnd) {
        startedCount.fetch_add(1, std::memory_order_relaxed);
        while (!release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        const int bit = 1 << IRJob::workerId();
        workerSet.fetch_or(bit, std::memory_order_relaxed);
        (void)rangeBegin;
        (void)rangeEnd;
    };

    // Spawn a separate thread to flip `release` once enkiTS has fed
    // every worker — the main thread can't release itself while it's
    // blocked in `parallelFor`. With 2 workers, we expect 2 task
    // partitions to start before the main thread joins in.
    std::thread releaser([&]() {
        while (startedCount.load(std::memory_order_acquire) < 2) {
            std::this_thread::yield();
        }
        release.store(true, std::memory_order_release);
    });
    IRJob::parallelFor(0, kCount, 1, body);
    releaser.join();

    EXPECT_NE(workerSet.load() & ~1, 0)
        << "expected at least one non-main worker bit set; mask=" << workerSet.load();
}

TEST_F(IRJobFixture, RunSingleTaskBlocksUntilDone) {
    std::atomic<bool> ran{false};
    IRJob::run("test_single", [&]() {
        // Simulate a brief workload.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ran.store(true, std::memory_order_release);
    });
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
}

TEST_F(IRJobFixture, IsMainThreadReturnsFalseFromPinnedWorker) {
    // `IRJob::run` is a TaskSet that enkiTS may execute on the
    // calling thread under WaitforTask. To pin the body to a
    // specific worker (where `isMainThread()` is required to return
    // false), use `IRJob::pinTo(1, ...)`.
    std::atomic<bool> ranOnMain{true};
    std::atomic<int> observedWorkerId{-1};
    IRJob::pinTo(1, [&]() {
        ranOnMain.store(IRJob::isMainThread(), std::memory_order_relaxed);
        observedWorkerId.store(IRJob::workerId(), std::memory_order_relaxed);
    });
    EXPECT_FALSE(ranOnMain.load());
    EXPECT_EQ(observedWorkerId.load(), 1);
}

TEST_F(IRJobFixture, WorkerRngIsSeededFromWorkerId) {
    // Main thread RNG is seeded from id 0 — deterministic per run.
    std::mt19937 expected(0u);
    const auto expectedFirst = expected();
    EXPECT_EQ(IRJob::workerRng()(), expectedFirst);
}

TEST(IRJobManagerTest, ManagerClearsGlobalPointerOnDestruction) {
    {
        IRJob::JobManager local(1);
        EXPECT_EQ(g_jobManager, &local);
    }
    EXPECT_EQ(g_jobManager, nullptr);
}

TEST(IRJobManagerTest, AutoWorkerCountResolvesToAtLeastOne) {
    IRJob::JobManager local(IRJob::JobManager::kAutoWorkerCount);
    EXPECT_GE(local.workerCount(), 1);
}

TEST(IRJobManagerTest, FreeFunctionsAreSafeWithoutManager) {
    // No active JobManager. The free functions should still report
    // something sane — code that asserts on isMainThread() without an
    // active pool should not deadlock.
    ASSERT_EQ(g_jobManager, nullptr);
    EXPECT_TRUE(IRJob::isMainThread());
    EXPECT_EQ(IRJob::workerCount(), 0);
    EXPECT_EQ(IRJob::workerId(), 0);
}
