#include <gtest/gtest.h>

#include <irreden/ir_job.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/job/job_manager.hpp>

#include <atomic>
#include <mutex>
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

TEST_F(IRJobFixture, IRMathThreadRngSharesStorageWithIRJobWorkerRng) {
    // Single source of truth: IRJob::workerRng() is a thin forwarder
    // over IRMath::threadRng(). Mutating one must be observed by the
    // other on the same thread.
    IRMath::seedThreadRng(123u);
    std::mt19937 expected(123u);
    EXPECT_EQ(IRJob::workerRng()(), expected());
    EXPECT_EQ(IRMath::threadRng()(), expected());
}

TEST_F(IRJobFixture, IRMathRandomFromWorkersDoesNotRace) {
    // Mirror the barrier strategy in `ParallelForRunsOnAtLeastOneWorker`
    // to force at least 2 chunks to be in-flight before any body runs;
    // otherwise enkiTS' work-stealing can pump every chunk on the main
    // thread under `WaitforTask` and the "worker actually ran on a
    // worker" property is left to luck. With that barrier in place we
    // can assert the parallel-safety property directly: per-worker RNG
    // state is isolated (no torn ints, no out-of-range values, no
    // crashes) and at least one non-main worker contributed samples.
    constexpr int kChunkCount = 32;
    constexpr int kSamplesPerChunk = 16;
    std::vector<int> samples(kChunkCount * kSamplesPerChunk, -1);
    std::atomic<int> workerBitmask{0};
    std::atomic<int> startedCount{0};
    std::atomic<bool> release{false};

    auto body = [&](int rangeBegin, int rangeEnd) {
        startedCount.fetch_add(1, std::memory_order_relaxed);
        while (!release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        const int worker = IRJob::workerId();
        workerBitmask.fetch_or(1 << worker, std::memory_order_relaxed);
        for (int chunk = rangeBegin; chunk < rangeEnd; ++chunk) {
            for (int i = 0; i < kSamplesPerChunk; ++i) {
                samples[chunk * kSamplesPerChunk + i] = IRMath::randomInt(0, 1000);
            }
        }
    };

    std::thread releaser([&]() {
        while (startedCount.load(std::memory_order_acquire) < 2) {
            std::this_thread::yield();
        }
        release.store(true, std::memory_order_release);
    });
    IRJob::parallelFor(0, kChunkCount, 1, body);
    releaser.join();

    for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
        EXPECT_GE(samples[i], 0) << "sample " << i << " below range";
        EXPECT_LE(samples[i], 1000) << "sample " << i << " above range";
    }
    EXPECT_NE(workerBitmask.load() & ~1, 0)
        << "expected at least one non-main worker bit set; mask=" << workerBitmask.load();
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

// ---------------------------------------------------------------------------
// parallelForAutoGrain (#1900) — flat auto-grain + serial fallback.
// ---------------------------------------------------------------------------

TEST_F(IRJobFixture, AutoGrainParallelCoversRangeExactlyOnce) {
    // Above the default minItemsToParallelize (4096) -> fans out. Each
    // item must be visited exactly once across all worker partitions.
    constexpr int kCount = 10000;
    std::vector<std::atomic<int>> hits(kCount);
    for (auto &h : hits) {
        h.store(0);
    }
    std::atomic<int> totalVisits{0};

    IRJob::parallelForAutoGrain(kCount, [&](int begin, int end) {
        for (int i = begin; i < end; ++i) {
            hits[i].fetch_add(1, std::memory_order_relaxed);
        }
        totalVisits.fetch_add(end - begin, std::memory_order_relaxed);
    });

    EXPECT_EQ(totalVisits.load(), kCount);
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(hits[i].load(), 1) << "index " << i << " visited " << hits[i].load() << " times";
    }
}

TEST_F(IRJobFixture, AutoGrainBelowThresholdRunsSerialSingleCall) {
    // Below threshold with a live pool -> serial: one fn(0, total) call
    // on the calling thread, no fan-out.
    constexpr int kCount = 100;
    int calls = 0;
    int seenBegin = -1;
    int seenEnd = -1;
    IRJob::parallelForAutoGrain(kCount, [&](int begin, int end) {
        ++calls;
        seenBegin = begin;
        seenEnd = end;
    });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seenBegin, 0);
    EXPECT_EQ(seenEnd, kCount);
}

