// IRRender::detail::GpuReadbackRing hands a slot to the reader only once the
// submission that encoded its copy has completed. On Metal every compact of a
// frame shares one uncommitted command buffer, so a slot filled several
// compacts ago in the same frame must still read as pending; a CPU read of it
// would see bytes the GPU has not written.

#include <gtest/gtest.h>

#include <irreden/render/gpu_readback_ring.hpp>
#include <irreden/render/presented_pick.hpp>

#include <cstdint>
#include <vector>

namespace {

using IRRender::detail::GpuReadbackRing;

struct Read {
    int slot_;
    int producer_;
};

template <int kMaxSlots>
std::vector<Read> drain(GpuReadbackRing<int, kMaxSlots> &ring, std::uint64_t completed) {
    std::vector<Read> reads;
    ring.drainCompleted(completed, [&reads](int slot, const int &producer) {
        reads.push_back({slot, producer});
    });
    return reads;
}

TEST(GpuReadbackRing, SameSubmissionSlotsStayPendingUntilItCompletes) {
    // Three voxel-pool canvases compact within one Metal frame: submission 1
    // is recording, nothing has completed.
    GpuReadbackRing<int, 8> ring;
    EXPECT_EQ(ring.write(1, 10), 0);
    EXPECT_EQ(ring.write(1, 11), 1);
    EXPECT_EQ(ring.write(1, 12), 2);

    EXPECT_TRUE(drain(ring, 0).empty());

    const std::vector<Read> reads = drain(ring, 1);
    ASSERT_EQ(reads.size(), 3u);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(reads[i].slot_, i);
        EXPECT_EQ(reads[i].producer_, 10 + i);
    }
}

TEST(GpuReadbackRing, EachCompletedSlotIsReadOnce) {
    GpuReadbackRing<int, 4> ring;
    ring.write(1, 10);
    EXPECT_EQ(drain(ring, 1).size(), 1u);
    EXPECT_TRUE(drain(ring, 1).empty());
    EXPECT_TRUE(drain(ring, 5).empty());
}

TEST(GpuReadbackRing, PartialCompletionReleasesOnlyFinishedSubmissions) {
    GpuReadbackRing<int, 4> ring;
    ring.write(1, 10);
    ring.write(1, 11);
    ring.write(2, 20);

    const std::vector<Read> first = drain(ring, 1);
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0].producer_, 10);
    EXPECT_EQ(first[1].producer_, 11);

    const std::vector<Read> second = drain(ring, 2);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0].producer_, 20);
}

TEST(GpuReadbackRing, GrowsToTheWritesInFlightThenReusesDrainedSlots) {
    // Every frame compacts five canvases and the previous frame's submission
    // completes before the next frame's first compact drains: the ring settles
    // at five slots and records every compact.
    GpuReadbackRing<int, 64> ring;
    int reads = 0;
    for (std::uint64_t frame = 1; frame <= 4; ++frame) {
        for (int canvas = 0; canvas < 5; ++canvas) {
            reads += static_cast<int>(drain(ring, frame - 1).size());
            ring.write(frame, canvas);
        }
    }
    reads += static_cast<int>(drain(ring, 4).size());
    EXPECT_EQ(ring.slotCount(), 5);
    EXPECT_EQ(reads, 20);
}

TEST(GpuReadbackRing, AtCapacityTheOldestPendingSampleIsDropped) {
    GpuReadbackRing<int, 3> ring;
    ring.write(1, 10);
    ring.write(2, 20);
    ring.write(3, 30);
    EXPECT_EQ(ring.write(4, 40), 0);
    EXPECT_EQ(ring.slotCount(), 3);

    const std::vector<Read> afterThree = drain(ring, 3);
    ASSERT_EQ(afterThree.size(), 2u);
    EXPECT_EQ(afterThree[0].producer_, 20);
    EXPECT_EQ(afterThree[1].producer_, 30);

    const std::vector<Read> afterFour = drain(ring, 4);
    ASSERT_EQ(afterFour.size(), 1u);
    EXPECT_EQ(afterFour[0].slot_, 0);
    EXPECT_EQ(afterFour[0].producer_, 40);
}

TEST(PresentedPick, NewestCompletedFrameWinsEvenWhenSlotsDrainOutOfFrameOrder) {
    IRRender::PresentedPickHistory history;
    IRRender::PresentedPickRecord frame12;
    frame12.frameId_ = 12;
    frame12.cursorPixel_ = {12, 4};
    IRRender::PresentedPickRecord frame11;
    frame11.frameId_ = 11;
    frame11.cursorPixel_ = {11, 4};

    history.accept(frame12);
    history.accept(frame11);

    ASSERT_NE(history.served(false), nullptr);
    EXPECT_EQ(history.served(false)->frameId_, 12u);
    ASSERT_NE(history.served(true), nullptr);
    EXPECT_EQ(history.served(true)->frameId_, 11u);
}

TEST(PresentedPick, HistoryKeepsTheNewestTwoCompletedFrames) {
    IRRender::PresentedPickHistory history;
    IRRender::PresentedPickRecord record;
    record.frameId_ = 3;
    history.accept(record);
    record.frameId_ = 1;
    history.accept(record);
    record.frameId_ = 2;
    history.accept(record);
    EXPECT_EQ(history.served(false)->frameId_, 3u);
    EXPECT_EQ(history.served(true)->frameId_, 2u);
    record.frameId_ = 4;
    history.accept(record);
    EXPECT_EQ(history.served(true)->frameId_, 3u);
    record.frameId_ = 1;
    history.accept(record);
    EXPECT_EQ(history.served(false)->frameId_, 4u);
    EXPECT_EQ(history.served(true)->frameId_, 3u);
}

TEST(PresentedPick, PendingSlotIsNotServedAndFullRingDropsItsOldestPendingFrame) {
    GpuReadbackRing<IRRender::PresentedPickRecord, 2> ring;
    IRRender::PresentedPickHistory history;
    IRRender::PresentedPickRecord frame1;
    frame1.frameId_ = 1;
    IRRender::PresentedPickRecord frame2;
    frame2.frameId_ = 2;
    IRRender::PresentedPickRecord frame3;
    frame3.frameId_ = 3;
    ring.write(1, frame1);
    ring.write(2, frame2);
    ring.write(3, frame3);

    ring.drainCompleted(1, [&history](int, const IRRender::PresentedPickRecord &record) {
        history.accept(record);
    });
    EXPECT_EQ(history.newestFrameId(), 0u);

    ring.drainCompleted(3, [&history](int, const IRRender::PresentedPickRecord &record) {
        history.accept(record);
    });
    EXPECT_EQ(history.newestFrameId(), 3u);
    ASSERT_NE(history.served(true), nullptr);
    EXPECT_EQ(history.served(true)->frameId_, 2u);
}

TEST(PresentedPick, DestroyedEntityIsFilteredWithoutDiscardingFrameOrCursorState) {
    IRRender::PresentedPickRecord record;
    record.frameId_ = 7;
    record.cursorPixel_ = {20, 30};
    record.layout_.entityId_ = {42u, 0u};

    const IRRender::MouseTrixelPick pick =
        IRRender::resolvePresentedPick(&record, IRMath::ivec2(21, 30), [](IREntity::EntityId) {
            return false;
        });

    EXPECT_EQ(pick.entity_, IREntity::kNullEntity);
    EXPECT_EQ(pick.frameId_, 7u);
    EXPECT_EQ(pick.renderedCursorPixel_, IRMath::ivec2(20, 30));
    EXPECT_TRUE(pick.stale_);
}

} // namespace
