#include <gtest/gtest.h>

#include <irreden/ir_job.hpp>
#include <irreden/job/job_manager.hpp>
#include <irreden/job/worker_block_queue.hpp>

#include <memory>
#include <vector>

namespace {

using Queue = IRJob::WorkerBlockQueue<int>;

// Every pushed entry drains exactly once, across block boundaries and across
// executors whose current blocks end partly filled.
TEST(WorkerBlockQueueTest, ParallelPushesDrainExactlyOnce) {
    auto jobs = std::make_unique<IRJob::JobManager>(3);
    constexpr int kCount = 10007;
    Queue queue;
    queue.reset(kCount);
    IRJob::parallelFor(0, kCount, 97, [&queue](int rangeBegin, int rangeEnd) {
        for (int i = rangeBegin; i < rangeEnd; ++i) {
            queue.push(i);
        }
    });

    std::vector<int> seen(kCount, 0);
    queue.forEach([&seen](int value) { ++seen[static_cast<std::size_t>(value)]; });
    for (int i = 0; i < kCount; ++i) {
        ASSERT_EQ(seen[static_cast<std::size_t>(i)], 1) << "entry " << i;
    }
    EXPECT_EQ(queue.size(), static_cast<std::size_t>(kCount));
}

// A pushed-into subset of a population leaves the unfilled tails of each
// executor's last block out of the drain.
TEST(WorkerBlockQueueTest, PartialBlocksDrainOnlyTheirFilledPrefix) {
    auto jobs = std::make_unique<IRJob::JobManager>(3);
    constexpr int kCount = 4096;
    Queue queue;
    queue.reset(kCount);
    IRJob::parallelFor(0, kCount, 64, [&queue](int rangeBegin, int rangeEnd) {
        for (int i = rangeBegin; i < rangeEnd; ++i) {
            if (i % 7 == 0) {
                queue.push(i);
            }
        }
    });

    int drained = 0;
    queue.forEach([&drained](int value) {
        EXPECT_EQ(value % 7, 0);
        ++drained;
    });
    EXPECT_EQ(drained, (kCount + 6) / 7);
}

// `reset` empties the queue; storage is reused, so a smaller frame after a
// larger one drains only its own entries.
TEST(WorkerBlockQueueTest, ResetStartsAnEmptyFrame) {
    Queue queue;
    queue.reset(200);
    for (int i = 0; i < 150; ++i) {
        queue.push(i);
    }
    EXPECT_EQ(queue.size(), 150u);

    queue.reset(200);
    EXPECT_EQ(queue.size(), 0u);
    queue.push(42);
    std::vector<int> drained;
    queue.forEach([&drained](int value) { drained.push_back(value); });
    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0], 42);
}

} // namespace