TEST_F(IRJobFixture, AutoGrainZeroAndNegativeAreNoOps) {
    int calls = 0;
    IRJob::parallelForAutoGrain(0, [&](int, int) { ++calls; });
    IRJob::parallelForAutoGrain(-5, [&](int, int) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(IRJobAutoGrainNoPool, RunsSerialWithoutManager) {
    // No pool — even above the parallel threshold the work must run
    // serially as a single fn(0, total) call rather than asserting.
    ASSERT_EQ(g_jobManager, nullptr);
    int calls = 0;
    int seenEnd = -1;
    IRJob::parallelForAutoGrain(10000, [&](int begin, int end) {
        ++calls;
        seenEnd = end;
        EXPECT_EQ(begin, 0);
    });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seenEnd, 10000);
}

// ---------------------------------------------------------------------------
// parallelChunks (#1900) — row-range planner + serial fallback.
// ---------------------------------------------------------------------------

TEST_F(IRJobFixture, ChunksSingleLargeNodeSplitsAndCoversExactlyOnce) {
    // One node of 10000 rows: under minNodes (8) but over
    // minItemsToParallelize (4096) -> the intra-node path splits it into
    // several row-range chunks, covering every row exactly once.
    constexpr int kLen = 10000;
    std::vector<int> nodeLengths{kLen};
    std::vector<IRJob::RowChunk> scratch;
    std::vector<std::atomic<int>> hits(kLen);
    for (auto &h : hits) {
        h.store(0);
    }
    std::atomic<int> chunkCount{0};

    IRJob::parallelChunks(nodeLengths, scratch, [&](int nodeIndex, int rowBegin, int rowEnd) {
        EXPECT_EQ(nodeIndex, 0);
        chunkCount.fetch_add(1, std::memory_order_relaxed);
        for (int i = rowBegin; i < rowEnd; ++i) {
            hits[i].fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (int i = 0; i < kLen; ++i) {
        EXPECT_EQ(hits[i].load(), 1) << "row " << i << " visited " << hits[i].load() << " times";
    }
    EXPECT_GT(chunkCount.load(), 1)
        << "a dominant node should split into multiple row-range chunks";
}

TEST_F(IRJobFixture, ChunksManySmallNodesEachStayWhole) {
    // 10 nodes (>= minNodes 8) of 100 rows each: the inter-node path
    // fans out, but each node is far below minChunk (2048) so it stays
    // whole — exactly one chunk per node, full range.
    constexpr int kNodes = 10;
    constexpr int kLen = 100;
    std::vector<int> nodeLengths(kNodes, kLen);
    std::vector<IRJob::RowChunk> scratch;

    std::mutex mu;
    std::vector<int> chunksPerNode(kNodes, 0);
    std::vector<std::vector<int>> rowHits(kNodes, std::vector<int>(kLen, 0));

    IRJob::parallelChunks(nodeLengths, scratch, [&](int nodeIndex, int rowBegin, int rowEnd) {
        std::lock_guard<std::mutex> lk(mu);
        ++chunksPerNode[nodeIndex];
        for (int i = rowBegin; i < rowEnd; ++i) {
            ++rowHits[nodeIndex][i];
        }
    });

    for (int n = 0; n < kNodes; ++n) {
        EXPECT_EQ(chunksPerNode[n], 1) << "small node " << n << " should stay whole (one chunk)";
        for (int i = 0; i < kLen; ++i) {
            EXPECT_EQ(rowHits[n][i], 1) << "node " << n << " row " << i;
        }
    }
}

TEST_F(IRJobFixture, ChunksBelowBothThresholdsRunSerialOneCallPerNode) {
    // 2 nodes (< minNodes 8) of 100 rows (200 total < minItems 4096) ->
    // serial: fn(i, 0, len) per node on the calling thread.
    std::vector<int> nodeLengths{100, 100};
    std::vector<IRJob::RowChunk> scratch;
    std::vector<int> chunksPerNode(2, 0);
    std::vector<std::vector<int>> rowHits(2, std::vector<int>(100, 0));

    IRJob::parallelChunks(nodeLengths, scratch, [&](int nodeIndex, int rowBegin, int rowEnd) {
        ++chunksPerNode[nodeIndex]; // serial path -> main thread, no lock needed
        for (int i = rowBegin; i < rowEnd; ++i) {
            ++rowHits[nodeIndex][i];
        }
    });

    for (int n = 0; n < 2; ++n) {
        EXPECT_EQ(chunksPerNode[n], 1);
        for (int i = 0; i < 100; ++i) {
            EXPECT_EQ(rowHits[n][i], 1);
        }
    }
}

TEST_F(IRJobFixture, ChunksEmptyNodeListIsNoOp) {
    std::vector<int> nodeLengths;
    std::vector<IRJob::RowChunk> scratch;
    int calls = 0;
    IRJob::parallelChunks(nodeLengths, scratch, [&](int, int, int) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(IRJobChunksNoPool, RunsSerialWithoutManager) {
    // No pool — even with a row total above the parallel threshold, the
    // planner must run serially: one fn(node, 0, len) call per node.
    ASSERT_EQ(g_jobManager, nullptr);
    std::vector<int> nodeLengths{10000};
    std::vector<IRJob::RowChunk> scratch;
    int calls = 0;
    int seenEnd = -1;
    IRJob::parallelChunks(nodeLengths, scratch, [&](int nodeIndex, int rowBegin, int rowEnd) {
        ++calls;
        seenEnd = rowEnd;
        EXPECT_EQ(nodeIndex, 0);
        EXPECT_EQ(rowBegin, 0);
    });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seenEnd, 10000);
}
