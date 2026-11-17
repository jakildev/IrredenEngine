#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <cstddef>
#include <utility>
#include <vector>

// The pool's queue of position ranges owed to the GPU position buffer. It is
// bounded, coalesces repeated and overlapping notifications, keeps every
// disjoint range it can, and saturates only when the disjoint ranges outgrow
// half its bound. The upload side is PositionUploadTest in
// test/render/gpu_compute_dispatch_test.cpp.

namespace {

using IRComponents::C_VoxelPool;
using IRMath::ivec3;
using Ranges = std::vector<std::pair<std::size_t, std::size_t>>;

constexpr std::size_t kCap = C_VoxelPool::kMaxPendingPositionRanges;

// Wide enough for `kCap + 1` disjoint single-slot ranges with a gap after each.
C_VoxelPool makeWidePool() {
    return C_VoxelPool(ivec3(static_cast<int>(2 * (kCap + 1)), 1, 1));
}

Ranges coalesced(C_VoxelPool &pool) {
    pool.coalescePendingPositionRanges();
    return pool.getPendingPositionRanges();
}

class VoxelPositionQueueTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
};

TEST_F(VoxelPositionQueueTest, RepeatedWritesBetweenFlushesCostOneEntry) {
    C_VoxelPool pool(ivec3(64, 1, 1));
    for (std::size_t i = 0; i < 3 * kCap; ++i) {
        pool.queuePositionRange(8, 4);
    }
    EXPECT_FALSE(pool.positionRangesSaturated());
    EXPECT_EQ(pool.getPendingPositionRanges(), (Ranges{{8, 4}}));
}

TEST_F(VoxelPositionQueueTest, OverlappingAndTouchingWritesMergeAndGapsStayUnqueued) {
    C_VoxelPool pool(ivec3(64, 1, 1));
    pool.queuePositionRange(20, 4);
    pool.queuePositionRange(2, 4);
    pool.queuePositionRange(4, 4);  // overlaps [2, 6)
    pool.queuePositionRange(8, 2);  // touches [2, 8)
    pool.queuePositionRange(22, 1); // inside [20, 24)
    pool.queuePositionRange(40, 1);
    EXPECT_EQ(coalesced(pool), (Ranges{{2, 8}, {20, 4}, {40, 1}}));
}

// The mostly-static million's shape: one disjoint range per moving cell,
// re-queued by every catch-up update before the flush.
TEST_F(VoxelPositionQueueTest, TenThousandDisjointRangesSurviveRepeatedCatchUpWrites) {
    constexpr std::size_t kMoving = 10000;
    constexpr std::size_t kStride = 100;
    C_VoxelPool pool(ivec3(static_cast<int>(kMoving * kStride), 1, 1));
    for (int update = 0; update < 4; ++update) {
        for (std::size_t cell = 0; cell < kMoving; ++cell) {
            pool.queuePositionRange(cell * kStride, 1);
        }
    }
    ASSERT_FALSE(pool.positionRangesSaturated());
    const Ranges ranges = coalesced(pool);
    ASSERT_EQ(ranges.size(), kMoving);
    for (std::size_t cell = 0; cell < kMoving; ++cell) {
        EXPECT_EQ(ranges[cell], (std::pair<std::size_t, std::size_t>{cell * kStride, 1}))
            << "cell " << cell;
    }
}

TEST_F(VoxelPositionQueueTest, TheBoundHoldsExactlyItsDisjointRangesAndOneMoreSaturates) {
    C_VoxelPool pool = makeWidePool();
    for (std::size_t i = 0; i < kCap; ++i) {
        pool.queuePositionRange(2 * i, 1);
    }
    EXPECT_FALSE(pool.positionRangesSaturated());
    EXPECT_EQ(pool.getPendingPositionRanges().size(), kCap);

    pool.queuePositionRange(2 * kCap, 1);
    EXPECT_TRUE(pool.positionRangesSaturated());

    pool.clearPendingPositionRanges();
    EXPECT_FALSE(pool.positionRangesSaturated());
    EXPECT_TRUE(pool.getPendingPositionRanges().empty());
}

// A full queue whose entries repeat is compacted in place when the next
// range arrives, so the repeats cost no capacity and nothing saturates.
TEST_F(VoxelPositionQueueTest, AFullQueueOfRepeatsCompactsInsteadOfSaturating) {
    C_VoxelPool pool = makeWidePool();
    // Two passes over the same kCap / 2 disjoint slots, out of order so
    // the append-time merge with the last entry cannot absorb them.
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < kCap / 2; ++i) {
            pool.queuePositionRange(2 * (kCap / 2 - 1 - i), 1);
        }
    }
    ASSERT_EQ(pool.getPendingPositionRanges().size(), kCap);

    pool.queuePositionRange(2 * kCap, 1);
    EXPECT_FALSE(pool.positionRangesSaturated());
    const Ranges ranges = coalesced(pool);
    ASSERT_EQ(ranges.size(), kCap / 2 + 1);
    for (std::size_t i = 0; i < kCap / 2; ++i) {
        EXPECT_EQ(ranges[i], (std::pair<std::size_t, std::size_t>{2 * i, 1}));
    }
    EXPECT_EQ(ranges.back(), (std::pair<std::size_t, std::size_t>{2 * kCap, 1}));
}

TEST_F(VoxelPositionQueueTest, ASaturatedQueueStillInvalidatesTheCullCaches) {
    C_VoxelPool pool = makeWidePool();
    for (std::size_t i = 0; !pool.positionRangesSaturated(); ++i) {
        pool.queuePositionRange(2 * i, 1);
    }
    (void)pool.consumeCullRebuildCounters();
    pool.queuePositionRange(1, 1);
    EXPECT_EQ(pool.consumeCullRebuildCounters().writeNotifications_, 1u)
        << "the write notification is counted before saturation can drop the range";
}

} // namespace
