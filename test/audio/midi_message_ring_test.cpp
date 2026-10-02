#include <gtest/gtest.h>

#include <irreden/audio/midi_message_ring.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

namespace {

using namespace IRAudio;
using IRComponents::C_MidiMessage;

constexpr std::size_t kMessageCount = 100000;

C_MidiMessage sequenceMessage(std::size_t sequence) {
    return C_MidiMessage{
        static_cast<unsigned char>(sequence & 0xFF),
        static_cast<unsigned char>((sequence >> 8) & 0xFF),
        static_cast<unsigned char>((sequence >> 16) & 0xFF)
    };
}

void expectSequence(const C_MidiMessage &message, std::size_t sequence) {
    EXPECT_EQ(message.status_, static_cast<unsigned char>(sequence & 0xFF));
    EXPECT_EQ(message.data1_, static_cast<unsigned char>((sequence >> 8) & 0xFF));
    EXPECT_EQ(message.data2_, static_cast<unsigned char>((sequence >> 16) & 0xFF));
}

TEST(MidiMessageRingTest, PreservesCountAndOrderAcrossThreads) {
    MidiMessageRing ring;
    std::atomic<bool> stop{false};
    std::vector<C_MidiMessage> received;
    received.reserve(kMessageCount);

    std::thread producer([&ring, &stop]() {
        for (std::size_t sequence = 0; sequence < kMessageCount; ++sequence) {
            const C_MidiMessage message = sequenceMessage(sequence);
            while (!ring.tryPush(message)) {
                if (stop.load(std::memory_order_relaxed)) {
                    return;
                }
                std::this_thread::yield();
            }
        }
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (received.size() < kMessageCount && std::chrono::steady_clock::now() < deadline) {
        ring.drain([&received](const C_MidiMessage &message) { received.push_back(message); });
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_relaxed);
    producer.join();

    ASSERT_EQ(received.size(), kMessageCount);
    for (std::size_t sequence = 0; sequence < received.size(); ++sequence) {
        expectSequence(received[sequence], sequence);
    }
    EXPECT_EQ(ring.takeDroppedCount(), 0u);
}

TEST(MidiMessageRingTest, DropsNewestMessagesAndCountsOverflow) {
    MidiMessageRing ring;
    constexpr std::size_t kExtraMessages = 37;
    for (std::size_t sequence = 0; sequence < kMidiMessageRingCapacity + kExtraMessages;
         ++sequence) {
        ring.push(sequenceMessage(sequence));
    }

    std::vector<C_MidiMessage> received;
    ring.drain([&received](const C_MidiMessage &message) { received.push_back(message); });

    ASSERT_EQ(received.size(), kMidiMessageRingCapacity);
    for (std::size_t sequence = 0; sequence < received.size(); ++sequence) {
        expectSequence(received[sequence], sequence);
    }
    EXPECT_EQ(ring.takeDroppedCount(), kExtraMessages);
    EXPECT_EQ(ring.takeDroppedCount(), 0u);
}

} // namespace
