#include <gtest/gtest.h>

#include <irreden/audio/audio_monitor_ring.hpp>

#include <array>
#include <atomic>
#include <thread>

namespace {

using IRAudio::detail::AudioMonitorRing;

TEST(AudioMonitorRingTest, ConsumerFasterThanProducerZeroFillsTheMissingTail) {
    AudioMonitorRing ring{16, 2, 2, 12};
    const std::array<float, 4> input{1.0f, -1.0f, 2.0f, -2.0f};
    ASSERT_TRUE(ring.push(input.data(), 2));
    std::array<float, 8> output;
    output.fill(9.0f);

    ring.consume(output.data(), 4);

    EXPECT_EQ(output[0], 1.0f);
    EXPECT_EQ(output[1], -1.0f);
    EXPECT_EQ(output[2], 2.0f);
    EXPECT_EQ(output[3], -2.0f);
    EXPECT_EQ(output[4], 0.0f);
    EXPECT_EQ(output[7], 0.0f);
    EXPECT_EQ(ring.copiedFrames(), 2u);
    EXPECT_EQ(ring.underrunFrames(), 2u);
}

TEST(AudioMonitorRingTest, ProducerFasterThanConsumerStaysBoundedAndDropsMonitorOnlyFrames) {
    AudioMonitorRing ring{16, 1, 2, 12};
    std::array<float, 12> first{};
    std::array<float, 8> second{};
    for (std::size_t i = 0; i < first.size(); ++i) {
        first[i] = static_cast<float>(i + 1);
    }
    ASSERT_TRUE(ring.push(first.data(), first.size()));
    EXPECT_FALSE(ring.push(second.data(), second.size()));
    std::array<float, 2> output{};

    ring.consume(output.data(), output.size());

    EXPECT_EQ(output[0], 1.0f);
    EXPECT_EQ(output[1], 2.0f);
    EXPECT_EQ(ring.overrunFrames(), second.size());

    std::array<float, 4> extra{13.0f, 14.0f, 15.0f, 16.0f};
    ASSERT_TRUE(ring.push(extra.data(), extra.size()));
    ring.consume(output.data(), output.size());
    EXPECT_GT(ring.discardedFrames(), 0u);
}

TEST(AudioMonitorRingTest, ConcurrentStereoFramesRemainOrderedAndUntorn) {
    constexpr int kFrameCount = 20'000;
    AudioMonitorRing ring{32'768, 2, 1, 30'000};
    std::atomic<bool> producerDone = false;
    std::atomic<int> consumed = 0;
    std::thread producer([&] {
        for (int frame = 1; frame <= kFrameCount; ++frame) {
            const std::array<float, 2> sample{
                static_cast<float>(frame),
                static_cast<float>(-frame)
            };
            while (!ring.push(sample.data(), 1)) {
                std::this_thread::yield();
            }
        }
        producerDone = true;
    });
    std::thread consumer([&] {
        int expected = 1;
        while (!producerDone || expected <= kFrameCount) {
            std::array<float, 2> sample{};
            ring.consume(sample.data(), 1);
            if (sample[0] == 0.0f) {
                std::this_thread::yield();
                continue;
            }
            EXPECT_FLOAT_EQ(sample[0], static_cast<float>(expected));
            EXPECT_FLOAT_EQ(sample[1], static_cast<float>(-expected));
            ++expected;
            ++consumed;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumed, kFrameCount);
}

} // namespace
